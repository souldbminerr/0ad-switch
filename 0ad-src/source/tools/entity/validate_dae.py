#!/usr/bin/env python3

import sys
from argparse import ArgumentParser
from concurrent.futures import ProcessPoolExecutor, as_completed
from logging import INFO, Formatter, StreamHandler, getLogger
from pathlib import Path

from collada import Collada, DaeBrokenRefError, DaeError, common, controller
from scriptlib import find_files


class InvalidControllerError(Exception):
    """Mesh contains controllers of other types than Skin."""


class VertexWithoutWeightError(Exception):
    """Vertex has no weight."""

    def __init__(self, num_vertices: int, num_vertices_no_weight: int) -> None:
        self.num_vertices = num_vertices
        self.num_vertices_no_weight = num_vertices_no_weight


def validate_mesh(path_dae: Path) -> None:
    """Validate a mesh."""
    dae = Collada(path_dae.as_posix(), ignore=[common.DaeUnsupportedError, DaeBrokenRefError])
    for ctr in dae.controllers:
        if type(ctr) is not controller.Skin:
            raise InvalidControllerError
        totalv = len(ctr.vcounts)
        totalv_0 = len(ctr.vcounts[ctr.vcounts == 0])
        if totalv_0 > 0:
            raise VertexWithoutWeightError(totalv, totalv_0)


class DaeValidator:
    def __init__(self, vfs_root: Path, mods: list[str]) -> None:
        self.has_weightless_vtx = []
        self.vfs_root = vfs_root
        self.mods = mods

        self.log = getLogger()
        self.log.setLevel(INFO)
        sh = StreamHandler(sys.stdout)
        sh.setLevel(INFO)
        sh.setFormatter(Formatter("%(levelname)s - %(message)s"))
        self.log.addHandler(sh)

    def run(self) -> bool:
        """Run validation for a bunch of meshes."""
        is_ok = True
        i = 0
        with ProcessPoolExecutor() as executor:
            futures = {}
            for _, dae_path in find_files(self.vfs_root, self.mods, Path("art/meshes"), ["dae"]):
                future = executor.submit(validate_mesh, dae_path)
                futures[future] = dae_path

            for future in as_completed(futures):
                i += 1

                dae_path = futures[future]

                try:
                    future.result()
                except InvalidControllerError:
                    self.log.warning("Mesh %s contains an invalid controller.", dae_path)
                    is_ok = False
                    continue
                except DaeError as exc:
                    self.log.warning("Failed to load mesh %s: %s", dae_path, exc)
                    is_ok = False
                    continue
                except VertexWithoutWeightError as exc:
                    self.log.warning(
                        "Mesh %s has %i (out of %i) vertices with no weight"
                        " and no bone assigned. Use P294 to find them in Blender.",
                        dae_path,
                        exc.num_vertices_no_weight,
                        exc.num_vertices,
                    )
                    self.has_weightless_vtx.append(dae_path)
                    is_ok = False
                    continue

        self.log.info(
            "%i out of %i files have vertices with no weight or bones.",
            len(self.has_weightless_vtx),
            i,
        )
        return is_ok


def main() -> None:
    parser = ArgumentParser(description="Validate COLLADA meshes.")
    parser.add_argument(
        "-m",
        "--mod-name",
        dest="mod_names",
        nargs="*",
        default=["public"],
        help="The name of the mod to validate.",
    )
    parser.add_argument(
        "-r",
        "--root",
        dest="vfs_root",
        default=Path(__file__).resolve().parents[3] / "binaries" / "data" / "mods",
        type=Path,
        help="The path to mod's root location.",
    )
    args = parser.parse_args()

    dv = DaeValidator(args.vfs_root, args.mod_names)
    if dv.run() > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
