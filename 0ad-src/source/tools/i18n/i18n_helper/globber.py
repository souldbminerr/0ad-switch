"""Utils to list .po."""

import os

from i18n_helper.catalog import Catalog


def get_catalogs(input_file_path, filters: list[str] | None = None) -> list[Catalog]:
    """Return a list of "real" catalogs (.po) in the given folder."""
    existing_translation_catalogs = []
    l10n_folder_path = os.path.dirname(input_file_path)
    input_file_name = os.path.basename(input_file_path)

    for filename in os.listdir(str(l10n_folder_path)):
        if filename.startswith("long") or not filename.endswith(".po"):
            continue
        if filename.split(".")[1] != input_file_name.split(".")[0]:
            continue
        if not filters or filename.split(".")[0] in filters:
            existing_translation_catalogs.append(
                Catalog.read_from(
                    os.path.join(l10n_folder_path, filename), locale=filename.split(".")[0]
                )
            )

    return existing_translation_catalogs
