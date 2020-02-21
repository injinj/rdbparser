#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <rdbparser/rdb_decode.h>
#include <rdbparser/rdb_pcre.h>
#include <rdbparser/glob_cvt.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

using namespace rdbparser;

bool
PcreFilter::set_filter_expr( const char *expr,  size_t expr_len,
                             bool ign_case,  bool inv ) noexcept
{
  pcre2_real_code_8       * re = NULL;
  pcre2_real_match_data_8 * md = NULL;
  size_t  erroff = 0;
  int     error  = 0;
  uint8_t patbuf[ 1024 ];
  GlobCvt<uint8_t> gl( patbuf, sizeof( patbuf ) );

  this->invert      = inv;
  this->ignore_case = ign_case;

  if ( expr_len > sizeof( this->name ) ||
       ::memchr( expr, '*', expr_len ) != NULL ||
       ::memchr( expr, '?', expr_len ) != NULL ||
       ::memchr( expr, '[', expr_len ) != NULL ||
       ::memchr( expr, '|', expr_len ) != NULL ) {
    /* make a glob style wildcard into a pcre wildcard */
    error = gl.convert_glob( (uint8_t *) expr, expr_len, true, ign_case );
    if ( error != 0 ) {
      fprintf( stderr, "convert_glob %d\n", error );
      return false;
    }
    re = pcre2_compile( patbuf, gl.off, 0, &error, &erroff, 0 );
    if ( re != NULL ) {
      md = pcre2_match_data_create_from_pattern( re, NULL );
      if ( md == NULL ) {
        pcre2_code_free( re );
        re = NULL;
      }
    }
    if ( re == NULL ) {
      fprintf( stderr, "pcre(%d,%ld): %.*s\n", error, erroff,
               (int) gl.off, (char *) patbuf );
      return false;
    }
    /* wildcard key filter */
    this->re = re;
    this->md = md;
    this->name_len = 0;
    return true;
  }
  /* strcmp key filter */
  ::memcpy( this->name, expr, expr_len );
  this->name_len = expr_len;
  if ( expr_len < sizeof( this->name ) )
    this->name[ expr_len ] = '\0';
  return true;
}

bool
PcreFilter::match_key( const RdbString &key ) noexcept
{
  bool matched = false;

  if ( key.coding == RDB_STR_VAL ) {
    if ( this->name_len != 0 ) {
      matched = ( key.s_len == this->name_len );
      if ( matched ) {
        if ( ! this->ignore_case )
          matched = ( ::memcmp( key.s, name, this->name_len ) == 0 );
        else
          matched = ( ::strncasecmp( key.s, name, this->name_len ) == 0 );
      }
    }
    else {
      matched = ( pcre2_match( this->re, (uint8_t *) key.s, key.s_len, 0, 0,
                               this->md, 0 ) > 0 );
    }
  }
  if ( this->invert )
    return ! matched;
  return matched;
}

