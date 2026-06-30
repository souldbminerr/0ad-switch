#!/bin/sh
set -e

cd "$(dirname "$0")"

# In case glad is not installed system wide or not installed via pip assume glad
# repository was fetched into glad subdirectory.
export PYTHONPATH="glad"

python -m glad --api="gl:core=2.1" --extensions="extensions/gl.txt" --out-path="." c
python -m glad --api="gles2=2.0" --extensions="extensions/gles2.txt" --out-path="." c
python -m glad --api="glx=1.4" --extensions="extensions/glx.txt" --out-path="." c
python -m glad --api="wgl=1.0" --extensions="extensions/wgl.txt" --out-path="." c
python -m glad --api="egl=1.5" --extensions="extensions/egl.txt" --out-path="." c
python -m glad --api="vulkan=1.1" --extensions="extensions/vulkan.txt" --out-path="." c

patch -p1 --ignore-whitespace --fuzz 1 <fix_macos.patch

mv src/gl.c src/gl.cpp
mv src/gles2.c src/gles2.cpp
mv src/glx.c src/glx.cpp
mv src/wgl.c src/wgl.cpp
mv src/egl.c src/egl.cpp
mv src/vulkan.c src/vulkan.cpp
