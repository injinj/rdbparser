#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
extern "C" {
#include <lzf.h>
}
#include <rdbparser/rdb_decode.h>

using namespace rdbparser;

bool
RdbBufptr::decompress(  size_t zlen,  size_t len ) noexcept
{
  void         ** list;
  uint8_t       * ptr;
  const uint8_t * b;

  if ( (b = this->incr( zlen )) == NULL )
    return false;
  list = (void **) ::malloc( sizeof( void * ) + len );
  if ( list == NULL )
    return false;
  list[ 0 ] = (void *) this->alloced_mem;
  ptr = (uint8_t *) &list[ 1 ];
  if ( lzf_decompress( b, zlen, ptr, len ) == 0 )
    return false;
  /* push the unconsumed mem after len */
  if ( this->avail > 0 ) {
    this->sav        = this->buf;
    this->sav_avail  = this->avail;
    this->sav_offset = this->offset;
  }
  this->buf         = ptr;
  this->avail       = len;
  this->offset      = 0;
  this->alloced_mem = list;

  return true;
}

void
RdbBufptr::free_alloced( void ) noexcept
{
  if ( this->alloced_mem == NULL )
    return;
  while ( this->alloced_mem != NULL ) {
    void ** list = this->alloced_mem;
    this->alloced_mem = (void **) list[ 0 ];
    ::free( list );
  }
  if ( this->avail == 0 ) {
    this->buf        = this->sav;
    this->avail      = this->sav_avail;
    this->offset     = this->sav_offset;
    this->sav        = NULL;
    this->sav_avail  = 0;
    this->sav_offset = 0;
  }
}

const uint8_t *
RdbBufptr::look( size_t n ) noexcept
{
  /* pop back to the main buffer */
  if ( this->avail == 0 && this->sav_avail > 0 ) {
    this->buf        = this->sav;
    this->avail      = this->sav_avail;
    this->offset     = this->sav_offset;
    this->sav        = NULL;
    this->sav_avail  = 0;
    this->sav_offset = 0;
  }
  if ( this->avail >= n ) {
    ::memcpy( this->lookahead, this->buf, n );
  }
  else { /* overflow, error will catch on incr() */
    ::memcpy( this->lookahead, this->buf, this->avail );
    ::memset( &this->lookahead[ this->avail ], 0, n - this->avail );
  }
  return this->lookahead;
}

int
RdbLength::decode_buf( const uint8_t *b ) noexcept
{
  switch ( length_encoding( b[ 0 ] ) ) {

    case RDB_LEN_6: /* 6 bit number */
      this->len = ( b[ 0 ] & 0x3f );
      return 1;

    case RDB_LEN_14: /* 14 bit number (big endian) */
      this->len = (size_t) ( ( b[ 0 ] & 0x3f ) << 8 ) | b[ 1 ];
      return 2;

    case RDB_LEN_32: /* 32 bit */
      this->len = be<uint32_t>( &b[ 1 ] );
      return 5;

    case RDB_LEN_64: /* 64 bit */
      this->len = be<uint64_t>( &b[ 1 ] );
      return 9;

    case RDB_LZF: { /* lzf <zlen> <len> */
      if ( this->is_lzf ) /* can't recurse into lzf again */
        return -1;
      int sz, sz2;
      this->is_lzf = true;

      if ( (sz = this->decode_buf( &b[ 1 ] )) < 0 ) /* recurse, get zlen */
        return sz;
      this->zlen = this->len;
      sz2 = this->decode_buf( &b[ sz + 1 ] ); /* rec again, get uncompressed */

      if ( this->is_enc ) /* can't have immediate, must have lengths */
        return -1;
      return 1 + sz + sz2;
    }

    case RDB_INT8: /* int8 */
      this->is_enc = 1;
      this->ival = (int32_t) (int8_t) b[ 1 ];
      return 2;

    case RDB_INT16: /* int16 */
      this->is_enc = 2;
      this->ival = (int32_t) (int16_t) le<uint16_t>( &b[ 1 ] );
      return 3;

    case RDB_INT32: /* int32 */
      this->is_enc = 4;
      this->ival = (int32_t) le<uint32_t>( &b[ 1 ] );
      return 5;

    default:
      break;
  }
  return -1;
}

RdbErrCode
RdbLength::consume( RdbBufptr &bptr,  const uint8_t *&b ) const noexcept
{
  if ( this->is_enc ) /* should be data */
    return RDB_ERR_HDR;
  if ( this->is_lzf ) {
    if ( ! bptr.decompress( this->zlen, this->len ) )
      return RDB_ERR_LZF;
  }
  if ( (b = bptr.incr( this->len )) == NULL )
    return RDB_ERR_TRUNC;
  return RDB_OK;
}

/* the entry pointer is the last value, itterate to the next,
 * entry -> [ next-idx ][ data_len data ][ prev-idx ][ next-idx-2 ]
 * if returns true, then
 * entry -> [ next-idx-2 ][ data_len data 2 ] */
bool
RdbZipList::next( RdbListValue &lval ) const noexcept
{
  /* skip over last entry data */
  lval.entry     = &lval.entry[ lval.entry_len ];
  lval.entry_len = this->zlbytes; /* if test below fails, it is > end */
  lval.data      = NULL;
  lval.data_len  = 0;
  lval.ival      = 0;

  if ( lval.entry < this->end ) {
    /* skip over previous length */
    ZipLinkEnc p = zlink_prev( lval.entry[ 0 ] );
    if ( p == ZIP_END )
      return false;
    lval.entry = &lval.entry[ zlink_size( p ) ];  /* prev_len */

    /* determine what next is encoding */
    if ( lval.entry < this->end ) {
      ZipLinkEnc      n    = zlink_next( lval.entry[ 0 ] );
      uint8_t         sz   = zlink_size( n );
      const uint8_t * next = &lval.entry[ sz ];
      /* check bounds of data start */
      if ( next <= this->end ) {
        /* could be immediate or length */
        lval.ival = zlink_val( n, lval.entry );
        if ( is_immed( n ) ) { /* if is immediate integer */
          lval.entry_len = (size_t) sz;
        }
        else {                 /* is string data length */
          lval.entry_len = (size_t) sz + (size_t) lval.ival;
          lval.data      = next;
          lval.data_len  = (size_t) lval.ival;
        }
      }
    }
  }
  return &lval.entry[ lval.entry_len ] <= this->end; /* check bounds */
}

