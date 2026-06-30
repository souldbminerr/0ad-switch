#!/bin/bash
# One-time: mirror the devkitPro Switch portlibs (the M4 cross-compiled deps) from
# the msys2 install into WSL's native /opt, so the engine can be generated AND
# compiled in WSL (reliable process spawning, unlike msys2). The msys2-built .a are
# the same aarch64-none-elf target, so they're used as-is. Run in WSL.
set -euo pipefail
SRC="${SRC:-/mnt/c/devkitPro/portlibs/switch}"
DST=/opt/devkitpro/portlibs/switch
[ -e "$SRC/lib/pkgconfig" ] || { echo "msys2 portlibs not found at $SRC"; exit 1; }
mkdir -p "$DST"
echo "Mirroring $SRC -> $DST ..."
cp -a "$SRC/." "$DST/"
echo "done: $(ls "$DST/lib/pkgconfig" | wc -l) pkg-config files"
