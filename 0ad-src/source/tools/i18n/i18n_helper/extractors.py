# Copyright (C) 2024 Wildfire Games.
# All rights reserved.
#
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# * Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the distribution.
# * The name of the author may not be used to endorse or promote products
#   derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR “AS IS” AND ANY EXPRESS OR IMPLIED
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
# EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import json
import os
import re
import sys
from functools import lru_cache
from textwrap import dedent

from babel.messages.jslexer import tokenize, unquote_string
from lxml import etree


@lru_cache
def get_mask_pattern(mask: str) -> re.Pattern:
    """Build a regex pattern for matching file paths."""
    parts = re.split(r"([*][*]?)", mask)
    pattern = ""
    for i, part in enumerate(parts):
        if i % 2 != 0:
            pattern += "[^/]+"
            if len(part) == 2:
                pattern += "(/[^/]+)*"
        else:
            pattern += re.escape(part)
    pattern += "$"
    return re.compile(pattern)


def pathmatch(mask, path):
    """Match paths to a mask, where the mask supports * and **.

    Paths use / as the separator
    * matches a sequence of characters without /.
    ** matches a sequence of characters without / followed by a / and
    sequence of characters without /
    :return: true if path matches the mask, false otherwise
    """
    return get_mask_pattern(mask).match(path) is not None


class Extractor:
    def __init__(self, directory_path, filemasks, options):
        self.directory_path = directory_path
        self.options = options

        if isinstance(filemasks, dict):
            self.include_masks = filemasks["includeMasks"]
            self.exclude_masks = filemasks["excludeMasks"]
        else:
            self.include_masks = filemasks
            self.exclude_masks = []

    def run(self):
        """Extract messages.

        :return:    An iterator over ``(message, plural, context, (location, pos), comment)``
                    tuples.
        :rtype:     ``iterator``
        """
        empty_string_pattern = re.compile(r"^\s*$")
        directory_absolute_path = os.path.abspath(self.directory_path)
        for root, folders, filenames in os.walk(directory_absolute_path):
            for subdir in folders:
                if subdir.startswith((".", "_")):
                    folders.remove(subdir)
            folders.sort()
            filenames.sort()
            for filename in filenames:
                filename = os.path.relpath(
                    os.path.join(root, filename), self.directory_path
                ).replace(os.sep, "/")
                for filemask in self.exclude_masks:
                    if pathmatch(filemask, filename):
                        break
                else:
                    for filemask in self.include_masks:
                        if not pathmatch(filemask, filename):
                            continue

                        filepath = os.path.join(directory_absolute_path, filename)
                        for message, plural, context, position, comments in self.extract_from_file(
                            filepath
                        ):
                            if empty_string_pattern.match(message):
                                continue

                            if " " in filename or "\t" in filename:
                                filename = "\u2068" + filename + "\u2069"
                            yield message, plural, context, (filename, position), comments

    def extract_from_file(self, filepath):
        """Extract messages from a specific file.

        :return:    An iterator over ``(message, plural, context, position, comments)`` tuples.
        :rtype:     ``iterator``
        """


