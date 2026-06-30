#!/bin/sh
set -e

cd "$(dirname "$0")"

while [ "$#" -gt 0 ]; do
	case "$1" in
		--diff)
			commitish=$2
			args="${args} --diff $2"
			shift
			;;
		-j*)
			args="${args} $1"
			;;
		*)
			printf "Unknown option: %s\n\n" "$1"
			exit 1
			;;
	esac
	shift
done

has_errors=false

if command -v npm-groovy-lint >/dev/null; then
	# shellcheck disable=SC2086
	./jenkinsfiles/jenkinsfiles.sh ${args} || has_errors=true
else
	echo "npm-groovy-lint not found in path"
fi

if command -v cppcheck >/dev/null; then
	# shellcheck disable=SC2086
	./cppcheck/cppcheck.sh ${args} || has_errors=true
else
	echo "Cppcheck not found in path"
fi

if [ -n "${commitish}" ]; then
	# shellcheck disable=SC2086
	copyright/copyright.sh --from ${commitish} || has_errors=true
else
	"Skipping copyright linter as no base commit was defined"
fi

if [ ${has_errors} = true ]; then
	exit 1
fi
