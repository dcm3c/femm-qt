# SPDX-License-Identifier: MIT
"""Compare heat-library k against the independent reference values in
heat_refs.csv; rows() gives (name, status, detail) per entry, and
make_provenance.py shows them beside the library's own source, White (1988),
whose values are in white_k.csv.

The library's k(T) is interpolated linearly between its points (a constant
<Kx> when it has none) and held at its end values up to 40 K beyond them;
reference points farther outside the library's range are not compared. Per
source, the worst difference (library - reference) / reference is taken; an
entry is "checked" when every source is within 10%, "mixed" when some are,
and "disagrees" when none are. Nothing is corrected from these: the handbooks
are older secondary compilations, so a disagreement says White and they differ,
not that White was copied wrongly (that is checked against white_k.csv).

Sources (keys in heat_refs.csv):
  R229/R232/R234/R235  Rohsenow, Hartnett and Cho, Handbook of Heat Transfer,
                       3rd ed. (1998), Tables 2.29, 2.32, 2.34, 2.35
  KB2/KC1/KC4          Kreith, CRC Handbook of Mechanical Engineering (1999),
                       Tables B.2, C.1, C.4
  C219 ... C6254       Haynes, ed., CRC Handbook of Chemistry and Physics, 97th
                       ed. (2016): Section 12 tables "Thermal Conductivity of"
                       Metals and Semiconductors (12-219, Ho, Powell and Liley,
                       JPCRD 1972), Alloys (12-221), Crystalline Dielectrics
                       (12-222), Ceramics and Other Insulating Materials
                       (12-224), Glasses (12-226); Section 6 Gases (6-252) and
                       Liquids (6-254)
"""
import csv
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
LABEL = {"R229": "Rohsenow T2.29", "R232": "Rohsenow T2.32", "R234": "Rohsenow T2.34",
         "R235": "Rohsenow T2.35", "KB2": "Kreith B.2", "KC1": "Kreith C.1", "KC4": "Kreith C.4",
         "C219": "CRC 12-219", "C221": "CRC 12-221", "C222": "CRC 12-222", "C224": "CRC 12-224",
         "C226": "CRC 12-226", "C6252": "CRC 6-252", "C6254": "CRC 6-254"}
TOL, HOLD = 0.10, 40.0


def library():
    lib = open(os.path.join(ROOT, "matlibs", "heatlib_fixed.dat"), encoding="utf-8").read()
    out, stack = {}, []
    tok = re.compile(r'<BeginFolder>|<EndFolder>|<FolderName>\s*=\s*"([^"]*)"|<BeginBlock>(.*?)<EndBlock>', re.S)
    for m in tok.finditer(lib):
        s = m.group(0)
        if s == "<BeginFolder>":
            stack.append("?")
        elif s == "<EndFolder>":
            stack.pop()
        elif m.group(1) is not None:
            stack[-1] = m.group(1)
        else:
            b = m.group(2)
            name = re.search(r'<BlockName>\s*=\s*"([^"]*)"', b).group(1)
            kx = float(re.search(r"<Kx>\s*=\s*([-\d.eE+]+)", b).group(1))
            pts = [(float(t), float(k)) for t, k in re.findall(r"^\s+([\d.]+)\t([-\d.eE+]+)\s*$", b, re.M)]
            if name == "Ammonia":
                name = "Ammonia (gas)" if "Gases" in stack[0] else "Ammonia (liquid)"
            out[name] = pts or [(None, kx)]
    return out


def k_at(pts, T):
    if pts[0][0] is None:
        return pts[0][1]
    if T < pts[0][0]:
        return pts[0][1] if pts[0][0] - T <= HOLD else None
    if T > pts[-1][0]:
        return pts[-1][1] if T - pts[-1][0] <= HOLD else None
    for (t0, k0), (t1, k1) in zip(pts, pts[1:]):
        if t0 <= T <= t1:
            return k0 + (k1 - k0) * (T - t0) / (t1 - t0)
    return pts[-1][1]


def rows():
    lib = library()
    refs = {}
    for r in csv.DictReader(open(os.path.join(HERE, "heat_refs.csv"), encoding="utf-8")):
        refs.setdefault(r["name"], []).append(r)
    result = []
    for name, rs in refs.items():
        assert name in lib, name
        worst = {}
        for r in rs:
            k = k_at(lib[name], float(r["T_K"]))
            if k is None:
                continue
            d = (k - float(r["k"])) / float(r["k"])
            w = worst.get(r["src"])
            if w is None or abs(d) > abs(w[0]):
                worst[r["src"]] = (d, float(r["T_K"]))
        assert worst, name
        n_ok = sum(abs(d) <= TOL for d, _ in worst.values())
        status = "checked" if n_ok == len(worst) else "mixed" if n_ok else "disagrees"
        detail = "; ".join("%s %+.0f%%%s" % (LABEL[s], 100 * d, "" if abs(d) <= TOL else " at %.0f K" % T)
                           for s, (d, T) in worst.items())
        result.append((name, status, detail))
    return result


def white():
    """White's values (white_k.csv) against the library: name -> (appendix, page, worst
    |difference| at White's points)."""
    lib = library()
    out = {}
    for r in csv.DictReader(open(os.path.join(HERE, "white_k.csv"), encoding="utf-8")):
        pts = [tuple(float(v) for v in p.split(":")) for p in r["points"].split()]
        worst = max(abs(k_at(lib[r["name"]], T) - k) / k for T, k in pts if k_at(lib[r["name"]], T) is not None)
        out[r["name"]] = (r["appendix"], r["page"], worst)
    return out


if __name__ == "__main__":
    for r in rows():
        print("%-26s %-10s %s" % r)
    print()
    for name, (app, page, worst) in white().items():
        if worst > 0.005:
            print("differs from White %-22s App. %s p. %s: %.0f%%" % (name, app, page, 100 * worst))