bool
RdbListPack::next( RdbListValue &lval ) const noexcept
{
  /* skip over last entry data */
  lval.entry     = &lval.entry[ lval.entry_len ];
  lval.entry_len = this->lpbytes; /* if test below fails, it is > end */
  lval.data      = NULL;
  lval.data_len  = 0;
  lval.ival      = 0;

  if ( lval.entry < this->end ) {
    /* the next code */
    ListPackEnc n = lp_code( lval.entry[ 0 ] );
    if ( n == LP_END )
      return false;

    uint8_t         sz   = lp_size( n );
    const uint8_t * next = &lval.entry[ sz ];
    if ( next <= this->end ) { /* if is immediate integer */
      lval.ival = lp_val( n, lval.entry );
      if ( is_immed( n ) ) {
        lval.entry_len = (size_t) sz;
      }
      else {                   /* is string data length */
        lval.entry_len = (size_t) sz + (size_t) lval.ival;
        lval.data      = next;
        lval.data_len  = (size_t) lval.ival;
      }
      /* the back code, add to entry_len to skip it */
      lval.entry_len += lpback_size( lval.entry_len );
    }
  }
  return &lval.entry[ lval.entry_len ] <= this->end; /* check bounds */
}

RdbErrCode
RdbDecode::decode_hdr( RdbBufptr &bptr ) noexcept
{
  RdbErrCode err;
  if ( this->out == NULL ) {
    if ( this->data_out == NULL )
      return RDB_ERR_OUTPUT;
    this->out = this->data_out;
  }
  /* must have at least version and crc */
  if ( this->ver == 0 ) {
    size_t off = 8;
    this->crc = 0;
    if ( bptr.avail < 10 ) /* min bytes for ver(2) + crc(8) */
      return RDB_ERR_TRUNC;
    /* ver from REDIS0009 */
    if ( ::memcmp( bptr.buf, "REDIS00", 7 ) == 0 ) {
      this->is_rdb_file = true;
      this->ver = 0;
      for ( size_t i = 5; i <= 8; i++ ) {
        uint8_t b = bptr.buf[ i ];
        if ( b < '0' || b > '9' )
          return RDB_ERR_VERSION;
        this->ver = ( this->ver * 10 ) + ( b - '0' );
      }
      if ( bptr.avail > 8 && bptr.buf[ bptr.avail - 9 ] == 0xff )
        this->crc = le<uint64_t>( &bptr.buf[ bptr.avail - 8 ] );
    }
    else {
      /* check rdb version */
      uint8_t v = bptr.buf[ bptr.avail - 9 ];
      this->ver = bptr.buf[ bptr.avail - 10 ];
      this->ver |= ( (uint16_t) v << 8 );
      /* eat newline, it's from redis-cli */
      if ( this->ver != 9 && bptr.buf[ bptr.avail - 1 ] == 0xa ) {
        off = 9;
        v = bptr.buf[ bptr.avail - 10 ];
        this->ver = bptr.buf[ bptr.avail - 11 ];
        this->ver |= ( (uint16_t) v << 8 );
      }
      this->crc = le<uint64_t>( &bptr.buf[ bptr.avail - off ] );
    }

      /* zero means it was not computed */
    if ( this->crc != 0 ) {
      uint64_t calc = jones_crc64( 0, bptr.buf, bptr.avail - off );
      if ( calc != this->crc ) {
        fprintf( stderr, "calc crc(0x%lx) != trail crc(0x%lx)\n",
                 calc, this->crc );
        if ( ! this->is_rdb_file )
          return RDB_ERR_CRC;
      }
    }
    if ( ! this->is_rdb_file )
      bptr.avail -= off + 2; /* eat the crc and version, no 0xff terminator */
    else
      bptr.incr( 5 + 4 );    /* eat past REDIS0009 */
  }

  if ( this->is_rdb_file ) {
    const uint8_t * b;
    int cnt;
    while ( bptr.avail > 0 ) {
      uint8_t next = bptr.buf[ 0 ];
      if ( next >= RDB_MODULE_AUX )
        bptr.incr( 1 );
      switch ( next ) {
        case RDB_MODULE_AUX:  /* 0xf7 - module2 */
          /* module specific codes */
          return RDB_ERR_NOTSUP;
        case RDB_IDLE: {      /* 0xf8 - value-type */
          RdbLength idle;
          if ( (err = idle.decode( bptr )) != RDB_OK )
            return err;
          if ( idle.is_lzf || idle.is_enc )
            return RDB_ERR_HDR;
          this->out->d_idle( idle.len );
          break;
        }
        case RDB_FREQ: {      /* 0xf9 - byte */
          if ( (b = bptr.incr( 1 )) == NULL )
            return RDB_ERR_TRUNC;
          this->out->d_freq( b[ 0 ] );
          break;
        }
        case RDB_AUX: {       /* 0xfa - string, string */
          RdbString s[ 2 ];
          for ( cnt = 0; cnt < 2; cnt++ ) {
            RdbLength aux;
            if ( (err = aux.decode( bptr )) != RDB_OK )
              return err;
            if ( aux.is_enc ) {
              s[ cnt ].set( aux.ival );
            }
            else {
              if ( (err = aux.consume( bptr, b )) != RDB_OK )
                return err;
              s[ cnt ].set( (char *) b, aux.len );
            }
          }
          this->out->d_aux( s[ 0 ], s[ 1 ] );
          break;
        }
        case RDB_DBRESIZE: {  /* 0xfb - length, length */
          uint64_t rsz[ 2 ];
          for ( cnt = 0; cnt < 2; cnt++ ) {
            RdbLength sz;
            if ( (err = sz.decode( bptr )) != RDB_OK )
              return err;
            if ( sz.is_lzf || sz.is_enc )
              return RDB_ERR_HDR;
            rsz[ cnt ] = sz.len;
          }
          this->out->d_dbresize( rsz[ 0 ], rsz[ 1 ] );
          break;
        }
        case RDB_EXPIRED_MS: {/* 0xfc - millisecond */
          uint64_t ms;
          if ( (b = bptr.incr( 8 )) == NULL )
            return RDB_ERR_TRUNC;
          ms = le<uint64_t>( b );
          this->out->d_expired_ms( ms );
          break;
        }
        case RDB_EXPIRED_SEC: { /* 0xfd - second */
          uint32_t sec;
          if ( (b = bptr.incr( 4 )) == NULL )
            return RDB_ERR_TRUNC;
          sec = le<uint32_t>( b );
          this->out->d_expired( sec );
          break;
        }
        case RDB_DBSELECT: { /* 0xfe - length */
          RdbLength sz;
          if ( (err = sz.decode( bptr )) != RDB_OK )
            return err;
          if ( sz.is_lzf || sz.is_enc )
            return RDB_ERR_HDR;
          this->out->d_dbselect( sz.len );
          break;
        }
        case RDB_EOF:
          return RDB_EOF_MARK;
        default:
          goto break_loop;
      }
    }
  break_loop:;
  }
  if ( bptr.avail == 0 )
    return RDB_ERR_TRUNC;
  /* get type end length */
  this->type = get_rdb_type( bptr.buf[ 0 ] );
  if ( this->type == RDB_BAD_TYPE )
    return RDB_ERR_TYPE;
  this->data_out->d_start_type( this->type );
  bptr.incr( 1 );

  this->rlen.zero();
  err = this->rlen.decode( bptr );
  if ( err != RDB_OK )
    return err;
  if ( this->is_rdb_file ) {
    if ( this->rlen.is_enc ) {
      this->key.set( this->rlen.ival );
    }
    else {
      const uint8_t * k;
      /* should be a key */
      if ( (err = this->rlen.consume( bptr, k )) != RDB_OK )
        return err;
      this->key.set( (const char *) k, this->rlen.len );
    }
    this->rlen.zero();
    err = this->rlen.decode( bptr );
    if ( err != RDB_OK )
      return err;
  }
  /* finally, unzip */
  if ( this->rlen.is_lzf ) {
    if ( ! bptr.decompress( this->rlen.zlen, this->rlen.len ) )
      return RDB_ERR_LZF;
  }
  return RDB_OK;
}

