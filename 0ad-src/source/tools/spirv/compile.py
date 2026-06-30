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

import argparse
import hashlib
import itertools
import json
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from enum import Enum
from functools import cache
from multiprocessing import Pool
from pathlib import Path

import yaml


try:
    from yaml import CSafeLoader as SafeLoader
except ImportError:
    from yaml import SafeLoader

STAGE_EXTENSIONS = {
    "vertex": ".vs",
    "fragment": ".fs",
    "geometry": ".gs",
    "compute": ".cs",
}

YAML_DIRECTIVE_PATTERN = re.compile(b"^%YAML 1.0\r?\n")


class VkDescriptorType(Enum):
    COMBINED_IMAGE_SAMPLER = 1
    STORAGE_IMAGE = 3
    UNIFORM_BUFFER = 6
    STORAGE_BUFFER = 7


@cache
def get_spirv_reflect_location():
    spirv_reflect = os.getenv("SPIRV_REFLECT", "spirv-reflect")
    if shutil.which(spirv_reflect) is None:
        spirv_reflect = (
            Path(__file__).resolve().parent.parent.parent.parent
            / "libraries/source/spirv-reflect/bin/spirv-reflect"
        )
    return spirv_reflect


def execute(command):
    try:
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = process.communicate()
    except:
        sys.stderr.write("Failed to run command: {}\n".format(" ".join(command)))
        raise
    return process.returncode, out, err


def calculate_hash(path):
    assert os.path.isfile(path)
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def get_combination_hash(combination: list[dict[str, str]]) -> str:
    """Turn combination information into a unique hash."""
    hashable_combination = tuple([tuple(sorted(i.items())) for i in combination])
    return hashlib.sha256(json.dumps(hashable_combination, sort_keys=True).encode()).hexdigest()


def resolve_if(defines, expression):
    for item in expression.strip().split("||"):
        item = item.strip()
        assert len(item) > 1
        name = item
        invert = False
        if name[0] == "!":
            invert = True
            name = item[1:]
            assert item[1].isalpha()
        else:
            assert item[0].isalpha()
        found_define = False
        for define in defines:
            if define["name"] == name:
                assert (
                    define["value"] == "UNDEFINED"
                    or define["value"] == "0"
                    or define["value"] == "1"
                )
                if invert:
                    if define["value"] != "1":
                        return True
                    found_define = True
                elif define["value"] == "1":
                    return True
        if invert and not found_define:
            return True
    return False


