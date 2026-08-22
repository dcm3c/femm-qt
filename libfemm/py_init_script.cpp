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
    from ulab import numpy as _unp, scipy as _uscipy

    # Complex linear algebra. ulab's numpy.linalg and numpy.dot reject complex
    # dtype (and ulab's eig is real-symmetric only); the _clinalg_* C kernels
    # (fullmatrix.cpp, registered as globals before this script runs) provide
    # general complex eig/qr/svd/solve/inv/det/dot/pinv/cond/rank/norm. Since
    # ulab's numpy is a read-only module that can't be patched in place, we
    # publish a thin proxy as `numpy`: it forwards everything to ulab but
    # overrides linalg.* and dot with the complex-capable kernels.
    #
    # _asc coerces to a dense complex ndarray: an existing ndarray is multiplied
    # by (1+0j) (real->complex, complex stays complex, fresh dense array); a
    # list/tuple is built first. We avoid np.array() on an already-complex
    # ndarray, which ulab rejects ("cannot convert complex type").
    def _asc(a):
        m = a if hasattr(a, 'dtype') else _unp.array(a)
        return m * (1 + 0j)
    def _eig(a):      return _clinalg_eig(_asc(a))
    def _qr(a):       return _clinalg_qr(_asc(a))
    def _svd(a):      return _clinalg_svd(_asc(a))
    def _solve(a, b): return _clinalg_solve(_asc(a), _asc(b))
    def _inv(a):      return _clinalg_inv(_asc(a))
    def _det(a):      return _clinalg_det(_asc(a))
    def _pinv(a):     return _clinalg_pinv(_asc(a))
    def _cond(a):     return _clinalg_cond(_asc(a))
    def _rank(a):     return _clinalg_rank(_asc(a))
    def _norm(a):     return _clinalg_norm(_asc(a))
    def _dot(a, b):   return _clinalg_dot(_asc(a), _asc(b))

    # np.array on a complex Python list ("[[1+2j, 3], ...]") fails in ulab
    # ("can't convert complex to float"). Patch np.array to fall back to building
    # from real+imag parts, which ulab does accept. Re()/Im() (femm globals)
    # split a scalar whether it's real or complex; _split mirrors list nesting.
    def _split(x):
        if isinstance(x, (list, tuple)):
            r = []; i = []
            for e in x:
                er, ei = _split(e); r.append(er); i.append(ei)
            return r, i
        return Re(x), Im(x)
    def _array(a, *args, **kw):
        try:
            return _unp.array(a, *args, **kw)        # real lists/ndarrays, dtype= cases
        except (TypeError, ValueError):
            if hasattr(a, 'dtype'):                  # a complex ndarray -> dense copy
                return a * (1 + 0j)
            r, i = _split(a)                         # complex nested list/tuple
            return _unp.array(r) * (1 + 0j) + _unp.array(i) * 1j

    # Linear reductions (sum/mean/trace) reject complex in ulab, but they're
    # separable: f(A) = f(A.real) + f(A.imag)*1j. Try the real op first (fast
    # path for real arrays); fall back to the split for complex.
    def _reduce(fn):
        def w(a, *args, **kw):
            try:
                return fn(a, *args, **kw)
            except (TypeError, ValueError, NotImplementedError):
                return fn(a.real, *args, **kw) + fn(a.imag, *args, **kw) * 1j
        return w
    _sum   = _reduce(_unp.sum)
    _mean  = _reduce(_unp.mean)
    _trace = _reduce(_unp.trace)

    # Vectorized transcendentals: ulab's np.sqrt/sin/... reject complex arrays.
    # We have complex-aware SCALAR versions (the _c* C helpers), so fall back to
    # applying one element-wise (a Python loop — slow but correct, fine for the
    # modest arrays FEMM scripts produce). Real arrays keep ulab's fast path.
    def _elw(z, fn):
        n = 1
        for d in z.shape: n *= d
        return _array([fn(x) for x in z.reshape((n,))]).reshape(z.shape)
    def _vec(real_fn, scalar_fn):
        def w(a, *args, **kw):
            try:
                return real_fn(a, *args, **kw)
            except (TypeError, ValueError, NotImplementedError):
                return _elw(a, scalar_fn)
        return w
    # Functions ulab lacks entirely (arcsin/arccos/arctan, angle): no real
    # fast-path exists, so always apply the scalar helper element-wise.
    def _velw(scalar_fn):
        def w(a, *args, **kw):
            if hasattr(a, 'shape'):
                return _elw(a, scalar_fn)
            if isinstance(a, (list, tuple)):
                return _elw(_array(a), scalar_fn)
            return scalar_fn(a)
        return w

    # real/imag: ndarrays expose .real/.imag (work for complex AND real); scalars
    # use the Re()/Im() globals; lists are coerced to complex first.
    def _real(a):
        if hasattr(a, 'shape'):
            try: return a.real
            except AttributeError: return a            # real ndarray: real part is itself
        if isinstance(a, (list, tuple)): return (_array(a) * (1 + 0j)).real
        return Re(a)
    def _imag(a):
        if hasattr(a, 'shape'):
            try: return a.imag
            except AttributeError: return a * 0.0       # real ndarray: imag is zeros
        if isinstance(a, (list, tuple)): return (_array(a) * (1 + 0j)).imag
        return Im(a)
    # power: ulab's elementwise ** rejects complex; fall back to Python's complex
    # scalar ** (real arrays/scalars take ulab's fast path).
    def _power(a, b):
        try:
            return a ** b
        except (TypeError, ValueError, NotImplementedError):
            if hasattr(b, 'shape'):                     # array ** array
                n = 1
                for d in a.shape: n *= d
                af = a.reshape((n,)); bf = b.reshape((n,))
                return _array([af[i] ** bf[i] for i in range(n)]).reshape(a.shape)
            return _elw(a, lambda x: x ** b)            # array ** scalar

    # Arbitrary-length FFT, via Bluestein's algorithm.
    #
    # ulab's FFT takes power-of-two lengths only, which is an implementation
    # limit and not a mathematical one -- MATLAB and CPython numpy transform
    # any length, and a script translated from either should not have to
    # resample its data to suit us. (The case that prompted this: a core-loss
    # script steps a rotor 180 times, and 180 is not a power of two.)
    #
    # Bluestein turns a length-N DFT into a CONVOLUTION, which a power-of-two
    # FFT can do. With w[n] = exp(-i*pi*n^2/N), the identity 2nk = n^2 + k^2 -
    # (k-n)^2 gives
    #
    #     X[k] = w[k] * sum_n (x[n]*w[n]) * conj(w[k-n])
    #
    # i.e. w[k] times the linear convolution of u = x*w with v = conj(w),
    # evaluated cyclically at length M = next power of two >= 2N-1.
    #
    # Powers of two keep ulab's own fast path untouched. Everything else costs
    # one forward and one inverse transform of length M -- about 3x a same-size
    # transform, since M overshoots N. v and its transform depend only on N, so
    # they are cached: a script doing thousands of equal-length transforms
    # (which is the usual shape) builds them once.
    _bluestein_cache = {}

    def _bluestein_tables(n):
        e = _bluestein_cache.get(n)
        if e is not None:
            return e
        m = 1
        while m < 2 * n - 1:
            m *= 2
        # w[j] = exp(-i*pi*j^2/n). j^2 % (2n) keeps the angle small enough that
        # cos/sin stay accurate for large n -- squaring j directly loses bits.
        wr = [0.0] * n
        wi = [0.0] * n
        for j in range(n):
            ang = -_math.pi * ((j * j) % (2 * n)) / n
            wr[j] = _math.cos(ang)
            wi[j] = _math.sin(ang)
        w = _unp.array(wr) * (1 + 0j) + _unp.array(wi) * 1j
        # v is conj(w) laid out for cyclic convolution: v[j] and v[m-j] both
        # hold conj(w[j]), so the wrap-around picks up the negative lags.
        vr = [0.0] * m
        vi = [0.0] * m
        for j in range(n):
            vr[j] = wr[j]
            vi[j] = -wi[j]
            if j:
                vr[m - j] = wr[j]
                vi[m - j] = -wi[j]
        v = _unp.array(vr) * (1 + 0j) + _unp.array(vi) * 1j
        e = (m, w, _unp.fft.fft(v))
        _bluestein_cache[n] = e
        return e

    def _fft_bluestein(x, n, m, w, vf):
        pad = _unp.zeros(m) * (1 + 0j)
        pad[0:n] = x * w
        c = _unp.fft.ifft(_unp.fft.fft(pad) * vf)
        return w * c[0:n]

    def _as_complex_1d(a):
        z = a if hasattr(a, 'dtype') else _array(a)
        z = z * (1 + 0j)
        n = 1
        for d in z.shape:
            n *= d
        return z.reshape((n,)), n

    def _fft(a, *args, **kw):
        z, n = _as_complex_1d(a)
        if n and (n & (n - 1)) == 0:
            return _unp.fft.fft(z, *args, **kw)     # ulab's own fast path
        m, w, vf = _bluestein_tables(n)
        return _fft_bluestein(z, n, m, w, vf)

    def _ifft(a, *args, **kw):
        z, n = _as_complex_1d(a)
        if n and (n & (n - 1)) == 0:
            return _unp.fft.ifft(z, *args, **kw)
        # ifft(x) = conj(fft(conj(x)))/n -- one code path, no second table set.
        m, w, vf = _bluestein_tables(n)
        r = _fft_bluestein(z.real - z.imag * 1j, n, m, w, vf)
        return (r.real - r.imag * 1j) / n

    class _FftProxy:
        fft  = staticmethod(_fft)
        ifft = staticmethod(_ifft)
        def __getattr__(self, k): return getattr(_unp.fft, k)

    class _LinalgProxy:
        eig   = staticmethod(_eig);   qr   = staticmethod(_qr)
        svd   = staticmethod(_svd);   solve = staticmethod(_solve)
        inv   = staticmethod(_inv);   det  = staticmethod(_det)
        pinv  = staticmethod(_pinv);  cond = staticmethod(_cond)
        rank  = staticmethod(_rank);  norm = staticmethod(_norm)
        def __getattr__(self, k): return getattr(_unp.linalg, k)
    class _NumpyProxy:
        linalg = _LinalgProxy()
        fft    = _FftProxy()
        dot    = staticmethod(_dot)
        array  = staticmethod(_array)
        sum    = staticmethod(_sum)
        mean   = staticmethod(_mean)
        trace  = staticmethod(_trace)
        abs    = staticmethod(abs)            # ulab has no np.abs; builtin abs works
        # complex-array transcendentals (real arrays fall through to ulab)
        sqrt = staticmethod(_vec(_unp.sqrt, _csqrt))
        log  = staticmethod(_vec(_unp.log,  _clog))
        sin  = staticmethod(_vec(_unp.sin,  _csin))
        cos  = staticmethod(_vec(_unp.cos,  _ccos))
        tan  = staticmethod(_vec(_unp.tan,  _ctan))
        sinh = staticmethod(_vec(_unp.sinh, _csinh))
        cosh = staticmethod(_vec(_unp.cosh, _ccosh))
        tanh = staticmethod(_vec(_unp.tanh, _ctanh))
        # ulab has none of these; serve them from the scalar complex helpers
        arcsin = staticmethod(_velw(_casin))
        arccos = staticmethod(_velw(_cacos))
        arctan = staticmethod(_velw(_catan))
        angle  = staticmethod(_velw(arg))     # phase; arg() returns a real
        real   = staticmethod(_real)
        imag   = staticmethod(_imag)
        power  = staticmethod(_power)
        def __getattr__(self, k): return getattr(_unp, k)

    _sys.modules['numpy'] = _NumpyProxy()
    _sys.modules['scipy'] = _uscipy
    clinalg = _sys.modules['numpy'].linalg   # back-compat: clinalg.eig(), etc.
