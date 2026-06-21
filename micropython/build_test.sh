#!/usr/bin/env bash
# Pre-CMake build harness to validate the MicroPython binding layer.
# Builds the embed lib (cached), then py_bind + test_py_bind, then runs it.
set -e
cd "$(dirname "$0")"
export PATH="/c/Qt/Tools/mingw1310_64/bin:/usr/bin:$PATH"
BD=build-test
mkdir -p "$BD"
MPYINC="-I. -Imicropython_embed -Imicropython_embed/port"
OURINC="-I../../libfemm -I../common"

if [ ! -f "$BD/libmpyembed.a" ]; then
  echo "Compiling MicroPython embed sources..."
  OBJS=""
  # Exclude the embed port's mphalport.c: py_state.cpp provides its own
  # mp_hal_stdout_tx_strn[_cooked] routed to a settable output sink.
  for c in $(find micropython_embed -name '*.c' | grep -v 'port/mphalport.c'); do
    o="$BD/$(echo "$c" | sed 's#[/.]#_#g').o"
    gcc -c $MPYINC -std=gnu11 -O1 -fno-common -o "$o" "$c"
    OBJS="$OBJS $o"
  done
  ar rcs "$BD/libmpyembed.a" $OBJS
  echo "embed archive built."
fi

echo "Compiling binding layer + tests..."
# complex.cpp needs the imaginary unit `I` supplied per-TU (matches the real
# CMake build: target_compile_definitions(... "I=CComplex(0,1)")).
gcc -c $MPYINC -std=gnu11 -O1 -o "$BD/py_bind_type.o" ../../libfemm/py_bind_type.c
g++ -c $MPYINC $OURINC -std=c++17 -O1 -o "$BD/py_bind.o"  ../../libfemm/py_bind.cpp
g++ -c $MPYINC $OURINC -std=c++17 -O1 -o "$BD/py_state.o" ../../libfemm/py_state.cpp
g++ -c $OURINC -std=c++17 -O1 -o "$BD/py_init_script.o" ../../libfemm/py_init_script.cpp
g++ -c -I../common -std=c++17 -O1 -D'I=CComplex(0,1)' -o "$BD/complex.o" ../common/complex.cpp
g++ -c $MPYINC $OURINC -std=c++17 -O1 -o "$BD/test_bind.o"  ../../tests/test_py_bind.cpp
g++ -c $MPYINC $OURINC -std=c++17 -O1 -o "$BD/test_state.o" ../../tests/test_py_state.cpp
g++ -c $OURINC -std=c++17 -O1 -o "$BD/test_stubs.o" ../../tests/test_stubs.cpp

COMMON_LINK="$BD/py_state.o $BD/py_init_script.o $BD/py_bind.o $BD/py_bind_type.o $BD/complex.o $BD/test_stubs.o $BD/libmpyembed.a"
g++ -o "$BD/test_py_bind"  "$BD/test_bind.o"  $COMMON_LINK
g++ -o "$BD/test_py_state" "$BD/test_state.o" $COMMON_LINK

echo "=== Phase 4: compile-check shared templates vs MagDocument (syntax only) ==="
g++ -fsyntax-only -std=c++17 $MPYINC $OURINC ../../tests/compile_check_templates.cpp \
  && echo "py_editor_common.h / py_postproc_common.h compile OK"

echo "=== Phase 5: compile-check per-physics modules ==="
for f in py_mag_editor py_mag_postproc py_elec_editor py_elec_postproc py_heat_editor py_heat_postproc py_curr_editor py_curr_postproc; do
  g++ -c -std=c++17 $MPYINC $OURINC -o "$BD/$f.o" "../../libfemm/$f.cpp" && echo "$f.cpp OK"
done

echo "=== Phase 10: compile-check py_state_globals + script_callbacks + fkn_py ==="
g++ -c -std=c++17 $MPYINC $OURINC -o "$BD/py_state_globals.o" "../../libfemm/py_state_globals.cpp" && echo "py_state_globals.cpp OK"
g++ -c -std=c++17 $OURINC -o "$BD/script_callbacks.o" "../../libfemm/script_callbacks.cpp" && echo "script_callbacks.cpp OK"
g++ -c -std=c++17 $MPYINC -I../common -I../fkn -o "$BD/fkn_py.o" "../../solvers/fkn/fkn_py.cpp" && echo "fkn_py.cpp OK"

echo "=== Phase 7: syntax-check tcp_server (exec_python wire path) ==="
g++ -fsyntax-only -std=c++17 -I../../ipc -I../../libfemm ../../ipc/tcp_server.cpp \
  && echo "tcp_server.cpp OK"

QTINC=/c/Qt/6.10.3/mingw_64/include
if [ -d "$QTINC/QtWidgets" ]; then
  echo "=== Phase 8: Qt syntax-check py_console.cpp ==="
  g++ -fsyntax-only -std=c++17 -fPIC -I"$QTINC" -I"$QTINC/QtCore" -I"$QTINC/QtGui" \
      -I"$QTINC/QtWidgets" $OURINC ../../gui/py_console.cpp && echo "py_console.cpp OK"
  echo "=== Phase 9: Qt syntax-check py_line_edit.cpp ==="
  g++ -fsyntax-only -std=c++17 -fPIC -I"$QTINC" -I"$QTINC/QtCore" -I"$QTINC/QtGui" \
      -I"$QTINC/QtWidgets" $OURINC -I../common ../../gui/py_line_edit.cpp && echo "py_line_edit.cpp OK"
fi

echo "=== running test_py_bind ==="
"./$BD/test_py_bind"
echo "=== running test_py_state ==="
"./$BD/test_py_state"
