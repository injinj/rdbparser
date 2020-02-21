#ifndef __rdbparser__rdb_json_h__
#define __rdbparser__rdb_json_h__

#ifdef __cplusplus
namespace rdbparser {

struct JsonOutput : public RdbOutput {
  uint64_t d_cnt;     /* track when a comma is needed */
  bool     show_meta; /* show meta fields */
  JsonOutput( RdbDecode &d ) : RdbOutput( d ), d_cnt( 0 ), show_meta( false ) {}

  /* called first */
  virtual void d_init( void ) noexcept;       /* start main */
  /* callled last */
  virtual void d_finish( bool success ) noexcept; /* finish main */

  /* meta info */
  virtual void d_idle( uint64_t i ) noexcept;
  virtual void d_freq( uint8_t f ) noexcept;
  virtual void d_aux( const RdbString &var,  const RdbString &val ) noexcept;
  virtual void d_dbresize( uint64_t i,  uint64_t j ) noexcept;
  virtual void d_expired_ms( uint64_t sec ) noexcept;
  virtual void d_expired( uint32_t sec ) noexcept;
  virtual void d_dbselect( uint32_t db ) noexcept;

  /* called foreach key */
  virtual void d_start_key( void ) noexcept;  /* start key */
  virtual void d_end_key( void ) noexcept;    /* end key */

  /* rdb types */
  virtual void d_string( const RdbString &str ) noexcept;
  virtual void d_module( const RdbString &str ) noexcept;
  virtual void d_hash( const RdbHashEntry &h ) noexcept;  /* foreach hash entry */
  virtual void d_list( const RdbListElem &l ) noexcept;   /* foreach list entry */
  virtual void d_set( const RdbSetMember &s ) noexcept;   /* foreach set member */
  virtual void d_zset( const RdbZSetMember &z ) noexcept; /* foreach zset member */
  virtual void d_stream_start( StreamPart c ) noexcept;
  virtual void d_stream_end( StreamPart c ) noexcept;
  virtual void d_stream_entry( const RdbStreamEntry &entry ) noexcept; /* a stream data record */
  virtual void d_stream_info( const RdbStreamInfo &info ) noexcept;
  virtual void d_stream_group( const RdbGroupInfo &group ) noexcept;   /* group info records v */
  virtual void d_stream_pend( const RdbPendInfo &pend ) noexcept;
  virtual void d_stream_cons( const RdbConsumerInfo &cons ) noexcept;
  virtual void d_stream_cons_pend( const RdbConsPendInfo &pend ) noexcept;
};

void print_s( const RdbString &str,  bool use_quotes = true ) noexcept;

} // namespace
#endif
#endif
