#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

# shellcheck disable=SC2120
die()
{
	[ $# -ne 0 ] && echo ERROR: "$*"
	exit 1
}

doins()
{
	mkdir -p "$2"
	cp -a "$1" "$2" || die
}

JOBS=${JOBS#-j}
njobs=${JOBS:-1}
arch=$(uname -m)
version=9999
while [ "$#" -gt 0 ]; do
	case "$1" in
		--jobs)
			njobs=$2
			shift
			;;
		--root)
			root=$2
			shift
			;;
		--skip-prepacking)
			skip_prepacking="yes"
			;;
		--version)
			version=$2
			shift
			;;
		*)
			printf "Unknown option: %s\n\n" "$1"
			exit 1
			;;
	esac
	shift
done

TOP_SRC_DIR=$(realpath .)
APPDIR="${TOP_SRC_DIR}/appimage-build/AppDir"
APPIMAGE="${TOP_SRC_DIR}/0ad-${version}-${arch}.AppImage"
ROOT="${TOP_SRC_DIR}"
if [ -n "${root}" ]; then
	ROOT=$(realpath "${root}")
fi

cd "${ROOT}" || die

./libraries/build-source-libs.sh "-j${njobs}" || die
./build/workspaces/update-workspaces.sh || die
make -C build/workspaces/gcc "-j${njobs}" || die
if [ -z "${root}" ] && [ -z "${skip_prepacking}" ]; then
	./source/tools/i18n/get-nightly-translations.sh || die
	./source/tools/spirv/get-nightly-shaders.sh || die
	# TODO: Fails at check for root user currently when generating mods archives
	#./source/tools/dist/build-archives.sh
fi

rm -rf "${APPDIR}" "${APPIMAGE}"

doins binaries/system/pyrogenesis "${APPDIR}/usr/bin"
ln -rs "${APPDIR}/usr/bin/pyrogenesis" "${APPDIR}/usr/bin/0ad" || die
for lib in \
	libmozjs*-release.so \
	libnvcore.so \
	libnvimage.so \
	libnvmath.so \
	libnvtt.so; do
	patchelf --set-rpath "${lib}:${ROOT}/binaries/system" "${APPDIR}/usr/bin/pyrogenesis" || die
done

# dlopen libs
doins binaries/system/libAtlasUI.so "${APPDIR}/usr/lib"
doins binaries/system/libCollada.so "${APPDIR}/usr/lib"

doins binaries/data/config/default.cfg "${APPDIR}/usr/data/config"
doins binaries/data/l10n "${APPDIR}/usr/data"
doins binaries/data/tools "${APPDIR}/usr/data"

doins binaries/data/mods/mod "${APPDIR}/usr/data/mods"
doins binaries/data/mods/public "${APPDIR}/usr/data/mods"

doins build/resources/0ad.appdata.xml "${APPDIR}/usr/share/metainfo"
doins build/resources/0ad.png "${APPDIR}/usr/share/pixmaps"

linuxdeploy \
	--appdir "${APPDIR}" \
	--executable "${APPDIR}/usr/bin/pyrogenesis" \
	--desktop-file "${ROOT}/build/resources/0ad.desktop" \
	--icon-file "${ROOT}/build/resources/0ad.png" \
	--icon-filename 0ad || die

appimagetool --comp zstd --mksquashfs-opt -Xcompression-level --mksquashfs-opt 20 "${APPDIR}" "${APPIMAGE}" || die
