# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
"""Use CPython packages -- matplotlib above all -- from femm-qt scripts.

    import matplotlib.pyplot as plt
    fig, ax = plt.subplots()
    ax.plot([0, 1, 2], [0, 1, 4], "r-")
    plt.show()

Objects live in a separate CPython process; what you hold here is a proxy that
forwards attribute access, calls, subscripting and arithmetic to it.  femm-qt
never links or ships matplotlib -- it uses the Python you already have.

Anything importable over there works, not just matplotlib:

    csv = pybridge.import_module("csv")

Two limits worth knowing, both inherent to the boundary rather than temporary:
callables cannot cross it (no `key=` function for `sorted`, no scipy objective
function -- define those server-side with `pybridge.exec`), and femm-qt's own
`numpy`/`scipy` stay local, since those names are ulab and running them over a
socket would be a large step backwards.  `pybridge.import_module("numpy")`
reaches the CPython one when you specifically want it.

This module is the protocol; `_pybridge` underneath it is only transport.
Design notes: pybridge-implementation-plan.md.
"""

import _pybridge
import json
import struct

import numpy as np   # ulab, in femm-qt

BULK_MIN = 256          # a numeric list this long goes over as raw bytes
TEXT_LIMIT = 4096       # matches the worker's repr/str truncation
RELEASE_BATCH = 64

# Tag keys the codec reserves.  A dict argument using one of these as a key
# cannot be told apart from an encoded value, so the encoder refuses it.
RESERVED = ("__h__", "__nd__", "__t__", "__b__", "__f__", "__c__")

# ulab's dtypes are the ord() of the array typecode.  int32 is femm-qt's
# addition to ulab (see micropython/ulab/code/ndarray.h); there is still no
# int64/uint32/float32, which is why the client tells the worker what it can
# hold (ACCEPT below) rather than taking whatever numpy happens to have.
_TO_WIRE = {100: "f8", 99: "c16", 98: "i1", 104: "i2", 66: "u1", 72: "u2",
            105: "i4"}
_FROM_WIRE = {"f8": np.float, "c16": np.complex, "i1": np.int8,
              "i2": np.int16, "u1": np.uint8, "u2": np.uint16,
              "i4": np.int32}
ACCEPT = list(_FROM_WIRE.keys())


class BridgeError(Exception):
    """The bridge itself failed: no Python found, worker died, stale handle."""


class RemoteError(Exception):
    """An exception raised by your code inside the CPython worker.

    Common exception types are re-raised as the matching built-in instead, so
    `except ValueError:` works the way it reads; this is the fallback for the
    rest.  `.etype` is the original class name and `.tb` the CPython traceback.
    """

    def __init__(self, etype, msg, tb=""):
        # super(), not Exception.__init__(self, ...): MicroPython's Exception
        # exposes no __init__ attribute, so the CPython spelling raises an
        # AttributeError from inside the error path and buries the real fault.
        super().__init__("%s: %s" % (etype, msg))
        self.etype = etype
        self.msg = msg
        self.tb = tb


_BUILTIN_ERRORS = {
    "ValueError": ValueError, "TypeError": TypeError, "KeyError": KeyError,
    "IndexError": IndexError, "AttributeError": AttributeError,
    "ZeroDivisionError": ZeroDivisionError, "OSError": OSError,
    "RuntimeError": RuntimeError, "NotImplementedError": NotImplementedError,
    "OverflowError": OverflowError, "MemoryError": MemoryError,
    "ArithmeticError": ArithmeticError, "ImportError": ImportError,
    "NameError": NameError, "StopIteration": StopIteration,
    # CPython raises ModuleNotFoundError for a missing import; MicroPython has
    # only its base class, which is what a script would catch anyway.
    "ModuleNotFoundError": ImportError,
    "FileNotFoundError": OSError, "PermissionError": OSError,
    "IsADirectoryError": OSError, "NotADirectoryError": OSError,
    "FloatingPointError": ArithmeticError, "AssertionError": AssertionError,
    "EOFError": EOFError, "LookupError": LookupError,
    "UnicodeError": ValueError, "SyntaxError": SyntaxError,
}

