lsb_dist     := $(shell if [ -f /etc/redhat-release ] ; then \
                  cat /etc/redhat-release | sed 's/ .*//' ; \
                  elif [ -f /etc/os-release ] ; then \
                  grep '^NAME=' /etc/os-release | sed 's/.*=\"//' | sed 's/ .*//' ; \
                  elif [ -x /usr/bin/lsb_release ] ; then \
                  lsb_release -is ; else echo Linux ; fi)
lsb_dist_ver := $(shell if [ -f /etc/redhat-release ] ; then \
                  cat /etc/redhat-release | sed s/.*release\ // | sed s/\ .*// ; \
                  elif [ -f /etc/os-release ] ; then \
                  grep '^VERSION=' /etc/os-release | sed 's/.*=\"//' | sed 's/ .*//' ; \
                  elif [ -x /usr/bin/lsb_release ] ; then \
                  lsb_release -rs | sed 's/[.].*//' ; else uname -r | sed 's/[-].*//' ; fi)
#lsb_dist     := $(shell if [ -x /usr/bin/lsb_release ] ; then lsb_release -is ; else uname -s ; fi)
#lsb_dist_ver := $(shell if [ -x /usr/bin/lsb_release ] ; then lsb_release -rs | sed 's/[.].*//' ; else uname -r | sed 's/[-].*//' ; fi)
uname_m      := $(shell uname -m)

short_dist_lc := $(patsubst CentOS,rh,$(patsubst RedHatEnterprise,rh,\
                   $(patsubst RedHat,rh,\
                     $(patsubst Fedora,fc,$(patsubst Ubuntu,ub,\
                       $(patsubst Debian,deb,$(patsubst SUSE,ss,$(lsb_dist))))))))
short_dist    := $(shell echo $(short_dist_lc) | tr a-z A-Z)
pwd           := $(shell pwd)
rpm_os        := $(short_dist_lc)$(lsb_dist_ver).$(uname_m)

# this is where the targets are compiled
build_dir ?= $(short_dist)$(lsb_dist_ver)_$(uname_m)$(port_extra)
bind      := $(build_dir)/bin
libd      := $(build_dir)/lib64
objd      := $(build_dir)/obj
dependd   := $(build_dir)/dep

default_cflags := -ggdb -O3
# use 'make port_extra=-g' for debug build
ifeq (-g,$(findstring -g,$(port_extra)))
  default_cflags := -ggdb
endif
ifeq (-a,$(findstring -a,$(port_extra)))
  default_cflags := -fsanitize=address -ggdb -O3
endif

CC          ?= gcc
CXX         ?= $(CC) -x c++
cpp         := $(CXX)
# if not linking libstdc++
ifdef NO_STL
cppflags    := -std=c++11 -fno-rtti -fno-exceptions
cpplink     := $(CC)
else
cppflags    := -std=c++11
cpplink     := $(CXX)
endif
#cppflags  := -fno-rtti -fno-exceptions -fsanitize=address
#cpplink   := $(CC) -lasan
arch_cflags := -fno-omit-frame-pointer
gcc_wflags  := -Wall -Wextra -Werror
fpicflags   := -fPIC
soflag      := -shared

# rpmbuild uses RPM_OPT_FLAGS
ifeq ($(RPM_OPT_FLAGS),)
CFLAGS ?= $(default_cflags)
else
CFLAGS ?= $(RPM_OPT_FLAGS)
endif
cflags := $(gcc_wflags) $(CFLAGS) $(arch_cflags)

INCLUDES   ?= -Iinclude -I/usr/include/liblzf
DEFINES    ?=
includes   := $(INCLUDES)
defines    := $(DEFINES)

rpath      := -Wl,-rpath,$(pwd)/$(libd)
cpp_lnk    :=
lnk_lib    := -lpcre2-8 -llzf

.PHONY: everything
everything: all

# copr/fedora build (with version env vars)
# copr uses this to generate a source rpm with the srpm target
-include .copr/Makefile

