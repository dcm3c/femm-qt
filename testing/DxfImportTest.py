# DxfImportTest.py
# mi_readdxf must bring in every entity, whatever the file's layout, and an arc
# longer than 180 degrees as the arc it is.
#
#     build/bin/femm_headless -run testing/DxfImportTest.py
#
# Reported from the field (2026-10): an AutoCAD drawing of three lines across a
# 292.6 degree ARC imported as a 67 degree arc plus two slivers, one of them a
# NEGATIVE arc, drawn as a line with no endpoint; FEMM 4.2 imported it fine.
# Two defects, both in libfemm/dxf_io.cpp:
#
#   * FEMM's arc model only holds arcs of 180 degrees or less -- GetCircle puts
#     the centre on the left of the chord, and the split in AddNode reads arg()
#     in (-180, 180] -- so the first line crossing a longer arc corrupted it.
#     FEMM 4.2's reader halves such arcs on import; ours now does too, for ARC
#     entities and for polyline bulges past 1 (also past 180 degrees).
#   * Each entity was handed back to the main loop by seeking back a guessed
#     byte count, right only for AutoCAD's "  0" + CRLF. With LF endings or an
#     unindented "0", every entity after the first was silently dropped, and
#     with both, depending on the values' lengths, it could seek back past the
#     pair it had just read and re-read one entity forever: the import HUNG
#     (reproduced with LF, bare codes and one-character values). It now keeps
#     one pair of lookahead in memory. The cases here use values that fail
#     rather than hang on the old reader, so a regression fails the suite
#     instead of stalling it: 31 of the checks below fail on the old reader.
#
# Restored from FEMM 4.2, which the from-scratch reader of d79faa5 had dropped:
# layers become groups (by order in the LAYER table), POINT entities become
# nodes, and arcs carry a 5 degree maximum segment rather than 1. And only the
# ENTITIES section is geometry -- a BLOCKS-section definition's lines are not.
# 24 checks of that last case fail on the reader of 04267cb.
#
# Every case writes its own DXF, in all four layouts (CRLF/LF, group codes
# right-aligned in three columns as AutoCAD writes them, or bare).

import os
from math import sin, atan, pi, sqrt

fails = []
DXF = "_dxf_import.dxf"
FEM = "_dxf_import.fem"
LAYOUTS = [("\r\n", 3), ("\r\n", 1), ("\n", 3), ("\n", 1)]


def arc_entity(cx, cy, r, a0, a1):
    return ["0", "ARC", "8", "0", "10", repr(cx), "20", repr(cy), "30", "0.0",
            "40", repr(r), "50", repr(a0), "51", repr(a1)]


def line_entity(x0, y0, x1, y1):
    return ["0", "LINE", "8", "0", "10", repr(x0), "20", repr(y0), "30", "0.0",
            "11", repr(x1), "21", repr(y1), "31", "0.0"]


def lwpoly_entity(pts):
    out = ["0", "LWPOLYLINE", "8", "0", "90", str(len(pts)), "70", "0"]
    for (x, y, b) in pts:
        out += ["10", repr(x), "20", repr(y)]
        if b:
            out += ["42", repr(b)]
    return out


def layout_name(layout):
    eol, width = layout
    return "%s, codes width %d" % ("CRLF" if eol == "\r\n" else "LF", width)


def import_dxf(entities, layout, before=None, raw=False):
    """Write the DXF in this layout, import it; return (points, segments, arcs),
    each arc as (n0 xy, n1 xy, angle). `before` is a list of whole sections
    (flat pair lists) written ahead of ENTITIES. With raw=True the parsed
    fields come back unconverted: points, segments and arcs as string lists."""
    eol, width = layout
    pairs = []
    for sec in (before or []):
        pairs += sec
    pairs += ["0", "SECTION", "2", "ENTITIES"]
    for e in entities:
        pairs += e
    pairs += ["0", "ENDSEC", "0", "EOF"]
    lines = []
    for k in range(0, len(pairs), 2):
        lines.append(("%" + str(width) + "s") % pairs[k])
        lines.append(pairs[k + 1])
    fh = _builtin_open(DXF, "wb")
    fh.write((eol.join(lines) + eol).encode())
    fh.close()

    newdocument(0)
    mi_readdxf(os.getcwd() + "/" + DXF)
    mi_saveas(FEM)
    mi_close()
    fh = _builtin_open(FEM, "r")
    txt = [l.strip() for l in fh.read().split("\n")]
    fh.close()

    def section(tag):
        i = [k for k, l in enumerate(txt) if l.startswith(tag)][0]
        return [txt[i + 1 + k].split() for k in range(int(txt[i].split("=")[1]))]

    if raw:
        return (section("[NumPoints]"), section("[NumSegments]"),
                section("[NumArcSegments]"))
    pts = [(float(f[0]), float(f[1])) for f in section("[NumPoints]")]
    segs = section("[NumSegments]")
    arcs = [(pts[int(f[0])], pts[int(f[1])], float(f[2]))
            for f in section("[NumArcSegments]")]
    return pts, segs, arcs


