cmake_minimum_required(VERSION 3.20)
project(femm-qt VERSION 5.0.0 LANGUAGES CXX C)

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
add_library(micropython_embed STATIC ${MPY_SOURCES} ${ULAB_SOURCES}
    "${MPY_DIR}/femm_port_hooks.c")
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
)
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