def compile_and_reflect(input_mod_path, dependencies, stage, path, out_path, defines):
    keep_debug = False
    input_path = os.path.normpath(path)
    output_path = os.path.normpath(out_path)
    command = [
        "glslc",
        "-x",
        "glsl",
        "--target-env=vulkan1.1",
        "-std=450core",
        "-I",
        os.path.join(input_mod_path, "shaders", "glsl"),
    ]
    for dependency in dependencies:
        if dependency != input_mod_path:
            command += ["-I", os.path.join(dependency, "shaders", "glsl")]
    command += [
        "-fshader-stage=" + stage,
        "-O",
        input_path,
    ]
    use_descriptor_indexing = False
    for define in defines:
        if define["value"] == "UNDEFINED":
            continue
        assert " " not in define["value"]
        command.append("-D{}={}".format(define["name"], define["value"]))
        if define["name"] == "USE_DESCRIPTOR_INDEXING":
            use_descriptor_indexing = True
    command.append("-D{}={}".format("USE_SPIRV", "1"))
    command.append("-DSTAGE_{}={}".format(stage.upper(), "1"))
    command += ["-o", output_path]
    # Compile the shader with debug information to see names in reflection.
    ret, out, err = execute([*command, "-g"])
    if ret:
        sys.stderr.write(
            "Command returned {}:\nCommand: {}\nInput path: {}\nOutput path: {}\n"
            "Error: {}\n".format(ret, " ".join(command), input_path, output_path, err)
        )
        preprocessor_output_path = os.path.abspath(
            os.path.join(os.path.dirname(__file__), "preprocessed_file.glsl")
        )
        execute([*command[:-2], "-g", "-E", "-o", preprocessor_output_path])
        raise ValueError(err)

    spirv_reflect = get_spirv_reflect_location()
    ret, out, err = execute([spirv_reflect, "-y", "-v", "1", output_path])
    if ret:
        sys.stderr.write(
            "Command returned {}:\nCommand: {}\nInput path: {}\nOutput path: {}\n"
            "Error: {}\n".format(ret, " ".join(command), input_path, output_path, err)
        )
        raise ValueError(err)

    # Reflect the result SPIRV.

    # libyaml doesn't support files with a YAML 1.0 directive, so
    # let's strip the directive out before loading the data.
    data = yaml.load(YAML_DIRECTIVE_PATTERN.sub(b"", out), Loader=SafeLoader)
    module = data["module"]
    interface_variables = []
    if data.get("all_interface_variables"):
        interface_variables = data["all_interface_variables"]
    push_constants = []
    vertex_attributes = []
    if module.get("push_constants"):
        assert len(module["push_constants"]) == 1

        def add_push_constants(node, push_constants):
            if node.get("members"):
                for member in node["members"]:
                    add_push_constants(member, push_constants)
            else:
                assert node["absolute_offset"] + node["size"] <= 128
                push_constants.append(
                    {
                        "name": node["name"],
                        "offset": node["absolute_offset"],
                        "size": node["size"],
                    }
                )

        assert module["push_constants"][0]["type_description"]["type_name"] == "DrawUniforms"
        assert module["push_constants"][0]["size"] <= 128
        add_push_constants(module["push_constants"][0], push_constants)
    descriptor_sets = []
    if module.get("descriptor_sets"):
        for descriptor_set in module["descriptor_sets"]:
            uniform_set = 1 if use_descriptor_indexing else 0
            is_storage_buffer_set = (
                descriptor_set["binding_count"] > 0
                and descriptor_set["bindings"][0]["descriptor_type"]
                == VkDescriptorType.STORAGE_BUFFER.value
            )
            storage_set = uniform_set + 1 if is_storage_buffer_set else 2
            bindings = []
            if descriptor_set["set"] == uniform_set:
                assert descriptor_set["binding_count"] > 0
                for binding in descriptor_set["bindings"]:
                    assert binding["set"] == uniform_set
                    block = binding["block"]
                    members = []
                    for member in block["members"]:
                        members.append(
                            {
                                "name": member["name"],
                                "offset": member["absolute_offset"],
                                "size": member["size"],
                            }
                        )
                    bindings.append(
                        {
                            "binding": binding["binding"],
                            "type": "uniform",
                            "size": block["size"],
                            "members": members,
                        }
                    )
                binding = descriptor_set["bindings"][0]
                assert binding["descriptor_type"] == VkDescriptorType.UNIFORM_BUFFER.value
            elif descriptor_set["set"] == storage_set:
                assert descriptor_set["binding_count"] > 0
                for binding in descriptor_set["bindings"]:
                    is_storage_image = (
                        binding["descriptor_type"] == VkDescriptorType.STORAGE_IMAGE.value
                    )
                    is_storage_buffer = (
                        binding["descriptor_type"] == VkDescriptorType.STORAGE_BUFFER.value
                    )
                    assert is_storage_image or is_storage_buffer
                    assert (
                        binding["descriptor_type"]
                        == descriptor_set["bindings"][0]["descriptor_type"]
                    )
                    assert binding["image"]["arrayed"] == 0
                    assert binding["image"]["ms"] == 0
                    binding_type = "storageImage"
                    binding_name = binding["name"]
                    if is_storage_buffer:
                        binding_type = "storageBuffer"
                        binding_name = binding["type_description"]["type_name"]
                    bindings.append(
                        {
                            "binding": binding["binding"],
                            "type": binding_type,
                            "name": binding_name,
                        }
                    )
            elif use_descriptor_indexing:
                if descriptor_set["set"] == 0:
                    assert descriptor_set["binding_count"] >= 1
                    for binding in descriptor_set["bindings"]:
                        assert (
                            binding["descriptor_type"]
                            == VkDescriptorType.COMBINED_IMAGE_SAMPLER.value
                        )
                        assert binding["array"]["dims"][0] == 16384
                        if binding["binding"] == 0:
                            assert binding["name"] == "textures2D"
                        elif binding["binding"] == 1:
                            assert binding["name"] == "texturesCube"
                        elif binding["binding"] == 2:
                            assert binding["name"] == "texturesShadow"
                else:
                    raise AssertionError
            else:
                assert descriptor_set["binding_count"] > 0
                for binding in descriptor_set["bindings"]:
                    assert (
                        binding["descriptor_type"] == VkDescriptorType.COMBINED_IMAGE_SAMPLER.value
                    )
                    assert binding["image"]["sampled"] == 1
                    assert binding["image"]["arrayed"] == 0
                    assert binding["image"]["ms"] == 0
                    sampler_type = "sampler{}D".format(binding["image"]["dim"] + 1)
                    if binding["image"]["dim"] == 3:
                        sampler_type = "samplerCube"
                    bindings.append(
                        {
                            "binding": binding["binding"],
                            "type": sampler_type,
                            "name": binding["name"],
                        }
                    )
            descriptor_sets.append(
                {
                    "set": descriptor_set["set"],
                    "bindings": bindings,
                }
            )
    if stage == "vertex":
        for variable in interface_variables:
            if variable["storage_class"] == 1:
                # Input.
                vertex_attributes.append(
                    {
                        "name": variable["name"],
                        "location": variable["location"],
                    }
                )
    # Compile the final version without debug information.
    if not keep_debug:
        strip_command = [
            "spirv-opt",
            "--strip-debug",
            "--compact-ids",
            output_path,
            "-o",
            output_path,
        ]
        ret, out, err = execute(strip_command)
        if ret:
            sys.stderr.write(
                "Command returned {}:\nCommand: {}\nError: {}\n".format(
                    ret, " ".join(strip_command), err
                )
            )
            raise ValueError(err)
    return {
        "push_constants": push_constants,
        "vertex_attributes": vertex_attributes,
        "descriptor_sets": descriptor_sets,
    }


