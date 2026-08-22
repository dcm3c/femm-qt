# Core Loss Calculation Script
#
# David Meeker
# dmeeker@ieee.org
# AGE version 18Feb2017
#
# Translated from mySPMLossScriptAGB.m to run natively inside femm-qt.  Runs
# in-process with no MATLAB, no Octave and no CPython installation:
#
#     femm_headless -run examples/mySPMLossScriptAGB.py
#
# TWO THINGS DIFFER FROM THE .m, both noted again where they occur:
#
#  1. femm-qt's FFT is one-dimensional, so where MATLAB writes fft(b) over a
#     whole matrix this loops the transform over elements.  Same arithmetic,
#     one column at a time.  The length is unrestricted -- the rotor is
#     stepped 180 times at 1 degree, exactly as the .m does.
#
#  2. mi_setcurrent, mo_geta and mo_getb are not femm-qt commands; the
#     underlying mi_modifycircprop and mo_getpointvalues are used instead.
#
# Memory note: the flux-density time series is the large object here, about
# 2 * 8 * (steps) * (number of elements carrying flux) bytes.  With the coarse
# mesh this script asks for it is a few tens of MB and comfortable.  A much
# finer mesh will not fit; coarsen the mesh rather than the sampling, since the
# loss integral is far more forgiving of mesh than of harmonics.

import math
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

# Rotor angle increment, in degrees.

# ============================================================================
# End User-Defined Parameters
# ============================================================================

deg = math.pi / 180.0

# angle through which to spin the rotor, degrees, and the increment
dk = 1.0
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

nn = 0
elem_area = None
elem_group = None
elem_cx = None
elem_cy = None

# Indices into the element list, split by what each element contributes.
iron_idx = []                # 0 < group <= 10: flux density is recorded
mag_idx = []                 # group > 10: vector potential is recorded

bx_ts = None                 # flux density time series, flat [i*ns + k]
by_ts = None
a_ts = None                  # vector potential time series, flat [i*ns + k]

tq = [0.0] * ns
probinfo = None

for kk in range(ns):
    t_start = time.time()
    k = kk * dk              # rotor angle, degrees

    # The model uses an air gap element, so rotor position is set by modifying
    # the rotor-position parameter of the AGE boundary rather than by moving
    # any geometry.
    mi_modifyboundprop("AGE", 10, k)

    # Set the phase currents for this rotor position.
    tta = (RotorMagnets / 2) * k * deg
    Id = (math.cos(tta), math.cos(tta - 2 * math.pi / 3),
          math.cos(tta + 2 * math.pi / 3))
    Iq = (-math.sin(tta), -math.sin(tta - 2 * math.pi / 3),
          -math.sin(tta + 2 * math.pi / 3))
    for name, i_d, i_q in zip(("A", "B", "C"), Id, Iq):
        # femm-qt has no mi_setcurrent; this is what it would do -- see note 2.
        mi_modifycircprop(name, 1, MyIdCurrent * i_d + MyIqCurrent * i_q)

    mi_analyze(1)
    mi_loadsolution()
    mo_smooth("off")         # flux smoothing off

    if kk == 0:
        # Record the mesh on the first pass.  The air gap element means every
        # later solve reuses this same mesh, so element i is the same element
        # at every rotor position -- which is what makes a per-element time
        # series meaningful.
        # int(): every femm getter returns a double, as FEMM's own scripting
        # does, and ulab will not take a float as an array length.
        nn = int(mo_numelements())
        elem_cx = np.zeros(nn)
        elem_cy = np.zeros(nn)
        elem_area = np.zeros(nn)
        elem_group = np.zeros(nn)
        for m in range(nn):
            elm = mo_getelement(m + 1)
            elem_cx[m] = elm[3]      # centroid x
            elem_cy[m] = elm[4]      # centroid y
            elem_area[m] = elm[5]    # area, in the drawing's length units
            elem_group[m] = elm[6]   # group number
            g = elm[6]
            if g > 10:
                mag_idx.append(m)
            elif g > 0:
                iron_idx.append(m)

        bx_ts = np.zeros(len(iron_idx) * ns)
        by_ts = np.zeros(len(iron_idx) * ns)
        a_ts = np.zeros(len(mag_idx) * ns)
        print("%d elements: %d carrying flux, %d in magnets"
              % (nn, len(iron_idx), len(mag_idx)))

    # Store element flux densities and vector potentials.
    for i in range(len(iron_idx)):
        m = iron_idx[i]
        # No mo_getb; getpointvalues[1] and [2] are Bx and By -- see note 2.
        v = mo_getpointvalues(elem_cx[m], elem_cy[m])
        bx_ts[i * ns + kk] = v[1]
        by_ts[i * ns + kk] = v[2]
    for i in range(len(mag_idx)):
        m = mag_idx[i]
        # No mo_geta; getpointvalues[0] is A -- see note 2.
        a_ts[i * ns + kk] = mo_getpointvalues(elem_cx[m], elem_cy[m])[0]

    # Depth into the page and the drawing's length unit, both needed to
    # integrate losses over the machine's volume.
    probinfo = mo_getprobleminfo()

    # mo_gapintegral returns a tuple of components; [0] is the torque.
    _gi = mo_gapintegral("AGE", 0)
    tq[kk] = _gi[0] if isinstance(_gi, tuple) else _gi
    mo_close()

    print("%6.2f of %.0f deg :: %6.1f s :: %8.4f N*m"
          % (k, n, time.time() - t_start, tq[kk]))

