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

"""Update the translator credits.

This file updates the translator credits located in the public mod GUI files, using
translator names from the .po files.

If translators change their names on Transifex, the script will remove the old names.
TODO: It should be possible to add people in the list manually, and protect them against
automatic deletion. This has not been needed so far. A possibility would be to add an
optional boolean entry to the dictionary containing the name.

Translatable strings will be extracted from the generated file, so this should be run
once before update_templates.py.
"""

import json
import os
import re
from collections import defaultdict
from pathlib import Path

from babel import Locale, UnknownLocaleError
from i18n_helper import L10N_FOLDER_NAME, PROJECT_ROOT_DIRECTORY, TRANSIFEX_CLIENT_FOLDER


# Set of translators by name to exclude from the credits.
EXCLUDE_TRANSLATORS = {"WFG Transifex"}

po_locations = []
for root, folders, _filenames in os.walk(PROJECT_ROOT_DIRECTORY):
    for folder in folders:
        if folder != L10N_FOLDER_NAME:
            continue

        if os.path.exists(os.path.join(root, folder, TRANSIFEX_CLIENT_FOLDER)):
            po_locations.append(os.path.join(root, folder))

credits_location = os.path.join(
    PROJECT_ROOT_DIRECTORY,
    "binaries",
    "data",
    "mods",
    "public",
    "gui",
    "credits",
    "texts",
    "translators.json",
)

# This dictionary will hold creditors lists for each language, indexed by code
langs_lists = defaultdict(list)

# Create the new JSON data
new_json_data = {"Title": "Translators", "Content": []}

# Now go through the list of languages and search the .po files for people

# Prepare some regexes
translator_match = re.compile(r"^#\s+([^,<]*)")
deleted_username_match = re.compile(r"[0-9a-f]{32}(_[0-9a-f]{7})?")

# Search
for location in po_locations:
    files = Path(location).glob("*.po")

    for file in files:
        lang = file.stem.split(".")[0]

        # Skip debug translations
        if lang in ("debug", "long"):
            continue

        with file.open(encoding="utf-8") as po_file:
            reached = False
            for line in po_file:
                if reached:
                    m = translator_match.match(line)
                    if not m:
                        break

                    username = m.group(1)
                    if not deleted_username_match.fullmatch(username):
                        langs_lists[lang].append(username)
                if line.strip() == "# Translators:":
                    reached = True

# Sort translator names and remove duplicates
# Sorting should ignore case, but prefer versions of names starting
# with an upper case letter to have a neat credits list.
for lang in langs_lists:
    translators = {}
    for name in sorted(langs_lists[lang], reverse=True):
        if (name.lower() not in translators or name.istitle()) and name not in EXCLUDE_TRANSLATORS:
            translators[name.lower()] = name
    langs_lists[lang] = sorted(translators.values(), key=lambda s: s.lower())

# Now insert the new data into the new JSON file
for lang_code, lang_list in sorted(langs_lists.items()):
    try:
        lang_name = Locale.parse(lang_code).english_name
    except UnknownLocaleError:
        lang_name = Locale.parse("en").languages.get(lang_code)

        if not lang_name:
            raise

    translators = [{"name": name} for name in lang_list]
    new_json_data["Content"].append({"LangName": lang_name, "List": translators})

# Sort languages by their English names
new_json_data["Content"] = sorted(new_json_data["Content"], key=lambda x: x["LangName"])

# Save the JSON data to the credits file
with open(credits_location, "w", encoding="utf-8") as credits_file:
    json.dump(new_json_data, credits_file, indent=4)