class JavascriptExtractor(Extractor):
    """Extract messages from JavaScript source code."""

    def extract_javascript_from_file(self, file_object):
        funcname = message_lineno = None
        messages = []
        last_argument = None
        translator_comments = []
        concatenate_next = False
        last_token = None
        call_stack = -1
        comment_tags = self.options.get("commentTags", [])
        keywords = self.options.get("keywords", {}).keys()

        for token in tokenize(file_object.read(), dotted=False):
            if token.type == "operator" and (
                token.value == "(" or (call_stack != -1 and (token.value in ("[", "{")))
            ):
                if funcname:
                    message_lineno = token.lineno
                    call_stack += 1

            elif call_stack == -1 and token.type == "linecomment":
                value = token.value[2:].strip()
                if translator_comments and translator_comments[-1][0] == token.lineno - 1:
                    translator_comments.append((token.lineno, value))
                    continue

                for comment_tag in comment_tags:
                    if value.startswith(comment_tag):
                        translator_comments.append((token.lineno, value.strip()))
                        break

            elif token.type == "multilinecomment":
                # only one multi-line comment may preceed a translation
                translator_comments = []
                value = token.value[2:-2].strip()
                for comment_tag in comment_tags:
                    if value.startswith(comment_tag):
                        lines = value.splitlines()
                        if lines:
                            lines[0] = lines[0].strip()
                            lines[1:] = dedent("\n".join(lines[1:])).splitlines()
                            for offset, line in enumerate(lines):
                                translator_comments.append((token.lineno + offset, line))
                        break

            elif funcname and call_stack == 0:
                if token.type == "operator" and token.value == ")":
                    if last_argument is not None:
                        messages.append(last_argument)
                    if len(messages) > 1:
                        messages = tuple(messages)
                    elif messages:
                        messages = messages[0]
                    else:
                        messages = None

                    # Comments don't apply unless they immediately precede the
                    # message
                    if translator_comments and translator_comments[-1][0] < message_lineno - 1:
                        translator_comments = []

                    if messages is not None:
                        yield (
                            message_lineno,
                            funcname,
                            messages,
                            [comment[1] for comment in translator_comments],
                        )

                    funcname = message_lineno = last_argument = None
                    concatenate_next = False
                    translator_comments = []
                    messages = []
                    call_stack = -1

                elif token.type == "string":
                    new_value = unquote_string(token.value)
                    if concatenate_next:
                        last_argument = (last_argument or "") + new_value
                        concatenate_next = False
                    else:
                        last_argument = new_value

                elif token.type == "operator":
                    if token.value == ",":
                        if last_argument is not None:
                            messages.append(last_argument)
                            last_argument = None
                        else:
                            messages.append(None)
                        concatenate_next = False
                    elif token.value == "+":
                        concatenate_next = True

            elif call_stack > 0 and token.type == "operator" and (token.value in (")", "]", "}")):
                call_stack -= 1

            elif funcname and call_stack == -1:
                funcname = None

            elif (
                call_stack == -1
                and token.type == "name"
                and token.value in keywords
                and (
                    last_token is None
                    or last_token.type != "name"
                    or last_token.value != "function"
                )
            ):
                funcname = token.value

            last_token = token

    def extract_from_file(self, filepath):
        with open(filepath, encoding="utf-8-sig") as file_object:
            for lineno, funcname, messages, comments in self.extract_javascript_from_file(
                file_object
            ):
                spec = self.options.get("keywords", {})[funcname] or (1,) if funcname else (1,)
                if not isinstance(messages, (list, tuple)):
                    messages = [messages]
                if not messages:
                    continue

                # Validate the messages against the keyword's specification
                context = None
                msgs = []
                invalid = False
                # last_index is 1 based like the keyword spec
                last_index = len(messages)
                for index in spec:
                    if isinstance(index, (list, tuple)):
                        context = messages[index[0] - 1]
                        continue
                    if last_index < index:
                        # Not enough arguments
                        invalid = True
                        break
                    message = messages[index - 1]
                    if message is None:
                        invalid = True
                        break
                    msgs.append(message)
                if invalid:
                    continue

                # keyword spec indexes are 1 based, therefore '-1'
                if isinstance(spec[0], (tuple, list)):
                    # context-aware *gettext method
                    first_msg_index = spec[1] - 1
                else:
                    first_msg_index = spec[0] - 1
                if not messages[first_msg_index]:
                    # An empty string msgid isn't valid, emit a warning
                    fname = (hasattr(file_object, "name") and file_object.name) or "(unknown)"
                    print(
                        f"{fname}:{lineno}: warning: Empty msgid.  It is reserved by GNU gettext: "
                        'gettext("") returns the header entry with meta information, '
                        "not the empty string.",
                        file=sys.stderr,
                    )
                    continue

                messages = tuple(msgs)
                message = messages[0]
                plural = None
                if len(messages) == 2:
                    plural = messages[1]

                yield message, plural, context, lineno, comments


class CppExtractor(JavascriptExtractor):
    """Extract messages from C++ source code."""


class TxtExtractor(Extractor):
    """Extract messages from plain text files."""

    def extract_from_file(self, filepath):
        with open(filepath, encoding="utf-8-sig") as file_object:
            for lineno, line in enumerate([line.strip("\n\r") for line in file_object], start=1):
                if line:
                    yield line, None, None, lineno, []


