#!/usr/bin/env python3
#
# Copyright (C) 2024 Wildfire Games.
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

"""Remove unnecessary personal data of translators.

This file removes unneeded personal data from the translators. Most notably
the e-mail addresses. We need to translators' nicks for the credits, but no
more data is required.

TODO: ideally we don't even pull the e-mail addresses in the .po files.
However that needs to be fixed on the transifex side, see rP25896. For now
strip the e-mails using this script.
"""

import glob
import os
import re

from i18n_helper import L10N_FOLDER_NAME, PROJECT_ROOT_DIRECTORY, TRANSIFEX_CLIENT_FOLDER


TRANSLATOR_REGEX = re.compile(r"^(#\s+[^,<]*)\s+<.*>(.*)$")
LAST_TRANSLATION_REGEX = re.compile(r"^(\"Last-Translator:[^,<]*)\s+<.*>(.*)$")


def main():
    for folder in glob.iglob(
        f"**/{L10N_FOLDER_NAME}/{TRANSIFEX_CLIENT_FOLDER}/",
        root_dir=PROJECT_ROOT_DIRECTORY,
        recursive=True,
    ):
        for file in glob.iglob(
            f"{os.path.join(folder, os.pardir)}/*.po", root_dir=PROJECT_ROOT_DIRECTORY
        ):
            absolute_file_path = os.path.abspath(f"{PROJECT_ROOT_DIRECTORY}/{file}")

            file_content = []
            usernames = []
            changes = False
            in_translators = False
            found_last_translator = False
            with open(absolute_file_path, "r+", encoding="utf-8") as fd:
                for line in fd:
                    if line.strip() == "# Translators:":
                        in_translators = True
                    elif not line.strip().startswith("#"):
                        in_translators = False
                    elif in_translators:
                        if line == "# \n":
                            changes = True
                            continue
                        translator_match = TRANSLATOR_REGEX.match(line)
                        if translator_match:
                            changes = True
                            if translator_match.group(1) in usernames:
                                continue
                            line = TRANSLATOR_REGEX.sub(r"\1\2", line)
                            usernames.append(translator_match.group(1))

                    if not in_translators and not found_last_translator:
                        last_translator_match = LAST_TRANSLATION_REGEX.match(line)
                        if last_translator_match:
                            found_last_translator = True
                            changes = True
                            line = LAST_TRANSLATION_REGEX.sub(r"\1\2", line)

                    file_content.append(line)

                if changes:
                    fd.seek(0)
                    fd.truncate()
                    fd.writelines(file_content)


if __name__ == "__main__":
    main()