# Dunders that carry data rather than behaviour, and so are safe to forward.
_DATA_DUNDERS = ("__version__", "__name__", "__doc__", "__file__", "__all__",
                 "__author__", "__path__", "__package__", "__module__",
                 "__qualname__", "__annotations__", "__bases__", "__dict__")

_next_id = [0]
_pending_release = []


# ---------------------------------------------------------------------------
# Framing
# ---------------------------------------------------------------------------

def _frame(msg, blob=b""):
    text = json.dumps(msg).encode()
    if blob:
        payload = b"B" + struct.pack("<I", len(text)) + text + blob
    else:
        payload = b"J" + text
    return struct.pack("<I", len(payload)) + payload


def _unframe(buf):
    n = struct.unpack("<I", buf[:4])[0]
    if len(buf) != 4 + n:
        raise BridgeError("short frame from the python worker")
    if buf[4:5] == b"J":
        return json.loads(buf[5:].decode()), b""
    hlen = struct.unpack("<I", buf[5:9])[0]
    return json.loads(buf[9:9 + hlen].decode()), buf[9 + hlen:]


def _rpc(op, blob=b"", **fields):
    """One round trip: encode, send, decode, raise anything that went wrong."""
    if _pending_release:
        _flush_releases()
    return _send(op, blob, fields)


def _send(op, blob, fields):
    _next_id[0] += 1
    fields["op"] = op
    fields["id"] = _next_id[0]
    try:
        reply = _pybridge.rpc(_frame(fields, blob))
    except OSError as e:
        # The transport reports every lifecycle failure as OSError; give it a
        # name that says which layer broke.
        raise BridgeError(str(e))
    msg, rblob = _unframe(reply)
    if msg.get("id") != _next_id[0]:
        raise BridgeError("the python worker answered the wrong request")
    if not msg.get("ok"):
        err = msg.get("err") or {}
        etype = err.get("etype", "RemoteError")
        text = err.get("msg", "")
        cls = _BUILTIN_ERRORS.get(etype)
        if cls is not None:
            raise cls(text)
        raise RemoteError(etype, text, err.get("tb", ""))
    return _decode(msg.get("v"), rblob)


# ---------------------------------------------------------------------------
# Encoding: what we send
# ---------------------------------------------------------------------------

def _encode(v, out):
    if isinstance(v, Proxy):
        if v._gen != _pybridge.session():
            raise BridgeError("this object belongs to a python worker that has "
                              "since been restarted")
        return {"__h__": v._h}
    if v is None or isinstance(v, bool) or isinstance(v, int) or \
            isinstance(v, str):
        return v
    if isinstance(v, float):
        return _encode_float(v)
    if isinstance(v, complex):
        return {"__c__": [_encode_float(v.real), _encode_float(v.imag)]}
    if isinstance(v, (bytes, bytearray)):
        return _blob_ref(bytes(v), out)
    if isinstance(v, np.ndarray):
        return _array_ref(v, out)
    if isinstance(v, (list, tuple)):
        if len(v) >= BULK_MIN:
            bulk, has_complex = _scan_numeric(v)
            if bulk:
                dt = np.complex if has_complex else np.float
                return _array_ref(np.array(v, dtype=dt), out)
        return [_encode(x, out) for x in v]
    if isinstance(v, dict):
        for k in v:
            if not isinstance(k, str):
                raise BridgeError("dictionary keys must be strings to cross "
                                  "the bridge")
            if k in RESERVED:
                raise BridgeError("the key %r is reserved by the bridge "
                                  "protocol; build this object server-side "
                                  "with pybridge.put() instead" % k)
        return dict([(k, _encode(x, out)) for k, x in v.items()])
    # Name the remedy that fits what was actually passed. This message is met
    # at the exact moment a user learns the process boundary exists, so a hint
    # about the wrong kind of object ("if it is a function...", for a file) is
    # worse than no hint -- it sends them somewhere that cannot work.
    if hasattr(v, "read") or hasattr(v, "write"):
        hint = ("a file belongs to the process that opened it; open it in the "
                "worker with pybridge.open(path, mode) and pass that instead")
    elif callable(v):
        hint = "define it server-side with pybridge.exec() and fetch it by name"
    else:
        hint = ("only numbers, strings, bytes, arrays and containers of those "
                "cross by value; build it server-side with pybridge.exec() or "
                "pybridge.put()")
    raise BridgeError("a %s cannot cross the bridge -- %s"
                      % (type(v).__name__, hint))


