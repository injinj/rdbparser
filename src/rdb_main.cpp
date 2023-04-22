#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS /* for freopen() */
#endif
#if defined( _MSC_VER ) || defined( __MINGW32__ )
#define RDB_WINDOWS 1
#endif
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#ifndef RDB_WINDOWS
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#else
#include <windows.h>
#endif
#include <rdbparser/rdb_decode.h>
#include <rdbparser/rdb_json.h>
#include <rdbparser/rdb_restore.h>
#include <rdbparser/rdb_pcre.h>

using namespace rdbparser;

static const char *
get_err_description( RdbErrCode err )
{
  switch ( err ) {
    default:
    case RDB_OK:          return "ok";
    case RDB_EOF_MARK:    return "eof";
    case RDB_ERR_OUTPUT:  return "No output";
    case RDB_ERR_TRUNC:   return "Input truncated";
    case RDB_ERR_VERSION: return "Rdb version too old";
    case RDB_ERR_CRC:     return "Crc does not match";
    case RDB_ERR_TYPE:    return "Error unknown type";
    case RDB_ERR_HDR:     return "Error parsing header";
    case RDB_ERR_LZF:     return "Bad LZF compression";
    case RDB_ERR_NOTSUP:  return "Object type not supported";
    case RDB_ERR_ZLEN:    return "Zip list len mismatch";
  }
}

/* display hex bytes around the error */
static void
show_error( RdbBufptr &bptr,  const uint8_t *start,  const uint8_t *end )
{
  if ( bptr.buf < start || bptr.buf > end ) {
    start = (const uint8_t *) (void *) &bptr.alloced_mem[ 1 ];
    end = &bptr.buf[ bptr.avail ];
  }
  if ( bptr.buf >= start && bptr.buf < end ) {
    char tmp[ 64 ];
    size_t off     = bptr.buf - start,
           end_off = end - start;
    snprintf( tmp, sizeof( tmp ), "offset %" PRIu64 " (0x%" PRIx64 ")", off, off );
    if ( off < 256 )
      off = 0;
    else
      off -= 256;
    off &= ~(size_t) 15;
    if ( end_off > off + 512 )
      end_off = off + 512;
    printf( "%" PRIx64 " -> %" PRIx64 "\n", off, end_off );
    print_hex( tmp, off, end_off, start );
  }
}

/* only output key, ignore data */
struct ListOutput : public RdbOutput {
  uint64_t list_cnt;
  ListOutput( RdbDecode &dec ) : RdbOutput( dec ), list_cnt( 0 ) {}

  virtual void d_start_key( void ) noexcept {
    this->list_cnt++;
    print_s( this->dec.key, false ); printf( "\n" );
  }
};

/* buffer for stdin input */
static uint8_t big_buf[ 10 * 1024 * 1024 ],
             * input_buf = big_buf;
static size_t  input_off,
               input_buf_size = sizeof( big_buf );
static bool    input_eof = true;

static void
fill_buf_stdin( void )
{
#ifdef RDB_WINDOWS
  freopen( NULL, "rb", stdin );
#endif
  for (;;) {
    size_t n = fread( &input_buf[ input_off ], 1,
                      input_buf_size - input_off, stdin );
    if ( n == 0 ) {
      input_eof = true;
      return;
    }
    input_off += n;
    if ( input_off == input_buf_size ) {
      /* if not an rdb file, is a dump, read it all */
      if ( input_eof && ::memcmp( input_buf, "REDIS00", 7 ) != 0 ) {
#if 0
        if ( input_buf_size > 512 * 1024 * 1024 + 1024 ) {
          max redis object size, too large of an object !!
        }
#endif
        /* increase buf size */
        if ( input_buf == big_buf ) {
          input_buf = (uint8_t *) ::malloc( input_buf_size * 2 );
          if ( input_buf == NULL )
            break;
          ::memcpy( input_buf, big_buf, input_buf_size );
        }
        else {
          input_buf = (uint8_t *) ::realloc( input_buf, input_buf_size * 2 );
          if ( input_buf == NULL )
            break;
        }
        input_buf_size *= 2;
      }
      else {
        input_eof = false;
        return;
      }
    }
  }
#ifndef RDB_WINDOWS
  ::perror( "malloc" );
#else
  fprintf( stderr, "err malloc: %ld\n", GetLastError() );
#endif
  exit( 1 );
}

static const char *
get_arg( int argc, char *argv[], int b, const char *f, const char *def )
{
  for ( int i = 1; i < argc - b; i++ )
    if ( ::strcmp( f, argv[ i ] ) == 0 ) /* -e pat */
      return argv[ i + b ];
  return def; /* default value */
}

