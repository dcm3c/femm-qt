"""Layer 2 'phasecurrent': the one scenario that sends COMPLEX ARGUMENTS.

    femm_headless -run l2_phase_reference.py

No accuracy suite passes a complex argument anywhere, which is exactly the hole
octavefemm's num.m lived in for five months: it sent MATLAB's `1+2i`, which the
interpreter rejects outright, and nothing noticed. This closes it.

examples/three-phase-line.FEM carries three solid conductors in circuits A, B
and C at 60 Hz. A balanced set is assigned:

    A = 1000                     0 degrees
    B = -500 - 866.0254038j   -120 degrees
    C = -500 + 866.0254038j   +120 degrees

which sums to exactly zero -- the physical constraint for a three-wire line with
no neutral, and a value chosen so the check is exact rather than approximate.

Two invariants make this partly self-checking, needing no oracle:

  * mo_getcircuitproperties echoes the PRESCRIBED current for a current-driven
    circuit, so the current read back must equal the complex number sent. A
    binding that drops or mangles an imaginary part fails here directly.
  * the three currents must sum to zero.

Voltage and flux linkage have no Original FEMM golden -- this scenario does not
exist upstream -- so those are compared against this in-process run, which uses
the same engine the bindings drive.
"""

EX = "C:/Claude/uPython/femm-qt/examples/"
TMP = "C:/temp/"

PHASES = (("A", complex(1000, 0)),
          ("B", complex(-500, -866.0254038)),
          ("C", complex(-500, 866.0254038)))


def fmt(z):
    r = z.real if isinstance(z, complex) else float(z)
    i = z.imag if isinstance(z, complex) else 0.0
    return "%.10g %.10g" % (r, i)


open(EX + "three-phase-line.FEM")
mi_saveas(TMP + "l2_phase_ref.fem")
mi_probdef(60, "inches", "planar")
for name, cur in PHASES:
    mi_modifycircprop(name, 1, cur)
mi_analyze()
mi_loadsolution()

print("===  three-phase-line balanced 1000A 60Hz  ===")
for name, cur in PHASES:
    I, V, F = mo_getcircuitproperties(name)
    print("  %-14s = %s | %s | %s" % (name, fmt(I), fmt(V), fmt(F)))
mo_close()
print("")
print("Done.")
