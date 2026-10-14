# SPDX-License-Identifier: MIT
"""check_ss_430.py -- the annealed stainless family against Carpenter's 1991
430F / 430FR curves, which are not members of it.

Curves: the user's digitization Stainless/Type430Scans.xlsx of the DC
magnetization curves on sheets 15-16 of Carpenter's Alloy Data -- Soft
Magnetic Alloys (2-91): 430F mill-annealed bar at Rockwell B 75/82 and
82/91, 430FR mill-annealed bar by diameter.  For each curve the family is
given only that curve's own maximum permeability and the grade's composition
saturation (ss_build.js_of), and predicts J at 10 and 100 Oe and the knee.

Result (2026-09-29): J at 10 Oe within 0.03 T of all four; knee RMS 0.08-0.38
in ln H below 0.8 Js; at 100 Oe the 1991 curves lie 0.10-0.15 T below the
family, a disagreement with Carpenter's current Chrome Core chart (430FR 1.48 T
at 100 Oe) and Valbruna's MG2, not a failure of the shape.  Kept as
validation, not as members (user's decision).

Usage:  python check_ss_430.py
"""
import json
import numpy as np
import openpyxl

import ss_build as B

MU0 = 4e-7 * np.pi
OE = 1000 / (4 * np.pi)


def main():
    mst = json.load(open("out/ss_fit.json"))["master"]
    u, x, cm = np.array(mst["u"]), np.array(mst["x"]), mst["c_M"]
    wb = openpyxl.load_workbook("Stainless/Type430Scans.xlsx", data_only=True)
    for ws in wb.worksheets:
        d = np.array([r for r in ws.iter_rows(min_row=2, values_only=True)
                      if r[0] is not None], float)
        h, b = d[:, 0] * OE, d[:, 1] / 10
        j = b - MU0 * h
        grade = "430FR" if ws.title.startswith("430FR") else "430F"
        js = B.js_of(*B.OX[grade])
        mumax = (b / (MU0 * h)).max()
        h50 = cm * js / (MU0 * (mumax - 1))
        fam = lambda hq: js * np.interp(np.log(hq / h50), np.log(u), x)
        lo = j < 0.8 * js
        e = np.log(h[lo]) - np.log(h50 * np.exp(np.interp(j[lo] / js, x,
                                                          np.log(u))))
        print("%-18s mu_max %5.0f | J at 10 Oe %.3f vs %.3f | at 100 Oe "
              "%.3f vs %.3f | knee ln H mean %+.2f RMS %.2f"
              % (ws.title, mumax, np.interp(10 * OE, h, j), fam(10 * OE),
                 np.interp(100 * OE, h, j), fam(100 * OE), e.mean(),
                 np.sqrt((e ** 2).mean())))


if __name__ == "__main__":
    main()
