package=miniupnpc
$(package)_version=2.2.7
$(package)_download_path=http://miniupnp.free.fr/files
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=b0c3a27056840fd0ec9328a5a9bac3dc5e0ec6d2e8733349cf577b0aa1e70ac1

# 2.2.7 still uses MINIUPNPC_API_VERSION 17 (src/net.cpp also supports 18+).
# 2.2.x fixes CVE-2024-52917 class issues (infinite loop on malformed replies).
define $(package)_set_vars
$(package)_build_opts=CC="$($(package)_cc)"
# No OS=... here: the Makefile reads the OS from "$(CC) -dumpmachine" and matches a lowercase
# "darwin" in it. Passing OS=Darwin (what the recipe of an older miniupnpc needed, when the
# Makefile read "uname -s") matches neither of its tests, so it would add -D_XOPEN_SOURCE=600
# and leave out -D_DARWIN_C_SOURCE. On macOS the first defines _POSIX_C_SOURCE, which without
# the second hides struct ifreq, IFNAMSIZ and IP_MULTICAST_IF, and minissdpc.c does not build.
$(package)_build_opts_darwin=LIBTOOL="$($(package)_libtool)"
$(package)_build_opts_mingw32=-f Makefile.mingw
# Makefile.mingw builds the static library in the top-level directory.
$(package)_libfile=build/libminiupnpc.a
$(package)_libfile_mingw32=libminiupnpc.a
$(package)_build_env+=CFLAGS="$($(package)_cflags) $($(package)_cppflags)" AR="$($(package)_ar)"
endef

define $(package)_build_cmds
	$(MAKE) $(or $($(package)_libfile_$(host_os)),$($(package)_libfile)) $($(package)_build_opts)
endef

define $(package)_stage_cmds
	mkdir -p $($(package)_staging_prefix_dir)/include/miniupnpc $($(package)_staging_prefix_dir)/lib &&\
	install include/*.h $($(package)_staging_prefix_dir)/include/miniupnpc &&\
	install $(or $($(package)_libfile_$(host_os)),$($(package)_libfile)) $($(package)_staging_prefix_dir)/lib
endef
