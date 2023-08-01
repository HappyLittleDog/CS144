#include "reassembler.hh"

using namespace std;

Reassembler::Buf_Record Reassembler::merge_record( const Reassembler::Buf_Record& r1,
                                                   const Reassembler::Buf_Record& r2 )
{
  assert( r1.index_ <= r2.index_ && r2.index_ <= r1.index_ + r1.len_ );

  if ( r2.index_ + r2.len_ <= r1.index_ + r1.len_ )
    return r1;

  string cur = r1.bytes_;
  cur = cur + r2.bytes_.substr( r1.index_ + r1.len_ - r2.index_ );
  return Reassembler::Buf_Record( r1.index_, cur );
}

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring, Writer& output )
{
  if ( first_index >= buf_beginning_ + output.available_capacity() )
    return;
  else if ( first_index < static_cast<uint64_t>( buf_beginning_ ) ) {
    if ( first_index + data.length() <= static_cast<uint64_t>( buf_beginning_ ) )
      return;
    data = data.substr( buf_beginning_ - first_index );
    first_index = buf_beginning_;
  }

  if ( output.available_capacity() < first_index - buf_beginning_ + data.length() ) {
    data = data.substr( 0, output.available_capacity() - first_index + buf_beginning_ );
  } else if ( is_last_substring ) {
    ended_ = true;
  }

  Buf_Record cur( first_index, data );
  if ( cur.index_ == buf_beginning_ ) {
    while ( !buf_.empty() ) {
      auto it = buf_.begin();
      if ( it->index_ <= cur.index_ + cur.len_ ) {
        cur = merge_record( cur, *it );
        buf_.erase( it );
      } else
        break;
    }
    output.push( cur.bytes_ );
    buf_beginning_ += cur.len_;

    if ( buf_.empty() && ended_ )
      output.close();
  } else {
    auto it = buf_.lower_bound( cur );
    while ( it != buf_.end() && it->index_ <= cur.index_ + cur.len_ ) {
      cur = merge_record( cur, *it );
      it = buf_.erase( it );
    }

    if ( !buf_.empty() && it != buf_.begin() ) {
      it--;
      if ( cur.index_ <= it->index_ + it->len_ ) {
        cur = merge_record( *it, cur );
        buf_.erase( it );
      }
    }

    buf_.insert( cur );
  }
}

uint64_t Reassembler::bytes_pending() const
{
  uint64_t cnt = 0;
  for ( const auto& t : buf_ )
    cnt += t.len_;
  return cnt;
}
