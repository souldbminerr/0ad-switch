#!/usr/bin/env python3
import configparser
import re
import sys
from argparse import ArgumentParser
from collections import defaultdict
from io import BytesIO
from itertools import chain
from json import load, loads
from logging import INFO, WARNING, Filter, Formatter, StreamHandler, getLogger
from pathlib import Path
from struct import calcsize, unpack
from xml.etree import ElementTree as ET

from scriptlib import SimulTemplateEntity, find_files
from validate_grammar import RelaxNGValidator
from validator import Validator


class SingleLevelFilter(Filter):
    def __init__(self, passlevel, reject):
        self.passlevel = passlevel
        self.reject = reject

    def filter(self, record):
        if self.reject:
            return record.levelno != self.passlevel
        return record.levelno == self.passlevel


class CheckRefs:
    def __init__(self):
        self.files: list[Path] = []
        self.roots: list[Path] = []
        self.deps: list[tuple[Path, Path]] = []
        self.gui_items: dict[str, list[str]] = {
            "gui_objects": [],
            "sprites": [],
            "styles": [],
            "scrollbars": [],
            "tooltips": [],
            "colors": [],
        }
        self.deps_gui_items: dict[str, list[tuple[Path, str]]] = {
            "gui_objects": [],
            "sprites": [],
            "styles": [],
            "scrollbars": [],
            "tooltips": [],
            "colors": [],
        }
        # Three or four positive integers below 256 separated by spaces
        integer_8bit = r"2[0-4]\d|25[0-5]|[01]?\d{1,2}"
        self.rgba_color_regex = rf"\s*(({integer_8bit})\s+){{2,3}}({integer_8bit})\s*"
        self.vfs_root = Path(__file__).resolve().parents[3] / "binaries" / "data" / "mods"
        self.supportedTextureFormats = ("dds", "png")
        self.supportedMeshesFormats = ("pmd", "dae")
        self.supportedAnimationFormats = ("psa", "dae")
        self.supportedAudioFormats = "ogg"
        self.mods = []
        self.__init_logger()
        self.inError = False

    def __init_logger(self):
        logger = getLogger(__name__)
        logger.setLevel(INFO)
        # create a console handler, seems nicer to Windows and for future uses
        ch = StreamHandler(sys.stdout)
        ch.setLevel(INFO)
        ch.setFormatter(Formatter("%(levelname)s - %(message)s"))
        f1 = SingleLevelFilter(INFO, False)
        ch.addFilter(f1)
        logger.addHandler(ch)
        errorch = StreamHandler(sys.stderr)
        errorch.setLevel(WARNING)
        errorch.setFormatter(Formatter("%(levelname)s - %(message)s"))
        logger.addHandler(errorch)
        self.logger = logger

    def main(self):
        ap = ArgumentParser(
            description="Checks the game files for missing dependencies, unused files,"
            " and for file integrity."
        )
        ap.add_argument(
            "-u",
            "--check-unused",
            action="store_true",
            help="check for all the unused files in the given mods and their dependencies."
            " Implies --check-map-xml. Currently yields a lot of false positives.",
        )
        ap.add_argument(
            "-x",
            "--check-map-xml",
            action="store_true",
            help="check maps for missing actor and templates.",
        )
        ap.add_argument(
            "-a",
            "--validate-actors",
            action="store_true",
            help="run the validator.py script to check if the actors files have extra or missing "
            "textures. This currently only works for the public mod.",
        )
        ap.add_argument(
            "-t",
            "--validate-templates",
            action="store_true",
            help="run the validator.py script to check if the xml files match their (.rng) "
            "grammar file.",
        )
        ap.add_argument(
            "-m",
            "--mods",
            metavar="MOD",
            dest="mods",
            nargs="+",
            default=["public"],
            help="specify which mods to check. Default to public.",
        )
        ap.add_argument(
            "-d",
            "--validate-meshes",
            action="store_true",
            help="check if meshes have vertices with no weight.",
        )
        args = ap.parse_args()
        # force check_map_xml if check_unused is used to avoid false positives.
        args.check_map_xml |= args.check_unused
        # ordered uniq mods (dict maintains ordered keys from python 3.6)
        self.mods = list(
            dict.fromkeys([*args.mods, *self.get_mod_dependencies(*args.mods), "mod"]).keys()
        )
        self.logger.info("Checking %s's integrity.", "|".join(args.mods))
        self.logger.info("The following mods will be loaded: %s.", "|".join(self.mods))
        if args.check_map_xml:
            self.add_maps_xml()
        self.add_maps_pmp()
        self.add_entities()
        self.add_actors()
        self.add_variants()
        self.add_art()
        self.add_materials()
        self.add_particles()
        self.add_soundgroups()
        self.add_audio()
        self.add_gui_xml()
        self.add_gui_data()
        self.add_civs()
        self.add_rms()
        self.add_techs()
        self.add_terrains()
        self.add_auras()
        self.add_tips()
        self.check_deps()
        self.check_deps_gui_items()
        self.check_fonts()
        if args.check_unused:
            self.check_unused()
        if args.validate_templates:
            validate = RelaxNGValidator(self.vfs_root, self.mods)
            if not validate.run():
                self.inError = True
        if args.validate_actors:
            validator = Validator(self.vfs_root, self.mods)
            if not validator.run():
                self.inError = True

        if args.validate_meshes:
            from validate_dae import DaeValidator  # noqa: PLC0415

            dv = DaeValidator(self.vfs_root, self.mods)
            self.inError = self.inError or not dv.run()

        return not self.inError

    def get_mod_dependencies(self, *mods):
        modjsondeps = []
        for mod in mods:
            mod_json_path = self.vfs_root / mod / "mod.json"
            if not mod_json_path.exists():
                self.logger.warning('Failed to find the mod.json for "%s"', mod)
                continue

            with open(mod_json_path, encoding="utf-8") as f:
                modjson = load(f)
            # 0ad's folder isn't named like the mod.
            modjsondeps.extend(
                ["public" if "0ad" in dep else dep for dep in modjson.get("dependencies", [])]
            )
        return modjsondeps

    def vfs_to_relative_to_mods(self, vfs_path):
        for dep in self.mods:
            fn = Path(dep) / vfs_path
            if (self.vfs_root / fn).exists():
                return fn
        return None

    def vfs_to_physical(self, vfs_path):
        fn = self.vfs_to_relative_to_mods(vfs_path)
        return self.vfs_root / fn

    def find_files(self, vfs_path, *ext_list):
        return find_files(self.vfs_root, self.mods, Path(vfs_path), ext_list)

    def add_maps_xml(self):
        self.logger.info("Loading maps XML...")
        mapfiles = chain(
            self.find_files("maps/scenarios", "xml"),
            self.find_files("maps/skirmishes", "xml"),
            self.find_files("maps/tutorials", "xml"),
        )
        actor_prefix = "actor|"
        resource_prefix = "resource|"
        for fp, ffp in sorted(mapfiles):
            self.files.append(fp)
            self.roots.append(fp)
            et_map = ET.parse(ffp).getroot()
            entities = et_map.find("Entities")
            used = (
                {entity.find("Template").text.strip() for entity in entities.findall("Entity")}
                if entities is not None
                else {}
            )
            for template in used:
                if template.startswith(actor_prefix):
                    self.deps.append((fp, Path(f"art/actors/{template[len(actor_prefix) :]}")))
                elif template.startswith(resource_prefix):
                    self.deps.append(
                        (fp, Path(f"simulation/templates/{template[len(resource_prefix) :]}.xml"))
                    )
                else:
                    self.deps.append((fp, Path(f"simulation/templates/{template}.xml")))
            # Map previews
            settings = loads(et_map.find("ScriptSettings").text)
            if settings.get("Preview"):
                self.deps.append(
                    (fp, Path(f"art/textures/ui/session/icons/mappreview/{settings['Preview']}"))
                )

    def add_maps_pmp(self):
        self.logger.info("Loading maps PMP...")
        # Need to generate terrain texture filename=>relative path lookup first
        terrains = {}
        for fp, ffp in self.find_files("art/terrains", "xml"):
            name = fp.stem
            # ignore terrains.xml
            if name != "terrains":
                if name in terrains:
                    self.inError = True
                    self.logger.error(
                        "Duplicate terrain name '%s' (from '%s' and '%s')",
                        name,
                        terrains[name],
                        ffp,
                    )
                terrains[name] = str(fp)
        mapfiles = chain(
            self.find_files("maps/scenarios", "pmp"),
            self.find_files("maps/skirmishes", "pmp"),
        )
        for fp, ffp in sorted(mapfiles):
            self.files.append(fp)
            self.roots.append(fp)
            with open(ffp, "rb") as f:
                expected_header = b"PSMP"
                header = f.read(len(expected_header))
                if header != expected_header:
                    raise ValueError(f"Invalid PMP header {header} in '{ffp}'")
                int_fmt = "<L"  # little endian long int
                int_len = calcsize(int_fmt)
                (version,) = unpack(int_fmt, f.read(int_len))
                if version != 7:
                    raise ValueError(f"Invalid PMP version ({version}) in '{ffp}'")
                (datasize,) = unpack(int_fmt, f.read(int_len))
                (mapsize,) = unpack(int_fmt, f.read(int_len))
                f.seek(2 * (mapsize * 16 + 1) * (mapsize * 16 + 1), 1)  # skip heightmap
                (numtexs,) = unpack(int_fmt, f.read(int_len))
                for _i in range(numtexs):
                    (length,) = unpack(int_fmt, f.read(int_len))
                    terrain_name = f.read(length).decode("ascii")  # suppose ascii encoding
                    self.deps.append(
                        (
                            fp,
                            Path(
                                terrains.get(
                                    terrain_name, f"art/terrains/(unknown)/{terrain_name}"
                                )
                            ),
                        )
                    )

    def get_existing_civ_codes(self):
        existing_civs = set()
        for _, ffp in self.find_files("simulation/data/civs", "json"):
            with open(ffp, encoding="utf-8") as f:
                civ = load(f)
                code = civ.get("Code")
                if code is not None:
                    existing_civs.add(code)

        return existing_civs

    def get_custom_phase_techs(self):
        existing_civs = self.get_existing_civ_codes()
        custom_phase_techs = []
        for fp, _ in self.find_files("simulation/data/technologies", "json"):
            path_str = str(fp)
            if "phase" not in path_str:
                continue

            # Get the last part of the phase tech name.
            if Path(path_str).stem.split("_")[-1] in existing_civs:
                custom_phase_techs.append(
                    fp.relative_to("simulation/data/technologies").as_posix()
                )

        return custom_phase_techs

    def add_entities(self):
        self.logger.info("Loading entities...")
        simul_templates_path = Path("simulation/templates")
        # TODO: We might want to get computed templates through the RL interface instead of
        #       computing the values ourselves.
        simul_template_entity = SimulTemplateEntity(self.vfs_root, self.logger)
        custom_phase_techs = self.get_custom_phase_techs()
        for fp, _ in self.find_files(simul_templates_path, "xml"):
            self.files.append(fp)
            entity = simul_template_entity.load_inherited(
                simul_templates_path, str(fp.relative_to(simul_templates_path)), self.mods
            )
            if entity.get("parent"):
                for parent in entity.get("parent").split("|"):
                    self.deps.append((fp, simul_templates_path / (parent + ".xml")))
            if not str(fp).startswith("template_"):
                self.roots.append(fp)
                if (
                    entity.find("VisualActor") is not None
                    and entity.find("VisualActor").find("Actor") is not None
                    and entity.find("Identity") is not None
                ):
                    cmp_identity = entity.find("Identity")

                    actor = entity.find("VisualActor").find("Actor")
                    if cmp_identity is not None:
                        actor_path = actor.text
                        if "{civ}" in actor_path:
                            civ_tag = cmp_identity.find("Civ")
                            civ = civ_tag.text if civ_tag is not None else "gaia"
                            actor_path = actor_path.replace("{civ}", civ)

                        if "{phenotype}" in actor_path:
                            phenotype_tag = cmp_identity.find("Phenotype")
                            phenotypes = (
                                phenotype_tag.text.split()
                                if (phenotype_tag is not None and phenotype_tag.text)
                                else ["default"]
                            )
                            for phenotype in phenotypes:
                                # See simulation2/components/CCmpVisualActor.cpp and Identity.js
                                # for explanation.
                                phenotype_path = actor_path.replace("{phenotype}", phenotype)
                                self.deps.append((fp, Path(f"art/actors/{phenotype_path}")))
                    else:
                        actor_path = actor.text
                        self.deps.append((fp, Path(f"art/actors/{actor_path}")))

                    foundation_actor = entity.find("VisualActor").find("FoundationActor")
                    if foundation_actor is not None:
                        self.deps.append((fp, Path(f"art/actors/{foundation_actor.text}")))

                if entity.find("Sound") is not None:
                    phenotype_tag = entity.find("Identity").find("Phenotype")
                    phenotypes = (
                        phenotype_tag.text.split()
                        if (phenotype_tag is not None and phenotype_tag.text)
                        else ["default"]
                    )
                    lang_tag = entity.find("Identity").find("Lang")
                    lang = lang_tag.text if lang_tag is not None and lang_tag.text else "greek"
                    sound_groups = entity.find("Sound").find("SoundGroups")
                    if sound_groups is not None and len(sound_groups) > 0:
                        for sound_group in sound_groups:
                            if sound_group.text and sound_group.text.strip():
                                if "{phenotype}" in sound_group.text:
                                    for phenotype in phenotypes:
                                        # see simulation/components/Sound.js and Identity.js
                                        # for explanation
                                        sound_path = sound_group.text.replace(
                                            "{phenotype}", phenotype
                                        ).replace("{lang}", lang)
                                        self.deps.append((fp, Path(f"audio/{sound_path}")))
                                else:
                                    sound_path = sound_group.text.replace("{lang}", lang)
                                    self.deps.append((fp, Path(f"audio/{sound_path}")))
                if entity.find("Identity") is not None:
                    icon = entity.find("Identity").find("Icon")
                    if icon is not None and icon.text:
                        if entity.find("Formation") is not None:
                            self.deps.append(
                                (fp, Path(f"art/textures/ui/session/icons/{icon.text}"))
                            )
                        else:
                            self.deps.append(
                                (fp, Path(f"art/textures/ui/session/portraits/{icon.text}"))
                            )
                if (
                    entity.find("Heal") is not None
                    and entity.find("Heal").find("RangeOverlay") is not None
                ):
                    range_overlay = entity.find("Heal").find("RangeOverlay")
                    for tag in ("LineTexture", "LineTextureMask"):
                        elem = range_overlay.find(tag)
                        if elem is not None and elem.text:
                            self.deps.append((fp, Path(f"art/textures/selection/{elem.text}")))
                if (
                    entity.find("Selectable") is not None
                    and entity.find("Selectable").find("Overlay") is not None
                    and entity.find("Selectable").find("Overlay").find("Texture") is not None
                ):
                    texture = entity.find("Selectable").find("Overlay").find("Texture")
                    for tag in ("MainTexture", "MainTextureMask"):
                        elem = texture.find(tag)
                        if elem is not None and elem.text:
                            self.deps.append((fp, Path(f"art/textures/selection/{elem.text}")))
                if entity.find("Formation") is not None:
                    icon = entity.find("Formation").find("Icon")
                    if icon is not None and icon.text:
                        self.deps.append((fp, Path(f"art/textures/ui/session/icons/{icon.text}")))

                cmp_auras = entity.find("Auras")
                if cmp_auras is not None:
                    aura_string = cmp_auras.text
                    for aura in aura_string.split():
                        if not aura:
                            continue
                        if aura.startswith("-"):
                            continue
                        self.deps.append((fp, Path(f"simulation/data/auras/{aura}.json")))

                cmp_identity = entity.find("Identity")
                if cmp_identity is not None:
                    req_tag = cmp_identity.find("Requirements")
                    if req_tag is not None:

                        def parse_requirements(fp, req, recursion_depth=1):
                            techs_tag = req.find("Techs")
                            if techs_tag is not None:
                                for tech_tag in techs_tag.text.split():
                                    self.deps.append(
                                        (fp, Path(f"simulation/data/technologies/{tech_tag}.json"))
                                    )

                            if recursion_depth > 0:
                                recursion_depth -= 1
                                all_req_tag = req.find("All")
                                if all_req_tag is not None:
                                    parse_requirements(fp, all_req_tag, recursion_depth)
                                any_req_tag = req.find("Any")
                                if any_req_tag is not None:
                                    parse_requirements(fp, any_req_tag, recursion_depth)

                        parse_requirements(fp, req_tag)

                cmp_researcher = entity.find("Researcher")
                if cmp_researcher is not None:
                    tech_string = cmp_researcher.find("Technologies")
                    if tech_string is not None:
                        for tech in tech_string.text.split():
                            if not tech:
                                continue
                            if tech.startswith("-"):
                                continue
                            if (
                                "{civ}" in tech
                                and cmp_identity is not None
                                and cmp_identity.find("Civ") is not None
                            ):
                                civ = cmp_identity.find("Civ").text
                                # Fallback for non specific phase techs.
                                if tech.startswith("phase") and not bool(
                                    [
                                        phase_tech
                                        for phase_tech in custom_phase_techs
                                        if (tech.replace("{civ}", civ) + ".json") == phase_tech
                                    ]
                                ):
                                    civ = "generic"
                                tech = tech.replace("{civ}", civ)
                            self.deps.append(
                                (fp, Path(f"simulation/data/technologies/{tech}.json"))
                            )

    def append_variant_dependencies(self, variant, fp):
        variant_file = variant.get("file")
        mesh = variant.find("mesh")
        particles = variant.find("particles")
        texture_files = (
            [tex.get("file") for tex in variant.find("textures").findall("texture")]
            if variant.find("textures") is not None
            else []
        )
        prop_actors = (
            [prop.get("actor") for prop in variant.find("props").findall("prop")]
            if variant.find("props") is not None
            else []
        )
        animation_files = (
            [anim.get("file") for anim in variant.find("animations").findall("animation")]
            if variant.find("animations") is not None
            else []
        )
        if variant_file:
            self.deps.append((fp, Path(f"art/variants/{variant_file}")))
        if mesh is not None and mesh.text:
            self.deps.append((fp, Path(f"art/meshes/{mesh.text}")))
        if particles is not None and particles.get("file"):
            self.deps.append((fp, Path(f"art/particles/{particles.get('file')}")))
        for texture_file in [x for x in texture_files if x]:
            self.deps.append((fp, Path(f"art/textures/skins/{texture_file}")))
        for prop_actor in [x for x in prop_actors if x]:
            self.deps.append((fp, Path(f"art/actors/{prop_actor}")))
        for animation_file in [x for x in animation_files if x]:
            self.deps.append((fp, Path(f"art/animation/{animation_file}")))

    def append_actor_dependencies(self, actor, fp):
        for group in actor.findall("group"):
            for variant in group.findall("variant"):
                self.append_variant_dependencies(variant, fp)
        material = actor.find("material")
        if material is not None and material.text:
            self.deps.append((fp, Path(f"art/materials/{material.text}")))

    def add_actors(self):
        self.logger.info("Loading actors...")
        for fp, ffp in self.find_files("art/actors", "xml"):
            self.files.append(fp)
            self.roots.append(fp)
            root = ET.parse(ffp).getroot()
            if root.tag == "actor":
                self.append_actor_dependencies(root, fp)

            # model has lods
            elif root.tag == "qualitylevels":
                qualitylevels = root
                for actor in qualitylevels.findall("actor"):
                    self.append_actor_dependencies(actor, fp)
                for actor in qualitylevels.findall("inline"):
                    self.append_actor_dependencies(actor, fp)

    def add_variants(self):
        self.logger.info("Loading variants...")
        for fp, ffp in self.find_files("art/variants", "xml"):
            self.files.append(fp)
            self.roots.append(fp)
            variant = ET.parse(ffp).getroot()
            self.append_variant_dependencies(variant, fp)

    def add_art(self):
        self.logger.info("Loading art files...")
        self.files.extend(
            [
                fp
                for (fp, ffp) in self.find_files(
                    "art/textures/particles", *self.supportedTextureFormats
                )
            ]
        )
        self.files.extend(
            [
                fp
                for (fp, ffp) in self.find_files(
                    "art/textures/terrain", *self.supportedTextureFormats
                )
            ]
        )
        self.files.extend(
            [
                fp
                for (fp, ffp) in self.find_files(
                    "art/textures/skins", *self.supportedTextureFormats
                )
            ]
        )
        self.files.extend(
            [fp for (fp, ffp) in self.find_files("art/meshes", *self.supportedMeshesFormats)]
        )
        self.files.extend(
            [fp for (fp, ffp) in self.find_files("art/animation", *self.supportedAnimationFormats)]
        )

    def add_materials(self):
        self.logger.info("Loading materials...")
        for fp, ffp in self.find_files("art/materials", "xml"):
            self.files.append(fp)
            material_elem = ET.parse(ffp).getroot()
            for alternative in material_elem.findall("alternative"):
                material = alternative.get("material")
                if material is not None:
                    self.deps.append((fp, Path(f"art/materials/{material}")))

    def add_particles(self):
        self.logger.info("Loading particles...")
        for fp, ffp in self.find_files("art/particles", "xml"):
            self.files.append(fp)
            self.roots.append(fp)
            particle = ET.parse(ffp).getroot()
            texture = particle.find("texture")
            if texture is not None:
                self.deps.append((fp, Path(texture.text)))

    def add_soundgroups(self):
        self.logger.info("Loading sound groups...")
        for fp, ffp in self.find_files("audio", "xml"):
            self.files.append(fp)
            self.roots.append(fp)
            sound_group = ET.parse(ffp).getroot()
            path = sound_group.find("Path").text.rstrip("/")
            for sound in sound_group.findall("Sound"):
                self.deps.append((fp, Path(f"{path}/{sound.text}")))

    def add_audio(self):
        self.logger.info("Loading audio files...")
        self.files.extend(
            [fp for (fp, ffp) in self.find_files("audio/", self.supportedAudioFormats)]
        )

    def add_gui_object(self, parent, fp):
        if parent is None:
            return

        for obj in parent.findall("object"):
            self.add_gui_object(obj, fp)
        for obj in parent.findall("repeat"):
            self.add_gui_object(obj, fp)
        self.add_gui_object_attribs(parent, fp)

    def add_gui_object_attribs(self, parent, fp):
        for attr, val in parent.attrib.items():
            if attr == "name":
                self.gui_items["gui_objects"].append(val)
            elif attr == "textcolor":
                self.add_gui_color_attrib(val, fp)
            elif "sound" in attr:
                self.deps.append((fp, Path(f"{val}")))
            elif "sprite" in attr:
                self.add_gui_sprite_attrib(val, fp)
            elif attr == "mouse_event_mask":
                if val.startswith("texture:"):
                    texture_file = val.replace("texture:", "")
                    self.deps.append((fp, Path(f"art/textures/ui/{texture_file}")))
                else:
                    self.logger.warning(
                        "Invalid mouse_event_mask in '%s': missing the \"texture:\" modifier",
                        str(self.vfs_to_relative_to_mods(fp)),
                    )
            elif attr == "style":
                self.deps_gui_items["styles"].append((fp, val))
            elif attr == "tooltip_style":
                self.deps_gui_items["tooltips"].append((fp, val))
            elif attr == "scrollbar_style":
                self.deps_gui_items["scrollbars"].append((fp, val))

    def add_gui_sprite_attrib(self, val, fp):
        if "stretched:" in val or "cropped:" in val:
            # Other modifiers like "grayscale:" are caught by this as well.
            texture_file = val.split(":")[-1]
            self.deps.append((fp, Path(f"art/textures/ui/{texture_file}")))
        elif "color:" in val:
            self.add_gui_color_attrib(val, fp)
        else:
            self.deps_gui_items["sprites"].append((fp, val))

    def add_gui_color_attrib(self, val, fp):
        color = val.replace("color:", "")
        # Colors can be given names (in setup.xml files)
        # which can then be used directly in place of rgb/rgba values.
        if re.fullmatch(self.rgba_color_regex, color) is None:
            self.deps_gui_items["colors"].append((fp, color))

    def add_gui_xml(self):
        self.logger.info("Loading GUI XML...")
        gui_page_regex = re.compile(r".*[\\\/]page(_[^.\/\\]+)?\.xml$")
        for fp, ffp in self.find_files("gui", "xml"):
            self.files.append(fp)
            # GUI page definitions are assumed to be named page_[something].xml and alone in that.
            if gui_page_regex.match(str(fp)):
                self.roots.append(fp)
                root_xml = ET.parse(ffp).getroot()
                for include in root_xml.findall("include"):
                    # If including an entire directory, find all the *.xml files
                    if include.text.endswith("/"):
                        self.deps.extend(
                            [
                                (fp, sub_fp)
                                for (sub_fp, sub_ffp) in self.find_files(
                                    f"gui/{include.text}", "xml"
                                )
                            ]
                        )
                    else:
                        self.deps.append((fp, Path(f"gui/{include.text}")))
            else:
                xml = ET.parse(ffp)
                root_xml = xml.getroot()
                name = root_xml.tag
                self.roots.append(fp)
                if name in ("objects", "object"):
                    for script in root_xml.findall("script"):
                        if script.get("file"):
                            self.deps.append((fp, Path(script.get("file"))))
                        if script.get("directory"):
                            # If including an entire directory, find all the *.js files
                            self.deps.extend(
                                [
                                    (fp, sub_fp)
                                    for (sub_fp, sub_ffp) in self.find_files(
                                        script.get("directory"), "js"
                                    )
                                ]
                            )
                    self.add_gui_object(root_xml, fp)
                elif name == "setup":
                    for obj in root_xml:
                        if obj.tag == "scrollbar":
                            if "name" in obj.attrib:
                                self.gui_items["scrollbars"].append(obj.get("name"))
                            else:
                                self.logger.warning(
                                    "Found scrollbar without name in %s",
                                    self.vfs_to_relative_to_mods(fp),
                                )
                            for attr, val in obj.attrib.items():
                                if "sprite" in attr:
                                    self.add_gui_sprite_attrib(val, fp)
                        elif obj.tag == "tooltip":
                            if "name" in obj.attrib:
                                self.gui_items["tooltips"].append(obj.get("name"))
                            else:
                                self.logger.warning(
                                    "Found tooltip without name in %s",
                                    self.vfs_to_relative_to_mods(fp),
                                )
                            for attr, val in obj.attrib.items():
                                if attr == "name":
                                    self.gui_items["tooltips"].append(val)
                                elif attr == "use_object":
                                    self.deps_gui_items["gui_objects"].append((fp, val))
                                elif attr == "sprite":
                                    self.add_gui_sprite_attrib(val, fp)
                                elif attr == "textcolor":
                                    self.add_gui_color_attrib(val, fp)
                        elif obj.tag == "color":
                            if "name" in obj.attrib:
                                self.gui_items["colors"].append(obj.get("name"))
                            else:
                                self.logger.warning(
                                    "Found scrollbar without name in %s",
                                    self.vfs_to_relative_to_mods(fp),
                                )

                elif name == "styles":
                    for style in root_xml.findall("style"):
                        if "name" in style.attrib:
                            self.gui_items["styles"].append(style.get("name"))
                        else:
                            self.logger.warning(
                                "Found scrollbar without name in %s",
                                self.vfs_to_relative_to_mods(fp),
                            )
                        self.gui_items["styles"].append(style.get("name"))
                        self.add_gui_object_attribs(style, fp)

                elif name == "sprites":
                    for sprite in root_xml.findall("sprite"):
                        if "name" in sprite.attrib:
                            self.gui_items["sprites"].append(sprite.get("name"))
                        else:
                            self.logger.warning(
                                "Found scrollbar without name in %s",
                                self.vfs_to_relative_to_mods(fp),
                            )
                        for el in sprite.attrib.items():
                            if el == "image":
                                if el.get("texture"):
                                    self.deps.append(
                                        (fp, Path(f"art/textures/ui/{el.get('texture')}"))
                                    )
                                elif el.get("backcolor"):
                                    self.add_gui_color_attrib(el.get("backcolor"), fp)
                            elif el == "effect" and el.get("add_color"):
                                self.add_gui_color_attrib(el.get("color"), fp)
                else:
                    bio = BytesIO()
                    xml.write(bio)
                    bio.seek(0)
                    raise ValueError(
                        f"Unexpected GUI XML root element '{name}':\n{bio.read().decode('ascii')}"
                    )

    def add_gui_data(self):
        self.logger.info("Loading GUI data...")
        self.files.extend([fp for (fp, ffp) in self.find_files("gui", "js")])
        self.files.extend([fp for (fp, ffp) in self.find_files("gamesettings", "js")])
        self.files.extend([fp for (fp, ffp) in self.find_files("autostart", "js")])
        self.roots.extend([fp for (fp, ffp) in self.find_files("autostart", "js")])
        self.files.extend(
            [fp for (fp, ffp) in self.find_files("art/textures/ui", *self.supportedTextureFormats)]
        )
        self.files.extend(
            [
                fp
                for (fp, ffp) in self.find_files(
                    "art/textures/selection", *self.supportedTextureFormats
                )
            ]
        )

    def add_civs(self):
        self.logger.info("Loading civs...")
        for fp, ffp in self.find_files("simulation/data/civs", "json"):
            self.files.append(fp)
            self.roots.append(fp)
            with open(ffp, encoding="utf-8") as f:
                civ = load(f)
            for music in civ.get("Music", []):
                self.deps.append((fp, Path(f"audio/music/{music['File']}")))

    def add_tips(self):
        self.logger.info("Loading tips...")
        self.files.extend([fp for (fp, _ffp) in self.find_files("gui/reference/tips/", ("txt"))])
        ffp = None
        fp = Path("gui/reference/tips/tipfiles.json")
        for mod in self.mods:
            json_path = self.vfs_root / mod / fp
            if json_path.exists():
                ffp = json_path
        if ffp is None:
            self.logger.error("Failed to load tips. File tipfiles.json missing.")
        else:
            self.files.append(fp)
            self.roots.append(fp)
            with open(ffp, encoding="utf-8") as f:
                categories = load(f)
                for category in categories:
                    for tips_category in category["files"]:
                        self.deps.append(
                            (fp, Path(f"gui/reference/tips/texts/{tips_category['textFile']}"))
                        )
                        for image in tips_category.get("imageFiles", []):
                            self.deps.append((fp, Path(f"art/textures/ui/tips/{image}")))

    def add_rms(self):
        self.logger.info("Loading random maps...")
        self.files.extend([fp for (fp, ffp) in self.find_files("maps/random", "js")])
        for fp, ffp in sorted(self.find_files("maps/random", "json")):
            if str(fp).startswith("maps/random/rmbiome"):
                continue
            self.files.append(fp)
            self.roots.append(fp)
            with open(ffp, encoding="utf-8") as f:
                randmap = load(f)
            settings = randmap.get("settings", {})
            if settings.get("Script"):
                self.deps.append((fp, Path(f"maps/random/{settings['Script']}")))
            # Map previews
            if settings.get("Preview"):
                self.deps.append(
                    (fp, Path(f"art/textures/ui/session/icons/mappreview/{settings['Preview']}"))
                )

    def add_techs(self):
        self.logger.info("Loading techs...")
        for fp, ffp in self.find_files("simulation/data/technologies", "json"):
            self.files.append(fp)
            with open(ffp, encoding="utf-8") as f:
                tech = load(f)
            if tech.get("autoResearch"):
                self.roots.append(fp)
            if tech.get("icon"):
                self.deps.append(
                    (fp, Path(f"art/textures/ui/session/portraits/technologies/{tech['icon']}"))
                )
            if tech.get("supersedes"):
                self.deps.append(
                    (fp, Path(f"simulation/data/technologies/{tech['supersedes']}.json"))
                )
            if tech.get("top"):
                self.deps.append((fp, Path(f"simulation/data/technologies/{tech['top']}.json")))
            if tech.get("bottom"):
                self.deps.append((fp, Path(f"simulation/data/technologies/{tech['bottom']}.json")))

    def add_terrains(self):
        self.logger.info("Loading terrains...")
        for fp, ffp in self.find_files("art/terrains", "xml"):
            # ignore terrains.xml
            if str(fp).endswith("terrains.xml"):
                continue
            self.files.append(fp)
            self.roots.append(fp)
            terrain = ET.parse(ffp).getroot()
            for texture in terrain.find("textures").findall("texture"):
                if texture.get("file"):
                    self.deps.append((fp, Path(f"art/textures/terrain/{texture.get('file')}")))
            if terrain.find("material") is not None:
                material = terrain.find("material").text
                self.deps.append((fp, Path(f"art/materials/{material}")))

    def add_auras(self):
        self.logger.info("Loading auras...")
        for fp, ffp in self.find_files("simulation/data/auras", "json"):
            self.files.append(fp)
            with open(ffp, encoding="utf-8") as f:
                aura = load(f)
            if aura.get("overlayIcon"):
                self.deps.append((fp, Path(aura["overlayIcon"])))
            range_overlay = aura.get("rangeOverlay", {})
            for prop in ("lineTexture", "lineTextureMask"):
                if range_overlay.get(prop):
                    self.deps.append((fp, Path(f"art/textures/selection/{range_overlay[prop]}")))

    def check_deps(self):
        self.logger.info("Looking for missing files...")
        uniq_files = {r.as_posix() for r in self.files}
        lower_case_files = {f.lower(): f for f in uniq_files}

        missing_files: dict[str, set[str]] = defaultdict(set)

        for parent, dep in self.deps:
            dep_str = dep.as_posix()
            if "simulation/templates" in dep_str and (
                dep_str.replace("templates/", "template/special/filter/") in uniq_files
                or dep_str.replace("templates/", "template/mixins/") in uniq_files
            ):
                continue

            if dep_str in uniq_files:
                continue

            missing_files[dep_str].add(parent.as_posix())

        for dep, parents in sorted(missing_files.items()):
            callers = [str(self.vfs_to_relative_to_mods(ref)) for ref in parents]
            self.logger.error(
                "Missing file '%s' referenced by: %s", dep, ", ".join(sorted(callers))
            )

            if dep.lower() in lower_case_files:
                self.logger.warning(
                    "### Case-insensitive match (found '%s')", lower_case_files[dep.lower()]
                )

            self.inError = True

    def check_deps_gui_items(self):
        self.logger.info("Looking for missing GUI items...")
        lower_case_items = {
            key: [item.lower() for item in value] for key, value in self.gui_items.items()
        }
        missing_items: dict[str, dict[str, set[str]]] = defaultdict(lambda: defaultdict(set))

        for category, items in self.deps_gui_items.items():
            for ref, item in items:
                if item != "" and item not in self.gui_items[category]:
                    missing_items[category][item].add(ref)

        for category, items in missing_items.items():
            # Simply remove the "s" at the end: ugly, but works.
            # (It's only for prettier error messages anyway...)
            category_name_singular = category[:-1]
            for item, refs in items.items():
                refs_relative = [str(self.vfs_to_relative_to_mods(ref)) for ref in refs]
                self.logger.error(
                    "Missing %s '%s' referenced by: %s",
                    category_name_singular,
                    item,
                    ", ".join(sorted(refs_relative)),
                )

                if item.lower() in lower_case_items[category]:
                    self.logger.warning(
                        "### Case-insensitive match (found '%s')", lower_case_items[item.lower()]
                    )

            self.inError = True

    def check_fonts(self):
        """Check that fonts referenced in default.cfg exist."""
        self.logger.info("Looking for missing font files...")

        base_path = Path(__file__).parents[3]
        default_cfg_path = base_path / "binaries/data/config/default.cfg"
        font_dir = self.vfs_root / "mod/fonts"

        try:
            config = configparser.ConfigParser(allow_unnamed_section=True)
            config.read(default_cfg_path)
        except TypeError:
            # fallback for Python <3.13
            configparser.UNNAMED_SECTION = "<UNNAMED_SECTION>"
            config = configparser.ConfigParser()
            content = "[" + configparser.UNNAMED_SECTION + "]\n"
            with default_cfg_path.open() as f:
                content += f.read()
            config.read_string(content)

        referenced_fonts = set()
        for section in config.sections():
            if section == configparser.UNNAMED_SECTION or not section.startswith("fonts."):
                continue

            for key, value in config[section].items():
                if key.split(".")[-1] in ["regular", "bold", "italic"]:
                    for font_name in value.split(","):
                        referenced_fonts.add(font_name.strip().strip('"'))

        for font in referenced_fonts:
            if not (font_dir / font).is_file():
                self.logger.error(
                    "Missing font '%s' referenced by: %s",
                    font,
                    default_cfg_path.relative_to(base_path),
                )
                self.inError = True

    def check_unused(self):
        self.logger.info("Looking for unused files...")
        deps = defaultdict(set)
        for parent, dep in self.deps:
            deps[parent.as_posix()].add(dep.as_posix())

        uniq_files = {r.as_posix() for r in self.files}
        reachable = [r.as_posix() for r in set(self.roots)]
        while True:
            new_reachable = []
            for r in reachable:
                new_reachable.extend([x for x in deps.get(r, {}) if x not in reachable])
            if new_reachable:
                reachable.extend(new_reachable)
            else:
                break

        for f in sorted(uniq_files):
            if f in reachable or f.startswith(("art/terrains/", "maps/random/")):
                continue
            self.logger.warning("Unused file '%s'", str(self.vfs_to_relative_to_mods(f)))


if __name__ == "__main__":
    check_ref = CheckRefs()
    if not check_ref.main():
        sys.exit(1)
