# bdwgc, as vendored into femm-qt

The Boehm-Demers-Weiser conservative garbage collector. It is the heap behind
MicroPython here: `py/gc.c` is not compiled at all, and `micropython/femm_gc.c`
implements the same `py/gc.h` interface on top of this library. The reasoning
and the measurements are in `docs/bdwgc-handoff-plan.md`.

## Version

| | |
|---|---|
| Release | 8.2.12 |
| Tarball | `gc-8.2.12.tar.gz` |
| sha256 | `42e5194ad06ab6ffb806c83eb99c03462b495d979cda782f3c72c08af833cd4e` |
| From | https://github.com/bdwgc/bdwgc/releases/download/v8.2.12/gc-8.2.12.tar.gz |

The project moved from `ivmai/bdwgc` to `bdwgc/bdwgc`; the old URL redirects.

## Nothing here is modified

Every file is byte-identical to the tarball. `LICENSE` and this file are the
only additions, and neither is upstream's. The collector is configured entirely
from the outside — compile-time defines in the top-level `CMakeLists.txt`, and
`GC_set_*` calls in `micropython/femm_gc.c` — precisely so that re-vendoring a
newer release stays a plain extract-and-copy.

## What was kept, and what was dropped

Kept: `include/` (public and private headers), the 31 top-level `*.c`,
`extra/gc.c`, `README.md`, `README.QUICK`, `doc/README.macros`.

Dropped: `tests/`, `tools/`, `cord/`, `m4/`, `build/`, the rest of `doc/`, and
every autotools and CMake file (`configure`, `Makefile.*`, `aclocal.m4`,
`config.guess`, `ltmain.sh`, `CMakeLists.txt`, …).

Dropping the build system is not just tidiness. Those GNU helper scripts are
the files upstream's notice means when it says "a few are GPL'ed"; none of them
is here, so nothing in this directory is under anything but the permissive
X11-style notice in `LICENSE`.

## How it is built

As **one translation unit**: `extra/gc.c` `#include`s every core source, which
is upstream's supported way to build the collector as a single object. That is
why all 31 top-level `.c` files are kept even though only `extra/gc.c` is named
in `CMakeLists.txt` — and why adding a bdwgc CMake subproject is unnecessary,
so normal femm-qt builds stay pure CMake.

The compile-time defines live in `CMakeLists.txt` (search for `add_library(bdwgc`)
and are mirrored in `micropython/build_test.sh`. **Keep the two lists in step.**
Every name in them is checked against `doc/README.macros` of this release;
several drifted between 7.x and 8.x, so re-check them on any version bump —
an unrecognised define is silently ignored, and the effect (say, incremental
collection quietly staying on) would not show up until it broke something.

## Header layout, on a version bump

8.2.x installs its headers flat: `include/gc.h`, `include/javaxfc.h`. The
development branch moved them under `include/gc/`. `micropython/femm_gc.c`
includes `<gc.h>` and `<javaxfc.h>` to match what is here; a bump to a release
that uses the `gc/` prefix needs those two lines changed.

Angle brackets are deliberate: MicroPython's own collector header is `py/gc.h`,
and `femm_gc.c` includes both.
