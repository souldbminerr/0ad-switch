#!/bin/sh
set -ev

XZOPTS="-9 -e"
GZIP7ZOPTS="-mx=9"

BUNDLE_VERSION=${BUNDLE_VERSION:="0.28.0dev"}
PREFIX="0ad-${BUNDLE_VERSION}"

# Prefetch third party tarballs
for pkg in ./libraries/source/*; do
	"${pkg}"/build.sh --fetch-only
done

# Collect the relevant files
tar cf "$PREFIX"-unix-build.tar \
	--exclude='*.bat' --exclude='*.dll' --exclude='*.exe' --exclude='*.lib' --exclude='*.pdb' \
	--exclude='libraries/win32' \
	-s "|.|$PREFIX/~|" \
	-- source build libraries binaries/system/readme.txt binaries/data/l10n binaries/data/tests binaries/data/mods/_test.* *.md *.txt

tar cf "$PREFIX"-unix-data.tar \
	--exclude='binaries/data/config/dev.cfg' \
	-s "|archives|$PREFIX/binaries/data/mods|" \
	-s "|binaries|$PREFIX/binaries|" \
	binaries/data/config binaries/data/tools archives/
# TODO: ought to include generated docs in here, perhaps?

# Compress
# shellcheck disable=SC2086
xz -kv ${XZOPTS} "$PREFIX"-unix-build.tar
# shellcheck disable=SC2086
xz -kv ${XZOPTS} "$PREFIX"-unix-data.tar
DO_GZIP=${DO_GZIP:=true}
if [ "$DO_GZIP" = true ]; then
	7zz a ${GZIP7ZOPTS} "$PREFIX"-unix-build.tar.gz "$PREFIX"-unix-build.tar
	7zz a ${GZIP7ZOPTS} "$PREFIX"-unix-data.tar.gz "$PREFIX"-unix-data.tar
fi

# Fix permissions
chmod -f 644 "${PREFIX}-unix-build.tar.xz"
chmod -f 644 "${PREFIX}-unix-data.tar.xz"
if [ "$DO_GZIP" = true ]; then
	chmod -f 644 "${PREFIX}-unix-build.tar.gz"
	chmod -f 644 "${PREFIX}-unix-data.tar.gz"
fi
