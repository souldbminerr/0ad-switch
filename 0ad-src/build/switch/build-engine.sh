#!/bin/bash
# Build the Pyrogenesis engine static libs for Nintendo Switch (devkitA64/libnx).
#
# Run in WSL/Linux (NOT msys2). The whole build lives in WSL now: msys2's native-exe
# process spawning degrades over a large build and intermittently misreports portlib
# headers as "No such file". Linux spawning is reliable, so a normal parallel make
# works. The Switch portlibs are mirrored into WSL's /opt/devkitpro/portlibs/switch
# (see build/switch/setup-wsl-portlibs.sh).
#
#   wsl -d archlinux -e bash build/switch/build-engine.sh
set -uo pipefail
export DEVKITPRO=/opt/devkitpro
export PATH=/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:$PATH

CFG="${CFG:-release}"
JOBS="${JOBS:-$(nproc)}"
WS="$(cd "$(dirname "${BASH_SOURCE[0]}")/../workspaces/switch" && pwd)"
cd "$WS"

# Build everything except the final pyrogenesis exe link (that's the separate NRO
# link step). Pass an explicit target (e.g. "clean") to override.
if [ "$#" -eq 0 ]; then
	set -- $(for mk in *.make; do p="${mk%.make}"; [ "$p" = pyrogenesis ] || echo "$p"; done)
fi

make "$@" config="$CFG" \
	CC=aarch64-none-elf-gcc CXX=aarch64-none-elf-g++ AR=aarch64-none-elf-ar \
	-j"$JOBS"