int
main( int argc, char *argv[] )
{
  const char * glob     = get_arg( argc, argv, 1, "-e", NULL ),
             * invert   = get_arg( argc, argv, 1, "-v", NULL ),
             * ign_case = get_arg( argc, argv, 0, "-i", NULL ),
             * fn       = get_arg( argc, argv, 1, "-f", NULL ),
             * meta     = get_arg( argc, argv, 0, "-m", NULL ),
             * list     = get_arg( argc, argv, 0, "-l", NULL ),
             * restore  = get_arg( argc, argv, 0, "-r", NULL ),
             * help     = get_arg( argc, argv, 0, "-h", NULL );
  if ( help != NULL ) {
    printf( "%s [-e pat] [-v] [-i] [-f file]\n"
            "   -e pat  : match key with glob pattern\n"
            "   -v      : invert key match\n"
            "   -i      : ignore key match case\n"
            "   -f file : dump rdb file to read\n"
            "   -m      : show meta data in json output\n"
            "   -l      : list keys which match\n"
            "   -r      : write restore commands | redis-cli --pipe\n"
            "default is to print json of matching data\n"
            "if no file is given, will read data from stdin\n", argv[ 0 ] );
    return 0;
  }

  RdbDecode  decode;
  PcreFilter pcre_filter( decode );
  void     * map = NULL;

  /* set up key filter */
  if ( glob != NULL ) {
    if ( ! pcre_filter.set_filter_expr( glob, ::strlen( glob ),
                                        ( ign_case != NULL ),
                                        ( invert != NULL ) ) ) {
      fprintf( stderr, "pcre filter failed\n" );
      return 1;
    }
    decode.filter = &pcre_filter;
  }

  /* map the file, if filename given */
  if ( fn != NULL ) {
#ifndef RDB_WINDOWS
    int fd = ::open( fn, O_RDONLY );
    struct stat st;
    if ( fd < 0 ) {
      ::perror( fn );
      return 1;
    }
    if ( ::fstat( fd, &st ) != 0 ) {
      ::perror( "fstat" );
      ::close( fd );
      return 1;
    }
    input_off = st.st_size;
    map = ::mmap( 0, input_off, PROT_READ, MAP_SHARED, fd, 0 );
    if ( map == MAP_FAILED ) {
      ::perror( "mmap" );
      ::close( fd );
      return 1;
    }
    ::close( fd );
    if ( ::madvise( map, input_off, MADV_SEQUENTIAL ) != 0 )
      ::perror( "madvise" );
#else
    HANDLE h = CreateFileA( fn, GENERIC_READ, 0, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    LARGE_INTEGER st;
    if ( h == INVALID_HANDLE_VALUE ) {
      fprintf( stderr, "err open %s: %ld\n", fn, GetLastError() );
      return 1;
    }
    GetFileSizeEx( h, &st );
    input_off = st.QuadPart;
    HANDLE maph = CreateFileMappingA( h, NULL, PAGE_READONLY, 0, 0, NULL );
    if ( maph == NULL ) {
      fprintf( stderr, "err map %s: %ld\n", fn, GetLastError() );
      CloseHandle( h );
      return 1;
    }
    map = MapViewOfFile( maph, FILE_MAP_READ, 0, 0, 0 );
    if ( map == NULL ) {
      fprintf( stderr, "err view %s: %ld\n", fn, GetLastError() );
      CloseHandle( h );
      CloseHandle( maph );
      return 1;
    }
    CloseHandle( h );
    CloseHandle( maph );
#endif
    input_buf = (uint8_t *) map;
  }
  /* load stdin buffer */
  else {
    fill_buf_stdin();
  }

  RdbBufptr     bptr( input_buf, input_off );
  JsonOutput    json_out( decode );
  ListOutput    list_out( decode );
  RestoreOutput rest_out( decode, bptr, true );

  /* set up the output */
  if ( list != NULL )
    decode.data_out = &list_out;
  else if ( restore != NULL ) {
#ifdef RDB_WINDOWS
    freopen( NULL, "wb", stdout );
#endif
    decode.data_out = &rest_out;
  }
  else {
    decode.data_out = &json_out;
    json_out.show_meta = ( meta != NULL );
  }
  decode.data_out->d_init();

  /* loop through the keys */
  for (;;) {
    RdbErrCode err = decode.decode_hdr( bptr ); /* find type, length and key */
    if ( err == RDB_OK )
      err = decode.decode_body( bptr );         /* decode the data type */
    if ( err != RDB_OK ) {
      if ( err == RDB_EOF_MARK )             /* 0xff marker found */
        goto break_loop;
      decode.data_out->d_finish( false );
      fprintf( stderr, "%s\n", get_err_description( err ) );
      show_error( bptr, input_buf, &input_buf[ input_off ] );
      return 1;
    }
    decode.key_cnt++;
    /* release lzf decompress allocations */
    if ( bptr.alloced_mem != NULL )
      bptr.free_alloced();
    if ( decode.data_out == &rest_out )
      rest_out.write_restore_cmd();
    /* fill more buffer from stdin */
    if ( ! input_eof && bptr.offset > input_buf_size / 2 ) {
      ::memmove( input_buf, bptr.buf, bptr.avail );
      bptr.buf           = input_buf;
      bptr.start_offset += bptr.offset;
      input_off          = bptr.avail;
      bptr.offset        = 0;
      fill_buf_stdin();
      bptr.avail         = input_off;
    }
    if ( bptr.avail == 0 ) /* no data left */
      break;
  }
break_loop:;
  decode.data_out->d_finish( true );
#ifndef RDB_WINDOWS
  if ( map != NULL )
    ::munmap( map, input_off );
#else
  if ( map != NULL )
    UnmapViewOfFile( map );
#endif
  else if ( input_buf != big_buf )
    ::free( input_buf );
  return 0;
}