def output_xml_tree(tree, path):
    """We use a simple custom printer to have the same output for all platforms."""
    with open(path, "w", encoding="utf-8") as handle:
        handle.write('<?xml version="1.0" encoding="utf-8"?>\n')
        handle.write(f"<!-- DO NOT EDIT: GENERATED BY SCRIPT {os.path.basename(__file__)} -->\n")

        def output_xml_node(node, handle, depth):
            indent = "\t" * depth
            attributes = ""
            for attribute_name in sorted(node.attrib.keys()):
                attributes += f' {attribute_name}="{node.attrib[attribute_name]}"'
            if len(node) > 0:
                handle.write(f"{indent}<{node.tag}{attributes}>\n")
                for child in node:
                    output_xml_node(child, handle, depth + 1)
                handle.write(f"{indent}</{node.tag}>\n")
            else:
                handle.write(f"{indent}<{node.tag}{attributes}/>\n")

        output_xml_node(tree.getroot(), handle, 0)


def build_combination(
    combination,
    program_defines,
    shaders,
    dependencies,
    program_name,
    input_mod_path,
    output_mod_path,
    output_spirv_mod_path,
):
    combination_hash = get_combination_hash(combination)

    program_path = f"spirv/{program_name}_{combination_hash}.xml"

    program_root = ET.Element("program")
    program_root.set("type", "spirv")
    for shader in shaders:
        extension = STAGE_EXTENSIONS[shader["type"]]
        tmp_file_name = f"{program_name}_{combination_hash}{extension}.spv"
        tmp_output_spirv_path = os.path.join(output_spirv_mod_path, tmp_file_name)

        input_glsl_path = os.path.join(input_mod_path, "shaders", shader["file"])
        # Some shader programs might use vs and fs shaders from different mods.
        if not os.path.isfile(input_glsl_path):
            input_glsl_path = None
            for dependency in dependencies:
                fallback_input_path = os.path.join(dependency, "shaders", shader["file"])
                if os.path.isfile(fallback_input_path):
                    input_glsl_path = fallback_input_path
                    break
        assert input_glsl_path is not None

        reflection = compile_and_reflect(
            input_mod_path,
            dependencies,
            shader["type"],
            input_glsl_path,
            tmp_output_spirv_path,
            combination + program_defines,
        )

        # Deduplicate identical shaders, by moving them to a
        # destination where their file name contains a hash over their
        # content. To avoid file corruption by race conditions when
        # multiple processes write the same shader file concurrently,
        # this intentionally renames the files instead of copying them.
        spirv_hash = calculate_hash(tmp_output_spirv_path)
        file_name = f"{spirv_hash}{extension}.spv"
        output_spirv_path = os.path.join(output_spirv_mod_path, file_name)
        if not os.path.exists(output_spirv_path):
            try:
                os.rename(tmp_output_spirv_path, output_spirv_path)
            except FileExistsError:
                os.remove(tmp_output_spirv_path)
        else:
            os.remove(tmp_output_spirv_path)

        shader_element = ET.SubElement(program_root, shader["type"])
        shader_element.set("file", "spirv/" + file_name)
        if shader["type"] == "vertex":
            for stream in shader["streams"]:
                if "if" in stream and not resolve_if(combination, stream["if"]):
                    continue

                found_vertex_attribute = False
                for vertex_attribute in reflection["vertex_attributes"]:
                    if vertex_attribute["name"] == stream["attribute"]:
                        found_vertex_attribute = True
                        break
                if not found_vertex_attribute and stream["attribute"] == "a_tangent":
                    continue
                if not found_vertex_attribute:
                    sys.stderr.write(
                        "Vertex attribute not found: {}\n".format(stream["attribute"])
                    )
                assert found_vertex_attribute

                stream_element = ET.SubElement(shader_element, "stream")
                stream_element.set("name", stream["name"])
                stream_element.set("attribute", stream["attribute"])
                for vertex_attribute in reflection["vertex_attributes"]:
                    if vertex_attribute["name"] == stream["attribute"]:
                        stream_element.set("location", vertex_attribute["location"])
                        break

        for push_constant in reflection["push_constants"]:
            push_constant_element = ET.SubElement(shader_element, "push_constant")
            push_constant_element.set("name", push_constant["name"])
            push_constant_element.set("size", push_constant["size"])
            push_constant_element.set("offset", push_constant["offset"])
        descriptor_sets_element = ET.SubElement(shader_element, "descriptor_sets")
        for descriptor_set in reflection["descriptor_sets"]:
            descriptor_set_element = ET.SubElement(descriptor_sets_element, "descriptor_set")
            descriptor_set_element.set("set", descriptor_set["set"])
            for binding in descriptor_set["bindings"]:
                binding_element = ET.SubElement(descriptor_set_element, "binding")
                binding_element.set("type", binding["type"])
                binding_element.set("binding", binding["binding"])
                if binding["type"] == "uniform":
                    binding_element.set("size", binding["size"])
                    for member in binding["members"]:
                        member_element = ET.SubElement(binding_element, "member")
                        member_element.set("name", member["name"])
                        member_element.set("size", member["size"])
                        member_element.set("offset", member["offset"])
                elif binding["type"].startswith("sampler") or binding["type"].startswith(
                    "storage"
                ):
                    binding_element.set("name", binding["name"])

        program_tree = ET.ElementTree(program_root)
        output_xml_tree(program_tree, os.path.join(output_mod_path, "shaders", program_path))


