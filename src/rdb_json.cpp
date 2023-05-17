#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <rdbparser/rdb_decode.h>
#include <rdbparser/rdb_json.h>

using namespace rdbparser;

void
rdbparser::print_s( const RdbString &str,  bool use_quotes ) noexcept
{
  switch ( str.coding ) {
    case RDB_NO_VAL:  printf( "\"nil\"" ); break;
    case RDB_INT_VAL: printf( "%" PRId64 "", str.ival ); break;
    case RDB_STR_VAL: {
      char out[ 128 ];
      size_t sz = 0, len = str.s_len;
      const char * s = str.s;
      if ( use_quotes )
        out[ sz++ ] = '\"';
      for ( size_t i = 0; i < len; i++ ) {
        if ( (uint8_t) s[ i ] >= ' ' && (uint8_t) s[ i ] <= 126 ) {
          switch ( s[ i ] ) {
            case '\'':
            case '"': out[ sz++ ] = '\\'; /* FALLTHRU */
            default:  out[ sz++ ] = s[ i ]; break;
          }
        }
        else {
          out[ sz ] = '\\';
          switch ( s[ i ] ) {
            case '\b': out[ sz + 1 ] = 'b'; sz += 2; break;
            case '\f': out[ sz + 1 ] = 'f'; sz += 2; break;
            case '\n': out[ sz + 1 ] = 'n'; sz += 2; break;
            case '\r': out[ sz + 1 ] = 'r'; sz += 2; break;
            case '\t': out[ sz + 1 ] = 't'; sz += 2; break;
            default:   out[ sz + 1 ] = 'u';
                       out[ sz + 2 ] = '0';
                       out[ sz + 3 ] = '0' + ( ( (uint8_t) s[ i ] / 100 ) % 10 );
                       out[ sz + 4 ] = '0' + ( ( (uint8_t) s[ i ] / 10 ) % 10 );
                       out[ sz + 5 ] = '0' + ( (uint8_t) s[ i ] % 10 );
                       sz += 6; break;
          }
        }
        if ( sz > 120 ) {
          fwrite( out, 1, sz, stdout );
          sz = 0;
        }
      }
      if ( use_quotes )
        out[ sz++ ] = '\"';
      if ( sz > 0 ) {
        fwrite( out, 1, sz, stdout );
      }
      break;
    }
    case RDB_DBL_VAL: printf( "%g", str.fval );
  }
}

static void
comma_nl( uint64_t &cnt )
{
  if ( cnt++ != 0 )
    fputs( ",\n", stdout );
}

void JsonOutput::d_idle( uint64_t i ) noexcept {
  if ( ! this->show_meta ) return;
  comma_nl( this->d_cnt );
  printf( "\"idle\" : %" PRIu64 "", i );
}
void JsonOutput::d_freq( uint8_t f ) noexcept {
  if ( ! this->show_meta ) return;
  comma_nl( this->d_cnt );
  printf( "\"freq\" : %u", f );
}
void JsonOutput::d_aux( const RdbString &var,  const RdbString &val ) noexcept {
  if ( ! this->show_meta ) return;
  comma_nl( this->d_cnt );
  print_s( var ); printf( " : " ); print_s( val );
}
void JsonOutput::d_dbresize( uint64_t i,  uint64_t j ) noexcept {
  if ( ! this->show_meta ) return;
  comma_nl( this->d_cnt );
  printf( "\"dbresize\" : [%" PRIu64 ", %" PRIu64 "]", i, j );
}
void JsonOutput::d_expired_ms( uint64_t ms ) noexcept {
  if ( ! this->show_meta ) return;
  comma_nl( this->d_cnt );
  printf( "\"expire_ms\" : %" PRIu64 "", ms );
}
void JsonOutput::d_expired( uint32_t sec ) noexcept {
  if ( ! this->show_meta ) return;
  comma_nl( this->d_cnt );
  printf( "\"expire\" : %u", sec );
}
void JsonOutput::d_dbselect( uint32_t db ) noexcept {
  if ( ! this->show_meta ) return;
  comma_nl( this->d_cnt );
  printf( "\"dbselect\" : %u", db );
}

void
JsonOutput::d_string( const RdbString &str ) noexcept
{
  print_s( str );
}

void
JsonOutput::d_module( const RdbString &str ) noexcept
{
  print_s( str );
}

void
JsonOutput::d_init( void ) noexcept
{
  printf( "{\n" );
}

