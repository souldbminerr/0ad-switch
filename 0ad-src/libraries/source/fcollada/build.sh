#!/bin/sh
set -e

: "${TAR:=tar}"

cd "$(dirname "$0")"

PV=28209
LIB_VERSION=${PV}+wfg1

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

	rm -Rf fcollada-${PV}
	svn export https://svn.wildfiregames.com/public/source-libs/trunk/fcollada@${PV} fcollada-${PV}
	# shellcheck disable=SC2086
	"${TAR}" -c ${tar_extra_opts} -Jf fcollada-${PV}.tar.xz fcollada-${PV}
	rm -R fcollada-${PV}
}

echo "Building FCollada..."
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
if [ ! -e "fcollada-${PV}.tar.xz" ]; then
	fetch
fi

# unpack
rm -Rf fcollada-${PV}
"${TAR}" xf fcollada-${PV}.tar.xz

# build
(
	cd fcollada-${PV}
	./build.sh
)

# install
rm -Rf include lib
cp -R fcollada-${PV}/include fcollada-${PV}/lib .

echo "${LIB_VERSION}" >.already-built
