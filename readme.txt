FEMM 5.0 18Jun2026
David Meeker
dmeeker@ieee.org

* Port of Windows MDI program to Qt so that it can be compiled on just 
  about any platform
* FEMM now uses tangle, compiled as an internal library, for meshing
  instead of Triangle.
* Updated all solvers to leverage OpenMP to speed up solutions.
* Licensing: the femm-qt application -- the GUI, the libfemm engine, the IPC
  server, the tangle mesher, the shared math, and the language bindings -- is
  released under the MIT License (see LICENSE-MIT). The finite element field
  solvers in solvers/ are released under the GNU General Public License v3 or
  later (see solvers/COPYING). The SPDX-License-Identifier tag in each source
  file is authoritative. Bundled components keep their own licenses: Qt
  (LGPL-3.0), MicroPython and ulab (MIT). For use of the GPL solvers under
  terms other than GPLv3 (e.g., in proprietary or closed-source applications),
  alternative licensing arrangements are available on request -- please
  contact the author.

