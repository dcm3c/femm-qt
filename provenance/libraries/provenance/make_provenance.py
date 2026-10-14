# SPDX-License-Identifier: MIT
"""Write per-entry provenance documents for FEMM's three non-magnetic libraries.

    condlib_provenance.tex   from ../condlib/catalog.csv
    statlib_provenance.tex   from ../statlib_src/catalog.csv
    heatlib_provenance.tex   from ../matlibs/heatlib_fixed.dat, the femm.info
                             'specificheat' page (../matlibs/femm-specificheat-
                             20261001.html) and heat_kcheck.csv

Each document is short: what the library is and how it was built, the
sources, one table row per entry, and the corrections. The tables are
generated, so they cannot drift from the libraries; rerun after any rebuild.
"""
import csv
import html
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")

PREAMBLE = r"""%% SPDX-License-Identifier: CC-BY-4.0
\documentclass[10pt]{article}
\usepackage[margin=0.8in]{geometry}
\usepackage{longtable,booktabs,array}
\usepackage[T1]{fontenc}
\usepackage{lmodern}
\usepackage{textcomp}
\usepackage[hidelinks]{hyperref}
\setlength{\LTcapwidth}{\textwidth}
\renewcommand{\arraystretch}{1.08}
\newcolumntype{L}[1]{>{\raggedright\arraybackslash}p{#1}}
\title{%s}
\author{David Meeker}
\date{%s}
\begin{document}
\maketitle
"""


def esc(s):
    """Escape text for LaTeX; keep a few typographic conveniences."""
    s = str(s)
    rep = {"\\": r"\textbackslash{}", "&": r"\&", "%": r"\%", "$": r"\$", "#": r"\#",
           "_": r"\_", "{": r"\{", "}": r"\}", "~": r"\textasciitilde{}", "^": r"\textasciicircum{}"}
    s = "".join(rep.get(ch, ch) for ch in s)
    s = s.replace("+-", r"$\pm$").replace("<=", r"$\le$").replace(">=", r"$\ge$")
    s = s.replace(" <", r" $<$").replace(" >", r" $>$")
    s = re.sub(r"10\\textasciicircum\{\}(-?\d+)", r"10$^{\1}$", s)
    s = s.replace("uOhm", r"$\mu\Omega$").replace("microhm", r"$\mu\Omega$")
    return s


def num(x, sig=4):
    """Compact number: plain for ordinary magnitudes, scientific otherwise."""
    v = float(x)
    if v == 0:
        return "0"
    if 1e-3 <= abs(v) < 1e5:
        return ("%." + str(sig) + "g") % v
    m, e = ("%.3e" % v).split("e")
    return r"$%s\times10^{%d}$" % (("%g" % float(m)), int(e))


def table(rows, cols, spec, folder_key):
    """Longtable with a full-width folder row whenever the folder changes."""
    out = [r"{\small", r"\begin{longtable}[l]{@{}%s@{}}" % spec, r"\toprule",
           " & ".join(r"\textbf{%s}" % c for c in cols) + r" \\", r"\midrule", r"\endhead",
           r"\bottomrule", r"\endlastfoot"]
    last = None
    for r in rows:
        if r[folder_key] != last:
            last = r[folder_key]
            out.append(r"\multicolumn{%d}{@{}l}{\textit{%s}} \\" % (len(cols), esc(last)))
        out.append(" & ".join(r["cells"]) + r" \\")
    out += [r"\end{longtable}", "}"]
    return "\n".join(out)


def bib(items):
    out = [r"\begin{thebibliography}{99}"]
    out += [r"\bibitem{%s} %s" % (k, v) for k, v in items]
    out.append(r"\end{thebibliography}")
    return "\n".join(out)


LICENSE = (r"\noindent Copyright 2026 David Meeker. This document is licensed under the "
           r"\href{https://creativecommons.org/licenses/by/4.0/}{Creative Commons Attribution "
           r"4.0 International License (CC BY 4.0)}.")


def write(name, title, date, body):
    p = os.path.join(HERE, name)
    open(p, "w", encoding="utf-8", newline="\n").write(
        PREAMBLE % (title, date) + body + "\n\n" + LICENSE + "\n\n\\end{document}\n")
    print("wrote", p)


DATE = "1 October 2026"

