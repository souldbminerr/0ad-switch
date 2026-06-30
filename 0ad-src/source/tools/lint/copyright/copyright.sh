#!/bin/sh
while [ "$#" -gt 0 ]; do
	case "$1" in
		--from)
			from_commitish=$2
			shift
			;;
		--to)
			to_commitish=$2
			git checkout --quiet "${to_commitish}"
			shift
			;;
		--fix)
			fix=--fix
			;;
		-j*) ;;
		*)
			printf "Unknown option: %s\n\n" "$1"
			exit 1
			;;
	esac
	shift
done

cd "$(dirname "$0")/../../../.." || exit

if [ -n "${from_commitish}" ]; then
	if [ -n "${to_commitish}" ]; then
		diff="${from_commitish}..${to_commitish}"
	else
		diff="${from_commitish}..$(git rev-parse HEAD)"
	fi
	printf "Running copyright linter for range:\n%s\n\n" "${diff}"
fi

if [ -n "${diff}" ]; then
	printf "Commits to check:\n%s\n\n" "$(git rev-list "${diff}")"
	rm -f copyright-check-error.log
	# shellcheck disable=SC2086
	git rev-list "${diff}" |
		xargs -L1 git diff-tree --no-commit-id --name-only --diff-filter AM -r |
		xargs ./source/tools/lint/copyright/check_copyright_year.py ${fix} >copyright-check-error.log
	cat copyright-check-error.log
	if [ -s copyright-check-error.log ]; then
		exit 1
	fi
else
	echo "WARNING: running copyright linter without base commit, likely not what you want."
	find . -type f -print0 |
		xargs -0 -L100 ./source/tools/lint/copyright/check_copyright_year.py
fi
