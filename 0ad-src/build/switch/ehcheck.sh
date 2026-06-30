#!/bin/bash
export PATH=/opt/devkitpro/devkitA64/bin:$PATH
ELF=/mnt/d/0ad-switch/0ad-src/binaries/system/pyrogenesis.elf
{
	echo "=== unwind / exception sections ==="
	aarch64-none-elf-readelf -SW "$ELF" | grep -iE "eh_frame|exidx|gcc_except|ARM.extab" || echo "(none found)"
	echo "=== __register_frame / eh_frame_hdr symbols ==="
	aarch64-none-elf-readelf -sW "$ELF" 2>/dev/null | grep -iE "register_frame|__gxx_personality|_Unwind_" | head
} > /mnt/c/tmp/ehcheck.txt 2>&1
echo wrote
