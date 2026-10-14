# XYExportCsvTest.py -- the X-Y plot CSV writer produces files a CSV reader
# reads back as written. CPython; drives femm_headless and the build's compiler.
#
#     python3 XYExportCsvTest.py <path to femm_headless>
#
# xyexport::writeCsv (libfemm/xy_export.h) is the one writer behind the plot
# window's Export... button and every *_makeplot file. It used to separate
# fields with ", " and never quote, so a label holding a comma -- the B-H
# plot's "H, A/m", "Re[B], T" -- split into two header columns and the header
# no longer lined up with the data. The rule now is RFC 4180: a bare comma
# separator, and a field quoted (embedded quotes doubled) exactly when it holds
# a comma, a quote or a line break.
#
# Two checks, because no script can reach the writer with labels of its own
# choosing (makeplot's headings come from fixed quantity tables, and the only
# free-form labels are the GUI's):
#
#   1. QUOTING. A ten-line driver compiled against xy_export.h with the same
#      compiler that built femm-qt (from the build's CMakeCache) writes labels
#      holding commas, quotes, brackets and plain text. Python's csv module must
#      read every header field and every value back exactly.
#   2. END TO END. mo_makeplot of the potential (a phasor) on a 60 Hz problem
#      writes a three-column file through the real engine. The header must have
#      as many fields as every data row, and every data field must be a bare
#      number.

import csv
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
fails = []

if len(sys.argv) < 2:
    print("usage: XYExportCsvTest.py <femm_headless>")
    sys.exit(2)
EXE = os.path.abspath(sys.argv[1])
BUILD = os.path.dirname(os.path.dirname(EXE))      # <build>/bin/femm_headless


def compiler():
    cache = os.path.join(BUILD, "CMakeCache.txt")
    with open(cache, encoding="utf-8", errors="replace") as fh:
        for ln in fh:
            if ln.startswith("CMAKE_CXX_COMPILER:"):
                return ln.split("=", 1)[1].strip()
    raise RuntimeError("no CMAKE_CXX_COMPILER in " + cache)


LABELS = ["Re[B], T", 'say "hi"', "plain label", 'a,b"c', "Im[B], T"]
XLABEL = "H, A/m"
X = [0.0, 1.5, -2.5e-07, 123456.789]
COLS = [[(c + 1) * x - c for x in X] for c in range(len(LABELS))]

DRIVER = r'''
#include "xy_export.h"
int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    std::vector<std::string> labels = { %(labels)s };
    std::vector<double> x = { %(x)s };
    std::vector<std::vector<double>> cols = { %(cols)s };
    return xyexport::writeCsv(argv[1], %(xlabel)s, labels, x, cols) ? 0 : 1;
}
'''


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def check_quoting(tmp):
    src = os.path.join(tmp, "csvdrv.cpp")
    exe = os.path.join(tmp, "csvdrv" + (".exe" if os.name == "nt" else ""))
    with open(src, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(DRIVER % {
            "labels": ", ".join(cstr(l) for l in LABELS),
            "x": ", ".join(repr(v) for v in X),
            "cols": ", ".join("{" + ", ".join(repr(v) for v in col) + "}"
                              for col in COLS),
            "xlabel": cstr(XLABEL),
        })
    cxx = compiler()
    # MinGW's g++ loads its own DLLs from its directory, which need not be on
    # PATH when the suite runs; prepending it is harmless everywhere else.
    env = dict(os.environ)
    env["PATH"] = os.path.dirname(cxx) + os.pathsep + env.get("PATH", "")
    cmd = [cxx, "-std=c++17", "-O0",
           "-I", os.path.join(ROOT, "libfemm"), "-I", os.path.join(ROOT, "common"),
           src, os.path.join(ROOT, "common", "utf8_io.cpp"), "-o", exe]
    if os.name == "nt":
        cmd[1:1] = ["-static"]
        cmd += ["-lshell32"]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        fails.append("driver did not compile:\n" + r.stderr[-1500:])
        return
    out = os.path.join(tmp, "quoting.csv")
    r = subprocess.run([exe, out], capture_output=True, text=True)
    if r.returncode != 0:
        fails.append("driver: writeCsv returned false")
        return
    with open(out, newline="", encoding="utf-8") as fh:
        rows = list(csv.reader(fh))
    if rows[0] != [XLABEL] + LABELS:
        fails.append("quoting: header read back as %r, want %r" % (rows[0], [XLABEL] + LABELS))
    for i, row in enumerate(rows[1:]):
        want = [X[i]] + [col[i] for col in COLS]
        try:
            got = [float(v) for v in row]
        except ValueError:
            fails.append("quoting: data row %d is not all numbers: %r" % (i, row))
            continue
        if len(got) != len(want) or any(abs(g - w) > 1e-12 * max(1.0, abs(w))
                                        for g, w in zip(got, want)):
            fails.append("quoting: data row %d read back as %r, want %r" % (i, got, want))
    if len(rows) != len(X) + 1:
        fails.append("quoting: %d rows, want %d" % (len(rows), len(X) + 1))


SCRIPT = r'''
open("%(fem)s")
mi_saveas("%(tmp)s/xycsv.fem")
mi_probdef(60, "inches", "planar")
mi_analyze()
mi_loadsolution()
mo_addcontour(-0.4, 0)
mo_addcontour(0.4, 0)
mo_makeplot(0, 40, "%(out)s")
mo_close()
mi_close()
'''


def check_makeplot(tmp):
    out = os.path.join(tmp, "makeplot.csv").replace("\\", "/")
    script = os.path.join(tmp, "xycsv.py")
    with open(script, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(SCRIPT % {"fem": os.path.join(HERE, "block.fem").replace("\\", "/"),
                           "tmp": tmp.replace("\\", "/"), "out": out})
    r = subprocess.run([EXE, "-run", script], capture_output=True, text=True, timeout=300)
    if r.returncode != 0 or not os.path.exists(out):
        fails.append("makeplot: engine exit %d, file %s\n%s"
                     % (r.returncode, "written" if os.path.exists(out) else "missing",
                        (r.stdout + r.stderr)[-1500:]))
        return
    with open(out, newline="", encoding="utf-8") as fh:
        rows = list(csv.reader(fh))
    hdr = rows[0]
    if hdr[0] != "Position" or len(hdr) != 4:
        fails.append("makeplot: header %r, want Position plus |A|, Re, Im" % hdr)
    for i, row in enumerate(rows[1:]):
        if len(row) != len(hdr):
            fails.append("makeplot: row %d has %d fields, header has %d" % (i, len(row), len(hdr)))
            break
        if any(v != v.strip() for v in row):
            fails.append("makeplot: row %d has padded fields: %r" % (i, row))
            break
        try:
            [float(v) for v in row]
        except ValueError:
            fails.append("makeplot: row %d is not all numbers: %r" % (i, row))
            break
    if len(rows) < 3:
        fails.append("makeplot: only %d rows" % len(rows))


tmp = tempfile.mkdtemp(prefix="xycsv_")
try:
    check_quoting(tmp)
    check_makeplot(tmp)
finally:
    shutil.rmtree(tmp, ignore_errors=True)

if fails:
    for m in fails:
        print("   " + m)
    print("xy export csv: %d FAILED" % len(fails))
    sys.exit(1)
print("xy export csv: labels with commas and quotes read back exactly through "
      "Python's csv module, and a mo_makeplot file has a header as wide as its "
      "rows with bare numeric fields")