# =============================================================== condlib ====
CONDSRC = {
    "ASM Metals Handbook Desk Edition (1998) Table 4": "desk",
    "Callister & Rethwisch 10th ed. (2018) Table B.9": "callister",
    "Dorf, Electrical Engineering Handbook 2nd ed. (2000) Table 55.1": "dorf",
    "Dorf, Electrical Engineering Handbook 2nd ed. (2000) Table 55.2": "dorf",
    "Kreith, CRC Handbook of Mechanical Engineering (1999) Table 10A.1.7": "kreith",
    "ITU-R P.527-6 (2021) eqs. 8-13, 17, 22-27": "p527",
    "ITU-R P.368-9 (2007)": "p368",
    "Light et al., Electrochem. Solid-State Lett. 8, E16 (2005)": "light",
    "eddy-current.com conductivity table (Wayback 2012-05-03)": "eddy",
    "International Annealed Copper Standard (1.7241 microhm-cm = 58.0 MS/m)": "iacs",
}
COND_BIB = [
    ("desk", r"J. R. Davis (ed.), \emph{ASM Metals Handbook, Desk Edition}, 2nd ed., ASM International, 1998, Table 4, ``Electrical conductivity and resistivity of metals and alloys.''"),
    ("callister", r"W. D. Callister and D. G. Rethwisch, \emph{Materials Science and Engineering: An Introduction}, 10th ed., Wiley, 2018, Table B.9, pp.~A-27 to A-29."),
    ("dorf", r"R. C. Dorf (ed.), \emph{The Electrical Engineering Handbook}, 2nd ed., CRC Press, 2000, Ch.~55 (R. Bartnikas), Tables 55.1 and 55.2."),
    ("kreith", r"F. Kreith (ed.), \emph{The CRC Handbook of Mechanical Engineering}, CRC Press, 1999, Table 10A.1.7."),
    ("p527", r"Recommendation ITU-R P.527-6, \emph{Electrical characteristics of the surface of the Earth}, ITU, 2021, eqs.~(8)--(13), (17), (22)--(27)."),
    ("p368", r"Recommendation ITU-R P.368-9, \emph{Ground-wave propagation curves for frequencies between 10 kHz and 30 MHz}, ITU, 2007 (superseded), Figs.~1--11."),
    ("light", r"T. S. Light, S. Licht, A. C. Bevilacqua and K. R. Morash, ``The fundamental conductivity and resistivity of water,'' \emph{Electrochem. Solid-State Lett.} 8 (2005) E16--E19, Table I."),
    ("eddy", r"Eddy Current Technology Inc., ``Conductivity of metals sorted by resistivity,'' \url{http://www.eddy-current.com/condres.htm}, Wayback Machine snapshot of 3 May 2012."),
    ("iacs", r"International Annealed Copper Standard: 100\% IACS = 1.7241 $\mu\Omega$\,cm = 58.0 MS/m, by definition."),
]


def cond_doc():
    rows = list(csv.DictReader(open(os.path.join(ROOT, "condlib", "catalog.csv"), encoding="utf-8")))
    out = []
    for r in rows:
        keys = [CONDSRC[s.strip()] for s in r["source"].split(" + ")]
        out.append(dict(folder=r["folder"].replace("/", " / "), cells=[
            esc(r["name"]), num(r["sigma_S_per_m"]), num(r["eps_r_written"]),
            num(r["tan_delta"], 3), "\\cite{%s}" % ",".join(keys), esc(r["note"])]))
    body = r"""
\section*{The library}

\texttt{condlib\_catalog.dat} is a current-flow material library for FEMM, %d entries in %d folders, built 1 October 2026. A current-flow problem is solved at one frequency $f$, and each material is a conductivity $\sigma$ (S/m), a relative permittivity $\varepsilon_r$ and a loss tangent $\tan\delta$, combined as $\sigma_\mathrm{eff} = \sigma + j\omega\varepsilon_0\varepsilon_r e^{-j\phi}$ with $\tan\phi = \tan\delta$. FEMM takes $\varepsilon_r$ as the \emph{magnitude} of the complex relative permittivity, so the library holds $\varepsilon'\sqrt{1+\tan^2\delta}$ where a source gives $\varepsilon'$; for all but a few lossy entries the two agree to better than 0.3\%%.

Dielectrics have one entry per tabulated frequency (60 Hz, 1 kHz, 1 MHz), named accordingly; metals, water and ground are frequency-independent over the range FEMM's current-flow solver is used for. Where a source gives a range, the entry uses its middle and the note says so; where it gives a bound, the bound is used. Leakage conductivity of insulators is from volume resistivity where one exists, otherwise zero.

The library is generated by \texttt{condlib/build\_condlib.py} from one CSV file per source; \texttt{condlib/catalog.csv} lists every entry, and the table below is generated from it. \texttt{SOURCES.md} records each source document with its SHA-256 hash.

\section*{Liquids}

Unlike the electrostatic and heat-flow libraries, this library needs no caution on its liquid entries. The current-flow solver models conduction and displacement current together, so water, sea water and the insulating liquids are represented correctly at any frequency their entries cover. The electrostatic library's water folder points users here for that reason.

\section*{Entries}

""" % (len(rows), len({r["folder"] for r in rows}) + 2)
    body += table(out, ["Entry", r"$\sigma$ (S/m)", r"$\varepsilon_r$", r"$\tan\delta$", "Src.", "Note"],
                  "L{2.1in}rrrlL{1.95in}", "folder")
    body += "\n\n" + bib(COND_BIB)
    write("condlib_provenance.tex", "Provenance of the Current-Flow Material Library", DATE, body)


