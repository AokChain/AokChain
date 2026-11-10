package=miniupnpc
$(package)_version=2.2.7
$(package)_download_path=http://miniupnp.free.fr/files
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=b0c3a27056840fd0ec9328a5a9bac3dc5e0ec6d2e8733349cf577b0aa1e70ac1

# 2.2.7 still uses MINIUPNPC_API_VERSION 17 (src/net.cpp also supports 18+).
# 2.2.x fixes CVE-2024-52917 class issues (infinite loop on malformed replies).
define $(package)_set_vars
$(package)_build_opts=CC="$($(package)_cc)"
$(package)_build_opts_darwin=OS=Darwin LIBTOOL="$($(package)_libtool)"
$(package)_build_opts_mingw32=-f Makefile.mingw
$(package)_build_env+=CFLAGS="$($(package)_cflags) $($(package)_cppflags)" AR="$($(package)_ar)"
endef

define $(package)_build_cmds
	$(MAKE) build/libminiupnpc.a $($(package)_build_opts)
endef

define $(package)_stage_cmds
	mkdir -p $($(package)_staging_prefix_dir)/include/miniupnpc $($(package)_staging_prefix_dir)/lib &&\
	install include/*.h $($(package)_staging_prefix_dir)/include/miniupnpc &&\
	install build/libminiupnpc.a $($(package)_staging_prefix_dir)/lib
endef