# debian build (debuild)
# target for building installable deb: dist_dpkg
-include deb/Makefile

all_exes    :=
all_libs    :=
all_dlls    :=
all_depends :=

librdbparser_files := rdb_decode rdb_json rdb_restore rdb_pcre
librdbparser_cfile := $(addprefix src/, $(addsuffix .cpp, $(librdbparser_files)))
librdbparser_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(librdbparser_files)))
librdbparser_dbjs  := $(addprefix $(objd)/, $(addsuffix .fpic.o, $(librdbparser_files)))
librdbparser_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(librdbparser_files))) \
                     $(addprefix $(dependd)/, $(addsuffix .fpic.d, $(librdbparser_files)))
librdbparser_dlnk  := $(lnk_lib)
librdbparser_spec  := $(version)-$(build_num)_$(git_hash)
librdbparser_ver   := $(major_num).$(minor_num)

$(libd)/librdbparser.a: $(librdbparser_objs)
$(libd)/librdbparser.so: $(librdbparser_dbjs)

all_libs    += $(libd)/librdbparser.a
all_dlls    += $(libd)/librdbparser.so
all_depends += $(librdbparser_deps)

rdbp_files := rdb_main
rdbp_cfile := src/rdb_main.cpp
rdbp_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(rdbp_files)))
rdbp_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(rdbp_files)))
rdbp_libs  := $(libd)/librdbparser.a
# statically link it
rdbp_lnk   := $(rdbp_libs) $(lnk_lib)

$(bind)/rdbp: $(rdbp_objs) $(rdbp_libs)

all_exes    += $(bind)/rdbp
all_depends += $(rdbp_deps)

all_dirs := $(bind) $(libd) $(objd) $(dependd)

all: $(all_libs) $(all_dlls) $(all_exes) cmake

.PHONY: cmake
cmake: CMakeLists.txt

.ONESHELL: CMakeLists.txt
CMakeLists.txt: .copr/Makefile
	@cat <<'EOF' > $@
	cmake_minimum_required (VERSION 3.9.0)
	if (POLICY CMP0111)
	  cmake_policy(SET CMP0111 OLD)
	endif ()
	project (rdbparser)
	include_directories (
	  include
	  $${CMAKE_SOURCE_DIR}/lzf/include
	)
	if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
	  add_definitions(/DPCRE2_STATIC)
	  if ($$<CONFIG:Release>)
	    add_compile_options (/arch:AVX2 /GL /std:c11)
	  else ()
	    add_compile_options (/arch:AVX2 /std:c11)
	  endif ()
	  if (NOT TARGET pcre2-8-static)
	    add_library (pcre2-8-static STATIC IMPORTED)
	    set_property (TARGET pcre2-8-static PROPERTY IMPORTED_LOCATION_DEBUG ../pcre2/build/Debug/pcre2-8-staticd.lib)
	    set_property (TARGET pcre2-8-static PROPERTY IMPORTED_LOCATION_RELEASE ../pcre2/build/Release/pcre2-8-static.lib)
	    include_directories (../pcre2/build)
	  else ()
	    include_directories ($${CMAKE_BINARY_DIR}/pcre2)
	  endif ()
	  if (NOT TARGET lzf)
	    add_library (lzf STATIC IMPORTED)
	    set_property (TARGET lzf PROPERTY IMPORTED_LOCATION_DEBUG ../lzf/build/Debug/lzf.lib)
	    set_property (TARGET lzf PROPERTY IMPORTED_LOCATION_RELEASE ../lzf/build/Release/lzf.lib)
	  endif ()
	else ()
	  add_compile_options ($(cflags))
	  if (TARGET pcre2-8-static)
	    include_directories ($${CMAKE_BINARY_DIR}/pcre2)
	  endif ()
	  if (NOT TARGET lzf)
	    add_library (lzf STATIC IMPORTED)
	    set_property (TARGET lzf PROPERTY IMPORTED_LOCATION ../lzf/build/liblzf.a)
	  endif ()
	endif ()
	add_library (rdbparser STATIC $(librdbparser_cfile))
	if (TARGET pcre2-8-static)
	  link_libraries (rdbparser lzf pcre2-8-static)
	else ()
	  link_libraries (rdbparser lzf -lpcre2-8)
	endif ()
	add_executable (rdbp $(rdbp_cfile))
	EOF

