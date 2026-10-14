# SPDX-License-Identifier: MIT
"""Build FEMM's electrostatic material library (statlib) from sourced data.

Writes ../matlibs/statlib_catalog.dat (the library) and catalog.csv (every
entry with its permittivity, the frequency it was measured at, source, note).

An electrostatic material is a relative permittivity (ex, ey; ey differs only
for anisotropic entries) and a volume charge density qv, always 0 here.
Electrostatics is the zero-frequency limit, so each dielectric takes the
lowest-frequency value its source gives (usually 60 Hz); catalog.csv records
which. The Dorf, Kreith data are shared with the current-flow catalog
(../condlib/*.csv) so that each number lives in one place.

Sources (all held; see SOURCES.md):
  NBS537  A. A. Maryott and F. Buckley, NBS Circular 537 (1953), Sec. 2.2
          recommended values at 20 C and 1 atm
  NBS514  A. A. Maryott and E. R. Smith, NBS Circular 514 (1951)
  DORF    Dorf, Electrical Engineering Handbook 2nd ed. (2000), Tables 55.1, 55.2
  KREITH  Kreith, CRC Handbook of Mechanical Engineering (1999), Table 10A.1.7
  CALL184 Callister and Rethwisch, 10th ed. (2018), Table 18.4
  IOFFE   Ioffe Institute NSM semiconductor database, 'basic parameters' pages
  KAPTON  DuPont, Kapton HN data sheet; DELRIN  Delrin 150 NC010 technical data;
  MYLAR   DuPont Teijin Films, Mylar electrical properties, Fig. 6;
  PYREX   Corning, Glass Silicon Constraint Substrates (Pyrex 7740)
  CRC1317 Haynes, ed., CRC Handbook of Chemistry and Physics, 97th ed. (2016),
          "Dielectric Constant of Selected Polymers", p. 13-17 (natural rubber,
          neoprene)
"""
import csv
import os

HERE = os.path.dirname(os.path.abspath(__file__))
COND = os.path.join(HERE, "..", "condlib")
OUT_DAT = os.path.join(HERE, "..", "matlibs", "statlib_catalog.dat")
OUT_CSV = os.path.join(HERE, "catalog.csv")

SRC = {
    "NBS537": "NBS Circular 537 (1953), Sec. 2.2",
    "NBS514": "NBS Circular 514 (1951)",
    "DORF55.1": "Dorf, Electrical Engineering Handbook 2nd ed. (2000) Table 55.1",
    "DORF55.2": "Dorf, Electrical Engineering Handbook 2nd ed. (2000) Table 55.2",
    "KREITH": "Kreith, CRC Handbook of Mechanical Engineering (1999) Table 10A.1.7",
    "CALL184": "Callister & Rethwisch 10th ed. (2018) Table 18.4",
    "IOFFE": "Ioffe Institute NSM database (www.ioffe.ru/SVA/NSM/Semicond)",
    "KAPTON": "DuPont, Kapton HN polyimide film data sheet",
    "DELRIN": "Delrin 150 NC010 technical data (ASTM D150)",
    "MYLAR": "DuPont Teijin Films, Mylar Electrical Properties, Fig. 6",
    "PYREX": "Corning, Glass Silicon Constraint Substrates (Pyrex 7740)",
    "KAPTONFN": "DuPont, Kapton FN polyimide film technical data sheet",
    "TROVIDUR": "Roechling, Trovidur PN grey (PVC-U) technical data sheet",
    "DORFTEXT": "Dorf, Electrical Engineering Handbook 2nd ed. (2000) Ch. 55 text, p. 1370",
    "USGS": "G. R. Olhoeft, USGS Open-File Report 79-993 (1979), Table 2",
    "CRC1317": "CRC Handbook of Chemistry and Physics 97th ed. (2016) p. 13-17",
}
entries = []   # (folder tuple, name, ex, ey, frequency, source, note)


def add(folder, name, ex, ey=None, freq="static", src="", note=""):
    entries.append((tuple(folder), name, float(ex), float(ey if ey is not None else ex), freq, src, note))


for r in csv.DictReader(open(os.path.join(HERE, "gases_nbs537.csv"), encoding="utf-8")):
    add(("Gases (20 C, 1 atm)",), r["name"], 1 + float(r["eps_minus_1_ppm"]) * 1e-6, src="NBS537",
        note="(eps-1) x 10^6 = %s" % r["eps_minus_1_ppm"])

