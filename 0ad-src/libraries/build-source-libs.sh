#!/bin/sh

die()
{
	echo ERROR: "$*"
	exit 1
}

if [ "$(uname -s)" = "Darwin" ]; then
	die "This script should not be used on macOS: use build-macos-libs.sh instead."
fi

cd "$(dirname "$0")" || die
# Now in libraries/ (where we assume this script resides)

# Check for whitespace in absolute path; this will cause problems in the
# SpiderMonkey build and maybe elsewhere, so we just forbid it.
case "$(realpath .)" in
	*[[:space:]]*)
		die "Absolute path contains whitespace, which will break the build - move the game to a path without spaces"
		;;
esac

print_help()
{
	cat <<EOF
usage:
	build-source-libs.sh [<options>]

options:
	--help                  - print this help
	--fetch-only            - only fetch sources
	--force-rebuild         - rebuild all
	--without-nvtt          - don't build nvtt
	--with-system-cxxtest   - don't build cxxtest
	--with-system-nvtt      - don't build nvtt
	--with-system-mozjs     - don't build spidermonkey
	--with-system-premake   - don't build premake
	--with-spirv-reflect    - build spirv-reflect
	-j<N>                   - use N threads
EOF
}

without_nvtt=false
with_system_cxxtest=false
with_system_nvtt=false
with_system_mozjs=false
with_system_premake=false
with_spirv_reflect=false

JOBS=${JOBS:="-j2"}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--help)
			print_help
			exit
			;;
		--fetch-only) build_sh_options="$build_sh_options --fetch-only" ;;
		--force-rebuild) build_sh_options="$build_sh_options --force-rebuild" ;;
		--without-nvtt) without_nvtt=true ;;
		--with-system-cxxtest) with_system_cxxtest=true ;;
		--with-system-nvtt) with_system_nvtt=true ;;
		--with-system-mozjs) with_system_mozjs=true ;;
		--with-system-premake) with_system_premake=true ;;
		--with-spirv-reflect) with_spirv_reflect=true ;;
		-j*) JOBS="$1" ;;
		*)
			printf "Unknown option: %s\n\n" "$1"
			print_help
			exit 1
			;;
	esac
	shift
done

# Some of our makefiles depend on GNU make, so we set some sane defaults if MAKE
# is not set.
case "$(uname -s)" in
	"FreeBSD" | "OpenBSD")
		MAKE=${MAKE:="gmake"}
		;;
	*)
		MAKE=${MAKE:="make"}
		;;
esac

export MAKE JOBS

# Build/update bundled external libraries
echo "Building third-party dependencies..."

if [ "$with_system_cxxtest" = "false" ]; then
	# shellcheck disable=SC2086
	./source/cxxtest-4.4/build.sh $build_sh_options || die "cxxtest build failed"
fi
# shellcheck disable=SC2086
./source/fcollada/build.sh $build_sh_options || die "FCollada build failed"
if [ "$with_system_nvtt" = "false" ] && [ "$without_nvtt" = "false" ]; then
	# shellcheck disable=SC2086
	./source/nvtt/build.sh $build_sh_options || die "NVTT build failed"
	cp source/nvtt/bin/* ../binaries/system/
fi
if [ "$with_system_premake" = "false" ]; then
	# shellcheck disable=SC2086
	./source/premake-core/build.sh $build_sh_options || die "Premake build failed"
fi
if [ "$with_system_mozjs" = "false" ]; then
	# shellcheck disable=SC2086
	./source/spidermonkey/build.sh $build_sh_options || die "SpiderMonkey build failed"
	cp source/spidermonkey/lib/* ../binaries/system/
fi
if [ "$with_spirv_reflect" = "true" ]; then
	# shellcheck disable=SC2086
	./source/spirv-reflect/build.sh $build_sh_options || die "spirv-reflect build failed"
fi

echo "Done."
