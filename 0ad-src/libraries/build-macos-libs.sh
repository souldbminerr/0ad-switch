#!/bin/sh
#
# Script for acquiring and building macOS dependencies for 0 A.D.
#
# The script checks whether a source tarball exists for each
# dependency, if not it will download the correct version from
# the project's website, then it removes previous build files,
# extracts the tarball, configures and builds the lib. The script
# should die on any errors to ease troubleshooting.
#
# make install is used to copy the compiled libs to each specific
# directory and also the config tools (e.g. sdl-config). Because
# of this, OS X developers must run this script at least once,
# to configure the correct lib directories. It must be run again
# if the libraries are moved.
#
# Building against an SDK is an option, though not required,
# as not all build environments contain the Developer SDKs
# (Xcode does, but the Command Line Tools package does not)
#
# --------------------------------------------------------------
# Library versions for ease of updating:
ZLIB_VERSION="zlib-1.3.1"
CURL_VERSION="curl-7.71.0"
ICONV_VERSION="libiconv-1.17"
XML2_VERSION="libxml2-2.13.5"
SDL2_VERSION="SDL2-2.24.0"
# NOTE: remember to also update LIB_URL below when changing version
BOOST_VERSION="boost_1_81_0"
# NOTE: remember to also update LIB_URL below when changing version
WXWIDGETS_VERSION="wxWidgets-3.2.8"
# libpng was included as part of X11 but that's removed from Mountain Lion
# (also the Snow Leopard version was ancient 1.2)
PNG_VERSION="libpng-1.6.44"
FREETYPE_VERSION="freetype-2.13.3"
OGG_VERSION="libogg-1.3.5"
VORBIS_VERSION="libvorbis-1.3.7"
# gloox requires GnuTLS, GnuTLS requires Nettle and GMP
GMP_VERSION="gmp-6.3.0"
NETTLE_VERSION="nettle-3.10"
# NOTE: remember to also update LIB_URL below when changing version
GLOOX_VERSION="gloox-1.0.28"
GNUTLS_VERSION="gnutls-3.8.4"
# OS X only includes part of ICU, and only the dylib
# NOTE: remember to also update LIB_URL below when changing version
ICU_VERSION="icu4c-69_1"
ENET_VERSION="enet-1.3.18"
MINIUPNPC_VERSION="miniupnpc-2.2.8"
SODIUM_VERSION="libsodium-1.0.20"
FMT_VERSION="7.1.3"
MOLTENVK_VERSION="1.3.0"
OPENAL_SOFT_VERSION="1.24.2"
# --------------------------------------------------------------
# Bundled with the game:
# * SpiderMonkey
# * NVTT
# * FCollada
# --------------------------------------------------------------
# --------------------------------------------------------------
# Provided by OS X:
# * OpenGL
# --------------------------------------------------------------

export CC="${CC:="clang"}" CXX="${CXX:="clang++"}"
export MIN_OSX_VERSION="${MIN_OSX_VERSION:="10.15"}"
export ARCH="${ARCH:=""}"

# The various libs offer inconsistent configure options, some allow
# setting sysroot and OS X-specific options, others don't. Adding to
# the confusion, Apple moved /Developer/SDKs into the Xcode app bundle
# so the path can't be guessed by clever build tools (like Boost.Build).
# Sometimes configure gets it wrong anyway, especially on cross compiles.
# This is why we prefer using (OBJ)CFLAGS, (OBJ)CXXFLAGS, and LDFLAGS.

# Check if SYSROOT is set and not empty
if [ -n "$SYSROOT" ]; then
	C_FLAGS="-isysroot $SYSROOT"
	LDFLAGS="$LDFLAGS -Wl,-syslibroot,$SYSROOT"
fi
# Check if MIN_OSX_VERSION is set and not empty
if [ -n "$MIN_OSX_VERSION" ]; then
	C_FLAGS="$C_FLAGS -mmacosx-version-min=$MIN_OSX_VERSION"
	# clang and llvm-gcc look at mmacosx-version-min to determine link target
	# and CRT version, and use it to set the macosx_version_min linker flag
	LDFLAGS="$LDFLAGS -mmacosx-version-min=$MIN_OSX_VERSION"
fi

CFLAGS="$CFLAGS $C_FLAGS -fvisibility=hidden"
CXXFLAGS="$CXXFLAGS $C_FLAGS -stdlib=libc++ -std=c++17"
OBJCFLAGS="$OBJCFLAGS $C_FLAGS"
OBJCXXFLAGS="$OBJCXXFLAGS $C_FLAGS"

# Annoyingly, ARCH use is rather unstandardised. Some libs expect -arch, others different things.
ARCHLESS_CFLAGS=$CFLAGS
ARCHLESS_CXXFLAGS=$CXXFLAGS
ARCHLESS_LDFLAGS="$LDFLAGS -stdlib=libc++"

# If ARCH isn't set, use the native architecture
if [ -z "${ARCH}" ]; then
	ARCH=$(uname -m)
fi
if [ "$ARCH" = "arm64" ]; then
	# Some libs want this passed to configure for cross compilation.
	HOST_PLATFORM="--host=aarch64-apple-darwin"
