from collections import Counter
from collections.abc import Generator
from decimal import Decimal
from os.path import exists
from pathlib import Path
from re import split
from xml.etree import ElementTree as ET


class SimulTemplateEntity:
    def __init__(self, vfs_root, logger):
        self.vfs_root = vfs_root
        self.logger = logger

    def get_file(self, base_path, vfs_path, mod):
        default_path = self.vfs_root / mod / base_path
        file = (default_path / "special" / "filter" / vfs_path).with_suffix(".xml")
        if not exists(file):
            file = (default_path / "mixins" / vfs_path).with_suffix(".xml")
        if not exists(file):
            file = (default_path / vfs_path).with_suffix(".xml")
        return file

    def get_main_mod(self, base_path, vfs_path, mods):
        for mod in mods:
            fp = self.get_file(base_path, vfs_path, mod)
            if fp.exists():
                main_mod = mod
                break
        else:
            # default to first mod
            # it should then not exist
            # it will raise an exception when trying to read it
            main_mod = mods[0]
        return main_mod

    def apply_layer(self, base_tag, tag):
        """Apply tag layer to base_tag."""
        if tag.get("datatype") == "tokens":
            base_tokens = split(r"\s+", base_tag.text or "")
            tokens = split(r"\s+", tag.text or "")
            final_tokens = base_tokens.copy()
            for token in tokens:
                if token.startswith("-"):
                    token_to_remove = token[1:]
                    if token_to_remove in final_tokens:
                        final_tokens.remove(token_to_remove)
                elif token not in final_tokens:
                    final_tokens.append(token)
            base_tag.text = " ".join(final_tokens)
            base_tag.set("datatype", "tokens")
        elif tag.get("op"):
            op = tag.get("op")
            op1 = Decimal(base_tag.text or "0")
            op2 = Decimal(tag.text or "0")
            # Try converting to integers if possible, to pass validation.
            if op == "add":
                base_tag.text = str(int(op1 + op2) if int(op1 + op2) == op1 + op2 else op1 + op2)
            elif op == "mul":
                base_tag.text = str(int(op1 * op2) if int(op1 * op2) == op1 * op2 else op1 * op2)
            elif op == "mul_round":
                base_tag.text = str(round(op1 * op2))
            else:
                raise ValueError(f"Invalid operator '{op}'")
        else:
            base_tag.text = tag.text
            for prop in tag.attrib:
                if prop not in ("disable", "replace", "parent", "merge"):
                    base_tag.set(prop, tag.get(prop))
        for child in tag:
            base_child = base_tag.find(child.tag)
            if "disable" in child.attrib:
                if base_child is not None:
                    base_tag.remove(base_child)
            elif ("merge" not in child.attrib) or (base_child is not None):
                if "replace" in child.attrib and base_child is not None:
                    base_tag.remove(base_child)
                    base_child = None
                if base_child is None:
                    base_child = ET.Element(child.tag)
                    base_tag.append(base_child)
                self.apply_layer(base_child, child)
                if "replace" in base_child.attrib:
                    del base_child.attrib["replace"]

    def load_inherited(self, base_path, vfs_path, mods):
        entity = self._load_inherited(base_path, vfs_path, mods)
        entity[:] = sorted(entity[:], key=lambda x: x.tag)
        return entity

    def _load_inherited(self, base_path, vfs_path, mods, base=None):
        # vfs_path should be relative to base_path in a mod
        if "|" in vfs_path:
            paths = vfs_path.split("|", 1)
            base = self._load_inherited(base_path, paths[1], mods, base)
            return self._load_inherited(base_path, paths[0], mods, base)

        main_mod = self.get_main_mod(base_path, vfs_path, mods)
        fp = self.get_file(base_path, vfs_path, main_mod)
        layer = ET.parse(fp).getroot()
        for el in layer.iter():
            children = [x.tag for x in el]
            duplicates = [x for x, c in Counter(children).items() if c > 1]
            if duplicates:
                for dup in duplicates:
                    self.logger.warning(
                        "Duplicate child node '%s' in tag %s of %s", dup, el.tag, fp
                    )
        if layer.get("parent"):
            parent = self._load_inherited(base_path, layer.get("parent"), mods, base)
            self.apply_layer(parent, layer)
            return parent
        if not base:
            return layer
        self.apply_layer(base, layer)
        return base


def find_files(
    vfs_root: Path, mods: list[str], vfs_path: Path, file_extensions: list[str]
) -> Generator[tuple[Path, Path], None, None]:
    """Find files.

    Returns a list of 2-size tuple with:
        - Path relative to the mod base
        - full Path
    """
    full_file_extensions = {f".{ext}" for ext in file_extensions}

    for mod in mods:
        for path in (vfs_root / mod / vfs_path).resolve().glob("**/*"):
            if not path.is_dir() and path.suffix in full_file_extensions:
                yield path.relative_to(vfs_root / mod), path
