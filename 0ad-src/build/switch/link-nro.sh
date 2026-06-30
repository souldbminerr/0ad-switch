#!/bin/bash
set -uo pipefail
export DEVKITPRO=/opt/devkitpro
export PATH=/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:$PATH

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"          # 0ad-src
SYS="$ROOT/binaries/system"                                         # engine .a + main.o
OBJ="$ROOT/build/workspaces/switch/obj/pyrogenesis_Release"        # main.o
LIBNX="$DEVKITPRO/libnx"
PORTLIBS="$DEVKITPRO/portlibs/switch"
SMLIBS="${SMLIBS:-$ROOT/../libs}"   # libjs_static.a, libjsrust.a, libswitchextra.a (repo-root /libs)

ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE"

NRO_LDSCRIPT="$ROOT/build/switch/switch_nro.ld"
NRO_SPECS="$ROOT/build/switch/switch_nro.specs"   # generated build artifact
sed "s#%:getenv(DEVKITPRO /libnx/switch.ld)#$NRO_LDSCRIPT#" "$LIBNX/switch.specs" > "$NRO_SPECS"
LDSPECS="-specs=$NRO_SPECS"
LDFLAGS="$LDSPECS -g $ARCH -Wl,-z,notext -Wl,-z,nopack-relative-relocs"

ENGINE_LIBS="-lmocks_real -lnetwork -lrlinterface -ltinygettext -llobby -lsimulation2 -lscriptinterface -lengine -lgraphics -latlas -lgui -llowlevel -lgladwrapper -lmongoose"
SM_LIBS="-ljs_static -ljsrust -lswitchextra"
PORTLIB_LINK="-lSDL2 -lEGL -lglapi -ldrm_nouveau -lxml2 -lenet -lcurl -licui18n -licuuc -licudata -lsodium -lfmt -lfreetype -lbz2 -lpng16 -lz -lharfbuzz -lopenal -lvorbisfile -lvorbis -logg"

echo "=== compiling main.o (rebuilt if main.cpp changed) ==="
( cd "$ROOT/build/workspaces/switch" && \
  make -f pyrogenesis.make config=release \
    CC=aarch64-none-elf-gcc CXX=aarch64-none-elf-g++ AR=aarch64-none-elf-ar \
    "obj/pyrogenesis_Release/main.o" ) \
  || { echo "FATAL: main.o failed to compile -- aborting (was silently linking a stale main.o)"; exit 1; }

COMPAT_SRC="$ROOT/build/switch/src/switch_engine_compat.c"
COMPAT_O="$ROOT/build/workspaces/switch/obj/switch_engine_compat.o"
echo "=== compiling compat shims ==="
aarch64-none-elf-gcc $ARCH -D__SWITCH__ -D_GNU_SOURCE -O2 \
	-I"$LIBNX/include" -I"$PORTLIBS/include" -c "$COMPAT_SRC" -o "$COMPAT_O"

AFFINITY_SRC="$ROOT/build/switch/src/switch_thread_affinity.c"
AFFINITY_O="$ROOT/build/workspaces/switch/obj/switch_thread_affinity.o"
echo "=== compiling thread-affinity shim ==="
aarch64-none-elf-gcc $ARCH -D__SWITCH__ -D_GNU_SOURCE -O2 \
	-I"$LIBNX/include" -I"$PORTLIBS/include" -c "$AFFINITY_SRC" -o "$AFFINITY_O"

KEYBOARD_SRC="$ROOT/build/switch/src/switch_keyboard.c"
KEYBOARD_O="$ROOT/build/workspaces/switch/obj/switch_keyboard.o"
echo "=== compiling keyboard shim ==="
aarch64-none-elf-gcc $ARCH -D__SWITCH__ -D_GNU_SOURCE -O2 \
	-I"$LIBNX/include" -I"$PORTLIBS/include" -c "$KEYBOARD_SRC" -o "$KEYBOARD_O"

OUT="$ROOT/binaries/system/pyrogenesis"
echo "=== linking pyrogenesis.elf ==="
aarch64-none-elf-g++ $LDFLAGS -Wl,--wrap=pthread_setname_np \
	"$OBJ/main.o" "$COMPAT_O" "$AFFINITY_O" "$KEYBOARD_O" \
	-L"$SYS" -L"$SMLIBS" -L"$PORTLIBS/lib" -L"$LIBNX/lib" \
	-Wl,--start-group $ENGINE_LIBS $SM_LIBS -Wl,--end-group \
	$PORTLIB_LINK \
	-lnx -lm \
	-o "$OUT.elf" 2>&1 | tee "${LINKLOG:-/tmp/pyro_link.log}" | tail -40

if [ -f "$OUT.elf" ]; then
	echo "=== pyrogenesis.elf linked ($(du -h "$OUT.elf" | cut -f1)); building NRO ==="
	nacptool --create "0 A.D." "Wildfire Games and Souldbminer" "Release 28" "$OUT.nacp"
	ICON="$ROOT/build/switch/icon.jpg"
	NROARGS=(--nacp="$OUT.nacp")
	[ -f "$ICON" ] && NROARGS+=(--icon="$ICON")
	elf2nro "$OUT.elf" "$OUT.nro" "${NROARGS[@]}" && echo "=== built $OUT.nro ==="
fi
