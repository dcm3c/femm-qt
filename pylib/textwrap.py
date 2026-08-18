# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
#
# textwrap -- written here, because micropython-lib's cannot be used.
#
# It fails at import for TWO independent reasons, either of which is fatal:
#
#   * its wordsep_re uses lookahead and lookbehind -- `(?=...)`, `(?<=...)` --
#     and this interpreter's regex engine (re1.5) rejects those outright with
#     "regex too complex". That is a construct it does not implement, not a
#     size limit that could be raised;
#   * its _whitespace_only_re passes re.MULTILINE, and this `re` has no flags
#     at all -- no MULTILINE, DOTALL, IGNORECASE or VERBOSE.
#
# Both are in class bodies and module scope, so they run at import and no
# amount of argument-passing avoids them. Hence a fresh implementation, which
# also lets it use no regex whatsoever.
#
# Covers wrap, fill, dedent, indent, shorten. NOT hyphen-breaking: CPython
# splits "long-winded" across lines and this does not, which is the feature
# that needed the lookahead in the first place. Everything else matches
# CPython for ordinary prose, which is what testing/PylibTest.py pins.

__all__ = ["wrap", "fill", "dedent", "indent", "shorten", "TextWrapper"]


class TextWrapper:
    """The class form, for callers that set options once and reuse them."""

    def __init__(self, width=70, initial_indent="", subsequent_indent="",
                 expand_tabs=True, replace_whitespace=True,
                 drop_whitespace=True, break_long_words=True,
                 tabsize=8, max_lines=None, placeholder=" [...]"):
        self.width = width
        self.initial_indent = initial_indent
        self.subsequent_indent = subsequent_indent
        self.expand_tabs = expand_tabs
        self.replace_whitespace = replace_whitespace
        self.drop_whitespace = drop_whitespace
        self.break_long_words = break_long_words
        self.tabsize = tabsize
        self.max_lines = max_lines
        self.placeholder = placeholder

    def _munge(self, text):
        if self.expand_tabs:
            text = text.expandtabs(self.tabsize) if hasattr(text, "expandtabs") \
                else _expandtabs(text, self.tabsize)
        if self.replace_whitespace:
            out = []
            for ch in text:
                out.append(" " if ch in "\t\n\v\f\r" else ch)
            text = "".join(out)
        return text

    def wrap(self, text):
        text = self._munge(text)
        words = [w for w in text.split(" ") if w != ""] if self.drop_whitespace \
            else text.split(" ")
        lines = []
        cur = []            # words on the line being built
        cur_len = 0         # their length, including the joining spaces
        indent = self.initial_indent

        def flush():
            nonlocal cur, cur_len, indent
            lines.append(indent + " ".join(cur))
            indent = self.subsequent_indent
            cur, cur_len = [], 0

        for word in words:
            sep = 1 if cur else 0
            room = self.width - len(indent) - cur_len - sep
            if len(word) <= room:
                cur.append(word)
                cur_len += sep + len(word)
                continue

            # It does not fit. A word longer than a whole line gets broken --
            # and CPython fills the CURRENT line first rather than starting a
            # fresh one, which is the part that is easy to get wrong.
            if self.break_long_words and len(word) + len(indent) > self.width:
                if room > 0:
                    cur.append(word[:room])
                    cur_len += sep + room
                    word = word[room:]
                if cur:
                    flush()
                while len(word) + len(indent) > self.width:
                    take = self.width - len(indent)
                    if take <= 0:
                        break           # the indent alone fills the width
                    lines.append(indent + word[:take])
                    indent = self.subsequent_indent
                    word = word[take:]
                if word:
                    cur, cur_len = [word], len(word)
            else:
                if cur:
                    flush()
                cur, cur_len = [word], len(word)

        if cur:
            lines.append(indent + " ".join(cur))

        if self.max_lines is not None and len(lines) > self.max_lines:
            lines = lines[:self.max_lines]
            if lines:
                last = lines[-1]
                if len(last) + len(self.placeholder) > self.width:
                    last = last[:self.width - len(self.placeholder)]
                    while last and last.endswith(" "):
                        last = last[:-1]
                lines[-1] = last + self.placeholder
        return lines

    def fill(self, text):
        return "\n".join(self.wrap(text))


def _expandtabs(text, tabsize):
    out = []
    col = 0
    for ch in text:
        if ch == "\t":
            n = tabsize - (col % tabsize)
            out.append(" " * n)
            col += n
        else:
            out.append(ch)
            col = 0 if ch == "\n" else col + 1
    return "".join(out)


def wrap(text, width=70, **kwargs):
    return TextWrapper(width=width, **kwargs).wrap(text)


def fill(text, width=70, **kwargs):
    return TextWrapper(width=width, **kwargs).fill(text)


def shorten(text, width, **kwargs):
    """Collapse whitespace and truncate to one line with a placeholder."""
    kwargs["max_lines"] = 1
    collapsed = " ".join([w for w in text.split() if w])
    return fill(collapsed, width=width, **kwargs)


def dedent(text):
    """Remove the longest common leading whitespace from every line.

    Lines that are entirely whitespace are ignored when choosing the prefix
    and are normalised to empty, as in CPython.
    """
    lines = text.split("\n")
    prefix = None
    for line in lines:
        stripped = line.strip()
        if not stripped:
            continue                    # all-whitespace lines do not count
        indent = line[:len(line) - len(line.lstrip())]
        if prefix is None:
            prefix = indent
        else:
            # Longest common prefix of the two indents.
            i = 0
            while i < len(prefix) and i < len(indent) and prefix[i] == indent[i]:
                i += 1
            prefix = prefix[:i]
        if prefix == "":
            break
    if not prefix:
        # Still normalise whitespace-only lines, which is what CPython does.
        return "\n".join([("" if not l.strip() else l) for l in lines])
    out = []
    for line in lines:
        if not line.strip():
            out.append("")
        elif line.startswith(prefix):
            out.append(line[len(prefix):])
        else:
            out.append(line)
    return "\n".join(out)


def indent(text, prefix, predicate=None):
    """Prefix every line for which `predicate` is true (default: non-blank)."""
    if predicate is None:
        def predicate(line):
            return line.strip() != ""
    out = []
    for line in text.splitlines(True) if hasattr(text, "splitlines") \
            else _splitlines_keepends(text):
        out.append(prefix + line if predicate(line) else line)
    return "".join(out)


def _splitlines_keepends(s):
    out, cur = [], []
    for ch in s:
        cur.append(ch)
        if ch == "\n":
            out.append("".join(cur))
            cur = []
    if cur:
        out.append("".join(cur))
    return out
