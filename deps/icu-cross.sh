#!/bin/bash
# icu-cross.sh - cross-build ICU 74.2 for Switch. RUN IN WSL.
# ICU needs a Linux host compiler for its data/build tools (genrb, pkgdata, ...),
# then devkitA64 for the target via --with-cross-build. Installs static ICU
# (libicuuc/i18n/data/io + pkgconfig) straight into the msys2 devkitPro portlibs.
#
# Invoked by build-deps.sh; can also be run directly:
#   DEPS_DIR=/mnt/c/Users/<you>/0ad-switch/deps bash icu-cross.sh
set -e
HERE="${DEPS_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}"
TARBALL="$HERE/src/icu4c-74_2-src.tgz"
JOBS="${JOBS:-4}"

DKP=/opt/devkitpro                     # WSL-native devkitPro (devkitA64 cross tools)
BIN="$DKP/devkitA64/bin"; LIBNX="$DKP/libnx"; PORTLIBS="$DKP/portlibs/switch"
# Install destination = the msys2 devkitPro portlibs (C:\devkitPro\portlibs\switch):
DST="${DST:-/mnt/c/devkitPro/portlibs/switch}"

WORK=/root/icu-build
SRC="$WORK/src"; HOST="$WORK/host"; TGT="$WORK/tgt"; STAGE="$WORK/stage"

mkdir -p "$SRC"; cd "$SRC"
[ -d icu ] || tar xzf "$TARBALL"

echo "=== ICU host build (Linux gcc -> data + cross tools) ==="
rm -rf "$HOST"; mkdir -p "$HOST"; cd "$HOST"
"$SRC/icu/source/configure" --enable-static --disable-shared --disable-samples --disable-tests >/dev/null
make -j"$JOBS"

echo "=== ICU target build (devkitA64) ==="
ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -ffunction-sections -fdata-sections -O2"
DEFS="-D__SWITCH__ -D__linux__ -D_GNU_SOURCE"
INC="-I$LIBNX/include -I$PORTLIBS/include"
rm -rf "$TGT" "$STAGE"; mkdir -p "$TGT"; cd "$TGT"
export CC="$BIN/aarch64-none-elf-gcc" CXX="$BIN/aarch64-none-elf-g++"
export AR="$BIN/aarch64-none-elf-ar" RANLIB="$BIN/aarch64-none-elf-ranlib"
export CFLAGS="$ARCH $DEFS $INC" CXXFLAGS="$ARCH $DEFS $INC -std=gnu++17"
export CPPFLAGS="$DEFS $INC" LDFLAGS="-L$LIBNX/lib"
# Present as linux so ICU selects config/mh-linux (aarch64-none-elf -> empty
# "mh-unknown"); the real compiler stays devkitA64 via CC/CXX above.
"$SRC/icu/source/configure" \
  --host=aarch64-unknown-linux-gnu --with-cross-build="$HOST" --prefix="$STAGE" \
  --enable-static --disable-shared --disable-tests --disable-samples \
  --disable-extras --disable-tools --disable-dyload --with-data-packaging=static
make -j"$JOBS"
make install

echo "=== install into msys2 portlibs: $DST ==="
[ -d "$DST" ] || { echo "ERROR: $DST not found (is devkitPro at C:\\devkitPro?)"; exit 1; }
cp -f "$STAGE"/lib/libicuuc.a "$STAGE"/lib/libicui18n.a \
      "$STAGE"/lib/libicudata.a "$STAGE"/lib/libicuio.a "$DST/lib/"
rm -rf "$DST/include/unicode"
cp -rL "$STAGE/include/unicode" "$DST/include/unicode"
for pc in icu-uc icu-i18n icu-io; do
  sed -E 's#^(prefix)[[:space:]]*=.*#\1 = /opt/devkitpro/portlibs/switch#' \
    "$STAGE/lib/pkgconfig/$pc.pc" > "$DST/lib/pkgconfig/$pc.pc"
done
echo "=== ICU installed ==="
ls -lh "$DST"/lib/libicu*.a