# =============================================================== statlib ====
STATSRC = {
    "NBS Circular 537 (1953), Sec. 2.2": "nbs537",
    "NBS Circular 514 (1951)": "nbs514",
    "Dorf, Electrical Engineering Handbook 2nd ed. (2000) Table 55.1": "dorf",
    "Dorf, Electrical Engineering Handbook 2nd ed. (2000) Table 55.2": "dorf",
    "Kreith, CRC Handbook of Mechanical Engineering (1999) Table 10A.1.7": "kreith",
    "Callister & Rethwisch 10th ed. (2018) Table 18.4": "callister",
    "Ioffe Institute NSM database (www.ioffe.ru/SVA/NSM/Semicond)": "ioffe",
    "DuPont, Kapton HN polyimide film data sheet": "kapton",
    "Delrin 150 NC010 technical data (ASTM D150)": "delrin",
    "DuPont Teijin Films, Mylar Electrical Properties, Fig. 6": "mylar",
    "Corning, Glass Silicon Constraint Substrates (Pyrex 7740)": "pyrex",
    "DuPont, Kapton FN polyimide film technical data sheet": "kaptonfn",
    "Roechling, Trovidur PN grey (PVC-U) technical data sheet": "trovidur",
    "Dorf, Electrical Engineering Handbook 2nd ed. (2000) Ch. 55 text, p. 1370": "dorf",
    "G. R. Olhoeft, USGS Open-File Report 79-993 (1979), Table 2": "usgs",
    "CRC Handbook of Chemistry and Physics 97th ed. (2016) p. 13-17": "crcstat",
}
STAT_BIB = [
    ("nbs537", r"A. A. Maryott and F. Buckley, \emph{Table of Dielectric Constants and Electric Dipole Moments of Substances in the Gaseous State}, NBS Circular 537, 1953, Sec.~2.2."),
    ("nbs514", r"A. A. Maryott and E. R. Smith, \emph{Table of Dielectric Constants of Pure Liquids}, NBS Circular 514, 1951."),
    ("dorf", COND_BIB[2][1] + r" Also the text of Ch.~55, p.~1370."),
    ("kreith", COND_BIB[3][1]),
    ("callister", r"W. D. Callister and D. G. Rethwisch, \emph{Materials Science and Engineering: An Introduction}, 10th ed., Wiley, 2018, Table 18.4, p.~683."),
    ("ioffe", r"Ioffe Institute, \emph{New Semiconductor Materials} database, ``basic parameters'' pages, \url{http://www.ioffe.ru/SVA/NSM/Semicond/}, accessed 1 October 2026."),
    ("kapton", r"DuPont, \emph{Kapton HN polyimide film}, technical data sheet."),
    ("kaptonfn", r"DuPont, \emph{Kapton FN polyimide film}, technical data sheet."),
    ("delrin", r"\emph{Delrin 150 NC010 acetal (POM-H)}, technical data (ASTM D150)."),
    ("mylar", r"DuPont Teijin Films, \emph{Mylar Electrical Properties}, Fig.~6 (values read from the graph)."),
    ("pyrex", r"Corning, \emph{Glass Silicon Constraint Substrates} (Pyrex 7740)."),
    ("trovidur", r"R\"ochling, \emph{Trovidur PN grey} (PVC-U), technical data sheet."),
    ("usgs", r"G. R. Olhoeft, \emph{Tables of Room Temperature Electrical Properties for Selected Rocks and Minerals with Dielectric Permittivity Statistics}, USGS Open-File Report 79-993, 1979, Table 2."),
    ("crcstat", r"W. M. Haynes, ed., \emph{CRC Handbook of Chemistry and Physics}, 97th ed., CRC Press, 2016: Permittivity (Dielectric Constant) of Water at Various Frequencies (p.~6-15, from Archer and Wang, JPCRD 1983), of Liquids (6-199, C.~Wohlfarth, from Landolt-B\"ornstein IV/6, 1991), of Gases (6-221, from NBS Circular 537 and Harvey and Lemmon 2005) and of Inorganic Solids (12-48, from Young and Frederikse, JPCRD 1973); Dielectric Constants of Glasses (12-58); Dielectric Constant of Selected Polymers (13-17, ``typical or average values''). Source of the natural-rubber and neoprene entries; otherwise used as a check."),
]