bool
RdbStreamEntry::read_header( RdbListPack &list,  RdbListValue &lval ) noexcept
{
  size_t sz = TMP_SIZE;
  bool b;
  b  = list.first_ival( lval ); this->items_count        = lval.ival;
  b &= list.next_ival( lval );  this->deleted_count      = lval.ival;
  b &= list.next_ival( lval );  this->master_field_count = lval.ival;
  if ( ! b )
    return false;

  this->master = this->tmp;
  if ( this->master_field_count > sz ) {
    sz = sizeof( this->master[ 0 ] ) * this->master_field_count;
    this->master = (RdbListValue *) ::malloc( sz );
  }
  for ( size_t i = 0; i < this->master_field_count; i++ ) {
    if ( ! list.next( lval ) )
      return false;
    this->master[ i ] = lval;
  }
  return list.next( lval ); /* skip the zero terminate */
}

bool
RdbStreamEntry::read_entry( RdbListPack &list,  RdbListValue &lval ) noexcept
{
  RdbListValue * fld = this->tmp;
  size_t         sz  = TMP_SIZE,
                 cnt = 0,
                 i;
  bool b;
  b  = list.next_ival( lval ); this->flags    = lval.ival;
  b &= list.next_ival( lval ); this->diff.ms  = lval.ival;
  b &= list.next_ival( lval ); this->diff.ser = lval.ival;
  if ( ! b )
    return false;
  if ( this->master == this->tmp ) {
    sz -= this->master_field_count;
    fld = &fld[ this->master_field_count ];
  }
  this->release( this->fields );
  this->release( this->values );
  this->fields = NULL;
  this->values = NULL;

  /* if not using master fields, alloc space for fields */
  if ( ( this->flags & RDB_STREAM_ENTRY_SAMEFIELDS ) == 0 ) {
    b = list.next_ival( lval ); this->entry_field_count = lval.ival;
    if ( ! b )
      return false;
    cnt = 4;
    if ( this->entry_field_count <= sz ) {
      this->fields = fld;
      sz -= this->entry_field_count;
      fld = &fld[ this->entry_field_count ];
    }
  }
  /* using the master fields */
  else {
    cnt = 3;
    this->entry_field_count = this->master_field_count;
    this->fields = this->master;
  }
  if ( this->entry_field_count <= sz )
    this->values = fld;
  sz = sizeof( this->fields[ 0 ] ) * this->entry_field_count;
  /* alloc space if needed */
  if ( this->fields == NULL )
    this->fields = (RdbListValue *) ::malloc( sz );
  if ( this->values == NULL )
    this->values = (RdbListValue *) ::malloc( sz );
  /* read the fields and the values */
  for ( i = 0; i < this->entry_field_count; i++ ) {
    if ( ( this->flags & RDB_STREAM_ENTRY_SAMEFIELDS ) == 0 ) {
      b &= list.next( lval ); 
      this->fields[ i ] = lval;
    }
    b &= list.next( lval ); 
    this->values[ i ] = lval;
  }
  cnt += i;
  if ( ( this->flags & RDB_STREAM_ENTRY_SAMEFIELDS ) == 0 )
    cnt += i;
  b &= list.next( lval ); /* skip the back count */
  if ( b ) { /* check that the field count is correct */
    if ( (size_t) lval.ival != cnt )
      return false;
  }
  return b;
}