except ImportError:
    pass

# --- pyfemm drop-in compatibility ------------------------------------------
# Let "import femm; femm.ei_..." scripts (and the pyfemm examples) run unchanged
# in-process. openfemm/closefemm are connection-lifecycle no-ops here -- we ARE
# femm, and closefemm must NOT exit the GUI. The `femm` module is a proxy whose
# attribute lookups resolve to the global FEMM commands (which live in this one
# shared namespace), so femm.ei_drawrectangle / from femm import X / bare
# ei_drawrectangle all work.
def openfemm(*a, **k):  pass
def closefemm(*a, **k): pass
import sys as _fsys
class _FemmProxy:
    def __getattr__(self, k):
        try:
            return globals()[k]
        except KeyError:
            raise AttributeError(k)
_fsys.modules['femm'] = _FemmProxy()
del _fsys

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
        # A complex value is shown as complex, even when its imaginary part is
        # zero. It used to collapse to the bare real, because the DC getters
        # handed back complex-typed reals and "1234.5+0j" for a force was
        # noise. Those return real numbers now, so a complex here means a
        # phasor -- and "0" imaginary is then worth seeing, not worth hiding.
        if re == 0:                       # pure imaginary: "0.5j", "-3j", "1j"
            return "%.14gj" % im
        if im < 0:
            return "%.14g-%.14gj" % (re, -im)
        return "%.14g+%.14gj" % (re, im)
    if isinstance(v, float):
        return "%.14g" % v
    return str(v)
def print(*args, **kw):
    # sep/end/file are forwarded rather than swallowed. The DEFAULT separator
    # stays a tab, which is FEMM's own convention and what every ported script
    # expects -- but a script that asks for CPython's spelling should get it,
    # and `print(msg, file=sys.stderr)` is ordinary Python that used to fail
    # with "unexpected keyword argument".
    sep = kw.pop('sep', '\t')
    text = sep.join(_femm_fmt(a) for a in args)
    end = kw.pop('end', None)
    f = kw.pop('file', None)
    if kw:
        raise TypeError("print() got an unexpected keyword argument '%s'"
                        % list(kw.keys())[0])
    if f is not None:
        f.write(text if end is None else text + end)
        if end is None:
            f.write('\n')
    elif end is None:
        _builtin_print(text)
    else:
        _builtin_print(text, end=end)

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
