# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
#
# glob -- filename pattern matching against the filesystem.
#
# Written here rather than vendored: micropython-lib's only glob lives under
# unix-ffi and calls libc through FFI, which this interpreter has no way to do.
# CPython's is pure Python but leans on os.scandir, fnmatch internals and
# re features that are not all present, so this is a fresh implementation over
# what femm-qt does have: os.listdir, os.path, and the vendored fnmatch.
#
# It exists because the signature FEMM batch job is "every .fem in this
# folder", and the alternative spelling is three lines of os.listdir and
# splitext that everyone writes slightly differently:
#
#     for path in glob.glob("models/*.fem"):
#         open(path)
#         mi_analyze()
#
# Supports *, ?, [seq] and [!seq] in any component, plus the recursive **
# form when recursive=True. Matches CPython's rules, including the two that
# surprise people: a leading dot is not matched by a wildcard, and the results
# come back in whatever order the filesystem gives (sort them if you care).

import os

from fnmatch import fnmatch

__all__ = ["glob", "iglob", "escape", "has_magic"]

_MAGIC = "*?["


def has_magic(s):
    for c in _MAGIC:
        if c in s:
            return True
    return False


def escape(pathname):
    """Quote every wildcard character, so a literal name can be globbed."""
    drive, path = _splitdrive(pathname)
    out = []
    for c in path:
        if c in _MAGIC:
            out.append("[" + c + "]")
        else:
            out.append(c)
    return drive + "".join(out)


def _splitdrive(p):
    # os.path here has no splitdrive, and a Windows path certainly has one.
    if len(p) >= 2 and p[1] == ":":
        return p[:2], p[2:]
    return "", p


def _split(path):
    """Path to (drive, is_absolute, [components]), separator-agnostic.

    Both separators are accepted on every platform, matching the rest of
    femm-qt: a script written with forward slashes has to work on Windows,
    since that is what nearly every FEMM script uses.
    """
    drive, rest = _splitdrive(path)
    rest = rest.replace("\\", "/")
    absolute = rest.startswith("/")
    return drive, absolute, [c for c in rest.split("/") if c]


def _listdir(path):
    try:
        return os.listdir(path if path else ".")
    except OSError:
        return []                    # unreadable or missing: no matches, not an error


def _isdir(path):
    return os.path.isdir(path if path else ".")


def _join(base, name):
    if not base:
        return name
    if base.endswith("/") or base.endswith("\\") or base.endswith(":"):
        return base + name
    return base + "/" + name


def _match_component(base, pattern, dirs_only):
    """Every entry of `base` matching one pattern component."""
    if not has_magic(pattern):
        # A literal component needs no listing -- and must still work for a
        # name the directory listing would not show.
        cand = _join(base, pattern)
        if (os.path.exists(cand) or os.path.islink(cand)) and \
                (not dirs_only or _isdir(cand)):
            return [cand]
        return []
    out = []
    for name in _listdir(base):
        if name.startswith(".") and not pattern.startswith("."):
            continue             # a wildcard does not match a leading dot
        if fnmatch(name, pattern):
            cand = _join(base, name)
            if not dirs_only or _isdir(cand):
                out.append(cand)
    return out


def _walk_dirs(base):
    """base, then every directory beneath it -- what ** expands to."""
    yield base
    for name in _listdir(base):
        if name.startswith("."):
            continue
        cand = _join(base, name)
        if _isdir(cand):
            for sub in _walk_dirs(cand):
                yield sub


def iglob(pathname, recursive=False):
    """glob(), as an iterator."""
    drive, absolute, parts = _split(pathname)
    if not parts:
        return iter([])
    base = drive + ("/" if absolute else "")
    return iter(_expand(base, parts, recursive))


def _expand(base, parts, recursive):
    pattern = parts[0]
    rest = parts[1:]
    last = not rest

    if pattern == "**" and recursive:
        # ** matches this directory and every one below it, so the remaining
        # pattern is tried against each in turn.
        out = []
        for d in _walk_dirs(base):
            if last:
                out.append(d)
            else:
                out.extend(_expand(d, rest, recursive))
        return out

    matches = _match_component(base, pattern, dirs_only=not last)
    if last:
        return matches
    out = []
    for m in matches:
        out.extend(_expand(m, rest, recursive))
    return out


def glob(pathname, recursive=False):
    """Every path matching `pathname`, as a list.

    Unsorted, as in CPython -- the order is the filesystem's. Sort it if the
    order matters to your results:  sorted(glob.glob("*.fem")).
    """
    return list(iglob(pathname, recursive))
