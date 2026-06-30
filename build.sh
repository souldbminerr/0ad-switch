#!/bin/bash
set -e
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=/opt/devkitpro/devkitA64
export PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitA64/bin:$PATH
make "${@:2}"
