#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <rdbparser/rdb_decode.h>
#include <rdbparser/rdb_restore.h>

using namespace rdbparser;

void RestoreOutput::d_expired_ms( uint64_t ms ) noexcept { this->ttl_ms = ms; }
void RestoreOutput::d_idle( uint64_t i ) noexcept        { this->idle = i; }
void RestoreOutput::d_freq( uint8_t f ) noexcept         { this->freq = f; }
void RestoreOutput::d_start_key( void ) noexcept    { this->is_matched = true; }

void
RestoreOutput::d_start_type( RdbType ) noexcept
{
  this->is_matched = false; /* is_matched set when d_start_key() called */
  this->type_offset = this->bptr.offset;
}

void
RestoreOutput::write_restore_cmd( void ) noexcept
{
  if ( ! this->is_matched ) { /* if not filtered */
    this->reset_state();
    return;
  }
  size_t end   = this->bptr.offset - this->bptr.start_offset,
         start = this->type_offset - this->bptr.start_offset;

  if ( this->type_offset < this->bptr.start_offset ||
       this->bptr.offset < this->bptr.start_offset ||
       start >= end ) {
    this->reset_state();
    fprintf( stderr, "Buffer does not contain key!!\n" );
    return;
  }
  RdbString     & key = this->dec.key;
  const uint8_t * buf = &this->bptr.buf[ -(int64_t) this->bptr.offset ];
  RdbLength       len;
  char            tmp[ 16 ]; /* snprintf() */
  int             n;
  uint64_t        crc;
  
  if ( this->dec.is_rdb_file ) /* skip over type and key */
    start += 1 + len.decode_buf( &buf[ start + 1 ] ) + key.s_len;
  else
    start += 1; /* no key in dump */

  /* command to write: RESTORE key ttl <type><data><ver><crc> */
  /* write the restore */
  static const char use_repl[] = "*5";
  static const char restore[]  = "*4\r\n$7\r\nRESTORE\r\n";
  if ( this->use_replace ) {
    fwrite( use_repl, 1, 2, stdout );
    fwrite( &restore[ 2 ], 1, sizeof( restore ) - 3, stdout );
  }
  else {
    fwrite( restore, 1, sizeof( restore ) - 1, stdout );
  }
  /* write the key */
  n = snprintf( tmp, sizeof( tmp ), "$%" PRId64 "\r\n", key.s_len );
  fwrite( tmp, 1, n, stdout );
  fwrite( key.s, 1, key.s_len, stdout );

  /* write the ttl (0) (plus linefeed for key) */
  static const char ttl_0[] = "\r\n$1\r\n0\r\n";
  fwrite( ttl_0, 1, sizeof( ttl_0 ) - 1, stdout );
  
  /* write the data length: <type><data><ver><crc> */
  n = snprintf( tmp, sizeof( tmp ), "$%" PRId64 "\r\n", end - start + 1 + 10 );
  fwrite( tmp, 1, n, stdout ); /* $len\r\n */

  /* write the type byte */
  fwrite( &buf[ this->type_offset ], 1, 1, stdout );
  crc = jones_crc64( 0, &buf[ this->type_offset ], 1 );

  /* write the data body */
  fwrite( &buf[ start ], 1, end - start, stdout );
  crc = jones_crc64( crc, &buf[ start ], end - start );

  /* write the version 9 */
  static uint8_t ver[ 2 ] = { 0x09, 0x00 };
  fwrite( ver, 1, 2, stdout );
  crc = jones_crc64( crc, ver, 2 );

  /* write the crc */
  fwrite( &crc, 1, 8, stdout );
  fwrite( "\r\n", 1, 2, stdout );
  if ( this->use_replace ) {
    static const char repl[] = "$7\r\nREPLACE\r\n";
    fwrite( repl, 1, sizeof( repl ) - 1, stdout );
  }
  fflush( stdout );

  this->reset_state();
}

