# Core Loss Calculation Script (Bulk Data Version)
#
# David Meeker
# dmeeker@ieee.org
# AGE version 18Feb2017
#
# Translated from mySPMLossScriptAGB_fast.m to run natively inside femm-qt.
# Runs in-process with no MATLAB, no Octave and no CPython installation:
#
#     femm_headless -run examples/mySPMLossScriptAGB_fast.py
#
# This is the fast twin of mySPMLossScriptAGB.py.  Both compute the same
# losses; the difference is entirely in how the field data comes back from the
# post-processor.  The plain version calls mo_getpointvalues once per element
# per rotor position -- about two million calls over a full spin -- while this
# one asks for the whole mesh in a single call per position.
#
# WHAT DIFFERS FROM THE .m, and each is noted again where it happens:
#
#  1. mo_getallelements() returns FOUR TYPED ARRAYS here, not the (ne x 7)
#     matrix octavefemm hands back:
#
#         conn, ctr, area, grp = mo_getallelements()
#
#     conn is (ne,3) int32 1-based connectivity, ctr is (ne,2) float
#     centroids, area is (ne,) float, grp is (ne,) int32.  Connectivity and
#     group are genuinely integers, so an all-double matrix would waste half
#     its memory on them and hand back a float array that np.take will not
#     accept as a gather index.
#
#  2. mo_getallb() is (ne,2) and mo_getalla() is (ne,) -- real for a DC
#     problem like this one, complex if the problem had a frequency.
#
#  3. mi_setcurrent is not a femm-qt command; mi_modifycircprop does the same
#     job and is what octavefemm's wrapper calls underneath.
#
#  4. The .m allocates b and A over EVERY element and leaves the air columns
#     full of zeros.  This gathers the elements that actually carry a
#     contribution instead -- on the shipped model that is 8701 of 18016 for
#     flux density and 2902 for vector potential, so the stored time series is
#     a third of the size for identical arithmetic.
#
# Memory: the time series dominates, at 8 bytes per element per step per
# component.  With the coarse mesh this script asks for that is a few tens of
# MB.  A much finer mesh will not fit; coarsen the mesh rather than the
# sampling, since the loss integral is far more forgiving of mesh than of
# harmonics.

import math
import os
import time

import numpy as np

# ============================================================================
# Start User-Defined Parameters
# ============================================================================

MyModel = "my_SPM_motor_AGB.fem"

wbase = 4000 / 60           # base frequency, Hz: 4000 rev/min

SpeedMin = 100              # RPM
SpeedMax = 8000             # RPM
SpeedStep = 100             # RPM

# Winding properties
MyIdCurrent = 0             # direct-axis current amplitude scaling
MyIqCurrent = 2             # quadrature-axis current amplitude scaling
MyLowestHarmonic = 2        # lowest harmonic present in the stator winding
AWG = 25                    # magnet wire gauge used in the winding
WindingFill = 0.3882
PhaseResistance = 0.223 * 2.333   # phase resistance incl. end turns at 20 C
TemperatureRise = 100             # temperature increase, degrees C

# Magnet properties
RotorMagnets = 16
omag = 0.556e6              # conductivity of sintered NdFeB, S/m

# Core properties
ce = 0.530                  # eddy current coefficient, W/(m^3 T^2 Hz^2)
ch = 143.0                  # hysteresis coefficient, W/(m^3 T^2 Hz)
cs = 0.95                   # lamination stacking factor

# Rotor angle increment, in degrees
dk = 1.0

# ============================================================================
# End User-Defined Parameters
# ============================================================================

deg = math.pi / 180.0

# angle through which to spin the rotor, degrees, and the number of steps
n = 360.0 / MyLowestHarmonic
ns = int(round(n / dk))

# Proximity-effect loss parameters for the phase windings.  The low-frequency
# approximation is used, since the wire is much smaller than a skin depth at
# the frequencies of interest.
dwire = 0.324861 * 0.0254 * math.exp(-0.115942 * AWG)   # wire diameter, m
owire = 58.0e6 / (1 + TemperatureRise * 0.004)          # wire conductivity, S/m
cePhase = (math.pi ** 2 / 8) * dwire * dwire * WindingFill * owire

# ============================================================================
# Perform a series of finite element analyses
# ============================================================================

open(MyModel)
mi_smartmesh(0)              # relatively coarse mesh, to save time
mi_saveas("temp.fem")

iron_idx = None              # elements whose flux density is recorded
mag_idx = None               # elements whose vector potential is recorded
vol_iron = None              # element volumes, m^3, gathered the same way
vol_mag = None
grp_iron = None              # group numbers, gathered the same way
grp_mag = None
bx = by = Amag = None        # (ns, nsel) time series
probinfo = None

tq = [0.0] * ns

