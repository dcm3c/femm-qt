# Run every regression suite. Portable: no absolute paths, so this works from
# a checkout anywhere and on any platform.
#
#     bash _verify_all.sh
#
# Two path forms are in play on Windows and they are not interchangeable. This
# script runs under Git Bash, which speaks /c/..., while the femm binaries are
# native Windows programs that need C:/... -- `pwd -W` is what converts, and it
# simply fails on macOS and Linux, where `pwd` is already right. Getting this
# wrong is silent: the binary starts and then cannot find the script it was
# handed.
cd "$(dirname "$0")" || exit 1
T=$(pwd -W 2>/dev/null || pwd)                  # this directory, as femm sees it

# The build tree, likewise in both forms. FEMM_BUILD overrides for an
# out-of-source build.
BUILD=${FEMM_BUILD:-../build}
if [ ! -d "$BUILD/bin" ]; then
    echo "no build at $BUILD/bin -- build first, or set FEMM_BUILD"
    exit 1
fi
BIN=$(cd "$BUILD/bin" && (pwd -W 2>/dev/null || pwd))

X=""
[ -x "$BUILD/bin/femm_headless.exe" ] && X=".exe"
B="$BIN/femm_headless$X"
FKN="$BIN/fkn$X"

# macOS ships everything inside femm_gui.app, and the bundle is what a user
# runs -- so the suite exercises the copies in Contents/MacOS/, not the loose
# build outputs in bin/. They are the same binaries, but not the same exeDir(),
# and exeDir() is where the solvers, bin/python, the PyBridge worker and the
# material libraries are all resolved from. Testing bin/ tests a layout nobody
# ships.
#
# The .app directory is NOT evidence the bundle is usable: gui/CMakeLists.txt
# sets MACOSX_BUNDLE, so CMake creates it at link time and a bare `ninja` leaves
# it holding only femm_gui. macos_build.sh is what copies the solvers and
# femm_headless in. So test for one of those, and say so out loud when it is
# missing rather than quietly falling back to the copies that do not ship.
APPBIN="$BIN/femm_gui.app/Contents/MacOS"
if [ -d "$BUILD/bin/femm_gui.app" ]; then
    GUI="$APPBIN/femm_gui"
    if [ -x "$BUILD/bin/femm_gui.app/Contents/MacOS/femm_headless" ]; then
        B="$APPBIN/femm_headless"
        FKN="$APPBIN/fkn"
        # femm_gui is linked straight into the bundle by CMake (MACOSX_BUNDLE),
        # so it is always current after a build. Everything else in there was
        # COPIED by macos_build.sh. If femm_gui is the newer of the two, someone
        # ran `ninja` without reassembling and the suite is about to test stale
        # binaries -- which it cannot otherwise tell, since they exist and run.
        if [ "$BUILD/bin/femm_gui.app/Contents/MacOS/femm_gui" -nt \
             "$BUILD/bin/femm_gui.app/Contents/MacOS/femm_headless" ]; then
            echo "STALE: femm_gui is newer than the copies inside femm_gui.app."
            echo "       Run ./macos_build.sh; ninja alone does not"
            echo "       reassemble the bundle, and these rows would test the"
            echo "       previous build."
            exit 1
        fi
    else
        echo "NOTE: femm_gui.app holds no femm_headless -- run ./macos_build.sh"
        echo "      to assemble it. Falling back to bin/, which is NOT the"
        echo "      layout that ships."
    fi
else
    GUI="$BIN/femm_gui$X"
fi

if [ ! -x "$B" ]; then
    echo "no femm_headless at $B -- build first"
    exit 1
fi

# The status-line filter is ANCHORED (^) on purpose. femm_headless writes its
# status to stderr and script output to stdout, and these rows merge the two
# with 2>&1 -- so if a status line were ever to land mid-line, an unanchored
# pattern would delete the whole joined line, taking a line of real output with
# it and leaving the line COUNT intact. That is not hypothetical: it happened
# on Ubuntu, and it turned a perfect run into "torque = 47595" via
#     torque = 0.38579Script complete, exiting.   <- deleted, prefix and all
#     47595 0                                     <- kept, looks like a number
# The cause is fixed (setvbuf in ipc/headless_main.cpp), but anchoring is what
# makes a recurrence LOUD -- the joined line survives the filter and the row
# fails on structure instead of quietly reporting a wrong value.

