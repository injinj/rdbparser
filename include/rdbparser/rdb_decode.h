#ifndef __rdbparser__rdb_decode_h__
#define __rdbparser__rdb_decode_h__

#ifdef __cplusplus
namespace rdbparser {
/* the redis rdb encoded data types, notes are based on rdb version 9 */
enum RdbType {
  RDB_STRING          = 0,            /* x data bytes */
  RDB_LIST            = 1,            /* x not used, quicklist */
  RDB_SET             = 2,            /* x list of string members */
  RDB_ZSET            = 3,            /* x string + dbl string score */
  RDB_HASH            = 4,            /* x list of strings for field + value */
  RDB_ZSET_2          = 5,            /* x string + dbl binary score */
  RDB_MODULE          = 6,            /* - what? */
  RDB_MODULE_2        = 7,            /* - are these? */
  RDB_BAD_TYPE        = 8,            /* x */
  RDB_HASH_ZIPMAP     = 9,            /* - not used, ziplist */
  RDB_LIST_ZIPLIST    = 10, /* a */   /* x not used, quicklist */
  RDB_SET_INTSET      = 11, /* b */   /* x array of ints, all same size */
  RDB_ZSET_ZIPLIST    = 12, /* c */   /* x ziplist of member + score */
  RDB_HASH_ZIPLIST    = 13, /* d */   /* x ziplist of field + value */
  RDB_LIST_QUICKLIST  = 14, /* e */   /* x list of ziplist */
  RDB_STREAM_LISTPACK = 15  /* f */   /* - */
};

/* info that is not data, encoded in the rdb file */
enum RdbMeta {
  RDB_MODULE_AUX  = 0xf7, /* module2 */
  RDB_IDLE        = 0xf8, /* value-type */
  RDB_FREQ        = 0xf9, /* length */
  RDB_AUX         = 0xfa, /* string, string */
  RDB_DBRESIZE    = 0xfb, /* length, length */
  RDB_EXPIRED_MS  = 0xfc, /* millisecond */
  RDB_EXPIRED_SEC = 0xfd, /* second */
  RDB_DBSEELECT   = 0xfe, /* length */
  RDB_EOF         = 0xff
};

/* decoding status errors */
enum RdbErrCode {
  RDB_OK          = 0,
  RDB_ERR_OUTPUT  = -1, /* no output */
  RDB_ERR_TRUNC   = -2, /* not enough data to decode */
  RDB_ERR_VERSION = -3, /* version 9 not matched */
  RDB_ERR_CRC     = -4, /* crc was non-zero and did not check */
  RDB_ERR_TYPE    = -5, /* RdbType is unknown */
  RDB_ERR_HDR     = -6, /* RdbLength in header is unparsable */
  RDB_ERR_LZF     = -7, /* lzf_decompress() failed */
  RDB_ERR_NOTSUP  = -8, /* a feature that is not supported, e.g. RDB_LIST type*/
  RDB_ERR_ZLEN    = -9, /* the count in the ziplist doesn't match */
  RDB_EOF_MARK    = -10 /* found eof meta marker */
};

static inline RdbType
get_rdb_type( uint8_t b ) /* the first byte in a rdb message */
{
  RdbType type = (RdbType) ( b & 0xf );
  if ( ( b & 0xf0 ) != 0 || type == RDB_BAD_TYPE )
    return RDB_BAD_TYPE;
  return type;
}

/* an index into a stream of rdb atoms */
struct RdbBufptr {
  const uint8_t * buf;             /* the data to be consumed */
  size_t          avail,           /* amount of data left */
                  offset,          /* amount of data consumed */
                  start_offset;    /* where buf starts in a stream */
  uint8_t         lookahead[ 32 ]; /* temp lookahead without overflow */
  void         ** alloced_mem;     /* lzf compression allocates new buffers */
  const uint8_t * sav;             /* main buffer, after decompression */
  size_t          sav_avail,       /* size left in main buffer */
                  sav_offset;      /* offset of main buffer */

