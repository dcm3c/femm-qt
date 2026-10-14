# SPDX-License-Identifier: MIT
"""Build FEMM's current-flow material library (condlib) from sourced data.

Writes ../matlibs/condlib_catalog.dat (the library) and catalog.csv (every
entry with its values, frequency, source and notes).

FEMM's current-flow material model (femm-qt manual, cflow.tex): the problem is
solved at one frequency f; each material has conductivity sigma (S/m), relative
permittivity eps_r and loss tangent tan(delta), combined as
    sigma_eff = sigma + j*2*pi*f*eps0*eps_r*exp(-j*phi),  tan(phi) = tan(delta),
where eps_r is the MAGNITUDE |eps|/eps0 of the complex permittivity, not its
real part. Sources tabulate the real part eps' and the dissipation factor, so
eps_r is written as eps' * sqrt(1 + tan(delta)^2).

Sources (all held; see SOURCES.md):
  DESK      ASM Metals Handbook, Desk Edition, 2nd ed., 1998, Table 4,
            "Electrical conductivity and resistivity of metals and alloys"
  CALL      W. D. Callister and D. G. Rethwisch, Materials Science and
            Engineering: An Introduction, 10th ed., 2018, Table B.9
  DORF      R. Dorf (ed.), The Electrical Engineering Handbook, 2nd ed., CRC,
            2000, Ch. 55 (Bartnikas), Tables 55.1 and 55.2
  KREITH    F. Kreith (ed.), The CRC Handbook of Mechanical Engineering, CRC,
            1999, Table 10A.1.7
  P527      ITU-R P.527-6 (09/2021), eqs. (8)-(13), (17), (22)-(27)
  P368      ITU-R P.368-9 (02/2007), Figures 1-11
  LIGHT     T. S. Light et al., Electrochem. Solid-State Lett. 8 (2005) E16
  EDDY      Eddy Current Technology Inc., conductivity table (Wayback 2012)
"""
import csv
import math
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DAT = os.path.join(HERE, "..", "matlibs", "condlib_catalog.dat")
OUT_CSV = os.path.join(HERE, "catalog.csv")

SRC = {
    "DESK": "ASM Metals Handbook Desk Edition (1998) Table 4",
    "CALL": "Callister & Rethwisch 10th ed. (2018) Table B.9",
    "DORF55.1": "Dorf, Electrical Engineering Handbook 2nd ed. (2000) Table 55.1",
    "DORF55.2": "Dorf, Electrical Engineering Handbook 2nd ed. (2000) Table 55.2",
    "KREITH": "Kreith, CRC Handbook of Mechanical Engineering (1999) Table 10A.1.7",
    "P527": "ITU-R P.527-6 (2021) eqs. 8-13, 17, 22-27",
    "P368": "ITU-R P.368-9 (2007)",
    "LIGHT": "Light et al., Electrochem. Solid-State Lett. 8, E16 (2005)",
    "EDDY": "eddy-current.com conductivity table (Wayback 2012-05-03)",
    "IACS": "International Annealed Copper Standard (1.7241 microhm-cm = 58.0 MS/m)",
}

entries = []   # (folder path tuple, name, sigma, eps_real, tand, freq, source, note)


def add(folder, name, sigma=0.0, eps=1.0, tand=0.0, freq="DC-1 MHz", src="", note=""):
    entries.append((tuple(folder), name, float(sigma), float(eps), float(tand), freq, src, note))


def mid(s):
    """'5.7-7.8' -> 6.75 (arithmetic middle); '2.9' -> 2.9."""
    if "-" in s:
        a, b = (float(x) for x in s.split("-"))
        return (a + b) / 2, True
    return float(s), False


# ---------------------------------------------------------------- metals ----
M = ("Metals and Alloys",)
add(M + ("Pure Metals",), "Copper (IACS standard)", 58.0e6, src="IACS",
    note="annealed copper standard, 100% IACS by definition")
add(M + ("Pure Metals",), "Aluminum, 99.99%", 3.767e7, src="EDDY",
    note="'Aluminum, 99.99%' 64.94% IACS, 2.655e-8 ohm-m")

