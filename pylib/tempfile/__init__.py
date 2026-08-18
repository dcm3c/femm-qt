# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
#
# tempfile -- micropython-lib's, pointed at a temp directory that exists.
#
# `_core.py` is micropython-lib's tempfile, copied VERBATIM (see
# pylib/README.md). It defaults to "/tmp", which is right for a POSIX board and
# wrong for Windows: there is no /tmp, and creating one at the root of C: is
# not a reasonable thing for a FEMM script to do. It also relies on os.mkdir
# raising an OSError whose errno is EEXIST, and femm-qt's os raises a message
# without an errno, so the "directory already there" case escaped as an error.
#
# Both are fixed by choosing the directory before upstream's code runs, which
# leaves its mkdtemp/mkstemp logic untouched.
import os

from . import _core
from ._core import *                      # noqa: F401,F403


def _default_dir():
    """The platform's temp directory, or the working directory if it has none.

    TMPDIR is POSIX, TEMP/TMP are Windows; checking all three costs nothing and
    means one code path. Falls back to "." rather than inventing /tmp, because
    a directory that already exists is better than one we would have to create.
    """
    for var in ("TMPDIR", "TEMP", "TMP"):
        v = os.getenv(var)
        if v and os.path.isdir(v):
            return v.replace("\\", "/")
    if os.path.isdir("/tmp"):
        return "/tmp"
    return "."


def gettempdir():
    return _default_dir()


# Upstream's _sanitize_inputs is what turns `dir=None` into "/tmp"; replacing
# it redirects mkdtemp and mkstemp together, without touching either.
def _sanitize_inputs(suffix, prefix, dir):
    if dir is None:
        dir = _default_dir()
    if suffix is None:
        suffix = ""
    if prefix is None:
        prefix = ""
    return suffix, prefix, dir


_core._sanitize_inputs = _sanitize_inputs
