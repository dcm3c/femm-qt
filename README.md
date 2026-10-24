# FEMM 5.0

FEMM is a finite element program for low-frequency electromagnetic problems in
two dimensions, planar or axisymmetric: **magnetics** (static and
time-harmonic), **electrostatics**, **heat flow** and **current flow**. It has
a graphical pre- and post-processor, solvers for all four physics, and
scripting from inside the program or from other languages.

FEMM 5.0 (femm-qt) is a port of FEMM 4.2 from a Windows-only MFC program to Qt,
by FEMM's author, David Meeker (dmeeker@ieee.org), with the contributors
listed in [AUTHORS](AUTHORS). FEMM 4.2 model files open directly.

## Downloads

Windows installers and macOS disk images are posted on
**[femm.info](https://www.femm.info/)**, along with the release notes for each
build. Linux and FreeBSD users build from source (below). Each release is also
tagged here, `v5.0.YYYYMMDD` after its release date, and GitHub offers the
source archive for every tag.

## What is new in 5.0

* Built with Qt, so it compiles on just about any platform. Builds and runs on
  Windows, Linux, FreeBSD and macOS.
* Meshing by **tangle**, compiled in as an internal library, instead of
  Triangle.
* Faster analysis: on the example models, meshing and solving together run
  about 3 to 9 times faster than in FEMM 4.2, mostly from solvers rebuilt
  around an incomplete LDL^T preconditioner in place of FEMM 4.2's SSOR.
  Results are also bit-for-bit repeatable from run to run.
* **MicroPython** is the embedded scripting language, replacing Lua 4.
* External programs drive FEMM over a cross-platform **TCP** connection
  instead of Windows-only ActiveX. Bindings are included for MATLAB/Octave,
  Python, Mathematica, Scilab and Julia, plus Lua 4 so that existing Lua
  scripts keep working.

## Building

Each platform has a one-step build script at the top of the tree and a guide
in `docs/` listing the prerequisites:

| Platform | Script | Guide |
| --- | --- | --- |
| Windows (Qt + MinGW) | `win_build.bat` | [docs/windows-build.pdf](docs/windows-build.pdf) |
| Linux | `./linux_build.sh` | [docs/linux-build.pdf](docs/linux-build.pdf) |
| macOS | `./macos_build.sh` | [docs/macos-build.pdf](docs/macos-build.pdf) |
| FreeBSD | `./freebsd_build.sh` | [docs/freebsd-build.pdf](docs/freebsd-build.pdf) |

Each builds into `build/`; pass `clean` to start over. The regression suite
runs with `bash testing/_verify_all.sh`.

## Documentation

* [manual/manual.pdf](manual/manual.pdf): the user's manual, including the
  scripting reference.
* [octavefemm/manual/octavefemm.pdf](octavefemm/manual/octavefemm.pdf): driving
  FEMM from MATLAB or Octave.
* [luafemm/femm-lua.pdf](luafemm/femm-lua.pdf): running legacy Lua 4 scripts.
* `docs/*-file-formats.pdf`: the on-disk formats of the four problem types.
* `examples/`: example models and scripts.

## Questions and bug reports

Ask questions and report bugs on the FEMM user group,
**[groups.io/g/femm](https://groups.io/g/femm/)**. A model file that shows the
problem helps a great deal. Pull requests are welcome; see
[CONTRIBUTING.md](CONTRIBUTING.md) first.

## Where things are

| Directory | Contents |
| --- | --- |
| `gui/` | the Qt user interface |
| `libfemm/` | document model, post-processing and the scripting engine |
| `solvers/` | the four finite element solvers |
| `tangle/` | the mesher |
| `ipc/` | the TCP server and the headless engine |
| `pyfemm/`, `octavefemm/`, `mathfemm/`, `scifemm/`, `juliafemm/`, `luafemm/` | language bindings |
| `matlibs/`, `provenance/` | material libraries, and the sources behind every entry |
| `testing/`, `tests/` | regression suites |

## Licensing

The femm-qt application -- the GUI, the libfemm engine, the IPC server, the
tangle mesher, the shared math and the language bindings -- is released under
the MIT License (see [LICENSE-MIT](LICENSE-MIT)). The finite element field
solvers in `solvers/` are released under the GNU General Public License v3 or
later (see [solvers/COPYING](solvers/COPYING)). The SPDX-License-Identifier tag
in each source file is authoritative.

Qt is used under the LGPL-3.0; see [NOTICE-Qt.txt](NOTICE-Qt.txt) and
[QT-THIRD-PARTY-NOTICES.txt](QT-THIRD-PARTY-NOTICES.txt). The other bundled
components keep their own licenses, all MIT/X11 or similar: MicroPython,
micropython-lib, ulab, the Boehm-Demers-Weiser garbage collector, Lua 4 and
LuaSocket (the legacy Lua client), and complexEigenPairSolver. Their license
texts ship with the program.

For use of the GPL solvers under terms other than GPLv3 (e.g., in proprietary
or closed-source applications), alternative licensing arrangements are
available on request -- please contact the author.
