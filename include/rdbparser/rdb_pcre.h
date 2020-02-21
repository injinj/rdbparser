#ifndef __rdbparser__rdb_pcre_h__
#define __rdbparser__rdb_pcre_h__

#ifdef __cplusplus

extern "C" {
  struct pcre2_real_code_8;
  struct pcre2_real_match_data_8;
}

namespace rdbparser {

/* filter keys by pcre */
struct PcreFilter : public RdbFilter {
  char                      name[ 40 ];  /* if expr is simple string match */
  size_t                    name_len;    /* len of name to match */
  pcre2_real_code_8       * re;          /* pcre regex compiled */
  pcre2_real_match_data_8 * md;          /* pcre match context  */
  bool                      ignore_case, /* like grep -i */
                            invert;      /* invert match, like grep -v */

  PcreFilter( RdbDecode &dec ) : RdbFilter( dec ), name_len( 0 ), re( 0 ),
                             md( 0 ), ignore_case( false ), invert( false ) {}
  /* return false if expr failed to compile */
  bool set_filter_expr( const char *expr,  size_t expr_len,
                        bool ign_case,  bool inv ) noexcept;
  /* return true if key matched */
  virtual bool match_key( const RdbString &key ) noexcept;
};

} // namespace
#endif
#endif