for kk in range(ns):
    t_start = time.time()
    k = kk * dk              # rotor angle, degrees

    # The model uses an air gap element, so rotor position is set by modifying
    # the rotor-position parameter of the AGE boundary rather than by moving
    # any geometry.  That is also why the mesh never changes, which is what
    # makes element i the same element at every rotor position.
    mi_modifyboundprop("AGE", 10, k)

    # Set the phase currents for this rotor position.
    tta = (RotorMagnets / 2) * k * deg
    Id = (math.cos(tta), math.cos(tta - 2 * math.pi / 3),
          math.cos(tta + 2 * math.pi / 3))
    Iq = (-math.sin(tta), -math.sin(tta - 2 * math.pi / 3),
          -math.sin(tta + 2 * math.pi / 3))
    for name, i_d, i_q in zip(("A", "B", "C"), Id, Iq):
        # No mi_setcurrent in femm-qt; this is what it would do -- see note 3.
        mi_modifycircprop(name, 1, MyIdCurrent * i_d + MyIqCurrent * i_q)

    mi_analyze(1)
    mi_loadsolution()
    mo_smooth("off")         # flux smoothing off

    if kk == 0:
        # Record the mesh once.  Four typed arrays, one call -- see note 1.
        conn, ctr, area, grp = mo_getallelements()
        ne = len(area)

        # Split the elements by what each contributes.  Done in Python rather
        # than with array masks because it runs exactly once and the index
        # arrays it produces are what np.take wants anyway.
        gl = [int(x) for x in grp]
        iron_idx = np.array([i for i in range(ne) if 0 < gl[i] <= 10],
                            dtype=np.int32)
        mag_idx = np.array([i for i in range(ne) if gl[i] > 10],
                           dtype=np.int32)

        # Depth into the page and the drawing's length unit, both needed to
        # integrate losses over the machine's volume.
        probinfo = mo_getprobleminfo()
        h = probinfo[2]              # machine depth, into the page
        lengthunits = probinfo[3]    # length of one drawing unit, in meters
        vol = area * (h * lengthunits * lengthunits)

        vol_iron = np.take(vol, iron_idx)
        vol_mag = np.take(vol, mag_idx)
        grp_iron = np.take(grp, iron_idx)
        grp_mag = np.take(grp, mag_idx)

        # Gathered time series -- see note 4.
        bx = np.zeros((ns, len(iron_idx)))
        by = np.zeros((ns, len(iron_idx)))
        Amag = np.zeros((ns, len(mag_idx)))

        print("%d elements: %d carrying flux, %d in magnets"
              % (ne, len(iron_idx), len(mag_idx)))

    # The whole mesh in two calls, rather than one call per element -- note 2.
    B = mo_getallb()
    A = mo_getalla()
    bx[kk] = np.take(B[:, 0], iron_idx)
    by[kk] = np.take(B[:, 1], iron_idx)
    Amag[kk] = np.take(A, mag_idx)

    # mo_gapintegral returns a tuple of components; [0] is the torque.
    _gi = mo_gapintegral("AGE", 0)
    tq[kk] = _gi[0] if isinstance(_gi, tuple) else _gi
    mo_close()

    print("%6.2f of %.0f deg :: %6.2f s :: %8.4f N*m"
          % (k, n, time.time() - t_start, tq[kk]))

mi_close()

# Clean up after the finite element runs, as the .m does.
for _f in ("temp.fem", "temp.ans"):
    try:
        os.remove(_f)
    except OSError:
        pass

# ============================================================================
# Add Up Core Losses
# ============================================================================

# The square of the amplitude of each harmonic at every element centroid.
# femm-qt's fft takes an axis, so this transforms the whole matrix down the
# time axis in one call, exactly as MATLAB's fft(b) does -- no per-element
# loop.  The 2/ns is MATLAB's single-sided amplitude scaling.
scale = 2.0 / ns
bxfft = np.abs(np.fft.fft(bx, axis=0)) * scale
byfft = np.abs(np.fft.fft(by, axis=0)) * scale
bsq = bxfft * bxfft + byfft * byfft            # (ns, n_iron)

# Contract over elements NOW, once per material, leaving three length-ns
# spectra.  The .m carries the whole (harmonic x element) matrix into the
# speed loop and contracts it there; doing it here is the same arithmetic and
# keeps nothing large alive across the sweep.
#
# vol_iron * (grp_iron == g) is a per-element weight that is zero off the
# material of interest, and multiplying an (ns, nsel) matrix by an (nsel,)
# vector broadcasts across rows.  np.sum(..., axis=1) then contracts.
#
# The .m writes this contraction as a matrix product, `bsq * (v.*g1)`, and
# `bsq @ (vol_iron * (grp_iron == 10))` would work here too.  np.sum over the
# element axis is the same arithmetic and reads better against the mask, which
# has to be built either way.
Srotor = np.sum(bsq * (vol_iron * (grp_iron == 10)), axis=1)
Sstator = np.sum(bsq * (vol_iron * (grp_iron == 1)), axis=1)
Sprox = np.sum(bsq * (vol_iron * (grp_iron == 2)), axis=1)

del bxfft, byfft, bsq

# Magnets.  The vector potential's harmonics give the induced electric field,
# and the net current in each magnet must be zero at every harmonic -- so the
# volume-weighted average over each magnet is subtracted before the loss is
# accumulated.  Done magnet by magnet, since each is independently isolated.
Jm = np.fft.fft(Amag, axis=0) * scale          # (ns, n_mag), complex

