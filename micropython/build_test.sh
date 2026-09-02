#!/usr/bin/env bash
# Pre-CMake build harness to validate the MicroPython binding layer.
# Builds the embed lib (cached), then py_bind + test_py_bind, then runs it.
set -e
cd "$(dirname "$0")"
export PATH="/c/Qt/Tools/mingw1310_64/bin:/usr/bin:$PATH"
BD=build-test
mkdir -p "$BD"
# ulab/code is on the list because py_state.cpp includes ndarray.h directly:
# format_value() flattens an ndarray to the wire, which is what makes the bulk
# getters byte-identical between a local caller and IPC. Adding that include to
# py_state.cpp without adding it here broke this script and nothing said so,
# because CMake builds py_state.cpp with a different include path and this is
# the only place tests/test_py_*.cpp are compiled at all.
MPYINC="-I. -Imicropython_embed -Imicropython_embed/port -Iulab/code -DMODULE_ULAB_ENABLED=1"
BDWGCINC="-I../third_party/bdwgc/include"
OURINC="-I../libfemm -I../common -I../tangle"

# The archive is CACHED -- it is only built when absent, because compiling the
# whole embed tree takes minutes and it almost never changes. It does change
# when ulab/code/ is edited, and then a stale archive fails at the LINK with an
# undefined reference that points at the caller rather than the edit (it was
# `ndarray_matmul_complex_hook', referenced from py_clinalg.o, when the @
# operator landed). Rebuild it after any ulab change:
#
#     rm micropython/build-test/libmpyembed.a
#
if [ ! -f "$BD/libmpyembed.a" ]; then
  echo "Compiling MicroPython embed sources..."
  OBJS=""
  # Exclude the embed port's mphalport.c: py_state.cpp provides its own
  # mp_hal_stdout_tx_strn[_cooked] routed to a settable output sink.
  # py/gc.c is replaced by femm_gc.c on bdwgc, exactly as in CMakeLists.txt.
  # Same exclusions as CMakeLists.txt, and for the same reasons:
  #   mphalport.c        - py_state.cpp provides mp_hal_stdout_tx_strn itself
  #   py/gc.c            - replaced by femm_gc.c on bdwgc
  #   sha256.c, re1.5/*  - their modules #include them as .c; compiling them
  #                        again defines every symbol twice
  for c in $(find micropython_embed -name '*.c'       | grep -v 'port/mphalport.c'       | grep -v 'py/gc.c'       | grep -v 'crypto-algorithms/sha256.c'       | grep -v 'lib/re1.5/'); do
    o="$BD/$(echo "$c" | sed 's#[/.]#_#g').o"
    # Mirror CMakeLists.txt: two vendored files define mp_sys_std*_obj, and
    # the one in vfs_posix_file.c puts them on raw POSIX fds. Rename them
    # there so sys_stdio_mphal.c's versions win and print() reaches the
    # output sink. Without it the harness links the fd-1 versions and the
    # sink test fails -- which is exactly what it was doing.
    case "$c" in
      *extmod/vfs_posix_file.c)
        gcc -c $MPYINC -std=gnu11 -O1 -fno-common \
            -Dmp_sys_stdin_obj=mp_sys_stdin_obj_posix_unused \
            -Dmp_sys_stdout_obj=mp_sys_stdout_obj_posix_unused \
            -Dmp_sys_stderr_obj=mp_sys_stderr_obj_posix_unused \
            -o "$o" "$c" ;;
      *)
        gcc -c $MPYINC -std=gnu11 -O1 -fno-common -o "$o" "$c" ;;
    esac
    OBJS="$OBJS $o"
  done
  gcc -c $MPYINC $BDWGCINC -std=gnu11 -O1 -fno-common -o "$BD/femm_gc.o" femm_gc.c
  OBJS="$OBJS $BD/femm_gc.o"
  # femm_port_hooks.c: the time HAL, the VM-loop hook pointer and the Windows
  # UTF-8 path helpers the core references under our mpconfigport.h.
  gcc -c $MPYINC -std=gnu11 -O1 -fno-common -o "$BD/femm_port_hooks.o" femm_port_hooks.c
  OBJS="$OBJS $BD/femm_port_hooks.o"
  # ulab is a USER_C_MODULE: genhdr references ulab_user_cmodule unconditionally,
  # so the archive does not link without it. Same flag as CMakeLists.txt.
  for c in $(find ulab/code -name '*.c'); do
    # Flatten the path, not just the basename: numpy/linalg/linalg.c and
    # scipy/linalg/linalg.c would otherwise write the same object file.
    o="$BD/$(echo "$c" | sed 's#[/.]#_#g').o"
    gcc -c $MPYINC -Iulab/code -DMODULE_ULAB_ENABLED=1 -std=gnu11 -O1 -fno-common -o "$o" "$c"
    OBJS="$OBJS $o"
  done
  ar rcs "$BD/libmpyembed.a" $OBJS
  echo "embed archive built."