RdbErrCode
RdbDecode::decode_body( RdbBufptr &bptr ) noexcept
{
  RdbErrCode err;

  /* decode the structures based on the type */
  switch ( this->type ) {
    case RDB_STRING: { /* a single string, the simplest structure */
      RdbString str;
      this->start_key();
      if ( (err = this->decode_str( bptr, str, this->rlen )) != RDB_OK )
        return err;
      this->out->d_string( str );
      this->out->d_end_key();
      return RDB_OK;
    }
    case RDB_HASH: { /* a sequence of strings, rlen.len is a num hash entries */
      RdbHashEntry hash;
      hash.cnt = this->rlen.len;
      this->start_key();
      for ( hash.num = 0; hash.num < hash.cnt; hash.num++ ) { /* field : val */
        if ( (err = this->decode_rlen( bptr, hash.field )) != RDB_OK ||
             (err = this->decode_rlen( bptr, hash.val )) != RDB_OK )
          return err;
        this->out->d_hash( hash );
      }
      this->out->d_end_key();
      return RDB_OK;
    }
    case RDB_HASH_ZIPMAP:
      return this->decode_hash_zipmap( bptr );

    case RDB_SET: {  /* same as above, rlen.len is a set size */
      RdbSetMember set;
      set.cnt = this->rlen.len;
      this->start_key();
      for ( set.num = 0; set.num < set.cnt; set.num++ ) { /* set member */
        if ( (err = this->decode_rlen( bptr, set.member )) != RDB_OK )
          return err;
        this->out->d_set( set );
      }
      this->out->d_end_key();
      return RDB_OK;
    }
    case RDB_LIST: { /* same as above, rlen.len is num list elems */
      RdbListElem list;
      list.cnt = this->rlen.len;
      this->start_key();
      for ( list.num = 0; list.num < list.cnt; list.num++ ) { /* list element */
        if ( (err = this->decode_rlen( bptr, list.val )) != RDB_OK )
          return err;
        this->out->d_list( list );
      }
      this->out->d_end_key();
      return RDB_OK;
    }
    case RDB_SET_INTSET: /* an array of integers, all the same size */
      return this->decode_set_intset( bptr );

    case RDB_HASH_ZIPLIST:   /* a ziplist coded list of strings, field, value */
    case RDB_ZSET_ZIPLIST:   /* a ziplist    ...               , member, score*/
    case RDB_LIST_ZIPLIST:   /* a ziplist    ...               , elements */
      return this->decode_ziplist( bptr );

    case RDB_LIST_QUICKLIST: /* an array of ziplists, may be zipped */
      return this->decode_quicklist( bptr );

    case RDB_ZSET:   /* string with score as string */
    case RDB_ZSET_2: /* string with score as float double */
      return this->decode_zset( bptr );

    case RDB_STREAM_LISTPACK:
      return this->decode_stream( bptr );

    case RDB_MODULE_2:
    case RDB_MODULE:
      return this->decode_module( bptr );

    case RDB_BAD_TYPE:
      break;
  }
  return RDB_ERR_NOTSUP;
}

RdbErrCode
RdbDecode::decode_rlen( RdbBufptr &bptr,  RdbString &str ) noexcept
{
  RdbLength  len;
  RdbErrCode err = len.decode( bptr );
  if ( err != RDB_OK )
    return err;
  if ( len.is_lzf ) {
    if ( ! bptr.decompress( len.zlen, len.len ) )
      return RDB_ERR_LZF;
  }
  return this->decode_str( bptr, str, len );
}

RdbErrCode
RdbDecode::decode_str( RdbBufptr &bptr,  RdbString &str,
                       RdbLength &len ) noexcept
{
  if ( len.is_enc )
    str.set( len.ival );
  else {
    const uint8_t * b;
    if ( (b = bptr.incr( len.len )) == NULL )
      return RDB_ERR_TRUNC;
    str.set( (const char *) b, len.len );
  }
  return RDB_OK;
}

RdbErrCode
RdbDecode::decode_hash_zipmap( RdbBufptr &bptr ) noexcept
{
  RdbHashEntry    hash;
  const uint8_t * b;

  this->start_key();
  if ( (b = bptr.incr( this->rlen.len )) == NULL )
    return RDB_ERR_TRUNC;
  RdbBufptr hptr( b, this->rlen.len );
  if ( hptr.avail > 0 )
    hash.cnt = *(hptr.incr( 1 ));
  for ( ; hptr.avail > 0; hash.num++ ) {
    uint32_t len = *(hptr.incr( 1 )); /* length of field */
    if ( len == 255 )
      break;
    if ( len >= 254 ) {               /* length extends to 32 bits */
      if ( (b = hptr.incr( 4 )) == NULL )
        return RDB_ERR_TRUNC;
      len = be<uint32_t>( b );
    }
    if ( (b = hptr.incr( len )) == NULL ) /* get field data */
      return RDB_ERR_TRUNC;
    hash.field.set( (const char *) b, len );

    if ( (b = hptr.incr( 1 )) == NULL ) /* length of value */
      return RDB_ERR_TRUNC;
    len = *b;
    if ( len >= 254 ) {
      if ( (b = hptr.incr( 4 )) == NULL ) /* length extends 32 bits */
        return RDB_ERR_TRUNC;
      len = be<uint32_t>( b );
    }
    if ( (b = hptr.incr( 1 )) == NULL ) /* free space, after val, 1 byte */
      return RDB_ERR_TRUNC;
    if ( (b = hptr.incr( len + b[ 0 ] )) == NULL ) /* get value data */
      return RDB_ERR_TRUNC;
    hash.val.set( (const char *) b, len );
    this->out->d_hash( hash );
  }
  this->out->d_end_key();

  return RDB_OK;
}

