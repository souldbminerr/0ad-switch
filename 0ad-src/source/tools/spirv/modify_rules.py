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
import json
import os

from merge_rules import merge_rules


def add_define(input_rules, define_to_remove, merge_input=False):
    define = define_to_remove.split("=")
    define_name = define[0]
    define_value = define[1]
    modified_rules = {}
    for program_name in input_rules:
        modified_combinations = []
        for combination in input_rules[program_name]["combinations"]:
            matched = False
            for define in combination:
                if define["name"] == define_name and define["value"] == define_value:
                    matched = True
            if matched:
                modified_combinations.append(combination)
            else:
                modified_combinations.append(
                    [*combination, {"name": define_name, "value": define_value}]
                )
        modified_rules[program_name] = {
            "name": program_name,
            "combinations": modified_combinations,
        }
    if merge_input:
        return merge_rules(input_rules, modified_rules)
    return merge_rules(modified_rules, {})


def remove_define(input_rules, define_to_remove, merge_removed=False):
    define = define_to_remove.split("=")
    define_name = define[0]
    define_value = define[1] if len(define) > 1 else None
    rules = {}
    modified_rules = {}
    for program_name in input_rules:
        combinations = []
        modified_combinations = []
        for combination in input_rules[program_name]["combinations"]:
            matched = False
            modified_combination = []
            for define in combination:
                if define["name"] == define_name and (
                    define_value is None or define["value"] == define_value
                ):
                    matched = True
                else:
                    modified_combination.append(define)
            if matched:
                modified_combinations.append(modified_combination)
            else:
                combinations.append(combination)
        rules[program_name] = {
            "name": program_name,
            "combinations": combinations,
        }
        modified_rules[program_name] = {
            "name": program_name,
            "combinations": modified_combinations,
        }
    if merge_removed:
        return merge_rules(rules, modified_rules)
    return merge_rules(rules, {})


def run():
    parser = argparse.ArgumentParser()
    parser.add_argument("output_rules_path", help="a path to output modified rules")
    parser.add_argument("input_rules_path", help="a path to input rules")
    parser.add_argument("-a", "--add-define", help="adds the define to combinations")
    parser.add_argument("-r", "--remove-define", help="removes combinations with the define")
    parser.add_argument(
        "-m",
        "--merge-modified",
        help="merge modified rules to the input ones",
        action="store_true",
    )
    args = parser.parse_args()

    if not args.input_rules_path or (not os.path.isfile(args.input_rules_path)):
        raise ValueError("Invalid input file.")

    with open(args.input_rules_path) as handle:
        input_rules = json.load(handle)

    if args.add_define:
        rules = add_define(input_rules, args.remove_define, args.merge_modified)
    elif args.remove_define:
        rules = remove_define(input_rules, args.remove_define, args.merge_modified)

    with open(args.output_rules_path, "w") as handle:
        json.dump(rules, handle, sort_keys=True)


if __name__ == "__main__":
    run()
