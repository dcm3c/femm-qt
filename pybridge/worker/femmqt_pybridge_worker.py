#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
"""femm-qt PyBridge worker: a resident CPython process that femm-qt's embedded
MicroPython drives over a localhost socket, so scripts can use matplotlib (and
any other CPython package) without femm-qt linking or shipping one.

Design notes live in pybridge-implementation-plan.md; the short version:

  * femm-qt LISTENS, this worker CONNECTS (``--connect host:port``).  The token
    arrives in the environment (FEMMQT_PYBRIDGE_TOKEN), never argv, because argv
    is visible in process listings.
  * The protocol is strictly request/response, one response per request, in
    order.  That is what lets the client block on a round trip and lets this
    process stay single threaded.
  * Single threaded on purpose: GUI work then happens on the process main
    thread with no marshalling, which is a hard requirement on macOS and a
    simplification everywhere else.  Between round trips the loop pumps the
    figure windows (``pump_gui``).
  * Objects stay HERE.  A call's result is normally handed back as an integer
    handle; only scalars and small containers travel by value, and bulk data
    only crosses when the client asks (``fetch``) or sends it (binary frames).

Everything in this file is stdlib except numpy and matplotlib, which are
imported at startup if present and reported in the ``hello`` response.  It runs
standalone: ``pybridge/tests/fake_client.py`` drives it with no femm-qt in
sight, which is how the protocol is developed and debugged.
"""

import argparse
import importlib
import json
import math
import operator
import os
import select
import socket
import struct
import sys
import traceback

WORKER_VERSION = "1.0"
PROTOCOL = 1

KIND_JSON = 0x4A  # 'J'
KIND_BIN = 0x42  # 'B'

MAX_FRAME = 256 * 1024 * 1024  # reject rather than allocate
MAX_FETCH = 128 * 1024 * 1024  # fetch payload cap
BY_VALUE_LEN = 64  # max container length returned by value
BY_VALUE_ELEMS = 4096  # max total elements in one by-value response
FETCH_ELEMS = 1 << 20  # fetch is explicit, so it gets a bigger budget
FETCH_ARRAY_MIN = 256  # a fetched numeric list this long becomes an array
TEXT_LIMIT = 4096  # repr/str truncation

# Protocol dtype names -> numpy dtype strings.  Little endian throughout: every
# platform femm-qt targets is LE, and pinning it keeps the MicroPython side from
# having to care.
DTYPES = {
    "f8": "<f8", "f4": "<f4",
    "i8": "<i8", "i4": "<i4", "i2": "<i2", "i1": "|i1",
    "u8": "<u8", "u4": "<u4", "u2": "<u2", "u1": "|u1",
    # Complex is one width on the wire.  ulab has exactly one complex type --
    # two mp_float_t, i.e. 16 bytes, laid out identically to numpy's
    # complex128 -- so c16 crosses with no conversion at either end, and a
    # worker-side complex64 is widened to it losslessly.
    "c16": "<c16",
}

# Operand names accepted by binop/unop.  Whitelisted rather than looked up in
# `operator` by name, so a request can't reach into the module's other contents.
BINOPS = {name: getattr(operator, name) for name in (
    "add", "sub", "mul", "truediv", "floordiv", "mod", "pow", "matmul",
    "eq", "ne", "lt", "le", "gt", "ge", "and_", "or_", "xor",
    "lshift", "rshift")}
UNOPS = {name: getattr(operator, name) for name in (
    "neg", "pos", "abs", "invert", "not_")}

np = None  # numpy, if importable
plt = None  # matplotlib.pyplot, if a backend came up
_backend = None
_backend_error = None


def log(msg):
    """Diagnostics go to stderr, which femm-qt captures into the script log."""
    sys.stderr.write("[pybridge] %s\n" % msg)
    sys.stderr.flush()


