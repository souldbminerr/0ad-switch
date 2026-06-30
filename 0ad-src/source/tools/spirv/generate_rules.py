#!/usr/bin/env python3
#
# Copyright (C) 2024 Wildfire Games.
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
import json
import os
import re

from merge_rules import merge_rules


def is_descriptor_indexing_define(define):
    return define["name"] == "USE_DESCRIPTOR_INDEXING"


def generate_rules(logs):
    re_error = re.compile(
        r"ERROR:\s+Program \'spirv/(.*?)\' with required defines not found\.(.*?)"
        r"ERROR:\s+Failed to load shader 'spirv/(.*?)'",
        re.DOTALL | re.MULTILINE,
    )
    re_define = re.compile(r'ERROR:\s+"(.*?)": "(.*?)"')
    rules = {}
    for error in re_error.findall(logs):
        if error[0] != error[2]:
            raise ValueError("Unexpected error format.")
        program_name = error[0]
        if program_name not in rules:
            rules[program_name] = {"name": program_name, "combinations": []}
        combination = []
        for define in re_define.findall(error[1]):
            name = define[0]
            value = define[1]
            # We have to avoid building all debug modes because it will cost x4
            # of SPIR-V shaders size.
            if name == "RENDER_DEBUG_MODE":
                value = "RENDER_DEBUG_MODE_NONE"
            combination.append({"name": name, "value": value})
        rules[program_name]["combinations"].append(combination)
        if any(is_descriptor_indexing_define(define) for define in combination):
            paired_combination = list(
                filter(lambda define: not is_descriptor_indexing_define(define), combination)
            )
        else:
            paired_combination = [*combination, {"name": "USE_DESCRIPTOR_INDEXING", "value": "1"}]
        rules[program_name]["combinations"].append(paired_combination)
    # Raw logs might contain duplicated values.
    return merge_rules(rules, {})


def run():
    parser = argparse.ArgumentParser()
    parser.add_argument("output_rules_path", help="a path to output rules")
    parser.add_argument(
        "input_paths", help="a paths to input files with logs", nargs=argparse.REMAINDER
    )
    args = parser.parse_args()

    if not args.input_paths or any(
        not os.path.isfile(input_path) for input_path in args.input_paths
    ):
        raise ValueError("Invalid input files.")

    rules = {}
    for input_path in args.input_paths:
        with open(input_path) as handle:
            input_logs = handle.read()
        rules = merge_rules(rules, generate_rules(input_logs))

    with open(args.output_rules_path, "w") as handle:
        json.dump(rules, handle, sort_keys=True)


if __name__ == "__main__":
    run()