fi

# The collector, as one translation unit (extra/gc.c includes the rest). Same
# defines as the bdwgc target in CMakeLists.txt -- keep the two in step.
if [ ! -f "$BD/libbdwgc.a" ]; then
  echo "Compiling bdwgc..."
  gcc -c -w -O1 $BDWGCINC       -DGC_NOT_DLL -DLARGE_CONFIG -DALL_INTERIOR_POINTERS -DUSE_MUNMAP       -DGC_DISABLE_INCREMENTAL -DNO_EXECUTE_PERMISSION -DGC_NO_THREADS_DISCOVERY       -o "$BD/bdwgc.o" ../third_party/bdwgc/extra/gc.c
  ar rcs "$BD/libbdwgc.a" "$BD/bdwgc.o"
  echo "bdwgc archive built."
fi

echo "Compiling binding layer + tests..."
# complex.cpp needs the imaginary unit `I` supplied per-TU (matches the real
# CMake build: target_compile_definitions(... "I=CComplex(0,1)")).
gcc -c $MPYINC -std=gnu11 -O1 -o "$BD/py_bind_type.o" ../libfemm/py_bind_type.c
g++ -c $MPYINC $OURINC -std=c++17 -O1 -o "$BD/py_bind.o"  ../libfemm/py_bind.cpp
g++ -c $MPYINC $OURINC -std=c++17 -O1 -o "$BD/py_state.o" ../libfemm/py_state.cpp
g++ -c $OURINC -std=c++17 -O1 -o "$BD/py_init_script.o" ../libfemm/py_init_script.cpp
g++ -c -I../common -std=c++17 -O1 -D'I=CComplex(0,1)' -o "$BD/complex.o" ../common/complex.cpp
g++ -c $MPYINC $OURINC -std=c++17 -O1 -o "$BD/test_bind.o"  ../tests/test_py_bind.cpp
g++ -c $MPYINC $OURINC -std=c++17 -O1 -o "$BD/test_state.o" ../tests/test_py_state.cpp
g++ -c $OURINC -std=c++17 -O1 -o "$BD/test_stubs.o" ../tests/test_stubs.cpp
# Everything else py_state.cpp references. Each of these post-dates the last
# revision of this script, which is why it had stopped linking.
g++ -c $MPYINC $OURINC -std=c++17 -O1 -o "$BD/py_os.o"          ../libfemm/py_os.cpp
g++ -c $MPYINC $OURINC -std=c++17 -O1 -o "$BD/py_pybridge.o"    ../libfemm/py_pybridge.cpp
g++ -c $MPYINC $OURINC -Iulab/code -std=c++17 -O1 -o "$BD/py_clinalg.o" ../libfemm/py_clinalg.cpp
g++ -c $OURINC -std=c++17 -O1 -o "$BD/pybridge_host.o"          ../libfemm/pybridge_host.cpp
g++ -c -I../common -std=c++17 -O1 -o "$BD/utf8_io.o"            ../common/utf8_io.cpp
# femm_linalg::* -- the complex linear algebra behind numpy.linalg in-process.
g++ -c -I../common -std=c++17 -O1 -D'I=CComplex(0,1)' -o "$BD/fullmatrix.o" ../common/fullmatrix.cpp

COMMON_LINK="$BD/py_state.o $BD/py_init_script.o $BD/py_bind.o $BD/py_bind_type.o $BD/complex.o $BD/test_stubs.o $BD/py_os.o $BD/py_pybridge.o $BD/py_clinalg.o $BD/pybridge_host.o $BD/utf8_io.o $BD/fullmatrix.o $BD/libmpyembed.a $BD/libbdwgc.a"
# -lws2_32: pybridge_host.cpp talks to the CPython worker over a socket.
g++ -o "$BD/test_py_bind"  "$BD/test_bind.o"  $COMMON_LINK -lws2_32
g++ -o "$BD/test_py_state" "$BD/test_state.o" $COMMON_LINK -lws2_32

