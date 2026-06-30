#!/bin/sh
set -e

cd "$(dirname "$0")/../../../.." || exit 1

while [ "$#" -gt 0 ]; do
	case "$1" in
		--from)
			from_commitish=$2
			shift
			;;
		--to)
			to_commitish=$2
			shift
			;;
		-j*) ;;
		*)
			printf "Unknown option: %s\n\n" "$1"
			exit 1
			;;
	esac
	shift
done

if [ -n "${from_commitish}" ]; then
	if [ -n "${to_commitish}" ]; then
		diff="${from_commitish}..${to_commitish}"
	else
		diff="${from_commitish}..$(git rev-parse HEAD)"
	fi
	printf "Running npm-groovy-lint linter for range\n%s\n\n" "${diff}"
fi

if [ -n "${diff}" ]; then
	git diff --name-status --no-renames "${diff}" |
		awk '!/^D/{if ($2 ~ /(\.Jenkinsfile)$/) {print "./" $2}}' |
		xargs -n 1 npm-groovy-lint -r .groovylintrc.json
else
	echo "WARNING: running npm-groovy-lint linter without base commit, likely not what you want."
	find "build/jenkins/pipelines" \( -name '*.Jenkinsfile' \) >npm-groovy-lint-file-list.txt
	xargs -n 1 npm-groovy-lint -r .groovylintrc.json <npm-groovy-lint-file-list.txt
	rm npm-groovy-lint-file-list.txt
fi
