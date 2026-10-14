# SPDX-License-Identifier: MIT
"""cofe_points.py -- every tabulated B(H) point for the cobalt-iron alloys, with
its source, as out/cofe_points.csv.

Vendor tables of B at stated fields, for stated anneals, are the data here;
they need no digitizing and say exactly what was measured.  Curves (the
Vacuumschmelze and Carpenter figures) are not included yet.  Each row is
one point: group, source, grade, condition, yield strength (MPa, where
stated), H (A/m), B (T), and a note.  Where a source tabulates polarization
J instead of B, B = J + mu0 H is stored and the note says so.

Groups, as the library entries will be:
  49V_mag    49Co-49Fe-2V after the anneal for magnetic properties
  49V_rotor  49Co-49Fe-2V after a strength anneal, ~450-600 MPa yield
  49V_hs     49Co-49Fe-2V at the high-strength end, ~680-950 MPa yield
  27Co       Fe-27Co
  18Co       Fe-17/18Co

Sources (SOURCES.md): VAC = Vacuumschmelze product information, 2021;
H50 = Carpenter Hiperco 50 datasheet 1990 (same table in the 12-90 Alloy
Data sheet); H50A = Carpenter Hiperco 50A Alloy Data (scan); H50HS =
Carpenter Hiperco 50 HS datasheet 1995; H27 = Carpenter Hiperco 27
datasheet 1995; AFK = Aperam Imphy AFK brochure 2022, page-4 table (the
page-6 table contradicts it and is not used).

Usage:  python cofe_points.py   -> out/cofe_points.csv
"""
import numpy as np

MU0 = 4e-7 * np.pi
OE = 1000.0 / (4 * np.pi)
rows = []


def add(group, source, grade, cond, mpa, h, b, note=""):
    rows.append((group, source, grade, cond, mpa, float(h), float(b), note))


def add_j(group, source, grade, cond, mpa, h, j, note=""):
    add(group, source, grade, cond, mpa, h, j + MU0 * h,
        ("J=%.3f tabulated; " % j) + note)


def add_kg_oe(group, source, grade, cond, mpa, pairs, note=""):
    for hoe, kg in pairs:
        add(group, source, grade, cond, mpa, hoe * OE, kg / 10.0, note)


VAC_H = [300, 800, 1600, 4000, 8000, 16000]
# ---- VAC strip, 0.35 mm, after final anneal (p.8) -------------------------
for grade, cond, mpa, bs, grp in [
        ("VACOFLUX 50", "magnetic anneal", 250,
         [1.90, 2.10, 2.20, 2.255, 2.28, 2.30], "49V_mag"),
        ("VACODUR 49", "optimum magnetic", 210,
         [1.90, 2.10, 2.20, 2.255, 2.28, 2.30], "49V_mag"),
        ("VACODUR 49", "optimum mechanical", 390,
         [1.80, 2.05, 2.15, 2.25, 2.27, 2.30], "49V_rotor"),
        ("VACODUR 50", "optimum mechanical", 450,
         [1.70, 2.00, 2.11, 2.19, 2.23, 2.26], "49V_rotor"),
        ("VACODUR S Plus", "optimum mechanical", 800,
         [None, 1.20, 1.80, 2.03, 2.11, 2.15], "49V_hs"),
        ("VACOFLUX 17", "final anneal", 250,
         [1.2, 1.5, 1.6, 1.75, 1.9, 2.05], "18Co")]:
    for h, b in zip(VAC_H, bs):
        if b is not None:
            add(grp, "VAC p.8", grade, cond, mpa, h, b, "strip 0.35 mm")
# ---- VAC solid material, J at 10-40 kA/m (p.9) ----------------------------
for grade, js, grp in [("VACOFLUX 50", [2.27, 2.28, 2.28, 2.29], "49V_mag"),
                       ("VACOFLUX 27", [2.11, 2.22, 2.27, 2.38], "27Co"),
                       ("VACOFLUX 17", [1.94, 2.04, 2.09, 2.19], "18Co")]:
    for h, j in zip([10000, 16000, 20000, 40000], js):
        add_j(grp, "VAC p.9", grade, "solid, final anneal", None, h, j)

# ---- Carpenter ---------------------------------------------------------
for t, vals in (("0.006 in", [21.2, 23.0, 23.6, 24.4]),
                ("0.010 in", [21.6, 22.9, 23.5, 24.2]),
                ("0.014 in", [21.7, 23.0, 23.6, 24.3])):
    add_kg_oe("49V_mag", "H50 p.6", "Hiperco 50", "1600F 2h, 180F/h", None,
              zip([10, 50, 100, 200], vals), "strip " + t + ", ASTM A596 ring")
