#!/bin/sh
set -ev

BUNDLE_VERSION=${BUNDLE_VERSION:="0.28.0dev"}
PREFIX="0ad-${BUNDLE_VERSION}"

WINARCH=${WINARCH:="win32"}

# Create Windows installer
# This needs nsisbi for files > 2GB
# nsisbi 3.10.3 is used on the CD
# To build and install on macOS:
# - install mingw-w64 and scons with Homebrew
# - download the latest source at https://sourceforge.net/projects/nsisbi/files/
# - build with `scons SKIPUTILS="Makensisw","NSIS Menu","zip2exe"`
# - install with `sudo scons install SKIPUTILS="Makensisw","NSIS Menu","zip2exe"`
# Running makensis also needs a LANG workaround for https://sourceforge.net/p/nsis/bugs/1165/
LANG="en_GB.UTF-8" makensis -V4 -nocd \
	-dcheckoutpath="." \
	-dversion="${BUNDLE_VERSION}" \
	-dprefix="${PREFIX}" \
	-dwinarch="${WINARCH}" \
	-darchive_path="archives/" \
	source/tools/dist/0ad.nsi

# Fix permissions
chmod -f 644 "${PREFIX}-${WINARCH}.exe"
