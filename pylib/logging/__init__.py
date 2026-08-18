# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
#
# logging -- micropython-lib's, with the default formatter CPython has.
#
# `_core.py` is micropython-lib's logging, copied VERBATIM (see
# pylib/README.md). One gap makes it fail on its most obvious call: Handler
# sets `self.formatter = None` and `Handler.format` then does
# `self.formatter.format(record)`, so ANY handler added without an explicit
# setFormatter() raises
#
#     AttributeError: 'NoneType' object has no attribute 'format'
#
# CPython falls back to a default Formatter instead, which is why
#
#     log.addHandler(logging.FileHandler("run.log"))
#
# -- three words of ordinary Python -- works there and crashed here. Fixed from
# the outside by rebinding Handler.format; _core.py is untouched.
#
# Note also that logging writes to sys.stderr, which did not exist in this
# interpreter until 2026-08-17 (MICROPY_PY_SYS_STDFILES). That is why this
# module could not be vendored earlier at all.
from . import _core
from ._core import *                      # noqa: F401,F403

# One shared instance: Formatter is stateless apart from its format strings,
# and CPython likewise keeps a single default.
_default_formatter = _core.Formatter()


def _format(self, record):
    return (self.formatter or _default_formatter).format(record)


_core.Handler.format = _format
