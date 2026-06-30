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
# along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.

"""Lint PO- and POT-files."""

# ruff: noqa: RUF001

import logging
import re
import sys
from collections.abc import Generator
from itertools import chain
from pathlib import Path

import click
from click import ClickException
from dennis.linter import ERROR, LintedEntry, Linter, LintMessage, LintRule, get_lint_rules
from dennis.templatelinter import TemplateLinter
from dennis.templatelinter import get_lint_rules as get_template_lint_rules
from dennis.tools import VariableTokenizer, get_available_formats, withlines


PROJECT_ROOT_DIRECTORY = Path(__file__).parent.parent.parent.parent
L10N_FOLGER_NAME = "l10n"

logger = logging.getLogger(__name__)


class URLSpamRule(LintRule):
    num = "E901"
    name = "urlspam"
    desc = "msgstr contains URL not present in msgid"

    def __init__(self):
        super().__init__()
        self.url_regex = re.compile(
            r"https?://(?:[a-z0-9-_$@./&+]|(?:%[0-9a-fA-F][0-9a-fA-F]))+", re.IGNORECASE
        )

    def lint(self, _: VariableTokenizer, linted_entry: LintedEntry) -> list[LintMessage]:
        msgs = []

        for trstr in linted_entry.strs:
            urls = self.url_regex.findall(trstr.msgstr_string)
            if not urls:
                continue

            for url in urls:
                if url in linted_entry.msgid:
                    continue

                msgs.append(
                    LintMessage(
                        ERROR,
                        linted_entry.poentry.linenum,
                        0,
                        self.num,
                        f"translation contains spam URL: {url}",
                        linted_entry.poentry,
                    )
                )

        return msgs


class MissingTagRule(LintRule):
    num = "E902"
    name = "missing-tag"
    desc = "tags present in msgid aren't present in msgstr"

    def __init__(self):
        super().__init__()
        self.tag_regex = re.compile(r"(?<![\\\\])\[[^]]+\]")

    def lint(self, _: VariableTokenizer, linted_entry: LintedEntry) -> list[LintMessage]:
        msgs = []

        for trstr in linted_entry.strs:
            msgid_tags = []
            for s in trstr.msgid_strings:
                msgid_tags += self.tag_regex.findall(s)

            msgstr_tags = self.tag_regex.findall(trstr.msgstr_string)
            if not msgstr_tags:
                continue

            missing_in_msgstr = [tag for tag in msgid_tags if tag not in msgstr_tags]
            if missing_in_msgstr:
                msgs.append(
                    LintMessage(
                        ERROR,
                        linted_entry.poentry.linenum,
                        0,
                        self.num,
                        f"missing tags: {', '.join(sorted(missing_in_msgstr))}",
                        linted_entry.poentry,
                    )
                )

        return msgs


class InvalidTagRule(LintRule):
    num = "E903"
    name = "invalid-tag"
    desc = "tags not present in msgid are present in msgstr"

    def __init__(self):
        super().__init__()
        self.tag_regex = re.compile(r"(?<![\\\\])\[[^]]+\]")

    def lint(self, _: VariableTokenizer, linted_entry: LintedEntry) -> list[LintMessage]:
        msgs = []

        for trstr in linted_entry.strs:
            msgid_tags = []
            for s in trstr.msgid_strings:
                msgid_tags += self.tag_regex.findall(s)

            msgstr_tags = self.tag_regex.findall(trstr.msgstr_string)
            if not msgstr_tags:
                continue

            not_in_msgid = [tag for tag in msgstr_tags if tag not in msgid_tags]
            if not_in_msgid:
                msgs.append(
                    LintMessage(
                        ERROR,
                        linted_entry.poentry.linenum,
                        0,
                        self.num,
                        f"invalid tags: {', '.join(sorted(not_in_msgid))}",
                        linted_entry.poentry,
                    )
                )

        return msgs


class WrongWhitespaceIn0AD(LintRule):
    num = "E904"
    name = "wrong-whitespace-in-translatable-string"
    desc = "msgid contains 0 A.D. without non-breaking space"

    def __init__(self):
        super().__init__()
        self.zeroad_regex = re.compile(r"0[^ ]?A[\s \.]{1,2}D[\s \.]{1,2}", flags=re.IGNORECASE)

    def lint(self, _: VariableTokenizer, linted_entry: LintedEntry) -> list[LintMessage]:
        msgs = []

        for trstr in linted_entry.strs:
            wrong_whitespaces = []
            for msgid in trstr.msgid_strings:
                if self.zeroad_regex.search(msgid):
                    wrong_whitespaces.append(msgid)

            if wrong_whitespaces:
                msgs.append(
                    LintMessage(
                        ERROR,
                        linted_entry.poentry.linenum,
                        0,
                        self.num,
                        f"string contains 0 A.D. without non-breaking space: "
                        f"{trstr.msgstr_string}",
                        linted_entry.poentry,
                    )
                )

        return msgs