def build(rules, input_mod_path, output_mod_path, dependencies, program_name, process_pool):
    sys.stdout.write(f'Program "{program_name}"\n')
    if rules and program_name not in rules:
        sys.stdout.write("  Skip.\n")
        return
    sys.stdout.write("  Building.\n")

    rebuild = False

    defines = []
    program_defines = []
    shaders = []

    tree = ET.parse(os.path.join(input_mod_path, "shaders", "glsl", program_name + ".xml"))
    root = tree.getroot()
    for element in root:
        element_tag = element.tag
        if element_tag == "defines":
            for child in element:
                values = []
                for value in child:
                    values.append(
                        {
                            "name": child.attrib["name"],
                            "value": value.text,
                        }
                    )
                defines.append(values)
        elif element_tag == "define":
            program_defines.append(
                {"name": element.attrib["name"], "value": element.attrib["value"]}
            )
        elif element_tag == "vertex":
            streams = []
            for shader_child in element:
                assert shader_child.tag == "stream"
                streams.append(
                    {
                        "name": shader_child.attrib["name"],
                        "attribute": shader_child.attrib["attribute"],
                    }
                )
                if "if" in shader_child.attrib:
                    streams[-1]["if"] = shader_child.attrib["if"]
            shaders.append(
                {
                    "type": "vertex",
                    "file": element.attrib["file"],
                    "streams": streams,
                }
            )
        elif element_tag == "fragment":
            shaders.append(
                {
                    "type": "fragment",
                    "file": element.attrib["file"],
                }
            )
        elif element_tag == "compute":
            shaders.append(
                {
                    "type": "compute",
                    "file": element.attrib["file"],
                }
            )
        else:
            raise ValueError(f'Unsupported element tag: "{element_tag}"')

    output_spirv_mod_path = os.path.join(output_mod_path, "shaders", "spirv")
    if not os.path.isdir(output_spirv_mod_path):
        os.mkdir(output_spirv_mod_path)

    if "combinations" in rules[program_name]:
        combinations = rules[program_name]["combinations"]
    else:
        combinations = list(itertools.product(*defines))

    results = []

    for combination in combinations:
        combination_hash = get_combination_hash(combination)

        program_path = f"spirv/{program_name}_{combination_hash}.xml"

        if not rebuild and os.path.isfile(os.path.join(output_mod_path, "shaders", program_path)):
            continue

        results.append(
            process_pool.apply_async(
                build_combination,
                [
                    combination,
                    program_defines,
                    shaders,
                    dependencies,
                    program_name,
                    input_mod_path,
                    output_mod_path,
                    output_spirv_mod_path,
                ],
            )
        )

    for result in results:
        result.get()

    root = ET.Element("programs")

    for combination in combinations:
        combination_hash = get_combination_hash(combination)

        program_path = f"spirv/{program_name}_{combination_hash}.xml"

        programs_element = ET.SubElement(root, "program")
        programs_element.set("type", "spirv")
        programs_element.set("file", program_path)

        defines_element = ET.SubElement(programs_element, "defines")
        for define in combination:
            if define["value"] == "UNDEFINED":
                continue
            define_element = ET.SubElement(defines_element, "define")
            define_element.set("name", define["name"])
            define_element.set("value", define["value"])

    tree = ET.ElementTree(root)
    output_xml_tree(tree, os.path.join(output_mod_path, "shaders", "spirv", program_name + ".xml"))