# Scratch files go beside the goldens, not in /tmp: a couple of the comparers
# read them back by relative name.
TMP=${TMPDIR:-/tmp}

# --- Did a row fail, and say so loudly if it did ------------------------
#
# This exists because a macOS pass was read as GREEN while FQReference was
# failing, and neither the row nor the script's exit status contradicted it.
# Two separate defects, both of which had been here from the start:
#
#   * every bare row printed `... | tail -1`, and a comparator does not have to
#     put its verdict last. FQReferenceCompare.py prints "REFERENCE CHECK
#     FAILED: N suite(s) exceed the budget" and then lists the offenders, so
#     tail -1 kept the TWELFTH violation -- a plausible number, visually
#     indistinguishable from a pass.
#   * the script ended with `echo "ALL DONE"`, so its exit status was echo's:
#     zero however many rows had failed. No caller could tell.
#
# 50b7a36 fixed exactly the first of these for inproc() and did not carry it to
# the other two row shapes. This carries it to all of them.
#
# Two signals, because neither covers everything: the comparator's exit status
# (only 30 of the 65 scripts here call sys.exit, but FQReferenceCompare is one)
# and a scan for failure markers. The markers are deliberately CASE-SENSITIVE --
# RunScriptSize's passing verdict contains the words "used to fail" -- and were
# checked against a full green run, where they match nothing.
#
# One row is EXPECTED to fail, on one platform, and has been since long before
# any of this. Saying so here is not a way of hiding it: it is reported, with
# the reason, on every run. What it must not do is colour the exit status --
# otherwise macOS ends every single run red, and a permanent red is worth
# exactly nothing. Training a reader to skip past a failing row is how the
# misreading that prompted all of this happened in the first place.
#
# An expected failure that starts PASSING is also reported, because that means
# the platform's arithmetic has changed and somebody should want to know.
expected_fail(){ # label
  [ "$(uname -s 2>/dev/null)" = "Darwin" ] && [ "$1" = "FQReference" ]
}
#
# The reason is one line because it goes in a headline; the long version --
# clang contracts on arm64, FMA changes the MESH rather than the last digits,
# and every budget in the comparator assumes both runs meshed identically -- is
# in FQReferenceCompare.py from line 60, along with the 2026-08-19 decision that
# FMA stays on. Do not turn it off to make this green.
expected_why(){ # label
  echo "expected on macOS: FMA changes the mesh -- FQReferenceCompare.py:60"
}

FAILED_ROWS=""
ROW_BAD=0

# Decide, and record. Printing is left to the caller, because a failing row
# wants a different headline from a passing one.
row_verdict(){ # label rc output
  ROW_BAD=0
  [ "$2" -ne 0 ] && ROW_BAD=1
  case "$3" in
    *FAIL*|*"Script error"*|*SHORT:*|*PROBEFAIL*) ROW_BAD=1 ;;
  esac
  printf '%s\n' "$3" | grep -qE '[1-9][0-9]* flagged' && ROW_BAD=1
  if expected_fail "$1"; then
      # 2 means "failed, and that is the documented outcome here"; 3 means the
      # documented outcome stopped happening. Neither touches FAILED_ROWS.
      [ "$ROW_BAD" -eq 1 ] && ROW_BAD=2 || ROW_BAD=3
  elif [ "$ROW_BAD" -eq 1 ]; then
      FAILED_ROWS="$FAILED_ROWS $1"
  fi
  return 0
}

# The whole tail, not one line: the verdict is in there somewhere and the point
# of all this is that we cannot predict where.
row_detail(){ printf '%s\n' "$1" | tail -25 | sed 's/^/    | /'; }

run(){ # test compare golden fq
  $B -run "$T/$1" > "$TMP/$4.raw" 2>&1
  grep -vE "^(FEMM headless|IPC:|Executing:|Script complete)" "$TMP/$4.raw" > "$4"
  printf "%-26s " "${1%.py}:"
  out=$(python3 "$2" "$3" "$4" 2>&1); rc=$?
  row_verdict "${1%.py}" "$rc" "$out"
  case "$ROW_BAD" in
    1) echo "FAILED"; row_detail "$out" ;;
    2) echo "FAILED ($(expected_why))"; row_detail "$out" ;;
    3) echo "PASSED -- but it is documented to FAIL here; see FQReferenceCompare.py"
       printf '%s\n' "$out" | head -1 ;;
    *) printf '%s\n' "$out" | head -1 ;;
  esac
}

