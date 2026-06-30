#!/bin/sh
set -e

OS="${OS:=$(uname -s)}"

# This script gets called from inside the extracted SM tarball.
PATCHES="../patches"

# The rust code is only linked if the JS Shell is enabled,
# which fails now that rust is required in all cases.
# https://bugzilla.mozilla.org/show_bug.cgi?id=1588340
patch -p1 <"${PATCHES}"/FixRustLinkage.diff

# Differentiate debug/release library names.
patch -p1 <"${PATCHES}"/FixLibNames.diff

# Add needed debug define in pkg-config file.
# https://bugzilla.mozilla.org/show_bug.cgi?id=1935346
patch -p1 <"${PATCHES}"/FixPkgConfigDebug.diff

# Fix a regression in GC tracing, which creates segfaults during GCs
# https://bugzilla.mozilla.org/show_bug.cgi?id=1982134
patch -p1 <"${PATCHES}"/FixExtraGCTracing.diff

# There is an issue on 32-bit linux builds sometimes.
# NB: the patch here is Comment 21 modified by Comment 25
# but that seems to imperfectly fix the issue with GCC.
# It also won't compile on windows - in doubt, apply only where relevant.
# https://bugzilla.mozilla.org/show_bug.cgi?id=1729459
if [ "$(uname -m)" = "i686" ] && [ "${OS}" != "Windows_NT" ]; then
	patch -p1 <"${PATCHES}"/FixFpNormIssue.diff
fi

# Fix build on macOS
# Workarounds adapted from Homebrew's formula at https://github.com/Homebrew/homebrew-core/blob/main/Formula/s/spidermonkey.rb
# - Allow use of pkg-config to use the same zlib for SM and pyrogenesis (https://bugzilla.mozilla.org/show_bug.cgi?id=1783570)
# - Force allowing build with older macOS SDK
# - Fix invocation of recent compiler (https://bugzilla.mozilla.org/show_bug.cgi?id=1844694)
# - Fix linker selection on macOS 15 (https://bugzilla.mozilla.org/show_bug.cgi?id=1964280)
if [ "${OS}" = "Darwin" ]; then
	patch -p1 <"${PATCHES}"/FixMacOSBuild.diff
fi

# Supress warning on newer GCC compilers.
# https://bugzilla.mozilla.org/show_bug.cgi?id=1953622
patch -p1 <"${PATCHES}"/SuppressDanglingPointerWarning.patch

# Fix building with python 3.14
patch -p1 <"${PATCHES}"/FixPython3_14.diff