  RdbBufptr( const uint8_t *b,  size_t sz )
    : buf( b ), avail( sz ), offset( 0 ), start_offset( 0 ), alloced_mem( 0 ),
      sav( 0 ), sav_avail( 0 ), sav_offset( 0 ) {}
  ~RdbBufptr() {
    if ( this->alloced_mem != NULL )
      this->free_alloced();
  }
  void free_alloced( void ) noexcept;
  /* lookahead type/size up to 20 bytes (type c3 81 .. 81 ..) */
  const uint8_t * look( size_t n ) noexcept;
  /* advance buf ptr */
  const uint8_t *incr( size_t amt ) {
    if ( amt > this->avail )
      return NULL;
    const uint8_t * b = this->buf;
    this->avail  -= amt;
    this->offset += amt;
    this->buf     = &this->buf[ amt ];
    return b;
  }
  /* lzf_decompress() from zlen -> len */
  bool decompress( size_t zlen,  size_t len ) noexcept;
};

/* the major length codec, always occurs in a rdb header
 *
 * length header: <= 0x3f         : 6 bit  ( 0 -> 0x3f )
 *                <= 0x7fff       : 14 bit ( 0 -> 0x3fff ) (big endian)
 *                == 0x80 4 bytes : 32 bit (big endian)
 *                == 0x81 8 bytes : 64 bit (big endian)
 *                -- 0xc0 <int8>
 *                -- 0xc1 <int16>
 *                -- 0xc2 <int32>
 *                == 0xc3 <zlen> <len> : compressed len, uncompressed len */
static const size_t MAX_RDB_HDRLEN = 9 * 2 + 1; /* c3 81 <64> 81 <64> */
struct RdbLength {
  size_t  len,    /* the length of data that follows */
          zlen;   /* if is_lzf, this is compressed size */
  int32_t ival;
  uint8_t is_enc; /* if > 0, then encoded signed integer */
  bool    is_lzf; /* if true, then lzf compressed */

  RdbLength()
    : len( 0 ), zlen( 0 ), ival( 0 ), is_enc( 0 ), is_lzf( false ) {}
  void zero( void ) {
    this->len    = this->zlen = 0;
    this->ival   = 0;
    this->is_enc = 0;
    this->is_lzf = false;
  }
  /* decode > 0, number of bytes in header, < 0 if overflow or bad byte */
  RdbErrCode decode( RdbBufptr &bptr ) {
    int sz;
    if ( bptr.avail >= MAX_RDB_HDRLEN )
      sz = this->decode_buf( bptr.buf );
    else
      sz = this->decode_buf( bptr.look( MAX_RDB_HDRLEN ) );
    if ( sz < 0 )
      return RDB_ERR_HDR;
    if ( bptr.incr( sz ) == NULL )
      return RDB_ERR_TRUNC;
    return RDB_OK;
  }
  /* must have a ptr with at least MAX_RDB_HDRLEN to call this */
  int decode_buf( const uint8_t *b ) noexcept;
  /* fetch data blob */
  RdbErrCode consume( RdbBufptr &bptr,  const uint8_t *&b ) const noexcept;
  /* type of lengths */
  enum LengthEnc {
    RDB_LEN_ERR = -1,
    RDB_LEN_6   = 0, /* 0x00 6 bits, 0x3f */
    RDB_LEN_14  = 1, /* 0x40 14 bits, 0x3fff (big endian) */
    RDB_LEN_32  = 2, /* 0x80 32 bits, big endian */
    RDB_LEN_64  = 3, /* 0x81 64 bits, big endian */
    RDB_LZF     = 4, /* 0xc3 <zlen> <len> */
    RDB_INT8    = 5, /* 0xc0 8 bits */
    RDB_INT16   = 6, /* 0xc1 16 bits, little endian */
    RDB_INT32   = 7  /* 0xc2 32 bits, little endian */
  };
  /* convert a byte into a type */
  static LengthEnc length_encoding( uint8_t b ) {
    switch ( b & 0xc0 ) {
      case 0x00: return RDB_LEN_6;  /* len = 6 bits */
      case 0x40: return RDB_LEN_14; /* len = 14 bits */
      case 0x80: return ( b == 0x80 ) ? RDB_LEN_32 : /* len = 32 bits */
                        ( b == 0x81 ) ? RDB_LEN_64 : RDB_LEN_ERR; /* 64 bits */
      case 0xc0:
        switch ( b ) {
          case 0xc3: return RDB_LZF; /* variable , 1 + <zlen> + <len> */
          case 0xc0: return RDB_INT8;  /* ival = 8 bits */
          case 0xc1: return RDB_INT16; /* ival = 16 bits */
          case 0xc2: return RDB_INT32; /* ival = 32 bits */
          default: break;
        }
        break;
    }
    return RDB_LEN_ERR;
  }
};

template<class Int, bool swp> inline Int any_endian( const uint8_t *p ) {
  Int i;
  ::memcpy( &i, p, sizeof( Int ) );
  if ( swp && sizeof( Int ) > 1 ) {
    if ( sizeof( Int ) == 2 )
      return __builtin_bswap16( i );
    if ( sizeof( Int ) == 4 )
      return __builtin_bswap32( i );
    return __builtin_bswap64( i );
  }
  return i;
}
/* le = little endian */
template<class Int> inline Int le( const uint8_t *p ) { /* little endian */
#ifdef MACH_IS_BIG_ENDIAN
  return any_endian<Int, true>( p );
#else
  return any_endian<Int, false>( p );
#endif
}
/* be = big endian */
template<class Int> inline Int be( const uint8_t *p ) { /* big endian */
#ifdef MACH_IS_BIG_ENDIAN
  return any_endian<Int, false>( p );
#else
  return any_endian<Int, true>( p );
#endif
}
/* unsigned encodings */
static inline uint64_t l32( const uint8_t *p ) { return le<uint32_t>( p ); }
static inline uint64_t b32( const uint8_t *p ) { return be<uint32_t>( p ); }
/* same as unsigned */
static inline uint64_t s64( const uint8_t *p ) { return le<uint64_t>( p ); }

/* signed little endian encodings, these are used to encode immediate ints
 * sign extend the negative bits out to 64b */
static inline uint64_t s32( const uint8_t *p ) {
  return (int64_t) (int32_t) le<uint32_t>( p ); /* sign 32 -> 64 */
}
static inline uint64_t s16( const uint8_t *p ) {
  return (int64_t) (int16_t) le<uint16_t>( p ); /* sign 16 -> 64 */
}
static inline uint64_t s24( const uint8_t *p ) {
  int16_t  top    = (int16_t) (int8_t) p[ 2 ];
  uint16_t bottom = le<uint16_t>( p );
  if ( top < 0 ) {
    uint32_t neg = ~0;
    return ( (uint64_t) neg << 32 ) | /* sign extend bits */
             (uint64_t) ( ( (uint32_t) top << 16 ) | (uint32_t) bottom );
  }
  return ( (uint32_t) top << 16 ) | (uint32_t) bottom; /* unsigned */
}
static inline uint64_t s8( uint8_t i ) {
  return (uint64_t) (int64_t) (int8_t) i;      /* sign 8 -> 64 */
}
static inline uint64_t s13( const uint8_t *p ) { /* big! */
  if ( ( p[ 0 ] & 0x10 ) != 0 ) { /* if sign bit is set */
    int32_t  top = (int8_t) 16 - (int8_t) ( p[ 0 ] & 0xf ); /* extend */
    uint32_t neg = ~0;
    return ( (uint64_t) neg << 32 ) |
             (uint64_t) ( ( (uint32_t) top << 8 ) | (uint32_t) p[ 1 ] );
  }
  return ( ( (uint16_t) p[ 0 ] << 8 ) | p[ 1 ] ) & 0x1fff; /* unsigned */
}


/* RdbListValue is used for zip list and list pack traversal
 * 
 *         [ next ][ opt data ][ back ]   <- list pack element
 * [ prev ][ next ][ opt data ]           <- zip list element
 *     .___^       ^_____.
 *     |                 |
 *   entry[ entry_len ]  data [ data_len ] or ival for immediate integers
 *
 *   entry is the encoded list element , data is the decoded string
 */
struct RdbListValue {
  const uint8_t * data,      /* decoded data */
                * entry;     /* entry is the head of data, undecoded */
  size_t          data_len,  /* decoded data len */
                  entry_len; /* undecoded entry len */
  int64_t         ival;      /* decoded immediate integer */
};

/* zip list is used quite often for coding lists of things: list, hash, zset
 *
 * zip list structure:
 * hdr:  [ zlbytes size ] [ zltail offset ] [ zllen entry count ]
 * list: [ 0 ] [ next ] [ optional data ]
 *       [ prev ] [ next ] [ optional data ]
 *       [ 0xff ]
 *
 * the prev link always encodes a length to traverse in reverse
 *
 * the next link could encode a length or an integer
 *   if next encodes an immediate integer, the optional data is not needed
 */
struct RdbZipList {
  uint32_t        zlbytes; /* count of bytes in list, including hdr */
  uint32_t        zltail;  /* offset of last entry */
  uint16_t        zllen;   /* count of entries */
  const uint8_t * data,    /* the first entry */
                * end;     /* end of data */