def stat_doc():
    rows = list(csv.DictReader(open(os.path.join(ROOT, "statlib_src", "catalog.csv"), encoding="utf-8")))
    crc = {r["name"]: r for r in csv.DictReader(open(os.path.join(ROOT, "statlib_src", "crc_check.csv"), encoding="utf-8"))}
    assert set(crc) <= {r["name"] for r in rows}, set(crc) - {r["name"] for r in rows}
    out, diffs = [], {}
    for r in rows:
        e = num(r["eps_x"], 8) if r["eps_x"] == r["eps_y"] else "%s / %s" % (num(r["eps_x"]), num(r["eps_y"]))
        chk = "--"
        c = crc.get(r["name"])
        if c:
            lib, ref = float(r["eps_x"]), float(c["crc_eps"])
            if ref < 1.01:   # gases: compare eps - 1
                d = (lib - 1) / (ref - 1) - 1
            else:
                d = lib / ref - 1
            diffs[r["name"]] = d
            chk = "%s (%s, %+.0f\\%%%s)" % (num(c["crc_eps"], 8), c["table"], 100 * d, "$^*$" if ref < 1.01 else "")
        out.append(dict(folder=r["folder"].replace("/", " / "), cells=[
            esc(r["name"]), e, esc(r["frequency"]), "\\cite{%s}" % STATSRC[r["source"]], chk,
            esc("; ".join(p for p in r["note"].split("; ") if "dissipation factor" not in p
                          and not p.startswith("ranges ")))]))
    n5 = sum(1 for d in diffs.values() if abs(d) <= 0.05)
    n10 = sum(1 for d in diffs.values() if 0.05 < abs(d) <= 0.10)
    big = sorted((n for n, d in diffs.items() if abs(d) > 0.10), key=lambda n: -abs(diffs[n]))
    body = r"""
\section*{The library}

\texttt{statlib\_catalog.dat} is an electrostatic material library for FEMM, %d entries, built 1 October 2026. An electrostatic material is a relative permittivity, $\varepsilon_x$ and $\varepsilon_y$ (they differ only for the anisotropic 6H-SiC entry, whose c-axis lies along y), and a volume charge density, zero here. Electrostatics is the zero-frequency limit, so each dielectric takes the lowest-frequency value its source gives; the frequency column says which. The solid-insulation, insulating-oil and laminate values are shared with the current-flow library and live in its CSV files.

A convenience entry, ``Vacuum'', with $\varepsilon_r = 1$ by definition, sits above the folders, as ``Air'' does in the magnetics library; it needs no source and is not counted or tabulated below. The library is generated by \texttt{statlib\_src/\allowbreak build\_statlib.py}; \texttt{statlib\_src/\allowbreak catalog.csv} lists every other entry, and the table below is generated from it. \texttt{SOURCES.md} records each source document with its SHA-256 hash.

\section*{Liquids}

The electrostatic solver treats every material as a perfect insulator, and water is not one. Even ultrapure water at 25~\textdegree C, with $\sigma = 5.5\ \mu$S/m and $\varepsilon_r = 78.3$ (the current-flow library's entry, from ITU-R P.527-6 and Light et al.), relaxes free charge in $\varepsilon_0\varepsilon_r/\sigma = 0.13$~ms. In a field held longer than that, water behaves as a conductor and its permittivity does not enter. The water entries therefore sit in a folder named ``Water (high-frequency limit; DC: use current flow)'', and a problem with water in it belongs in the current-flow solver, which models conduction and permittivity together. A library entry carries no comment, so the folder name is the only warning a user sees.

The organic liquids with $\varepsilon_r$ above 10 (the alcohols, glycerol, acetone, formamide, nitrobenzene and 1,2-di\-chloro\-ethane) sit in ``Polar Organic Liquids (high-frequency limit)'' for the same reason. Polar solvents dissolve and dissociate ionic impurities, so they conduct too, by an amount that depends on purity; no held source gives a value. The low-permittivity organic liquids and the insulating oils are good insulators and can be used as they stand.

\section*{Check against the CRC Handbook}

The \emph{CRC Handbook of Chemistry and Physics} \cite{crcstat} covers %d of the entries; the ``CRC'' column gives its value, table and the library's difference from it (for gases, the difference in $\varepsilon-1$, marked $^*$). %d agree within 5\%%, %d within 10\%%, and %d differ by more. The gases agree within 1\%% in $\varepsilon-1$, but CRC's values descend partly from the same NBS Circular 537, so they are not independent. The liquids are an independent check: CRC's are Wohlfarth's Landolt--B\"ornstein compilation (1991), evaluated at the library's temperature from CRC's fits where it tabulates another; water agrees within 0.3\%%, and the other liquids within 2\%% except 2-propanol ($-$5\%%) and glycerol ($-$7\%%). Its glass values are at 100~MHz, its inorganic-solid values often in the infrared, and its polymer values ``typical or average'', so differences of several per cent there are expected.

The larger differences: Si$_3$N$_4$ (Dorf's 12.7 against CRC's 4.2 for a film; both are far from the 7--8 usually quoted for silicon nitride, and neither is a reliable figure); Ta$_2$O$_5$ (Dorf's 28 against 24 for the $\beta$ phase); nylon 6,6 (Callister's 4.0 against 3.5 for polyamides generally; nylon absorbs water, which raises it). Silicon's CRC value is at 4.2~K and is not comparable.

\section*{Entries}

""" % (len(rows), len(diffs), n5, n10, len(big))
    body += table(out, ["Entry", r"$\varepsilon_r$", "Frequency", "Src.", "CRC", "Note"],
                  "L{1.6in}lL{0.7in}L{0.4in}L{1.25in}L{1.45in}", "folder")
    body += "\n\n" + bib(STAT_BIB)
    write("statlib_provenance.tex", "Provenance of the Electrostatic Material Library", DATE, body)


