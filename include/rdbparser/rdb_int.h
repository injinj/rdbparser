#ifndef __rdbparser__rdb_int_h__
#define __rdbparser__rdb_int_h__

#ifdef __cplusplus

namespace rdbparser {

template<class Int, bool swp> inline Int endian_read( const void *p ) {
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
template<class Int> inline Int le( const void *p ) { /* little endian */
#ifdef MACH_IS_BIG_ENDIAN
  return endian_read<Int, true>( p );
#else
  return endian_read<Int, false>( p );
#endif
}
/* be = big endian */
template<class Int> inline Int be( const void *p ) { /* big endian */
#ifdef MACH_IS_BIG_ENDIAN
  return endian_read<Int, false>( p );
#else
  return endian_read<Int, true>( p );
#endif
}

template<class Int, bool swp> inline void endian_write( void *p,  Int i ) {
  if ( swp && sizeof( Int ) > 1 ) {
    if ( sizeof( Int ) == 2 )
      i = __builtin_bswap16( i );
    else if ( sizeof( Int ) == 4 )
      i = __builtin_bswap32( i );
    else
      i = __builtin_bswap64( i );
  }
  ::memcpy( p, &i, sizeof( Int ) );
}
/* le = little endian */
template<class Int> inline void le( void *p,  Int i ) { /* little endian */
#ifdef MACH_IS_BIG_ENDIAN
  endian_write<Int, true>( p, i );
#else
  endian_write<Int, false>( p, i );
#endif
}
/* be = big endian */
template<class Int> inline void be( void *p,  Int i ) { /* big endian */
#ifdef MACH_IS_BIG_ENDIAN
  endian_write<Int, false>( p, i );
#else
  endian_write<Int, true>( p, i );
#endif
}
/* unsigned encodings */
static inline uint64_t l32( const void *p ) { return le<uint32_t>( p ); }
static inline uint64_t b32( const void *p ) { return be<uint32_t>( p ); }
/* same as unsigned */
static inline uint64_t s64( const void *p ) { return le<uint64_t>( p ); }

/* signed little endian encodings, these are used to encode immediate ints
 * sign extend the negative bits out to 64b */
static inline uint64_t s32( const void *p ) {
  return (int64_t) (int32_t) le<uint32_t>( p ); /* sign 32 -> 64 */
}
static inline uint64_t s16( const void *p ) {
  return (int64_t) (int16_t) le<uint16_t>( p ); /* sign 16 -> 64 */
}
static inline uint64_t s24( const void *p ) {
  const uint8_t * b = (const uint8_t *) p;
  int16_t  top      = (int16_t) (int8_t) b[ 2 ];
  uint16_t bottom   = le<uint16_t>( p );
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
static inline uint64_t s13( const void *p ) { /* big! */
  const uint8_t * b = (const uint8_t *) p;
  if ( ( b[ 0 ] & 0x10 ) != 0 ) { /* if sign bit is set */
    int32_t  top = (int8_t) 16 - (int8_t) ( b[ 0 ] & 0xf ); /* extend */
    uint32_t neg = ~0;
    return ( (uint64_t) neg << 32 ) |
             (uint64_t) ( ( (uint32_t) top << 8 ) | (uint32_t) b[ 1 ] );
  }
  return ( ( (uint16_t) b[ 0 ] << 8 ) | b[ 1 ] ) & 0x1fff; /* unsigned */
}

} // namespace
#endif
#endif
