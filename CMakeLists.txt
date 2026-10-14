cmake_minimum_required(VERSION 3.20)
# The ONE version number, 5.0.YYYYMMDD with the RELEASE date (not the build
# date: a source archive built later must report the same number). Everything
# else derives from it: the About box and femm_headless --version, the Windows
# VERSIONINFO (cmake/WinVersion.cmake), the installer (generated/version.iss),
# the macOS Info.plist. A release is cut by changing it here, publishing, and
# tagging the published commit with packaging/tag_release.sh, which refuses a
# tag that does not match this line.
project(femm-qt VERSION 5.0.20261013 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# All executables go into a single bin/ directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Paths to source trees. License split: tangle/ and common/ are MIT (shared by
# the GUI and the GPL solvers); solvers/ is GPL-3.0-or-later.
set(SOLVER_DIR "${CMAKE_SOURCE_DIR}/solvers")
set(TANGLE_DIR "${CMAKE_SOURCE_DIR}/tangle")        # MIT: tangle mesher + float256
set(COMMON_DIR "${CMAKE_SOURCE_DIR}/common")        # MIT: shared math (complex/spars/StdAfx)
set(SOLVER_COMMON_DIR "${SOLVER_DIR}/common")       # GPL: solver-only (cspars/fullmatrix/expr_eval)
set(MPY_DIR "${CMAKE_SOURCE_DIR}/micropython")      # MIT: embedded MicroPython engine (libfemm-only)

# --- Collector: bdwgc replaces MicroPython's block allocator ---------------
# py/gc.c is not compiled; micropython/femm_gc.c implements the same py/gc.h
# on top of the Boehm-Demers-Weiser collector. Why, and the measurements:
# docs/bdwgc-handoff-plan.md.
#
# The option exists so a field problem on one platform can be bisected to the
# collector in a minute; it is meant to be removed after a release without
# incident. OFF restores py/gc.c and the split-heap configuration.
option(FEMM_GC_BDWGC "Use bdwgc instead of MicroPython's block allocator" ON)

if(FEMM_GC_BDWGC)
    set(BDWGC_DIR "${CMAKE_SOURCE_DIR}/third_party/bdwgc")
    # One translation unit: extra/gc.c #includes every core source, which is
    # upstream's supported way to build the collector as a single object. It
    # keeps normal builds pure CMake -- no bdwgc subproject, no autotools.
    #
    # LICENSING, since bdwgc's own LICENSE flags two exceptions and the answer
    # is not obvious from reading it:
    #
    #  * The "modified GPL" paragraph is about the AUTOTOOLS helper scripts --
    #    config.guess, config.sub, install-sh, ltmain.sh and friends, which
    #    carry the GPL with the autoconf exception. None of them is vendored
    #    here, and the line below is why: this builds with CMake and never
    #    invokes the GNU build procedure at all. That paragraph cannot reach
    #    femm-qt.
    #  * The two specially-licensed FILES are include/gc_allocator.h (SGI 1996
    #    + HP 2002, permissive) and include/private/msvc_dbg.h (MIT). Both are
    #    vendored, NEITHER is compiled -- extra/gc.c includes neither -- so
    #    nothing from them is in any binary. Each carries its full notice in
    #    its own header, which is all those licences ask of a source
    #    distribution, so they need no separate LICENSE-*.txt beside
    #    LICENSE-bdwgc.txt.
    #
    # Deleting those two from the vendored tree was considered and rejected:
    # it would buy nothing (no obligation exists to remove) and would cost the
    # ability to diff this directory against upstream on the next re-vendor.
    add_library(bdwgc STATIC "${BDWGC_DIR}/extra/gc.c")
    target_include_directories(bdwgc PUBLIC "${BDWGC_DIR}/include")
    target_compile_definitions(bdwgc PRIVATE
        GC_NOT_DLL              # static link (Windows); harmless elsewhere
        LARGE_CONFIG            # heap tables sized for GB-scale heaps, not MB
        ALL_INTERIOR_POINTERS   # ulab views/frombuffer hold interior pointers
        USE_MUNMAP              # return empty heap sections to the OS
        GC_DISABLE_INCREMENTAL  # no dirty-bit tracking, no SIGSEGV handler ever
        NO_EXECUTE_PERMISSION   # heap pages are data, not code
        GC_NO_THREADS_DISCOVERY
    )
    # No GC_THREADS: MICROPY_PY_THREAD is 0 and only the script thread touches
    # the collector. Qt's threads and PyBridgeHost's socket loop never allocate
    # from it and never hold the only reference to a GC object.
    target_compile_options(bdwgc PRIVATE -w)   # vendored, unmodified: not our warnings
endif()

# --- MicroPython embed library (compiled as C, -std=gnu11) ---
# The generated micropython_embed/ tree is vendored (committed), platform-
# independent C pinned by micropython/MICROPYTHON_VERSION. Normal
# builds are pure CMake; GNU make + Python are only needed for the optional
# regen-micropython target when bumping MicroPython or editing mpconfigport.h.
file(GLOB_RECURSE MPY_SOURCES "${MPY_DIR}/micropython_embed/*.c")
# Exclude the embed port's mphalport.c: py_state.cpp provides its own
# mp_hal_stdout_tx_strn[_cooked] routed through a settable output sink.
list(FILTER MPY_SOURCES EXCLUDE REGEX "port/mphalport\\.c$")
# Exclude the lib/ sources that their modules #include as .c files (upstream
# does this deliberately): crypto-algorithms/sha256.c inside modhashlib.c, and
# re1.5's engine inside modre.c. Compiling them again on their own would define
# every sha256_/re1.5 symbol twice.
list(FILTER MPY_SOURCES EXCLUDE REGEX "crypto-algorithms/sha256\\.c$")
list(FILTER MPY_SOURCES EXCLUDE REGEX "lib/re1\\.5/.*\\.c$")

# The block allocator/collector in py/gc.c is replaced by micropython/femm_gc.c
# on top of bdwgc. Refuse to configure if a version bump moves the file, rather
# than silently linking two collectors: the duplicate-symbol error that would
# follow is loud, but says nothing about why.
if(FEMM_GC_BDWGC)
    set(MPY_GC_FILE "${MPY_DIR}/micropython_embed/py/gc.c")
    list(FIND MPY_SOURCES "${MPY_GC_FILE}" _mpy_gc_idx)
    if(_mpy_gc_idx EQUAL -1)
        message(FATAL_ERROR
            "[micropython] ${MPY_GC_FILE} is not in the source list, so the "
            "exclusion below matches nothing and femm_gc.c would be linked "
            "alongside upstream gc.c. Find where the file moved to.")
    endif()
    list(FILTER MPY_SOURCES EXCLUDE REGEX "py/gc\\.c$")
endif()

# --- sys.stdout must go through the output sink, not through fd 1 ---
# Two files define mp_sys_std{in,out,err}_obj: extmod/vfs_posix_file.c, which
# puts them on raw POSIX fds, and shared/runtime/sys_stdio_mphal.c, which puts
# them on mp_hal_stdout_tx_strn -- i.e. on py_state.cpp's settable output sink.
# MICROPY_PY_SYS_STDFILES makes those objects the destination of print(), of
# sys.stdout.write(), and of MP_PYTHON_PRINTER (mpprint.h), so which one links
# decides where ALL script output goes.
#
# Only one can link, and the choice was being made by accident: vfs_posix_file.c
# is pulled in regardless for ordinary file I/O, so its definition won and
# sys_stdio_mphal.c was never pulled out of the archive. That routed every
# print() to fd 1, which bypasses the GUI console -- and in a GUI-subsystem
# process, which has no console at all, fd 1 is not even a valid descriptor, so
# printing raised OSError(EBADF) instead of printing.
#
# Rename the posix ones out of the way (nothing references them inside their own
# translation unit unless MICROPY_PY_SYS_STDIO_BUFFER is on, which it is not) so
# the mp_hal definitions are what modsys.c resolves against. Done here rather
# than by editing the file, which would put a local change in a tree that
# regen.sh regenerates wholesale -- so a regen carries this fix along for free.
#
# What a regen CAN break is the path below. set_source_files_properties() on a
# file that is not in the target is a silent no-op: no warning, clean build, and
# every print() back on fd 1 -- invisible to a developer, because a binary
# started from a shell has a valid fd 1. Only ConsolelessPrintTest.py would
# notice. So assert the file is really in the glob, and fail the configure if a
# version bump ever moves it.
set(MPY_VFS_POSIX_FILE "${MPY_DIR}/micropython_embed/extmod/vfs_posix_file.c")
list(FIND MPY_SOURCES "${MPY_VFS_POSIX_FILE}" _mpy_vfs_idx)
if(_mpy_vfs_idx EQUAL -1)
    message(FATAL_ERROR
        "[micropython] ${MPY_VFS_POSIX_FILE} is not in the source list, so the "
        "mp_sys_std*_obj rename below would do nothing and all script output "
        "would go to fd 1 -- which a GUI-subsystem process does not have. "
        "Find where the file moved to and update this path.")
endif()
set_source_files_properties("${MPY_VFS_POSIX_FILE}" PROPERTIES
    COMPILE_DEFINITIONS "mp_sys_stdin_obj=mp_sys_stdin_obj_posix_unused;mp_sys_stdout_obj=mp_sys_stdout_obj_posix_unused;mp_sys_stderr_obj=mp_sys_stderr_obj_posix_unused")

# ulab (numpy/scipy ndarrays) is a USER_C_MODULE: the regen generates its qstrs
# and the `ulab` module registration into genhdr (USER_C_MODULES=ulab in
# regen.sh), but the embed packager only ships py/*, so ulab's own .c are
# compiled here directly, against the same genhdr + core includes. The core's
# generated moduledefs.h references ulab_user_cmodule unconditionally, so these
# MUST be linked in (built with MODULE_ULAB_ENABLED=1, see below).
file(GLOB_RECURSE ULAB_SOURCES "${MPY_DIR}/ulab/code/*.c")
# femm_port_hooks.c provides symbols the core references under our
# mpconfigport.h (femm_vm_hook_fn, gc_get_max_new_split). It lives outside
# the generated tree so regen-micropython leaves it alone.
if(FEMM_GC_BDWGC)
    set(MPY_GC_SOURCE "${MPY_DIR}/femm_gc.c")
else()
    set(MPY_GC_SOURCE "")
endif()
add_library(micropython_embed STATIC ${MPY_SOURCES} ${ULAB_SOURCES}
    "${MPY_DIR}/femm_port_hooks.c" ${MPY_GC_SOURCE})
if(FEMM_GC_BDWGC)
    target_link_libraries(micropython_embed PUBLIC bdwgc)
else()
    # PUBLIC, not PRIVATE: this switches MICROPY_GC_SPLIT_HEAP in
    # mpconfigport.h, which changes the layout of mp_state_mem_t. Every
    # translation unit that includes a MicroPython header -- libfemm and the
    # test executables included -- must agree, or the two halves disagree about
    # where the interpreter's state lives and corrupt the heap.
    target_compile_definitions(micropython_embed PUBLIC FEMM_GC_MICROPYTHON)
endif()
target_include_directories(micropython_embed PUBLIC
    "${MPY_DIR}"
    "${MPY_DIR}/micropython_embed"
    "${MPY_DIR}/micropython_embed/port"
)
# ulab sources need their own dir on the include path and the enable flag.
target_include_directories(micropython_embed PRIVATE "${MPY_DIR}/ulab/code")
set_source_files_properties(${ULAB_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "MODULE_ULAB_ENABLED=1")
# shared/runtime/gchelper_generic.c deliberately reads callee-saved CPU
# registers (x19-x29 on AArch64) bound via `const register long x asm(...)` to
# scan them for the GC. The compiler can't see they're live and flags them as
# uninitialized const reads; the warnings are false positives for this
# intentional pattern, and the file is vendored/regenerated, so silence them at
# the target rather than patching the generated source. -Wno-uninitialized is
# universally recognized. The const-init diagnostic, however, exists only on
# newer Clang -- and Clang (unlike GCC) does NOT silently ignore an unknown
# -Wno-... option: it emits -Wunknown-warning-option for EVERY file. So gate it
# on the compiler actually knowing the warning. We probe the POSITIVE form,
# because an unknown -W<name> is what Clang rejects (an unknown -Wno-<name> is
# accepted without error and would mis-report as supported).
include(CheckCCompilerFlag)
check_c_compiler_flag("-Wdefault-const-init-var-unsafe" HAVE_WCONST_INIT_UNSAFE)
set(_mpy_const_init_flag "")
if(HAVE_WCONST_INIT_UNSAFE)
    set(_mpy_const_init_flag -Wno-default-const-init-var-unsafe)
endif()
target_compile_options(micropython_embed PRIVATE -std=gnu11 -fno-common -Wall
    -Wno-unused-variable -Wno-sign-compare
    -Wno-uninitialized ${_mpy_const_init_flag})
# Staleness guard (content-based, not mtime). The committed micropython_embed
# package records the mpconfigport.h it was generated against in
# mpconfigport.sha256 (line-ending-normalized, so it's identical across
# platforms). Warn only if the current config's content differs from that stamp
# -- i.e. someone actually edited mpconfigport.h. A clean checkout/build never
# trips it, and end users never need to rebuild MicroPython. NOTICE (not
# WARNING) keeps it low-key; it's only actionable for someone editing the config.
if(EXISTS "${MPY_DIR}/mpconfigport.sha256")
    file(READ "${MPY_DIR}/mpconfigport.h" _mpy_cfg)
    string(REGEX REPLACE "\r" "" _mpy_cfg "${_mpy_cfg}")
    string(SHA256 _mpy_cfg_hash "${_mpy_cfg}")
    file(READ "${MPY_DIR}/mpconfigport.sha256" _mpy_cfg_stamp)
    string(STRIP "${_mpy_cfg_stamp}" _mpy_cfg_stamp)
    if(NOT _mpy_cfg_hash STREQUAL _mpy_cfg_stamp)
        message(NOTICE
            "[micropython] mpconfigport.h has changed since the vendored embed "
            "package was generated. Only relevant if you edited it: re-run the "
            "regen-micropython target for a qstr/feature change, or just refresh "
            "the stamp for a comment/declaration-only edit:\n"
            "    cmake -P ${MPY_DIR}/stamp_mpconfig.cmake")
    endif()
endif()

# --- Build identity (Help > About, and the femm_headless banner) ----------
# common/build_info.cpp is GENERATED, not written by hand: see common/
# build_info.h for why a hand-written __DATE__ would silently freeze at the day
# the tree was first built. Generated once here so a fresh configure has the
# file, and again by femm_build_stamp on every build so the date is the date.
# cmake/BuildInfo.cmake rewrites it only when the content changes, so this
# costs one recompile-and-relink per day, not per build.
set(BUILD_INFO_IN "${COMMON_DIR}/build_info.cpp.in")
set(BUILD_INFO_CPP "${CMAKE_BINARY_DIR}/generated/build_info.cpp")
set(BUILD_INFO_ARGS
    "-DIN=${BUILD_INFO_IN}" "-DOUT=${BUILD_INFO_CPP}"
    "-DFEMM_VERSION=${PROJECT_VERSION}")

# The installer's version (packaging/femm-qt.iss #includes this). Written at
# configure time like the rest of the version plumbing, so a bump above
# reaches the installer through the ordinary ninja run pack.bat already needs.
file(WRITE "${CMAKE_BINARY_DIR}/generated/version.iss.tmp"
     "; GENERATED by CMakeLists.txt from project(VERSION) -- do not edit.\n"
     "#define FemmVersion \"${PROJECT_VERSION}\"\n")
configure_file("${CMAKE_BINARY_DIR}/generated/version.iss.tmp"
               "${CMAKE_BINARY_DIR}/generated/version.iss" COPYONLY)
execute_process(COMMAND "${CMAKE_COMMAND}" ${BUILD_INFO_ARGS}
                -P "${CMAKE_SOURCE_DIR}/cmake/BuildInfo.cmake")
add_custom_target(femm_build_stamp
    COMMAND "${CMAKE_COMMAND}" ${BUILD_INFO_ARGS}
            -P "${CMAKE_SOURCE_DIR}/cmake/BuildInfo.cmake"
    COMMENT "Stamping the build date")
set_source_files_properties("${BUILD_INFO_CPP}" PROPERTIES GENERATED TRUE)

# --- Common math library (complex, spars, StdAfx) — MIT ---
# The shared numeric foundation linked by BOTH the MIT GUI/libfemm and the GPL
# solvers. (Solver-only matrix code — cspars/fullmatrix — stays GPL in
# solvers/common and is compiled into the solvers that use it. float256 moved
# to tangle/.)
add_library(femm_common STATIC
    "${COMMON_DIR}/complex.cpp"
    "${COMMON_DIR}/spars.cpp"
    "${COMMON_DIR}/StdAfx.cpp"
    "${COMMON_DIR}/fullmatrix.cpp"   # CFullMatrix + complex eig/qr kernels (MIT)
    "${COMMON_DIR}/utf8_io.cpp"      # UTF-8 path shims (femm_fopen/femm_path/...)
    "${BUILD_INFO_CPP}"              # generated; femm_version/femm_build_date
)
add_dependencies(femm_common femm_build_stamp)
target_include_directories(femm_common PUBLIC "${COMMON_DIR}")
target_compile_options(femm_common PRIVATE -Wall
    -Wno-unused-variable -Wno-sign-compare -Wno-write-strings
)
target_compile_definitions(femm_common PRIVATE "I=CComplex(0,1)")

# (OpenMP was removed 2026-08: with the whole-matrix ILDL preconditioner the
# solvers are dominated by inherently sequential work, and measured speedup
# across 10k-156k node problems was -11% to +4% -- while sequential
# reductions make results bit-deterministic and macOS packaging loses the
# Homebrew libomp dependency.)
target_link_libraries(femm_common PUBLIC
    # utf8_io.cpp's femm_utf8_args() calls CommandLineToArgvW (shell32) on Windows.
    $<$<BOOL:${WIN32}>:shell32>)

# --- Threads ---
# py_state.cpp (via femm_stack_guard.h) calls pthread_attr_get{,stack} to size
# the MicroPython C-stack guard. On Linux these live in libc (glibc folds in
# libpthread) and on macOS/Windows the runtime is implicit, so the link
# historically worked with no explicit thread lib. FreeBSD keeps them in
# libpthread/libthr, so the symbol is unresolved unless we link the thread
# library. Threads::Threads expands to the right flag (-pthread) where one is
# needed and to nothing where it isn't. Linked PUBLIC on libfemm below so it
# propagates to femm_headless, femm_gui, and anything else that uses libfemm.
find_package(Threads REQUIRED)

# Windows VERSIONINFO for every shipped executable, from project(VERSION).
include("${CMAKE_SOURCE_DIR}/cmake/WinVersion.cmake")

# --- Layer 1: libfemm (core engine, no Qt) ---
add_subdirectory(libfemm)

# --- Layer 2: IPC server + headless binary (no Qt) ---
add_subdirectory(ipc)

# --- Lua 4 legacy scripting client (no Qt) ---
# Builds the bundled Lua 4.0 interpreter + LuaSocket as "lua4" in bin/, so
# legacy FEMM Lua scripts run against femm-qt over TCP (see luafemm/).
add_subdirectory(lua)

# --- Layer 3: Qt GUI (optional) ---
find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
    message(STATUS "Qt6 found: ${Qt6_DIR}")
    add_subdirectory(gui)
else()
    message(STATUS "Qt6 not found — building headless only")
endif()

# --- Tangle mesher (MIT, self-contained: tangle + float256) ---
# tangle uses only float256 (no complex/spars), so tangle_lib has no
# femm_common dependency. Its PUBLIC include exports tangle_mesh.h/float256.h.
add_library(tangle_lib STATIC
    "${TANGLE_DIR}/tangle.cpp"
    "${TANGLE_DIR}/float256.cpp"
)
target_include_directories(tangle_lib PUBLIC "${TANGLE_DIR}")
target_compile_definitions(tangle_lib PRIVATE TANGLE_AS_LIBRARY)
target_compile_options(tangle_lib PRIVATE -Wall
    -Wno-unused-variable -Wno-sign-compare -Wno-write-strings
)

# Shared compiler options for CLI solvers
set(SOLVER_WARNINGS -Wall -Wno-unused-variable -Wno-unused-but-set-variable
    -Wno-sign-compare -Wno-write-strings -Wno-unused-result)

# Helper function to build a CLI solver
function(add_solver NAME SOLVER_DIR)
    add_executable(${NAME} ${ARGN})
    # common/ (complex/spars/StdAfx) and tangle/ come transitively via the
    # PUBLIC includes of femm_common and tangle_lib; add the solver's own dir
    # and solvers/common (GPL solver-only headers: cspars/fullmatrix/...).
    target_include_directories(${NAME} PRIVATE
        "${SOLVER_DIR}" "${SOLVER_COMMON_DIR}")
    target_link_libraries(${NAME} PRIVATE
        tangle_lib femm_common)
    target_compile_options(${NAME} PRIVATE ${SOLVER_WARNINGS})
    # Solvers use I as imaginary unit shorthand (removed from complex.h
    # because it clashes with Qt, but solvers never see Qt headers)
    target_compile_definitions(${NAME} PRIVATE "I=CComplex(0,1)")
    # Static link to avoid runtime DLL ABI mismatches (Windows only —
    # macOS doesn't support fully static linking, Linux doesn't need it)
    if(WIN32)
        target_link_options(${NAME} PRIVATE -static)
    endif()
endfunction()

# --- fkn (magnetics solver) ---
# MagDirFctn expressions are evaluated by the standalone expr_eval +
# mag_dir_eval (no scripting engine), so fkn is now a plain solver like the
# others and no longer links micropython_embed.
add_solver(fkn "${SOLVER_DIR}/fkn"
    "${SOLVER_DIR}/fkn/main.cpp"
    "${SOLVER_DIR}/fkn/load_tangle_mesh.cpp"
    "${SOLVER_DIR}/fkn/femmedoccore.cpp"
    "${SOLVER_DIR}/fkn/matprop.cpp"
    "${SOLVER_DIR}/fkn/prob1big.cpp"
    "${SOLVER_DIR}/fkn/prob2big.cpp"
    "${SOLVER_DIR}/fkn/prob3big.cpp"
    "${SOLVER_DIR}/fkn/prob4big.cpp"
    "${SOLVER_DIR}/fkn/mag_dir_eval.cpp"
    "${SOLVER_COMMON_DIR}/expr_eval.cpp"
    "${SOLVER_COMMON_DIR}/cspars.cpp"      # CBigComplexLinProb (AC magnetics)
    # CFullMatrix (matprop B-H interpolation) now comes from the unified MIT
    # common/fullmatrix.cpp via femm_common — see add_library(femm_common).
)

# --- belasolv (electrostatics solver) ---
add_solver(belasolv "${SOLVER_DIR}/belasolv"
    "${SOLVER_DIR}/belasolv/main.cpp"
    "${SOLVER_DIR}/belasolv/load_tangle_mesh.cpp"
    "${SOLVER_DIR}/belasolv/femmedoccore.cpp"
    "${SOLVER_DIR}/belasolv/prob1big.cpp"
)

# --- hsolv (heat flow solver) ---
add_solver(hsolv "${SOLVER_DIR}/hsolv"
    "${SOLVER_DIR}/hsolv/main.cpp"
    "${SOLVER_DIR}/hsolv/load_tangle_mesh.cpp"
    "${SOLVER_DIR}/hsolv/hsolvdoc.cpp"
    "${SOLVER_DIR}/hsolv/prob1big.cpp"
)

# --- csolv (current flow solver) ---
add_solver(csolv "${SOLVER_DIR}/csolv"
    "${SOLVER_DIR}/csolv/main.cpp"
    "${SOLVER_DIR}/csolv/load_tangle_mesh.cpp"
    "${SOLVER_DIR}/csolv/femmedoccore.cpp"
    "${SOLVER_DIR}/csolv/prob1big.cpp"
    "${SOLVER_COMMON_DIR}/cspars.cpp"      # CBigComplexLinProb (AC current flow)
)

femm_add_version_resource(fkn      "FEMM magnetics solver")
femm_add_version_resource(belasolv "FEMM electrostatics solver")
femm_add_version_resource(hsolv    "FEMM heat flow solver")
femm_add_version_resource(csolv    "FEMM current flow solver")

# =====================================================================
#  install() rules -- for a SPLIT (FHS) layout, which is what a distro
#  packager needs. Nothing else uses them: the Windows installer packages
#  build/bin directly (packaging/femm-qt.iss), packaging/unix_install.sh
#  lays down a self-contained ~/.local/opt/femm-qt, and macos_build.sh
#  assembles the bundle. Those three are unaffected by anything here.
#
#  The layout deliberately matches what femmResource() searches
#  (libfemm/femm_resource.h): executables in bin/, everything read-only in
#  share/femm/, so /usr/bin/femm_gui finds /usr/share/femm/python via
#  exeDir() + "../share/femm/". No compiled-in prefix is needed, which is
#  why this works for /usr, /usr/local and ~/.local without recompiling.
#
#  DESTDIR comes free with install() and is what every packaging tool
#  stages into; nothing here defeats it by writing absolute paths.
#
#  NOT installed, on purpose:
#    * the four .dat material libraries go to share/femm as READ-ONLY
#      seeds. They are the one thing femm-qt writes, and it writes them
#      per-user now (libfemm/material_lib_path.h), so a root-owned copy
#      under share/ is correct rather than a problem.
#    * examples are installed read-only for the same reason; every example
#      script writes temp.fem beside itself, so a user needs their own
#      copy. unix_install.sh puts one in ~/Documents/FEMM; a package
#      should leave that to the application or to the user.
# =====================================================================
include(GNUInstallDirs)
set(FEMM_DATADIR "${CMAKE_INSTALL_DATADIR}/femm")

# Seven executables on Linux/BSD, eight on Windows. `lua` installs as lua4
# (OUTPUT_NAME, lua/CMakeLists.txt).
install(TARGETS femm_headless fkn belasolv hsolv csolv lua
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(TARGET femm_gui)
    # BUNDLE DESTINATION is not optional on macOS. femm_gui sets MACOSX_BUNDLE
    # (gui/CMakeLists.txt), and CMake REFUSES to configure an install() of a
    # bundle target that has no BUNDLE DESTINATION -- "install TARGETS given no
    # BUNDLE DESTINATION for MACOSX_BUNDLE executable target". That is a
    # CONFIGURE error, so it stops the build before a single file is compiled,
    # and it is invisible everywhere else: RUNTIME is what Windows, Linux and
    # BSD use, and the rule only applies on Apple.
    #
    # The install() rules arrived with the relocatable-install work (2026-08-29)
    # and the last macOS build predated them, so this had never been configured
    # on a Mac until 2026-09-09.
    #
    # "." puts femm_gui.app at the prefix root, which is where a .app belongs;
    # macOS ships the bundle in a DMG (assembled by macos_build.sh) rather than
    # a bin/ tree, so this path exists for completeness rather than for the way
    # anyone actually installs it there.
    install(TARGETS femm_gui
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
            BUNDLE  DESTINATION ".")
endif()
if(TARGET femm_headlessw)
    # Windows only, and ipc/CMakeLists.txt builds it only there: it is
    # femm_headless linked -mwindows, so a GUI-spawned run raises no console
    # window. Linux and BSD have neither problem -- no console flashes, and
    # SIGINT already works in a terminal -- so there is nothing to install.
    install(TARGETS femm_headlessw RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

# The in-process stdlib and the PyBridge worker are STAGED into bin/ by
# custom targets that assemble them from three source trees (pybridge/mpy,
# pybridge/mpy/shims, pylib/). Installing the staged result rather than the
# sources is what guarantees an installed tree contains exactly what the
# test suite ran against.
install(DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/python/"
        DESTINATION "${FEMM_DATADIR}/python")
install(DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/pybridge-worker/"
        DESTINATION "${FEMM_DATADIR}/pybridge-worker")

install(FILES
        "${CMAKE_SOURCE_DIR}/matlibs/matlib.dat"
        "${CMAKE_SOURCE_DIR}/matlibs/statlib.dat"
        "${CMAKE_SOURCE_DIR}/matlibs/heatlib.dat"
        "${CMAKE_SOURCE_DIR}/matlibs/condlib.dat"
        DESTINATION "${FEMM_DATADIR}")

# Built by manual/doit.bat rather than by CMake, so it may legitimately be
# absent in a source tree that has never built it. OPTIONAL keeps `install`
# working in that case instead of failing at the last step.
install(FILES "${CMAKE_SOURCE_DIR}/manual/manual.pdf"
        DESTINATION "${FEMM_DATADIR}" OPTIONAL)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/examples/"
        DESTINATION "${FEMM_DATADIR}/examples")

# License texts. Not optional: the solvers are GPL-3.0-or-later, whose section
# 4 requires conveying the license with the program, and every vendored MIT/X11
# component requires its notice to be included in distributions. A package that
# omits these is not a lawfully distributable package.
#
# Into share/doc/femm rather than share/femm, which is where a packager and
# every distro policy expect to find them.
install(FILES
        "${CMAKE_SOURCE_DIR}/LICENSE-MIT"
        "${CMAKE_SOURCE_DIR}/LICENSE-GPL"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}")
foreach(_lic
        "NOTICE-Qt.txt"
        "QT-THIRD-PARTY-NOTICES.txt"
        "LICENSE-melihaltun.txt"
        # NOT THIRD-PARTY-NOTICES.txt: it is generated from the macOS .app's
        # Contents/Frameworks and documents the Homebrew dylibs macdeployqt
        # copies in. A packaged build links the system Qt and ships none of
        # them, so including it would attribute libraries that are not there.
        "lua/liblua/COPYRIGHT"
        "lua/luasocket-1.0/COPYRIGHT"
        "micropython/LICENSE.micropython"
        "micropython/ulab/LICENSE"
        "third_party/bdwgc/LICENSE"
        "pylib/vendor/LICENSE.micropython-lib")
    # Flatten to a distinct name: four of these are called COPYRIGHT or
    # LICENSE, so copying them without renaming would silently leave one.
    string(REPLACE "/" "-" _name "${_lic}")
    install(FILES "${CMAKE_SOURCE_DIR}/${_lic}"
            DESTINATION "${CMAKE_INSTALL_DOCDIR}" RENAME "${_name}" OPTIONAL)
endforeach()

# The language bindings. Clients find these themselves -- they are not
# resolved through femmResource -- but a package that omits them ships an
# engine nobody can drive from Octave, Python, Scilab, Julia or Lua.
foreach(_b octavefemm mathfemm scifemm juliafemm luafemm pyfemm)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/${_b}/"
            DESTINATION "${FEMM_DATADIR}/${_b}"
            PATTERN "__pycache__" EXCLUDE
            PATTERN "examples" EXCLUDE)
endforeach()

# Desktop integration, Linux/BSD only -- a .desktop file is meaningless on
# Windows and macOS. Exec is the INSTALLED bin/, not a build path.
if(UNIX AND NOT APPLE)
    install(FILES "${CMAKE_SOURCE_DIR}/gui/icons/femm.png"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps"
            RENAME femm-qt.png OPTIONAL)
    configure_file("${CMAKE_SOURCE_DIR}/packaging/femm-qt.desktop.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/femm-qt.desktop" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/femm-qt.desktop"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
endif()