class WrongWhitespaceIn0ADInTranslation(LintRule):
    num = "E905"
    name = "wrong-whitespace-in-translation"
    desc = "msgstr contains 0 A.D. without non-breaking space"

    def __init__(self):
        super().__init__()
        self.zeroad_regex = re.compile(r"0[^ ]?A[\s \.]{1,2}D[\s \.]{1,2}", flags=re.IGNORECASE)

    def lint(self, _: VariableTokenizer, linted_entry: LintedEntry) -> list[LintMessage]:
        msgs = []

        for trstr in linted_entry.strs:
            if self.zeroad_regex.search(trstr.msgstr_string):
                msgs.append(
                    LintMessage(
                        ERROR,
                        linted_entry.poentry.linenum,
                        0,
                        self.num,
                        f"translation contains 0 A.D. without non-breaking space: "
                        f"{trstr.msgstr_string}",
                        linted_entry.poentry,
                    )
                )

        return msgs


def get_relative_path(path: Path) -> Path:
    """Get relative path to project directory.

    If the path isn't below the project directory, return the unchanged path.
    """
    try:
        return path.relative_to(PROJECT_ROOT_DIRECTORY)
    except ValueError:
        return path


def get_po_files() -> Generator[Path, None, None]:
    """Return a list of PO- and POT-files in the project."""
    for l10n_dir in Path(PROJECT_ROOT_DIRECTORY).glob(f"**/{L10N_FOLGER_NAME}"):
        yield from chain(l10n_dir.glob("*.po"), l10n_dir.glob("*.pot"))


def lint_files(
    paths: list[Path], rules: list[str] | None
) -> Generator[tuple[Path, LintMessage], None, None]:
    """Lint files and return results."""
    varformat = get_available_formats().keys()

    if rules:
        lint_rules = rules
        template_lint_rules = rules
    else:
        lint_rules = get_lint_rules()
        del lint_rules["W302"]
        template_lint_rules = get_template_lint_rules()

    linter = Linter(varformat, lint_rules)
    template_linter = TemplateLinter(varformat, template_lint_rules)

    for path in paths:
        if path.suffix == ".po":
            results = linter.verify_file(path)
        elif path.suffix == ".pot":
            results = template_linter.verify_file(path)
        else:
            raise ValueError(f"Unexpected file type for {path}")
        for result in results:
            yield path, result


def print_results(
    results: Generator[tuple[Path, LintMessage], None, None], num_files: int
) -> None:
    """Format results and print them to stdout."""
    num_warnings = 0
    num_errors = 0
    for path, result in results:
        if result.kind == "err":
            code_color = "red"
            num_errors += 1
        elif result.kind == "warn":
            code_color = "yellow"
            num_warnings += 1
        else:
            raise ValueError(f"Unexpected lint result code {result.kind}")

        click.secho(f"{click.format_filename(get_relative_path(path))}", bold=True, nl=False)
        click.echo(f":{result.line}: ", nl=False)
        click.echo(click.style(result.code, fg=code_color) + f" {result.msg}")
        click.echo(withlines(result.poentry.linenum, result.poentry.original))
        click.echo()

    click.echo(f"Found {num_errors} errors and {num_warnings} warnings in {num_files} files.")
    if num_errors > 0:
        sys.exit(1)


def run(
    locales: str, files: list[Path], rules: list[str] | None
) -> tuple[Generator[tuple[Path, LintMessage], None, None], int]:
    """Collect files to lint and run linting."""
    if files:
        po_files = []
        for f in files:
            if f.suffix in [".po", ".pot"]:
                po_files.append(f)
            else:
                logger.warning(
                    "%s is not a valid PO- or POT-file. Skipping.", get_relative_path(f)
                )
    else:
        po_files = list(get_po_files())

    if locales:
        files_to_check = []
        for f in po_files:
            lang_code = f.stem.split(".")[0]
            if lang_code in locales:
                files_to_check.append(f)
    else:
        files_to_check = po_files

    if len(files_to_check) == 0:
        raise ClickException("Found no files to check.")

    return lint_files(files_to_check, rules), len(files_to_check)


@click.command()
@click.option(
    "--locales", help="Comma-separated list of locales to check. Defaults to all locales"
)
@click.option("--rules", help="Comma-separated list of lint rules to use. Defaults to all rules.")
@click.argument("files", nargs=-1, type=click.Path(exists=True, resolve_path=True, path_type=Path))
def cli(locales, rules, files):
    """Lint PO- and POT-files.

    Provide one or multiple FILES to check. If omitted all files in
    the project will be checked.
    """
    if rules:
        rules = [rule.strip() for rule in rules.split(",") if rule.strip()]
    if locales:
        locales = [locale.strip() for locale in locales.split(",") if locale.strip()]

    lint_results, num_checked_files = run(locales, files, rules)
    print_results(lint_results, num_checked_files)


if __name__ == "__main__":
    cli()