for kmag in range(1, RotorMagnets + 1):
    sel = (grp_mag == (10 + kmag))             # 1.0 on this magnet, 0.0 off it
    wgt = vol_mag * sel
    vmag = np.sum(wgt)
    if vmag == 0:
        continue
    # Volume-weighted average of each harmonic over this magnet, then
    # subtracted from its own elements only.  reshape makes the (ns,) average
    # broadcast down the columns; without it the shapes will not line up.
    Jo = np.sum(Jm * wgt, axis=1) / vmag
    Jm = Jm - Jo.reshape((ns, 1)) * sel

Smag = np.sum((np.abs(Jm) ** 2) * vol_mag, axis=1)

del Jm

Iphase = math.sqrt(MyIdCurrent ** 2 + MyIqCurrent ** 2) / math.sqrt(2)
PhaseOhmic = 3 * (PhaseResistance * (1 + TemperatureRise * 0.004)) * Iphase ** 2

# Only the lower half of the spectrum is counted: the upper half mirrors it,
# and counting both would double every harmonic.  This is the .m's
# `w=0:(ns-1); w=w.*(w<(ns/2))` verbatim.
#
# dtype=np.float is not decoration: np.arange(n) defaults to int16 here where
# numpy would give int64.  It is not a correctness hazard -- ulab widens to
# float64 at 32769, exactly where int16 would overflow -- but an integer index
# vector multiplied by a frequency is asking for a surprise, and the comparison
# below wants floats anyway.
half = np.arange(ns, dtype=np.float)
half = half * (half < ns / 2)

speeds = []
rotor_loss = []
stator_loss = []
magnet_loss = []
ohmic_loss = []
prox_loss = []
total_loss = []

thisSpeed = SpeedMin
while thisSpeed <= SpeedMax:
    thisFrequency = thisSpeed / 60.0          # mechanical speed, Hz
    w = half * (MyLowestHarmonic * thisFrequency)

    core = ch * w + ce * w * w
    r_l = float(np.sum(core * Srotor)) / cs
    s_l = float(np.sum(core * Sstator)) / cs
    p_l = float(np.sum((cePhase * w * w) * Sprox))
    tp = 2 * math.pi * w
    m_l = 0.5 * float(np.sum((omag * tp * tp) * Smag))

    speeds.append(thisSpeed)
    rotor_loss.append(r_l)
    stator_loss.append(s_l)
    magnet_loss.append(m_l)
    ohmic_loss.append(PhaseOhmic)
    prox_loss.append(p_l)
    total_loss.append(r_l + s_l + p_l + PhaseOhmic + m_l)

    thisSpeed += SpeedStep

# ============================================================================
# Results
# ============================================================================

print("")
print("  Speed     Rotor    Stator    Magnet     Ohmic      Prox     Total")
print("    RPM     Watts     Watts     Watts     Watts     Watts     Watts")
for i in range(len(speeds)):
    print("%7d %9.2f %9.2f %9.2f %9.2f %9.2f %9.2f"
          % (speeds[i], rotor_loss[i], stator_loss[i], magnet_loss[i],
             ohmic_loss[i], prox_loss[i], total_loss[i]))

# The loss plot, drawn with real matplotlib.  femm-qt ships none of it: the
# import finds a Python 3 on the machine and proxies the calls to it, so this
# is the same matplotlib a MATLAB user would reach for and takes the same
# arguments as the .m's plot/xlabel/legend calls.
#
# Guarded, because the table above is the point and a machine with no suitable
# Python should not lose a solve to a failed import.  femm-qt's own plot() is
# the fallback -- no CPython needed, same six curves.
try:
    import matplotlib.pyplot as plt

    series = ((rotor_loss, "Rotor Core"), (stator_loss, "Stator Core"),
              (magnet_loss, "Magnets"), (ohmic_loss, "Coil Ohmic"),
              (prox_loss, "Coil Proximity"), (total_loss, "Total Loss"))
    fig, ax = plt.subplots()
    for y, lbl in series:
        ax.plot(speeds, y, label=lbl)
    ax.set_xlabel("Speed, RPM")
    ax.set_ylabel("Total Losses, Watts")
    ax.set_title("Loss versus Speed")
    ax.legend(loc="upper left")
    ax.grid(True)
    # fig.savefig("loss-vs-speed.png", dpi=150)   # for an unattended run
    plt.show()
except Exception as e:
    print("matplotlib unavailable (%s); using femm-qt's built-in plot" % e)
    plot("Loss versus Speed", speeds, "Speed, RPM",
         rotor_loss, "Rotor Core",
         stator_loss, "Stator Core",
         magnet_loss, "Magnets",
         ohmic_loss, "Coil Ohmic",
         prox_loss, "Coil Proximity",
         total_loss, "Total Loss")

# Torque against rotor angle, if wanted:
# plt.plot([i * dk for i in range(ns)], tq)
# plt.xlabel("Rotor Angle, Degrees"); plt.ylabel("Torque, N*m")
# plt.title("Torque on Rotor vs. Angle"); plt.show()
