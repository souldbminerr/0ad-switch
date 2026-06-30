#!/bin/sh
set -e

: "${TAR:=tar}"

cd "$(dirname "$0")"

PV=4.4
LIB_VERSION=${PV}+wfg1

fetch()
{
	curl -fLo "cxxtest-${PV}.tar.gz" \
		"https://github.com/CxxTest/cxxtest/releases/download/${PV}/cxxtest-${PV}.tar.gz"
}

echo "Building CxxTest..."
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
if [ ! -e "cxxtest-${PV}.tar.gz" ]; then
	fetch
fi

# unpack
rm -Rf "cxxtest-${PV}"
"${TAR}" -xf "cxxtest-${PV}.tar.gz"

# patch
patch -d "cxxtest-${PV}" -p1 <patches/0001-Add-Debian-python3-patch.patch

# nothing to actually build
# built as part of building tests

# install
rm -Rf bin cxxtest python
cp -R "cxxtest-${PV}/bin" "cxxtest-${PV}/cxxtest" "cxxtest-${PV}/python" .

echo "${LIB_VERSION}" >.already-built