  RdbZipList() : zlbytes( 0 ), zltail( 0 ), zllen( 0 ), data( 0 ), end( 0 ) {}

  /* header is [ zlbytes * 4 ][ zltail * 4 ][ zllen * 2 ]
   *   zlbytes = size of ziplist including header
   *   zltail  = offset of last entry
   *   zllen   = number of entries or 0xffff (many) */
  bool init( const uint8_t *b,  size_t sz ) {
    if ( sz < 10 )
      return false;
    this->zlbytes = le<uint32_t>( b );
    this->zltail  = le<uint32_t>( &b[ 4 ] );
    this->zllen   = le<uint16_t>( &b[ 8 ] );
    if ( this->zlbytes < 11 ) /* if only end marker (255) */
      return false;
    this->data = &b[ 10 ];
    this->end  = &b[ this->zlbytes ];
    return true;
  }
  enum ZipLinkEnc {
    ZIP_END  = 0, /* end of list (prev) */
    LEN_32   = 1, /* 0xfe <32> little endian (prev) */
    BIG_32   = 2, /* 0x80 <32> big endian */
    LEN_8    = 3, /* 1 byte, less than 0xfe (prev) */
    LEN_6    = 4, /* lower 6 bits, 0x3f */
    BIG_14   = 5, /* lower 14 bits, 0x3fff big endian */
    IMMED_16 = 6, /* 2 bytes following 0xc0, immediate integer data */
    IMMED_32 = 7, /* 4 bytes following 0xd0, immediate are little endian */
    IMMED_64 = 8, /* 8 bytes following 0xe0 */
    IMMED_24 = 9, /* 3 bytes following 0xf0 */
    IMMED_8  = 10,/* 1 byte following 0xfe */
    IMMED_4  = 11 /* lower 4 bits - 1 */
  };
  static inline bool is_immed( ZipLinkEnc e ) {
    return e >= IMMED_16;
  }
  #define ISZ( v, e ) ( (uint64_t) v << ( (int) e * 4 ) )
  static const uint64_t ZSIZE =
    ISZ( 0, ZIP_END  ) | /* end of list */
    ISZ( 5, LEN_32   ) | /* 0xfe <32> little endain */
    ISZ( 5, BIG_32   ) | /* 0x80 <32> big endian */
    ISZ( 1, LEN_8    ) | /* 1 byte, less than 0xfe */
    ISZ( 1, LEN_6    ) | /* lower 6 bits, 0x3f */
    ISZ( 2, BIG_14   ) | /* lower 14 bits, 0x3fff */
    ISZ( 3, IMMED_16 ) | /* 2 bytes following 0xc0, immediate integer data */
    ISZ( 5, IMMED_32 ) | /* 4 bytes following 0xd0, immediate are little end */
    ISZ( 9, IMMED_64 ) | /* 8 bytes following 0xe0 */
    ISZ( 4, IMMED_24 ) | /* 3 bytes following 0xf0 */
    ISZ( 2, IMMED_8  ) | /* 1 byte following 0xfe */
    ISZ( 1, IMMED_4  );  /* lower 4 bits - 1 */
  #undef ISZ
  static inline uint8_t zlink_size( ZipLinkEnc e ) {
    return ( ZSIZE >> ( e * 4 ) ) & 0xfU; /* sizes stored in a u64 constant */
  }
  /* find encoding for prev, always a length */
  static ZipLinkEnc zlink_prev( uint8_t n ) {
    if ( n == 0xff ) /* end if list */
      return ZIP_END;
    if ( n == 0xfe ) /* 0xfe <32> bits little endian */
      return LEN_32;
    return LEN_8; /* 8 bit len */
  }
  /* find encoding for next, may be length, may be an immediate integer */
  static ZipLinkEnc zlink_next( uint8_t n ) {
    switch ( n & 0xc0 ) {
      case 0x00: return LEN_6;  /* len enc in 0x3f bits       (00......) */
      case 0x40: return BIG_14; /* len enc in 0x3fff bits     (01......) */
      case 0x80: return BIG_32; /* len enc in 0x80 <32> big   (10......) */
      case 0xc0:                                        /*    (11......) */
      default: /* only 0xc0 is possible */
        switch ( n & 0xf0 ) {
          case 0xc0: return IMMED_16; /* int enc in 0xc0 <16> (1100....) */
          case 0xd0: return IMMED_32; /* int enc in 0xd0 <32> (1101....) */
          case 0xe0: return IMMED_64; /* int enc in 0xe0 <64> (1110....) */
          case 0xf0:                                    /*    (1111....) */
          default: /* only 0xf0 is possible */
            switch ( n & 0x0f ) {
              case 0x00: return IMMED_24; /* int enc 0xf0 <24>(11110000) */
              case 0x0e: return IMMED_8;  /* int enc 0xfe <8> (11111110) */
              case 0x0f: return ZIP_END;  /* end of zip list  (11111111) */
              default:   return IMMED_4;  /* int enc 0xf [0x1,0xd] - 1 */
            }                                           /*    (11110001) */
        }                                               /*... (11111101) */
    }
  }
  /* contents of index */
  static uint64_t zlink_val( ZipLinkEnc e,  const uint8_t *p ) {
    switch ( e ) {
      case ZIP_END:  return 0;
      case LEN_32:   return l32( &p[ 1 ] );      /* 0xfe <32> little endian */
      case BIG_32:   return b32( &p[ 1 ] );      /* 0x80 <32> big endian */
      case LEN_8:    return p[ 0 ];              /* 1 byte, less than 0xfe */
      case LEN_6:    return p[ 0 ] & 0x3f;       /* lower 6 bits, 0x3f */
      case BIG_14:   return ( (uint64_t) ( p[ 0 ] & 0x3f ) << 8 ) | p[ 1 ];
                                                 /* lower 14 bits, 0x3fff */
      case IMMED_16: return s16( &p[ 1 ] );      /* 2 bytes following 0xc0 */
      case IMMED_32: return s32( &p[ 1 ] );      /* 4 bytes following 0xd0 */
      case IMMED_64: return s64( &p[ 1 ] );      /* 8 bytes following 0xe0 */
      case IMMED_24: return s24( &p[ 1 ] );      /* 3 bytes following 0xf0 */
      case IMMED_8:  return s8( p[ 1 ] );        /* 1 byte following 0xfe */
      case IMMED_4:  return ( p[ 0 ] & 0xf ) - 1;/* lower 4 bits - 1 (unsign) */
    }
    return 0; /* not possible */
  }
  /* first is [ 0 ][ next-idx ] */
  bool first( RdbListValue &lval ) const {
    lval.entry     = this->data;
    lval.entry_len = 0; /* zero length data */
    return this->next( lval );
  }
  /* entry -> [ entry_len data ][ prev-idx ][ next-idx ] */
  bool next( RdbListValue &lval ) const noexcept;
};

/* list pack is used for coding streams, it is quite similar to zip lists
 *
 * list pack structure:
 * hdr:  [ lpbytes size ] [ lplen entry count ]
 * list: [ next ] [ optional data ] [ back ]
 *       [ next ] [ optional data ] [ back ]
 *       [ 0xff ]
 *
 * the back link always encodes a length to traverse to the previous next
 * position
 *
 * the next link could encode a length or an integer
 *   if next encodes an immediate integer, the optional data is not needed
 */
struct RdbListPack {
  uint32_t        lpbytes; /* count of bytes in list, including hdr */
  uint16_t        lplen;   /* count of entries */
  const uint8_t * data,    /* the first entry */
                * end;     /* end of data */
  /* header is [ lpbytes * 4 ][ lplen * 2 ]
   *   lpbytes = size of ziplist including header
   *   lplen   = number of entries or 0xffff (many) */
  bool init( const uint8_t *b,  size_t sz ) {
    if ( sz < 6 )
      return false;
    this->lpbytes = le<uint32_t>( b );
    this->lplen   = le<uint16_t>( &b[ 4 ] );
    if ( this->lpbytes < 7 ) /* if only end marker (255) */
      return false;
    this->data = &b[ 6 ];
    this->end  = &b[ this->lpbytes ];
    return true;
  }
  enum ListPackEnc {   /* expanding opcode, ints and strings */
    LP_7BIT_UINT = 0, /* 0.......   7 bits */
    LP_6BIT_STR  = 1, /* 10......   6 bits */
    LP_13BIT_INT = 2, /* 110.....   5 +  8 bits */
    LP_12BIT_STR = 3, /* 1110....   4 +  8 bits */
    LP_32BIT_STR = 4, /* 11110000   x   32 bits */
    LP_16BIT_INT = 5, /* 11110001   x   16 bits */
    LP_24BIT_INT = 6, /* 11110010   x   24 bits */
    LP_32BIT_INT = 7, /* 11110011   x   32 bits */
    LP_64BIT_INT = 8, /* 11110100   x   64 bits */
    LP_END       = 9, /* 11111111 */
    LP_UNUSED    = 10 /* bits 11110101 -> 11111110 not used */
  };
  static inline bool is_immed( ListPackEnc e ) {
    return e != LP_6BIT_STR && e != LP_12BIT_STR && e != LP_32BIT_STR;
  }
  #define ISZ( v, e ) ( (uint64_t) v << ( (int) e * 4 ) )
  static const uint64_t LP_SIZE =
    ISZ( 1, LP_7BIT_UINT ) | /* 0.......   7 bits immediate */
    ISZ( 1, LP_6BIT_STR  ) | /* 10......   6 bits string */
    ISZ( 2, LP_13BIT_INT ) | /* 110.....   5 +  8 bits immediate */
    ISZ( 2, LP_12BIT_STR ) | /* 1110....   4 +  8 bits string */
    ISZ( 5, LP_32BIT_STR ) | /* 11110000   x   32 bits string */
    ISZ( 3, LP_16BIT_INT ) | /* 11110001   x   16 bits immediate */
    ISZ( 4, LP_24BIT_INT ) | /* 11110010   x   24 bits immediate */
    ISZ( 5, LP_32BIT_INT ) | /* 11110011   x   32 bits immediate */
    ISZ( 9, LP_64BIT_INT ) | /* 11110100   x   64 bits immediate */
    ISZ( 1, LP_END       ) | /* 11111111 */
    ISZ( 0, LP_UNUSED    );  /* bits 11110101 -> 11111110 not used */
  #undef ISZ
  static inline uint8_t lp_size( ListPackEnc e ) {
    return ( LP_SIZE >> ( e * 4 ) ) & 0xfU; /* sizes stored in a u64 constant */
  }
  static ListPackEnc lp_code( uint8_t b ) {
    switch ( ( b & 0xF0 ) >> 4 ) {
      case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: /*(0...)*/
        return LP_7BIT_UINT;
      case 8: case 9: case 10: case 11: /* 0x80, 0x90, 0xA0, 0xB0       (10..)*/
        return LP_6BIT_STR;
      case 12: case 13:                 /* 0xC0, 0xD0             (1100, 1101)*/
        return LP_13BIT_INT;
      case 14:                          /* 0xE0                         (1110)*/
        return LP_12BIT_STR;
      case 15:
        switch ( b & 0x0F ) {
          case 0: return LP_32BIT_STR;  /* 0xF0 */
          case 1: return LP_16BIT_INT;  /* 0xF1 */
          case 2: return LP_24BIT_INT;  /* 0xF2 */
          case 3: return LP_32BIT_INT;  /* 0xF3 */
          case 4: return LP_64BIT_INT;  /* 0xF4 */
          case 15: return LP_END;       /* 0xFF */
          default: return LP_UNUSED;    /* 0xF5 -> 0xFE */
        }
    }
    return LP_UNUSED; /* not reached */
  }
  /* contents of index */
  static uint64_t lp_val( ListPackEnc e,  const uint8_t *p ) {
    switch ( e ) {
      case LP_7BIT_UINT: return p[ 0 ] & 0x7f;  /* 0.......   7 bits */
      case LP_6BIT_STR:  return p[ 0 ] & 0x3f;  /* 10......   6 bits */
      case LP_13BIT_INT: return s13( p );       /* 110.....   5 +  8 bits */
      case LP_12BIT_STR:                        /* 1110....   4 +  8 bits */
        return ( ( (uint64_t) p[ 0 ] & 0xf ) << 8 ) | p[ 1 ]; /* big! */
      case LP_32BIT_STR: return l32( &p[ 1 ] ); /* 11110000   x   32 bits */
      case LP_16BIT_INT: return s16( &p[ 1 ] ); /* 11110001   x   16 bits */
      case LP_24BIT_INT: return s24( &p[ 1 ] ); /* 11110010   x   24 bits */
      case LP_32BIT_INT: return s32( &p[ 1 ] ); /* 11110011   x   32 bits */
      case LP_64BIT_INT: return s64( &p[ 1 ] ); /* 11110100   x   64 bits */
      case LP_END:       return 0;              /* 11111111 */
      case LP_UNUSED:    return 0; /* bits 11110101 -> 11111110 not used */
    }
    return 0; /* not possible */
  }
  /* first is [ next ][ opt-data ]  */
  bool first( RdbListValue &lval ) const {
    lval.entry     = this->data;
    lval.entry_len = 0; /* zero length data */
    return this->next( lval );
  }
  bool first_ival( RdbListValue &lval ) const {
    return this->first( lval ) && lval.data == NULL;
  }
  /* entry -> [ next ][ opt-data ][ back ][ next-2 ], skip to next-2 code
   *          |  entry len               |            and advance entry */
  bool next( RdbListValue &lval ) const noexcept;