def _scan_numeric(seq):
    """(every element is bulk-encodable, any of them complex), in one pass."""
    has_complex = False
    for x in seq:
        if isinstance(x, float):
            continue
        if isinstance(x, bool):
            return False, False
        if isinstance(x, complex):
            has_complex = True
        elif not isinstance(x, int):
            return False, False
    return True, has_complex


def _encode_float(f):
    # JSON has no nan/inf and MicroPython's parser takes no extensions, so the
    # non-finite values matplotlib deals in travel tagged.
    if f != f:
        return {"__f__": "nan"}
    if f == float("inf"):
        return {"__f__": "inf"}
    if f == float("-inf"):
        return {"__f__": "-inf"}
    return f


def _blob_ref(data, out):
    off = len(out)
    out.extend(data)
    return {"__b__": {"off": off, "len": len(data)}}


def _array_ref(a, out):
    code = _TO_WIRE.get(int(a.dtype))
    if code is None:
        raise BridgeError("arrays of dtype %r cannot cross the bridge"
                          % (a.dtype,))
    try:
        raw = a.tobytes()
    except ValueError:
        # ulab refuses tobytes() on a non-dense array, and a slice like x[::2]
        # is exactly what gets plotted. Materialise a dense copy and go on --
        # never let a sliced array fail out of ax.plot().
        raw = np.array(a).tobytes()
    off = len(out)
    out.extend(raw)
    return {"__nd__": {"dtype": code, "shape": list(a.shape), "off": off,
                       "len": len(raw)}}


def _encode_args(args, kwargs):
    out = bytearray()
    enc_args = [_encode(a, out) for a in args]
    enc_kw = dict([(k, _encode(v, out)) for k, v in kwargs.items()])
    return enc_args, enc_kw, bytes(out)


# ---------------------------------------------------------------------------
# Decoding: what comes back
# ---------------------------------------------------------------------------

def _decode(v, blob):
    if isinstance(v, list):
        return [_decode(x, blob) for x in v]
    if not isinstance(v, dict):
        return v
    if "__h__" in v:
        return Proxy(v["__h__"], v.get("cls", "?"))
    if "__t__" in v:
        return tuple([_decode(x, blob) for x in v["__t__"]])
    if "__f__" in v:
        return {"nan": float("nan"), "inf": float("inf"),
                "-inf": float("-inf")}[v["__f__"]]
    if "__c__" in v:
        parts = [_decode(x, blob) for x in v["__c__"]]
        return complex(parts[0], parts[1])
    if "__b__" in v:
        s = v["__b__"]
        return bytes(blob[s["off"]:s["off"] + s["len"]])
    if "__nd__" in v:
        return _decode_array(v["__nd__"], blob)
    return dict([(k, _decode(x, blob)) for k, x in v.items()])


def _decode_array(spec, blob):
    dt = _FROM_WIRE.get(spec["dtype"])
    if dt is None:
        raise BridgeError("the worker sent dtype %r, which ulab has no type "
                          "for" % (spec["dtype"],))
    raw = blob[spec["off"]:spec["off"] + spec["len"]]
    a = np.frombuffer(raw, dtype=dt)
    shape = spec.get("shape") or []
    if len(shape) > 1:
        a = a.reshape(tuple(shape))
    return a


# ---------------------------------------------------------------------------
# The proxy
# ---------------------------------------------------------------------------