# Water and the polar organics (eps > 10) conduct: even ultrapure water at 25 C
# (5.5 uS/m, eps 78.3; ../condlib/catalog.csv) relaxes free charge in
# eps0 eps/sigma = 0.13 ms, so at DC it is a conductor and its permittivity
# applies only well above 1/(2 pi tau). Entries carry no comment field, so the
# folder name is the only warning a user sees; DC problems belong in current flow.
WATER = "Water (high-frequency limit; DC: use current flow)"
POLAR = "Polar Organic Liquids (high-frequency limit)"
NONPOLAR = "Low-Permittivity Organic Liquids"
for r in csv.DictReader(open(os.path.join(HERE, "liquids_nbs514.csv"), encoding="utf-8")):
    note = "%s C" % r["temp_C"] + ("; " + r["note"] if r["note"] else "")
    sub = WATER if r["folder"] == "Water" else POLAR if float(r["eps"]) > 10 else NONPOLAR
    add(("Liquids", sub), r["name"], r["eps"], src="NBS514", note=note)

for r in csv.DictReader(open(os.path.join(COND, "liquids_dorf.csv"), encoding="utf-8")):
    add(("Liquids", "Insulating Oils"), r["name"], r["er_60Hz"], freq="60 Hz", src="DORF55.1",
        note="25 C" + ("; " + r["note"] if r["note"] else ""))

CERAMIC = {"Alumina (Al2O3)", "Porcelain (mullite)", "Steatite", "Magnesium oxide (MgO)",
           "Glass (soda lime)", "Mica"}
FILM = {"SiO2 film", "Si3N4", "Ta2O5", "HfO2"}
for r in csv.DictReader(open(os.path.join(COND, "insulants_dorf.csv"), encoding="utf-8")):
    for tag, lab in (("60Hz", "60 Hz"), ("1kHz", "1 kHz"), ("1MHz", "1 MHz")):
        if r["er_" + tag]:
            break
    sub = "Ceramics, Glass and Stone" if r["name"] in CERAMIC else \
          "Thin-Film Dielectrics" if r["name"] in FILM else "Polymers"
    note = r["note"].split("; rho:")[0].replace("rho: ", "") if r["note"] else ""
    note = "" if note.startswith("Callister") else note
    add(("Solid Insulation", sub), r["name"], r["er_" + tag], freq=lab, src="DORF55.2", note=note)

for r in csv.DictReader(open(os.path.join(COND, "laminates_kreith.csv"), encoding="utf-8")):
    add(("Circuit Board Laminates",), r["name"], r["er_1MHz"], freq="1 MHz", src="KREITH", note=r["note"])

for r in csv.DictReader(open(os.path.join(HERE, "other_sources.csv"), encoding="utf-8")):
    folder = ("Semiconductors",) if r["folder"] == "Semiconductors" else ("Solid Insulation", r["folder"])
    freq = r["note"].split(",")[0].split(";")[0] if r["src"] in ("CALL184", "KAPTON", "DELRIN", "MYLAR", "PYREX", "KAPTONFN", "TROVIDUR", "USGS", "DORFTEXT", "CRC1317") else "static"
    add(folder, r["name"], r["ex"], r["ey"], freq=freq, src=r["src"], note=r["note"])

names = [e[1] for e in entries]
dups = {n for n in names if names.count(n) > 1}
assert not dups, dups

ORDER = ["Gases (20 C, 1 atm)", "Liquids", "Solid Insulation", "Circuit Board Laminates", "Semiconductors"]
SUB = {"Liquids": [WATER, POLAR, NONPOLAR, "Insulating Oils"],
       "Solid Insulation": ["Ceramics, Glass and Stone", "Polymers", "Thin-Film Dielectrics"]}
tree = {}
for e in entries:
    tree.setdefault(e[0], []).append(e)

out = []
emitted = []   # entries in library order, so catalog.csv groups by folder


def emit(path):
    out.append("<BeginFolder>\n<FolderName> = \"%s\"\n" % path[-1])
    for e in tree.get(path, []):
        emitted.append(e)
        _, name, ex, ey, *_ = e
        out.append("<BeginBlock>\n<BlockName> = \"%s\"\n<ex> = %.8g\n<ey> = %.8g\n<qv> = 0\n<EndBlock>\n\n"
                   % (name, ex, ey))
    for k in SUB.get(path[-1], []):
        if path + (k,) in tree:
            emit(path + (k,))
    out.append("<EndFolder>\n\n")


# "Vacuum" first, above the folders, as matlib.dat puts "Air": a convenience
# entry, relative permittivity 1 by definition, so no source is needed
out.append("<BeginBlock>\n<BlockName> = \"Vacuum\"\n<ex> = 1\n<ey> = 1\n<qv> = 0\n<EndBlock>\n\n")
for top in ORDER:
    emit((top,))
open(OUT_DAT, "w", encoding="utf-8", newline="\r\n").write("".join(out))

with open(OUT_CSV, "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["folder", "name", "eps_x", "eps_y", "frequency", "source", "note"])
    assert len(emitted) == len(entries)
    for folder, name, ex, ey, freq, src, note in emitted:
        w.writerow(["/".join(folder), name, "%.8g" % ex, "%.8g" % ey, freq, SRC[src], note])
written = sum(1 for e in entries if e[0] in tree and (e[0][0] in ORDER))
print("%d entries plus Vacuum -> %s" % (len(entries), OUT_DAT))