def run():
    def positive_int(arg):
        if int(arg) <= 0:
            raise argparse.ArgumentTypeError("must be a positive number")
        return int(arg)

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "input_mod_path",
        help="a path to a directory with input mod with GLSL shaders "
        "like binaries/data/mods/public",
    )
    parser.add_argument("rules_path", help="a path to JSON with rules")
    parser.add_argument(
        "output_mod_path",
        help="a path to a directory with mod to store SPIR-V shaders "
        "like binaries/data/mods/spirv",
    )
    parser.add_argument(
        "-d",
        "--dependency",
        action="append",
        help="a path to a directory with a dependency mod (at least "
        "modmod should present as dependency)",
        required=True,
    )
    parser.add_argument(
        "-p",
        "--program_name",
        help="a shader program name (in case of presence the only program will be compiled)",
        default=None,
    )
    parser.add_argument(
        "-j",
        "--jobs",
        type=positive_int,
        help="Number of jobs to run in parallel. Defaults to the number of available CPU cores.",
        default=os.cpu_count(),
    )
    args = parser.parse_args()

    if not os.path.isfile(args.rules_path):
        sys.stderr.write(f'Rules "{args.rules_path}" are not found\n')
        return
    with open(args.rules_path, encoding="utf-8") as handle:
        rules = json.load(handle)

    if not os.path.isdir(args.input_mod_path):
        sys.stderr.write(f'Input mod path "{args.input_mod_path}" is not a directory\n')
        return

    if not os.path.isdir(args.output_mod_path):
        sys.stderr.write(f'Output mod path "{args.output_mod_path}" is not a directory\n')
        return

    mod_shaders_path = os.path.join(args.input_mod_path, "shaders", "glsl")
    if not os.path.isdir(mod_shaders_path):
        sys.stderr.write(f'Directory "{mod_shaders_path}" was not found\n')
        return

    mod_name = os.path.basename(os.path.normpath(args.input_mod_path))
    sys.stdout.write(f'Building SPIRV for "{mod_name}"\n')

    with Pool(processes=args.jobs) as process_pool:
        if not args.program_name:
            for file_name in os.listdir(mod_shaders_path):
                name, ext = os.path.splitext(file_name)
                if ext.lower() == ".xml":
                    build(
                        rules,
                        args.input_mod_path,
                        args.output_mod_path,
                        args.dependency,
                        name,
                        process_pool,
                    )
        else:
            build(
                rules,
                args.input_mod_path,
                args.output_mod_path,
                args.dependency,
                args.program_name,
                process_pool,
            )


if __name__ == "__main__":
    run()
