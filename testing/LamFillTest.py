# LamFillTest.py -- nonlinear materials laminated on edge (LamType 1 and 2):
# the bulk lamination model against the same stack drawn lamination by
# lamination (CPython; drives femm_headless).
#
# A user report (8 Nov 2026): a nanocrystalline ribbon core (fill 0.78,
# saturation 1.2 T) modelled as one block laminated parallel to the field
# saturated at 1.2 T, where the same stack drawn as ten iron strips with air
# between them saturated at 0.78 x 1.2 T; the postprocessor's H at the block
# centre was 270 times the applied field and energy plus coenergy was 270
# times B.H.V. Two defects behind that, both from FEMM 4.2:
#
#   1. The planar solver read the B-H curve at the stack-average flux density
#      with the wrong component scaled by the fill factor: the ACROSS
#      component was divided by the fill, the ALONG one was not. For flux
#      along the laminations the iron's curve was therefore read at B_avg
#      instead of B_avg/c, so the stack kept the iron's full saturation. The
#      postprocessor scaled the right component, hence the inconsistent H.
#   2. Along the laminations the (1-c) air path in parallel with the iron was
#      dropped (the manual's "mu_ez ~ c mu_r mu_o" approximation), a few
#      tenths of a percent at this fill, growing into deep saturation.
#
# Both are gone: solver (GetLamProps) and postprocessor (LamIron) now solve
# for the flux the iron carries along the laminations with the insulation in
# parallel, read the curve at the iron's own |B|, and use the manual's full
# parallel and series permeabilities. The energy density gained the
# insulation's along-lamination term.
#
# The test: a uniform field H is applied to a 25 mm stack by current sheets
# (a long solenoid in cross-section, Neumann ends, so H = I/height inside
# and the drawn reference is exact: iron and air side by side see the same H
# along the laminations, the same B across them). Cases:
#   planar, laminations along y, field along y   (LamType 2) at 30, 1000, 10000 A/m
#   planar, laminations along x, field along x   (LamType 1) at 30, 10000 A/m
#   planar, field ACROSS the laminations, both orientations (LamType 1 and
#          2), at a prescribed 1.30 T so the iron is past its knee and the
#          series path has a visible iron share; a current sheet cannot
#          drive this case one-dimensionally, so A is prescribed instead
#   axisymmetric, laminations along z, field along z (LamType 2), shells
#          instead of strips, at 1000, 10000 A/m
#   axisymmetric, discs stacked along z (LamType 1) with the field ACROSS
#          them at a prescribed 1.30 T, the other axisymmetric branch
#   axisymmetric, MIXED field: a disc-laminated plate on a pot core that
#          flux enters axially and leaves radially, bulk vs 16 drawn discs on
#          flux linkage, energies and the plate's mean Br (no closed form;
#          both meshes converge to the same values, agreement 0.1-0.4%)
# Checks, bulk block against the drawn stack: average B along the field,
# energy and coenergy separately, and for the bulk block the energy identity
# (W + W')/(B.H.V) = 1 and the point H at the centre equal to the applied H
# (across: the drawn stack's mean H). On the old code the along cases miss
# B by 8% to 28% and the identity by a factor of 270; the across cases read
# the curve at B/0.78 and miss H by tens of percent.
#
# Usage:  python3 LamFillTest.py [path-to-femm_headless]
# Runs from testing/; writes and removes lf_tmp* scratch files.
# Exits nonzero on any violation.

import glob, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
def _default_engine(name):
    # Windows and Linux leave the engine in build/bin. macos_build.sh keeps
    # only the copy inside the app bundle, where the solvers sit beside it.
    b = os.path.join(HERE, "..", "build", "bin")
    loose = os.path.join(b, name + (".exe" if os.name == "nt" else ""))
    bundled = os.path.join(b, "femm_gui.app", "Contents", "MacOS", name)
    return bundled if not os.path.exists(loose) and os.path.exists(bundled) else loose


