# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
#
# shutil -- micropython-lib's, with the parts a desktop actually needs.
#
# `_core.py` is micropython-lib's shutil, copied VERBATIM (see
# pylib/README.md). It is written for a bare-metal `os` and does not fit this
# one: `rmtree` calls os.ilistdir and `disk_usage` calls os.statvfs, neither of
# which femm-qt's os module has (it offers listdir/stat -- see py_os.cpp). And
# the calls a script really wants -- copyfile, copy, move -- are not there at
# all; only copyfileobj is.
#
# So rmtree is REPLACED here and the file operations are added. Only
# copyfileobj is inherited unchanged. disk_usage is deliberately left as
# upstream wrote it: it will raise for want of os.statvfs, which is honest,
# and inventing a Windows answer for it is a different job.
from ._core import copyfileobj, disk_usage        # noqa: F401

import os

__all__ = ["copyfileobj", "copyfile", "copy", "copy2", "move", "rmtree",
           "copytree", "disk_usage"]


def _isdir(path):
    return os.path.isdir(path)


def rmtree(path):
    """Delete a directory and everything under it.

    Replaces upstream's, which walks with os.ilistdir. This one uses
    listdir + isdir, which is what femm-qt's os provides.
    """
    if not path:
        raise ValueError("refusing to remove an empty path")
    for name in os.listdir(path):
        full = path + "/" + name
        if _isdir(full):
            rmtree(full)
        else:
            os.remove(full)
    os.rmdir(path)


def copyfile(src, dst):
    """Copy a file's contents. Binary, and in chunks, so a big .ans does not
    have to fit in memory twice."""
    if os.path.isdir(dst):
        raise IsADirectoryError(dst) if hasattr(__builtins__, "IsADirectoryError") \
            else OSError("destination is a directory: " + dst)
    fsrc = open(src, "rb")
    try:
        fdst = open(dst, "wb")
        try:
            while True:
                buf = fsrc.read(65536)
                if not buf:
                    break
                fdst.write(buf)
        finally:
            fdst.close()
    finally:
        fsrc.close()
    return dst


def copy(src, dst):
    """copyfile, but a directory destination keeps the source's name --
    which is the form `copy(result, outdir)` a batch script wants."""
    if os.path.isdir(dst):
        dst = dst + "/" + os.path.basename(src)
    return copyfile(src, dst)


# No metadata is carried on this platform, so copy2 is copy. Named anyway,
# because scripts written against CPython ask for it.
copy2 = copy


def copytree(src, dst):
    """Recursively copy a directory. Creates dst; does not merge into it."""
    os.makedirs(dst)
    for name in os.listdir(src):
        s, d = src + "/" + name, dst + "/" + name
        if _isdir(s):
            copytree(s, d)
        else:
            copyfile(s, d)
    return dst


def move(src, dst):
    """Rename where possible, copy-and-delete where not.

    os.rename cannot cross a volume, and a results directory on another drive
    is an ordinary thing to want, so the fallback is not an edge case.
    """
    if os.path.isdir(dst):
        dst = dst + "/" + os.path.basename(src)
    try:
        os.rename(src, dst)
        return dst
    except OSError:
        pass
    if _isdir(src):
        copytree(src, dst)
        rmtree(src)
    else:
        copyfile(src, dst)
        os.remove(src)
    return dst
