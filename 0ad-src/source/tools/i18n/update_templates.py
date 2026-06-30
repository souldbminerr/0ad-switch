#!/usr/bin/env python3
#
# Copyright (C) 2022 Wildfire Games.
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

import argparse
import glob
import json
import multiprocessing
import os
from importlib import import_module

from i18n_helper import L10N_FOLDER_NAME, PROJECT_ROOT_DIRECTORY
from i18n_helper.catalog import Catalog


messages_filename = "messages.json"


def warn_about_untouched_mods():
    """Warn about mods that are not properly configured to get their messages extracted."""
    mods_root_folder = os.path.join(PROJECT_ROOT_DIRECTORY, "binaries", "data", "mods")
    untouched_mods = {}
    for mod_folder in os.listdir(mods_root_folder):
        if mod_folder.startswith(("_", ".")):
            continue

        if not os.path.exists(os.path.join(mods_root_folder, mod_folder, L10N_FOLDER_NAME)):
            untouched_mods[mod_folder] = (
                f"There is no '{L10N_FOLDER_NAME}' folder in the root folder of this mod."
            )
        elif not os.path.exists(
            os.path.join(mods_root_folder, mod_folder, L10N_FOLDER_NAME, messages_filename)
        ):
            untouched_mods[mod_folder] = (
                f"There is no '{messages_filename}' file within the '{L10N_FOLDER_NAME}' "
                f"folder in the root folder of this mod."
            )

    if untouched_mods:
        print("Warning: No messages were extracted from the following mods:")
        for mod_folder, error in untouched_mods.items():
            print(f"• {mod_folder}: {error}")
        print(
            ""
            f"For this script to extract messages from a mod folder, this mod folder must contain "
            f"a '{L10N_FOLDER_NAME}' folder, and this folder must contain a '{messages_filename}' "
            f"file that describes how to extract messages for the mod. See the folder of the main "
            f"mod ('public') for an example, and see the documentation for more information."
        )


def generate_pot(template_settings, root_path):
    if "skip" in template_settings and template_settings["skip"] == "yes":
        return

    input_root_path = root_path
    if "inputRoot" in template_settings:
        input_root_path = os.path.join(root_path, template_settings["inputRoot"])

    template = Catalog(
        project=template_settings["project"],
        copyright_holder=template_settings["copyrightHolder"],
        locale="en",
    )

    for rule in template_settings["rules"]:
        if "skip" in rule and rule["skip"] == "yes":
            return

        options = rule.get("options", {})
        extractor_class = getattr(
            import_module("i18n_helper.extractors"), f"{rule['extractor'].title()}Extractor"
        )
        extractor = extractor_class(input_root_path, rule["filemasks"], options)
        format_flag = None
        if "format" in options:
            format_flag = options["format"]
        for message, plural, context, location, comments in extractor.run():
            message_id = (message, plural) if plural else message

            saved_message = template.get(message_id, context) or template.add(
                id=message_id,
                context=context,
                auto_comments=comments,
                flags=[format_flag] if format_flag and message.find("%") != -1 else [],
            )
            saved_message.locations.append(location)
            saved_message.flags.discard("python-format")

    template.write_to(os.path.join(root_path, template_settings["output"]))
    print('Generated "{}" with {} messages.'.format(template_settings["output"], len(template)))


def generate_templates_for_messages_file(messages_file_path):
    with open(messages_file_path, encoding="utf-8") as file_object:
        settings = json.load(file_object)

    for template_settings in settings:
        multiprocessing.Process(
            target=generate_pot, args=(template_settings, os.path.dirname(messages_file_path))
        ).start()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--scandir",
        help="Directory to start scanning for l10n folders in. "
        "Type '.' for current working directory",
    )
    args = parser.parse_args()
    dir_to_scan = args.scandir or PROJECT_ROOT_DIRECTORY
    for messages_file_path in glob.glob(
        f"**/{L10N_FOLDER_NAME}/{messages_filename}", root_dir=dir_to_scan, recursive=True
    ):
        generate_templates_for_messages_file(
            os.path.abspath(f"{dir_to_scan}/{messages_file_path}")
        )

    warn_about_untouched_mods()


if __name__ == "__main__":
    main()