class Proxy:
    """Stands in for an object living in the CPython worker."""

    def __init__(self, h, cls):
        # object.__setattr__, so these do not go back down the wire as
        # attribute assignments on the remote object.
        object.__setattr__(self, "_h", h)
        object.__setattr__(self, "_cls", cls)
        object.__setattr__(self, "_gen", _pybridge.session())

    # -- attributes -------------------------------------------------------

    def _handle(self):
        """The remote handle, refusing it if the worker has been replaced.

        Every operation goes through this, not just the ones that take a proxy
        as an argument: after a restart, handle 7 in a fresh worker is a
        different object, so an unchecked call would quietly act on the wrong
        thing instead of failing.
        """
        if self._gen != _pybridge.session():
            raise BridgeError("this object belongs to a python worker that "
                              "has since been restarted")
        return self._h

    def __getattr__(self, name):
        if name.startswith("__") and name.endswith("__") and \
                name not in _DATA_DUNDERS:
            # Never answer a *protocol* dunder from here. Resolving __iter__ or
            # __len__ would make every proxy claim to be iterable and sized,
            # and answering __del__ would put a network round trip inside the
            # garbage collector -- possibly after the worker is gone. The
            # handful of dunders that are ordinary data, like __version__, are
            # forwarded, because reading them is exactly what a user does.
            raise AttributeError(name)
        return _rpc("getattr", h=self._handle(), name=name)

    def __setattr__(self, name, value):
        (v,), _, blob = _encode_args((value,), {})
        _rpc("setattr", blob, h=self._handle(), name=name, value=v)

    # -- calling ----------------------------------------------------------

    def __call__(self, *args, **kwargs):
        a, kw, blob = _encode_args(args, kwargs)
        return _rpc("call", blob, h=self._handle(), args=a, kwargs=kw)

    # -- containers -------------------------------------------------------

    def __getitem__(self, key):
        (k,), _, blob = _encode_args((key,), {})
        return _rpc("getitem", blob, h=self._handle(), key=k)

    def __setitem__(self, key, value):
        (k, v), _, blob = _encode_args((key, value), {})
        _rpc("setitem", blob, h=self._handle(), key=k, value=v)

    def __delitem__(self, key):
        (k,), _, blob = _encode_args((key,), {})
        _rpc("delitem", blob, h=self._handle(), key=k)

    def __len__(self):
        return _rpc("len", h=self._handle())

    def __contains__(self, item):
        (v,), _, blob = _encode_args((item,), {})
        return _rpc("contains", blob, h=self._handle(), item=v)

    def __bool__(self):
        return _rpc("truth", h=self._handle())

    def __iter__(self):
        return _rpc("iter", h=self._handle())

    def __next__(self):
        v = _send("next", b"", {"h": self._handle()})
        if isinstance(v, dict) and v.get("t") == "stopiter":
            raise StopIteration
        return v

    def __enter__(self):
        return _rpc("callmethod", h=self._handle(), name="__enter__", args=[],
                    kwargs={})

    def __exit__(self, et, ev, tb):
        return _rpc("callmethod", h=self._handle(), name="__exit__",
                    args=[None, None, None], kwargs={})

    # -- text -------------------------------------------------------------

    def __repr__(self):
        try:
            return _rpc("repr", h=self._handle())
        except Exception:
            return "<py.%s #%d>" % (self._cls, self._h)

    def __str__(self):
        try:
            return _rpc("str", h=self._handle())
        except Exception:
            return self.__repr__()

    # -- operators --------------------------------------------------------

    def _bin(self, name, a, b):
        (ea, eb), _, blob = _encode_args((a, b), {})
        return _rpc("binop", blob, operator=name, a=ea, b=eb)

    def _un(self, name):
        (a,), _, blob = _encode_args((self,), {})
        return _rpc("unop", blob, operator=name, a=a)

    def __hash__(self):
        # CPython's `__hash__ = object.__hash__` idiom is unavailable here --
        # MicroPython's `object` exposes no __hash__ to borrow. The handle is a
        # perfectly good hash, and defining one keeps a proxy usable as a dict
        # key despite __eq__ being forwarded.
        return self._h


def _install_operators():
    """Bind the arithmetic and comparison dunders.

    Written as a loop because there are 32 of them and spelling each out by
    hand is 32 chances to paste the wrong operator name.  Requires
    MICROPY_PY_ALL_SPECIAL_METHODS and ..._REVERSE_SPECIAL_METHODS, both
    enabled for exactly this (see testing/ProxyDunderTest.py).
    """
    def binder(op, reverse):
        if reverse:
            def fn(self, other):
                return self._bin(op, other, self)
        else:
            def fn(self, other):
                return self._bin(op, self, other)
        return fn

    def unary(op):
        def fn(self):
            return self._un(op)
        return fn

    for op, dunder in (("add", "add"), ("sub", "sub"), ("mul", "mul"),
                       ("truediv", "truediv"), ("floordiv", "floordiv"),
                       ("mod", "mod"), ("pow", "pow"), ("matmul", "matmul"),
                       ("and_", "and"), ("or_", "or"), ("xor", "xor"),
                       ("lshift", "lshift"), ("rshift", "rshift")):
        setattr(Proxy, "__%s__" % dunder, binder(op, False))
        setattr(Proxy, "__r%s__" % dunder, binder(op, True))
    for op in ("eq", "ne", "lt", "le", "gt", "ge"):
        setattr(Proxy, "__%s__" % op, binder(op, False))
    for op in ("neg", "pos", "abs", "invert"):
        setattr(Proxy, "__%s__" % op, unary(op))