mi_close()

# Clean up after the finite element runs, as the .m does.
import os
for _f in ("temp.fem", "temp.ans"):
    try:
        os.remove(_f)
    except OSError:
        pass

# ============================================================================
# Add Up Core Losses
# ============================================================================

# Volume of each element in m^3.
h = probinfo[2]             # machine depth, into the page
lengthunits = probinfo[3]   # length of one drawing unit, in meters
vol = elem_area * (h * lengthunits * lengthunits)

# Harmonic sums, reduced over elements as we go.  MATLAB keeps the whole
# (harmonic x element) matrix and contracts it at the end; contracting inside
# the loop gives the same numbers and keeps only a handful of length-ns
# vectors alive, which is what makes a fine mesh survivable here.
Srotor = np.zeros(ns)       # sum over rotor iron   of |B_j|^2 * volume
Sstator = np.zeros(ns)      # sum over stator iron  of |B_j|^2 * volume
Sprox = np.zeros(ns)        # sum over coil         of |B_j|^2 * volume

scale = 2.0 / ns
for i in range(len(iron_idx)):
    m = iron_idx[i]
    lo = i * ns
    # One transform per element -- see note 1.
    fx = np.fft.fft(bx_ts[lo:lo + ns])
    fy = np.fft.fft(by_ts[lo:lo + ns])
    bsq = ((np.abs(fx) * scale) ** 2) + ((np.abs(fy) * scale) ** 2)
    vg = vol[m]
    g = elem_group[m]
    if g == 10:
        Srotor += bsq * vg
    elif g == 1:
        Sstator += bsq * vg
    elif g == 2:
        Sprox += bsq * vg

# Magnets.  The vector potential's harmonics give the induced electric field,
# and the net current in each magnet must be zero at every harmonic -- so the
# volume-weighted average over each magnet is subtracted before the loss is
# accumulated.  Done magnet by magnet, since each is independently isolated.
nmag = len(mag_idx)
Jre = np.zeros(nmag * ns)
Jim = np.zeros(nmag * ns)
for i in range(nmag):
    lo = i * ns
    f = np.fft.fft(a_ts[lo:lo + ns])
    Jre[lo:lo + ns] = f.real * scale
    Jim[lo:lo + ns] = f.imag * scale

Smag = np.zeros(ns)
for kmag in range(1, RotorMagnets + 1):
    members = [i for i in range(nmag) if elem_group[mag_idx[i]] == 10 + kmag]
    if not members:
        continue
    vmag = 0.0
    Jo_re = np.zeros(ns)
    Jo_im = np.zeros(ns)
    for i in members:
        vg = vol[mag_idx[i]]
        vmag += vg
        lo = i * ns
        Jo_re += Jre[lo:lo + ns] * vg
        Jo_im += Jim[lo:lo + ns] * vg
    Jo_re = Jo_re / vmag
    Jo_im = Jo_im / vmag
    for i in members:
        vg = vol[mag_idx[i]]
        lo = i * ns
        dre = Jre[lo:lo + ns] - Jo_re
        dim = Jim[lo:lo + ns] - Jo_im
        Smag += (dre * dre + dim * dim) * vg

Iphase = math.sqrt(MyIdCurrent ** 2 + MyIqCurrent ** 2) / math.sqrt(2)
PhaseOhmic = 3 * (PhaseResistance * (1 + TemperatureRise * 0.004)) * Iphase ** 2

# Only the lower half of the spectrum is counted: the upper half mirrors it,
# and counting both would double every harmonic.
half = np.zeros(ns)
for j in range(ns // 2):
    half[j] = j

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

# print("")
# print("  Speed     Rotor    Stator    Magnet     Ohmic      Prox     Total")
# print("    RPM      Watts     Watts     Watts     Watts     Watts     Watts")
# for i in range(len(speeds)):
    # print("%7d %9.2f %9.2f %9.2f %9.2f %9.2f %9.2f"
          # % (speeds[i], rotor_loss[i], stator_loss[i], magnet_loss[i],
             # ohmic_loss[i], prox_loss[i], total_loss[i]))

# The loss plot, drawn with real matplotlib.  femm-qt ships none of it: the
# import finds a Python 3 on the machine and proxies the calls to it, so this
# is the same matplotlib a MATLAB user would reach for and takes the same
# arguments as the .m's plot/xlabel/legend calls.
#
# Guarded, because the results above are the point and a machine with no
# suitable Python should not lose a minute of solving to a failed import.
# femm-qt's own plot() is the fallback -- no CPython needed, same six curves.
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