BIN = sys.argv[1] if len(sys.argv) > 1 else _default_engine("femm_headless")
BASE = os.path.join(HERE, "lf_tmp").replace("\\", "/")

SCRIPT = r'''
BASE = "%s"
FILL, T, HGT = 0.78, 25.0, 100.0
BH = [(0, 0.0), (5, 0.18850), (10, 0.37693), (15, 0.56446), (20, 0.74652), (25, 0.90994),
      (30, 1.03518), (35, 1.11362), (40, 1.15563), (50, 1.18724), (60, 1.19567), (80, 1.19931),
      (100, 1.19992), (200, 1.20025), (500, 1.20063), (1000, 1.20126), (3000, 1.20377),
      (10000, 1.21257), (30000, 1.23770), (100000, 1.32566), (300000, 1.57699)]

def materials(lamtype):
    mi_addmaterial("Air", 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0)
    mi_addmaterial("Laminated", 30000, 30000, 0, 0, 0, 0.025, 0, FILL, lamtype, 0, 0, 0, 0)
    mi_addmaterial("Metal", 30000, 30000, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0)
    for h, b in BH:
        mi_addbhpoint("Laminated", b, h)
        mi_addbhpoint("Metal", b, h)

def label(x, y, mat, circ, turns, group):
    mi_addblocklabel(x, y); mi_selectlabel(x, y)
    mi_setblockprop(mat, 1, 0, circ, 0, group, turns); mi_clearselected()

def measure(comp, H, px, py, hidx):
    mi_analyze(1); mi_loadsolution()
    mo_groupselectblock(1)
    Bint = mo_blockintegral(comp); V = mo_blockintegral(10)
    W = mo_blockintegral(2); Wc = mo_blockintegral(17)
    mo_clearblock()
    Hpt = mo_getpointvalues(px, py)[hidx] if px is not None else 0.0
    mo_close(); mi_close()
    return Bint / V, W, Wc, V, Hpt

# Planar. Work in (u, v): u across the bar, v along the field. The bar spans
# the model in v between the two Neumann ends; the sheets sit at u = +-xs.
# fielddir "y": (x, y) = (u, v); "x": (x, y) = (v, u). lamdir is the axis
# the laminations run along; layered draws them as strips.
def planar(H, fielddir, lamdir, layered, name, gap=20):
    P = (lambda u, v: (u, v)) if fielddir == "y" else (lambda u, v: (v, u))
    lamtype = 1 if lamdir == "x" else 2
    newdocument(0)
    mi_probdef(0, "millimeters", "planar", 1e-8, 10, 30)
    materials(lamtype)
    mi_addboundprop("A0", 0, 0, 0, 0, 0, 0, 0, 0, 0)
    mi_addcircprop("sheets", H * HGT / 1000, 1)
    # gap: air between the bar and each sheet. Zero for the across case, so
    # the drawn layers span the whole space between the sheets and cannot
    # fringe sideways into air beside the bar -- that keeps the reference
    # one-dimensional, which it is not when a layered bar has free sides.
    xs = T / 2 + gap
    W = xs + 6
    def rect(u1, v1, u2, v2):
        x1, y1 = P(u1, v1); x2, y2 = P(u2, v2)
        mi_drawrectangle(x1, y1, x2, y2)
    rect(-W, -HGT / 2, W, HGT / 2)
    rect(-T / 2, -HGT / 2, T / 2, HGT / 2)
    rect(-xs - 1, -HGT / 2, -xs, HGT / 2)
    rect(xs, -HGT / 2, xs + 1, HGT / 2)
    mi_selectsegment(*P(-W, 0)); mi_setsegmentprop("A0", 0, 1, 0, 0); mi_clearselected()
    if not layered:
        label(*P(0, 0), "Laminated", "<None>", 0, 1)
    elif lamdir == fielddir:
        # strips along the field: lines at constant u across the 25 mm bar
        n = 10; p = T / n; m = FILL * p; g = p - m
        for i in range(n):
            u1 = -T / 2 + i * p + g / 2
            mi_drawline(*(P(u1, -HGT / 2) + P(u1, HGT / 2)))
            mi_drawline(*(P(u1 + m, -HGT / 2) + P(u1 + m, HGT / 2)))
            label(*P(u1 + m / 2, 0), "Metal", "<None>", 0, 1)
            label(*P(u1 - g / 4, 0), "Air", "<None>", 0, 1)
        label(*P(T / 2 - g / 4, 0), "Air", "<None>", 0, 1)
    else:
        # strips across the field: layers at constant v over the bar's length
        n = 20; p = HGT / n; m = FILL * p; g = p - m
        for i in range(n):
            v1 = -HGT / 2 + i * p + g / 2
            mi_drawline(*(P(-T / 2, v1) + P(T / 2, v1)))
            mi_drawline(*(P(-T / 2, v1 + m) + P(T / 2, v1 + m)))
            label(*P(0, v1 + m / 2), "Metal", "<None>", 0, 1)
            label(*P(0, v1 - g / 4), "Air", "<None>", 0, 1)
        label(*P(0, HGT / 2 - g / 4), "Air", "<None>", 0, 1)
    label(*P(-xs - 0.5, 0), "Air", "sheets", 1, 0)
    label(*P(xs + 0.5, 0), "Air", "sheets", -1, 0)
    if gap > 0:
        label(*P(-xs + 5, 0), "Air", "<None>", 0, 0)
        label(*P(xs - 5, 0), "Air", "<None>", 0, 0)
    label(*P(-W + 2.5, 0), "Air", "<None>", 0, 0)
    label(*P(W - 2.5, 0), "Air", "<None>", 0, 0)
    mi_saveas(BASE + "_%%s_%%g_%%d.fem" %% (name, H, layered))
    comp, hidx = (9, 6) if fielddir == "y" else (8, 5)
    px, py = (None, None) if layered else (0.0, 0.0)
    return measure(comp, H, px, py, hidx)

# Planar, field ACROSS the laminations. A current sheet cannot drive this
# case one-dimensionally (flux leaks out past the air gaps near the sheet),
# so the flux density is prescribed instead: A linear across the model,
# B0 along v, everywhere. Layers at constant v span the whole width. The
# bulk block then has one H; the drawn stack alternates H_fe and H_air, and
# its mean, FILL*H_fe + (1-FILL)*H_air, is what the bulk H must equal.
def across(B0, fielddir, layered, name):
    P = (lambda u, v: (u, v)) if fielddir == "y" else (lambda u, v: (v, u))
    lamtype = 1 if fielddir == "y" else 2      # laminations run across the field
    newdocument(0)
    mi_probdef(0, "millimeters", "planar", 1e-8, 10, 30)
    materials(lamtype)
    W = T / 2
    mi_addboundprop("Apos", B0 * W / 1000, 0, 0, 0, 0, 0, 0, 0, 0)
    mi_addboundprop("Aneg", -B0 * W / 1000, 0, 0, 0, 0, 0, 0, 0, 0)
    x1, y1 = P(-W, -HGT / 2); x2, y2 = P(W, HGT / 2)
    mi_drawrectangle(x1, y1, x2, y2)
    mi_selectsegment(*P(-W, 0)); mi_setsegmentprop("Apos", 0, 1, 0, 0); mi_clearselected()
    mi_selectsegment(*P(W, 0)); mi_setsegmentprop("Aneg", 0, 1, 0, 0); mi_clearselected()
    n = 20; p = HGT / n; m = FILL * p; g = p - m
    if not layered:
        label(*P(0, 0), "Laminated", "<None>", 0, 1)
    else:
        for i in range(n):
            v1 = -HGT / 2 + i * p + g / 2
            mi_drawline(*(P(-W, v1) + P(W, v1)))
            mi_drawline(*(P(-W, v1 + m) + P(W, v1 + m)))
            label(*P(0, v1 + m / 2), "Metal", "<None>", 0, 1)
            label(*P(0, v1 - g / 4), "Air", "<None>", 0, 1)
        label(*P(0, HGT / 2 - g / 4), "Air", "<None>", 0, 1)
    mi_saveas(BASE + "_%%s_%%g_%%d.fem" %% (name, B0, layered))
    comp, hidx = (9, 6) if fielddir == "y" else (8, 5)
    mi_analyze(1); mi_loadsolution()
    mo_groupselectblock(1)
    Bint = mo_blockintegral(comp); V = mo_blockintegral(10)
    Wf = mo_blockintegral(2); Wc = mo_blockintegral(17)
    mo_clearblock()
    if not layered:
        Hpt = mo_getpointvalues(*P(0, 0))[hidx]
    else:
        vfe = -HGT / 2 + 10 * p + g / 2 + m / 2     # middle of an iron layer
        vair = -HGT / 2 + 10 * p                    # middle of the air gap before it
        Hfe = mo_getpointvalues(*P(0, vfe))[hidx]
        Hair = mo_getpointvalues(*P(0, vair))[hidx]
        Hpt = FILL * abs(Hfe) + (1 - FILL) * abs(Hair)
    mo_close(); mi_close()
    return Bint / V, Wf, Wc, V, Hpt

# Axisymmetric: a cylinder of radius T on the axis inside a one-turn current
# sheet; Neumann ends and a natural outer boundary make it the infinite
# solenoid, H = I/height inside. Laminations parallel to z are cylindrical
# shells (LamType 2); layered draws them.
def axi(H, layered, name):
    newdocument(0)
    mi_probdef(0, "millimeters", "axi", 1e-8, 1, 30)
    materials(2)
    mi_addcircprop("sheets", H * HGT / 1000, 1)
    xs = T + 20
    W = xs + 6
    mi_drawrectangle(0, -HGT / 2, W, HGT / 2)
    mi_drawline(T, -HGT / 2, T, HGT / 2)
    mi_drawrectangle(xs, -HGT / 2, xs + 1, HGT / 2)
    if not layered:
        label(T / 2, 0, "Laminated", "<None>", 0, 1)
    else:
        n = 10; p = T / n; m = FILL * p; g = p - m
        for i in range(n):
            r1 = i * p + g / 2
            mi_drawline(r1, -HGT / 2, r1, HGT / 2)
            mi_drawline(r1 + m, -HGT / 2, r1 + m, HGT / 2)
            label(r1 + m / 2, 0, "Metal", "<None>", 0, 1)
            label(r1 - g / 4, 0, "Air", "<None>", 0, 1)
        label(T - g / 4, 0, "Air", "<None>", 0, 1)
    label(xs + 0.5, 0, "Air", "sheets", 1, 0)
    label(xs - 5, 0, "Air", "<None>", 0, 0)
    label(W - 2.5, 0, "Air", "<None>", 0, 0)
    mi_saveas(BASE + "_%%s_%%g_%%d.fem" %% (name, H, layered))
    px, py = (None, None) if layered else (T / 2, 0.0)
    return measure(9, H, px, py, 6)

# Axisymmetric, MIXED field. A pot-core-like fixture: linear high-mu post,
# yoke and outer wall, a coil in the window, and a top plate laminated as
# discs stacked along z (LamType 1). Flux enters the plate axially from the
# post and the wall (across the laminations) and runs radially through it
# (along them), so one on-edge laminated region carries both components at
# once; near the post Br and Bz are both several tenths of a tesla. There is
# no closed form, so the bulk plate (meshed at 0.2 mm) is compared with the
# plate drawn as 16 discs on the coil's flux linkage, the total energy and
# coenergy, and the plate's mean Br, energy and coenergy. Both converge on
# the same values as their meshes refine (bulk automesh -> 0.5 -> 0.2 mm,
# discs 8 -> 16 -> 32); at this resolution they agree to 0.1-0.4 percent. A wrong
# radial component would cut the plate's radial permeability by thousands.
def axiMixed(NI, layered, name):
    RP, RW1, RW2, HC, ZP0, ZP1, RB, ZB = 10.0, 30.0, 36.0, 20.0, 20.0, 30.0, 60.0, 60.0
    def lab(x, y, mat, circ, turns, group, size=0):
        mi_addblocklabel(x, y); mi_selectlabel(x, y)
        mi_setblockprop(mat, 1 if size == 0 else 0, size, circ, 0, group, turns); mi_clearselected()
    newdocument(0)
    mi_probdef(0, "millimeters", "axi", 1e-8, 1, 30)
    materials(1)
    mi_addmaterial("Core", 5000, 5000, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0)
    mi_addboundprop("A0", 0, 0, 0, 0, 0, 0, 0, 0, 0)
    mi_addcircprop("NI", NI, 1)
    mi_drawrectangle(0, -ZB / 2, RB, ZB)
    for (x, y) in ((RB, 0), (RB / 2, -ZB / 2), (RB / 2, ZB)):
        mi_selectsegment(x, y)
    mi_setsegmentprop("A0", 0, 1, 0, 0); mi_clearselected()
    mi_drawrectangle(0, -8, RW2, 0)
    mi_drawrectangle(0, 0, RP, HC)
    mi_drawrectangle(RW1, 0, RW2, HC)
    mi_drawrectangle(RP + 2, 2, RW1 - 2, HC - 2)
    mi_drawrectangle(0, ZP0, RW2, ZP1)
    lab(RW2 / 2, -4, "Core", "<None>", 0, 0)
    lab(RP / 2, HC / 2, "Core", "<None>", 0, 0)
    lab((RW1 + RW2) / 2, HC / 2, "Core", "<None>", 0, 0)
    lab((RP + RW1) / 2, HC / 2, "Air", "NI", 1, 0)
    lab(RP + 1, 1, "Air", "<None>", 0, 0)
    lab(RB - 5, ZB - 5, "Air", "<None>", 0, 0)
    if not layered:
        lab(RW2 / 2, (ZP0 + ZP1) / 2, "Laminated", "<None>", 0, 1, 0.2)
    else:
        n = 16; p = (ZP1 - ZP0) / n; m = FILL * p; g = p - m
        for i in range(n):
            z1 = ZP0 + i * p + g / 2
            mi_drawline(0, z1, RW2, z1); mi_drawline(0, z1 + m, RW2, z1 + m)
            lab(RW2 / 2, z1 + m / 2, "Metal", "<None>", 0, 1)
            lab(RW2 / 2, z1 - g / 4, "Air", "<None>", 0, 1)
        lab(RW2 / 2, ZP1 - g / 4, "Air", "<None>", 0, 1)
    mi_saveas(BASE + "_%%s_%%g_%%d.fem" %% (name, NI, layered))
    mi_analyze(1); mi_loadsolution()
    mo_groupselectblock(1)
    Br = mo_blockintegral(8); V = mo_blockintegral(10)
    Wp = mo_blockintegral(2); Wcp = mo_blockintegral(17)
    mo_clearblock()
    mo_groupselectblock()
    Wt = mo_blockintegral(2); Wct = mo_blockintegral(17)
    mo_clearblock()
    lam = mo_getcircuitproperties("NI")[2]
    mo_close(); mi_close()
    return lam, Wt, Wct, Br / V, Wp, Wcp

for NI in (500, 2000):
    for layered in (0, 1):
        vals = axiMixed(NI, layered, "axiMixed")
        print("MIXED %%g %%d " %% (NI, layered) + " ".join("%%.10g" %% v for v in vals))

# (name, builder, levels): levels are applied H in A/m, except for the
# across cases, where they are the prescribed B0 in tesla and the H written
# out is the drawn stack's mean H.
# Axisymmetric, field ACROSS the laminations: discs stacked along z
# (laminations parallel to r, LamType 1) in a uniform axial field, which is
# prescribed as A = B0*r/2 on the outer radius; Neumann ends keep B_r = 0.
# Exercises the other axisymmetric branch, with B_z as the across component.
def acrossAxi(B0, layered, name):
    newdocument(0)
    mi_probdef(0, "millimeters", "axi", 1e-8, 1, 30)
    materials(1)
    W = T
    # A = A0 + A1*r + A2*z with r, z in the problem's length units (mm here),
    # so B0*r/2 in Wb/m needs A1 = B0/2 per metre = B0/2000 per mm.
    mi_addboundprop("Aout", 0, B0 / 2000, 0, 0, 0, 0, 0, 0, 0)
    mi_drawrectangle(0, -HGT / 2, W, HGT / 2)
    mi_selectsegment(W, 0); mi_setsegmentprop("Aout", 0, 1, 0, 0); mi_clearselected()
    n = 20; p = HGT / n; m = FILL * p; g = p - m
    if not layered:
        label(W / 2, 0, "Laminated", "<None>", 0, 1)
    else:
        for i in range(n):
            z1 = -HGT / 2 + i * p + g / 2
            mi_drawline(0, z1, W, z1)
            mi_drawline(0, z1 + m, W, z1 + m)
            label(W / 2, z1 + m / 2, "Metal", "<None>", 0, 1)
            label(W / 2, z1 - g / 4, "Air", "<None>", 0, 1)
        label(W / 2, HGT / 2 - g / 4, "Air", "<None>", 0, 1)
    mi_saveas(BASE + "_%%s_%%g_%%d.fem" %% (name, B0, layered))
    mi_analyze(1); mi_loadsolution()
    mo_groupselectblock(1)
    Bint = mo_blockintegral(9); V = mo_blockintegral(10)
    Wf = mo_blockintegral(2); Wc = mo_blockintegral(17)
    mo_clearblock()
    if not layered:
        Hpt = mo_getpointvalues(W / 2, 0)[6]
    else:
        zfe = -HGT / 2 + 10 * p + g / 2 + m / 2
        zair = -HGT / 2 + 10 * p
        Hfe = mo_getpointvalues(W / 2, zfe)[6]
        Hair = mo_getpointvalues(W / 2, zair)[6]
        Hpt = FILL * abs(Hfe) + (1 - FILL) * abs(Hair)
    mo_close(); mi_close()
    return Bint / V, Wf, Wc, V, Hpt

cases = [("alongY",  lambda L, lay: planar(L, "y", "y", lay, "alongY"), (30, 1000, 10000)),
         ("alongX",  lambda L, lay: planar(L, "x", "x", lay, "alongX"), (30, 10000)),
         ("acrossY", lambda L, lay: across(L, "y", lay, "acrossY"),     (1.30,)),
         ("acrossX", lambda L, lay: across(L, "x", lay, "acrossX"),     (1.30,)),
         ("axiZ",    lambda L, lay: axi(L, lay, "axiZ"),                 (1000, 10000)),
         ("acrossAxi", lambda L, lay: acrossAxi(L, lay, "acrossAxi"),    (1.30,))]
for name, build, levels in cases:
    for L in levels:
        rows = {}
        for layered in (1, 0):
            rows[layered] = build(L, layered)
        H = rows[1][4] if name.startswith("across") else L
        for layered in (0, 1):
            Bavg, W, Wc, V, Hpt = rows[layered]
            print("RESULT %%s %%.10g %%d %%.10g %%.10g %%.10g %%.10g %%.10g" %% (name, H, layered, Bavg, W, Wc, V, Hpt))
print("SCRIPT-DONE")
'''

