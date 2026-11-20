OSX_MIN_VERSION=11.0

darwin_CFLAGS=-pipe
darwin_CXXFLAGS=$(darwin_CFLAGS)

darwin_release_CFLAGS=-O2
darwin_release_CXXFLAGS=$(darwin_release_CFLAGS)

darwin_debug_CFLAGS=-O1
darwin_debug_CXXFLAGS=$(darwin_debug_CFLAGS)

# On a Mac, builders/darwin.mk sets the compiler and the tools (those of Xcode). What follows is the
# cross build from Linux: the clang, lld and LLVM tools of the system (clang 18 or later; Ubuntu
# 24.04: apt install clang lld llvm) and a macOS SDK. The SDK may not be passed around (Apple's
# license), so it is not in the tree: copy it from a Mac or extract it from Xcode, and give its path
# with OSX_SDK=... (contrib/aokchain/build-release.sh does that). Its version is read from the SDK.
ifneq ($(build_os),darwin)
OSX_SDK ?= $(SDK_PATH)/MacOSX.sdk
ifeq ($(OSX_SDK_VERSION),)
OSX_SDK_VERSION := $(shell python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["Version"])' $(OSX_SDK)/SDKSettings.json 2>/dev/null)
endif
# Qt's mkspec asks for an Xcode version; nothing is taken from Xcode itself.
XCODE_VERSION=15.0
# The ld64 version that clang is told the linker has: it then passes the newer options (lld knows them).
LLD_VERSION=711

llvm_bindir := $(shell llvm-config --bindir 2>/dev/null)
clang_prog := $(shell $(SHELL) $(.SHELLFLAGS) "command -v clang")
clangxx_prog := $(shell $(SHELL) $(.SHELLFLAGS) "command -v clang++")

darwin_AR=$(llvm_bindir)/llvm-ar
darwin_RANLIB=$(llvm_bindir)/llvm-ranlib
darwin_STRIP=$(llvm_bindir)/llvm-strip
darwin_NM=$(llvm_bindir)/llvm-nm
darwin_LIBTOOL=$(llvm_bindir)/llvm-libtool-darwin
darwin_OTOOL=$(llvm_bindir)/llvm-otool
darwin_INSTALL_NAME_TOOL=$(llvm_bindir)/llvm-install-name-tool

# -isysroot ... -nostdlibinc: the headers of the SDK (and clang's own), none of the build system's.
darwin_CC=$(clang_prog) --target=$(host) -isysroot$(OSX_SDK) -nostdlibinc \
            -iwithsysroot/usr/include -iframeworkwithsysroot/System/Library/Frameworks
darwin_CXX=$(clangxx_prog) --target=$(host) -isysroot$(OSX_SDK) -nostdlibinc \
             -iwithsysroot/usr/include/c++/v1 \
             -iwithsysroot/usr/include -iframeworkwithsysroot/System/Library/Frameworks

darwin_CFLAGS += -mmacos-version-min=$(OSX_MIN_VERSION) -mlinker-version=$(LLD_VERSION)
# lld gives an arm64 program an ad hoc signature as it links it (macOS on Apple silicon starts no
# program without one), and llvm-strip renews that signature when it strips the program.
darwin_LDFLAGS=-fuse-ld=lld -Wl,-platform_version,macos,$(OSX_MIN_VERSION),$(OSX_SDK_VERSION)

darwin_native_toolchain=
endif
