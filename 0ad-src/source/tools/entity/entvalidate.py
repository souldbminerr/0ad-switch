#!/usr/bin/env python3

import argparse
import logging
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from xml.etree import ElementTree as ET

from lxml import etree
from lxml.etree import DocumentInvalid, ElementTree
from scriptlib import SimulTemplateEntity, find_files


SIMUL_TEMPLATES_PATH = Path("simulation/templates")
ENTITY_RELAXNG_FNAME = "entity.rng"


def init_logger(log_level) -> logging.Logger:
    """Initialize a logger."""
    logger = logging.getLogger(__name__)
    logger.setLevel(log_level)
    handler = logging.StreamHandler(sys.stdout)
    handler.setLevel(log_level)
    handler.setFormatter(logging.Formatter("%(levelname)s: %(message)s"))
    logger.addHandler(handler)
    return logger


def validate_template(
    simulation_template_entity: SimulTemplateEntity,
    template_path: Path,
    relaxng_schema: ElementTree,
    mod_name: str,
) -> None:
    """Validate a single template."""
    entity = simulation_template_entity.load_inherited(
        SIMUL_TEMPLATES_PATH, str(template_path.relative_to(SIMUL_TEMPLATES_PATH)), [mod_name]
    )
    relaxng_schema.assertValid(etree.fromstring(ET.tostring(entity, encoding="utf-8")))


class ValidationError(Exception):
    pass


def validate_templates(
    logger: logging.Logger,
    vfs_root: Path,
    mod_name: str,
    relaxng_schema_path: Path,
    templates: list[Path] | None,
) -> None:
    """Validate templates against the given RELAX NG schema."""
    if templates:
        templates = [(template, None) for template in templates]
    else:
        templates = find_files(vfs_root.resolve(), [mod_name], SIMUL_TEMPLATES_PATH, ["xml"])

    templates_to_validate = []
    for fp, _ in templates:
        if fp.stem.startswith("template_"):
            continue

        template_path = fp.as_posix()
        if template_path.startswith(
            (
                f"{SIMUL_TEMPLATES_PATH.as_posix()}/mixins/",
                f"{SIMUL_TEMPLATES_PATH.as_posix()}/special/",
            )
        ):
            continue

        templates_to_validate.append(fp)

    simulation_template_entity = SimulTemplateEntity(vfs_root, logger)
    relaxng_schema = etree.RelaxNG(file=relaxng_schema_path)

    count, failed = 0, 0

    with ThreadPoolExecutor() as executor:
        futures = {}
        for template_path in templates_to_validate:
            future = executor.submit(
                validate_template,
                simulation_template_entity,
                template_path,
                relaxng_schema,
                mod_name,
            )
            futures[future] = template_path

        for future in as_completed(futures):
            count += 1

            template_path = futures[future]

            logger.debug("Processed %s", template_path)

            try:
                future.result()
            except DocumentInvalid as e:
                failed += 1
                logger.error("%s: %s", template_path, e)  # noqa: TRY400

    logger.info("Total: %s; failed: %s", count, failed)

    if failed:
        raise ValidationError


def main() -> None:
    parser = argparse.ArgumentParser(description="Validate templates against a RELAX NG schema.")
    parser.add_argument("-m", "--mod-name", required=True, help="The name of the mod to validate.")
    parser.add_argument(
        "-r",
        "--root",
        dest="vfs_root",
        default=Path(),
        type=Path,
        help="The path to mod's root location.",
    )
    parser.add_argument(
        "-s",
        "--relaxng-schema",
        default=Path() / ENTITY_RELAXNG_FNAME,
        type=Path,
        help="The path to the RELAX NG schema.",
    )
    parser.add_argument(
        "-t",
        "--templates",
        nargs="*",
        type=Path,
        help="A list of templates to validate. If omitted all templates will be validated.",
    )
    parser.add_argument(
        "-v", "--verbose", help="Be verbose about the output.", action="store_true"
    )

    args = parser.parse_args()

    log_level = logging.DEBUG if args.verbose else logging.INFO
    logger = init_logger(log_level)

    if not args.relaxng_schema.exists():
        logger.error(
            "RELAX NG schema file doesn't exist. Please create the file: %s. You can do that by "
            'running "pyrogenesis -dumpSchema" in the "binaries/system" directory',
            args.relaxng_schema,
        )
        sys.exit(1)

    try:
        validate_templates(
            logger, args.vfs_root, args.mod_name, args.relaxng_schema, args.templates
        )
    except ValidationError:
        sys.exit(1)


if __name__ == "__main__":
    main()