# --------------------------------------------------------------------------
# Framing
# --------------------------------------------------------------------------
#
#   [u32 length LE][u8 kind][payload]        length counts kind + payload
#
#   kind 'J': payload is a UTF-8 JSON document.
#   kind 'B': payload is [u32 header_len LE][header JSON][binary region], and
#             values in the header may reference offsets into that region.

class ProtocolFault(Exception):
    """A malformed or impossible request.  Reported as BridgeProtocolError."""


class FramingError(Exception):
    """The byte stream itself is broken.  Unrecoverable: the worker exits."""


class FrameBuffer:
    """Reassembles frames from arbitrarily chopped TCP reads."""

    def __init__(self):
        self._buf = bytearray()

    def feed(self, data):
        self._buf += data

    def frames(self):
        """Yield (kind, payload) for every complete frame held."""
        while True:
            if len(self._buf) < 4:
                return
            (n,) = struct.unpack_from("<I", self._buf, 0)
            if n < 1:
                raise FramingError("zero-length frame")
            if n > MAX_FRAME:
                raise FramingError("frame of %d bytes exceeds the %d byte limit"
                                   % (n, MAX_FRAME))
            if len(self._buf) < 4 + n:
                return
            kind = self._buf[4]
            payload = bytes(self._buf[5:4 + n])
            del self._buf[:4 + n]
            yield kind, payload


def parse_frame(kind, payload):
    """(kind, payload) -> (message dict, binary region)."""
    if kind == KIND_JSON:
        blob = b""
        text = payload
    elif kind == KIND_BIN:
        if len(payload) < 4:
            raise FramingError("binary frame shorter than its header length")
        (hlen,) = struct.unpack_from("<I", payload, 0)
        if 4 + hlen > len(payload):
            raise FramingError("binary frame header runs past the frame")
        text = payload[4:4 + hlen]
        blob = payload[4 + hlen:]
    else:
        raise FramingError("unknown frame kind 0x%02X" % kind)
    try:
        msg = json.loads(text.decode("utf-8"))
    except Exception as e:
        raise FramingError("bad JSON in frame: %s" % e)
    if not isinstance(msg, dict):
        raise FramingError("frame payload is not a JSON object")
    return msg, blob


def build_frame(msg, blob=b""):
    text = json.dumps(msg, allow_nan=False).encode("utf-8")
    if blob:
        payload = struct.pack("<I", len(text)) + text + bytes(blob)
        kind = KIND_BIN
    else:
        payload = text
        kind = KIND_JSON
    body = bytes([kind]) + payload
    if len(body) > MAX_FRAME:
        raise ProtocolFault("response of %d bytes exceeds the frame limit"
                            % len(body))
    return struct.pack("<I", len(body)) + body


class Out:
    """Accumulates the binary region of a response being encoded."""

    def __init__(self):
        self.blob = bytearray()

    def add(self, data):
        off = len(self.blob)
        self.blob += data
        return off, len(data)


# --------------------------------------------------------------------------
# matplotlib startup
# --------------------------------------------------------------------------

def backend_order():
    if sys.platform == "darwin":
        return ["macosx", "qtagg", "tkagg"]
    return ["qtagg", "tkagg"]


def init_matplotlib():
    """Pick an interactive backend and import pyplot, or record why not.

    An explicit FEMMQT_PYBRIDGE_BACKEND wins -- that is both the preferences
    hook and the CI hook, which is why "agg" is honoured here although it is
    never auto-selected (an Agg worker draws nothing a user could look at).
    """
    global plt, _backend, _backend_error
    try:
        import matplotlib
    except Exception as e:
        _backend_error = "matplotlib is not installed (%s)" % e
        return
    try:
        import matplotlib.pyplot as pyplot
    except Exception as e:
        _backend_error = "matplotlib.pyplot did not import (%s)" % e
        return
    override = os.environ.get("FEMMQT_PYBRIDGE_BACKEND", "").strip()
    candidates = [override] if override else backend_order()
    errors = []
    for cand in candidates:
        try:
            # switch_backend, not use(): use() only records a preference, so a
            # qtagg with no PyQt installed "succeeds" here and then raises on
            # the first figure -- which is far too late to fall back to tkagg.
            pyplot.switch_backend(cand)
        except Exception as e:
            errors.append("%s: %s" % (cand, first_line(e)))
            continue
        plt = pyplot
        _backend = matplotlib.get_backend()
        plt.ion()
        _patch_show()
        return
    _backend_error = "no usable backend; tried " + "; ".join(errors)