def centre(p0, p1, angle):
    """The centre FEMM derives for an arc (DocumentBase::GetCircle)."""
    a0 = complex(*p0)
    a1 = complex(*p1)
    d = abs(a1 - a0)
    t = (a1 - a0) / d
    r = d / (2 * sin(angle * pi / 360))
    return a0 + (d / 2 + 1j * sqrt(max(0.0, r * r - d * d / 4))) * t, r


def report(name, bad, detail):
    print("  %-52s %s  %s" % (name, detail, "ok" if not bad else "FAIL"))
    for b in bad:
        fails.append("%s: %s" % (name, b))


# --- every entity arrives, in every layout -----------------------------------
SIMPLE = [line_entity(0, 0, 10, 0), line_entity(0, 5, 10, 5),
          line_entity(0, 9, 10, 9), arc_entity(30, 0, 5, 0, 90),
          lwpoly_entity([(50.0, 0.0, 0), (60.0, 0.0, 0), (60.0, 10.0, 0)])]
for layout in LAYOUTS:
    pts, segs, arcs = import_dxf(SIMPLE, layout)
    bad = []
    if len(segs) != 5 or len(arcs) != 1:
        bad.append("%d segments and %d arcs, want 5 and 1 (3 LINEs, 1 ARC, "
                   "a 2-segment LWPOLYLINE)" % (len(segs), len(arcs)))
    report("all entities (%s)" % layout_name(layout), bad,
           "%d segs, %d arcs" % (len(segs), len(arcs)))


# --- arcs past 180 degrees ----------------------------------------------------
def check_arcs(tag, entities, cx, cy, r, sweep, layout):
    pts, segs, arcs = import_dxf(entities, layout)
    bad = []
    total = 0.0
    for (p0, p1, a) in arcs:
        total += a
        if not (0 < a <= 180 + 1e-9):
            bad.append("an arc of %.4f degrees" % a)
            continue
        c, rr = centre(p0, p1, a)
        if abs(c - complex(cx, cy)) > 1e-6 * r or abs(rr - r) > 1e-6 * r:
            bad.append("an arc of %.4f degrees about (%.4f, %.4f) R %.4f, not "
                       "(%g, %g) R %g" % (a, c.real, c.imag, rr, cx, cy, r))
    if abs(total - sweep) > 1e-6:
        bad.append("arcs sum to %.6f degrees, not %.6f" % (total, sweep))
    report("%s (%s)" % (tag, layout_name(layout)), bad,
           "%d arcs, %8.4f deg" % (len(arcs), total))


# A 292.5 degree ARC, CCW from 300 to 232.5 (through 0), crossed by lines --
# the reported shape, on our own numbers.
CX, CY, R = 100.0, 50.0, 40.0
CROSS = [line_entity(40, 60, 170, 40), line_entity(70, 110, 105, 0),
         line_entity(95, 75, 135, 5)]
LONG = arc_entity(CX, CY, R, 300.0, 232.5)
# A two-vertex LWPOLYLINE with bulge 2: an included angle of 4*atan(2) = 253.7
# degrees, CCW from (-10, 0) to (10, 0); its centre is on the -y side.
B = 2.0
BANG = 4 * atan(B) * 180 / pi
BR = 10 / sin(BANG * pi / 360)
BCY = -sqrt(BR * BR - 100)