RdbErrCode
RdbDecode::decode_set_intset( RdbBufptr &bptr ) noexcept
{
  RdbSetMember    set;
  uint32_t        nbyte,
                  nelem;
  int64_t         ival = 0;
  const uint8_t * b;

  this->start_key();
  b = bptr.incr( 8 );
  if ( b == NULL )
    return RDB_ERR_TRUNC;
  nbyte = le<uint32_t>( b );
  nelem = le<uint32_t>( &b[ 4 ] );
  /* check valid int size */
  if ( nbyte != 1 && nbyte != 2 && nbyte != 4 && nbyte != 8 )
    return RDB_ERR_NOTSUP;
  if ( (b = bptr.incr( nbyte * nelem )) == NULL )
    return RDB_ERR_TRUNC;
  set.cnt = nelem;
  for ( set.num = 0; set.num < nelem; set.num++ ) {
    switch ( nbyte ) {
      case 1: ival = (int8_t) b[ 0 ]; break;
      case 2: ival = (int16_t) le<uint16_t>( b ); break;
      case 4: ival = (int32_t) le<uint32_t>( b ); break;
      case 8: ival = (int32_t) le<uint32_t>( b ); break;
    }
    b = &b[ nbyte ];
    set.member.set( ival );
    this->out->d_set( set );
  }
  this->out->d_end_key();
  return RDB_OK;
}

RdbErrCode
RdbDecode::decode_zset( RdbBufptr &bptr ) noexcept /* or zset_2 */
{
  RdbZSetMember   zset;
  const uint8_t * b;
  size_t          cnt;
  RdbErrCode      err;

  zset.cnt = this->rlen.len;
  zset.num = 0;
  this->start_key();
  for ( cnt = zset.cnt; cnt > 0; cnt-- ) {
    if ( (err = this->decode_rlen( bptr, zset.member )) != RDB_OK )
      return err;
    if ( this->type == RDB_ZSET_2 ) { /* binary double value */
      double dbl;
      if ( (b = bptr.incr( 8 )) == NULL )
        return RDB_ERR_TRUNC;
      ::memcpy( &dbl, b, 8 );
      zset.score.set( dbl );
    }
    else { /* RDB_ZSET, a string encoded float */
      if ( (b = bptr.incr( 1 )) == NULL )
        return RDB_ERR_TRUNC;
      switch ( b[ 0 ] ) {
        case 253: zset.score.set( "nan", 3 ); break;
        case 254: zset.score.set( "inf", 3 ); break;
        case 255: zset.score.set( "-inf", 4 ); break;
        default: {
          int len = b[ 0 ];
          if ( (b = bptr.incr( len )) == NULL )
            return RDB_ERR_TRUNC;
          zset.score.set( (char *) b, len );
          break;
        }
      }
    }
    this->out->d_zset( zset );
    zset.num++;
  }
  this->out->d_end_key();
  return RDB_OK;
}

static inline void
decode_lval( RdbListValue &lval,  RdbString &str )
{
  if ( lval.data != NULL )
    str.set( (char *) lval.data, lval.data_len );
  else
    str.set( lval.ival );
}

RdbErrCode
RdbDecode::decode_ziplist( RdbBufptr &bptr ) noexcept
{
  RdbZipList      zip;
  RdbListValue    lval;
  const uint8_t * b;

  this->start_key();
  /* the same structure, different outputs */
  if ( (b = bptr.incr( this->rlen.len )) == NULL ||
       ! zip.init( b, this->rlen.len ) )
    return RDB_ERR_TRUNC;
  switch ( this->type ) {
    default:
    case RDB_HASH_ZIPLIST: {   /* hash ziplist */
      RdbHashEntry hash;
      hash.cnt = zip.zllen / 2;
      if ( zip.first( lval ) ) {
        for ( hash.num = 0; ; hash.num++ ) { /* foreach field : value */
          decode_lval( lval, hash.field );
          if ( ! zip.next( lval ) )
            break;
          decode_lval( lval, hash.val );
          this->out->d_hash( hash );
          if ( ! zip.next( lval ) )
            break;
        }
      }
      break;
    }
    case RDB_ZSET_ZIPLIST: {   /* zset ziplist */
      RdbZSetMember zset;
      zset.cnt = zip.zllen / 2;
      if ( zip.first( lval ) ) {
        for ( zset.num = 0; ; zset.num++ ) { /* foreach member : score */
          decode_lval( lval, zset.member );
          if ( ! zip.next( lval ) )
            break;
          decode_lval( lval, zset.score );
          this->out->d_zset( zset );
          if ( ! zip.next( lval ) )
            break;
        }
      }
      break;
    }
    case RDB_LIST_ZIPLIST: {   /* list ziplist */
      RdbListElem list;
      list.cnt = zip.zllen;
      if ( zip.first( lval ) ) {
        for ( list.num = 0; ; list.num++ ) { /* foreach list element */
          decode_lval( lval, list.val );
          this->out->d_list( list );
          if ( ! zip.next( lval ) )
            break;
        }
      }
      break;
    }
  }
  this->out->d_end_key();
  return RDB_OK;
}

RdbErrCode
RdbDecode::decode_quicklist( RdbBufptr &bptr ) noexcept
{
  RdbListElem     list;
  const uint8_t * b;
  size_t          cnt;
  RdbErrCode      err;

  list.num = 0;
  list.cnt = 0;
  this->start_key();
  /* for each ziplist */
  for ( cnt = this->rlen.len; cnt > 0; cnt-- ) {
    RdbZipList   zip;
    RdbListValue lval;
    RdbLength    llen;
    if ( (err = llen.decode( bptr )) != RDB_OK ||
         (err = llen.consume( bptr, b )) != RDB_OK )
      return err;
    if ( ! zip.init( b, llen.len ) )
      return RDB_ERR_TRUNC;
    /* iterate the zip values */
    if ( zip.first( lval ) ) {
      for ( ; ; list.num++ ) {
        decode_lval( lval, list.val );
        this->out->d_list( list );
        if ( ! zip.next( lval ) )
          break;
      }
    }
  }
  this->out->d_end_key();
  return RDB_OK;
}

