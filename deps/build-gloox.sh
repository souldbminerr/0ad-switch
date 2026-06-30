#!/usr/bin/env bash
# build-gloox.sh - cross-compile gloox (XMPP) for Nintendo Switch into libgloox.a,
# using the mbedTLS backend added in src/tlsmbedtls.cpp. Run in WSL/Linux.
#
#   wsl -d archlinux -e bash deps/build-gloox.sh
#
# Produces /opt/devkitpro/portlibs/switch/lib/libgloox.a and installs the gloox
# public headers to .../include/gloox/. The 0 A.D. lobby project links -lgloox.
set -euo pipefail

export DEVKITPRO=/opt/devkitpro
export PATH="/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:$PATH"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/gloox-1.0.24/src"
PORTLIBS=/opt/devkitpro/portlibs/switch
LIBNX=/opt/devkitpro/libnx
JOBS="${JOBS:-$(nproc)}"

[ -f "$SRC/tlsmbedtls.cpp" ] || { echo "gloox source not found at $SRC"; exit 1; }

CXX=aarch64-none-elf-g++
AR=aarch64-none-elf-ar

# Match the engine's target/ABI (PIE NRO). -DHAVE_MBEDTLS comes via config.h.unix,
# but define it too so the headers (compiled standalone) agree.
ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE"
# switch-stubs supplies empty <arpa/nameser.h>/<resolv.h> (no resolver on newlib;
# the res_query code is guarded out anyway). Force-include <arpa/inet.h> so htons/
# ntohs are declared in the socket files that only pull <netinet/in.h>.
CXXFLAGS="$ARCH -O2 -std=gnu++17 -D__SWITCH__ -D_GNU_SOURCE -DHAVE_MBEDTLS=1 -DHAVE_CONFIG_H \
  -fno-strict-aliasing -Wno-deprecated-declarations \
  -include arpa/inet.h \
  -I$HERE/gloox-1.0.24/switch-stubs -I$SRC -isystem $PORTLIBS/include -isystem $LIBNX/include"

OBJDIR="$HERE/gloox-1.0.24/obj-switch"
mkdir -p "$OBJDIR"

echo "=== compiling gloox sources ($JOBS jobs) ==="
# Compile every src/*.cpp. The unused TLS backends (gnutls/openssl/schannel) and
# optional features are #ifdef-guarded and compile to empty objects.
compile_one() {
  local f="$1" o
  o="$OBJDIR/$(basename "${f%.cpp}").o"
  $CXX $CXXFLAGS -c "$f" -o "$o"
}
export -f compile_one
export CXX CXXFLAGS OBJDIR
# connectiontcpserver.cpp is server-only (the lobby is a client) and pulls headers
# newlib lacks; skip it. Everything the client path needs is still built.
ls "$SRC"/*.cpp | grep -v '/connectiontcpserver\.cpp$' | xargs -P"$JOBS" -I{} bash -c 'compile_one "$@"' _ {}

echo "=== archiving libgloox.a ==="
rm -f "$PORTLIBS/lib/libgloox.a"
$AR rcs "$PORTLIBS/lib/libgloox.a" "$OBJDIR"/*.o

echo "=== installing headers to $PORTLIBS/include/gloox ==="
mkdir -p "$PORTLIBS/include/gloox"
cp "$SRC"/*.h "$PORTLIBS/include/gloox/"
cp "$SRC"/config.h.unix "$PORTLIBS/include/gloox/"

echo "=== installing pkg-config (gloox.pc) ==="
# 0 A.D.'s premake locates gloox via pkg-config (pkgconfig.add_includes("gloox")).
# gloox's TLS backend is mbedTLS here, so list it as a dependency lib.
mkdir -p "$PORTLIBS/lib/pkgconfig"
cat > "$PORTLIBS/lib/pkgconfig/gloox.pc" <<EOF
prefix=$PORTLIBS
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${exec_prefix}/include

Name: gloox
Description: XMPP/Jabber client/component library (Switch build, mbedTLS backend)
Version: 1.0.24
Libs: -L\${libdir} -lgloox -lmbedtls -lmbedx509 -lmbedcrypto
Cflags: -I\${includedir}
EOF

echo "=== done: $(du -h "$PORTLIBS/lib/libgloox.a" | cut -f1) libgloox.a ==="
