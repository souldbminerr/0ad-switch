from pathlib import Path
from tempfile import NamedTemporaryFile
from unittest import TestCase

from check_translations import lint_files


class TestCheckTranslations(TestCase):
    def test_var_missing_in_msgstr(self):
        with NamedTemporaryFile(mode="w+", suffix=".po") as f:
            f.write(r"""
#: simulation/components/EntityLimits.js:195
#, javascript-format
msgid "%(category)s build limit of %(limit)s reached"
msgstr "Baulimit von %(limit)s erreicht"
            """)
            f.flush()
            path_to_lint = Path(f.name)
            lint_results = list(lint_files([path_to_lint]))

        self.assertEqual(1, len(lint_results))
        linted_file, lint_result = lint_results[0]
        self.assertEqual(linted_file, path_to_lint)
        self.assertEqual("W202", lint_result.code)
        self.assertEqual("warn", lint_result.kind)
        self.assertEqual("missing variables: %(category)s", lint_result.msg)

    def test_var_missing_in_msgid(self):
        with NamedTemporaryFile(mode="w+", suffix=".po") as f:
            f.write(r"""
#: simulation/components/EntityLimits.js:195
#, javascript-format
msgid "build limit of %(limit)s reached"
msgstr "%(category)s Baulimit von %(limit)s erreicht"
            """)
            f.flush()
            path_to_lint = Path(f.name)
            lint_results = list(lint_files([path_to_lint]))

        self.assertEqual(1, len(lint_results))
        linted_file, lint_result = lint_results[0]
        self.assertEqual(linted_file, path_to_lint)
        self.assertEqual("E201", lint_result.code)
        self.assertEqual("err", lint_result.kind)
        self.assertEqual("invalid variables: %(category)s", lint_result.msg)

    def test_spam_url_detection(self):
        with NamedTemporaryFile(mode="w+", suffix=".po") as f:
            f.write(r"""
#: gui/incompatible_mods/incompatible_mods.xml:(caption):12
msgid "Incompatible mods"
msgstr "Inkompatible Mods http://example.tld/"
            """)
            f.flush()
            path_to_lint = Path(f.name)
            lint_results = list(lint_files([path_to_lint]))

        self.assertEqual(1, len(lint_results))
        linted_file, lint_result = lint_results[0]
        self.assertEqual(linted_file, path_to_lint)
        self.assertEqual("E901", lint_result.code)
        self.assertEqual("err", lint_result.kind)
        self.assertEqual("translation contains spam URL: http://example.tld/", lint_result.msg)

    def test_missing_tag_in_msgstr(self):
        with NamedTemporaryFile(mode="w+", suffix=".po") as f:
            f.write(r"""
#: gui/prelobby/common/terms/Privacy_Policy.txt:6
msgid "[font=\"sans-bold-14\"]1. Player name[/font]"
msgstr "[font=\"sans-bold-14\"]1. Spielername"
            """)
            f.flush()
            path_to_lint = Path(f.name)
            lint_results = list(lint_files([path_to_lint]))

        self.assertEqual(1, len(lint_results))
        linted_file, lint_result = lint_results[0]
        self.assertEqual(linted_file, path_to_lint)
        self.assertEqual("E902", lint_result.code)
        self.assertEqual("err", lint_result.kind)
        self.assertEqual("missing tags: [/font]", lint_result.msg)

    def test_missing_tag_in_msgstr_escaped(self):
        with NamedTemporaryFile(mode="w+", suffix=".po") as f:
            f.write(r"""
#: gui/pregame/MainMenuItems.js:194
msgid "Launch the multiplayer lobby. \\[DISABLED BY BUILD]"
msgstr "Multiplayer-Lobby starten. \\[IN BUILD DEAKTIVIERT]"
            """)
            f.flush()
            path_to_lint = Path(f.name)
            lint_results = list(lint_files([path_to_lint]))

        self.assertEqual(0, len(lint_results))

    def test_invalid_tag_in_msgstr(self):
        with NamedTemporaryFile(mode="w+", suffix=".po") as f:
            f.write(r"""
#: gui/prelobby/common/terms/Privacy_Policy.txt:6
msgid "[font=\"sans-bold-14\"]1. Player name"
msgstr "[font=\"sans-bold-14\"]1. Spielername[/font]"
            """)
            f.flush()
            path_to_lint = Path(f.name)
            lint_results = list(lint_files([path_to_lint]))

        self.assertEqual(1, len(lint_results))
        linted_file, lint_result = lint_results[0]
        self.assertEqual(linted_file, path_to_lint)
        self.assertEqual("E903", lint_result.code)
        self.assertEqual("err", lint_result.kind)
        self.assertEqual("invalid tags: [/font]", lint_result.msg)
