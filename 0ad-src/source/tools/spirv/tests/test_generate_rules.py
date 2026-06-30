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

import unittest

from generate_rules import generate_rules


class TestMerge(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.TEST_RULES = {
            "canvas2d": {
                "name": "canvas2d",
                "combinations": [[], [{"name": "USE_DESCRIPTOR_INDEXING", "value": "1"}]],
            }
        }

    def make_rules(self, program_name, combinations):
        return {program_name: {"name": program_name, "combinations": combinations}}

    def test_empty(self):
        self.assertDictEqual(generate_rules(""), {})

    def test_empty_combinations(self):
        logs = """
            <p class="error">ERROR: Program 'spirv/canvas2d' with required defines not found.</p>
            <p class="error">ERROR: Failed to load shader 'spirv/canvas2d'</p>
        """
        self.assertDictEqual(generate_rules(logs), self.TEST_RULES)

        logs = """
            ERROR: Program 'spirv/canvas2d' with required defines not found.
            ERROR: Failed to load shader 'spirv/canvas2d'
        """
        self.assertDictEqual(generate_rules(logs), self.TEST_RULES)

    def test_combinations(self):
        logs = """
            <p class="error">ERROR: Program 'spirv/canvas2d' with required defines not found.</p>
            <p class="error">ERROR:   "USE_DESCRIPTOR_INDEXING": "1"</p>
            <p class="error">ERROR: Failed to load shader 'spirv/canvas2d'</p>
        """
        self.assertDictEqual(generate_rules(logs), self.TEST_RULES)

        logs = """
            <p class="error">ERROR: ERROR</p>
            <p class="error">ERROR: Program 'spirv/canvas2d' with required defines not found.</p>
            <p class="error">ERROR:   "USE_DESCRIPTOR_INDEXING": "1"</p>
            <p class="error">ERROR: Failed to load shader 'spirv/canvas2d'</p>
            <p class="error">ERROR: ERROR</p>
            <p class="error">ERROR: Program 'spirv/canvas2d' with required defines not found.</p>
            <p class="error">ERROR:   "USE_DESCRIPTOR_INDEXING": "1"</p>
            <p class="error">ERROR: Failed to load shader 'spirv/canvas2d'</p>
            <p class="error">ERROR: ERROR</p>
        """
        self.assertDictEqual(generate_rules(logs), self.TEST_RULES)

        logs = """
            ERROR: Program 'spirv/canvas2d' with required defines not found.
            ERROR:   "USE_DESCRIPTOR_INDEXING": "1"
            ERROR: Failed to load shader 'spirv/canvas2d'
        """
        self.assertDictEqual(generate_rules(logs), self.TEST_RULES)

    def test_incorrect_logs(self):
        logs = """
            <p class="error">ERROR:Program 'spirv/canvas2d' with required defines not found.</p>
            <p class="error">ERROR:"USE_DESCRIPTOR_INDEXING": "1"</p>
            <p class="error">ERROR:Failed to load shader 'spirv/canvas2d'</p>
        """
        self.assertDictEqual(generate_rules(logs), {})

    def test_different_eol(self):
        logs = (
            "ERROR:\tProgram 'spirv/canvas2d' with required defines not found.\r\n\r\n"
            "ERROR:\tFailed to load shader 'spirv/canvas2d'\r\n\r\n"
        )
        self.assertDictEqual(generate_rules(logs), self.TEST_RULES)

    def test_render_debug_mode(self):
        test_rules = {
            "canvas2d": {
                "name": "canvas2d",
                "combinations": [
                    [{"name": "RENDER_DEBUG_MODE", "value": "RENDER_DEBUG_MODE_NONE"}],
                    [
                        {"name": "RENDER_DEBUG_MODE", "value": "RENDER_DEBUG_MODE_NONE"},
                        {"name": "USE_DESCRIPTOR_INDEXING", "value": "1"},
                    ],
                ],
            }
        }

        logs = """
            <p class="error">ERROR: Program 'spirv/canvas2d' with required defines not found.</p>
            <p class="error">ERROR:   "RENDER_DEBUG_MODE": "RENDER_DEBUG_MODE_NONE"</p>
            <p class="error">ERROR: Failed to load shader 'spirv/canvas2d'</p>
        """
        self.assertDictEqual(generate_rules(logs), test_rules)

        logs = """
            <p class="error">ERROR: Program 'spirv/canvas2d' with required defines not found.</p>
            <p class="error">ERROR:   "RENDER_DEBUG_MODE": "RENDER_DEBUG_MODE_AO"</p>
            <p class="error">ERROR: Failed to load shader 'spirv/canvas2d'</p>
        """
        self.assertDictEqual(generate_rules(logs), test_rules)

        logs = """
            <p class="error">ERROR: Program 'spirv/canvas2d' with required defines not found.</p>
            <p class="error">ERROR:   "RENDER_DEBUG_MODE": "RENDER_DEBUG_MODE_AO"</p>
            <p class="error">ERROR: Failed to load shader 'spirv/canvas2d'</p>
        """
        self.assertDictEqual(generate_rules(logs), test_rules)

        logs = """
            <p class="error">ERROR: Program 'spirv/canvas2d' with required defines not found.</p>
            <p class="error">ERROR:   "RENDER_DEBUG_MODE": "RENDER_DEBUG_MODE_CUSTOM"</p>
            <p class="error">ERROR: Failed to load shader 'spirv/canvas2d'</p>
        """
        self.assertDictEqual(generate_rules(logs), test_rules)

        logs = """
            <p class="error">ERROR: Program 'spirv/canvas2d' with required defines not found.</p>
            <p class="error">ERROR:   "RENDER_DEBUG_MODE": "1"</p>
            <p class="error">ERROR: Failed to load shader 'spirv/canvas2d'</p>
        """
        self.assertDictEqual(generate_rules(logs), test_rules)
