package=openssl
$(package)_version=3.0.15
$(package)_download_path=https://www.openssl.org/source
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=23c666d0edf20f14249b3d8f0368acaee9ab585b09e1de82107c66e1f3ec9533

# OpenSSL is only used for the RNG seeding in random.cpp/util.cpp and by the
# Qt GUI (BIP70). Build a minimal static libcrypto/libssl.
define $(package)_set_vars
$(package)_config_env=AR="$($(package)_ar)" RANLIB="$($(package)_ranlib)" CC="$($(package)_cc)"
$(package)_config_opts=--prefix=$(host_prefix) --openssldir=$(host_prefix)/etc/openssl --libdir=lib
$(package)_config_opts+=no-shared
$(package)_config_opts+=no-dso
$(package)_config_opts+=no-engine
$(package)_config_opts+=no-tests
$(package)_config_opts+=no-comp
$(package)_config_opts+=no-zlib
$(package)_config_opts+=no-ssl3
$(package)_config_opts+=no-weak-ssl-ciphers
$(package)_config_opts+=no-camellia
$(package)_config_opts+=no-idea
$(package)_config_opts+=no-md2
$(package)_config_opts+=no-mdc2
$(package)_config_opts+=no-rc5
$(package)_config_opts+=no-seed
$(package)_config_opts+=no-whirlpool
$(package)_config_opts+=no-ssl-trace
$(package)_config_opts+=no-sctp
$(package)_config_opts+=no-rfc3779
$(package)_config_opts+=no-capieng
$(package)_config_opts+=$($(package)_cflags) $($(package)_cppflags)
$(package)_config_opts_linux=-fPIC -Wa,--noexecstack
$(package)_config_opts_x86_64_linux=linux-x86_64
$(package)_config_opts_i686_linux=linux-x86
$(package)_config_opts_arm_linux=linux-armv4
$(package)_config_opts_armv7l_linux=linux-armv4
$(package)_config_opts_aarch64_linux=linux-aarch64
$(package)_config_opts_mipsel_linux=linux-mips32
$(package)_config_opts_mips_linux=linux-mips32
$(package)_config_opts_powerpc_linux=linux-ppc
$(package)_config_opts_x86_64_darwin=darwin64-x86_64-cc
$(package)_config_opts_x86_64_mingw32=mingw64
$(package)_config_opts_i686_mingw32=mingw
endef

define $(package)_config_cmds
  ./Configure $($(package)_config_opts)
endef

define $(package)_build_cmds
  $(MAKE) build_libs
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install_dev
endef

define $(package)_postprocess_cmds
  rm -rf share bin etc
endef