# A row driven by a python comparer rather than by the engine directly. Was a
# bare `python3 X.py | tail -1` at each call site; that is what discarded the
# FQReference verdict.
pyrow(){ # label command...
  _label=$1; shift
  printf "%-26s " "$_label:"
  out=$("$@" 2>&1); rc=$?
  row_verdict "$_label" "$rc" "$out"
  case "$ROW_BAD" in
    1) echo "FAILED"; row_detail "$out" ;;
    2) echo "FAILED ($(expected_why))"; row_detail "$out" ;;
    3) echo "PASSED -- but it is documented to FAIL here; see FQReferenceCompare.py"
       printf '%s\n' "$out" | tail -1 ;;
    *) printf '%s\n' "$out" | tail -1 ;;
  esac
}

# Runs inside the interpreter rather than driving it, so it needs no comparer.
inproc(){ # script label
  printf "%-26s " "$2:"
  # The engine's own exit status counts. It used to be dropped (row_verdict
  # was handed a literal 0), so a suite that SEGFAULTED printed nothing, had
  # no failure marker to find, and read as green -- which is precisely how a
  # use-after-free shows up half the time (ListdirGcTest on the unfixed
  # converter: three crashes in six runs). Captured through a file because
  # the status of a command substitution's pipeline is not otherwise ours.
  out=$($B -run "$T/$1" 2>&1; echo "$?" > "$TMP/inproc.rc")
  rc=$(cat "$TMP/inproc.rc" 2>/dev/null || echo 1)
  out=$(printf "%s\n" "$out" | grep -vE "^(FEMM headless|IPC:|Executing:|Script complete)")
  [ "$rc" -ne 0 ] && out=$(printf "%s\nengine exited with status %s\n" "$out" "$rc")
  last=$(printf "%s\n" "$out" | tail -1)
  printf "%s\n" "$last"
  # A suite that dies says WHY on the lines ABOVE its last one -- and tail -1
  # threw exactly those away. A red row on a remote machine then read only
  # "Script error: 1", which costs a round trip to the machine under test
  # before the real diagnosis can even start.
  row_verdict "$2" "$rc" "$out"
  if [ "$ROW_BAD" -eq 1 ]; then
      row_detail "$out"
  fi
}

run MOBlockIntegralTest.py   MOBlockIntegralCompare.py   OF_result.txt      fq_result.txt
run ElecBlockIntegralTest.py EOBlockIntegralCompare.py   OF_elec_result.txt fq_elec_result.txt
run HeatBlockIntegralTest.py HeatBlockIntegralCompare.py OF_heat_result.txt fq_heat_result.txt
run CurrBlockIntegralTest.py CurrBlockIntegralCompare.py OF_curr_result.txt fq_curr_result.txt
run MOLineIntegralTest.py    MOLineIntegralCompare.py    OF_line_result.txt  fq_line_result.txt
run EOLineIntegralTest.py    EOLineIntegralCompare.py    OF_eline_result.txt fq_eline_result.txt
run CILineIntegralTest.py    CILineIntegralCompare.py    OF_iline_result.txt fq_iline_result.txt
run HILineIntegralTest.py    HILineIntegralCompare.py    OF_hline_result.txt fq_hline_result.txt
run MOGapIntegralTest.py     MOGapIntegralCompare.py     OF_gap_result.txt   fq_gap_result.txt
run MOCircuitPropTest.py     MOCircuitPropCompare.py     OF_circ_result.txt  fq_circ_result.txt
run EOCondPropTest.py        CondPropCompare.py          OF_eic_result.txt   fq_eic_result.txt
run COCondPropTest.py        CondPropCompare.py          OF_cic_result.txt   fq_cic_result.txt
run HOCondPropTest.py        CondPropCompare.py          OF_hic_result.txt   fq_hic_result.txt
run MOPointValueTest.py      MOPointValueCompare.py      OF_pv_result.txt    fq_pv_result.txt
run MOBHCurveTest.py         MOBHCurveCompare.py         OF_bh_result.txt    fq_bh_result.txt
run EOPointValueTest.py      ScalarPointValueCompare.py  OF_eopv_result.txt  fq_eopv_result.txt
run COPointValueTest.py      ScalarPointValueCompare.py  OF_copv_result.txt  fq_copv_result.txt
run HOPointValueTest.py      ScalarPointValueCompare.py  OF_hopv_result.txt  fq_hopv_result.txt

