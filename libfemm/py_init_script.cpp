// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_init_script.cpp - embedded Python init script.
// Run once at the end of init_py().
//
// Notes on the port:
//  - Complex-aware math: top-level sqrt/sin/.../exp dispatch to cmath for a
//    complex argument, else math (matching the old C overloads which used the
//    CComplex transcendentals for complex args and the C library for reals).
//    abs/max/min use Python builtins (abs already returns complex magnitude).
//    atan2 uses the real parts only, matching CComplex::atan2.
//  - I, pi, PI, Complex, Re/Im/re/im, conj, arg are bound at the C level in
//    init_py() (py_state.cpp), not here.
//  - per-physics drawarc/drawline/drawrectangle helpers wrap the bound
//    addnode/addsegment/addarc editor commands.
//  - makeABC (improvised asymptotic boundary conditions) is NOT here: it is
//    native C++ in femm_makeabc.cpp, bound as mi_/ei_/ci_/hi_makeABC in the
//    per-physics editor files and shared with the Lua tree.
//
// Why this is exec'd as a C-string and deliberately NOT frozen as a .mpy:
// The script is run with exec_python() into the SHARED global namespace, so its
// functions and module-scope code see the names bound at the C level in
// init_py() -- e.g. PI at module scope (Pi/pi/uo/Oersted), exp() used by AWG,
// and the draw helpers calling the bound mi_addnode/etc. A frozen module imports
// into its OWN namespace, and the functions it defines capture THAT namespace as
// __globals__, so they would no longer see PI/exp/mi_addnode/... --
// `from _femm_init import *` would fail at import (module-scope PI) and the draw
// helpers would NameError at call time. Freezing would buy a few ms of one-time
// compile and a few KB of GC heap (negligible on desktop; it only matters for
// MCU flash/RAM), in exchange for re-architecting how the C-bound names share a
// namespace with this script, building mpy-cross, and wiring frozen_content.c
// through the embed regen. Not worth it -- exec-into-globals is simple/correct.
#include "py_init_script.h"

const char *PY_INIT_SCRIPT = R"PYSRC(
# math is bound twice on purpose (import is cached -> same module object, no
# double load): `_math` is the private alias the wrappers below use for the real
# fast-path -- underscored so a user rebinding `math` in the shared global
# namespace can't break sqrt/sin/... -- while public `math`/`cmath` let users
# reach for `math.sqrt(2)` / `cmath.sqrt(-1)` out of Python habit.
import math as _math
import math, cmath

# Make the familiar CPython incantations `import numpy as np` / `import scipy`
# resolve to ulab's numpy/scipy. MicroPython can't do dotted `import ulab.numpy`,
# so we alias the modules into sys.modules; a later `import numpy` then finds
# them already "loaded". Nothing is pre-bound -- you still type the import, it
# just does the right thing. Guarded so a build without ulab still inits.
try:
    import sys as _sys
    from ulab import numpy as _numpy, scipy as _scipy
    _sys.modules['numpy'] = _numpy
    _sys.modules['scipy'] = _scipy
    del _sys, _numpy, _scipy
except ImportError:
    pass

# --- complex-aware elementary functions (override builtins/promoted names) ---
# Each works for real AND complex arguments, matching original FEMM (where every
# number was complex). A real, in-domain argument takes the real fast-path and
# returns a real (so trig coordinates etc. stay float and flow into the bound
# double-typed editor calls). Complex arguments -- and out-of-domain reals like
# sqrt(-1), log(-1), asin(2) -- promote to complex via the CComplex
# transcendentals bound in init_py (_csqrt/_csin/...), the same math OF used.
# The strict, real-only forms remain available as math.* (e.g. math.sqrt raises).
def _cx(realfn, cplxfn, x):
    if isinstance(x, complex):
        return cplxfn(x)
    if realfn is not None:
        try: return realfn(x)
        except ValueError: pass        # out of the real domain -> promote
    r = cplxfn(x)                       # CComplex result for a real argument
    return r.real if r.imag == 0 else r

def sqrt(x): return _cx(_math.sqrt, _csqrt, x)
def log(x):  return _cx(_math.log,  _clog,  x)
def exp(x):  return _cx(_math.exp,  _cexp,  x)
def sin(x):  return _cx(_math.sin,  _csin,  x)
def cos(x):  return _cx(_math.cos,  _ccos,  x)
def tan(x):  return _cx(_math.tan,  _ctan,  x)
def asin(x): return _cx(_math.asin, _casin, x)
def acos(x): return _cx(_math.acos, _cacos, x)
def sinh(x): return _cx(None, _csinh, x)   # math has no real sinh/cosh/tanh,
def cosh(x): return _cx(None, _ccosh, x)   # so these go through CComplex and
def tanh(x): return _cx(None, _ctanh, x)   # collapse to real when im == 0.
def log10(x): return log(x) / _math.log(10)

def atan2(y, x):
    yr = y.real if isinstance(y, complex) else y
    xr = x.real if isinstance(x, complex) else x
    return _math.atan2(yr, xr)

def atan(x, y=None):
    if y is not None:
        return atan2(x, y)
    return _cx(_math.atan, _catan, x)