else
	CXXFLAGS="$CXXFLAGS -msse4.1"
	# Some libs want this passed to configure for cross compilation.
	HOST_PLATFORM="--host=x86_64-apple-darwin"
fi
if [ "$ARCH" != "$(uname -m)" ]; then
	# wxWidgets cross-compilation does not seem to work, unless we build both architectures at once.
	WX_UNIVERSAL="--enable-universal-binary=x86_64,arm64"
	ICU_CROSS_BUILD=true
	LIBSODIUM_SKIP_TESTS=true
fi

CFLAGS="$CFLAGS -arch $ARCH"
CXXFLAGS="$CXXFLAGS -arch $ARCH"

LDFLAGS="$LDFLAGS -arch $ARCH"

# CMake doesn't seem to pick up on architecture with CFLAGS only
CMAKE_FLAGS="-DCMAKE_OSX_ARCHITECTURES=$ARCH -DCMAKE_OSX_DEPLOYMENT_TARGET=$MIN_OSX_VERSION"

JOBS=${JOBS:="-j2"}

set -e

die()
{
	# Do not display a message if called with no argument
	if [ $# -gt 0 ]; then
		echo ERROR: "$*"
	fi
	exit 1
}

# $1 base url for download
# $2 target file name
download_lib()
{
	if [ ! -e "$2" ]; then
		echo "Downloading $2"
		curl -fLo "$2" "$1$2" || die "Download of $1$2 failed"
	fi
}

already_built()
{
	echo "Skipping - already built (use --force-rebuild to override)"
}

# Check that we're actually on macOS
if [ "$(uname -s)" != "Darwin" ]; then
	die "This script is intended for macOS only"
fi

# Parse command-line options:
force_rebuild=false

while [ "$#" -gt 0 ]; do
	case "$1" in
		--force-rebuild)
			force_rebuild=true
			build_sh_options="$build_sh_options --force-rebuild"
			;;
		-j*) JOBS="$1" ;;
		*)
			echo "Unknown option: $1"
			exit 1
			;;
	esac
	shift
done

cd "$(dirname "$0")" # Now in libraries/ (where we assume this script resides)
mkdir -p macos
cd macos

# Create a location to create copies of dependencies' *.pc files, so they can be found by pkg-config
PC_PATH="$(pwd)/pkgconfig/"
if [ $force_rebuild = "true" ]; then
	rm -rf "$PC_PATH"
fi
mkdir -p "$PC_PATH"
export PKG_CONFIG_PATH="${PKG_CONFIG_PATH}:${PC_PATH}"

echo "Building third-party dependencies..."
# --------------------------------------------------------------
echo "Building zlib..."

