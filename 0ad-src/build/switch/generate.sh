#!/bin/bash
# Generate the Pyrogenesis gmake workspace for Nintendo Switch (devkitA64/libnx).
#
# Run this in WSL (Linux), NOT msys2: premake must run on a host where
# os.istarget("linux") is true so 0 A.D. selects its POSIX/unix sources (the same
# "fake Linux" approach used for SpiderMonkey). pkg-config is pointed at the msys2
# portlibs (.pc prefix /opt/devkitpro/... is valid again when we COMPILE in msys2).
#
#   wsl -d archlinux -e bash /mnt/d/0ad-switch/0ad-src/build/switch/generate.sh
#
# Then compile in msys2 with build/switch/build-engine.sh.
set -euo pipefail

export DEVKITPRO=/opt/devkitpro
export PATH="/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:$PATH"
export CC=aarch64-none-elf-gcc CXX=aarch64-none-elf-g++ AR=aarch64-none-elf-ar
# Switch portlibs pkg-config, mirrored natively into WSL by setup-wsl-portlibs.sh.
export PKG_CONFIG_LIBDIR=/opt/devkitpro/portlibs/switch/lib/pkgconfig

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"          # 0ad-src
PM="$ROOT/libraries/source/premake-core/bin/premake5"

if [ ! -x "$PM" ]; then
	echo "premake5 not built. Build it OUTSIDE any git tree (else git describe grabs"
	echo "0 A.D.'s 0.28.0 tag and the version gate aborts), then place it at:"
	echo "  $PM"
	exit 1
fi

cd "$ROOT/build/premake"
# Clean the output dir: premake never deletes stale per-project .make files.
rm -rf "$ROOT/build/workspaces/switch"
"$PM" --file=premake5.lua --outpath=../workspaces/switch/ --switch gmake
# Both generation AND compilation run in WSL/Linux now (msys2's process spawning
# was too flaky for the large engine projects), so the native /opt/devkitpro paths
# are correct as-is -- no Windows-path rewrite needed.
echo "=== generated $ROOT/build/workspaces/switch ==="