for layout in LAYOUTS:
    check_arcs("ARC 292.5, alone", [LONG], CX, CY, R, 292.5, layout)
    check_arcs("ARC 292.5, crossed by lines", [LONG] + CROSS, CX, CY, R, 292.5, layout)
    check_arcs("ARC 292.5, lines first", CROSS + [LONG], CX, CY, R, 292.5, layout)
    check_arcs("ARC 120, no split needed", [arc_entity(CX, CY, R, 30.0, 150.0)],
               CX, CY, R, 120.0, layout)
    check_arcs("bulge 2, crossed",
               [lwpoly_entity([(-10.0, 0.0, B), (10.0, 0.0, 0)]),
                line_entity(-30, BCY, 30, BCY)], 0.0, BCY, BR, BANG, layout)
    check_arcs("bulge -2 (clockwise)",
               [lwpoly_entity([(10.0, 0.0, -B), (-10.0, 0.0, 0)])],
               0.0, BCY, BR, BANG, layout)

# --- what FEMM 4.2's reader did and the rewrite of d79faa5 dropped -----------
# Layers become groups, numbered by their order in the LAYER table; POINTs
# become nodes; arcs carry a 5 degree maximum segment. And only ENTITIES is
# geometry: lines inside a BLOCKS-section definition (a dimension's arrowheads,
# say) must not be imported.
def on_layer(entity, layer):
    """The same entity with its layer (group code 8) set."""
    out = list(entity)
    out[3] = layer
    return out


TABLES = ["0", "SECTION", "2", "TABLES", "0", "TABLE", "2", "LAYER", "70", "3",
          "0", "LAYER", "2", "0", "70", "0",
          "0", "LAYER", "2", "rotor", "70", "0",
          "0", "LAYER", "2", "stator", "70", "0",
          "0", "ENDTAB", "0", "ENDSEC"]
BLOCKS = ["0", "SECTION", "2", "BLOCKS",
          "0", "BLOCK", "8", "0", "2", "*D1", "70", "1", "10", "0", "20", "0"] + \
         line_entity(500, 500, 600, 600) + \
         ["0", "ENDBLK", "8", "0", "0", "ENDSEC"]
POINT = ["0", "POINT", "8", "rotor", "10", "-20.0", "20", "-20.0", "30", "0.0"]

for layout in LAYOUTS:
    ents = [on_layer(line_entity(0, 0, 10, 0), "rotor"),
            on_layer(arc_entity(30, 0, 5, 0, 90), "stator"),
            on_layer(line_entity(0, 20, 10, 20), "not_in_table"),
            POINT]
    pts, segs, arcs = import_dxf(ents, layout, before=[TABLES, BLOCKS], raw=True)
    bad = []
    seg_groups = sorted(int(f[5]) for f in segs)
    if seg_groups != [0, 1]:
        bad.append("segment groups %s, want [0, 1] (rotor = 1, unlisted "
                   "layer = 0)" % seg_groups)
    arc_groups = [int(f[6]) for f in arcs]
    if arc_groups != [2]:
        bad.append("arc groups %s, want [2] (stator)" % arc_groups)
    arc_maxseg = [float(f[3]) for f in arcs]
    if arc_maxseg != [5.0]:
        bad.append("arc max segment %s, want [5.0] as FEMM 4.2" % arc_maxseg)
    point = [f for f in pts if float(f[0]) == -20.0 and float(f[1]) == -20.0]
    if len(point) != 1:
        bad.append("the POINT at (-20, -20) did not become a node")
    elif int(point[0][3]) != 1:
        bad.append("the POINT's node is in group %s, want 1 (rotor)" % point[0][3])
    if any(float(f[0]) >= 500 for f in pts):
        bad.append("a line from a BLOCKS-section definition was imported")
    if len(pts) != 7:
        bad.append("%d nodes, want 7 (2 lines, 1 arc, 1 point)" % len(pts))
    report("layers, POINT, max segment, BLOCKS (%s)" % layout_name(layout), bad,
           "%d nodes" % len(pts))

for f in (DXF, FEM):
    try:
        os.remove(f)
    except OSError:
        pass

if fails:
    for m in fails:
        print("   " + m)
    print("dxf import: %d FAILED" % len(fails))
else:
    print("dxf import: every entity arrives in all four layouts, arcs past 180 "
          "degrees import as halves on the right circle, layers become groups, "
          "POINTs become nodes, and BLOCKS definitions stay out")
