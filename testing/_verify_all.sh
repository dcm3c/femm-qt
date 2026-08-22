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

# macOS builds the GUI as a bundle; the executable is inside it.
if [ -d "$BUILD/bin/femm_gui.app" ]; then
    GUI="$BIN/femm_gui.app/Contents/MacOS/femm_gui"
else
    GUI="$BIN/femm_gui$X"
fi

if [ ! -f "$BUILD/bin/femm_headless$X" ]; then
    echo "no femm_headless in $BUILD/bin -- build first"
    exit 1
fi

# Scratch files go beside the goldens, not in /tmp: a couple of the comparers
# read them back by relative name.
TMP=${TMPDIR:-/tmp}

run(){ # test compare golden fq
  $B -run "$T/$1" > "$TMP/$4.raw" 2>&1
  grep -vE "FEMM headless|IPC:|Executing:|Script complete" "$TMP/$4.raw" > "$4"
  printf "%-26s " "${1%.py}:"; python3 "$2" "$3" "$4" 2>&1 | head -1
}

# Runs inside the interpreter rather than driving it, so it needs no comparer.
inproc(){ # script label
  printf "%-26s " "$2:"
  $B -run "$T/$1" 2>&1 \
    | grep -vE "FEMM headless|IPC:|Executing:|Script complete" | tail -1
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

printf "%-26s " "FluxConstraint:"; python3 FluxConstraintTest.py "$B" | tail -1
printf "%-26s " "Transient:"; python3 TransientTest.py "$FKN" "$B" | tail -1
printf "%-26s " "IncrementalLorentz:"; python3 IncrementalLorentzTest.py "$B" | tail -1
printf "%-26s " "FQReference:"; python3 FQReferenceCompare.py | tail -1
# Runs inside the interpreter rather than driving it, so it needs no comparer.
inproc OsModuleTest.py OsModule
inproc StdlibModuleTest.py StdlibModules
inproc UlabInt32Test.py UlabInt32
inproc DateTimeTest.py DateTime
inproc CsvTest.py Csv
inproc PylibTest.py Pylib
printf "%-26s " "RunScriptSize:"; python3 RunScriptSizeTest.py "$B" | tail -1
inproc ProxyDunderTest.py ProxyDunders
inproc PointLocationTest.py PointLocation
inproc MagDirTransformTest.py MagDirTransform
inproc FftTest.py Fft
# The matplotlib bridge. All three SKIP cleanly when no usable CPython is
# installed -- it is an optional capability, not a build requirement.
inproc PyBridgeHostTest.py PyBridgeHost
inproc PyBridgeClientTest.py PyBridgeClient
inproc PyBridgeBenchTest.py PyBridgeBench
printf "%-26s " "PyBridgeInterrupt:"; python3 PyBridgeInterruptTest.py "$B" | tail -1
printf "%-26s " "PyBridgeDiscovery:"; python3 PyBridgeDiscoveryTest.py "$B" | tail -1
printf "%-26s " "PyBridgeNested:"; python3 PyBridgeNestedTest.py "$GUI" | tail -1
# Needs the GUI, and needs it started the way Explorer starts it -- with no
# console behind it. Every other row here inherits this shell's descriptors,
# so this is the only one that can see output escaping the sink.
printf "%-26s " "ConsolelessPrint:"; python3 ConsolelessPrintTest.py "$GUI" | tail -1
echo "ALL DONE"
