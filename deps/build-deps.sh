#!/usr/bin/env bash
# build-deps.sh - install 0 A.D.'s external dependencies into devkitPro portlibs.
# RUN FROM msys2. The ICU sub-build is delegated to WSL (it needs a Linux host
# compiler for ICU's data tools); everything else builds natively in msys2.
#
# Sources are vendored in ./src (pinned versions). Already-packaged libs
# (libxml2, enet, libsodium, openal-soft) come from devkitPro pacman.
#
# Usage (msys2):
#   ./build-deps.sh            # everything
#   ./build-deps.sh pacman     # just the pacman packages
#   ./build-deps.sh fmt        # just fmt
#   ./build-deps.sh boost      # just boost headers
#   ./build-deps.sh icu        # just ICU (via WSL)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/src"
PORTLIBS=/opt/devkitpro/portlibs/switch

# devkitPro environment (Switch.cmake needs DEVKITPRO to use static-lib
# try-compile; the toolchain binaries must be on PATH).
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=/opt/devkitpro/devkitA64
export PATH="/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:$PATH"
WSL_DISTRO="${WSL_DISTRO:-archlinux}"
JOBS="${JOBS:-4}"
WHAT="${1:-all}"

FMT_VER=10.2.1
BOOST_VER=1_83_0

do_pacman() {
  echo "### devkitPro packages: libxml2, enet, libsodium, openal-soft ###"
  pacman -S --noconfirm --needed switch-libxml2 switch-enet switch-libsodium switch-openal-soft
}

do_fmt() {
  echo "### fmt $FMT_VER (libfmt.a) ###"
  local d=/tmp/fmt-build; rm -rf "$d"; mkdir -p "$d"
  tar xf "$SRC/fmt-$FMT_VER.tar.gz" -C "$d"
  local s="$d/fmt-$FMT_VER"
  cmake -S "$s" -B "$s/build" -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
    -DFMT_TEST=OFF -DFMT_DOC=OFF -DFMT_INSTALL=ON -DCMAKE_BUILD_TYPE=Release
  cmake --build "$s/build" -j"$JOBS"
  cmake --install "$s/build" --prefix "$PORTLIBS"
}

do_boost() {
  echo "### boost $BOOST_VER (header-only) ###"
  local d=/tmp/boost-hdr; rm -rf "$d"; mkdir -p "$d"
  tar xzf "$SRC/boost_$BOOST_VER.tar.gz" -C "$d" "boost_$BOOST_VER/boost"
  rm -rf "$PORTLIBS/include/boost"
  mv "$d/boost_$BOOST_VER/boost" "$PORTLIBS/include/boost"
}

do_icu() {
  echo "### ICU 74.2 (cross-build via WSL:$WSL_DISTRO) ###"
  local wsl_here
  wsl_here="$(printf '%s\n' "$HERE" | sed -E 's#^/([a-zA-Z])/#/mnt/\L\1/#')"
  # Strip CRLF before running in WSL (repo files may be checked out CRLF).
  wsl.exe -d "$WSL_DISTRO" -e bash -c \
    "sed 's/\r$//' '$wsl_here/icu-cross.sh' > /tmp/icu-cross.sh && \
     DEPS_DIR='$wsl_here' JOBS='$JOBS' bash /tmp/icu-cross.sh"
}

case "$WHAT" in
  pacman) do_pacman ;;
  fmt)    do_fmt ;;
  boost)  do_boost ;;
  icu)    do_icu ;;
  all)    do_pacman; do_fmt; do_boost; do_icu ;;
  *) echo "usage: $0 [all|pacman|fmt|boost|icu]"; exit 1 ;;
esac
echo "=== build-deps: $WHAT done ==="
