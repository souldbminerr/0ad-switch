#!/usr/bin/env python3
#
# Copyright (C) 2025 Wildfire Games.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

# ruff: noqa: FURB122,SIM115

import sys
import xml.etree.ElementTree as ET
from functools import lru_cache
from pathlib import Path


sys.path.append(str(Path(__file__).parent.parent / "entity"))
from scriptlib import SimulTemplateEntity


ATTACK_TYPES = ["Hack", "Pierce", "Crush", "Poison", "Fire"]
RESOURCES = ["food", "wood", "stone", "metal"]

# Generic templates to load
# The way this works is it tries all generic templates
# But only loads those who have one of the following parents
# EG adding "template_unit.xml" will load all units.
LOAD_TEMPLATES_IF_PARENT = [
    "template_unit_infantry.xml",
    "template_unit_cavalry.xml",
    "template_unit_champion.xml",
    "template_unit_hero.xml",
]

# Those describe Civs to analyze.
# The script will load all entities that derive (to the nth degree) from one of
# the above templates.
CIVS = [
    "athen",
    "brit",
    "cart",
    "gaul",
    "iber",
    "kush",
    "han",
    "mace",
    "maur",
    "pers",
    "ptol",
    "rome",
    "sele",
    "spart",
    # "gaia",
]

# Remote Civ templates with those strings in their name.
FILTER_OUT = ["marian", "thureophoros", "thorakites", "kardakes"]

# In the Civilization specific units table, do you want to only show the units
# that are different from the generic templates?
SHOW_CHANGED_ONLY = True

# Sorting parameters for the "roster variety" table
COMPARATIVE_SORT_BY_CAV = True
COMPARATIVE_SORT_BY_CHAMP = True
CLASSES_USED_FOR_SORT = [
    "Support",
    "Pike",
    "Spear",
    "Sword",
    "Archer",
    "Javelin",
    "Sling",
    "Elephant",
]

# Disable if you want the more compact basic data. Enable to allow filtering and
# sorting in-place.
ADD_SORTING_OVERLAY = True

# This is the path to the /templates/ folder to consider. Change this for mod
# support.
mods_folder = Path(__file__).resolve().parents[3] / "binaries" / "data" / "mods"
base_path = mods_folder / "public" / "simulation" / "templates"

sim_entity = SimulTemplateEntity(mods_folder, None)


def htbout(file, balise, value):
    file.write("<" + balise + ">" + value + "</" + balise + ">\n")


def htout(file, value):
    file.write("<p>" + value + "</p>\n")


@lru_cache
def get_parent_names(template_file_name: str) -> list[str]:
    """Get the names of the parent templates."""
    parent_string = ET.parse(base_path / template_file_name).getroot().get("parent")
    if not parent_string:
        return []
    return parent_string.split("|")


@lru_cache
def get_parents(template_file_name: str) -> set[Path]:
    """Get the file paths of the parent templates."""
    parents = set()
    for parent in get_parent_names(str(base_path / template_file_name)):
        parents.add(parent)
        for element in get_parents(
            sim_entity.get_file("simulation/templates/", parent + ".xml", "public")
        ):
            parents.add(element)

    return parents


def extract_value(value):
    return float(value.text) if value is not None else 0.0


def has_any_parent_template(template_name: str, parent_names: list[str]) -> bool:
    """Check whether the template has at least one of the given parents."""
    template_parents = [
        str(template_parent) + ".xml" for template_parent in get_parents(template_name)
    ]
    return any(parent_name in template_parents for parent_name in parent_names)