  bool next_ival( RdbListValue &lval ) const {
    return this->next( lval ) && lval.data == NULL;
  }
};

enum RdbValueCoding {
  RDB_NO_VAL  = 0,
  RDB_INT_VAL = 1,
  RDB_STR_VAL = 2,
  RDB_DBL_VAL = 3
};

struct RdbString {
  RdbValueCoding coding; /* all the coding methods in rdb */
  const char   * s;
  union {
    int64_t ival;
    size_t  s_len;  /* strlen of s, if RDB_STR_VAL */
    double  fval;
  };

  RdbString() : coding( RDB_NO_VAL ) {}

  void set( const char *str,  size_t len ) {
    this->coding = RDB_STR_VAL;
    this->s = str; this->s_len = len;
  }
  void set( int32_t i ) {
    this->coding = RDB_INT_VAL;
    this->ival = i;
  }
  void set( int64_t i ) {
    this->coding = RDB_INT_VAL;
    this->ival = i;
  }
  void set( double f ) {
    this->coding = RDB_DBL_VAL;
    this->fval = f;
  }
};

struct RdbElemNum {
  size_t num,  /* num is the current element or field */
         cnt;  /* cnt may be set to the total number, if available */
  RdbElemNum() : num( 0 ), cnt( 0 ) {}
};

struct RdbHashEntry : public RdbElemNum {
  RdbString field, /* a hash field = value */
            val;
};

struct RdbListElem : public RdbElemNum {
  RdbString val;   /* an list element */
};

struct RdbSetMember : public RdbElemNum {
  RdbString member; /* a set member */
};

struct RdbZSetMember : public RdbElemNum {
  RdbString member, /* a zset member with score */
            score;
};

struct RdbStreamRecId {
  uint64_t ms,  /* a stream id is an incrementing 128 bit number */
           ser; /* usually the UTC milliseconds + a serial */
  void set( uint64_t m, uint64_t s ) {
    this->ms  = m;
    this->ser = s;
  }
};

enum RdbStreamFlags {
  RDB_STREAM_NONE             = 0,
  RDB_STREAM_ENTRY_DELETED    = 1,
  RDB_STREAM_ENTRY_SAMEFIELDS = 2
};

struct RdbStreamEntry : public RdbElemNum {
  RdbStreamRecId id,          /* the current id = id + diff */
                 diff;        /* each entry codes the difference of ref id */
  size_t         items_count, /* number of items in the list pack (not total)*/
                 deleted_count, /* number of items deleted in the list pack */
                 master_field_count, /* count of fields reused for each entry */
                 entry_field_count;  /* count of fields in the entry */
  uint32_t       flags;         /* flags above */

