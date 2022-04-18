#ifndef __rdbparser__rdb_encode_h__
#define __rdbparser__rdb_encode_h__

#include <rdbparser/rdb_decode.h>

#ifdef __cplusplus
namespace rdbparser {

struct RdbLenEncode {
  RdbLength::LengthEnc lcode;
  union {
    uint64_t len;
    int64_t  ival;
  };

  void init( void ) {
    this->lcode = RdbLength::RDB_LEN_ERR;
    this->len   = 0;
  }
  size_t len_size( size_t x ) {
    this->len   = x;
    this->lcode = RdbLength::str_code( x );
    return RdbLength::len_size( this->lcode );
  }
  size_t str_size( size_t x ) {
    return this->len_size( x ) + x;
  }
  size_t int_size( int64_t x ) {
    this->ival  = x;
    this->lcode = RdbLength::int_code( x );
    return RdbLength::len_size( this->lcode );
  }
  size_t len_encode( void *p,  uint64_t x ) {
    this->len = x;
    this->lcode = RdbLength::str_code( x );
    return this->len_encode( p );
  }
  size_t len_encode( void *p ) {
    uint8_t *b = (uint8_t *) p;
    switch ( this->lcode ) {
      case RdbLength::RDB_LEN_6:
        b[ 0 ] = (uint8_t) this->len;
        return 1;

      case RdbLength::RDB_LEN_14:
        b[ 0 ] = 0x40U | (uint8_t) ( this->len >> 8 );
        b[ 1 ] = (uint8_t) ( this->len & 0xffU );
        return 2;

      case RdbLength::RDB_LEN_32:
        b[ 0 ] = 0x80U;
        be<uint32_t>( &b[ 1 ], (uint32_t) this->len );
        return 5;

      case RdbLength::RDB_LEN_64:
        b[ 0 ] = 0x81U;
        be<uint64_t>( &b[ 1 ], this->len );
        return 9;

      default:
        return 0;
    }
  }
  size_t str_encode( void *p,  const void *data ) {
    uint8_t *b = (uint8_t *) p;
    size_t sz = this->len_encode( b );
    ::memcpy( &b[ sz ], data, this->len );
    return sz + this->len;
  }
  size_t str_encode( void *p,  const void *data, const void *data2,
                     size_t sz,  size_t sz2 ) {
    uint8_t *b = (uint8_t *) p;
    size_t off = this->len_encode( p, sz + sz2  );
    ::memcpy( &b[ off ], data, sz );
    if ( sz2 > 0 )
      ::memcpy( &b[ off + sz ], data2, sz2 );
    return off + this->len;
  }
  size_t int_encode( void *p ) {
    uint8_t *b = (uint8_t *) p;
    switch ( this->lcode ) {
      case RdbLength::RDB_INT8:
        b[ 0 ] = 0xc0U;
        b[ 1 ] = (uint8_t) this->ival;
        return 2;

      case RdbLength::RDB_INT16:
        b[ 0 ] = 0xc1U;
        le<uint16_t>( &b[ 1 ], (uint16_t) this->ival );
        return 3;

      case RdbLength::RDB_INT32:
        b[ 0 ] = 0xc2U;
        le<uint32_t>( &b[ 1 ], (uint32_t) this->ival );
        return 5;

      default: 
        return 0;
    }
  }
};

struct RdbZipEncode {
  /* [ prev ] [ next ] [ data ] */
  RdbZipList::ZipLinkEnc next,
                         prev;
  uint32_t               link_sz,
                         off,
                         prev_off;
  void                 * p;

  void init( void *out = NULL ) {
    this->prev     = RdbZipList::ZIP_END;
    this->link_sz  = 0;
    this->off      = 10;/* zlbytes(4) + zltail(4) + zllen(2) */
    this->prev_off = 0;
    this->p        = out;
  }

  void calc_end( void ) {
    this->prev = RdbZipList::ZIP_END;
    this->off += 1; /* for the end */
  }

  void calc_link( uint32_t sz ) {
    this->prev    = RdbZipList::zprev_code( this->link_sz +
                      RdbZipList::zlink_size( this->prev ) );
    this->next    = RdbZipList::znext_str_code( sz );
    this->link_sz = RdbZipList::zlink_size( this->next ) + sz;
                      /* [ prev ] + [ next ] */
    this->off    += RdbZipList::zlink_size( this->prev ) + this->link_sz;
  }