OE50A = [2, 4, 6, 10, 50, 100, 200]
add_kg_oe("49V_mag", "H50A p.2", "Hiperco 50A", "strip 1600F", None,
          zip(OE50A, [15.5, 18.6, 20.0, 21.0, 22.7, 22.9, 23.5]))
add_kg_oe("49V_mag", "H50A p.2", "Hiperco 50A", "bar 1600F", None,
          zip(OE50A, [10.0, 15.5, 18.0, 20.0, 22.5, 22.8, 23.0]))
add_kg_oe("49V_mag", "H50A p.2", "Hiperco 50A", "bar 1850F WQ + 1600F", None,
          zip(OE50A, [16.0, 19.0, 20.2, 21.0, 22.7, 22.8, 23.0]))
add_kg_oe("49V_mag", "H50A p.2", "Hiperco 50A", "bar 1700F slow cool", None,
          zip(OE50A, [11.0, 17.5, 19.0, 20.0, 22.5, 22.8, 23.0]))
OEHS = [10, 20, 50, 100, 200]
for mpa, vals, grp in ((503, [19.3, 20.7, 21.9, 22.3, 23.0], "49V_rotor"),
                       (593, [19.0, 20.3, 21.8, 22.3, 22.9], "49V_rotor"),
                       (683, [18.2, 19.9, 21.5, 22.3, 22.9], "49V_hs")):
    add_kg_oe(grp, "H50HS p.4", "Hiperco 50 HS", "strength anneal", mpa,
              zip(OEHS, vals), "strip, ASTM A596 ring")
OE27 = [10, 50, 100, 150, 200, 250]
for cond, vals in (("strip 0.010 in, 1400F 4h", [15.0, 19.4, 21.0, 22.0, 22.9, 23.6]),
                   ("strip 0.014 in, 1400F 4h", [15.2, 18.9, 20.1, 21.2, 22.1, 22.6]),
                   ("bar 6 in, 1553F 4h", [15.2, 19.3, 20.7, 21.6, 22.3, 22.9]),
                   ("slab 9x4 in, 1553F 4h", [14.3, 19.0, 20.5, 21.6, 22.2, 22.8])):
    add_kg_oe("27Co", "H27 p.2", "Hiperco 27", cond, None, zip(OE27, vals))

# ---- Imphy, page-4 table ------------------------------------------------
for t in ("0.35 mm", "0.20 mm", "0.10 mm"):
    add("49V_mag", "AFK p.4", "AFK 502 R", "annealed", 300, 2500, 2.25, t)
    add("49V_mag", "AFK p.4", "AFK 502 R", "annealed", 300, 10000, 2.32, t)
for t, b25, b10 in (("0.35 mm", 1.87, 2.10), ("0.20 mm", 1.85, 2.05)):
    add("27Co", "AFK p.4", "AFK 1", "annealed", 300, 2500, b25, t)
    add("27Co", "AFK p.4", "AFK 1", "annealed", 300, 10000, b10, t)
for t in ("0.35 mm", "0.20 mm"):
    add("18Co", "AFK p.4", "AFK 18", "annealed", 210, 2500, 1.85, t)
    add("18Co", "AFK p.4", "AFK 18", "annealed", 210, 10000, 2.05, t)
for mpa in (600, 800, 950):
    add("49V_rotor" if mpa == 600 else "49V_hs", "AFK p.5", "AFK 502 FP",
        "fully processed", mpa, 10000, 2.13, "0.35 mm")

# ---- stated saturation, J_s (T), for reference --------------------------
JS = {"VACOFLUX 50": 2.30, "VACODUR 49": 2.30, "VACODUR 50": 2.28,
      "VACODUR S Plus": 2.25, "VACOFLUX 27": 2.38, "VACOFLUX 17": 2.22,
      "AFK 502 R": 2.35, "AFK 1": 2.38, "AFK 18": 2.30,
      "Hiperco 27": 2.36}

if __name__ == "__main__":
    with open("out/cofe_points.csv", "w", newline="\n") as f:
        f.write("group,source,grade,condition,yield_MPa,H_Am,B_T,note\n")
        for r in rows:
            f.write('%s,%s,"%s","%s",%s,%.1f,%.4f,"%s"\n'
                    % (r[0], r[1], r[2], r[3], "" if r[4] is None else r[4],
                       r[5], r[6], r[7]))
    import collections
    c = collections.Counter(r[0] for r in rows)
    print("wrote out/cofe_points.csv:", dict(c))