void
JsonOutput::d_finish( bool success ) noexcept
{
  if ( success )
    printf( "\n}\n" );
  else
    printf( "\n" );
  fflush( stdout );
}

void
JsonOutput::d_start_key( void ) noexcept
{
  const char * fmt = NULL,
             * typ = NULL;

  switch ( this->dec.type ) {
    case RDB_STRING:
      fmt = " : ";
      typ = "string";
      break;
    case RDB_LIST:
    case RDB_LIST_ZIPLIST:
    case RDB_LIST_QUICKLIST:
    case RDB_LIST_QUICKLIST_2:
      fmt = " : [\n";
      typ = "list";
      break;
    case RDB_SET:
    case RDB_SET_INTSET:
      fmt = " : [\n";
      typ = "set";
      break;
    case RDB_ZSET:
    case RDB_ZSET_2:
    case RDB_ZSET_ZIPLIST:
    case RDB_ZSET_LISTPACK:
      fmt = " : {\n";
      typ = "zset";
      break;
    case RDB_HASH:
    case RDB_HASH_ZIPMAP:
    case RDB_HASH_ZIPLIST:
    case RDB_HASH_LISTPACK:
      fmt = " : {\n";
      typ = "hash";
      break;
    case RDB_STREAM_LISTPACK:
    case RDB_STREAM_LISTPACKS_2:
      fmt = " : {\n";
      typ = "stream";
      break;
    case RDB_MODULE_2:
    case RDB_MODULE:
      fmt = " : ";
      typ = "module";
      break;
    default:
      break;
  }
  if ( fmt != NULL ) {
    comma_nl( this->d_cnt );
    if ( this->dec.key.coding != RDB_NO_VAL ) {
      if ( this->dec.key.coding != RDB_STR_VAL )
        printf( "\"" );
      print_s( this->dec.key );
      if ( this->dec.key.coding != RDB_STR_VAL )
        printf( "\"" );
    }
    else
      printf( "\"%s\"", typ );
    fputs( fmt, stdout );
  }
}

void
JsonOutput::d_end_key( void ) noexcept
{
  switch ( this->dec.type ) {
    case RDB_STRING:
    case RDB_MODULE_2:
    case RDB_MODULE:
      break;
    case RDB_LIST:
    case RDB_LIST_ZIPLIST:
    case RDB_LIST_QUICKLIST:
    case RDB_LIST_QUICKLIST_2:
      printf( "\n]" );
      break;
    case RDB_SET:
    case RDB_SET_INTSET:
      printf( "\n]" );
      break;
    case RDB_ZSET:
    case RDB_ZSET_2:
    case RDB_ZSET_ZIPLIST:
    case RDB_ZSET_LISTPACK:
      printf( "\n}" );
      break;
    case RDB_HASH:
    case RDB_HASH_ZIPMAP:
    case RDB_HASH_ZIPLIST:
    case RDB_HASH_LISTPACK:
      printf( "\n}" );
      break;
    case RDB_STREAM_LISTPACK:
    case RDB_STREAM_LISTPACKS_2:
      printf( "\n}" );
      break;
    default:
      break;
  }
}

static void
tab( bool test,  const char *s,  size_t n ) {
  if ( test )
    printf( "%s", s );
  for ( ; n > 0; n-- )
    printf( "  " );
}

void
JsonOutput::d_hash( const RdbHashEntry &h ) noexcept
{
  tab( h.num != 0, ",\n", 1 );
  print_s( h.field ); printf( " : " ); print_s( h.val );
}

void
JsonOutput::d_list( const RdbListElem &l ) noexcept
{
  tab( l.num != 0, ",\n", 1 );
  print_s( l.val );
}

void
JsonOutput::d_set( const RdbSetMember &s ) noexcept
{
  tab( s.num != 0, ",\n", 1 );
  print_s( s.member );
}

void
JsonOutput::d_zset( const RdbZSetMember &z ) noexcept
{
  tab( z.num != 0, ",\n", 1 );
  print_s( z.member ); printf( " : " ); print_s( z.score );
}

