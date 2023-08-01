#include "file_descriptor.hh"

#include "exception.hh"

#include <algorithm>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

using namespace std;

template<typename T>
T FileDescriptor::FDWrapper::CheckSystemCall( string_view s_attempt, T return_value ) const
{
  if ( return_value >= 0 ) {
    return return_value;
  }

  if ( non_blocking_ and ( errno == EAGAIN or errno == EINPROGRESS ) ) {
    return 0;
  }

  throw unix_error { s_attempt };
}

template<typename T>
T FileDescriptor::CheckSystemCall( std::string_view s_attempt, T return_value ) const
{
  if ( not internal_fd_ ) {
    throw runtime_error( "internal error: missing internal_fd_" );
  }
  return internal_fd_->CheckSystemCall( s_attempt, return_value );
}

// fd is the file descriptor number returned by [open(2)](\ref man2::open) or similar
FileDescriptor::FDWrapper::FDWrapper( int fd ) : fd_( fd )
{
  if ( fd < 0 ) {
    throw runtime_error( "invalid fd number:" + to_string( fd ) );
  }

  const int flags = CheckSystemCall( "fcntl", fcntl( fd, F_GETFL ) ); // NOLINT(*-vararg)
  non_blocking_ = flags & O_NONBLOCK;                                 // NOLINT(*-bitwise)
}

void FileDescriptor::FDWrapper::close()
{
  CheckSystemCall( "close", ::close( fd_ ) );
  eof_ = closed_ = true;
}

FileDescriptor::FDWrapper::~FDWrapper()
{
  try {
    if ( closed_ ) {
      return;
    }
    close();
  } catch ( const exception& e ) {
    // don't throw an exception from the destructor
    cerr << "Exception destructing FDWrapper: " << e.what() << endl;
  }
}

// fd is the file descriptor number returned by [open(2)](\ref man2::open) or similar
FileDescriptor::FileDescriptor( int fd ) : internal_fd_( make_shared<FDWrapper>( fd ) ) {}

// Private constructor used by duplicate()
FileDescriptor::FileDescriptor( shared_ptr<FDWrapper> other_shared_ptr ) : internal_fd_( move( other_shared_ptr ) )
{}

// returns a copy of this FileDescriptor
FileDescriptor FileDescriptor::duplicate() const
{
  return FileDescriptor { internal_fd_ };
}

// buffer is the string to be read into
void FileDescriptor::read( string& buffer )
{
  buffer.clear();
  buffer.resize( kReadBufferSize );

  const ssize_t bytes_read = ::read( fd_num(), buffer.data(), buffer.size() );
  if ( bytes_read < 0 ) {
    if ( internal_fd_->non_blocking_ and ( errno == EAGAIN or errno == EINPROGRESS ) ) {
      return;
    }
    throw unix_error { "read" };
  }

  register_read();

  if ( bytes_read == 0 ) {
    internal_fd_->eof_ = true;
  }

  if ( bytes_read > static_cast<ssize_t>( buffer.size() ) ) {
    throw runtime_error( "read() read more than requested" );
  }

  buffer.resize( bytes_read );
}

void FileDescriptor::rio_read( string& buffer )
{
  buffer.clear();
  int bytes_read = 0;

  while ( true ) {
    string tpbuf;
    try {
      read( tpbuf );
      if ( internal_fd_->eof_ == true ) {
        break;
      }
      bytes_read += tpbuf.size();
      buffer = buffer + tpbuf;
    } catch ( const unix_error& err ) {
      throw err;
    } catch ( const runtime_error& err ) {
      buffer = buffer + tpbuf;
      throw runtime_error( "rio_read() read more than requested" );
    }
  } // end of while loop

  buffer.resize( bytes_read );
}

void FileDescriptor::read( vector<unique_ptr<string>>& buffers )
{
  if ( buffers.empty() ) {
    return;
  }

  buffers.back()->clear();
  buffers.back()->resize( kReadBufferSize );

  vector<iovec> iovecs;
  iovecs.reserve( buffers.size() );
  size_t total_size = 0;
  for ( const auto& x : buffers ) {
    iovecs.push_back( { const_cast<char*>( x->data() ), x->size() } ); // NOLINT(*-const-cast)
    total_size += x->size();
  }

  const ssize_t bytes_read = ::readv( fd_num(), iovecs.data(), static_cast<int>( iovecs.size() ) );
  if ( bytes_read < 0 ) {
    if ( internal_fd_->non_blocking_ and ( errno == EAGAIN or errno == EINPROGRESS ) ) {
      return;
    }
    throw unix_error { "read" };
  }

  register_read();

  if ( bytes_read > static_cast<ssize_t>( total_size ) ) {
    throw runtime_error( "read() read more than requested" );
  }

  size_t remaining_size = bytes_read;
  for ( auto& buf : buffers ) {
    if ( remaining_size <= buf->size() ) {
      remaining_size -= buf->size();
    } else if ( remaining_size == 0 ) {
      buf->clear();
    } else {
      buf->resize( remaining_size );
      remaining_size = 0;
    }
  }
}

size_t FileDescriptor::write( string_view buffer )
{
  return write( vector<string_view> { buffer } );
}

size_t FileDescriptor::rio_write( string_view buffer )
{
  int len = buffer.size();
  int written_bytes = 0;
  string cur_buf( buffer );

  while ( written_bytes < len ) {
    int cur_write = write( cur_buf );
    written_bytes += cur_write;
    if ( written_bytes == len )
      break;
    cur_buf = cur_buf.substr( cur_write );
  }

  if ( written_bytes > len )
    throw runtime_error( "rio_write::write wrote more than length of input buffer" );
  return len;
}

size_t FileDescriptor::write( const vector<string_view>& buffers )
{
  vector<iovec> iovecs;
  iovecs.reserve( buffers.size() );
  size_t total_size = 0;
  for ( const auto x : buffers ) {
    iovecs.push_back( { const_cast<char*>( x.data() ), x.size() } ); // NOLINT(*-const-cast)
    total_size += x.size();
  }

  const ssize_t bytes_written
    = CheckSystemCall( "writev", ::writev( fd_num(), iovecs.data(), static_cast<int>( iovecs.size() ) ) );
  register_write();

  if ( bytes_written == 0 and total_size != 0 ) {
    throw runtime_error( "write returned 0 given non-empty input buffer" );
  }

  if ( bytes_written > static_cast<ssize_t>( total_size ) ) {
    throw runtime_error( "write wrote more than length of input buffer" );
  }

  return bytes_written;
}

size_t FileDescriptor::rio_write( const vector<string_view>& buffers )
{
  string cur_buf;
  for ( const auto x : buffers ) {
    cur_buf = cur_buf + string( x );
  }
  return rio_write( cur_buf );
}

void FileDescriptor::set_blocking( bool blocking )
{
  int flags = CheckSystemCall( "fcntl", fcntl( fd_num(), F_GETFL ) ); // NOLINT(*-vararg)
  if ( blocking ) {
    flags ^= ( flags & O_NONBLOCK ); // NOLINT(*-bitwise)
  } else {
    flags |= O_NONBLOCK; // NOLINT(*-bitwise)
  }

  CheckSystemCall( "fcntl", fcntl( fd_num(), F_SETFL, flags ) ); // NOLINT(*-vararg)

  internal_fd_->non_blocking_ = not blocking;
}
