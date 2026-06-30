#!/bin/sh
set -e

: "${TAR:=tar}"

cd "$(dirname "$0")"

PV=28209
LIB_VERSION=${PV}+wfg4

fetch()
{
	tar_version=$(tar --version | head --lines 1)
	case "${tar_version}" in
		*"GNU tar"*)
			tar_extra_opts="--owner root --group root"
			;;
		*"libarchive"*)
			tar_extra_opts="--uname root --gname root"
			;;
		*)
			echo "unknown tar implementation ${tar_version}"
			;;
	esac

	rm -Rf nvtt-${PV}
	svn export https://svn.wildfiregames.com/public/source-libs/trunk/nvtt@${PV} nvtt-${PV}
	# shellcheck disable=SC2086
	"${TAR}" -c ${tar_extra_opts} -Jf nvtt-${PV}.tar.xz nvtt-${PV}
	rm -R nvtt-${PV}
}

echo "Building NVTT..."
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
if [ ! -e "nvtt-${PV}.tar.xz" ]; then
	fetch
fi

# unpack
rm -Rf nvtt-${PV}
"${TAR}" xf nvtt-${PV}.tar.xz

# patch
patch -d nvtt-${PV} -p1 <patches/0001-Don-t-overspecify-flags.patch
patch -d nvtt-${PV} -p1 <patches/0002-Bump-cmake-min-version-to-3.10.patch
patch -d nvtt-${PV} -p1 <patches/0003-Use-execute_process-insted-of-exec_program.patch
patch -d nvtt-${PV} -p1 <patches/0004-Properly-detect-ppc64le-systems.patch
patch -d nvtt-${PV} -p1 <patches/0005-Fix-compiler-flags-on-ppc64le-systems.patch
patch -d nvtt-${PV} -p1 <patches/0006-Fix-altivec-include-on-ppc64le-systems.patch

# build
(
	cd nvtt-${PV}
	mkdir bin lib
	./build.sh
)

# install
rm -Rf bin include lib
cp -R nvtt-${PV}/bin nvtt-${PV}/include nvtt-${PV}/lib .

echo "${LIB_VERSION}" >.already-built