def calc_unit(unit_name, existing_unit=None):
    if existing_unit is not None:
        unit = existing_unit
    else:
        unit = {
            "HP": 0,
            "BuildTime": 0,
            "Cost": {
                "food": 0,
                "wood": 0,
                "stone": 0,
                "metal": 0,
                "population": 0,
            },
            "Attack": {
                "Melee": {"Hack": 0, "Pierce": 0, "Crush": 0},
                "Ranged": {"Hack": 0, "Pierce": 0, "Crush": 0},
            },
            "RepeatRate": {"Melee": "0", "Ranged": "0"},
            "PrepRate": {"Melee": "0", "Ranged": "0"},
            "Resistance": {"Hack": 0, "Pierce": 0, "Crush": 0},
            "Ranged": False,
            "Classes": [],
            "AttackBonuses": {},
            "Restricted": [],
            "WalkSpeed": 0,
            "Range": 0,
            "Spread": 0,
            "Civ": None,
        }

    template = sim_entity.load_inherited("simulation/templates/", unit_name, ["public"])

    # 0ad started using unit class/category prefixed to the unit name
    # separated by |, known as mixins since A25 (rP25223)
    # We strip these categories for now
    # This can be used later for classification
    unit["Parent"] = get_parent_names(unit_name)[-1] + ".xml"
    unit["Civ"] = template.find("./Identity/Civ").text
    unit["HP"] = extract_value(template.find("./Health/Max"))
    unit["BuildTime"] = extract_value(template.find("./Cost/BuildTime"))
    unit["Cost"]["population"] = extract_value(template.find("./Cost/Population"))

    resource_cost = template.find("./Cost/Resources")
    if resource_cost is not None:
        for resource_type in list(resource_cost):
            unit["Cost"][resource_type.tag] = extract_value(resource_type)

    if template.find("./Attack/Melee") is not None:
        unit["RepeatRate"]["Melee"] = extract_value(template.find("./Attack/Melee/RepeatTime"))
        unit["PrepRate"]["Melee"] = extract_value(template.find("./Attack/Melee/PrepareTime"))

        for atttype in ATTACK_TYPES:
            unit["Attack"]["Melee"][atttype] = extract_value(
                template.find("./Attack/Melee/Damage/" + atttype)
            )

        attack_melee_bonus = template.find("./Attack/Melee/Bonuses")
        if attack_melee_bonus is not None:
            for bonus in attack_melee_bonus:
                against = []
                civ_ag = []
                if bonus.find("Classes") is not None and bonus.find("Classes").text is not None:
                    against = bonus.find("Classes").text.split(" ")
                if bonus.find("Civ") is not None and bonus.find("Civ").text is not None:
                    civ_ag = bonus.find("Civ").text.split(" ")
                val = float(bonus.find("Multiplier").text)
                unit["AttackBonuses"][bonus.tag] = {
                    "Classes": against,
                    "Civs": civ_ag,
                    "Multiplier": val,
                }

        attack_restricted_classes = template.find("./Attack/Melee/RestrictedClasses")
        if attack_restricted_classes is not None:
            new_classes = attack_restricted_classes.text.split(" ")
            for elem in new_classes:
                if elem.find("-") != -1:
                    new_classes.pop(new_classes.index(elem))
                    if elem in unit["Restricted"]:
                        unit["Restricted"].pop(new_classes.index(elem))
            unit["Restricted"] += new_classes

    elif template.find("./Attack/Ranged") is not None:
        unit["Ranged"] = True
        unit["Range"] = extract_value(template.find("./Attack/Ranged/MaxRange"))
        unit["Spread"] = extract_value(template.find("./Attack/Ranged/Projectile/Spread"))
        unit["RepeatRate"]["Ranged"] = extract_value(template.find("./Attack/Ranged/RepeatTime"))
        unit["PrepRate"]["Ranged"] = extract_value(template.find("./Attack/Ranged/PrepareTime"))

        for atttype in ATTACK_TYPES:
            unit["Attack"]["Ranged"][atttype] = extract_value(
                template.find("./Attack/Ranged/Damage/" + atttype)
            )

        if template.find("./Attack/Ranged/Bonuses") is not None:
            for bonus in template.find("./Attack/Ranged/Bonuses"):
                against = []
                civ_ag = []
                if bonus.find("Classes") is not None and bonus.find("Classes").text is not None:
                    against = bonus.find("Classes").text.split(" ")
                if bonus.find("Civ") is not None and bonus.find("Civ").text is not None:
                    civ_ag = bonus.find("Civ").text.split(" ")
                val = float(bonus.find("Multiplier").text)
                unit["AttackBonuses"][bonus.tag] = {
                    "Classes": against,
                    "Civs": civ_ag,
                    "Multiplier": val,
                }
        if template.find("./Attack/Melee/RestrictedClasses") is not None:
            new_classes = template.find("./Attack/Melee/RestrictedClasses").text.split(" ")
            for elem in new_classes:
                if elem.find("-") != -1:
                    new_classes.pop(new_classes.index(elem))
                    if elem in unit["Restricted"]:
                        unit["Restricted"].pop(new_classes.index(elem))
            unit["Restricted"] += new_classes

    if template.find("Resistance") is not None:
        for atttype in ATTACK_TYPES:
            unit["Resistance"][atttype] = extract_value(
                template.find("./Resistance/Entity/Damage/" + atttype)
            )

    if (
        template.find("./UnitMotion") is not None
        and template.find("./UnitMotion/WalkSpeed") is not None
    ):
        unit["WalkSpeed"] = extract_value(template.find("./UnitMotion/WalkSpeed"))

    if template.find("./Identity/VisibleClasses") is not None:
        new_classes = template.find("./Identity/VisibleClasses").text.split(" ")
        for elem in new_classes:
            if elem.find("-") != -1:
                new_classes.pop(new_classes.index(elem))
                if elem in unit["Classes"]:
                    unit["Classes"].pop(new_classes.index(elem))
        unit["Classes"] += new_classes

    if template.find("./Identity/Classes") is not None:
        new_classes = template.find("./Identity/Classes").text.split(" ")
        for elem in new_classes:
            if elem.find("-") != -1:
                new_classes.pop(new_classes.index(elem))
                if elem in unit["Classes"]:
                    unit["Classes"].pop(new_classes.index(elem))
        unit["Classes"] += new_classes

    return unit