ZLIB_DIR="$(pwd)/zlib"
(
	LIB_VERSION="${ZLIB_VERSION}"
	LIB_ARCHIVE="$LIB_VERSION.tar.gz"
	LIB_DIRECTORY=$LIB_VERSION
	LIB_URL="https://zlib.net/fossils/"

	mkdir -p zlib
	cd zlib

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$LIB_VERSION" ]; then
		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY include lib share
		tar -xf $LIB_ARCHIVE || die

		(
			cd $LIB_DIRECTORY || die
			# patch zlib's configure script to use our CFLAGS and LDFLAGS
			patch -Np0 -i ../../../macos-patches/zlib_flags.diff || die
			# hand written configure script, need to set CFLAGS and LDFLAGS in env
			CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" \
				./configure \
				--prefix="$ZLIB_DIR" \
				--static || die
			make "${JOBS}" || die
			make install || die
		) || die "zlib build failed"

		cp -f lib/pkgconfig/* "$PC_PATH"
		echo "$LIB_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed building zlib"

# --------------------------------------------------------------
echo "Building libcurl..."

(
	LIB_VERSION="${CURL_VERSION}"
	LIB_ARCHIVE="$LIB_VERSION.tar.bz2"
	LIB_DIRECTORY="$LIB_VERSION"
	LIB_URL="http://curl.haxx.se/download/"

	mkdir -p libcurl
	cd libcurl

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$LIB_VERSION" ]; then
		INSTALL_DIR="$(pwd)"

		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY bin include lib share
		tar -xf $LIB_ARCHIVE || die

		(
			cd $LIB_DIRECTORY || die
			./configure \
				CFLAGS="$CFLAGS" \
				LDFLAGS="$LDFLAGS" \
				"$HOST_PLATFORM" \
				--prefix="$INSTALL_DIR" \
				--enable-ipv6 \
				--with-darwinssl \
				--without-gssapi \
				--without-libmetalink \
				--without-libpsl \
				--without-librtmp \
				--without-libssh2 \
				--without-nghttp2 \
				--without-nss \
				--without-polarssl \
				--without-ssl \
				--without-gnutls \
				--without-brotli \
				--without-cyassl \
				--without-winssl \
				--without-mbedtls \
				--without-wolfssl \
				--without-spnego \
				--disable-ares \
				--disable-ldap \
				--disable-ldaps \
				--without-libidn2 \
				--with-zlib="${ZLIB_DIR}" \
				--enable-shared=no || die
			make "${JOBS}" || die
			make install || die
		) || die "libcurl build failed"

		cp -f lib/pkgconfig/* "$PC_PATH"
		echo "$LIB_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build curl"

# --------------------------------------------------------------
echo "Building libiconv..."

ICONV_DIR="$(pwd)/iconv"
(
	LIB_VERSION="${ICONV_VERSION}"
	LIB_ARCHIVE="$LIB_VERSION.tar.gz"
	LIB_DIRECTORY="$LIB_VERSION"
	LIB_URL="http://ftp.gnu.org/pub/gnu/libiconv/"

	mkdir -p iconv
	cd iconv

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$LIB_VERSION" ]; then
		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY bin include lib share
		tar -xf $LIB_ARCHIVE || die

		(
			cd $LIB_DIRECTORY || die
			./configure \
				CFLAGS="$CFLAGS" \
				LDFLAGS="$LDFLAGS" \
				"$HOST_PLATFORM" \
				--prefix="$ICONV_DIR" \
				--without-libiconv-prefix \
				--without-libintl-prefix \
				--disable-nls \
				--enable-shared=no || die
			make "${JOBS}" || die
			make install || die
		) || die "libiconv build failed"

		echo "$LIB_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build iconv"

# --------------------------------------------------------------
echo "Building libxml2..."

(
	LIB_VERSION="${XML2_VERSION}"
	LIB_ARCHIVE="$LIB_VERSION.tar.xz"
	LIB_DIRECTORY="$LIB_VERSION"
	LIB_URL="https://download.gnome.org/sources/libxml2/2.13/"

	mkdir -p libxml2
	cd libxml2

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$LIB_VERSION" ]; then
		INSTALL_DIR="$(pwd)"

		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY bin include lib share
		tar -xf $LIB_ARCHIVE || die

		(
			cd $LIB_DIRECTORY || die
			./configure \
				CFLAGS="$CFLAGS" \
				LDFLAGS="$LDFLAGS" \
				"$HOST_PLATFORM" \
				--prefix="$INSTALL_DIR" \
				--without-lzma \
				--without-python \
				--with-iconv="${ICONV_DIR}" \
				--with-zlib="${ZLIB_DIR}" \
				--enable-shared=no || die
			make "${JOBS}" || die
			make install || die
		) || die "libxml2 build failed"

		cp -f lib/pkgconfig/* "$PC_PATH"
		echo "$LIB_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build libxml2"

# --------------------------------------------------------------

echo "Building SDL2..."

(
	LIB_VERSION="${SDL2_VERSION}"
	LIB_ARCHIVE="$LIB_VERSION.tar.gz"
	LIB_DIRECTORY=$LIB_VERSION
	LIB_URL="https://libsdl.org/release/"

	mkdir -p sdl2
	cd sdl2

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$LIB_VERSION" ]; then
		INSTALL_DIR="$(pwd)"

		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY bin include lib share
		tar -xf $LIB_ARCHIVE || die

		(
			cd $LIB_DIRECTORY || die
			# We don't want SDL2 to pull in system iconv, force it to detect ours with flags.
			# Don't use X11 - we don't need it and Mountain Lion removed it
			./configure \
				CPPFLAGS="-I${ICONV_DIR}/include" \
				CFLAGS="$CFLAGS" \
				CXXFLAGS="$CXXFLAGS" \
				LDFLAGS="$LDFLAGS -L${ICONV_DIR}/lib" \
				"$HOST_PLATFORM" \
				--prefix="$INSTALL_DIR" \
				--disable-video-x11 \
				--without-x \
				--enable-video-cocoa \
				--enable-shared=no || die
			make "${JOBS}" || die
			make install || die
		) || die "SDL2 build failed"

		cp -f lib/pkgconfig/* "$PC_PATH"
		echo "$LIB_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build SDL2"

# --------------------------------------------------------------
echo "Building Boost..."

(
	LIB_VERSION="${BOOST_VERSION}"
	LIB_ARCHIVE="$LIB_VERSION.tar.bz2"
	LIB_DIRECTORY="$LIB_VERSION"
	LIB_URL="https://archives.boost.io/release/1.81.0/source/"

	mkdir -p boost
	cd boost

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$LIB_VERSION" ]; then
		INSTALL_DIR="$(pwd)"

		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY include lib
		tar -xf $LIB_ARCHIVE || die

		(
			cd $LIB_DIRECTORY || die
			# Can't use macosx-version, see above comment.
			./bootstrap.sh \
				--with-libraries=filesystem,system \
				--prefix="$INSTALL_DIR" || die
			./b2 \
				cflags="$CFLAGS" \
				toolset=clang \
				cxxflags="$CXXFLAGS" \
				linkflags="$LDFLAGS" "${JOBS}" \
				-d2 \
				--layout=system \
				--debug-configuration \
				link=static \
				threading=multi \
				variant=release \
				install || die
		) || die "Boost build failed"

		echo "$LIB_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build boost"

# --------------------------------------------------------------
# TODO: This build takes ages, anything we can exclude?
echo "Building wxWidgets..."

(
	LIB_VERSION="${WXWIDGETS_VERSION}"
	LIB_ARCHIVE="$LIB_VERSION.tar.bz2"
	LIB_DIRECTORY="$LIB_VERSION"
	LIB_URL="http://github.com/wxWidgets/wxWidgets/releases/download/v3.2.8/"

	mkdir -p wxwidgets
	cd wxwidgets

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$LIB_VERSION" ]; then
		INSTALL_DIR="$(pwd)"

		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY bin include lib share
		tar -xf $LIB_ARCHIVE || die

		(
			mkdir -p $LIB_DIRECTORY/build-release
			cd $LIB_DIRECTORY/build-release

			CONF_OPTS="
				--prefix=$INSTALL_DIR
				--disable-shared
				--disable-sys-libs
				--enable-unicode
				--with-cocoa
				--with-opengl
				--with-libiconv-prefix=${ICONV_DIR}
				${WX_UNIVERSAL:=""}
				--with-expat=builtin
				--with-libpng=builtin
				--without-libtiff
				--without-sdl
				--without-x
				--disable-stc
				--disable-webview
				--disable-webviewwebkit
				--disable-webviewie
				--without-libjpeg"
			# wxWidgets configure now defaults to targeting 10.5, if not specified,
			# but that conflicts with our flags
			if [ -n "$MIN_OSX_VERSION" ]; then
				CONF_OPTS="$CONF_OPTS --with-macosx-version-min=$MIN_OSX_VERSION"
			fi
			# shellcheck disable=SC2086
			../configure \
				CFLAGS="$ARCHLESS_CFLAGS" \
				CXXFLAGS="$ARCHLESS_CXXFLAGS" \
				CPPFLAGS="-D__ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES=1" \
				LDFLAGS="$ARCHLESS_LDFLAGS" $CONF_OPTS || die
			make "${JOBS}" || die
			make install || die
		) || die "wxWidgets build failed"

		echo "$LIB_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build wxWidgets"

# --------------------------------------------------------------
echo "Building libpng..."

(
	LIB_VERSION="${PNG_VERSION}"
	LIB_ARCHIVE="$LIB_VERSION.tar.gz"
	LIB_DIRECTORY="$LIB_VERSION"
	LIB_URL="http://download.sourceforge.net/libpng/"

	mkdir -p libpng
	cd libpng

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$LIB_VERSION" ]; then
		INSTALL_DIR="$(pwd)"

		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY bin include lib share
		tar -xf $LIB_ARCHIVE || die

		(
			cd $LIB_DIRECTORY || die
			# libpng has no flags for zlib but the 10.12 version is too old, so link our own.
			./configure \
				CFLAGS="$CFLAGS" \
				CPPFLAGS=" -I $ZLIB_DIR/include " \
				LDFLAGS="$LDFLAGS -L$ZLIB_DIR/lib" \
				"$HOST_PLATFORM" \
				--prefix="$INSTALL_DIR" \
				--enable-shared=no || die
			make "${JOBS}" || die
			make install || die
		) || die "libpng build failed"

		cp -f lib/pkgconfig/* "$PC_PATH"
		echo "$LIB_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build libpng"

# --------------------------------------------------------------
echo "Building freetype..."

(
	LIB_VERSION="${FREETYPE_VERSION}"
	LIB_ARCHIVE="$LIB_VERSION.tar.gz"
	LIB_DIRECTORY="$LIB_VERSION"
	LIB_URL="https://download.savannah.gnu.org/releases/freetype/"

	mkdir -p freetype
	cd freetype

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$LIB_VERSION" ]; then
		INSTALL_DIR="$(pwd)"

		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY bin include lib share
		tar -xf $LIB_ARCHIVE || die

		(
			cd $LIB_DIRECTORY || die
			./configure \
				CFLAGS="$CFLAGS" \
				LDFLAGS="$LDFLAGS" \
				"$HOST_PLATFORM" \
				--prefix="$INSTALL_DIR" \
				--enable-shared=no \
				--with-harfbuzz=no \
				--with-bzip2=no \
				--with-brotli=no || die
			make "${JOBS}" || die
			make install || die
		) || die "freetype build failed"

		cp -f lib/pkgconfig/* "$PC_PATH"
		echo "$LIB_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build freetype"

# --------------------------------------------------------------
# Dependency of vorbis
echo "Building libogg..."

OGG_DIR="$(pwd)/libogg"
(
	LIB_VERSION="${OGG_VERSION}"
	LIB_ARCHIVE="$LIB_VERSION.tar.gz"
	LIB_DIRECTORY="$LIB_VERSION"
	LIB_URL="http://downloads.xiph.org/releases/ogg/"

	mkdir -p libogg
	cd libogg

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$LIB_VERSION" ]; then
		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY include lib share
		tar -xf $LIB_ARCHIVE || die

		# shellcheck disable=SC2086
		cmake -B libogg \
			-S $LIB_DIRECTORY \
			-G "Unix Makefiles" \
			-DCMAKE_INSTALL_PREFIX="$OGG_DIR" \
			-DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
			-DBUILD_SHARED_LIBS=OFF \
			-DINSTALL_DOCS=OFF \
			-DCMAKE_C_FLAGS="$CFLAGS" \
			$CMAKE_FLAGS || die
		cmake --build libogg "${JOBS}" --target install || die

		cp -f lib/pkgconfig/* "$PC_PATH"
		echo "$LIB_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build libogg"

# --------------------------------------------------------------
echo "Building libvorbis..."

(
	LIB_VERSION="${VORBIS_VERSION}"
	LIB_ARCHIVE="$LIB_VERSION.tar.gz"
	LIB_DIRECTORY="$LIB_VERSION"
	LIB_URL="http://downloads.xiph.org/releases/vorbis/"

	mkdir -p vorbis
	cd vorbis

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$LIB_VERSION" ]; then
		INSTALL_DIR="$(pwd)"

		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY include lib share
		tar -xf $LIB_ARCHIVE || die

		# shellcheck disable=SC2086
		cmake -B libvorbis \
			-S $LIB_DIRECTORY \
			-G "Unix Makefiles" \
			-DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
			-DCMAKE_PREFIX_PATH="$OGG_DIR" \
			-DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
			-DBUILD_SHARED_LIBS=OFF \
			-DINSTALL_DOCS=OFF \
			-DCMAKE_C_FLAGS="$CFLAGS" \
			$CMAKE_FLAGS || die
		cmake --build libvorbis "${JOBS}" --target install || die

		cp -f lib/pkgconfig/* "$PC_PATH"
		echo "$LIB_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build libvorbis"

# --------------------------------------------------------------
echo "Building GMP..."

GMP_DIR="$(pwd)/gmp"
(
	LIB_VERSION="${GMP_VERSION}"
	LIB_ARCHIVE="$LIB_VERSION.tar.bz2"
	LIB_DIRECTORY="$LIB_VERSION"
	LIB_URL="https://gmplib.org/download/gmp/"

	mkdir -p gmp
	cd gmp

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$LIB_VERSION" ]; then
		INSTALL_DIR="$(pwd)"

		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY bin include lib
		tar -xf $LIB_ARCHIVE || die

		(
			cd $LIB_DIRECTORY || die
			# NOTE: enable-fat in this case allows building and running on different CPUS.
			# Otherwise CPU-specific instructions will be used with no fallback for older CPUs.
			./configure \
				CFLAGS="$CFLAGS" \
				CXXFLAGS="$CXXFLAGS" \
				LDFLAGS="$LDFLAGS" \
				"$HOST_PLATFORM" \
				--prefix="$INSTALL_DIR" \
				--enable-fat \
				--disable-shared \
				--with-pic || die
			make "${JOBS}" || die
			make install || die
		) || die "GMP build failed"

		cp -f lib/pkgconfig/* "$PC_PATH"
		echo "$LIB_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build GMP"

# --------------------------------------------------------------
echo "Building Nettle..."
# Also builds hogweed

NETTLE_DIR="$(pwd)/nettle"
(
	LIB_VERSION="${NETTLE_VERSION}"
	LIB_ARCHIVE="$LIB_VERSION.tar.gz"
	LIB_DIRECTORY="$LIB_VERSION"
	LIB_URL="https://ftp.gnu.org/gnu/nettle/"

	mkdir -p nettle
	cd nettle

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$LIB_VERSION" ]; then
		INSTALL_DIR="$(pwd)"

		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY bin include lib
		tar -xf $LIB_ARCHIVE || die

		(
			cd $LIB_DIRECTORY || die
			# NOTE: enable-fat in this case allows building and running on different CPUS.
			# Otherwise CPU-specific instructions will be used with no fallback for older CPUs.
			./configure \
				CFLAGS="$CFLAGS" \
				CXXFLAGS="$CXXFLAGS" \
				LDFLAGS="$LDFLAGS" \
				"$HOST_PLATFORM" \
				--with-include-path="${GMP_DIR}/include" \
				--with-lib-path="${GMP_DIR}/lib" \
				--prefix="$INSTALL_DIR" \
				--enable-fat \
				--disable-shared \
				--disable-documentation \
				--disable-openssl \
				--disable-assembler || die
			make "${JOBS}" || die
			make install || die
		) || die "Nettle build failed"

		cp -f lib/pkgconfig/* "$PC_PATH"
		echo "$LIB_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build nettle"

# --------------------------------------------------------------
echo "Building GnuTLS..."

GNUTLS_DIR="$(pwd)/gnutls"
(
	LIB_VERSION="${GNUTLS_VERSION}"
	LIB_ARCHIVE="$LIB_VERSION.tar.xz"
	LIB_DIRECTORY="$LIB_VERSION"
	LIB_URL="https://www.gnupg.org/ftp/gcrypt/gnutls/v3.8/"

	mkdir -p gnutls
	cd gnutls

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$LIB_VERSION" ]; then
		INSTALL_DIR="$(pwd)"

		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY bin include lib
		tar -xf $LIB_ARCHIVE || die

		(
			cd $LIB_DIRECTORY || die
			# Patch GNUTLS for a linking issue with isdigit
			# Patch by Ross Nicholson: https://gitlab.com/gnutls/gnutls/-/issues/1033#note_379529145
			patch -Np1 -i ../../../macos-patches/03-undo-libtasn1-cisdigit.patch || die
			# Patch GNUTLS for a duplicate symbol issue in nettle (should be fixed in the next version)
			# See https://gitlab.com/gnutls/gnutls/-/merge_requests/1826
			patch -Np1 -i ../../../macos-patches/04-fix-duplicate-symbols.patch || die
			./configure \
				CFLAGS="$CFLAGS" \
				CXXFLAGS="$CXXFLAGS" \
				LDFLAGS="$LDFLAGS" \
				LIBS="-L${GMP_DIR}/lib -lgmp" \
				NETTLE_CFLAGS="-I${NETTLE_DIR}/include" \
				NETTLE_LIBS="-L${NETTLE_DIR}/lib -lnettle" \
				HOGWEED_CFLAGS="-I${NETTLE_DIR}/include" \
				HOGWEED_LIBS="-L${NETTLE_DIR}/lib -lhogweed" \
				GMP_CFLAGS="-I${GMP_DIR}/include" \
				GMP_LIBS="-L${GMP_DIR}/lib -lgmp" \
				"$HOST_PLATFORM" \
				--prefix="$INSTALL_DIR" \
				--enable-shared=no \
				--without-idn \
				--with-included-unistring \
				--with-included-libtasn1 \
				--without-p11-kit \
				--without-brotli \
				--without-zstd \
				--without-tpm2 \
				--disable-libdane \
				--disable-tests \
				--disable-doc \
				--disable-tools \
				--disable-nls || die
			make "${JOBS}" LDFLAGS= install || die
		) || die "GnuTLS build failed"

		cp -f lib/pkgconfig/* "$PC_PATH"
		echo "$LIB_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build GnuTLS"

# --------------------------------------------------------------
echo "Building gloox..."

(
	LIB_VERSION="${GLOOX_VERSION}"
	LIB_ARCHIVE="$LIB_VERSION.tar.bz2"
	LIB_DIRECTORY="$LIB_VERSION"
	LIB_URL="https://releases.wildfiregames.com/libs/"

	mkdir -p gloox
	cd gloox

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$LIB_VERSION" ]; then
		INSTALL_DIR="$(pwd)"

		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY bin include lib
		tar -xf $LIB_ARCHIVE || die

		(
			cd $LIB_DIRECTORY || die
			# TODO: pulls in libresolv dependency from /usr/lib
			./configure \
				CFLAGS="$CFLAGS" \
				CXXFLAGS="$CXXFLAGS" \
				LDFLAGS="$LDFLAGS" \
				"$HOST_PLATFORM" \
				--prefix="$INSTALL_DIR" \
				GNUTLS_CFLAGS="-I${GNUTLS_DIR}/include" \
				GNUTLS_LIBS="-L${GNUTLS_DIR}/lib -lgnutls" \
				--enable-shared=no \
				--with-zlib="${ZLIB_DIR}" \
				--without-libidn \
				--with-gnutls="yes" \
				--without-openssl \
				--without-tests \
				--without-examples \
				--disable-getaddrinfo || die
			make "${JOBS}" || die
			make install || die
		) || die "gloox build failed"

		cp -f lib/pkgconfig/* "$PC_PATH"
		echo "$LIB_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build gloox"

# --------------------------------------------------------------
echo "Building ICU..."

(
	LIB_VERSION="${ICU_VERSION}"
	LIB_ARCHIVE="$LIB_VERSION-src.tgz"
	LIB_DIRECTORY="icu"
	LIB_URL="https://github.com/unicode-org/icu/releases/download/release-69-1/"

	mkdir -p icu
	cd icu

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$LIB_VERSION" ]; then
		INSTALL_DIR="$(pwd)"

		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY bin include lib sbin share
		tar -xf $LIB_ARCHIVE || die

		(
			mkdir -p $LIB_DIRECTORY/source/build
			# If cross-compiling, ICU first needs a native build then a cross-build:
			# https://unicode-org.github.io/icu/userguide/icu4c/build.html#how-to-cross-compile-icu
			# Do the former in all cases (without make install), then the latter if
			# needed.
			if [ "$ICU_CROSS_BUILD" = "true" ]; then
				mkdir -p $LIB_DIRECTORY/source/build-native
				cd $LIB_DIRECTORY/source/build-native
				ICU_NATIVE_BUILD_DIR="$(pwd)"
			else
				cd $LIB_DIRECTORY/source/build
			fi
			CFLAGS="$ARCHLESS_CFLAGS" CXXFLAGS="$ARCHLESS_CXXFLAGS" LDFLAGS="$ARCHLESS_LDFLAGS" \
				../runConfigureICU MacOSX \
				--prefix="$INSTALL_DIR" \
				--disable-shared \
				--enable-static \
				--disable-samples \
				--enable-extras \
				--enable-icuio \
				--enable-tools || die
			make "${JOBS}" || die
			if [ "$ICU_CROSS_BUILD" = "true" ]; then
				cd ../build || die
				CFLAGS="$CFLAGS" CXXFLAGS="$CXXFLAGS" LDFLAGS="$LDFLAGS" \
					../runConfigureICU MacOSX \
					"$HOST_PLATFORM" \
					--with-cross-build="$ICU_NATIVE_BUILD_DIR" \
					--prefix="$INSTALL_DIR" \
					--disable-shared \
					--enable-static \
					--disable-samples \
					--enable-extras \
					--enable-icuio \
					--enable-tools || die
				make "${JOBS}" || die
			fi
			make install || die
		) || die "ICU build failed"

		cp -f lib/pkgconfig/* "$PC_PATH"
		echo "$LIB_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build ICU"

# --------------------------------------------------------------
echo "Building ENet..."

(
	LIB_VERSION="${ENET_VERSION}"
	LIB_ARCHIVE="$LIB_VERSION.tar.gz"
	LIB_DIRECTORY="$LIB_VERSION"
	LIB_URL="http://enet.bespin.org/download/"

	mkdir -p enet
	cd enet

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$LIB_VERSION" ]; then
		INSTALL_DIR="$(pwd)"

		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY bin include lib sbin share
		tar -xf $LIB_ARCHIVE || die

		(
			cd $LIB_DIRECTORY || die
			./configure \
				CFLAGS="$CFLAGS" \
				LDFLAGS="$LDFLAGS" \
				"$HOST_PLATFORM" \
				--prefix="${INSTALL_DIR}" \
				--enable-shared=no || die
			make clean || die
			make "${JOBS}" || die
			make install || die
		) || die "ENet build failed"

		cp -f lib/pkgconfig/* "$PC_PATH"
		echo "$LIB_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build ENet"

# --------------------------------------------------------------
echo "Building MiniUPnPc..."

(
	LIB_VERSION="${MINIUPNPC_VERSION}"
	LIB_ARCHIVE="$LIB_VERSION.tar.gz"
	LIB_DIRECTORY="$LIB_VERSION"
	LIB_URL="http://miniupnp.free.fr/files/"

	mkdir -p miniupnpc
	cd miniupnpc

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat <.already-built)" != "$LIB_VERSION" ]; then
		INSTALL_DIR="$(pwd)"

		rm -f .already-built
		download_lib "$LIB_URL" $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY bin include lib share
		tar -xf $LIB_ARCHIVE || die

		(
			cd $LIB_DIRECTORY || die
			make clean || die
			make CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" "${JOBS}" || die
			make INSTALLPREFIX="$INSTALL_DIR" install || die
		) || die "MiniUPnPc build failed"

		# TODO: how can we not build the dylibs?
		rm -f lib/*.dylib
		cp -f lib/pkgconfig/* "$PC_PATH"
		echo "$LIB_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build MiniUPnPc"

# --------------------------------------------------------------
echo "Building libsodium..."

(
	LIB_VERSION="${SODIUM_VERSION}"
	LIB_ARCHIVE="$SODIUM_VERSION.tar.gz"
	LIB_DIRECTORY="$LIB_VERSION"
	LIB_URL="https://download.libsodium.org/libsodium/releases/"

	mkdir -p libsodium
	cd libsodium

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$LIB_VERSION" ]; then
		INSTALL_DIR="$(pwd)"

		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY include lib
		tar -xf $LIB_ARCHIVE || die

		(
			cd $LIB_DIRECTORY || die
			./configure CFLAGS="$CFLAGS" \
				LDFLAGS="$LDFLAGS" \
				"$HOST_PLATFORM" \
				--prefix="${INSTALL_DIR}" \
				--enable-shared=no || die
			make clean || die
			make CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" "${JOBS}" || die
			if [ "$LIBSODIUM_SKIP_TESTS" != "true" ]; then
				make check || die
			fi
			make INSTALLPREFIX="$INSTALL_DIR" install || die
		) || die "libsodium build failed"

		cp -f lib/pkgconfig/* "$PC_PATH"
		echo "$LIB_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build libsodium"

echo "Building OpenAL Soft"
(
	LIB_DIRECTORY="openal-soft-$OPENAL_SOFT_VERSION"
	LIB_ARCHIVE="$OPENAL_SOFT_VERSION.tar.gz"
	LIB_URL="https://github.com/kcat/openal-soft/archive/refs/tags/"

	mkdir -p openal-soft
	cd openal-soft

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$OPENAL_SOFT_VERSION" ]; then
		INSTALL_DIR="$(pwd)"

		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY include lib share libopenal-soft
		tar -xf $LIB_ARCHIVE || die

		# shellcheck disable=SC2086
		cmake -B libopenal-soft \
			-S $LIB_DIRECTORY \
			-DCMAKE_C_FLAGS="$CFLAGS" \
			-DLIBTYPE=STATIC \
			-DALSOFT_EXAMPLES=OFF \
			-DALSOFT_UTILS=OFF \
			-DALSOFT_TESTS=OFF \
			-G "Unix Makefiles" \
			-DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
			$CMAKE_FLAGS || die
		cmake --build libopenal-soft "${JOBS}" --target install || die

		cp -f lib/pkgconfig/* "$PC_PATH"
		echo "$OPENAL_SOFT_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build OpenAL Soft"

# --------------------------------------------------------------
echo "Building fmt..."

(
	LIB_DIRECTORY="fmt-$FMT_VERSION"
	LIB_ARCHIVE="$FMT_VERSION.tar.gz"
	LIB_URL="https://github.com/fmtlib/fmt/archive/"

	mkdir -p fmt
	cd fmt

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$FMT_VERSION" ]; then
		INSTALL_DIR="$(pwd)"

		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf $LIB_DIRECTORY include lib
		tar -xf $LIB_ARCHIVE || die

		# It appears that older versions of Clang require constexpr statements to have a user-set constructor.
		patch -Np1 -d $LIB_DIRECTORY -i ../../../macos-patches/fmt_constexpr.diff || die
		# shellcheck disable=SC2086
		cmake -B libfmt \
			-S $LIB_DIRECTORY \
			-DFMT_TEST=False \
			-DFMT_DOC=False \
			-DCMAKE_C_FLAGS="$CFLAGS" \
			-DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
			$CMAKE_FLAGS || die
		cmake --build libfmt "${JOBS}" --target install || die

		cp -f lib/pkgconfig/* "$PC_PATH"
		echo "$FMT_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build fmt"

# --------------------------------------------------------------
echo "Building Molten VK..."

(
	LIB_DIRECTORY="MoltenVK-$MOLTENVK_VERSION"
	LIB_ARCHIVE="v$MOLTENVK_VERSION.tar.gz"
	LIB_URL="https://github.com/KhronosGroup/MoltenVK/archive/refs/tags/"

	mkdir -p "molten-vk"
	cd "molten-vk"

	if [ $force_rebuild = "true" ] || [ ! -e .already-built ] || [ "$(cat .already-built)" != "$MOLTENVK_VERSION" ] || [ ! -e ../../../binaries/system/libMoltenVK.dylib ]; then
		INSTALL_DIR="../../../binaries/system/"

		rm -f .already-built
		download_lib $LIB_URL $LIB_ARCHIVE || die

		rm -rf "$LIB_DIRECTORY"
		tar -xf $LIB_ARCHIVE || die

		(
			cd "$LIB_DIRECTORY"
			# dont use --parallel-build, it breaks the build since it calls kill.
			MACOSX_DEPLOYMENT_TARGET="$MIN_OSX_VERSION" CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" ./fetchDependencies --macos || die

			xcodebuild \
				-quiet \
				-project MoltenVKPackaging.xcodeproj \
				-scheme "MoltenVK Package (macOS only)" \
				-destination "generic/platform=macOS" \
				MACOSX_DEPLOYMENT_TARGET="$MIN_OSX_VERSION" \
				CFLAGS="$CFLAGS" \
				CXXFLAGS="$CXXFLAGS" \
				LDFLAGS="$LDFLAGS" \
				ARCHS="arm64 x86_64" || die
		) || die

		# We don't use the provided install command, because we haven't got enough permissions to use it.
		mv $LIB_DIRECTORY/Package/Latest/MoltenVK/dylib/macOS/libMoltenVK.dylib $INSTALL_DIR || die

		echo "$MOLTENVK_VERSION" >.already-built
	else
		already_built
	fi
) || die "Failed to build MoltenVK"

# --------------------------------------------------------------------
# The following libraries and build tools are shared on different OSes
# and may be customized, so we build and install them from bundled sources
# (served over SVN or other sources)
# --------------------------------------------------------------------

export ARCH CXXFLAGS CFLAGS LDFLAGS CMAKE_FLAGS JOBS

# --------------------------------------------------------------
# shellcheck disable=SC2086
./../source/cxxtest-4.4/build.sh $build_sh_options || die "cxxtest build failed"

# --------------------------------------------------------------
# shellcheck disable=SC2086
./../source/fcollada/build.sh $build_sh_options || die "FCollada build failed"

# --------------------------------------------------------------
# shellcheck disable=SC2086
./../source/nvtt/build.sh $build_sh_options || die "NVTT build failed"

# --------------------------------------------------------------
# shellcheck disable=SC2086
./../source/spidermonkey/build.sh $build_sh_options || die "SpiderMonkey build failed"

# --------------------------------------------------------------------
# Build tools must be compiled for the build arch, not the target arch.
# premake needs this to be very explicit.
# --------------------------------------------------------------------

# --------------------------------------------------------------
# shellcheck disable=SC2086
ARCH="$(uname -m)" \
CFLAGS="${ARCHLESS_CFLAGS} -arch $(uname -m)" \
CXXFLAGS="${ARCHLESS_CXXFLAGS} -arch $(uname -m)" \
LDFLAGS="${ARCHLESS_LDFLAGS} -arch $(uname -m)" \
	./../source/premake-core/build.sh $build_sh_options || die "Premake build failed"