def first_line(e):
    return str(e).splitlines()[0] if str(e) else type(e).__name__


def _patch_show():
    """Neuter show(block=True): in ion mode show never blocks anyway, but a
    pasted-from-CPython ``plt.show(block=True)`` would otherwise sit inside a
    backend event loop with an RPC half-answered."""
    real_show = plt.show
    warned = [False]

    def show(*args, **kwargs):
        if kwargs.pop("block", None) and not warned[0]:
            warned[0] = True
            log("plt.show(block=True) ignored: figures are interactive already")
        if args:
            args = tuple(a for a in args if a is not True)
        return real_show(*args, **kwargs)

    plt.show = show


def pump_gui():
    """Give open figure windows a slice of time.

    Deliberately not plt.pause(): that creates a figure when none exists and
    steals focus on some backends.  Exceptions are swallowed per manager because
    a window closing underneath us is normal, not an error.
    """
    if plt is None:
        return
    try:
        from matplotlib._pylab_helpers import Gcf
        managers = Gcf.get_all_fig_managers()
    except Exception:
        return
    for m in managers:
        try:
            m.canvas.flush_events()
        except Exception:
            pass


def any_figures_open():
    if plt is None:
        return False
    try:
        from matplotlib._pylab_helpers import Gcf
        return bool(Gcf.get_all_fig_managers())
    except Exception:
        return False


# --------------------------------------------------------------------------
# The worker
# --------------------------------------------------------------------------

_STOPITER = object()  # `next` ran out; not an error, a protocol signal
NOT_SCALAR = object()  # "this value has no by-value encoding"


class Raw:
    """Wraps an already-encoded value so dispatch passes it through untouched."""

    def __init__(self, value):
        self.value = value


