# Plotting a real FEMM solution with matplotlib, over the PyBridge.
#
#     femm_headless -run examples/plot-mesh-matplotlib.py
#
# or paste it into the GUI's Python console.  It solves coil.fem, reads the
# finite-element mesh back, and draws the vector potential as a filled contour
# on the actual triangulation -- not on a resampled grid.
#
# femm-qt does not ship matplotlib; the bridge drives whichever CPython you
# already have.  If none has matplotlib, this prints why and exits.
#
# Three things here are worth copying into your own scripts:
#
#   1. Bulk data goes into a ulab array (`np.zeros(...)` then assign), never a
#      Python list.  A list of 73k floats is 73k separate heap objects, and
#      MicroPython's collector walks all of them on every later allocation: the
#      element loop below runs in 2 ms with the data in an array and 70 ms with
#      the same data in a list.  It gets worse as the mesh grows.
#   2. The triangle indices are `np.int32`.  A mesh passes 32767 nodes almost
#      immediately, and ulab's next-widest integer is int16, which would wrap
#      silently.
#   3. Arrays cross to matplotlib as raw bytes, so the size of the mesh is not
#      the thing that costs -- 8 MB moves in about 60 ms.
import time

import numpy as np
import pybridge

MODEL = "coil.fem"


def main():
    try:
        pybridge.start()
    except pybridge.BridgeError as e:
        print(e)
        return

    print("solving %s ..." % MODEL)
    open(MODEL)
    mi_analyze(1)
    mi_loadsolution()

    # mo_numnodes() and friends return floats, as all FEMM numeric getters do.
    nn = int(mo_numnodes())
    ne = int(mo_numelements())
    print("mesh: %d nodes, %d elements" % (nn, ne))

    t0 = time.ticks_ms()
    x = np.zeros(nn)
    y = np.zeros(nn)
    a = np.zeros(nn)
    for k in range(1, nn + 1):
        nx, ny = mo_getnode(k)
        x[k - 1] = nx
        y[k - 1] = ny
        # [0] is the vector potential A at that point.
        a[k - 1] = mo_getpointvalues(nx, ny)[0]

    tri = np.zeros((ne, 3), dtype=np.int32)
    for k in range(1, ne + 1):
        e = mo_getelement(k)
        tri[k - 1][0] = int(e[0]) - 1     # FEMM numbers nodes from 1
        tri[k - 1][1] = int(e[1]) - 1
        tri[k - 1][2] = int(e[2]) - 1
    print("read back in %d ms" % time.ticks_diff(time.ticks_ms(), t0))

    t0 = time.ticks_ms()
    plt = pybridge.import_module("matplotlib.pyplot")
    fig, ax = plt.subplots(figsize=[7.0, 6.0])
    ax.tricontourf(x, y, tri, a, levels=40)
    ax.set_aspect("equal")
    # mo_getprobleminfo() is (ProblemType, Frequency, Depth, LengthConv);
    # ProblemType 1 is axisymmetric.
    axisym = int(mo_getprobleminfo()[0]) == 1
    ax.set_xlabel("r (cm)" if axisym else "x (cm)")
    ax.set_ylabel("z (cm)" if axisym else "y (cm)")
    ax.set_title("Vector potential, %s (%d elements)" % (MODEL, ne))
    fig.savefig("coil-A.png", dpi=140)
    print("wrote coil-A.png")
    # The mesh itself, over a zoomed-in corner -- drawing 145k triangle edges
    # across the whole figure buries the contours in ink.
    ax.triplot(x, y, tri, color="k", linewidth=0.2, alpha=0.4)
    ax.set_xlim(0.0, 40.0)
    ax.set_ylim(-30.0, 30.0)
    ax.set_title("Vector potential and mesh, near the coil")
    print("plotted in %d ms" % time.ticks_diff(time.ticks_ms(), t0))

    fig.savefig("coil-A-mesh.png", dpi=140)
    print("wrote coil-A-mesh.png")
    plt.show()      # a real, interactive window: zoom, pan, save


main()
