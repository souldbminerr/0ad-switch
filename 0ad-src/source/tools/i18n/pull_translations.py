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

import os
import subprocess

from i18n_helper import L10N_FOLDER_NAME, PROJECT_ROOT_DIRECTORY, TRANSIFEX_CLIENT_FOLDER


def main():
    for root, folders, _ in os.walk(PROJECT_ROOT_DIRECTORY):
        for folder in folders:
            if folder != L10N_FOLDER_NAME:
                continue

            if os.path.exists(os.path.join(root, folder, TRANSIFEX_CLIENT_FOLDER)):
                path = os.path.join(root, folder)
                os.chdir(path)
                print(f"INFO: Starting to pull translations in {path}...")
                subprocess.run(["tx", "pull", "--all", "--force", "--workers=12"], check=False)


if __name__ == "__main__":
    main()