class Worker:
    def __init__(self, token):
        self.token = token
        self.authed = False
        self.running = True
        self.handles = {}
        self.next_id = 1
        self.ns = None
        self.ops = {
            "hello": self.op_hello, "import": self.op_import,
            "getattr": self.op_getattr, "setattr": self.op_setattr,
            "call": self.op_call, "callmethod": self.op_callmethod,
            "getitem": self.op_getitem, "setitem": self.op_setitem,
            "delitem": self.op_delitem, "binop": self.op_binop,
            "unop": self.op_unop, "len": self.op_len, "repr": self.op_repr,
            "str": self.op_str, "dir": self.op_dir, "iter": self.op_iter,
            "next": self.op_next, "contains": self.op_contains,
            "truth": self.op_truth, "release": self.op_release,
            "clear": self.op_clear, "fetch": self.op_fetch,
            "eval": self.op_eval, "put": self.op_put,
            "shutdown": self.op_shutdown,
        }

    # -- handles ----------------------------------------------------------

    def alloc(self, obj):
        h = self.next_id
        self.next_id += 1
        self.handles[h] = obj
        return h

    def resolve(self, h):
        if not isinstance(h, int) or isinstance(h, bool):
            raise ProtocolFault("handle must be an integer, got %r" % (h,))
        try:
            return self.handles[h]
        except KeyError:
            raise ProtocolFault("unknown handle %d (released, or from a "
                                "previous worker session)" % h)

    def target(self, msg):
        if "h" not in msg:
            raise ProtocolFault("request is missing its handle")
        return self.resolve(msg["h"])

    # -- namespace --------------------------------------------------------

    def namespace(self):
        if self.ns is None:
            import builtins
            self.ns = {"__builtins__": builtins}
            if np is not None:
                self.ns["np"] = np
            if plt is not None:
                self.ns["plt"] = plt
        return self.ns

    # -- decoding (client -> worker) --------------------------------------

    def decode(self, v, blob):
        if isinstance(v, list):
            return [self.decode(x, blob) for x in v]
        if not isinstance(v, dict):
            return v
        if "__h__" in v:
            return self.resolve(v["__h__"])
        if "__nd__" in v:
            return self.decode_array(v["__nd__"], blob)
        if "__b__" in v:
            return self.slice_blob(v["__b__"], blob)
        if "__f__" in v:
            return decode_special_float(v["__f__"])
        if "__c__" in v:
            parts = v["__c__"]
            if not isinstance(parts, list) or len(parts) != 2:
                raise ProtocolFault("malformed complex %r" % (parts,))
            re, im = (self.decode(p, blob) for p in parts)
            if not isinstance(re, (int, float)) or not isinstance(im,
                                                                  (int, float)):
                raise ProtocolFault("complex parts must be numbers, got %r"
                                    % (parts,))
            return complex(re, im)
        if "__t__" in v:
            return tuple(self.decode(x, blob) for x in v["__t__"])
        return {k: self.decode(x, blob) for k, x in v.items()}

    def slice_blob(self, spec, blob):
        try:
            off = int(spec["off"])
            n = int(spec["len"])
        except Exception:
            raise ProtocolFault("malformed binary reference %r" % (spec,))
        if off < 0 or n < 0 or off + n > len(blob):
            raise ProtocolFault("binary reference [%d,%d) runs past the %d "
                                "byte region" % (off, off + n, len(blob)))
        return blob[off:off + n]

    def decode_array(self, spec, blob):
        if np is None:
            raise ProtocolFault("numpy is not available in this worker, so "
                                "array arguments cannot be decoded")
        raw = self.slice_blob(spec, blob)
        name = spec.get("dtype")
        if name not in DTYPES:
            raise ProtocolFault("unsupported array dtype %r" % (name,))
        dt = np.dtype(DTYPES[name])
        shape = spec.get("shape")
        if not isinstance(shape, list) or not all(
                isinstance(d, int) and d >= 0 for d in shape):
            raise ProtocolFault("malformed array shape %r" % (shape,))
        count = 1
        for d in shape:
            count *= d
        if count * dt.itemsize != len(raw):
            raise ProtocolFault("array shape %r of %s needs %d bytes, got %d"
                                % (shape, name, count * dt.itemsize, len(raw)))
        # Copy: frombuffer would alias (and keep alive) the whole frame, and
        # would hand matplotlib a read-only array.
        return np.frombuffer(raw, dtype=dt).reshape(shape).copy()

    # -- encoding (worker -> client) --------------------------------------

    def encode_scalar(self, v, out):
        """The encoded form of a by-value scalar, or NOT_SCALAR."""
        if v is None or isinstance(v, (bool, int, str)):
            return v
        if isinstance(v, float):
            return encode_float(v)
        if isinstance(v, complex):
            # femm-qt is a phasor program: a complex is an ordinary result
            # here, not an exotic one, and it travels by value like any scalar.
            return {"__c__": [encode_float(v.real), encode_float(v.imag)]}
        if np is not None and isinstance(v, np.generic):
            return self.encode_scalar(v.item(), out)
        if isinstance(v, (bytes, bytearray)):
            off, n = out.add(bytes(v))
            return {"__b__": {"off": off, "len": n}}
        return NOT_SCALAR

    def encode(self, v, out, budget=None):
        """The general result rule: scalars and short sequences by value, and
        everything else -- objects, dicts, arrays -- by handle.

        A sequence goes by value even when its *elements* cannot: they become
        handles inside it.  That is what makes ``fig, ax = plt.subplots()``
        unpack and ``ax.plot(...)`` return a one-element list of Line2D
        handles, which is the whole reason for the by-value rule.
        """
        if budget is None:
            budget = [BY_VALUE_ELEMS]
        s = self.encode_scalar(v, out)
        if s is not NOT_SCALAR:
            return s
        if (isinstance(v, (list, tuple)) and len(v) <= BY_VALUE_LEN
                and len(v) <= budget[0]):
            budget[0] -= len(v)
            items = [self.encode(x, out, budget) for x in v]
            return {"__t__": items} if isinstance(v, tuple) else items
        return self.handle_ref(v)

    def encode_strict(self, v, out, budget, accept=None):
        """As encode(), but for fetch: no length limit, and anything that
        cannot cross by value is an error rather than a handle."""
        s = self.encode_scalar(v, out)
        if s is not NOT_SCALAR:
            return s
        if np is not None and isinstance(v, np.ndarray):
            return self.encode_array(v, out, accept)
        if isinstance(v, (list, tuple)) and len(v) <= budget[0]:
            budget[0] -= len(v)
            items = [self.encode_strict(x, out, budget, accept) for x in v]
            return {"__t__": items} if isinstance(v, tuple) else items
        raise TypeError("cannot fetch a %s by value; index into it, or "
                        "convert it server-side" % type(v).__name__)

    def handle_ref(self, v):
        cls = type(v)
        name = getattr(cls, "__module__", "") + "." + getattr(
            cls, "__qualname__", getattr(cls, "__name__", "?"))
        return {"__h__": self.alloc(v), "cls": name.lstrip(".")}

    def encode_array(self, a, out, accept=None):
        """ndarray -> binary region reference, dtype preserved where possible.

        `accept` is the client's list of dtypes it can actually hold. ulab has
        only f8/c16/i1/i2/u1/u2 -- no int32, int64 or float32 -- so without
        this a perfectly ordinary `numpy.arange(10)` (i4 or i8) would arrive as
        something the client cannot construct. Narrowing here means numpy does
        the conversion at C speed instead of the client unpacking element by
        element.
        """
        a = np.ascontiguousarray(a)
        if a.dtype.kind == "c":
            # One complex width on the wire (see DTYPES): complex64 widens,
            # complex128 passes through, and neither loses anything.  Casting
            # complex to f8 -- the old "unknown dtype" fallback -- would have
            # dropped the imaginary part silently, which for a phasor program
            # is the worst outcome available.
            a = a.astype("<c16")
            key = "c16"
            if accept is not None and key not in accept:
                raise TypeError("this client cannot hold complex arrays; take "
                                ".real / .imag server-side first")
        else:
            if a.dtype.kind == "b":
                a = a.astype("<u1")
            key = a.dtype.kind + str(a.dtype.itemsize)
            if key not in DTYPES or a.dtype.byteorder == ">":
                a = a.astype("<f8")
                key = "f8"
            if accept is not None and key not in accept:
                a = a.astype("<f8")   # f8 is required of every client
                key = "f8"
        if a.nbytes > MAX_FETCH:
            raise ValueError("array of %d bytes exceeds the %d byte fetch "
                             "limit" % (a.nbytes, MAX_FETCH))
        off, n = out.add(a.tobytes())
        return {"__nd__": {"dtype": key, "shape": list(a.shape),
                           "off": off, "len": n}}

    def encode_fetch(self, v, out, accept=None):
        """fetch overrides the handle rule: force the value across the wire."""
        if (np is not None and isinstance(v, (list, tuple))
                and len(v) >= FETCH_ARRAY_MIN):
            # A long numeric list is bulk data whatever its Python type; send
            # it the same way an array would go, which is what the client's
            # own >= 256 element rule does in the other direction.  A list of
            # complex is just as plausible here as a list of float -- it is
            # what a loop over a frequency sweep produces.
            bulk, has_complex = scan_numeric(v)
            if bulk:
                return self.encode_array(
                    np.asarray(v, dtype="<c16" if has_complex else "<f8"),
                    out, accept)
        return self.encode_strict(v, out, [FETCH_ELEMS], accept)

    # -- ops --------------------------------------------------------------

    def op_hello(self, msg, blob, out):
        if msg.get("protocol") != PROTOCOL:
            raise ProtocolFault("protocol version %r is not supported (this "
                                "worker speaks %d)" % (msg.get("protocol"),
                                                       PROTOCOL))
        if self.token and msg.get("token") != self.token:
            raise ProtocolFault("bad authentication token")
        self.authed = True
        # Protocol metadata, not an object graph: returned as a literal JSON
        # object rather than through the value encoder (which would hand back a
        # handle for any dict).
        return Raw({
            "worker": WORKER_VERSION,
            "protocol": PROTOCOL,
            "pid": os.getpid(),
            "executable": sys.executable,
            "python": sys.version,
            "python_version": list(sys.version_info[:3]),
            "numpy": getattr(np, "__version__", None),
            "matplotlib": matplotlib_version(),
            "backend": _backend,
            "backend_error": _backend_error,
        })

    def op_import(self, msg, blob, out):
        name = msg.get("name")
        if not isinstance(name, str) or not name:
            raise ProtocolFault("import needs a module name")
        return importlib.import_module(name)

    def op_getattr(self, msg, blob, out):
        return getattr(self.target(msg), msg["name"])

    def op_setattr(self, msg, blob, out):
        setattr(self.target(msg), msg["name"], self.decode(msg["value"], blob))
        return None

    def op_call(self, msg, blob, out):
        args, kwargs = self.call_args(msg, blob)
        return self.target(msg)(*args, **kwargs)

    def op_callmethod(self, msg, blob, out):
        args, kwargs = self.call_args(msg, blob)
        return getattr(self.target(msg), msg["name"])(*args, **kwargs)

    def call_args(self, msg, blob):
        args = msg.get("args") or []
        kwargs = msg.get("kwargs") or {}
        if not isinstance(args, list) or not isinstance(kwargs, dict):
            raise ProtocolFault("args must be a list and kwargs an object")
        for k in kwargs:
            if not isinstance(k, str):
                raise ProtocolFault("keyword names must be strings")
        return ([self.decode(a, blob) for a in args],
                {k: self.decode(v, blob) for k, v in kwargs.items()})

    def op_getitem(self, msg, blob, out):
        return self.target(msg)[self.decode(msg["key"], blob)]

    def op_setitem(self, msg, blob, out):
        self.target(msg)[self.decode(msg["key"], blob)] = self.decode(
            msg["value"], blob)
        return None

    def op_delitem(self, msg, blob, out):
        del self.target(msg)[self.decode(msg["key"], blob)]
        return None

    def op_binop(self, msg, blob, out):
        # The operator name rides in "operator", not "op": "op" already names
        # the RPC itself.
        fn = BINOPS.get(msg.get("operator"))
        if fn is None:
            raise ProtocolFault("unknown binary operator %r"
                                % (msg.get("operator"),))
        return fn(self.decode(msg["a"], blob), self.decode(msg["b"], blob))

    def op_unop(self, msg, blob, out):
        fn = UNOPS.get(msg.get("operator"))
        if fn is None:
            raise ProtocolFault("unknown unary operator %r"
                                % (msg.get("operator"),))
        return fn(self.decode(msg["a"], blob))

    def op_len(self, msg, blob, out):
        return len(self.target(msg))

    def op_repr(self, msg, blob, out):
        return truncate(repr(self.target(msg)))

    def op_str(self, msg, blob, out):
        return truncate(str(self.target(msg)))

    def op_dir(self, msg, blob, out):
        # Protocol metadata again: dir() of a module is hundreds of names, so
        # the by-value length rule must not turn it into a handle.
        return Raw([str(n) for n in dir(self.target(msg))])

    def op_iter(self, msg, blob, out):
        return iter(self.target(msg))

    def op_next(self, msg, blob, out):
        try:
            return next(self.target(msg))
        except StopIteration:
            return _STOPITER

    def op_contains(self, msg, blob, out):
        return self.decode(msg["item"], blob) in self.target(msg)

    def op_truth(self, msg, blob, out):
        return bool(self.target(msg))

    def op_release(self, msg, blob, out):
        for h in msg.get("handles") or []:
            self.handles.pop(h, None)
        return None

    def op_clear(self, msg, blob, out):
        # Figures survive: pyplot's Gcf holds them independently of this table.
        self.handles.clear()
        self.ns = None
        return None

    def op_fetch(self, msg, blob, out):
        # "accept" is optional: a client that omits it takes whatever dtype the
        # data already has, which is what a numpy-side client wants.
        accept = msg.get("accept")
        if accept is not None and not isinstance(accept, list):
            raise ProtocolFault("accept must be a list of dtype names")
        return Raw(self.encode_fetch(self.target(msg), out, accept))

    def op_eval(self, msg, blob, out):
        code = msg.get("code")
        if not isinstance(code, str):
            raise ProtocolFault("eval needs source text")
        mode = msg.get("mode", "eval")
        ns = self.namespace()
        if mode == "eval":
            return eval(compile(code, "<pybridge>", "eval"), ns, ns)
        if mode == "exec":
            exec(compile(code, "<pybridge>", "exec"), ns, ns)
            return None
        raise ProtocolFault("mode must be 'eval' or 'exec', not %r" % (mode,))

    def op_put(self, msg, blob, out):
        name = msg.get("name")
        if not isinstance(name, str) or not name.isidentifier():
            raise ProtocolFault("put needs a valid identifier, got %r" % (name,))
        self.namespace()[name] = self.decode(msg["value"], blob)
        return None

    def op_shutdown(self, msg, blob, out):
        self.running = False
        return None

    # -- dispatch ---------------------------------------------------------

    def dispatch(self, msg, blob):
        rid = msg.get("id")
        out = Out()
        try:
            op = msg.get("op")
            if not isinstance(op, str):
                raise ProtocolFault("request has no op")
            fn = self.ops.get(op)
            if fn is None:
                raise ProtocolFault("unknown op %r" % (op,))
            if op != "hello" and not self.authed:
                raise ProtocolFault("%s before a successful hello" % op)
            r = fn(msg, blob, out)
            if r is _STOPITER:
                v = {"t": "stopiter"}
            elif isinstance(r, Raw):
                v = r.value
            else:
                v = self.encode(r, out)
            return {"id": rid, "ok": True, "v": v}, out.blob
        except ProtocolFault as e:
            return error_response(rid, "BridgeProtocolError", str(e)), b""
        except KeyError as e:
            # A missing request field reads as a KeyError from the op handlers;
            # that is our bug or the client's, not the user's Python failing.
            if isinstance(e.args[0], str) and e.args[0] in (
                    "name", "value", "key", "item", "a", "b"):
                return error_response(rid, "BridgeProtocolError",
                                      "request is missing field %r" % e.args[0]
                                      ), b""
            return error_response(rid, "KeyError", str(e)), b""
        except BaseException as e:  # user code may raise anything at all
            return error_response(rid, type(e).__name__, str(e)), b""