pyrow FluxConstraint python3 FluxConstraintTest.py "$B"
pyrow Transient python3 TransientTest.py "$FKN" "$B"
pyrow IncrementalLorentz python3 IncrementalLorentzTest.py "$B"
inproc ACBHCurveTest.py ACBHCurve
pyrow FQReference python3 FQReferenceCompare.py
# Runs inside the interpreter rather than driving it, so it needs no comparer.
inproc OsModuleTest.py OsModule
inproc StdlibModuleTest.py StdlibModules
inproc UlabInt32Test.py UlabInt32
inproc GcTest.py GcTest
inproc ListdirGcTest.py ListdirGc
inproc DateTimeTest.py DateTime
inproc CsvTest.py Csv
inproc PylibTest.py Pylib
pyrow RunScriptSize python3 RunScriptSizeTest.py "$B"
pyrow RedirectedOutput python3 RedirectedOutputTest.py "$B"
pyrow ScriptArgv python3 ScriptArgvTest.py "$B"
pyrow XYExportCsv python3 XYExportCsvTest.py "$B"
inproc ProxyDunderTest.py ProxyDunders
inproc PointLocationTest.py PointLocation
inproc MagDirTransformTest.py MagDirTransform
inproc NodePropsTransformTest.py NodePropsTransform
inproc PointFillFactorTest.py PointFillFactor
inproc CurrentFlowContrastTest.py CurrentFlowContrast
inproc DxfImportTest.py DxfImport
inproc CreateRadiusTest.py     CreateRadius
inproc FftTest.py Fft
inproc NumpyProxyTest.py NumpyProxy
inproc NumpyComplexTest.py NumpyComplex
inproc BulkGetterTest.py BulkGetters
inproc MaterialLibTest.py MaterialLib
inproc MaterialLibHistoryTest.py MaterialLibHistory
inproc SaveBeforeRunTest.py SaveBeforeRun
inproc CreateMeshTest.py CreateMesh
# Documentation drift, both manuals against the shipped wrappers and against
# the engine. Driven from outside because it asks the ENGINE which commands
# exist -- which manual should document a wrapper is not decidable from its
# name, and guessing put ~30 commands in the wrong bucket.
pyrow DocDrift python3 DocDriftTest.py "$B"
# The Lua binding: FEMM 4.2 compatibility, and the launcher nothing else tests.
# It had no coverage at all until 2026-09-11, and the first look found the
# launcher missing from the macOS distribution, all three examples broken, and
# 56 unwrapped 4.2 commands.
pyrow LuaFemm python3 LuaFemmTest.py "$B"
# The matplotlib bridge. All three SKIP cleanly when no usable CPython is
# installed -- it is an optional capability, not a build requirement.
inproc PyBridgeHostTest.py PyBridgeHost
inproc PyBridgeClientTest.py PyBridgeClient
inproc PyBridgeBenchTest.py PyBridgeBench
pyrow PyBridgeInterrupt python3 PyBridgeInterruptTest.py "$B"
pyrow PyBridgeDiscovery python3 PyBridgeDiscoveryTest.py "$B"
pyrow PyBridgeNested python3 PyBridgeNestedTest.py "$GUI"
# Needs the GUI, and needs it started the way Explorer starts it -- with no
# console behind it. Every other row here inherits this shell's descriptors,
# so this is the only one that can see output escaping the sink.
pyrow ConsolelessPrint python3 ConsolelessPrintTest.py "$GUI"
if [ -n "$FAILED_ROWS" ]; then
    echo
    echo "FAILED:$FAILED_ROWS"
    exit 1
fi
echo "ALL DONE"
