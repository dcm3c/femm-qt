"""
of_ac_curve.py -- which Original FEMM golden values can no longer vouch for
femm-qt: harmonic (AC) magnetics in nonlinear iron.

For a harmonic problem a nonlinear material's B-H curve is replaced by an
effective curve whose B at amplitude Hi is the fundamental of B(Hi sin t).
GetSlopes evaluates that integral in closed form, and the closed form Original
FEMM 4.2 ships is wrong: up to ~2-3% high in effective B below and around the
knee (2.3% on M-19 at 1 T). femm-qt corrected it in 175dcfe, so on every AC
fixture with a B-H curve the two programs now solve different problems, and
any agreement within the OF tolerances would be coincidence.

Same rule as MOBlockIntegralCompare.OF_DEFECT: not a tolerance escape hatch.
The cause is demonstrated, and ACBHCurveTest.py is the independent coverage --
it checks both copies of the curve (solver and postprocessor) against a
quadrature of the integral, and fails on the old formula in either copy.

What stays compared against OF:
  * every DC section, and f=0 rows of the B-H sweep (the effective curve is
    not built at f=0);
  * AC fixtures with only linear materials (AxiLorentz, PlanarLorentz), which
    the fix does not touch -- FQReference confirmed them bit-identical;
  * incremental AC sections. Those linearize about a DC bias and read the AC
    curve only for mu_max, which scales the hysteresis lag; they moved ~1e-4,
    two orders inside the OF tolerances, so OF still checks them.
"""
import re

# Golden fixtures that carry a B-H curve and are also solved harmonically.
NONLINEAR_AC_FIXTURES = {"RMB", "RMB-AGE", "torque", "roters1b", "rolled"}

_HARMONIC = re.compile(r"\bAC \d+Hz")      # "Incremental AC" carries no "Hz"
_SWEEP_F = re.compile(r"^f=(\d+)\b")       # B-H sweep row label: "f=60 Bdes=..."

REASON = "AC nonlinear: OF's effective B-H curve is wrong, see ACBHCurveTest"


def excluded(section, row=""):
    """True if this OF golden value is computed on the wrong AC curve."""
    m = _SWEEP_F.match(row.strip())
    if m:
        return int(m.group(1)) != 0
    fixture = re.match(r"[\w-]+", section.strip())
    return (fixture is not None and fixture.group(0) in NONLINEAR_AC_FIXTURES
            and _HARMONIC.search(section) is not None)


def note(n):
    """Suffix for a comparer's summary line; says nothing when nothing was cut."""
    return "" if not n else "; %d excluded (%s)" % (n, REASON)
