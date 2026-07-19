#!/usr/bin/env python3
"""
MOBHCurveCompare.py -- diff two MOBHCurveTest runs.

block.fem is a prescribed-A cube of M-19 Steel, so B == Bdes everywhere with zero
mesh dependence -- mo_getpointvalues(0,0) isolates the B-H curve / permeability
evaluation. femm-qt and OF must therefore agree to solver precision at every
Bdes, so this is a *tight* check (default 1e-5 = 0.001% tol), unlike the mesh-
dependent point suite. Measured agreement is ~4e-7, limited only by the near-zero
B1 cross-component; the meaningful values (mu, H, E) agree tighter. It exists
because it caught a real bug: the DC GetMu double-counted the lamination fill
factor (a ~2% systematic mu/H error in nonlinear laminated iron), which 1e-5
catches with 2000x margin.

Each row is keyed by Bdes with the 14 values as "re im" pairs. B1/B2 are
normalized by local |B|, H1/H2 by local |H|, the rest by per-column dominant.

Usage:
    femm_headless -run MOBHCurveTest.py > fq_bh_result.txt
    python3 MOBHCurveCompare.py OF_bh_result.txt fq_bh_result.txt [tol]
"""
import sys
import MOPointValueCompare as PV   # identical parse/normalization; only the tol differs

if __name__ == "__main__":
    ref  = sys.argv[1] if len(sys.argv) > 1 else "OF_bh_result.txt"
    test = sys.argv[2] if len(sys.argv) > 2 else "fq_bh_result.txt"
    tol  = float(sys.argv[3]) if len(sys.argv) > 3 else 1e-5
    sys.exit(1 if PV.compare(ref, test, tol) else 0)
