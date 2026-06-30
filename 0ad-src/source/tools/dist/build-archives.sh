#!/bin/sh
set -e

die()
{
	echo ERROR: "$*"
	exit 1
}

# Build the mod .zip using the pyrogenesis executable.
# This must be run from the trunk of a nightly build (containing all translations and SPIR-V shaders)

echo "Building archives"

echo "Filtering languages"
# Included languages
# Note: Needs to be edited manually at each release.
# Keep in sync with the installer languages in 0ad.nsi.
LANGS="ast ca cs de el en_GB es eu fi fr gl hu id it ja ko nl pl pt_BR pt_PT ru sk sv tr uk vi zh zh_TW"

# shellcheck disable=SC2086
REGEX=$(printf "\|%s" ${LANGS} | cut -c 2-)
REGEX=".*/\(${REGEX}\)\.[-A-Za-z0-9_.]\+\.po"

find binaries/ -name "*.po" | grep -v "$REGEX" | xargs rm -fv || die "Error filtering languages."

# Build archive(s) - don't archive the _test.* mods
cd binaries/data/mods || die
archives=""
ONLY_MOD="${ONLY_MOD:=false}"
if [ "${ONLY_MOD}" = true ]; then
	archives="mod"
else
	for modname in [a-zA-Z0-9]*; do
		archives="${archives} ${modname}"
	done
fi
cd - || die

for modname in $archives; do
	echo "Building archive for '${modname}'..."
	ARCHIVEBUILD_INPUT="binaries/data/mods/${modname}"
	ARCHIVEBUILD_OUTPUT="archives/${modname}"

	mkdir -p "${ARCHIVEBUILD_OUTPUT}"

	(./binaries/system/pyrogenesis -mod=mod -archivebuild="${ARCHIVEBUILD_INPUT}" -archivebuild-output="${ARCHIVEBUILD_OUTPUT}/${modname}.zip") || die "Archive build for '${modname}' failed!"
	cp "${ARCHIVEBUILD_INPUT}/mod.json" "${ARCHIVEBUILD_OUTPUT}" >/dev/null 2>&1 || true
done