  static const size_t TMP_SIZE = 64;
  RdbListValue * master,      /* master field list for this block */
               * fields,      /* fields in the current entry */
               * values,      /* fields[ i ] pairs with values[ i ] */
                 tmp[ TMP_SIZE ]; /* tmp space for fields, values */

  RdbStreamEntry() : items_count( 0 ), deleted_count( 0 ),
                     master_field_count( 0 ), master( 0 ), fields( 0 ),
                     values( 0 ) {}
  ~RdbStreamEntry() {
    this->release( this->master ); /* if allocated, release */
    this->release( this->fields );
    this->release( this->values );
  }
  /* header contains constants for the ListPack block, master fields */
  bool read_header( RdbListPack &list,  RdbListValue &lval ) noexcept;
  /* entry contains deltas for the next field value list */
  bool read_entry( RdbListPack &list,  RdbListValue &lval ) noexcept;
  /* free if necessary */
  void release( RdbListValue *f ) {
    if ( f != NULL && ( f < this->tmp || f >= &this->tmp[ TMP_SIZE ] ) )
      ::free( f );
  }
};

struct RdbStreamInfo {
  RdbStreamRecId last;        /* last id used, next must be larger */
  size_t         num_elems,   /* count of stream entries */
                 num_cgroups, /* count of groups defined */
                 entry_cnt;
  RdbStreamInfo( size_t cnt ) : entry_cnt( cnt ) {}
};

struct RdbGroupInfo : public RdbElemNum {
  const char    * gname;       /* name of group */
  size_t          gname_len,
                  pending_cnt; /* how many pending entries, waiting for ack */
  RdbStreamRecId  last;        /* last id read by the group */
  RdbStreamInfo & stream;
  RdbGroupInfo( RdbStreamInfo &s ) : stream( s ) {}
};

struct RdbPendInfo : public RdbElemNum {
  RdbStreamRecId id;            /* this pending entry id */
  uint64_t       last_delivery, /* last time delivered */
                 delivery_cnt;  /* count of times delivered */
  RdbGroupInfo & group;
  RdbPendInfo( RdbGroupInfo &g ) : group( g ) {}
};

struct RdbConsumerInfo : public RdbElemNum {
  const char   * cname;         /* name of consumer */
  size_t         cname_len,
                 pend_cnt;      /* how many pending records */
  uint64_t       last_seen;     /* time in milliseconds */
  RdbGroupInfo & group;
  RdbConsumerInfo( RdbGroupInfo &g ) : group( g ) {}
};

struct RdbConsPendInfo : public RdbElemNum {
  RdbStreamRecId id;            /* a record in the consumer's pending list */
  RdbConsumerInfo & cons;
  RdbConsPendInfo( RdbConsumerInfo &c ) : cons( c ) {}
};

struct RdbDecode;

struct RdbOutput {
  RdbDecode & dec;