# =============================================================== heatlib ====
KT_SRC = [   # substring of the specificheat page's source cell -> bib key
    ("Heat Transfer Textbook", "lienhard4"), ("Thermtest", "thermtest"), ("White", "white"),
    ("Matweb", "matweb"), ("Holman", "holman"), ("Cengel", "cengel"), ("Rolle", "rolle"),
    ("Carpenter", "carpenter"), ("C-Therm", "ctherm"), ("greenspec", "greenspec"),
    ("spacematdb", "spacemat"), ("tradekorea", "tradekorea"), ("citeseerx", "citeseer"),
    ("Chromalox", "chromalox"), ("vdmgroup", "vdm"), ("ncbi", "ncbi"), ("bearingworks", "bearingworks"),
    ("1.1710018", "doiarmco"),
]
HEAT_BIB = [
    ("specheat", r"FEMM wiki, ``Specific Heat,'' \url{https://www.femm.info/doku/doku.php?id=specificheat}, saved 1 October 2026: the source of every Kt entry, row by row."),
    ("lienhard6", r"J. H. Lienhard IV and J. H. Lienhard V, \emph{A Heat Transfer Textbook}, 6th ed., v6.00, 2024, \url{https://ahtt.mit.edu}, Tables A.1, A.3 and A.6. Used to check k, and the source of k for the entries marked ``replaced'' that cite it."),
    ("crc", r"W. M. Haynes, ed., \emph{CRC Handbook of Chemistry and Physics}, 97th ed., CRC Press, 2016: Section 12, ``Thermal Conductivity of'' Metals and Semiconductors as a Function of Temperature (p.~12-219, from Ho, Powell and Liley, J.~Phys.\ Chem.\ Ref.\ Data 1, 279, 1972), Alloys (12-221), Crystalline Dielectrics (12-222), Ceramics and Other Insulating Materials (12-224) and Glasses (12-226); Section 6, Thermal Conductivity of Gases (6-252, M.~L. Huber and A.~H. Harvey) and of Liquids (6-254). Used to check k only."),
    ("rohsenow", r"W. M. Rohsenow, J. P. Hartnett and Y. I. Cho, eds., \emph{Handbook of Heat Transfer}, 3rd ed., McGraw-Hill, 1998, Ch.~2: Table 2.29 (liquid metals, from the AEC/Navy \emph{Liquid Metals Handbook}, 1952), Tables 2.32 and 2.34 (elements and alloys) and Table 2.35 (miscellaneous materials, reprinted from Incropera and DeWitt, 3rd ed., 1990). Used to check k only."),
    ("kreithhb", r"F. Kreith, ed., \emph{The CRC Handbook of Mechanical Engineering}, CRC Press, 1999, Appendix Tables B.2 (common liquids, 300 K), C.1 (common solids) and C.4 (pure metals, 25~\textdegree C). Used to check k only."),
    ("lienhard4", r"J. H. Lienhard IV and J. H. Lienhard V, \emph{A Heat Transfer Textbook}, 4th ed., as cited by \cite{specheat} for most $K_t$ values. Not held."),
    ("thermtest", r"Thermtest Inc., materials thermal property database, as cited by \cite{specheat}."),
    ("white", r"F. M. White, \emph{Heat and Mass Transfer}, Addison-Wesley, 1988 (printed copy held; scans of Appendices C--F, pp.~672--681, in \texttt{Thermal/}). Source of the library's k; also a $K_t$ source cited by \cite{specheat}. White's values are transcribed in \texttt{provenance/white\_k.csv}."),
    ("matweb", r"MatWeb material property data, as cited by \cite{specheat}."),
    ("holman", r"J. P. Holman, \emph{Heat Transfer}, 8th ed., McGraw-Hill, 2001, pp.~640--642, as cited by \cite{specheat}."),
    ("cengel", r"Y. A. \c{C}engel and A. J. Ghajar, \emph{Heat and Mass Transfer}, 4th ed., McGraw-Hill, 2011, as cited by \cite{specheat}."),
    ("rolle", r"K. Rolle, \emph{Heat and Mass Transfer}, 2nd ed., Cengage Learning, 2015, as cited by \cite{specheat}."),
    ("carpenter", r"Carpenter Technology catalog (HyMu 80, High Permeability 49), as cited by \cite{specheat}."),
    ("ctherm", r"C-Therm Technologies, as cited by \cite{specheat}."),
    ("greenspec", r"GreenSpec, insulation materials thermal properties, as cited by \cite{specheat}."),
    ("spacemat", r"SpaceMatDB, as cited by \cite{specheat}."),
    ("tradekorea", r"TradeKorea, rigid graphite felt board, as cited by \cite{specheat}."),
    ("citeseer", r"CiteSeerX document 10.1.1.543.2996, as cited by \cite{specheat}."),
    ("chromalox", r"Chromalox, as cited by \cite{specheat}."),
    ("vdm", r"VDM Group and Newport City Council rock-wool documents, as cited by \cite{specheat}."),
    ("ncbi", r"NCBI PMC3699319 and Loughborough University ICEE paper, as cited by \cite{specheat}."),
    ("bearingworks", r"Bearing Works, as cited by \cite{specheat}."),
    ("doiarmco", r"doi:10.1063/1.1710018 (Armco iron), as cited by \cite{specheat}."),
]
PAGE_NAME = {"Tin (white)": "Tin", "Corning 7740": "Corning Pyrex 7740", "Steam": "Steam at 373.15 K",
             "Platinum, Pure": "Platinum, Pure", "Carpenter 49": "Carpenter49"}


