packages:=boost openssl libevent zeromq
native_packages := native_ccache

qt_native_packages = native_protobuf
qt_packages = qrencode protobuf

qt_linux_packages:=qt expat libxcb xcb_proto libXau xproto freetype fontconfig libxkbcommon libxcb_util libxcb_util_render libxcb_util_keysyms libxcb_util_image libxcb_util_wm

qt_darwin_packages=qt
qt_mingw32_packages=qt

wallet_packages=bdb

upnp_packages=miniupnpc

darwin_native_packages = native_biplist native_ds_store native_mac_alias

# The cross build from Linux uses the clang, lld and LLVM tools of the system (hosts/darwin.mk) and
# packs the programs in a zip archive (contrib/aokchain/build-release.sh), not in a disk image: it
# needs neither cctools nor the disk image tools.
ifneq ($(build_os),darwin)
darwin_native_packages =
endif