class JsonExtractor(Extractor):
    """Extract messages from JSON files."""

    def __init__(self, directory_path=None, filemasks=None, options=None):
        if options is None:
            options = {}
        if filemasks is None:
            filemasks = []
        super().__init__(directory_path, filemasks, options)
        self.keywords = self.options.get("keywords", {})
        self.context = self.options.get("context", None)
        self.comments = self.options.get("comments", [])

    def set_options(self, options):
        self.options = options
        self.keywords = self.options.get("keywords", {})
        self.context = self.options.get("context", None)
        self.comments = self.options.get("comments", [])

    def extract_from_file(self, filepath):
        with open(filepath, encoding="utf-8") as file_object:
            for message, context in self.extract_from_string(file_object.read()):
                yield message, None, context, None, self.comments

    def extract_from_string(self, string):
        json_document = json.loads(string)
        yield from self.parse(json_document)

    def parse(self, data, key=None):
        """Recursively parse JSON data and extract strings."""
        if isinstance(data, list):
            for item in data:
                yield from self.parse(item)
        elif isinstance(data, dict):
            for key2, value in data.items():
                if key2 in self.keywords:
                    if isinstance(value, str):
                        yield self.extract_string(value, key2)
                    elif isinstance(value, list):
                        yield from self.extract_list(value, key2)
                    elif isinstance(value, dict):
                        if self.keywords[key2].get("extractFromInnerKeys"):
                            for value2 in value.values():
                                yield from self.parse(value2, key2)
                        else:
                            yield from self.extract_dictionary(value, key2)
                else:
                    yield from self.parse(value, key2)
        elif isinstance(data, str) and key in self.keywords:
            yield self.extract_string(data, key)

    def extract_string(self, string, keyword):
        if "tagAsContext" in self.keywords[keyword]:
            context = keyword
        elif "customContext" in self.keywords[keyword]:
            context = self.keywords[keyword]["customContext"]
        else:
            context = self.context
        return string, context

    def extract_list(self, items_list, keyword):
        for list_item in items_list:
            if isinstance(list_item, str):
                yield self.extract_string(list_item, keyword)
            elif isinstance(list_item, dict):
                extract = self.extract_dictionary(list_item[keyword], keyword)
                if extract:
                    yield extract

    def extract_dictionary(self, dictionary, keyword):
        message = dictionary.get("_string", None)
        if message and isinstance(message, str):
            if "context" in dictionary:
                context = str(dictionary["context"])
            elif "tagAsContext" in self.keywords[keyword]:
                context = keyword
            elif "customContext" in self.keywords[keyword]:
                context = self.keywords[keyword]["customContext"]
            else:
                context = self.context
            yield message, context


class XmlExtractor(Extractor):
    """Extract messages from XML files."""

    def __init__(self, directory_path, filemasks, options):
        super().__init__(directory_path, filemasks, options)
        self.keywords = self.options.get("keywords", {})
        self.json_extractor = None

    def get_json_extractor(self):
        if not self.json_extractor:
            self.json_extractor = JsonExtractor(self.directory_path)
        return self.json_extractor

    def extract_from_file(self, filepath):
        with open(filepath, encoding="utf-8-sig") as file_object:
            xml_document = etree.parse(file_object)

        for element in xml_document.iter(*self.keywords.keys()):
            keyword = element.tag

            lineno = element.sourceline
            if element.text is None:
                continue

            comments = []
            if "extractJson" in self.keywords[keyword]:
                json_extractor = self.get_json_extractor()
                json_extractor.set_options(self.keywords[keyword]["extractJson"])
                for message, context in json_extractor.extract_from_string(element.text):
                    yield message, None, context, lineno, comments
            else:
                context = None
                if "context" in element.attrib:
                    context = str(element.get("context"))
                elif "tagAsContext" in self.keywords[keyword]:
                    context = keyword
                elif "customContext" in self.keywords[keyword]:
                    context = self.keywords[keyword]["customContext"]
                if "comment" in element.attrib:
                    comment = element.get("comment")
                    comment = " ".join(
                        comment.split()
                    )  # Remove tabs, line breaks and unnecessary spaces.
                    comments.append(comment)
                if "splitOnWhitespace" in self.keywords[keyword]:
                    for split_text in element.text.split():
                        # split on whitespace is used for token lists, there, a
                        # leading '-' means the token has to be removed, so it's not
                        # to be processed here either
                        if split_text[0] != "-":
                            yield str(split_text), None, context, lineno, comments
                else:
                    yield str(element.text), None, context, lineno, comments