def write_unit(name, unit_dict):
    ret = "<tr>"
    ret += '<td class="Sub">' + name + "</td>"
    ret += "<td>" + str("{:.0f}".format(float(unit_dict["HP"]))) + "</td>"
    ret += "<td>" + str("{:.0f}".format(float(unit_dict["BuildTime"]))) + "</td>"
    ret += "<td>" + str("{:.1f}".format(float(unit_dict["WalkSpeed"]))) + "</td>"

    for atype in ATTACK_TYPES:
        percent_value = 1.0 - (0.9 ** float(unit_dict["Resistance"][atype]))
        ret += (
            "<td>"
            + str("{:.0f}".format(float(unit_dict["Resistance"][atype])))
            + " / "
            + str("%.0f" % (percent_value * 100.0))
            + "%</td>"
        )

    att_type = "Ranged" if unit_dict["Ranged"] is True else "Melee"
    if unit_dict["RepeatRate"][att_type] != "0":
        for atype in ATTACK_TYPES:
            repeat_time = float(unit_dict["RepeatRate"][att_type]) / 1000.0
            ret += (
                "<td>"
                + str("%.1f" % (float(unit_dict["Attack"][att_type][atype]) / repeat_time))
                + "</td>"
            )

        ret += "<td>" + str("%.1f" % (float(unit_dict["RepeatRate"][att_type]) / 1000.0)) + "</td>"
    else:
        for _ in ATTACK_TYPES:
            ret += "<td> - </td>"
        ret += "<td> - </td>"

    if unit_dict["Ranged"] is True and unit_dict["Range"] > 0:
        ret += "<td>" + str("{:.1f}".format(float(unit_dict["Range"]))) + "</td>"
        spread = float(unit_dict["Spread"])
        ret += "<td>" + str(f"{spread:.1f}") + "</td>"
    else:
        ret += "<td> - </td><td> - </td>"

    for rtype in RESOURCES:
        ret += "<td>" + str("{:.0f}".format(float(unit_dict["Cost"][rtype]))) + "</td>"

    ret += "<td>" + str("{:.0f}".format(float(unit_dict["Cost"]["population"]))) + "</td>"

    ret += '<td style="text-align:left;">'
    for bonus in unit_dict["AttackBonuses"]:
        ret += "["
        for classe in unit_dict["AttackBonuses"][bonus]["Classes"]:
            ret += classe + " "
        ret += ": {}]  ".format(unit_dict["AttackBonuses"][bonus]["Multiplier"])
    ret += "</td>"

    ret += "</tr>\n"
    return ret


