package=boost
$(package)_version=1_73_0
$(package)_download_path=https://archives.boost.io/release/$(subst _,.,$($(package)_version))/source/
$(package)_file_name=boost_$($(package)_version).tar.bz2
$(package)_sha256_hash=4eb3b8d442b426dc35346235c8733b5ae35ba431690e38c6a8263dce9fcbb402
$(package)_dependencies=native_b2

define $(package)_set_vars
$(package)_config_opts_release=variant=release
$(package)_config_opts_debug=variant=debug
$(package)_config_opts=--layout=tagged --build-type=complete --user-config=user-config.jam
$(package)_config_opts+=threading=multi link=static -sNO_COMPRESSION=1
$(package)_config_opts_linux=target-os=linux threadapi=pthread runtime-link=shared
$(package)_config_opts_darwin=target-os=darwin runtime-link=shared
$(package)_config_opts_mingw32=target-os=windows binary-format=pe threadapi=win32 runtime-link=static
$(package)_config_opts_x86_64=architecture=x86 address-model=64
$(package)_config_opts_i686=architecture=x86 address-model=32
$(package)_config_opts_aarch64=address-model=64
$(package)_config_opts_armv7a=address-model=32
$(package)_config_opts_i686_android=address-model=32
$(package)_config_opts_aarch64_android=address-model=64
$(package)_config_opts_x86_64_android=address-model=64
$(package)_config_opts_armv7a_android=address-model=32
unary_function=unary_function
ifneq (,$(findstring clang,$($(package)_cxx)))
$(package)_toolset_$(host_os)=clang
ifeq ($(build_os),darwin)
unary_function=__unary_function
endif
else
$(package)_toolset_$(host_os)=gcc
endif
$(package)_config_libraries=chrono,filesystem,program_options,system,thread,test
$(package)_cxxflags=-std=c++17 -fvisibility=hidden -Wno-enum-constexpr-conversion
$(package)_cxxflags_linux=-fPIC
$(package)_cxxflags_freebsd=-fPIC
$(package)_cxxflags_openbsd=-fPIC
$(package)_cxxflags_android=-fPIC
endef

# Fix unused variable in boost_process, can be removed after upgrading to 1.72
# Fix missing unary_function in clang15 on macos, can be removed after upgrading to 1.81
define $(package)_preprocess_cmds
  sed -i.old "s/int ret_sig = 0;//" boost/process/detail/posix/wait_group.hpp && \
  sed -i.old "s/unary_function/$(unary_function)/" boost/container_hash/hash.hpp && \
  echo "using $($(package)_toolset_$(host_os)) : : $($(package)_cxx) : <cflags>\"$($(package)_cflags)\" <cxxflags>\"$($(package)_cxxflags)\" <compileflags>\"$($(package)_cppflags)\" <linkflags>\"$($(package)_ldflags)\" <archiver>\"$($(package)_ar)\" <striper>\"$(host_STRIP)\"  <ranlib>\"$(host_RANLIB)\" <rc>\"$(host_WINDRES)\" : ;" > user-config.jam
endef

define $(package)_config_cmds
  ./bootstrap.sh --without-icu --with-libraries=$($(package)_config_libraries) --with-toolset=$($(package)_toolset_$(host_os)) --with-bjam=b2
endef

define $(package)_build_cmds
  b2 -d2 -j2 -d1 --prefix=$($(package)_staging_prefix_dir) $($(package)_config_opts) toolset=$($(package)_toolset_$(host_os)) stage
endef

define $(package)_stage_cmds
  b2 -d0 -j4 --prefix=$($(package)_staging_prefix_dir) $($(package)_config_opts) toolset=$($(package)_toolset_$(host_os)) install
endef