  RdbOutput( RdbDecode &d ) : dec( d ) {}

  /* called first */
  virtual void d_init( void ) noexcept;       /* start main */
  /* callled last */
  virtual void d_finish( bool success ) noexcept; /* finish main */
  /* meta info, the idle, freq, expired belong to the next key */
  virtual void d_idle( uint64_t i ) noexcept;
  virtual void d_freq( uint8_t f ) noexcept;
  virtual void d_aux( const RdbString &var,  const RdbString &val ) noexcept;
  virtual void d_dbresize( uint64_t i,  uint64_t j ) noexcept;
  virtual void d_expired_ms( uint64_t ms ) noexcept;
  virtual void d_expired( uint32_t sec ) noexcept;
  virtual void d_dbselect( uint32_t db ) noexcept;

  /* called foreach key, start type after decoding it */
  virtual void d_start_type( RdbType t ) noexcept;
  /* called foreach key that passes the key filters (RdbFilter below) */
  virtual void d_start_key( void ) noexcept;       /* start key after filtering */
  virtual void d_end_key( void ) noexcept;         /* end key */
  /* rdb types, called after passing key filters, depending on type */
  virtual void d_string( const RdbString &str ) noexcept;
  virtual void d_module( const RdbString &str ) noexcept;
  virtual void d_hash( const RdbHashEntry &h ) noexcept;/* foreach hash entry */
  virtual void d_list( const RdbListElem &l ) noexcept; /* foreach list entry */
  virtual void d_set( const RdbSetMember &s ) noexcept; /* foreach set member */
  virtual void d_zset( const RdbZSetMember &z ) noexcept;/* foreach zset memb */