# Sort the templates dictionary.
def sort_fn(a):
    sort_val = 0
    for classe in CLASSES_USED_FOR_SORT:
        sort_val += 1
        if classe in a[1]["Classes"]:
            break
    if COMPARATIVE_SORT_BY_CHAMP is True and a[0].find("champion") == -1:
        sort_val -= 20
    if COMPARATIVE_SORT_BY_CAV is True and a[0].find("cavalry") == -1:
        sort_val -= 10
    if a[1]["Civ"] is not None and a[1]["Civ"] in CIVS:
        sort_val += 100 * CIVS.index(a[1]["Civ"])
    return sort_val


def write_coloured_diff(file, diff, is_changed):
    """Help to write coloured text.

    diff value must always be computed as a unit_spec - unit_generic.
    A positive imaginary part represents advantageous trait.
    """

    def clever_parse(diff):
        if float(diff) - int(diff) < 0.001:
            return str(int(diff))
        return str(f"{float(diff):.1f}")

    is_advantageous = diff.imag > 0
    diff = diff.real
    if diff != 0:
        is_changed = True
    else:
        # do not change its value if one parameter is not changed (yet)
        # some other parameter might be different
        pass

    if diff == 0:
        rgb_str = "200,200,200"
    elif (is_advantageous and diff > 0) or (not is_advantageous and diff < 0):
        rgb_str = "180,0,0"
    else:
        rgb_str = "0,150,0"

    file.write(
        f"""<td><span style="color:rgb({rgb_str});">{clever_parse(diff)}</span></td>
        """
    )
    return is_changed


def compute_unit_efficiency_diff(templates_by_parent, civs):
    efficiency_table = {}
    for parent in templates_by_parent:
        for template in [
            template for template in templates_by_parent[parent] if template[1]["Civ"] not in civs
        ]:
            print(template)

        templates_by_parent[parent] = [
            template for template in templates_by_parent[parent] if template[1]["Civ"] in civs
        ]
        templates_by_parent[parent].sort(key=lambda x: civs.index(x[1]["Civ"]))

        for tp in templates_by_parent[parent]:
            # HP
            diff = -1j + (int(tp[1]["HP"]) - int(templates[parent]["HP"]))
            efficiency_table[(parent, tp[0], "HP")] = diff
            efficiency_table[(parent, tp[0], "HP")] = diff

            # Build Time
            diff = +1j + (int(tp[1]["BuildTime"]) - int(templates[parent]["BuildTime"]))
            efficiency_table[(parent, tp[0], "BuildTime")] = diff

            # walk speed
            diff = -1j + (float(tp[1]["WalkSpeed"]) - float(templates[parent]["WalkSpeed"]))
            efficiency_table[(parent, tp[0], "WalkSpeed")] = diff

            # Resistance
            for atype in ATTACK_TYPES:
                diff = -1j + (
                    float(tp[1]["Resistance"][atype])
                    - float(templates[parent]["Resistance"][atype])
                )
                efficiency_table[(parent, tp[0], "Resistance/" + atype)] = diff

            # Attack types (DPS) and rate.
            att_type = "Ranged" if tp[1]["Ranged"] is True else "Melee"
            if tp[1]["RepeatRate"][att_type] != "0":
                for atype in ATTACK_TYPES:
                    my_dps = float(tp[1]["Attack"][att_type][atype]) / (
                        float(tp[1]["RepeatRate"][att_type]) / 1000.0
                    )
                    parent_dps = float(templates[parent]["Attack"][att_type][atype]) / (
                        float(templates[parent]["RepeatRate"][att_type]) / 1000.0
                    )
                    diff = -1j + (my_dps - parent_dps)
                    efficiency_table[(parent, tp[0], "Attack/" + att_type + "/" + atype)] = diff
                diff = -1j + (
                    float(tp[1]["RepeatRate"][att_type]) / 1000.0
                    - float(templates[parent]["RepeatRate"][att_type]) / 1000.0
                )
                efficiency_table[
                    (parent, tp[0], "Attack/" + att_type + "/" + atype + "/RepeatRate")
                ] = diff
                # range and spread
                if tp[1]["Ranged"] is True:
                    diff = -1j + (float(tp[1]["Range"]) - float(templates[parent]["Range"]))
                    efficiency_table[(parent, tp[0], "Attack/" + att_type + "/Ranged/Range")] = (
                        diff
                    )

                    diff = float(tp[1]["Spread"]) - float(templates[parent]["Spread"])
                    efficiency_table[
                        (parent, tp[0], "Attack/" + att_type + "/Ranged/Projectile/Spread")
                    ] = diff

            for rtype in RESOURCES:
                diff = +1j + (
                    float(tp[1]["Cost"][rtype]) - float(templates[parent]["Cost"][rtype])
                )
                efficiency_table[(parent, tp[0], "Resources/" + rtype)] = diff

            diff = +1j + (
                float(tp[1]["Cost"]["population"]) - float(templates[parent]["Cost"]["population"])
            )
            efficiency_table[(parent, tp[0], "Population")] = diff

    return efficiency_table


