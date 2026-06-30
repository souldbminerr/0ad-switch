#!/bin/sh
set -ev

# Download SPIR-V shaders from the latest nightly build

cd "$(dirname "$0")"

for m in "mod" "public"; do
	svn export --force https://svn.wildfiregames.com/nightly-build/trunk/binaries/data/mods/${m}/shaders/spirv ../../../binaries/data/mods/${m}/shaders/spirv
done