RdbErrCode
RdbDecode::decode_stream( RdbBufptr &bptr ) noexcept
{
  /* decode stream entries */
  RdbStreamEntry  entry;
  const uint8_t * b;
  size_t          cnt;
  RdbErrCode      err;

  entry.num = 0;
  entry.cnt = 0;
  this->start_key();
  /* for each list pack */
  for ( cnt = this->rlen.len; cnt > 0; cnt-- ) {
    RdbListPack  list;
    RdbListValue lval;
    RdbLength    k, lp;
    if ( (err = k.decode( bptr )) != RDB_OK ||
         (err = k.consume( bptr, b )) != RDB_OK )
      return err;
    entry.id.set( be<uint64_t>( b ),
                  be<uint64_t>( &b[ 8 ] ) );
    if ( (err = lp.decode( bptr )) != RDB_OK ||
         (err = lp.consume( bptr, b )) != RDB_OK )
      return err;
    if ( list.init( b, lp.len ) ) {
      /* header is first */
      if ( ! entry.read_header( list, lval ) )
        return RDB_ERR_HDR;
      /* multiple entries follow the header */
      for ( size_t i = 0; i < entry.items_count; i++ ) {
        if ( ! entry.read_entry( list, lval ) )
          return RDB_ERR_TRUNC;
        /* skip deleted entries */
        if ( ( entry.flags & RDB_STREAM_ENTRY_DELETED ) == 0 ) {
          if ( entry.num == 0 )
            this->out->d_stream_start( RdbOutput::STREAM_ENTRY_LIST );
          this->out->d_stream_entry( entry );
          entry.num++;
        }
      }
    }
  }
  if ( entry.num != 0 )
    this->out->d_stream_end( RdbOutput::STREAM_ENTRY_LIST );
  /* info about the stream */
  RdbLength num_elems, last_ms, last_ser, num_cgroups;

  if ( (err = num_elems.decode( bptr )) != RDB_OK ||
       (err = last_ms.decode( bptr )) != RDB_OK ||
       (err = last_ser.decode( bptr )) != RDB_OK ||
       (err = num_cgroups.decode( bptr )) != RDB_OK )
    return err;

  RdbStreamInfo str( entry.num );
  str.num_elems   = num_elems.len;
  str.last.set( last_ms.len, last_ser.len );
  str.num_cgroups = num_cgroups.len;
  this->out->d_stream_info( str );
  /* for each group */
  RdbGroupInfo group( str );
  group.cnt = num_cgroups.len;
  if ( group.cnt != 0 )
    this->out->d_stream_start( RdbOutput::STREAM_GROUP_LIST );
  for ( group.num = 0; group.num < num_cgroups.len; group.num++ ) {
    RdbLength gname, last_ms, last_ser;

    if ( (err = gname.decode( bptr )) != RDB_OK ||
         (err = gname.consume( bptr, b )) != RDB_OK )
      return RDB_OK;
    /* group, last id, pending */
    group.gname_len = gname.len;
    group.gname     = (char *) b;

    if ( (err = last_ms.decode( bptr )) != RDB_OK ||
         (err = last_ser.decode( bptr )) != RDB_OK )
      return err;
    group.last.set( last_ms.len, last_ser.len );

    RdbLength pend_cnt;
    if ( (err = pend_cnt.decode( bptr )) != RDB_OK )
      return err;

    group.pending_cnt = pend_cnt.len;
    this->out->d_stream_group( group );
    /* for each pending entry of the group */
    RdbPendInfo pend( group );
    pend.cnt = group.pending_cnt;
    if ( pend.cnt != 0 )
      this->out->d_stream_start( RdbOutput::STREAM_PENDING_LIST );
    for ( pend.num = 0; pend.num < pend.cnt; pend.num++ ) {
      RdbLength deliv;
      if ( (b = bptr.incr( 128 / 8 )) == NULL ) /* stream id */
        return RDB_ERR_TRUNC;
      pend.id.set( be<uint64_t>( b ), be<uint64_t>( &b[ 8 ] ) );
      if ( (b = bptr.incr( 8 )) == NULL )
        return RDB_ERR_TRUNC;
      pend.last_delivery = le<uint64_t>( b );
      if ( (err = deliv.decode( bptr )) != RDB_OK )
        return err;
      pend.delivery_cnt = deliv.len;
      this->out->d_stream_pend( pend );
    }
    if ( pend.cnt != 0 )
      this->out->d_stream_end( RdbOutput::STREAM_PENDING_LIST );
    /* get the group's consumers */
    RdbLength cons_cnt;
    if ( (err = cons_cnt.decode( bptr )) != RDB_OK )
      return err;
    /* for each consumer */
    RdbConsumerInfo cons( group );
    cons.cnt = cons_cnt.len;
    if ( cons.cnt != 0 )
      this->out->d_stream_start( RdbOutput::STREAM_CONSUMER_LIST );
    for ( cons.num = 0; cons.num < cons.cnt; cons.num++ ) {
      RdbLength cname;
      /* name and info */
      if ( (err = cname.decode( bptr )) != RDB_OK ||
           (err = cname.consume( bptr, b )) != RDB_OK )
        return RDB_OK;
      cons.cname     = (char *) b;
      cons.cname_len = cname.len;
      if ( (b = bptr.incr( 8 )) == NULL )
        return RDB_ERR_TRUNC;
      cons.last_seen = le<uint64_t>( b );

      RdbLength cpend;
      if ( (err = cpend.decode( bptr )) != RDB_OK )
        return err;
      cons.pend_cnt = cpend.len;
      this->out->d_stream_cons( cons );
      /* the consumer's pending list */
      RdbConsPendInfo conp( cons );
      conp.cnt = cons.pend_cnt;
      if ( conp.cnt != 0 )
        this->out->d_stream_start( RdbOutput::STREAM_CONSUMER_PENDING_LIST );
      for ( conp.num = 0; conp.num < conp.cnt; conp.num++ ) {
        if ( (b = bptr.incr( 128 / 8 )) == NULL ) /* stream id */
          return RDB_ERR_TRUNC;
        conp.id.set( be<uint64_t>( b ), be<uint64_t>( &b[ 8 ] ) );
        this->out->d_stream_cons_pend( conp );
      }
      if ( conp.cnt != 0 )
        this->out->d_stream_end( RdbOutput::STREAM_CONSUMER_PENDING_LIST );
      this->out->d_stream_end( RdbOutput::STREAM_CONSUMER );
    }
    if ( cons.cnt != 0 )
      this->out->d_stream_end( RdbOutput::STREAM_CONSUMER_LIST );
    this->out->d_stream_end( RdbOutput::STREAM_GROUP );
  }
  if ( group.cnt != 0 )
    this->out->d_stream_end( RdbOutput::STREAM_GROUP_LIST );
  this->out->d_end_key();
  return RDB_OK;
}