# Desk Edition Table 4: heading -> (folder, name prefix); None = left out.
DESK_MAP = {
    "Aluminum and aluminum alloys": ("Aluminum Alloys", "Aluminum, "),
    "Copper and copper alloys / Wrought copper": ("Copper and Copper Alloys", ""),
    "Free-machining copper": ("Copper and Copper Alloys", "Free-machining copper, "),
    "Wrought alloys": ("Copper and Copper Alloys", ""),
    "Casting alloys": ("Copper and Copper Alloys", "Cast "),
    "Electrical contact materials / Copper alloys": ("Copper and Copper Alloys", "Contact copper, "),
    "Silver and silver alloys": ("Silver Alloys", ""),
    "Platinum and platinum alloys": ("Platinum and Palladium Alloys", ""),
    "Palladium and palladium alloys": ("Platinum and Palladium Alloys", ""),
    "Gold and gold alloys": ("Gold Alloys", ""),
    "Electrical heating alloys / Ni-Cr and Ni-Cr-Fe alloys": ("Resistance and Heating Alloys", ""),
    "Fe-Cr-Al alloys": ("Resistance and Heating Alloys", ""),
    "Fe-Cr-Al alloy": ("Resistance and Heating Alloys", ""),
    "Pure metals": ("Pure Metals", ""),
    "Nonmetallic heating element materials": None,          # handled below
    "Instrument and control alloys / Cu-Ni alloys": ("Resistance and Heating Alloys", ""),
    "Cu-Mn-Ni alloys": ("Resistance and Heating Alloys", ""),
    "Nickel-base alloys": ("Resistance and Heating Alloys", ""),
    "Relay steels and alloys after annealing / Low-carbon iron and steel": ("Steels and Irons", ""),
    "Silicon steels": ("Steels and Irons", "Silicon steel, "),
    "Stainless steels": ("Stainless Steels", "Stainless, "),
    "Stainless and heat-resistant alloys": ("Stainless Steels", ""),
}
RENAME = {"Platinum": "Platinum", "99.8 Ni": "Nickel, 99.8%", "Iron (99.99%)": "Iron, 99.99%",
          "1010 steel": "Steel, 1010 (relay, annealed)", "Low-carbon iron": "Low-carbon iron (relay, annealed)",
          "Pure copper": "Copper, pure", "Fine silver": "Silver, fine", "Gold": "Gold",
          "Palladium": "Palladium", "HH": "Cast heat-resistant alloy HH",
          "HK": "Cast heat-resistant alloy HK", "HT": "Cast heat-resistant alloy HT"}
seen = set()
for r in csv.DictReader(open(os.path.join(HERE, "metals_desk.csv"), encoding="utf-8")):
    fm = DESK_MAP.get(r["headings"], "absent")
    if fm is None or fm == "absent":
        continue
    folder, prefix = fm
    nm = r["name"]
    if r["headings"] == "Stainless and heat-resistant alloys" and nm.startswith("Type"):
        nm = "Stainless, " + nm
    nm = re.sub(r"\([a-e]\)$", "", nm).strip()
    name = RENAME.get(nm, prefix + nm)
    if name == "Nickel, 99.8%":
        folder = "Pure Metals"
    if name in seen:            # Platinum is listed twice (10.6 and 10.64)
        continue
    seen.add(name)
    rho, rng = mid(r["rho_uohm_cm"])
    note = "p. %s" % r["pdf_page"]
    if rng:
        note += "; resistivity %s microhm-cm (middle used)" % r["rho_uohm_cm"]
    add(M + (folder,), name, 1e8 / rho, src="DESK", note=note)

for r in csv.DictReader(open(os.path.join(HERE, "metals_callister.csv"), encoding="utf-8")):
    add(M + (r["folder"],), r["name"], 1.0 / float(r["rho_ohm_m"]), src="CALL", note=r["note"])

# --------------------------------------------- nonmetallic conductors -------
N = ("Nonmetallic Conductors",)
add(N, "Silicon carbide heating element", 1e8 / 150, src="DESK",
    note="resistivity 100-200 microhm-cm (middle used)")
add(N, "Molybdenum disilicide (MoSi2)", 1e8 / 37.24, src="DESK")
add(N, "Graphite (heating element)", 1e8 / 910.1, src="DESK")
add(N, "Graphite, extruded (with grain)", 1 / 13.5e-6, src="CALL",
    note="7-20 x 10^-6 ohm-m (middle used)")
add(N, "Graphite, isostatically molded", 1 / 14e-6, src="CALL",
    note="10-18 x 10^-6 ohm-m (middle used)")

# ---------------------------------------------------- dielectrics -----------
for r in csv.DictReader(open(os.path.join(HERE, "insulants_dorf.csv"), encoding="utf-8")):
    sig = 1.0 / float(r["rho_ohm_m"]) if r["rho_ohm_m"] else 0.0
    for tag, lab in (("60Hz", "60 Hz"), ("1kHz", "1 kHz"), ("1MHz", "1 MHz")):
        e, d = r["er_" + tag], r["df_" + tag]
        if e and d:
            add(("Solid Insulation", lab), "%s (%s)" % (r["name"], lab), sig, float(e), float(d),
                freq=lab, src="DORF55.2", note=r["note"])

for r in csv.DictReader(open(os.path.join(HERE, "liquids_dorf.csv"), encoding="utf-8")):
    note = "dielectric constant at 25 C, dissipation factor at 100 C"
    if r["note"]:
        note += "; " + r["note"]
    add(("Insulating Liquids (60 Hz)",), "%s (60 Hz)" % r["name"], 0.0, float(r["er_60Hz"]),
        float(r["df_60Hz"]), freq="60 Hz", src="DORF55.1", note=note)

for r in csv.DictReader(open(os.path.join(HERE, "laminates_kreith.csv"), encoding="utf-8")):
    add(("Circuit Board Laminates (1 MHz)",), "%s (1 MHz)" % r["name"], 0.0, float(r["er_1MHz"]),
        float(r["df_1MHz"]), freq="1 MHz", src="KREITH", note=r["note"])

