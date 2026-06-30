#!/bin/bash
# Resolve crash addresses in pyrogenesis.elf. Usage: crashinfo.sh <module_off> [...]
# The module base in the emulator/loader log is subtracted already (rtld:0xOFF).
export PATH=/opt/devkitpro/devkitA64/bin:$PATH
ELF=/mnt/d/0ad-switch/0ad-src/binaries/system/pyrogenesis.elf
OUT=/mnt/c/tmp/crashinfo.txt
{
	echo "=== ELF entry + first LOAD vaddr ==="
	aarch64-none-elf-readelf -hl "$ELF" | grep -E "Entry|LOAD" | head -5
	echo
	for off in "$@"; do
		echo "=== module+$off ==="
		aarch64-none-elf-addr2line -e "$ELF" -f -C -i "$off"
	done
} > "$OUT" 2>&1
echo "wrote $OUT"