def compute_templates(load_templates_if_parent):
    """Loops over template XMLs and selectively insert into templates dict."""
    templates = {}
    for template in base_path.glob("template_*.xml"):
        if not template.is_file():
            continue

        template_file_name = str(template.relative_to(base_path))
        if has_any_parent_template(template_file_name, load_templates_if_parent):
            templates[template_file_name] = calc_unit(str(template))

    return templates


def compute_civ_templates(civs: list):
    """Load Civ specific templates.

    NOTE: whether a Civ can train a certain unit is not recorded in the unit
    .xml files, and hence we have to get that info elsewhere, e.g. from the
    Civ tree. This should be delayed until this whole parser is based on the
    Civ tree itself.

    This function must always ensure that Civ unit parenthood works as
    intended, i.e. a unit in a Civ indeed has a 'Civ' field recording its
    loyalty to that Civ. Check this when upgrading this script to keep
    up with the game engine.
    """
    civ_templates = {}

    for civ in civs:
        civ_templates[civ] = {}
        # Load all templates that start with that civ indicator
        # TODO: consider adding mixin/civs here too
        for template in base_path.glob("units/" + civ + "/*.xml"):
            if not template.is_file():
                continue

            template_file_name = str(template.relative_to(base_path))

            # filter based on FilterOut
            break_it = False
            for civ_filter in FILTER_OUT:
                if template_file_name.find(civ_filter) != -1:
                    break_it = True
            if break_it:
                continue

            # filter based on loaded generic templates
            if not has_any_parent_template(template_file_name, LOAD_TEMPLATES_IF_PARENT):
                continue

            unit = calc_unit(str(template))

            # Remove variants for now
            if unit["Parent"].find("template_") == -1:
                continue

            # load template
            civ_templates[civ][template_file_name] = unit

    return civ_templates


def compute_templates_by_parent(templates: dict, civs: list, civ_templates: dict):
    """Get them in the array."""
    # civs:list -> civ_templates:dict -> templates:dict -> templates_by_parent
    templates_by_parent = {}
    for civ in civs:
        for civ_unit_template in civ_templates[civ]:
            parent = civ_templates[civ][civ_unit_template]["Parent"]

            # We have the following constant equality
            # templates[*]["Civ"] === gaia
            # if parent in templates and templates[parent]["Civ"] == None:
            if parent in templates:
                if parent not in templates_by_parent:
                    templates_by_parent[parent] = []
                templates_by_parent[parent].append(
                    (civ_unit_template, civ_templates[civ][civ_unit_template])
                )

    # debug after CivTemplates are non-empty
    return templates_by_parent