# ------------------------------------------------------------ water --------


def eps_static(T):
    """P.527-6 eq. (8): static permittivity of pure water, T in C."""
    return 77.66 + 103.3 * (300.0 / (T + 273.15) - 1.0)


def eps_static_sea(T, S):
    """P.527-6 eq. (17)."""
    return eps_static(T) * math.exp(-3.33330e-3 * S + 4.74868e-6 * S * S)


def sigma_sea(T, S):
    """P.527-6 eqs. (22)-(27), S/m."""
    s35 = 2.903602 + 8.607e-2 * T + 4.738817e-4 * T ** 2 - 2.991e-6 * T ** 3 + 4.3047e-9 * T ** 4
    r15 = S * (37.5109 + 5.45216 * S + 1.4409e-2 * S ** 2) / (1004.75 + 182.283 * S + S ** 2)
    a0 = (6.9431 + 3.2841 * S - 9.9486e-2 * S ** 2) / (84.850 + 69.024 * S + S ** 2)
    a1 = 49.843 - 0.2276 * S + 0.198e-2 * S ** 2
    return s35 * r15 * (1 + a0 * (T - 15) / (a1 + T))


W = ("Water",)
LIGHT_RHO = {20: 23.85, 25: 18.18}       # MOhm-cm, Light et al. Table I
for T in (20, 25):
    add(W, "Pure water, %d C" % T, 1.0 / (LIGHT_RHO[T] * 1e6 * 1e-2), eps_static(T),
        src="P527+LIGHT",
        note="eps_r: P.527-6 eq. 8 (static; Debye relaxation above 17 GHz, so constant below 1 MHz); "
             "sigma: ultrapure water %.2f MOhm-cm (Light et al.)" % LIGHT_RHO[T])
for T in (0, 10, 20):
    add(W, "Sea water, 35 ppt, %d C" % T, sigma_sea(T, 35.0), eps_static_sea(T, 35.0), src="P527",
        note="P.527-6 eqs. 17 and 22-27, salinity 35 g/kg; frequency-independent below 1 MHz")
for r in csv.DictReader(open(os.path.join(HERE, "ground_itu.csv"), encoding="utf-8")):
    add((r["folder"],), r["name"], float(r["sigma_S_m"]), float(r["er"]), freq="10 kHz-30 MHz",
        src="P368", note="Figure %s; standard ground constants for ground-wave curves" % r["figure"])

# ------------------------------------------------------------- write --------
ORDER = ["Metals and Alloys", "Nonmetallic Conductors", "Water", "Soil and Ground",
         "Insulating Liquids (60 Hz)", "Solid Insulation", "Circuit Board Laminates (1 MHz)"]
SUB = {"Solid Insulation": ["60 Hz", "1 kHz", "1 MHz"],
       "Metals and Alloys": ["Pure Metals", "Copper and Copper Alloys", "Aluminum Alloys",
                             "Silver Alloys", "Gold Alloys", "Platinum and Palladium Alloys",
                             "Steels and Irons", "Cast Irons", "Stainless Steels",
                             "Nickel and Cobalt Alloys", "Titanium Alloys", "Magnesium Alloys",
                             "Other Metals", "Resistance and Heating Alloys"]}

names = [e[1] for e in entries]
dups = {n for n in names if names.count(n) > 1}
assert not dups, dups


def block(e):
    _, name, sig, eps, tand, *_ = e
    er = eps * math.sqrt(1 + tand * tand)
    return ("<BeginBlock>\n<BlockName> = \"%s\"\n<ox> = %.6g\n<oy> = %.6g\n<ex> = %.6g\n"
            "<ey> = %.6g\n<ltx> = %.6g\n<lty> = %.6g\n<EndBlock>\n\n"
            % (name, sig, sig, er, er, tand, tand))


tree = {}
for e in entries:
    tree.setdefault(e[0], []).append(e)

lines = []
def emit_folder(path, depth):
    lines.append("<BeginFolder>\n<FolderName> = \"%s\"\n" % path[-1])
    for e in tree.get(path, []):
        lines.append(block(e))
    kids = sorted({p[depth + 1] for p in tree if len(p) > depth + 1 and p[:depth + 1] == path},
                  key=lambda k: SUB[path[-1]].index(k))
    for k in kids:
        emit_folder(path + (k,), depth + 1)
    lines.append("<EndFolder>\n\n")


for top in ORDER:
    emit_folder((top,), 0)
open(OUT_DAT, "w", encoding="utf-8", newline="\r\n").write("".join(lines))

with open(OUT_CSV, "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["folder", "name", "sigma_S_per_m", "eps_real", "tan_delta", "eps_r_written",
                "frequency", "source", "note"])
    for e in entries:
        folder, name, sig, eps, tand, freq, src, note = e
        w.writerow(["/".join(folder), name, "%.6g" % sig, "%.6g" % eps, "%.6g" % tand,
                    "%.6g" % (eps * math.sqrt(1 + tand * tand)), freq,
                    " + ".join(SRC[s] for s in src.split("+")), note])
print("%d entries -> %s" % (len(entries), OUT_DAT))
