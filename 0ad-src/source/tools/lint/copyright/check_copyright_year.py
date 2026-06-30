#!/usr/bin/env python3
#
# Copyright (C) 2025 Wildfire Games.
# This file is part of 0 A.D.
#
# 0 A.D. is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# 0 A.D. is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with 0 A.D. If not, see <http://www.gnu.org/licenses/>.

"""pre-commit hook to check for correct copyright year.

This script checks whether files, which contain a copyright notice,
contain the correct copyright year. When run as a pre-commit hook,
it checks the staged files only. When run on all files
(pre-commit run --all-files), to not generate warnings for all files,
it checks that the copyright year matches the year the file was last
modified.

This script will only work for text files encoded in UTF-8. All other
files passed to it will be silently ignored.
"""

import difflib
import re
import subprocess
from argparse import ArgumentDefaultsHelpFormatter, ArgumentError, ArgumentParser
from collections.abc import Sequence
from datetime import UTC, datetime


def check_copyright_year(
    filenames: list[str],
    copyright_regex: re.Pattern,
    lines_to_check: int = 100,
    show_diff: bool = False,
    fix: bool = False,
) -> int:
    """Check files for correct copyright year."""
    diff_process = subprocess.run(
        ["git", "diff", "--cached", "--name-only"], capture_output=True, check=True
    )
    staged_files = diff_process.stdout.decode().split("\n")
    current_year = datetime.now(UTC).date().year
    is_error = False

    last_commit = subprocess.run(
        ["git", "log", "-1", "--pretty=%cI"], capture_output=True, check=True
    )
    last_commit_year = datetime.fromisoformat(last_commit.stdout.decode().strip()).year

    for filename in filenames:
        with open(filename, encoding="utf8") as f:
            if lines_to_check > 0:
                data_list = []
                try:
                    for _ in range(lines_to_check):
                        data_list.append(next(f))
                except StopIteration:
                    pass
                except UnicodeDecodeError:
                    continue
                data = "".join(data_list)
            else:
                try:
                    data = f.read()
                except UnicodeDecodeError:
                    continue

        match = copyright_regex.search(data)
        if not match:
            continue

        copyright_year = int(match.group(1))
        if filename in staged_files:
            if copyright_year == current_year:
                continue
        # Avoid reporting outdated copyright years when commits were
        # done at the end of a year, but the check runs in the
        # following year, by not using the current year, but the year
        # of the last commit as expected year in that case.
        elif copyright_year == last_commit_year:
            continue

        expected_year = current_year

        # file to check isn't staged, so we're likely running with
        # --all-files. Use committer date of the last commit instead
        # as indication what the copyright year should be.
        if filename not in staged_files:
            last_modified = subprocess.run(
                ["git", "log", "-1", "--pretty=%cI", filename],
                capture_output=True,
                check=True,
            )
            last_modified_year = datetime.fromisoformat(last_modified.stdout.decode().strip()).year
            if last_modified_year == copyright_year:
                continue
            # Reporting the last modified year as expected year is
            # probably misleading, if it isn't the current year as
            # well, as changing the copyright year causes the last
            # modification year to be the current year. However,
            # reporting the current year as expected year would be
            # equally confusing, as the file might not have been
            # modified in the current year yet, so the expected year
            # wouldn't match the year the file got last modified.
            expected_year = last_modified_year

        print(f"{filename}: Copyright year {copyright_year} instead of {expected_year}")
        is_error = True

        data_modified = copyright_regex.sub(
            lambda x: x.group(0).replace(x.group(1), str(current_year)), data
        )

        if show_diff:
            show_diff = difflib.unified_diff(data.split("\n"), data_modified.split("\n"), n=2)
            print("\n".join(list(show_diff)[2:]), end="\n\n")

        if fix:
            with open(filename, "r+", encoding="utf8") as f:
                f.write(data_modified)

    return 1 if is_error else 0


def regex_type(value: str) -> re.Pattern:
    """Regex pattern argument type for argparse."""
    try:
        return re.compile(value)
    except re.error as exc:
        raise ArgumentError from exc


def main(argv: Sequence[str] | None = None) -> int:
    """Parse command line parameters and call checking logic."""
    parser = ArgumentParser(
        description="Check files with license header for correct copyright year.",
        formatter_class=lambda prog: ArgumentDefaultsHelpFormatter(prog, width=78),
    )
    parser.add_argument(
        "filenames",
        nargs="*",
        help="Files to check for the copyright year.",
    )
    parser.add_argument(
        "--regex",
        type=regex_type,
        default="(?im)^(?://|/\\*|#) Copyright \\(C?\\) (\\d+) Wildfire Games",
        help="The regex to search for copyright notices and to use to fix copyright years. Must "
        "contain a single capture group with the copyright year.",
    )
    parser.add_argument(
        "--diff",
        action="store_true",
        help="Show differences of actual and desired copyright years",
    )
    parser.add_argument(
        "--fix",
        action="store_true",
        help="Automatically fix outdated coypyright years.",
    )
    parser.add_argument(
        "--lines-to-check",
        type=int,
        default=100,
        help="Number of lines to check to find a copyright notice. Set to 0 or a negative "
        "value to read whole files.",
    )
    args = parser.parse_args(argv)
    return check_copyright_year(
        args.filenames, args.regex, args.lines_to_check, args.diff, args.fix
    )


if __name__ == "__main__":
    raise SystemExit(main())