  void encode_prev( uint32_t val ) {
    uint8_t * b = (uint8_t *) p;
    if ( this->prev == RdbZipList::LEN_8 )
      b[ this->off++ ] = (uint8_t) val;
    else if ( this->prev == RdbZipList::LEN_32 ) {
      b[ this->off++ ] = 0xfe;
      le<uint32_t>( &b[ this->off ], val );
      this->off += 4;
    }
    else {
      b[ this->off++ ] = 0;
    }
  }

  void encode_next( uint32_t sz ) {
    uint8_t * b = (uint8_t *) p;
    if ( this->next == RdbZipList::LEN_6 )
      b[ this->off++ ] = (uint8_t) sz;
    else if ( this->next == RdbZipList::BIG_14 ) {
      b[ this->off++ ] = 0x40 | (uint8_t) ( sz >> 8 );
      b[ this->off++ ] = (uint8_t) ( sz & 0xffU );
    }
    else if ( this->next == RdbZipList::BIG_32 ) {
      b[ this->off++ ] = 0x80;
      be<uint32_t>( &b[ this->off ], sz );
      this->off += 4;
    }
  }

  void encode_next( const void *data,  uint32_t sz ) {
    uint8_t * b = (uint8_t *) p;
    this->encode_next( sz );
    ::memcpy( &b[ this->off ], data, sz );
    this->off += sz;
  }

  void encode_next( const void *data, const void *data2,  uint32_t sz,
                    uint32_t sz2 ) {
    uint8_t * b = (uint8_t *) p;
    this->encode_next( sz + sz2 );
    ::memcpy( &b[ this->off ], data, sz );
    if ( sz2 > 0 )
      ::memcpy( &b[ this->off + sz ], data2, sz2 );
    this->off += sz + sz2;
  }

  void append_end( uint32_t count ) {
    uint8_t * b = (uint8_t *) p;
    b[ this->off++ ] = 0xff;
    le<uint32_t>( b, this->off );
    le<uint32_t>( &b[ 4 ], this->prev_off );
    le<uint16_t>( &b[ 8 ], count <= 0xffffU ? count : 0xffffU );
  }

  void append_link( const void *data, uint32_t sz ) {
    uint32_t prev_val;
    this->prev_off = this->off;
    prev_val       = this->link_sz + RdbZipList::zlink_size( this->prev );
    this->prev     = RdbZipList::zprev_code( prev_val );
    this->encode_prev( prev_val );
    this->next     = RdbZipList::znext_str_code( sz );
    this->encode_next( data, sz );
    this->link_sz  = RdbZipList::zlink_size( this->next ) + sz;
  }

  void append_link( const void *data, const void *data2,  uint32_t sz,
                    uint32_t sz2 ) {
    uint32_t prev_val;
    this->prev_off = this->off;
    prev_val       = this->link_sz + RdbZipList::zlink_size( this->prev );
    this->prev     = RdbZipList::zprev_code( prev_val );
    this->encode_prev( prev_val );
    this->next     = RdbZipList::znext_str_code( sz + sz2 );
    this->encode_next( data, data2, sz, sz2 );
    this->link_sz  = RdbZipList::zlink_size( this->next ) + sz + sz2;
  }
};

struct RdbListPackEncode {
  /* [ next ] [ data ] [ back ] [ 0xff ] */
  RdbListPack::ListPackEnc next;
  uint32_t                 link_sz,
                           off,
                           items;
  void                   * p;

  void init( void *out = NULL ) {
    this->off   = 6;/* lpbytes(4) + lplen(2) */
    this->items = 0;
    this->p     = out;
  }

  void calc_end( void ) {
    this->off += 1; /* for the end */
  }

  void calc_immediate_int( int64_t val ) {
    this->next    = RdbListPack::lpnext_imm_code( val );
    this->link_sz = RdbListPack::lp_size( this->next );
    this->off    += RdbListPack::lpback_size( this->link_sz ) + this->link_sz;
    this->items++;
  }

  void calc_link( uint32_t sz ) {
    this->next    = RdbListPack::lpnext_str_code( sz );
    this->link_sz = RdbListPack::lp_size( this->next ) + sz;
    this->off    += RdbListPack::lpback_size( this->link_sz ) + this->link_sz;
    this->items++;
  }

