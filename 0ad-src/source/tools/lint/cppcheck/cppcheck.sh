#!/bin/sh

cd "$(dirname "$0")/../../../.." || exit 1

. source/tools/utils.sh

while [ "$#" -gt 0 ]; do
	case "$1" in
		--diff)
			mode="diff"
			commitish=$2
			shift
			;;
		-j*) JOBS="$1" ;;
		*)
			printf "Unknown option: %s\n\n" "$1"
			exit 1
			;;
	esac
	shift
done

: "${JOBS:=-j$(utils_num_online_cpu)}"

if [ "${mode}" = diff ]; then
	git diff --name-status "${commitish}" | awk '!/^D/{if ($2 ~ /(\.cpp|\.h)$/) {print "./" $2}}' >cppcheck-file-list.txt
else
	find . \( -name '*.cpp' -o -name '*.h' \) >cppcheck-file-list.txt
fi

awk '!/^\.\/(binaries|build|libraries|source\/third_party)\//' <cppcheck-file-list.txt >cppcheck-file-list-filtered.txt

rm -f cppcheck-error.log

cppcheck \
	"${JOBS}" \
	--language=c++ \
	--std=c++17 \
	--force \
	--check-level=exhaustive \
	--library=boost \
	--library=icu \
	--library=libcurl \
	--library=sdl \
	--library=wxwidgets \
	-Isource \
	--file-list=cppcheck-file-list-filtered.txt \
	--suppressions-list=source/tools/lint/cppcheck/suppressions-list.txt \
	2>&1 >/dev/null | tee cppcheck-error.log >&2

rm cppcheck-file-list.txt
rm cppcheck-file-list-filtered.txt

has_errors=$(wc -l <cppcheck-error.log)
if [ "${has_errors}" != 0 ]; then
	exit 1
else
	rm cppcheck-error.log
fi