def error_response(rid, etype, msg):
    return {"id": rid, "ok": False,
            "err": {"etype": etype, "msg": msg, "tb": traceback.format_exc()}}


def scan_numeric(seq):
    """(every element is bulk-encodable, any of them complex) in one pass."""
    has_complex = False
    for x in seq:
        if isinstance(x, float):  # much the commonest case: test it first
            continue
        if isinstance(x, bool) or not isinstance(x, (int, complex)):
            return False, False
        if isinstance(x, complex):
            has_complex = True
    return True, has_complex


def truncate(s):
    return s if len(s) <= TEXT_LIMIT else s[:TEXT_LIMIT] + "...[truncated]"


def matplotlib_version():
    m = sys.modules.get("matplotlib")
    return getattr(m, "__version__", None) if m else None


def encode_float(f):
    """JSON has no nan/inf and MicroPython's parser has no extensions, so the
    non-finite floats matplotlib hands out (axis limits, masked data) travel
    tagged instead."""
    if f != f:
        return {"__f__": "nan"}
    if f == math.inf:
        return {"__f__": "inf"}
    if f == -math.inf:
        return {"__f__": "-inf"}
    return f


def decode_special_float(tag):
    if tag == "nan":
        return math.nan
    if tag == "inf":
        return math.inf
    if tag == "-inf":
        return -math.inf
    raise ProtocolFault("unknown float tag %r" % (tag,))