def main():
    if not os.path.exists(BIN):
        print("LamFillTest: no engine at %s" % BIN)
        return 2
    script = BASE + "_run.py"
    with open(script, "w") as f:
        f.write(SCRIPT % BASE)
    try:
        r = subprocess.run([BIN, "-run", script], capture_output=True, text=True, timeout=900)
        out = r.stdout + r.stderr
        if "SCRIPT-DONE" not in out:
            print("LamFillTest: engine run failed (rc %s)\n%s" % (r.returncode, out[-3000:]))
            return 1
        res = {}; mixed = {}
        for line in out.splitlines():
            if line.startswith("RESULT "):
                _, name, H, layered, Bavg, W, Wc, V, Hpt = line.split()
                res[(name, float(H), int(layered))] = tuple(float(x) for x in (Bavg, W, Wc, V, Hpt))
            elif line.startswith("MIXED "):
                p = line.split()
                mixed[(float(p[1]), int(p[2]))] = tuple(float(x) for x in p[3:])
        bad = 0
        # mixed-field axisymmetric plate: bulk vs 16 drawn discs, quantity by quantity
        names = ("flux linkage", "total W", "total W'", "plate mean Br", "plate W", "plate W'")
        tols = (0.01, 0.01, 0.01, 0.01, 0.015, 0.015)
        for NI in sorted({k[0] for k in mixed}):
            if (NI, 0) not in mixed or (NI, 1) not in mixed:
                print("FAIL: mixed-field case at NI=%g incomplete" % NI); bad += 1; continue
            b, d = mixed[(NI, 0)], mixed[(NI, 1)]
            rel = [x / y - 1 for x, y in zip(b, d)]
            ok = all(abs(r) < t for r, t in zip(rel, tols))
            print("axiMixed NI=%5g  " % NI + "  ".join("%s %+.1e" % (n, r) for n, r in zip(names, rel)) + ("" if ok else "  FAIL"))
            bad += not ok
        if len(mixed) != 4:
            print("FAIL: expected 4 mixed-field results, got %d" % len(mixed)); bad += 1
        print("%-7s %8s  %-9s %-9s %8s %8s %8s %8s" %
              ("case", "H", "B bulk", "B drawn", "dB", "dW", "dW'", "ident"))
        for (name, H, layered), (Bb, Wb, Wcb, V, Hpt) in sorted(res.items()):
            if layered:
                continue
            Br, Wr, Wcr, Vr, _ = res[(name, H, 1)]
            # the field direction is the test's choice; only magnitudes matter
            Bb, Br, Hpt = abs(Bb), abs(Br), abs(Hpt)
            dB = Bb / Br - 1
            dW = Wb / Wr - 1
            dWc = Wcb / Wcr - 1
            ident = (Wb + Wcb) / (Bb * H * V)
            identr = (Wr + Wcr) / (Br * H * Vr)
            dH = Hpt / H - 1
            ok = (abs(dB) < 2e-3 and abs(dW) < 5e-3 and abs(dWc) < 5e-3 and
                  abs(ident - 1) < 2e-3 and abs(identr - 1) < 2e-3 and abs(dH) < 5e-3)
            print("%-7s %8g  %9.5f %9.5f %8.1e %8.1e %8.1e %8.5f  H_pt/H-1=%8.1e %s" %
                  (name, H, Bb, Br, dB, dW, dWc, ident, dH, "" if ok else "FAIL"))
            bad += not ok
        # physical sanity: along the laminations the stack saturates at FILL x Bs
        Bs = res[("alongY", 10000.0, 0)][0]
        if not (0.94 < Bs < 0.96):
            print("FAIL: along-lamination saturation %.4f T, expected ~0.78 x 1.21" % Bs)
            bad += 1
        if len(res) != 20:
            print("FAIL: expected 20 results, got %d" % len(res))
            bad += 1
        print("LamFillTest: %s" % ("PASS" if bad == 0 else "%d FAILURE(S)" % bad))
        return 1 if bad else 0
    finally:
        for f in glob.glob(BASE + "*"):
            try: os.remove(f)
            except OSError: pass

if __name__ == "__main__":
    sys.exit(main())