def heat_doc():
    t = open(os.path.join(ROOT, "matlibs", "femm-specificheat-20261001.html"), encoding="utf-8", errors="replace").read()
    page = {}
    order = []
    for tr in re.findall(r"<tr[^>]*>(.*?)</tr>", t, re.S):
        c = [html.unescape(re.sub(r"<[^>]+>", "", x)).strip() for x in re.findall(r"<t[dh][^>]*>(.*?)</t[dh]>", tr, re.S)]
        if c and c[0] != "Material":
            order.append(c)
    # the page writes Teflon and Balsa as a name row and an unnamed max row
    prev = None
    for c in order:
        name = c[0] or prev
        prev = name
        page.setdefault(PAGE_NAME.get(name, name), []).append(c[2])
    chk = {r["name"]: r for r in csv.DictReader(open(os.path.join(HERE, "heat_kcheck.csv"), encoding="utf-8"))}
    sys.path.insert(0, HERE)
    import heat_compare
    comp = {n: (s, d) for n, s, d in heat_compare.rows()}
    wh = heat_compare.white()

    lib = open(os.path.join(ROOT, "matlibs", "heatlib_fixed.dat"), encoding="utf-8").read()
    rows, stack = [], []
    tok = re.compile(r'<BeginFolder>|<EndFolder>|<FolderName>\s*=\s*"([^"]*)"|<BeginBlock>(.*?)<EndBlock>', re.S)
    for m in tok.finditer(lib):
        s = m.group(0)
        if s == "<BeginFolder>":
            stack.append("?")
            continue
        if s == "<EndFolder>":
            stack.pop()
            continue
        if m.group(1) is not None:
            stack[-1] = m.group(1)
            continue
        b = m.group(2)
        name = re.search(r'<BlockName>\s*=\s*"([^"]*)"', b).group(1)
        kx = float(re.search(r"<Kx>\s*=\s*([-\d.eE+]+)", b).group(1))
        kt = float(re.search(r"<Kt>\s*=\s*([-\d.eE+]+)", b).group(1))
        npt = re.search(r"<TKPoints>\s*=\s*(\d+)", b)
        npt = int(npt.group(1)) if npt else 0
        key = name
        if name == "Ammonia":
            key = "Ammonia (gas)" if "Gases" in stack[0] else "Ammonia (liquid)"
        base = re.sub(r"-(min|max)-heat-capacity$", "", name)
        srcs = page.get(name) or page.get(base) or []
        if base in ("Teflon", "Balsa") and len(srcs) == 2:
            srcs = [srcs[0] if name.endswith("min-heat-capacity") else srcs[1]]
        kts = sorted({k for s in srcs for sub, k in KT_SRC if sub.lower() in s.lower()})
        c = chk.get(key)
        st, det = (c["status"], c["detail"]) if c else ("none", "")
        if key in comp:
            if st == "none":
                st = comp[key][0]
            det = (det + "; " if det else "") + comp[key][1]
        status = {"checked": "checked", "replaced": r"\textbf{replaced}", "mixed": "mixed",
                  "disagrees": r"\textit{disagrees}", "none": "none"}[st]
        ksrc = ["white"] if key in wh else []
        if st == "replaced" and "Table A." in det:
            ksrc.append("lienhard6")
        if st == "replaced" and "from CRC" in det:
            ksrc.append("crc")
        rows.append(dict(folder=" / ".join(stack), white=key in wh, cells=[
            esc(name), num(kx), num(kt), ("%d" % npt) if npt else "--",
            "\\cite{%s}" % ",".join(kts) if kts else "--",
            "\\cite{%s}~%s" % (",".join(ksrc), wh[key][0]) if key in wh else "--",
            status, esc(det)]))
    col = lambda r: r["cells"][6]
    nwh = sum(1 for r in rows if r["white"])
    nchk = sum(1 for r in rows if col(r) == "checked")
    ncor = sum(1 for r in rows if "replaced" in col(r))
    nmix = sum(1 for r in rows if col(r) == "mixed")
    ndis = sum(1 for r in rows if "disagrees" in col(r))
    nun = sum(1 for r in rows if col(r) == "none")
    body = r"""
\section*{The library}

\texttt{matlibs/heatlib\_fixed.dat} is a heat-flow material library for FEMM, %d entries. Each entry carries a thermal conductivity $k$ (W/m\,K), as a constant or as a table of $k$ against temperature, and a volumetric heat capacity $K_t$ (MJ/m$^3$\,K).

\textbf{Heat capacity.} Every $K_t$ is documented row by row on the FEMM wiki's ``Specific Heat'' page \cite{specheat}, which names its source; the table's $K_t$ column shows it. All agree with the page to 0.5\%%.

\textbf{Thermal conductivity: source.} The page does not cover $k$. It comes from F. M. White's \emph{Heat and Mass Transfer} \cite{white}, Appendices C (metals), D (nonmetals), E (liquid metals) and F (saturated liquids): the library's points at 273, 473, 673\ldots\ K are White's 0, 200, 400\ldots\ \textdegree C columns, and %d entries take their $k$ from White, point for point, except those described below that take it from another source. Two entries hold fewer points than White (Fireclay brick, engine oil), within 4\%% of White when interpolated. The ``$k$ src.'' column gives White's appendix. The gases were not compared (White's gas appendix was not among the pages scanned), 6061-T6 and 7075-T6 (not in White) match Lienhard's Table A.1 exactly, and the other aluminium tempers (2024-T3, -O, -T351, -T4, 6061-T651, 7075-T651), HyMu 80, Carpenter 49 and 17-4 PH have no $k$ source recorded.

\textbf{Thermal conductivity: checks.} Independently of White, $k$ was checked against Lienhard's \emph{A Heat Transfer Textbook}, 6th ed. \cite{lienhard6}, against the \emph{CRC Handbook of Chemistry and Physics} \cite{crc}, and against two older handbooks \cite{rohsenow,kreithhb}. Of the %d entries, %d agree with a check source (the worst difference over their points is given), %d take $k$ from Lienhard or CRC instead of White (``replaced''), %d are mixed (one check source agrees within 10\%%, another does not), %d disagree with every check source by more than 10\%%, and %d have no independent check.

Where White's $k$ is wrong or outdated, another source is used. Three cases are errors in White's own table: White gives constantan 26 at 200~\textdegree C, which is Lienhard's 100~\textdegree C value (Lienhard: 35); it drops 1\%% carbon steel's 600~\textdegree C value, so that its 800 and 1000~\textdegree C values sit a column early; and it gives solid silver 355 at 1000~\textdegree C, above silver's melting point (silver keeps White's other points and omits that one). For these Lienhard's values are used. White's R-12 and liquid-ammonia data are older than Lienhard's, and Lienhard's are used; White's mercury $k$ (7.3--14.2) is also older, and Lienhard's is used. Three more take their values from the CRC Handbook \cite{crc}: Diatomaceous Earth (White's 1.3 is more than ten times the 0.05--0.16 that CRC gives for diatomite and Incropera for the powders), Teflon (0.35; CRC gives 0.26 for PTFE) and Window glass (0.84; CRC gives 1.00 at 0~\textdegree C for a window-type soda-lime glass).

The CRC Handbook is the best sourced of these. Its metals table is Ho, Powell and Liley's recommended values (J.~Phys.\ Chem.\ Ref.\ Data, 1972) for high-purity specimens; its gas table is from modern reference correlations (Huber and Harvey), and every library gas agrees with it within 6\%%; its liquids table agrees within 2\%% for water, ethanol, ethylene glycol and glycerin. Its ceramics table calls itself ``only \ldots\ a rough guide''. Where a library entry has only 0 and 200~\textdegree C points (silicon, germanium, the diamonds, magnesium oxide), FEMM interpolates linearly across a conductivity that falls roughly as $1/T$, so it overestimates at 400~K by 12--22\%% against CRC even where the tabulated points agree.

The two older handbooks are secondary compilations of older data and only flag entries; a disagreement means White and they differ, not that White was copied wrongly. Rohsenow's element and alloy tables (2.32, 2.34) are round values in kcal/(m\,h\,\textdegree C) converted to W/m\,K, and several of their pure-metal values sit well below modern ones (rhodium 88 against 150, sodium 100 against 134). Its Table 2.35 is Incropera and DeWitt's Table A.3 reprinted, with some decimal slips in the reprint (ice 0.188, paper 0.011 W/m\,K), which were not used. Many disagreements are a choice of variety rather than an error (``Concrete'', ``Window'' glass, ``Sandstone''). Besides the three replaced from CRC, the ones where White itself may be off are Cobalt (White's 104 against 69--70 in the older handbooks; not in CRC) and Vanadium (White's 31 against Kreith's 60; not in CRC), both left as White gives them; White's footnote warns that nonmetal properties are ``subject to large uncertainty (up to 50\%%)''.

\section*{Fluids}

The heat-flow solver models conduction only. A fluid entry is therefore right only where the fluid cannot move, as in a thin gap or a sealed cavity too small to convect. A fluid that flows or convects carries heat far faster than its conductivity alone allows; it belongs in a convection boundary condition (a heat-transfer coefficient $h$) on the surface it cools, not in a region of its own. The three fluid folders are named ``Liquid Metals (stagnant only)'', ``Saturated Liquids (stagnant only)'' and ``Gases at 1 atm (stagnant only)''. A library entry carries no comment, so the folder name is the only warning a user sees.

\section*{Entries}

The $k$ column is the constant value (FEMM uses the table when one is given; ``Pts.'' is its length).

""" % (len(rows), nwh, len(rows), nchk, ncor, nmix, ndis, nun)
    body += table(rows, ["Entry", "$k$", "$K_t$", "Pts.", r"$K_t$ src.", r"$k$ src.", "$k$ check", "Detail"],
                  "L{1.35in}rrrL{0.55in}L{0.55in}L{0.62in}L{1.4in}", "folder")
    body += "\n\n" + bib(HEAT_BIB)
    write("heatlib_provenance.tex", "Provenance of the Heat-Flow Material Library", DATE, body)


cond_doc()
stat_doc()
heat_doc()