RdbErrCode
RdbDecode::decode_module( RdbBufptr &bptr ) noexcept
{
  static const char char_set[ 65 ] = /* 64 chars */
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ" "abcdefghijklmnopqrstuvwxyz" "0123456789-_";
  RdbString       str;
  char            buf[ 16 ];
  size_t          j = 0;
  uint64_t        m = this->rlen.len; /* the module id */
  const uint8_t * b;

  if ( this->rlen.is_enc  || this->rlen.is_lzf )
    return RDB_ERR_HDR;

  /* decode it */
  for ( int i = 9; i > 0; i-- )
    buf[ j++ ] = char_set[ ( m >> ( ( i * 6 ) + 4 ) ) & 63 ];
  /* set string with module version */
  str.set( buf, j + ::snprintf( &buf[ j ], 5, ".%d", (int) ( m & 1023 ) ) );

  this->start_key();
  this->out->d_module( str );
  this->out->d_end_key();

  /* find terminating eof */
  while ( (b = bptr.incr( 1 )) != NULL )
    if ( b[ 0 ] == 0 )
      break;
  return RDB_OK;
}
/* null output */
void RdbOutput::d_init( void ) noexcept {}
void RdbOutput::d_finish( bool ) noexcept {}
void RdbOutput::d_idle( uint64_t ) noexcept {}
void RdbOutput::d_freq( uint8_t ) noexcept {}
void RdbOutput::d_aux( const RdbString &,  const RdbString & ) noexcept {}
void RdbOutput::d_dbresize( uint64_t,  uint64_t ) noexcept {}
void RdbOutput::d_expired_ms( uint64_t ) noexcept {}
void RdbOutput::d_expired( uint32_t s ) noexcept { /* forward to ms */
  this->d_expired_ms( (uint64_t) s * 1000 ); }
void RdbOutput::d_dbselect( uint32_t ) noexcept {}
void RdbOutput::d_start_type( RdbType ) noexcept {}
void RdbOutput::d_start_key( void ) noexcept {}
void RdbOutput::d_end_key( void ) noexcept {}
void RdbOutput::d_string( const RdbString & ) noexcept {}
void RdbOutput::d_module( const RdbString & ) noexcept {}
void RdbOutput::d_hash( const RdbHashEntry & ) noexcept {}
void RdbOutput::d_list( const RdbListElem & ) noexcept {}
void RdbOutput::d_set( const RdbSetMember & ) noexcept {}
void RdbOutput::d_zset( const RdbZSetMember & ) noexcept {}
void RdbOutput::d_stream_start( StreamPart ) noexcept {}
void RdbOutput::d_stream_end( StreamPart ) noexcept {}
void RdbOutput::d_stream_entry( const RdbStreamEntry & ) noexcept {}
void RdbOutput::d_stream_info( const RdbStreamInfo & ) noexcept {}
void RdbOutput::d_stream_group( const RdbGroupInfo & ) noexcept {}
void RdbOutput::d_stream_pend( const RdbPendInfo & ) noexcept {}
void RdbOutput::d_stream_cons( const RdbConsumerInfo & ) noexcept {}
void RdbOutput::d_stream_cons_pend( const RdbConsPendInfo & ) noexcept {}
bool RdbFilter::match_key( const RdbString & ) noexcept { return true; }

namespace {
static const char hex_chars[] = "0123456789abcdef";
struct HexDump {
  char line[ 80 ];
  uint32_t boff, hex, ascii;
  uint64_t stream_off;

  HexDump( uint64_t off ) : boff( 0 ), stream_off( off ) {
    this->flush_line();
  }
  void reset( void ) {
    this->boff = 0;
    this->stream_off = 0;
    this->flush_line();
  }
  void flush_line( void ) {
    this->stream_off += this->boff;
    this->boff  = 0;
    this->hex   = 9;
    this->ascii = 61;
    this->init_line();
  }
  void init_line( void ) {
    uint64_t j, k = this->stream_off;
    ::memset( this->line, ' ', 79 );
    this->line[ 79 ] = '\0';
    this->line[ 5 ] = hex_chars[ k & 0xf ];
    k >>= 4; j = 4;
    while ( k > 0 ) {
      this->line[ j ] = hex_chars[ k & 0xf ];
      if ( j-- == 0 )
        break;
      k >>= 4;
    }
  }
  uint32_t fill_line( const void *ptr,  uint64_t off,  uint64_t len ) {
    while ( off < len && this->boff < 16 ) {
      uint8_t b = ((uint8_t *) ptr)[ off++ ];
      this->line[ this->hex ]   = hex_chars[ b >> 4 ];
      this->line[ this->hex+1 ] = hex_chars[ b & 0xf ];
      this->hex += 3;
      if ( b >= ' ' && b <= 127 )
        line[ this->ascii ] = b;
      this->ascii++;
      if ( ( ++this->boff & 0x3 ) == 0 )
        this->hex++;
    }
    return off;
  }
};
}