  void encode_back( void ) {
    uint8_t * b   = (uint8_t *) p;
    uint32_t  val = this->link_sz,
              sz  = RdbListPack::lpback_size( val );
    this->off += sz;
    for ( uint32_t i = 1; i < sz; i++ ) {
      b[ this->off - i ] = 0x80 | ( val & 0x7f );
      val >>= 7;
    }
    b[ this->off - sz ] = (uint8_t) val;
    this->items++;
  }

  void encode_immediate_int( int64_t val ) {
    uint8_t * b = (uint8_t *) p;
    switch ( this->next ) {
      default:
      case RdbListPack::LP_7BIT_UINT:
        b[ this->off++ ] = (uint8_t) val;
        break;
      case RdbListPack::LP_13BIT_INT:
        b[ this->off++ ] = 0xc0 | (uint8_t) ( ( val >> 8 ) & 0xf );
        b[ this->off++ ] = (uint8_t) ( val & 0xff );
        break;
      case RdbListPack::LP_16BIT_INT:
        b[ this->off++ ] = 0xf1;
        le<uint16_t>( &b[ this->off ], val & 0xffff );
        this->off += 2;
        break;
      case RdbListPack::LP_24BIT_INT:
        b[ this->off++ ] = 0xf2;
        b[ this->off++ ] = (uint8_t) ( ( val & 0xff0000 ) >> 16 );
        le<uint16_t>( &b[ this->off ], val & 0xffff );
        this->off += 2;
        break;
      case RdbListPack::LP_32BIT_INT:
        b[ this->off++ ] = 0xf3;
        le<uint32_t>( &b[ this->off ], val & 0xffffffffU );
        this->off += 4;
        break;
      case RdbListPack::LP_64BIT_INT:
        b[ this->off++ ] = 0xf4;
        le<uint64_t>( &b[ this->off ], (uint64_t) val );
        this->off += 8;
        break;
    }
    this->link_sz = RdbListPack::lp_size( this->next );
  }

  void encode_next( uint32_t sz ) {
    uint8_t * b = (uint8_t *) p;
    if ( this->next == RdbListPack::LP_6BIT_STR ) {
      b[ this->off++ ] = 0x80 | (uint8_t) sz;
    }
    else if ( this->next == RdbListPack::LP_12BIT_STR ) {
      b[ this->off++ ] = 0xe0 | (uint8_t) ( sz >> 8 );
      b[ this->off++ ] = (uint8_t) ( sz & 0xffU );
    }
    else if ( this->next == RdbListPack::LP_32BIT_STR ) {
      b[ this->off++ ] = 0xf0;
      be<uint32_t>( &b[ this->off ], sz );
      this->off += 4;
    }
    this->link_sz = RdbListPack::lp_size( this->next ) + sz;
  }

  void encode_next( const void *data,  uint32_t sz ) {
    uint8_t * b = (uint8_t *) p;
    this->encode_next( sz );
    ::memcpy( &b[ this->off ], data, sz );
    this->off += sz;
  }

  void encode_next( const void *data, const void *data2,  uint32_t sz,
                    uint32_t sz2 ) {
    uint8_t * b = (uint8_t *) p;
    this->encode_next( sz + sz2 );
    ::memcpy( &b[ this->off ], data, sz );
    if ( sz2 > 0 )
      ::memcpy( &b[ this->off + sz ], data2, sz2 );
    this->off += sz + sz2;
  }

  void append_end( void ) {
    uint8_t * b = (uint8_t *) p;
    b[ this->off++ ] = 0xff;
    le<uint32_t>( b, this->off );
    le<uint16_t>( &b[ 4 ], this->items <= 0xffffU ? this->items : 0xffffU );
  }

  void append_immediate_int( int64_t val ) {
    this->next = RdbListPack::lpnext_imm_code( val );
    this->encode_immediate_int( val );
    this->encode_back();
  }

  void append_link( const void *data, uint32_t sz ) {
    this->next = RdbListPack::lpnext_str_code( sz );
    this->encode_next( data, sz );
    this->encode_back();
  }

  void append_link( const void *data, const void *data2,  uint32_t sz,
                    uint32_t sz2 ) {
    this->next = RdbListPack::lpnext_str_code( sz + sz2 );
    this->encode_next( data, data2, sz, sz2 );
    this->encode_back();
  }
};

}
#endif
#endif