# --------------------------------------------------------------------------
# Main loop
# --------------------------------------------------------------------------

def send_frame(sock, frame):
    """Blocking send even though the socket is non-blocking for reads: partial
    sends on a half-written frame would desynchronise the stream."""
    sock.setblocking(True)
    try:
        sock.sendall(frame)
    finally:
        sock.setblocking(False)


def serve(sock, worker):
    buf = FrameBuffer()
    sock.setblocking(False)
    while worker.running:
        timeout = 0.02 if any_figures_open() else 0.25
        try:
            r, _, _ = select.select([sock], [], [], timeout)
        except (OSError, ValueError):
            return 0
        if r:
            try:
                data = sock.recv(1 << 20)
            except (BlockingIOError, InterruptedError):
                data = None  # spurious readiness; go round again
            except OSError as e:
                log("connection lost: %s" % e)
                return 0
            if data is None:
                pump_gui()
                continue
            if not data:
                return 0  # femm-qt closed or died; clean exit
            buf.feed(data)
            try:
                for kind, payload in buf.frames():
                    msg, blob = parse_frame(kind, payload)
                    resp, rblob = worker.dispatch(msg, blob)
                    try:
                        frame = build_frame(resp, rblob)
                    except ProtocolFault as e:
                        frame = build_frame(error_response(
                            msg.get("id"), "BridgeProtocolError", str(e)))
                    send_frame(sock, frame)
                    if not worker.running:
                        break
            except FramingError as e:
                log("framing error, exiting: %s" % e)
                return 2
            except OSError as e:
                log("connection lost while replying: %s" % e)
                return 0
        pump_gui()
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--connect", required=True, metavar="HOST:PORT",
                    help="femm-qt's listening address")
    ap.add_argument("--connect-timeout", type=float, default=10.0)
    args = ap.parse_args(argv)

    host, _, port = args.connect.rpartition(":")
    try:
        addr = (host or "127.0.0.1", int(port))
    except ValueError:
        log("--connect wants HOST:PORT, got %r" % args.connect)
        return 3

    token = os.environ.get("FEMMQT_PYBRIDGE_TOKEN", "")

    global np
    try:
        import numpy
        np = numpy
    except Exception:
        np = None

    try:
        sock = socket.create_connection(addr, timeout=args.connect_timeout)
    except OSError as e:
        log("could not connect to %s:%d: %s" % (addr[0], addr[1], e))
        return 3
    sock.settimeout(None)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    # Backend selection happens after connecting so a wrong port fails fast,
    # and before the greeting so `hello` can report what was actually chosen.
    init_matplotlib()
    if _backend_error:
        log(_backend_error)

    worker = Worker(token)
    try:
        # Unsolicited greeting: femm-qt validates this token before it lets
        # MicroPython near the connection, which is what stops another local
        # process from answering on the worker's behalf.  `hello` then runs the
        # same check in the other direction and carries the version report.
        send_frame(sock, build_frame({"t": "greeting", "protocol": PROTOCOL,
                                      "token": token, "pid": os.getpid()}))
        rc = serve(sock, worker)
    finally:
        if plt is not None:
            try:
                plt.close("all")
            except Exception:
                pass
        try:
            sock.close()
        except OSError:
            pass
    return rc


if __name__ == "__main__":
    sys.exit(main())