############################################################
## Pre-compute all tables
templates = compute_templates(LOAD_TEMPLATES_IF_PARENT)
CivTemplates = compute_civ_templates(CIVS)
TemplatesByParent = compute_templates_by_parent(templates, CIVS, CivTemplates)

# Not used; use it for your own custom analysis
efficiency_table = compute_unit_efficiency_diff(TemplatesByParent, CIVS)


############################################################
def write_html():
    """Create the HTML file."""
    f = open(Path(__file__).parent / "unit_summary_table.html", "w", encoding="utf8")

    f.write(
        """
<!DOCTYPE html PUBLIC "-//W3C//DTD HTML 4.01//EN">
<html>
<head>
        <title>Unit Tables</title>
        <link rel="stylesheet" href="style.css">
</head>
<body>
        """
    )
    htbout(f, "h1", "Unit Summary Table")
    f.write("\n")

    # Write generic templates
    htbout(f, "h2", "Units")
    f.write(
        """
<table id="genericTemplates">
  <thead>
    <tr>
      <th> </th> <th>HP </th> <th>BuildTime </th> <th>Speed(walk) </th>
          <th colspan="5">Resistance </th>
          <th colspan="8">Attack (DPS) </th>
          <th colspan="5">Costs </th>
          <th>Efficient Against </th>
        </tr>
    <tr class="Label" style="border-bottom:1px black solid;">
      <th> </th> <th> </th> <th> </th> <th> </th>
          <th>H </th> <th>P </th> <th>C </th><th>P </th><th>F </th>
          <th>H </th> <th>P </th> <th>C </th><th>P </th><th>F </th>
      <th>Rate </th> <th>Range </th> <th>Spread (/100m) </th>
          <th>F </th> <th>W </th> <th>S </th> <th>M </th> <th>P </th>
          <th> </th>
        </tr>
</thead>
        """
    )
    for template in templates:
        f.write(write_unit(template, templates[template]))
    f.write("</table>")

    # Write unit specialization
    # Sort them by civ and write them in a table.
    #
    # TODO: pre-compute the diffs then render, filtering out the non-interesting
    # ones
    #
    f.write(
        """
<h2>Units Specializations
</h2>

<p class="desc">This table compares each template to its parent, showing the
differences between the two.
  <br/>Note that like any table, you can copy/paste this in Excel (or Numbers or
  ...) and sort it.
</p>

<table id="TemplateParentComp">
  <thead>
    <tr>
      <th> </th> <th> </th> <th>HP </th> <th>BuildTime </th>
          <th>Speed (/100m) </th>
          <th colspan="5">Resistance </th>
          <th colspan="8">Attack </th>
          <th colspan="5">Costs </th>
          <th>Civ </th>
        </tr>
    <tr class="Label" style="border-bottom:1px black solid;">
      <th> </th> <th> </th> <th> </th> <th> </th> <th> </th>
          <th>H </th> <th>P </th> <th>C </th><th>P </th><th>F </th>
          <th>H </th> <th>P </th> <th>C </th><th>P </th><th>F </th>
      <th>Rate </th> <th>Range </th> <th>Spread </th>
          <th>F </th> <th>W </th> <th>S </th> <th>M </th> <th>P </th>
          <th> </th>
        </tr>
  </thead>
        """
    )
    for parent in TemplatesByParent:
        TemplatesByParent[parent].sort(key=lambda x: CIVS.index(x[1]["Civ"]))
        for tp in TemplatesByParent[parent]:
            is_changed = False
            ff = open(Path(__file__).parent / ".cache", "w", encoding="utf8")

            ff.write("<tr>")
            ff.write(
                "<th style='font-size:10px'>"
                + parent.replace(".xml", "").replace("template_", "")
                + "</th>"
            )
            ff.write(
                '<td class="Sub">' + tp[0].replace(".xml", "").replace("units/", "") + "</td>"
            )

            # HP
            diff = -1j + (int(tp[1]["HP"]) - int(templates[parent]["HP"]))
            is_changed = write_coloured_diff(ff, diff, is_changed)

            # Build Time
            diff = +1j + (int(tp[1]["BuildTime"]) - int(templates[parent]["BuildTime"]))
            is_changed = write_coloured_diff(ff, diff, is_changed)

            # walk speed
            diff = -1j + (float(tp[1]["WalkSpeed"]) - float(templates[parent]["WalkSpeed"]))
            is_changed = write_coloured_diff(ff, diff, is_changed)

            # Resistance
            for atype in ATTACK_TYPES:
                diff = -1j + (
                    float(tp[1]["Resistance"][atype])
                    - float(templates[parent]["Resistance"][atype])
                )
                is_changed = write_coloured_diff(ff, diff, is_changed)

            # Attack types (DPS) and rate.
            att_type = "Ranged" if tp[1]["Ranged"] is True else "Melee"
            if tp[1]["RepeatRate"][att_type] != "0":
                for atype in ATTACK_TYPES:
                    my_dps = float(tp[1]["Attack"][att_type][atype]) / (
                        float(tp[1]["RepeatRate"][att_type]) / 1000.0
                    )
                    parent_dps = float(templates[parent]["Attack"][att_type][atype]) / (
                        float(templates[parent]["RepeatRate"][att_type]) / 1000.0
                    )
                    is_changed = write_coloured_diff(ff, -1j + (my_dps - parent_dps), is_changed)
                is_changed = write_coloured_diff(
                    ff,
                    -1j
                    + (
                        float(tp[1]["RepeatRate"][att_type]) / 1000.0
                        - float(templates[parent]["RepeatRate"][att_type]) / 1000.0
                    ),
                    is_changed,
                )
                # range and spread
                if tp[1]["Ranged"] is True:
                    is_changed = write_coloured_diff(
                        ff,
                        -1j + (float(tp[1]["Range"]) - float(templates[parent]["Range"])),
                        is_changed,
                    )
                    my_spread = float(tp[1]["Spread"])
                    parent_spread = float(templates[parent]["Spread"])
                    is_changed = write_coloured_diff(
                        ff, +1j + (my_spread - parent_spread), is_changed
                    )
                else:
                    ff.write(
                        "<td><span style='color:rgb(200,200,200);'>-</span></td><td>"
                        "<span style='color:rgb(200,200,200);'>-</span></td>"
                    )
            else:
                ff.write("<td></td><td></td><td></td><td></td><td></td><td></td>")

            for rtype in RESOURCES:
                is_changed = write_coloured_diff(
                    ff,
                    +1j + (float(tp[1]["Cost"][rtype]) - float(templates[parent]["Cost"][rtype])),
                    is_changed,
                )

            is_changed = write_coloured_diff(
                ff,
                +1j
                + (
                    float(tp[1]["Cost"]["population"])
                    - float(templates[parent]["Cost"]["population"])
                ),
                is_changed,
            )

            ff.write("<td>" + tp[1]["Civ"] + "</td>")
            ff.write("</tr>\n")

            ff.close()  # to actually write into the file
            with open(Path(__file__).parent / ".cache", encoding="utf-8") as ff:
                unit_str = ff.read()

            if SHOW_CHANGED_ONLY:
                if is_changed:
                    f.write(unit_str)
            else:
                # print the full table if SHOW_CHANGED_ONLY is false
                f.write(unit_str)

    f.write("<table/>")

    # Table of unit having or not having some units.
    f.write(
        """
<h2>Roster Variety
</h2>

<p class="desc">This table show which civilizations have units who derive from
each loaded generic template.
  <br/>Grey means the civilization has no unit derived from a generic template;
  <br/>dark green means 1 derived unit, mid-tone green means 2, bright green
  means 3 or more.
  <br/>The total is the total number of loaded units for this civ, which may be
  more than the total of units inheriting from loaded templates.
</p>
<table class="CivRosterVariety">
  <tr>
    <th>Template </th>
"""
    )
    for civ in CIVS:
        f.write('<td class="vertical-text">' + civ + "</td>\n")
    f.write("</tr>\n")

    sorted_dict = sorted(templates.items(), key=sort_fn)

    for tp in sorted_dict:
        if tp[0] not in TemplatesByParent:
            continue
        f.write("<tr><td>" + tp[0] + "</td>\n")
        for civ in CIVS:
            found = 0
            for temp in TemplatesByParent[tp[0]]:
                if temp[1]["Civ"] == civ:
                    found += 1
            if found == 1:
                f.write('<td style="background-color:rgb(0,90,0);"></td>')
            elif found == 2:
                f.write('<td style="background-color:rgb(0,150,0);"></td>')
            elif found >= 3:
                f.write('<td style="background-color:rgb(0,255,0);"></td>')
            else:
                f.write('<td style="background-color:rgb(200,200,200);"></td>')
        f.write("</tr>\n")
    f.write(
        '<tr style="margin-top:2px;border-top:2px #aaa solid;">\
        <th style="text-align:right; padding-right:10px;">Total:</th>\n'
    )
    for civ in CIVS:
        count = 0
        for _units in CivTemplates[civ]:
            count += 1
        f.write('<td style="text-align:center;">' + str(count) + "</td>\n")

    f.write("</tr>\n")

    f.write("<table/>")

    # Add a simple script to allow filtering on sorting directly in the HTML
    # page.
    if ADD_SORTING_OVERLAY:
        f.write(
            """
<script src="tablefilter/tablefilter.js"></script>
<script data-config>
var cast = function (val) {
console.log(val);                       if (+val != val)
                return -999999999999;
        return +val;
}


var filtersConfig = {
    base_path: "tablefilter/",
    col_0: "checklist",
    alternate_rows: true,
    rows_counter: true,
    btn_reset: true,
    loader: false,
    status_bar: false,
    mark_active_columns: true,
    highlight_keywords: true,
    col_number_format: Array(22).fill("US"),
    filters_row_index: 2,
    headers_row_index: 1,
    extensions: [
        {
            name: "sort",
            types: ["string",
                    ...Array(6).fill("us"),
                    ...Array(6).fill("mytype"),
                    ...Array(5).fill("us"),
                    "string",
                   ],
            on_sort_loaded: function (o, sort) {
                sort.addSortType("mytype", cast);
            },
        },
    ],
    col_widths: [...Array(18).fill(null), "120px"],
};

var tf = new TableFilter('genericTemplates', filtersConfig,2);
tf.init();

var secondFiltersConfig = {
    base_path: "tablefilter/",
    col_0: "checklist",
    col_19: "checklist",
    alternate_rows: true,
    rows_counter: true,
    btn_reset: true,
    loader: false,
    status_bar: false,
    mark_active_columns: true,
    highlight_keywords: true,
    col_number_format: [null, null, ...Array(17).fill("US"), null],
    filters_row_index: 2,
    headers_row_index: 1,
    extensions: [
        {
            name: "sort",
            types: ["string", "string",
                    ...Array(6).fill("us"),
                    ...Array(6).fill("typetwo"),
                    ...Array(5).fill("us"),
                    "string",
                   ],
            on_sort_loaded: function (o, sort) {
                sort.addSortType("typetwo", cast);
            },
        },
    ],
    col_widths: Array(20).fill(null),
};


var tf2 = new TableFilter('TemplateParentComp', secondFiltersConfig,2);
tf2.init();

</script>
        """
        )

    f.write("</body>\n</html>")


if __name__ == "__main__":
    write_html()