void
rdbparser::print_hex( const char *nm, size_t off, size_t end,
                      const uint8_t *b ) noexcept
{
  HexDump hex( off );
  fprintf( stderr, "%s:\n", nm );
  while ( off < end ) {
    off = hex.fill_line( b, off, end );
    fprintf( stderr, "%s\n", hex.line );
    hex.flush_line();
  }
}

/* Originally,
 * Copyright (c) 2014, Matt Stancliff <matt@genges.com>
 * All rights reserved.
 * source: https://github.com/mattsta/crcspeed
 * BSD or Apache 2.0 License */

static uint64_t jones_tab[8][256];

/* Fill in a CRC constants table. */
static void
jones_crc64_init( void )
{
  static const uint64_t POLY = 0xad93d23594c935a9ULL;
  uint64_t tab[ 8 ][ 256 ];
  uint64_t crc, val;
  uint8_t n = 0;
  int k;

  /* generate CRCs for all single byte sequences */
  do {
    crc = 0;
    for ( uint8_t i = 1; i != 0; i <<= 1 ) {
      k = ( crc & 0x8000000000000000 ) != 0;
      if ( ( n & i ) != 0 )
        k = ! k;

      crc <<= 1;
      if ( k )
        crc ^= POLY;
    }
    val = crc & 1;
    k   = 64;
    while ( --k != 0 )
      val = ( val << 1 ) | ( ( crc >>= 1 ) & 1 );
    tab[ 0 ][ n ] = val;
  } while ( n++ != 255 );
  /* generate nested CRC table for future slice-by-8 lookup */
  n = 0;
  do {
    crc = tab[ 0 ][ n ];
    for ( k = 1; k < 8; k++ ) {
      crc           = tab[ 0 ][ crc & 0xff ] ^ ( crc >> 8 );
      tab[ k ][ n ] = crc;
    }
  } while ( n++ != 255 );
#ifdef MACH_IS_BIG_ENDIAN
  n = 0;
  do {
    for ( k = 0; k < 8; k++ ) {
      crc           = tab[ k ][ n ];
      tab[ k ][ n ] = __builtin_bswap64( crc );
    }
  } while ( n++ != 255 );
#endif
  memcpy( jones_tab, tab, sizeof( jones_tab ) );
}

/* Calculate a non-inverted CRC multiple bytes at a time on a little-endian
 * architecture. If you need inverted CRC, invert *before* calling and invert
 * *after* calling.
 * 64 bit crc = process 8 bytes at once;
 */
uint64_t
rdbparser::jones_crc64( uint64_t crc, const void *buf, size_t len ) noexcept
{
  if ( jones_tab[ 7 ][ 255 ] == 0 )
    jones_crc64_init();

  const uint8_t *next = (const uint8_t *) buf;

#ifdef MACH_IS_BIG_ENDIAN
  crc = __builtin_bswap64( crc );
#define FIRST( X ) ( X >> 56 )
#define LAST( X ) ( X << 8 )
#else
#define FIRST( X ) X
#define LAST( X ) ( X >> 8 )
#endif
  /* process individual bytes until we reach an 8-byte aligned pointer */
  while ( len != 0 && ( (uintptr_t) next & 7 ) != 0 ) {
    crc = jones_tab[ 0 ][ ( FIRST( crc ) ^ *next++ ) & 0xff ] ^ LAST( crc );
    len--;
  }
#ifdef MACH_IS_BIG_ENDIAN
  #define ORDER( X ) 7-X
#else
  #define ORDER( X ) X
#endif
  /* fast middle processing, 8 bytes (aligned!) per loop */
  while ( len >= 8 ) {
    crc ^= *(uint64_t *) next;
    crc = jones_tab[ ORDER( 7 ) ][ crc & 0xff ] ^
          jones_tab[ ORDER( 6 ) ][ ( crc >> 8 ) & 0xff ] ^
          jones_tab[ ORDER( 5 ) ][ ( crc >> 16 ) & 0xff ] ^
          jones_tab[ ORDER( 4 ) ][ ( crc >> 24 ) & 0xff ] ^
          jones_tab[ ORDER( 3 ) ][ ( crc >> 32 ) & 0xff ] ^
          jones_tab[ ORDER( 2 ) ][ ( crc >> 40 ) & 0xff ] ^
          jones_tab[ ORDER( 1 ) ][ ( crc >> 48 ) & 0xff ] ^
          jones_tab[ ORDER( 0 ) ][ crc >> 56 ];
    next += 8;
    len -= 8;
  }

  /* process remaining bytes (can't be larger than 8) */
  while ( len != 0 ) {
    crc = jones_tab[ 0 ][ ( FIRST( crc ) ^ *next++ ) & 0xff ] ^ LAST( crc );
    len--;
  }

#ifdef MACH_IS_BIG_ENDIAN
  return __builtin_bswap64( crc );
#else
  return crc;
#endif
}

/* Test main */
#if defined(MY_CRC64_TEST)
#include <stdio.h>

int
main( int argc, char *argv[] )
{
  printf( "[64speed]: e9c6d914c4b8d9ca == %016lx\n",
          (uint64_t) jones_crc64( 0, "123456789", 9 ) );
  char li[] = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed "
              "do eiusmod tempor incididunt ut labore et dolore magna "
              "aliqua. Ut enim ad minim veniam, quis nostrud exercitation "
              "ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis "
              "aute irure dolor in reprehenderit in voluptate velit esse "
              "cillum dolore eu fugiat nulla pariatur. Excepteur sint "
              "occaecat cupidatat non proident, sunt in culpa qui officia "
              "deserunt mollit anim id est laborum.";
  printf( "[64speed]: c7794709e69683b3 == %016lx\n",
          (uint64_t) jones_crc64( 0, li, sizeof( li ) ) );
  return 0;
}
#endif