void
JsonOutput::d_stream_entry( const RdbStreamEntry &entry ) noexcept
{ 
  tab( entry.num != 0, ",\n", 2 );
  printf( "{ \"id\" : \"%" PRIu64 "-%" PRIu64 "\", ", entry.id.ms + entry.diff.ms,
                         entry.id.ser + entry.diff.ser );
  for ( size_t i = 0; i < entry.entry_field_count; i++ ) {
    RdbListValue & f = entry.fields[ i ],
                 & v = entry.values[ i ];
    if ( i > 0 )
      printf( ", " );
    if ( f.data != NULL )
      printf( "\"%.*s\" : ", (int) f.data_len, (char *) f.data );
    else
      printf( "\"%" PRId64 "\" : ", f.ival );
    if ( v.data != NULL )
      printf( "\"%.*s\"", (int) v.data_len, (char *) v.data );
    else
      printf( "%" PRId64 "", v.ival );
  }
  printf( " }" );
}

void
JsonOutput::d_stream_info( const RdbStreamInfo &info ) noexcept
{
  printf( "  \"last_id\" : \"%" PRIu64 "-%" PRIu64 "\",\n"
          "  \"num_elems\" : %" PRIu64 ",\n"
          "  \"num_cgroups\" : %" PRIu64 "",
          info.last.ms, info.last.ser,
          info.num_elems, info.num_cgroups );
}

void
JsonOutput::d_stream_start( StreamPart c ) noexcept
{
  switch ( c ) {
    case STREAM_ENTRY_LIST:
      tab( false, NULL, 1 );
      printf( "\"entries\" : [\n" );
      break;
    case STREAM_GROUP_LIST:
      printf( ",\n" );
      tab( false, NULL, 1 );
      printf( "\"groups\" : [\n" );
      break;
    case STREAM_PENDING_LIST:
      printf( ",\n" );
      tab( false, NULL, 2 );
      printf( "\"pel\" : [\n" );
      break;
    case STREAM_CONSUMER_LIST:
      printf( ",\n" );
      tab( false, NULL, 3 );
      printf( "\"consumers\" : [\n" );
      break;
    case STREAM_CONSUMER_PENDING_LIST:
      printf( ",\n" );
      tab( false, NULL, 4 );
      printf( "\"pel\" : [\n" );
      break;
    case STREAM_CONSUMER: /* these have record data */
    case STREAM_GROUP:
      break;
  }
}

void
JsonOutput::d_stream_end( StreamPart c ) noexcept
{
  switch ( c ) {
    case STREAM_ENTRY_LIST:
      printf( " ],\n" );
      break;
    case STREAM_GROUP_LIST:
      printf( " ]" );
      break;
    case STREAM_PENDING_LIST:
      printf( " ]" );
      break;
    case STREAM_CONSUMER_LIST:
      printf( " ]" );
      break;
    case STREAM_CONSUMER_PENDING_LIST:
      printf( " ]" );
      break;
    case STREAM_CONSUMER:
      printf( " }" );
      break;
    case STREAM_GROUP:
      printf( " }" );
      break;
  }
}

void
JsonOutput::d_stream_group( const RdbGroupInfo &group ) noexcept
{
  tab( group.num != 0, ",\n", 2 );
  printf( "{ \"group\" : \"%.*s\","
           " \"pending\" : %" PRIu64 ","
           " \"last_id\" : \"%" PRIu64 "-%" PRIu64 "\"",
          (int) group.gname_len, group.gname, group.pending_cnt,
          group.last.ms, group.last.ser );
}

void
JsonOutput::d_stream_pend( const RdbPendInfo &pend ) noexcept
{
  tab( pend.num != 0, ",\n", 4 );
  printf( "{ \"id\" : \"%" PRIu64 "-%" PRIu64 "\","
           " \"last_d\" : %" PRIu64 ","
           " \"d_cnt\" : %" PRIu64 " }",
          pend.id.ms, pend.id.ser, pend.last_delivery, pend.delivery_cnt );
}

void
JsonOutput::d_stream_cons( const RdbConsumerInfo &cons ) noexcept
{
  tab( cons.num != 0, ",\n", 4 );
  printf( "{ \"name\" : \"%.*s\","
           " \"pending\" : %" PRIu64 ","
           " \"last_seen\" : %" PRIu64 "",
        (int) cons.cname_len, cons.cname, cons.pend_cnt, cons.last_seen );
}

void
JsonOutput::d_stream_cons_pend( const RdbConsPendInfo &pend ) noexcept
{
  tab( pend.num != 0, ",\n", 5 );
  printf( "\"%" PRIu64 "-%" PRIu64 "\"", pend.id.ms, pend.id.ser );
}

