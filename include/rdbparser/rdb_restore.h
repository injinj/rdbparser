#ifndef __rdbparser__rdb_restore_h__
#define __rdbparser__rdb_restore_h__

#ifdef __cplusplus
namespace rdbparser {

/* write restore command, key, and data, using:
 * RESTORE key ttl <type><data><ver><crc> [REPLACE] */
struct RestoreOutput : public RdbOutput {
  RdbBufptr & bptr;        /* buf containing data for offsets */
  uint64_t    ttl_ms,
              idle;
  size_t      type_offset; /* where type of data starts */
  bool        use_replace, /* use replace to overwrite key if it exists */
              is_matched;  /* if matched by filter */
  uint8_t     freq;

  RestoreOutput( RdbDecode &dec,  RdbBufptr &b,  bool repl )
    : RdbOutput( dec ), bptr( b ), ttl_ms( 0 ), idle( 0 ), type_offset( 0 ),
      use_replace( repl ), is_matched( false ), freq( 0 ) {}

  virtual void d_idle( uint64_t i ) noexcept;
  virtual void d_freq( uint8_t f ) noexcept;
  virtual void d_expired_ms( uint64_t ms ) noexcept;
  virtual void d_start_type( RdbType ) noexcept;
  virtual void d_start_key( void ) noexcept;

  void reset_state( void ) {
    this->is_matched = false;
    this->ttl_ms     = 0;
    this->idle       = 0;
    this->freq       = 0;
  }
  /* at end of key data, call this to write restore command to stdout */
  void write_restore_cmd( void ) noexcept;
};

} // namespace
#endif
#endif
