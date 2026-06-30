#!/bin/sh
set -e

: "${OS:=$(uname -s || true)}"
: "${MAKE:=make}"
: "${JOBS:=-j1}"
: "${TAR:=tar}"

cd "$(dirname "$0")"

PV=5.0.0-beta7
LIB_VERSION=${PV}+wfg0

fetch()
{
	curl -fLo "premake-core-${PV}.tar.gz" \
		"https://github.com/premake/premake-core/archive/refs/tags/v${PV}.tar.gz"
}

echo "Building Premake..."
while [ "$#" -gt 0 ]; do
	case "$1" in
		--fetch-only)
			fetch
			exit
			;;
		--force-rebuild) rm -f .already-built ;;
		*)
			echo "Unknown option: $1"
			exit 1
			;;
	esac
	shift
done

if [ -e .already-built ] && [ "$(cat .already-built || true)" = "${LIB_VERSION}" ]; then
	echo "Skipping - already built (use --force-rebuild to override)"
	exit
fi

# fetch
if [ ! -e "premake-core-${PV}.tar.gz" ]; then
	fetch
fi

# unpack
rm -Rf "premake-core-${PV}"
"${TAR}" -xf "premake-core-${PV}.tar.gz"

# patch
# https://github.com/premake/premake-core/issues/2338
patch -d "premake-core-${PV}" -p1 <patches/0001-Make-clang-default-toolset-for-BSD.patch

#build
(
	cd "premake-core-${PV}"
	case "${OS}" in
		Windows)
			${MAKE} "${JOBS}" -f Bootstrap.mak windows
			;;
		Darwin)
			${MAKE} "${JOBS}" -f Bootstrap.mak PREMAKE_OPTS="--zlib-src=none --curl-src=none" osx
			;;
		*BSD)
			${MAKE} "${JOBS}" -f Bootstrap.mak bsd
			;;
		*)
			${MAKE} "${JOBS}" -f Bootstrap.mak linux
			;;
	esac
)

# install
rm -Rf bin
mkdir -p bin
cp "premake-core-${PV}/bin/release/premake5" bin/

echo "${LIB_VERSION}" >.already-built
