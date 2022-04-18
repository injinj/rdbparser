#ifndef __rdbparser__rdb_int_h__
#define __rdbparser__rdb_int_h__

#ifdef _MSC_VER
#define rdb_int_bswap16( x ) _byteswap_ushort( x )
#define rdb_int_bswap32( x ) _byteswap_ulong( x )
#define rdb_int_bswap64( x ) _byteswap_uint64( x )
#else
#define rdb_int_bswap16( x ) __builtin_bswap16( x )
#define rdb_int_bswap32( x ) __builtin_bswap32( x )
#define rdb_int_bswap64( x ) __builtin_bswap64( x )
#endif
#ifdef __cplusplus

namespace rdbparser {

template<bool swp> inline void endian_read( const void *p,  uint8_t &i ) {
  ::memcpy( &i, p, sizeof( uint8_t ) );
}
template<bool swp> inline void endian_read( const void *p,  uint16_t &i ) {
  ::memcpy( &i, p, sizeof( uint16_t ) );
  if ( swp ) i = rdb_int_bswap16( i );
}
template<bool swp> inline void endian_read( const void *p,  uint32_t &i ) {
  ::memcpy( &i, p, sizeof( uint32_t ) );
  if ( swp ) i = rdb_int_bswap32( i );
}
template<bool swp> inline void endian_read( const void *p,  uint64_t &i ) {
  ::memcpy( &i, p, sizeof( uint64_t ) );
  if ( swp ) i = rdb_int_bswap64( i );
}
/* le = little endian */
template<class Int> inline Int le( const void *p ) { /* little endian */
  Int i;
#ifdef MACH_IS_BIG_ENDIAN
  endian_read<true>( p, i );
#else
  endian_read<false>( p, i );
#endif
  return i;
}
/* be = big endian */
template<class Int> inline Int be( const void *p ) { /* big endian */
  Int i;
#ifdef MACH_IS_BIG_ENDIAN
  endian_read<false>( p, i );
#else
  endian_read<true>( p, i );
#endif
  return i;
}

template<bool swp> inline void endian_write( void *p,  uint8_t i ) {
  ::memcpy( p, &i, sizeof( uint8_t ) );
}
template<bool swp> inline void endian_write( void *p,  uint16_t i ) {
  if ( swp ) i = rdb_int_bswap16( i );
  ::memcpy( p, &i, sizeof( uint16_t ) );
}
template<bool swp> inline void endian_write( void *p,  uint32_t i ) {
  if ( swp ) i = rdb_int_bswap32( i );
  ::memcpy( p, &i, sizeof( uint32_t ) );
}
template<bool swp> inline void endian_write( void *p,  uint64_t i ) {
  if ( swp ) i = rdb_int_bswap64( i );
  ::memcpy( p, &i, sizeof( uint64_t ) );
}
/* le = little endian */
template<class Int> inline void le( void *p,  Int i ) { /* little endian */
#ifdef MACH_IS_BIG_ENDIAN
  endian_write<true>( p, i );
#else
  endian_write<false>( p, i );
#endif
}
/* be = big endian */
template<class Int> inline void be( void *p,  Int i ) { /* big endian */
#ifdef MACH_IS_BIG_ENDIAN
  endian_write<false>( p, i );
#else
  endian_write<true>( p, i );
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