  /* streams have several parts: entries, groups, pending lists, consumers */
  enum StreamPart {
    STREAM_ENTRY_LIST, STREAM_GROUP_LIST, STREAM_GROUP, STREAM_PENDING_LIST,
    STREAM_CONSUMER_LIST, STREAM_CONSUMER, STREAM_CONSUMER_PENDING_LIST
  };
  /* these make it easier to open and close json objects / lists */
  virtual void d_stream_start( StreamPart c ) noexcept;
  virtual void d_stream_end( StreamPart c ) noexcept;
  /* foreach stream entry */
  virtual void d_stream_entry( const RdbStreamEntry &entry ) noexcept;
  /* one info for the stream */
  virtual void d_stream_info( const RdbStreamInfo &info ) noexcept;
  /* foreach group in the stream */
  virtual void d_stream_group( const RdbGroupInfo &group ) noexcept;
  /* foreach pending rec in the group */
  virtual void d_stream_pend( const RdbPendInfo &pend ) noexcept;
  /* foreach consumer */
  virtual void d_stream_cons( const RdbConsumerInfo &cons ) noexcept;
  /* foreach pending that the consumer has */
  virtual void d_stream_cons_pend( const RdbConsPendInfo &pend ) noexcept;
};

struct RdbFilter {
  RdbDecode & dec;

