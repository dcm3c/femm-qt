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

    # REAL INPUT MUST GIVE A REAL RESULT. Every wrapper below used to call
    # _asc() unconditionally, so np.dot on two float arrays came back complex
    # where numpy -- and ulab's own dot, which this proxy shadows -- returns a
    # float. That is a dtype divergence with teeth: the result prints as
    # "(14+0j)", compares differently, and is refused by anything wanting a
    # real array. It was also less accurate, because routing real work through
    # the complex kernel loses the exactness of the real one: inv([[1,2],[3,4]])
    # came back -1.9999999999999996 where ulab's real path gives exactly -2.
    #
    # Two tiers, in this order:
    #   real input, and ulab has a real implementation  ->  delegate to ulab
    #   real input, and it does not (solve, svd, pinv)  ->  complex kernel,
    #                                                       then drop the
    #                                                       zero imaginary part
    # The second is not "collapse if the imaginary part looks small". It is
    # decided from the INPUT dtype, where the answer is known a priori: the
    # inverse, solution, pseudo-inverse, QR and SVD of a real matrix are real
    # as a matter of algebra, not of rounding.
    #
    # eig/eigvals/roots are deliberately NOT in that set. A real matrix can
    # have genuinely complex eigenvalues, so their results stay complex --
    # which is also why they cannot delegate to ulab, whose eig refuses a
    # non-symmetric matrix outright.
    def _isreal(a):
        if hasattr(a, 'dtype'):
            return int(a.dtype) != int(_unp.complex)
        if isinstance(a, (list, tuple)):
            for e in a:
                if not _isreal(e):
                    return False
            return True
        return not isinstance(a, complex)
    def _re(x):
        if isinstance(x, tuple):
            return tuple(_re(e) for e in x)
        return x.real if hasattr(x, 'real') else x

    def _eig(a):      return _clinalg_eig(_asc(a))
    def _qr(a):
        if _isreal(a):  return _unp.linalg.qr(a if hasattr(a, 'dtype') else _unp.array(a))
        return _clinalg_qr(_asc(a))
    def _svd(a):
        if _isreal(a):  return _re(_clinalg_svd(_asc(a)))
        return _clinalg_svd(_asc(a))
    def _solve(a, b):
        if _isreal(a) and _isreal(b):  return _re(_clinalg_solve(_asc(a), _asc(b)))
        return _clinalg_solve(_asc(a), _asc(b))
    def _inv(a):
        if _isreal(a):  return _unp.linalg.inv(a if hasattr(a, 'dtype') else _unp.array(a))
        return _clinalg_inv(_asc(a))
    def _det(a):
        if _isreal(a):  return _unp.linalg.det(a if hasattr(a, 'dtype') else _unp.array(a))
        return _clinalg_det(_asc(a))
    def _pinv(a):
        if _isreal(a):  return _re(_clinalg_pinv(_asc(a)))
        return _clinalg_pinv(_asc(a))
    def _cond(a):     return _clinalg_cond(_asc(a))
    def _rank(a):     return _clinalg_rank(_asc(a))
    def _norm(a):     return _clinalg_norm(_asc(a))
    def _dot(a, b):
        if _isreal(a) and _isreal(b):
            return _unp.dot(a if hasattr(a, 'dtype') else _unp.array(a),
                            b if hasattr(b, 'dtype') else _unp.array(b))
        return _clinalg_dot(_asc(a), _asc(b))

    # Built on the kernels above rather than on anything new. numpy spells the
    # rank of a matrix `matrix_rank`; this proxy shipped it as `rank`, which is
    # not a numpy name at all, so pasted code could not find it. `rank` stays as
    # an alias -- scripts here may already use it.
    def _eigvals(a):  return _eig(a)[0]

    def _lstsq(a, b, rcond=None):
        """numpy returns (solution, residuals, rank, singular values). The
        residuals are the sum of squares, and numpy gives an EMPTY array rather
        than a number when the system is underdetermined or rank-deficient,
        because there is no unique residual to report."""
        if rcond is not None:
            # rcond cuts small singular values, which changes the answer. It is
            # not implemented, so it must refuse rather than be dropped.
            raise NotImplementedError("lstsq(): rcond= is not supported")
        A = _asc(a)
        B = _asc(b)
        x = _clinalg_dot(_clinalg_pinv(A), B)
        rk = _clinalg_rank(A)
        sv = _clinalg_svd(A)[1]
        m = A.shape[0]
        n = A.shape[1] if len(A.shape) > 1 else 1
        if rk == n and m > n:
            d = _clinalg_dot(A, x) - B
            t = 0.0
            for k in range(len(d)):
                t += abs(d[k]) ** 2
            res = _unp.zeros(1)
            res[0] = t
        else:
            res = _unp.zeros(0)
        return (x, res, rk, sv)

    def _roots(p):
        """Roots of a polynomial, coefficients highest power first -- the
        eigenvalues of its companion matrix, which is why this belongs next to
        eig rather than anywhere else. Leading zeros are dropped and trailing
        ones become roots at the origin, as numpy does.

        Returns complex, always. numpy hands back a real array when every root
        happens to be real; this proxy's eig is complex throughout and
        collapsing on a zero imaginary part is the thing femm-qt deliberately
        stopped doing everywhere else."""
        c = [complex(v) for v in (p if isinstance(p, (list, tuple)) else _asc(p))]
        nz = [i for i in range(len(c)) if c[i] != 0]
        if not nz:
            return _unp.zeros(0) * (1 + 0j)
        trailing = len(c) - nz[-1] - 1
        c = c[nz[0]:nz[-1] + 1]
        n = len(c) - 1
        r = []
        if n >= 1:
            A = _unp.zeros((n, n)) * (1 + 0j)
            row = _unp.zeros((n,)) * (1 + 0j)
            for j in range(n):
                row[j] = -c[j + 1] / c[0]
            A[0] = row
            for i in range(1, n):
                row = _unp.zeros((n,)) * (1 + 0j)
                row[i - 1] = 1.0
                A[i] = row
            r = [complex(v) for v in _eig(A)[0]]
        out = _unp.zeros(len(r) + trailing) * (1 + 0j)
        for k in range(len(r)):
            out[k] = r[k]
        return out

    def _where(*args):
        """numpy's where does two different jobs. With three arguments it
        selects element-wise, which ulab implements; with one it is simply
        nonzero(), which ulab also implements -- but ulab's where demands three,
        so the one-argument form (the more common of the two in real code) was
        raising a TypeError about positional arguments."""
        if len(args) == 1:
            return _unp.nonzero(args[0])
        if len(args) == 3:
            return _unp.where(*args)
        raise ValueError("where() takes either 1 or 3 arguments, got %d"
                         % len(args))

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
    _INT_DTYPES = (_unp.int8, _unp.uint8, _unp.int16, _unp.uint16, _unp.int32)
    def _hascomplex(x):
        if isinstance(x, (list, tuple)):
            for e in x:
                if _hascomplex(e): return True
            return False
        return isinstance(x, complex)
    def _toint(x):
        if isinstance(x, (list, tuple)):
            return [_toint(e) for e in x]
        return int(x)                                # truncates toward zero, as numpy casts
    def _array(a, *args, **kw):
        try:
            return _unp.array(a, *args, **kw)        # real lists/ndarrays, dtype= cases
        except (TypeError, ValueError):
            # ulab raised. Decide WHY before falling back -- the fallback below
            # only knows how to handle complex input, and it used to run for any
            # failure at all, so np.array(floats, dtype=int32) came back COMPLEX
            # (ulab refuses to truncate a float into an integer dtype; the
            # complex branch then swallowed that and returned 16 bytes per
            # element of the wrong type, silently). It also dropped *args/**kw,
            # so a dtype= was discarded even when the fallback was right.
            dtype = kw.get('dtype', args[0] if args else None)
            cplx = (int(a.dtype) == int(_unp.complex)) if hasattr(a, 'dtype') \
                   else _hascomplex(a)
            if dtype in _INT_DTYPES:
                if cplx:
                    raise                            # numpy refuses this too
                # numpy casts float -> int by truncation; ulab will not, so do
                # it here and hand ulab something it accepts.
                return _unp.array(_toint(a.tolist() if hasattr(a, 'dtype') else a),
                                  dtype=dtype)
            if not cplx:
                raise                                # not a complex-input problem
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
    # one forward and one inverse transform of length M, and M is at least 2N,
    # so the work is 2*(M/N)*(log M / log N) times a same-size transform: about
    # 4.5x where M lands just above 2N, and about 10x for an N just past a power
    # of two, where M overshoots to nearly 4N. Measured on this machine at
    # N=255/256, that is 41 us against 8 us.
    #
    # It really is all transform work and not per-call overhead: cost per
    # butterfly comes out at 0.002-0.003 us on both paths for N >= 256, so the
    # ratio is simply the butterfly count. (Below that a fixed few microseconds
    # per call dominates either way.) Nothing here can be shaved -- a
    # convolution needs two transforms, and ulab offers no length but a power of
    # two to do them at.
    #
    # v and its transform depend only on N, so they are cached: a script doing
    # thousands of equal-length transforms (which is the usual shape) builds
    # them once.
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

    def _as_complex(a):
        z = a if hasattr(a, 'dtype') else _array(a)
        return z * (1 + 0j)

    def _fft_1d(z, n, inv):
        """One transform of a 1-D complex array. The only place the two
        implementations are chosen between."""
        if n and (n & (n - 1)) == 0:
            return _unp.fft.ifft(z) if inv else _unp.fft.fft(z)   # ulab's own
        m, w, vf = _bluestein_tables(n)
        if not inv:
            return _fft_bluestein(z, n, m, w, vf)
        # ifft(x) = conj(fft(conj(x)))/n -- one code path, no second table set.
        r = _fft_bluestein(z.real - z.imag * 1j, n, m, w, vf)
        return (r.real - r.imag * 1j) / n

    def _norm_factor(norm, n, inv):
        """Scale to apply ON TOP of what _fft_1d already produces, which is
        numpy's default 'backward': nothing on the forward transform, 1/n on
        the inverse. 'ortho' is Mathematica's convention, which is the one a
        script ported from there needs."""
        if norm is None or norm == "backward":
            return 1.0
        if norm == "ortho":
            r = _math.sqrt(n)
            return r if inv else (1.0 / r)
        if norm == "forward":
            return float(n) if inv else (1.0 / n)
        raise ValueError("invalid norm %r; expected 'backward', 'ortho' or "
                         "'forward'" % (norm,))

    def _resize_axis(z, n, axis):
        """numpy's n=: zero-pad or truncate along the transform axis, so the
        transform is of length n rather than of the input's length."""
        if not isinstance(n, int) or n < 1:
            raise ValueError("invalid number of FFT data points (%r)" % (n,))
        shp = z.shape
        if len(shp) == 1:
            if n == shp[0]:
                return z
            k = n if n < shp[0] else shp[0]
            out = _unp.zeros((n,)) * (1 + 0j)
            out[0:k] = z[0:k]
            return out
        if axis == 0:
            return _resize_axis(z.transpose(), n, -1).transpose()
        rows, cols = shp
        if n == cols:
            return z
        k = n if n < cols else cols
        out = _unp.zeros((rows, n)) * (1 + 0j)
        for i in range(rows):
            row = _unp.zeros((n,)) * (1 + 0j)
            row[0:k] = z[i][0:k]
            out[i] = row
        return out

    def _fft_along(z, inv, axis):
        """Transform along one axis of a 1- or 2-D complex array, like numpy:
        the shape is preserved and each line is transformed on its own. ulab is
        built with ULAB_MAX_DIMS=2, so there is no third case."""
        shp = z.shape
        if len(shp) == 1:
            if axis not in (0, -1):
                raise ValueError("axis %r is out of bounds for a 1-D array"
                                 % (axis,))
            return _fft_1d(z, shp[0], inv)
        if axis == 0:
            # Columns. ulab has no axis argument anywhere, so borrow the rows
            # case; transpose is cheap next to the transforms themselves.
            return _fft_along(z.transpose(), inv, -1).transpose()
        if axis not in (1, -1):
            raise ValueError("axis %r is out of bounds for a 2-D array"
                             % (axis,))
        rows, cols = shp
        out = _unp.zeros(shp) * (1 + 0j)
        for i in range(rows):
            out[i] = _fft_1d(z[i], cols, inv)
        return out

    # numpy's own default is axis=-1, i.e. ROWS. MATLAB's fft() of a matrix
    # transforms COLUMNS, so a script ported from there wants axis=0 (or a
    # transpose) -- silently the wrong answer otherwise, since both shapes are
    # legal and neither complains.
    def _fft_1axis(a, n, axis, norm, inv):
        z = _as_complex(a)
        if n is not None:
            z = _resize_axis(z, n, axis)
        # The transform length after any resize, which is what norm scales by.
        shp = z.shape
        ln = shp[0] if (len(shp) == 1 or axis == 0) else shp[1]
        r = _fft_along(z, inv, axis)
        f = _norm_factor(norm, ln, inv)
        return r if f == 1.0 else r * f

    def _fft(a, n=None, axis=-1, norm=None):
        return _fft_1axis(a, n, axis, norm, False)

    def _ifft(a, n=None, axis=-1, norm=None):
        return _fft_1axis(a, n, axis, norm, True)

    def _fft2_impl(a, s, axes, norm, inv, who):
        if axes is not None and tuple(axes) not in ((-2, -1), (0, 1)):
            raise NotImplementedError(who + "(): only the last two axes")
        z = _as_complex(a)
        if len(z.shape) != 2:
            raise ValueError(who + "() needs a 2-D array")
        if s is not None:
            if len(s) != 2:
                raise ValueError(who + "(): s must hold one length per axis")
            z = _resize_axis(_resize_axis(z, s[1], -1), s[0], 0)
        # Both axes carry the same convention, so composing gives numpy's
        # answer: ortho ends up 1/sqrt(rows*cols), forward 1/(rows*cols).
        rows, cols = z.shape
        r = _fft_along(z, inv, -1)
        f = _norm_factor(norm, cols, inv)
        if f != 1.0:
            r = r * f
        r = _fft_along(r, inv, 0)
        f = _norm_factor(norm, rows, inv)
        return r if f == 1.0 else r * f

    def _fft2(a, s=None, axes=None, norm=None):
        return _fft2_impl(a, s, axes, norm, False, "fft2")

    def _ifft2(a, s=None, axes=None, norm=None):
        return _fft2_impl(a, s, axes, norm, True, "ifft2")

    # --- complex coverage gaps -------------------------------------------------
    #
    # Found by sweeping dir(numpy) with complex input (testing/NumpyComplexTest.py).
    # Everything below already works on a REAL array through ulab; these wrappers
    # add only the complex branch, so real arrays keep ulab's fast path.
    #
    # Added only where NUMPY ITSELF supports complex. Deliberately absent, and
    # pinned as absent by the test: floor, ceil, degrees, radians, arctan2.
    # numpy raises TypeError for complex on those too, so ulab's refusal is
    # already correct and "fixing" them would be a divergence, not a fix.
    _LN10 = 2.302585092994046
    _LN2 = 0.6931471805599453
    _INF = float('inf')

    # Inverse hyperbolics: no C scalar helper, and ulab has no complex form, so
    # compose from log and sqrt on the principal branch the way numpy does.
    # acosh uses the FACTORED sqrt(z+1)*sqrt(z-1) rather than sqrt(z*z-1) --
    # that is what puts the branch cut where numpy puts it.
    def _cacosh(z): return _clog(z + _csqrt(z + 1.0) * _csqrt(z - 1.0))
    def _casinh(z): return _clog(z + _csqrt(z * z + 1.0))
    def _catanh(z): return (_clog(1.0 + z) - _clog(1.0 - z)) * 0.5
    def _clog10(z): return _clog(z) / _LN10
    def _clog2(z):  return _clog(z) / _LN2
    def _cexpm1(z): return _cexp(z) - 1.0
    def _csinc(z):
        w = PI * z
        return 1.0 + 0j if w == 0 else _csin(w) / w
    def _cisfinite(z):
        r, i = Re(z), Im(z)
        return r == r and i == i and abs(r) != _INF and abs(i) != _INF
    def _cisinf(z):
        return abs(Re(z)) == _INF or abs(Im(z)) == _INF

    def _iscplx(a):
        if hasattr(a, 'dtype'):
            return int(a.dtype) == int(_unp.complex)
        return isinstance(a, complex)
    def _flatc(z):
        n = 1
        for d in z.shape: n *= d
        return z.reshape((n,))
    def _boolarr(vals, shape):
        return _unp.array([1 if v else 0 for v in vals],
                          dtype=_unp.bool).reshape(shape)

    # numpy ranks complex LEXICOGRAPHICALLY -- real part first, imaginary as the
    # tie-break. Every function here that orders or compares complex values goes
    # through this one key, so they cannot disagree with each other.
    def _ckey(v): return (Re(v), Im(v))

    def _cunary(real_fn, scalar_fn, boolean=False):
        def w(a, *args, **kw):
            if not _iscplx(a):
                return real_fn(a, *args, **kw)
            if not hasattr(a, 'shape'):
                return scalar_fn(a)
            z = _flatc(a)
            vals = [scalar_fn(z[i]) for i in range(len(z))]
            return _boolarr(vals, a.shape) if boolean \
                else _array(vals).reshape(a.shape)
        return w

    def _cbinary(real_fn, scalar_fn, boolean=False):
        def w(x, y, *args, **kw):
            if not (_iscplx(x) or _iscplx(y)):
                return real_fn(x, y, *args, **kw)
            hx, hy = hasattr(x, 'shape'), hasattr(y, 'shape')
            xs = _flatc(x) if hx else None
            ys = _flatc(y) if hy else None
            n = len(xs) if hx else (len(ys) if hy else 1)
            vals = [scalar_fn(xs[i] if hx else x, ys[i] if hy else y)
                    for i in range(n)]
            if not (hx or hy):
                return vals[0]
            shape = x.shape if hx else y.shape
            return _boolarr(vals, shape) if boolean \
                else _array(vals).reshape(shape)
        return w

    def _crank(real_fn, cplx_fn):
        def w(a, *args, **kw):
            return cplx_fn(a) if _iscplx(a) else real_fn(a, *args, **kw)
        return w

    def _cpick(a, lower, want_index):
        z = _flatc(a)
        best, bk = 0, _ckey(z[0])
        for i in range(1, len(z)):
            k = _ckey(z[i])
            if (k < bk) if lower else (k > bk):
                best, bk = i, k
        return best if want_index else z[best]

    def _csorted(a):
        z = _flatc(a)
        return sorted([z[i] for i in range(len(z))], key=_ckey)

    def _cmedian(a):
        s = _csorted(a)
        n = len(s)
        return s[n // 2] if n % 2 else (s[n // 2 - 1] + s[n // 2]) * 0.5

    def _cstd(a):
        # numpy's std of a complex array is REAL: sqrt(mean(|x - mean|^2))
        z = _flatc(a)
        n = len(z)
        m = _sum(z) / n
        t = 0.0
        for i in range(n):
            d = z[i] - m
            t += Re(d) * Re(d) + Im(d) * Im(d)
        return _math.sqrt(t / n)

    def _cdiff(a, n=1, **kw):
        z = a
        for _ in range(n):
            z = z[1:] - z[:-1]
        return z

    def _caround(a, decimals=0):
        if not _iscplx(a):
            return _unp.around(a, decimals) if decimals else _unp.around(a)
        f = 10.0 ** decimals
        def r1(v):
            return round(Re(v) * f) / f + (round(Im(v) * f) / f) * 1j
        if not hasattr(a, 'shape'):
            return r1(a)
        z = _flatc(a)
        return _array([r1(z[i]) for i in range(len(z))]).reshape(a.shape)

    def _cpolyval(p, x):
        if not (_iscplx(p) or _iscplx(x)):
            return _unp.polyval(p, x)
        cs = [c for c in (_flatc(p) if hasattr(p, 'shape') else p)]
        def ev(t):
            r = 0
            for c in cs: r = r * t + c
            return r
        if hasattr(x, 'shape'):
            z = _flatc(x)
            return _array([ev(z[i]) for i in range(len(z))]).reshape(x.shape)
        return ev(x)

    def _cmaxfn(a, b): return a if _ckey(a) >= _ckey(b) else b
    def _cminfn(a, b): return a if _ckey(a) <= _ckey(b) else b

    class _FftProxy:
        fft   = staticmethod(_fft)
        ifft  = staticmethod(_ifft)
        fft2  = staticmethod(_fft2)
        ifft2 = staticmethod(_ifft2)
        def __getattr__(self, k): return getattr(_unp.fft, k)

    class _LinalgProxy:
        eig   = staticmethod(_eig);   qr   = staticmethod(_qr)
        svd   = staticmethod(_svd);   solve = staticmethod(_solve)
        inv   = staticmethod(_inv);   det  = staticmethod(_det)
        pinv  = staticmethod(_pinv);  cond = staticmethod(_cond)
        rank  = staticmethod(_rank);  norm = staticmethod(_norm)
        matrix_rank = staticmethod(_rank)     # numpy's own spelling
        eigvals = staticmethod(_eigvals)
        lstsq   = staticmethod(_lstsq)
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
        # ulab has none of these SPELLINGS; serve them from the scalar helpers.
        # The bare acos/asin/atan below are ulab's own names for the same
        # functions -- they take the real fast path and only fall back for
        # complex, so prefer them; these stay for numpy-spelling compatibility.
        arcsin = staticmethod(_velw(_casin))
        arccos = staticmethod(_velw(_cacos))
        arctan = staticmethod(_velw(_catan))
        angle  = staticmethod(_velw(arg))     # phase; arg() returns a real
        # ulab HAS these names but rejects complex; add the complex branch.
        # np.arccos(z) worked while np.acos(z) raised, which is a confusing
        # thing to hit -- same function, one spelling wired up and one not.
        acos  = staticmethod(_vec(_unp.acos,  _cacos))
        asin  = staticmethod(_vec(_unp.asin,  _casin))
        atan  = staticmethod(_vec(_unp.atan,  _catan))
        acosh = staticmethod(_vec(_unp.acosh, _cacosh))
        asinh = staticmethod(_vec(_unp.asinh, _casinh))
        atanh = staticmethod(_vec(_unp.atanh, _catanh))
        # elementwise, complex branch only
        log10 = staticmethod(_cunary(_unp.log10, _clog10))
        log2  = staticmethod(_cunary(_unp.log2,  _clog2))
        expm1 = staticmethod(_cunary(_unp.expm1, _cexpm1))
        sinc  = staticmethod(_cunary(_unp.sinc,  _csinc))
        isfinite = staticmethod(_cunary(_unp.isfinite, _cisfinite, True))
        isinf    = staticmethod(_cunary(_unp.isinf,    _cisinf,    True))
        around   = staticmethod(_caround)
        # ranking and statistics -- lexicographic, as numpy orders complex
        max    = staticmethod(_crank(_unp.max,    lambda a: _cpick(a, False, False)))
        min    = staticmethod(_crank(_unp.min,    lambda a: _cpick(a, True,  False)))
        argmax = staticmethod(_crank(_unp.argmax, lambda a: _cpick(a, False, True)))
        argmin = staticmethod(_crank(_unp.argmin, lambda a: _cpick(a, True,  True)))
        sort   = staticmethod(_crank(_unp.sort,   lambda a: _array(_csorted(a))))
        median = staticmethod(_crank(_unp.median, _cmedian))
        std    = staticmethod(_crank(_unp.std,    _cstd))
        diff   = staticmethod(_crank(_unp.diff,   _cdiff))
        # elementwise comparisons
        equal     = staticmethod(_cbinary(_unp.equal,     lambda a, b: a == b, True))
        not_equal = staticmethod(_cbinary(_unp.not_equal, lambda a, b: a != b, True))
        maximum   = staticmethod(_cbinary(_unp.maximum,   _cmaxfn))
        minimum   = staticmethod(_cbinary(_unp.minimum,   _cminfn))
        polyval   = staticmethod(_cpolyval)
        real   = staticmethod(_real)
        imag   = staticmethod(_imag)
        power  = staticmethod(_power)
        where  = staticmethod(_where)
        roots  = staticmethod(_roots)
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
