#!/bin/sh
set -e

: "${TAR:=tar}"

cd "$(dirname "$0")"

PV=1.3.290.0
LIB_VERSION=${PV}

fetch()
{
	curl -fLo "vulkan-sdk-${PV}.tar.gz" \
		"https://github.com/KhronosGroup/SPIRV-Reflect/archive/refs/tags/vulkan-sdk-${PV}.tar.gz"
}

echo "Building SPIRV-Reflect..."
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
if [ ! -e "vulkan-sdk-${PV}.tar.gz" ]; then
	fetch
fi

# unpack
rm -Rf "SPIRV-Reflect-vulkan-sdk-${PV}"
"${TAR}" xf "vulkan-sdk-${PV}.tar.gz"

# configure
cmake -B build -S "SPIRV-Reflect-vulkan-sdk-${PV}" \
	-DCMAKE_INSTALL_PREFIX="$(realpath . || true)"

# build
cmake --build build

# install
rm -Rf bin
cmake --install build

echo "${LIB_VERSION}" >.already-built
