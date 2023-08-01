#include <stdexcept>

#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ) {}

void Writer::push( string data )
{
  if ( buf.length() + data.length() <= capacity_ ) {
    pushed_len += data.length();
    buf = buf + data;
  } else {
    pushed_len += ( capacity_ - buf.length() );
    buf = buf + data.substr( 0, capacity_ - buf.length() );
  }
}

void Writer::close()
{
  ended_ = true;
}

void Writer::set_error()
{
  err_ = true;
}

bool Writer::is_closed() const
{
  return ended_;
}

uint64_t Writer::available_capacity() const
{
  return capacity_ - buf.length();
}

uint64_t Writer::bytes_pushed() const
{
  return pushed_len;
}

string_view Reader::peek() const
{
  return buf;
}

bool Reader::is_finished() const
{
  return ended_ && buf.empty();
}

bool Reader::has_error() const
{
  return err_;
}

void Reader::pop( uint64_t len )
{
  if ( len < buf.length() ) {
    buf = buf.substr( len );
    poped_len += len;
  } else {
    poped_len += buf.length();
    buf.clear();
  }
}

uint64_t Reader::bytes_buffered() const
{
  return buf.length();
}

uint64_t Reader::bytes_popped() const
{
  return poped_len;
}
