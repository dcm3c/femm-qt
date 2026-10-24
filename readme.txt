FEMM 5.0
David Meeker
dmeeker@ieee.org

* Port of Windows MDI program to Qt so that it can be compiled on just
  about any platform.  Builds and runs on Windows, Linux, FreeBSD and macOS.
* FEMM now uses tangle, compiled as an internal library, for meshing
  instead of Triangle
* Faster analysis: on the example models, meshing and solving together run
  about 3 to 9 times faster than in FEMM 4.2, mostly from solvers rebuilt
  around an incomplete LDL^T preconditioner in place of FEMM 4.2's SSOR.
  Results are also bit-for-bit repeatable from run to run
* External bindings use cross-platform capable TCP IPC instead of Windows-
  only ActiveX
* MicroPython now used as an embedded scripting language rather than Lua 4
* Bindings included for Matlab/Octave, Python, Mathematica, Scilab, Julia,
  and Lua 4 (for backward-compatibility of Lua scripts)
* Licensing: the femm-qt application -- the GUI, the libfemm engine, the IPC
  server, the tangle mesher, the shared math, and the language bindings -- is
  released under the MIT License (see LICENSE-MIT). The finite element field
  solvers in solvers/ are released under the GNU General Public License v3 or
  later (see solvers/COPYING). The SPDX-License-Identifier tag in each source
  file is authoritative. Bundled components keep their own licenses, and the
  license text for each ships in bin/ beside the program: Qt (LGPL-3.0, see
  NOTICE-Qt.txt and QT-THIRD-PARTY-NOTICES.txt), MicroPython, micropython-lib,
  ulab, the Boehm-Demers-Weiser garbage collector, Lua 4 and LuaSocket (the
  legacy Lua client), and complexEigenPairSolver -- all MIT/X11 or similar.
  For use of the GPL solvers under terms other than GPLv3 (e.g., in
  proprietary or closed-source applications), alternative licensing
  arrangements are available on request -- please contact the author.