  RdbFilter( RdbDecode &d ) : dec( d ) {}
  /* returns true if key should be output */
  virtual bool match_key( const RdbString &key ) noexcept;
};

/* decode an rdb blob which has a header, trailer, and a body 
 *
 * decode_hdr()  = RdbType + RdbLength, trailer = crc + version
 * decode_body() = elements coded according to the RdbType
 *   if complicated type, decode_body() calls one of these : 
 *     decode_hash_zipmap()  : HASH_ZIPMAP
 *     decode_set_intset()   : SET_INTSET
 *     decode_zset()         : ZSET or ZSET_2
 *     decode_ziplist()      : HASH_ZIPLIST, ZSET_ZIPLIST, LIST_ZIPLIST
 *     decode_quicklist()    : LIST_QUICKLIST
 *     decode_stream()       : STREAM_LISTPACK
 *     decode_module()       : MODULE, MODULE_2 */

struct RdbDecode {
  RdbOutput * out,      /* current output */
            * data_out; /* real output */
  RdbOutput   null_out; /* used when keys are filtered */
  RdbFilter * filter;   /* match keys */
  RdbLength   rlen;     /* the length in the header of the record */
  RdbType     type;     /* the type of the record being decoded */
  uint64_t    crc;      /* trail crc check, if present */
  RdbString   key;      /* the key in a rdb file, not in a dump */
  uint64_t    key_cnt;  /* count of keys decoded */
  uint16_t    ver;      /* rdb ver check */
  bool        is_rdb_file; /* "dump" or "save" used, if "save", then true */

  RdbDecode()
    : out( 0 ), data_out( 0 ), null_out( *this ), filter( 0 ),
      type( RDB_BAD_TYPE ), crc( 0 ), key_cnt( 0 ), ver( 0 ),
      is_rdb_file( false ) {}

  /* call filter if present and set up output */
  void start_key( void ) {
    if ( this->filter == NULL || this->filter->match_key( this->key ) )
      this->out = this->data_out;
    else
      this->out = &this->null_out;
    this->out->d_start_key();
  }
  /* determine type, crc check */
  RdbErrCode decode_hdr( RdbBufptr &bptr ) noexcept;
  /* iterate through the elements in the dump */
  RdbErrCode decode_body( RdbBufptr &bptr ) noexcept;
  /* decode a HASH_ZIPMAP type */
  RdbErrCode decode_hash_zipmap( RdbBufptr &bptr ) noexcept;
  /* decode a SET_INTSET type */
  RdbErrCode decode_set_intset( RdbBufptr &bptr ) noexcept;
  /* decode ZSET or ZSET_2 type */
  RdbErrCode decode_zset( RdbBufptr &bptr ) noexcept;
  /* decode HASH_ZIPLIST, ZSET_ZIPLIST, LIST_ZIPLIST types */
  RdbErrCode decode_ziplist( RdbBufptr &bptr ) noexcept;
  /* decode LIST_QUICKLIST types */
  RdbErrCode decode_quicklist( RdbBufptr &bptr ) noexcept;
  /* decode a STREAM_LISTPACK type */
  RdbErrCode decode_stream( RdbBufptr &bptr ) noexcept;
  /* decode a MODULE, MODULE_2 type */
  RdbErrCode decode_module( RdbBufptr &bptr ) noexcept;
  /* decode a integer or length and copy a reference to string */
  RdbErrCode decode_rlen( RdbBufptr &bptr,  RdbString &str ) noexcept;
  /* copy a reference of length to string */
  RdbErrCode decode_str( RdbBufptr &bptr,  RdbString &str,
                         RdbLength &len ) noexcept;
};

void print_hex( const char *nm, size_t off, size_t end,
                const uint8_t *b ) noexcept;

uint64_t jones_crc64( uint64_t crc, const void *buf, size_t len ) noexcept;

} // namespace
#endif
#endif
