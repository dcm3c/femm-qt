# SPDX-License-Identifier: MIT
"""run_all.py -- regenerate every result in this package, then build the papers.

Run from the package root:

    python run_all.py              everything: fits, libraries, figures, PDFs
    python run_all.py --no-pdf     skip LaTeX
    python run_all.py --wiki       also write <paper>.md and .txt (tex2wiki)
    python run_all.py --only silicon    run only one folder's steps

Each paper has its own folder.  Every step runs with its folder as the
working directory, so the scripts' relative paths (out/..., femm_*.csv)
resolve inside it; common/ holds the shared modules, and the cobalt-iron
and nickel-iron folders are also on the import path because other papers
import their build modules.

The order matters in two places: the iron entries are emitted before the
low-carbon library files that carry them, and the low-carbon fit runs
before the iron figure that compares with it; the cobalt-iron points are
written before the nickel-iron, stainless and Metglas builds read them.
Expected non-zero exits are listed in EXPECT.
"""
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
PYPATH = os.pathsep.join(os.path.join(ROOT, d) for d in
                         ("common", "cobalt-iron", "nickel-iron"))

STEPS = [
    ("low-carbon", "bh_fit.py"),
    ("iron", "emit_measured.py"),           # ARMCO pure iron and Telar 57
    ("low-carbon", "emit_matlib.py"),       # carries the iron entries too
    ("low-carbon", "emit_fem.py"),
    ("low-carbon", "compare_scans.py"),
    ("low-carbon", "make_levelaudit.py"),
    ("low-carbon", "check_ordering.py"),
    ("iron", "make_ironfig.py"),
    ("silicon", "si_composition.py"),
    ("silicon", "si_measured.py"),
    ("silicon", "bh_fit_si.py"),
    ("silicon", "emit_si_matlib.py"),
    ("silicon", "emit_si_fem.py"),
    ("silicon", "compare_scans_si.py"),
    ("silicon", "compare_dimax.py"),
    ("silicon", "si_crosscheck.py"),
    ("silicon", "si_basis.py"),
    ("silicon", "check_ordering_si.py"),
    ("cobalt-iron", "cofe_points.py"),
    ("nickel-iron", "nife_build.py"),
    ("nickel-iron", "emit_nife_fem.py"),
    ("nickel-iron", "make_nifefig.py"),
    ("stainless", "ss_build.py"),
    ("stainless", "ph_build.py"),
    ("stainless", "emit_ss_fem.py"),
    ("stainless", "emit_ph_fem.py"),
    ("stainless", "make_ssfig.py"),
    ("stainless", "check_ph.py"),
    ("stainless", "check_ss_430.py"),
    ("cobalt-iron", "cofe_build.py"),
    ("cobalt-iron", "emit_cofe_fem.py"),
    ("cobalt-iron", "make_cofefig.py"),
    ("metglas", "metglas_build.py"),
    ("metglas", "emit_metglas_fem.py"),
    ("metglas", "make_metglasfig.py"),
    ("ceramic", "cer_check.py"),
    ("libraries", "condlib/build_condlib.py"),
    ("libraries", "statlib_src/build_statlib.py"),
    ("libraries", "provenance/make_provenance.py"),
]

# check_ordering_si reports two documented ordering defects of the silicon
# family (Type 2-S 0.024 in, below 0.7 T; see sicurves) and exits 1.
EXPECT = {"check_ordering_si.py": 1}

PAPERS = [("low-carbon", "bhcurves"), ("iron", "ironcurves"),
          ("silicon", "sicurves"), ("nickel-iron", "nifecurves"),
          ("stainless", "sscurves"), ("cobalt-iron", "cofecurves"),
          ("metglas", "metglascurves"), ("ceramic", "ceramiccurves"),
          ("libraries/provenance", "condlib_provenance"),
          ("libraries/provenance", "statlib_provenance"),
          ("libraries/provenance", "heatlib_provenance"),
          ("magnets", "AlnicoProperties"), ("magnets", "SinteredNdFeBProperties"),
          ("magnets", "BondedNdFeB"), ("magnets", "SmCoProperties")]


def run(cmd, cwd, log):
    with open(log, "w", encoding="utf-8") as f:
        env = dict(os.environ, PYTHONPATH=PYPATH, PYTHONIOENCODING="utf-8",
                   MPLBACKEND="Agg")
        return subprocess.run(cmd, cwd=cwd, stdout=f, stderr=subprocess.STDOUT,
                              env=env).returncode


def main():
    args = sys.argv[1:]
    only = args[args.index("--only") + 1] if "--only" in args else None
    logdir = os.path.join(ROOT, "logs")
    os.makedirs(logdir, exist_ok=True)
    bad = []
    for folder, script in STEPS:
        if only and folder != only:
            continue
        cwd = os.path.join(ROOT, folder)
        os.makedirs(os.path.join(cwd, "out"), exist_ok=True)
        name = "%s__%s" % (folder, script.replace("/", "_")[:-3])
        t0 = time.time()
        rc = run([sys.executable, os.path.join(cwd, script)], cwd,
                 os.path.join(logdir, name + ".txt"))
        ok = rc == EXPECT.get(os.path.basename(script), 0)
        print("%-12s %-32s %5.0f s  %s" % (folder, script, time.time() - t0,
              "ok" if ok else "EXIT %d  (see logs/%s.txt)" % (rc, name)))
        if not ok:
            bad.append("%s/%s" % (folder, script))
    if "--no-pdf" not in args and not only:
        for folder, stem in PAPERS:
            cwd = os.path.join(ROOT, folder)
            for _ in range(3):                # references settle by the third pass
                rc = run(["pdflatex", "-interaction=nonstopmode", stem + ".tex"],
                         cwd, os.path.join(logdir, "latex_%s.txt" % stem))
            print("%-12s %-32s %s" % ("latex", "%s/%s.tex" % (folder, stem),
                                      "ok" if rc == 0 else "EXIT %d" % rc))
            if rc:
                bad.append(stem)
    if "--wiki" in args:
        for folder, stem in PAPERS[:8]:
            run([sys.executable, os.path.join(ROOT, "common", "tex2wiki.py"), stem],
                os.path.join(ROOT, folder), os.path.join(logdir, "wiki_%s.txt" % stem))
        print("wiki         <paper>.md, <paper>.txt in each folder")
    print("\n%s" % ("all steps ok" if not bad else "FAILED: " + ", ".join(bad)))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