_install_operators()


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def import_module(name):
    """The CPython module `name`, as a proxy. The escape hatch for anything
    without a shim: pybridge.import_module("scipy.optimize")."""
    return _rpc("import", name=name)


def eval(code):
    """Evaluate an expression in the worker's persistent namespace, which is
    pre-seeded with `np` (real numpy) and `plt`."""
    return _rpc("eval", code=code, mode="eval")


def exec(code):
    """Run statements in that same namespace. This is where to define any
    function that needs to be callable from the CPython side."""
    return _rpc("eval", code=code, mode="exec")


def put(name, value):
    """Bind a value into the worker's namespace under `name`. Bulk arrays take
    the binary path, so this is the cheap way to move a lot of data."""
    (v,), _, blob = _encode_args((value,), {})
    return _rpc("put", blob, name=name, value=v)


def fetch(proxy):
    """Pull a remote value across by value. An ndarray comes back as a ulab
    array; a dtype ulab has no room for (int32, float32, ...) is widened to
    float64 by the worker rather than failing."""
    if not isinstance(proxy, Proxy):
        return proxy
    return _rpc("fetch", h=proxy._h, accept=ACCEPT)


def open(path, mode="r", **kwargs):
    """Open a file *in the worker*. Both processes see the same filesystem, so
    this is how a CPython reader (csv, json, ...) gets a file object it can
    actually use -- a femm-qt file object cannot cross."""
    builtins = import_module("builtins")
    return builtins.open(path, mode, **kwargs)


def call_method(proxy, name, *args, **kwargs):
    """obj.name(*args) in one round trip instead of two. Rarely needed --
    proxy.name(...) reads better and costs about 100 microseconds more."""
    a, kw, blob = _encode_args(args, kwargs)
    return _rpc("callmethod", blob, h=proxy._h, name=name, args=a, kwargs=kw)


def dir(proxy):
    """Attribute names of a remote object -- the way to explore an API from
    the femm-qt console."""
    return _rpc("dir", h=proxy._h)


def release(*proxies):
    """Tell the worker it may drop these objects. Optional: everything goes at
    restart() anyway, and a plotting session's leftovers are a few thousand
    dictionary entries in a CPython process."""
    session = _pybridge.session()
    for p in proxies:
        if isinstance(p, Proxy) and p._gen == session:
            _pending_release.append(p._h)
    if len(_pending_release) > RELEASE_BATCH:
        _flush_releases()


def _flush_releases():
    handles = list(_pending_release)
    del _pending_release[:]
    _send("release", b"", {"handles": handles})


def info():
    """What the worker reported at startup: versions, interpreter path, and
    the plot backend actually in use. Empty dict if it has not started."""
    raw = _pybridge.info()
    if not raw:
        return {}
    return json.loads(raw).get("v", {})


def interpreter():
    """Path of the CPython in use (or the one pinned by set_interpreter)."""
    return _pybridge.interpreter()


def set_interpreter(path):
    """Pin a specific python. "" restores automatic discovery. Stops any
    running worker, since the point is to use a different one."""
    _pybridge.set_interpreter(path)


def set_backend(name):
    """Force a matplotlib backend ("qtagg", "tkagg", "macosx"). "" probes.
    Takes effect at the next start."""
    _pybridge.set_backend(name)


def start():
    """Start the worker now rather than at first use. Only useful to front-load
    the delay, or to check that plotting will work at all."""
    try:
        _pybridge.ensure_started()
    except OSError as e:
        raise BridgeError(str(e))


def restart():
    """Discard the worker and its figures; the next use starts a fresh one."""
    del _pending_release[:]
    _pybridge.restart()


def shutdown():
    """Stop the worker and close its windows."""
    del _pending_release[:]
    _pybridge.shutdown()


def running():
    return _pybridge.running()