echo "=== Phase 4: compile-check shared templates vs MagDocument (syntax only) ==="
g++ -fsyntax-only -std=c++17 $MPYINC $OURINC ../tests/compile_check_templates.cpp \
  && echo "py_editor_common.h / py_postproc_common.h compile OK"

echo "=== Phase 5: compile-check per-physics modules ==="
for f in py_mag_editor py_mag_postproc py_elec_editor py_elec_postproc py_heat_editor py_heat_postproc py_curr_editor py_curr_postproc; do
  g++ -c -std=c++17 $MPYINC $OURINC -o "$BD/$f.o" "../libfemm/$f.cpp" && echo "$f.cpp OK"
done

echo "=== Phase 10: compile-check py_state_globals + script_callbacks ==="
g++ -c -std=c++17 $MPYINC $OURINC -o "$BD/py_state_globals.o" "../libfemm/py_state_globals.cpp" && echo "py_state_globals.cpp OK"
g++ -c -std=c++17 $OURINC -o "$BD/script_callbacks.o" "../libfemm/script_callbacks.cpp" && echo "script_callbacks.cpp OK"
# fkn_py.cpp was removed in 1edc420, when MagDirFctn moved to a native
# expression evaluator and the solver stopped embedding MicroPython. The
# compile-check line for it stayed behind and has been failing ever since.

echo "=== Phase 7: syntax-check tcp_server (exec_python wire path) ==="
g++ -fsyntax-only -std=c++17 -I../ipc -I../libfemm ../ipc/tcp_server.cpp \
  && echo "tcp_server.cpp OK"

QTINC=/c/Qt/6.10.3/mingw_64/include
if [ -d "$QTINC/QtWidgets" ]; then
  echo "=== Phase 8: Qt syntax-check py_console.cpp ==="
  g++ -fsyntax-only -std=c++17 -fPIC -I"$QTINC" -I"$QTINC/QtCore" -I"$QTINC/QtGui" \
      -I"$QTINC/QtWidgets" $OURINC ../gui/py_console.cpp && echo "py_console.cpp OK"
  echo "=== Phase 9: Qt syntax-check py_line_edit.cpp ==="
  g++ -fsyntax-only -std=c++17 -fPIC -I"$QTINC" -I"$QTINC/QtCore" -I"$QTINC/QtGui" \
      -I"$QTINC/QtWidgets" $OURINC -I../common ../gui/py_line_edit.cpp && echo "py_line_edit.cpp OK"
fi

# The material-library seed state machine. Links nothing from MicroPython --
# material_lib_path.cpp needs only utf8_io and an exeDir(), and the test brings
# its own exeDir() so it can point at a scratch directory rather than wherever
# the test binary happens to live.
echo "=== Phase 11: material library seed logic ==="
g++ -std=c++17 -O1 $OURINC -I../common -o "$BD/test_material_lib" \
    ../tests/test_material_lib.cpp ../libfemm/material_lib_path.cpp \
    ../common/utf8_io.cpp

# The sparse preconditioner's breakdown handling. spars.cpp needs only the
# console-dialog stub, the CString/MsgBox shims in StdAfx.cpp and utf8_io --
# no document layer, no MicroPython.
echo "=== Phase 12: sparse preconditioner breakdown ==="
g++ -std=c++17 -O1 $OURINC -o "$BD/test_spars_pc" \
    ../tests/test_spars_pc.cpp ../common/spars.cpp ../common/StdAfx.cpp \
    ../common/utf8_io.cpp

# The complex twin of the above. cspars.cpp is the GPL half of the tree, so
# its test lives in solvers/tests/ rather than tests/ -- see
# reference_license_split: `grep -rl GPL-3.0-or-later` must match solvers/
# and nothing else. complex.cpp needs the per-TU imaginary unit.
echo "=== Phase 13: complex sparse preconditioner breakdown ==="
g++ -std=c++17 -O1 $OURINC -I../solvers/common -I../solvers/fkn \
    -D'I=CComplex(0,1)' -o "$BD/test_cspars_pc" \
    ../solvers/tests/test_cspars_pc.cpp ../solvers/common/cspars.cpp \
    ../common/StdAfx.cpp ../common/complex.cpp ../common/utf8_io.cpp

echo "=== running test_py_bind ==="
"./$BD/test_py_bind"
echo "=== running test_py_state ==="
"./$BD/test_py_state"
echo "=== running test_material_lib ==="
"./$BD/test_material_lib"
echo "=== running test_spars_pc ==="
"./$BD/test_spars_pc"
echo "=== running test_cspars_pc ==="
"./$BD/test_cspars_pc"
