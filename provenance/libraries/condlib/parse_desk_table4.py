# SPDX-License-Identifier: MIT
"""Parse ASM Metals Handbook Desk Edition (1998) Table 4, "Electrical
conductivity and resistivity of metals and alloys", into a CSV.

Input: a text extraction of the Desk Edition PDF with '@@PAGE n' markers
(pymupdf page.get_text(), one cell per line). Each entry is: name, %IACS,
resistivity in microhm-cm; lines without numbers are headings, recorded as
they come (the folder layout is decided later, by hand). Output columns:
headings, name, iacs, rho_uohm_cm, note, pdf_page (1-based).
"""
import csv
import re
import sys

t = open(sys.argv[1], encoding="utf-8").read()
a = t.index("Table 4 Electrical conductivity and resistivity of metals and alloys")
z = t.index("Table 5 Approximate melting temperatures")
page = int(re.findall(r"@@PAGE (\d+)", t[:a])[-1])

NUM = re.compile(r"^(\. \. \.|[\d.]+(?:-[\d.]+)?)(\([a-e]\))?$")
SKIP = re.compile(r"^(Copyright|ASM Handbook|ASM |Page \d+|\d{1,2} \w{3} \d{4}$)")
HEAD = {"Metal or alloy", "Conductivity,", "% IACS", "Resistivity,", "μΩ· cm", "μΩ·cm",
        "Table 4 Electrical conductivity and resistivity of metals and alloys"}
cells = []
for ln in t[a:z].split("\n"):
    m = re.match(r"@@PAGE (\d+)", ln)
    if m:
        page = int(m.group(1))
        continue
    s = ln.strip()
    if s and not SKIP.match(s) and s not in HEAD:
        cells.append((s, page))

rows, heads = [], []
i = 0
while i < len(cells):
    s, pg = cells[i]
    nxt = [c[0] for c in cells[i + 1:i + 3]]
    if len(nxt) == 2 and NUM.match(nxt[0]) and NUM.match(nxt[1]) and not NUM.match(s):
        a1, a2 = NUM.match(nxt[0]), NUM.match(nxt[1])
        rows.append([" / ".join(heads), s, a1.group(1), a2.group(1),
                     (a1.group(2) or "") + (a2.group(2) or ""), pg + 1])
        i += 3
        continue
    if s.startswith("(") and s[1:2] in "abcde" and s[2:3] == ")":
        rows and rows[-1].__setitem__(4, rows[-1][4] + " " + s)   # footnote text
    elif not NUM.match(s):
        if rows and rows[-1][0] == " / ".join(heads):
            heads = []          # a heading straight after data starts a new group
        heads.append(s)
    i += 1

with open(sys.argv[2], "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["headings", "name", "iacs", "rho_uohm_cm", "note", "pdf_page"])
    w.writerows(rows)
print(len(rows), "rows")