def pow(a, b):
    if isinstance(a, complex) or isinstance(b, complex):
        return a ** b
    try:               return _math.pow(a, b)
    except ValueError: return complex(a) ** b    # e.g. pow(-8, 1/3) -> complex

# --- print override (tab-separated, native-Python "j" complex formatting) ---
# Display uses parenthesized j notation, %.14g for readability, with a
# real-valued complex (im==0) collapsed to a bare real. The TCP wire keeps I*
# notation (format_complex in py_state.cpp) — that is the IPC contract.
_builtin_print = print
def _femm_fmt(v):
    if isinstance(v, complex):
        re, im = v.real, v.imag
        if im == 0:                       # collapse: real-valued reads as real
            return "%.14g" % re
        if re == 0:                       # pure imaginary: "0.5j", "-3j", "1j"
            return "%.14gj" % im
        if im < 0:
            return "%.14g-%.14gj" % (re, -im)
        return "%.14g+%.14gj" % (re, im)
    if isinstance(v, float):
        return "%.14g" % v
    return str(v)
def print(*args):
    _builtin_print('\t'.join(_femm_fmt(a) for a in args))

# --- open() smart wrapper: FEMM document open vs Python file open ---
_builtin_open = open
try:
    _femm_open
    _femm_exts = ('.fem', '.fee', '.feh', '.fec')
    def open(*args, **kwargs):
        if len(args) == 1 and not kwargs and isinstance(args[0], str):
            s = args[0].lower()
            for ext in _femm_exts:
                if s.endswith(ext):
                    return _femm_open(args[0])
        return _builtin_open(*args, **kwargs)
except NameError:
    pass

# --- physical constants ---
Pi  = PI
pi  = PI
uo  = PI * 4.0e-7          # magnetic permeability of free space
muo = uo
eo  = 8.8541878188e-12     # electrical permittivity of free space

# --- unit definitions ---
meter = 1
meters = Meter = Meters = meter

inch = 0.0254
inches = Inch = Inches = inch

cm = 0.01
centimeter = centimeters = Centimeters = Centimeter = cm

mm = 0.001
millimeters = millimeter = Millimeter = Millimeters = milimeter = milimeters = mm

um = 0.000001
micrometer = Micrometer = micrometers = Micrometers = um
micron = Micron = microns = Microns = um

mil = 0.001 * inch
mils = Mils = Mil = mil

Tesla     = 1
mTesla    = 0.001
Gauss     = 0.0001
kGauss    = 0.1
AmpMeter  = 1
kAmpMeter = 1000
Oersted   = 250.0 / PI
kOersted  = 1000.0 * Oersted

# --- AWG wire diameter (mm) ---
def AWG(awg):
    return 8.2514694 * exp(-0.115943 * awg)

# --- per-physics draw helpers ---
def mi_drawarc(x1, y1, x2, y2, tta, dtta):
    mi_addnode(x1, y1); mi_addnode(x2, y2); mi_addarc(x1, y1, x2, y2, tta, dtta)
def mi_drawline(x1, y1, x2, y2):
    mi_addnode(x1, y1); mi_addnode(x2, y2); mi_addsegment(x1, y1, x2, y2)
def mi_drawrectangle(x1, y1, x2, y2):
    mi_drawline(x1, y1, x2, y1); mi_drawline(x2, y1, x2, y2)
    mi_drawline(x2, y2, x1, y2); mi_drawline(x1, y2, x1, y1)

def ei_drawarc(x1, y1, x2, y2, tta, dtta):
    ei_addnode(x1, y1); ei_addnode(x2, y2); ei_addarc(x1, y1, x2, y2, tta, dtta)
def ei_drawline(x1, y1, x2, y2):
    ei_addnode(x1, y1); ei_addnode(x2, y2); ei_addsegment(x1, y1, x2, y2)
def ei_drawrectangle(x1, y1, x2, y2):
    ei_drawline(x1, y1, x2, y1); ei_drawline(x2, y1, x2, y2)
    ei_drawline(x2, y2, x1, y2); ei_drawline(x1, y2, x1, y1)

def ci_drawarc(x1, y1, x2, y2, tta, dtta):
    ci_addnode(x1, y1); ci_addnode(x2, y2); ci_addarc(x1, y1, x2, y2, tta, dtta)
def ci_drawline(x1, y1, x2, y2):
    ci_addnode(x1, y1); ci_addnode(x2, y2); ci_addsegment(x1, y1, x2, y2)
def ci_drawrectangle(x1, y1, x2, y2):
    ci_drawline(x1, y1, x2, y1); ci_drawline(x2, y1, x2, y2)
    ci_drawline(x2, y2, x1, y2); ci_drawline(x1, y2, x1, y1)

def hi_drawarc(x1, y1, x2, y2, tta, dtta):
    hi_addnode(x1, y1); hi_addnode(x2, y2); hi_addarc(x1, y1, x2, y2, tta, dtta)
def hi_drawline(x1, y1, x2, y2):
    hi_addnode(x1, y1); hi_addnode(x2, y2); hi_addsegment(x1, y1, x2, y2)
def hi_drawrectangle(x1, y1, x2, y2):
    hi_drawline(x1, y1, x2, y1); hi_drawline(x2, y1, x2, y2)
    hi_drawline(x2, y2, x1, y2); hi_drawline(x1, y2, x1, y1)
)PYSRC";