# create directories
$(dependd):
	@mkdir -p $(all_dirs)

# remove target bins, objs, depends
.PHONY: clean
clean:
	rm -r -f $(bind) $(libd) $(objd) $(dependd)
	if [ "$(build_dir)" != "." ] ; then rmdir $(build_dir) ; fi

.PHONY: clean_dist
clean_dist:
	rm -rf dpkgbuild rpmbuild

.PHONY: clean_all
clean_all: clean clean_dist

$(dependd)/depend.make: $(dependd) $(all_depends)
	@echo "# depend file" > $(dependd)/depend.make
	@cat $(all_depends) >> $(dependd)/depend.make

ifeq (SunOS,$(lsb_dist))
remove_rpath = rpath -r
else
remove_rpath = chrpath -d
endif
# target used by rpmbuild, dpkgbuild
.PHONY: dist_bins
dist_bins: $(all_libs) $(all_dlls) $(bind)/rdbp
	$(remove_rpath) $(libd)/librdbparser.so
	$(remove_rpath) $(bind)/rdbp

# target for building installable rpm
.PHONY: dist_rpm
dist_rpm: srpm
	( cd rpmbuild && rpmbuild --define "-topdir `pwd`" -ba SPECS/rdbparser.spec )

# force a remake of depend using 'make -B depend'
.PHONY: depend
depend: $(dependd)/depend.make

# dependencies made by 'make depend'
-include $(dependd)/depend.make

ifeq ($(DESTDIR),)
# 'sudo make install' puts things in /usr/local/lib, /usr/local/include
install_prefix ?= /usr/local
else
# debuild uses DESTDIR to put things into debian/libdecnumber/usr
install_prefix = $(DESTDIR)/usr
endif
# this should be 64 for rpm based, /64 for SunOS
install_lib_suffix ?=

install: dist_bins
	install -d $(install_prefix)/lib$(install_lib_suffix)
	install -d $(install_prefix)/bin $(install_prefix)/include/rdbparser
	for f in $(libd)/librdbparser.* ; do \
	if [ -h $$f ] ; then \
	cp -a $$f $(install_prefix)/lib$(install_lib_suffix) ; \
	else \
	install $$f $(install_prefix)/lib$(install_lib_suffix) ; \
	fi ; \
	done
	install $(bind)/rdbp $(install_prefix)/bin
	install -m 644 include/rdbparser/*.h $(install_prefix)/include/rdbparser

$(objd)/%.o: src/%.cpp
	$(cpp) $(cflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.fpic.o: src/%.cpp
	$(cpp) $(cflags) $(fpicflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(libd)/%.a:
	ar rc $@ $($(*)_objs)

$(libd)/%.so:
	$(cpplink) $(soflag) $(rpath) $(cflags) -o $@.$($(*)_spec) -Wl,-soname=$(@F).$($(*)_ver) $($(*)_dbjs) $($(*)_dlnk) $(cpp_dll_lnk) $(sock_lib) $(math_lib) $(thread_lib) $(malloc_lib) $(dynlink_lib) && \
	cd $(libd) && ln -f -s $(@F).$($(*)_spec) $(@F).$($(*)_ver) && ln -f -s $(@F).$($(*)_ver) $(@F)

$(bind)/%:
	$(cpplink) $(cflags) $(rpath) -o $@ $($(*)_objs) -L$(libd) $($(*)_lnk) $(cpp_lnk) $(sock_lib) $(math_lib) $(thread_lib) $(malloc_lib) $(dynlink_lib)

$(dependd)/%.d: src/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).o -MF $@

$(dependd)/%.fpic.d: src/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).fpic.o -MF $@

