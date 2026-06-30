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

import unittest

from modify_rules import add_define, remove_define


class TestModify(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.TEST_RULES = {
            "canvas2d": {
                "name": "canvas2d",
                "combinations": [[], [{"name": "USE_DESCRIPTOR_INDEXING", "value": "1"}]],
            }
        }

    def make_rules(self, combinations, program_name="A"):
        return {program_name: {"name": program_name, "combinations": combinations}}

    def test_empty(self):
        self.assertDictEqual(add_define({}, "D=1"), {})
        self.assertDictEqual(
            add_define(self.TEST_RULES, "USE_DESCRIPTOR_INDEXING=1", False),
            self.make_rules([[{"name": "USE_DESCRIPTOR_INDEXING", "value": "1"}]], "canvas2d"),
        )
        self.assertDictEqual(
            add_define(self.TEST_RULES, "USE_DESCRIPTOR_INDEXING=1", True), self.TEST_RULES
        )

        self.assertDictEqual(remove_define({}, "D"), {})
        self.assertDictEqual(remove_define(self.make_rules([]), "D"), self.make_rules([]))
        self.assertDictEqual(remove_define(self.TEST_RULES, "D", False), self.TEST_RULES)
        self.assertDictEqual(remove_define(self.TEST_RULES, "D", True), self.TEST_RULES)

    def test_order(self):
        # We need to guarantee an order for reproducible builds.

        cmb_a = {"name": "A", "value": "1"}
        cmb_a2 = {"name": "A", "value": "2"}
        cmb_a3 = {"name": "A", "value": "3"}

        self.assertDictEqual(
            add_define(self.make_rules([[]]), "A=1", False), self.make_rules([[cmb_a]])
        )
        self.assertDictEqual(
            add_define(self.make_rules([[]]), "A=1", True), self.make_rules([[], [cmb_a]])
        )

        self.assertDictEqual(
            add_define(self.make_rules([[], [cmb_a], [cmb_a2]]), "A=2", False),
            self.make_rules([[cmb_a, cmb_a2], [cmb_a2]]),
        )
        self.assertDictEqual(
            add_define(self.make_rules([[], [cmb_a], [cmb_a2]]), "A=2", True),
            self.make_rules([[], [cmb_a], [cmb_a, cmb_a2], [cmb_a2]]),
        )

        self.assertDictEqual(
            remove_define(self.make_rules([[cmb_a]]), "D"), self.make_rules([[cmb_a]])
        )
        self.assertDictEqual(
            remove_define(self.make_rules([[cmb_a]]), "A", False), self.make_rules([])
        )
        self.assertDictEqual(
            remove_define(self.make_rules([[cmb_a]]), "A", True), self.make_rules([[]])
        )
        self.assertDictEqual(
            remove_define(self.make_rules([[cmb_a]]), "A=1", False), self.make_rules([])
        )
        self.assertDictEqual(
            remove_define(self.make_rules([[cmb_a]]), "A=1", True), self.make_rules([[]])
        )
        self.assertDictEqual(
            remove_define(self.make_rules([[cmb_a]]), "A=2"), self.make_rules([[cmb_a]])
        )

        self.assertDictEqual(
            remove_define(self.make_rules([[cmb_a, cmb_a2, cmb_a3]]), "A", True),
            self.make_rules([[]]),
        )
        self.assertDictEqual(
            remove_define(self.make_rules([[cmb_a, cmb_a2, cmb_a3], [cmb_a]]), "A", True),
            self.make_rules([[]]),
        )

        self.assertDictEqual(
            remove_define(self.make_rules([[cmb_a], [cmb_a2], [cmb_a3]]), "A=2"),
            self.make_rules([[cmb_a], [cmb_a3]]),
        )
        self.assertDictEqual(
            remove_define(self.make_rules([[cmb_a, cmb_a2, cmb_a3]]), "A=2"), self.make_rules([])
        )
        self.assertDictEqual(
            remove_define(self.make_rules([[cmb_a, cmb_a2, cmb_a3]]), "A=2", True),
            self.make_rules([[cmb_a, cmb_a3]]),
        )
        self.assertDictEqual(
            remove_define(
                self.make_rules([[cmb_a, cmb_a2, cmb_a3], [cmb_a, cmb_a3]]), "A=2", False
            ),
            self.make_rules([[cmb_a, cmb_a3]]),
        )
        self.assertDictEqual(
            remove_define(
                self.make_rules([[cmb_a, cmb_a2, cmb_a3], [cmb_a, cmb_a3]]), "A=2", True
            ),
            self.make_rules([[cmb_a, cmb_a3]]),
        )
