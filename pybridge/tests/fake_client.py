# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
"""A CPython implementation of the PyBridge client, standing in for femm-qt.

Two jobs:

  * it is the whole test harness for the worker -- listen, spawn, handshake,
    drive -- so the protocol can be developed and debugged with no femm-qt
    build in sight (Milestone 1);
  * it is the reference the MicroPython client (``pybridge.py``, Milestone 3)
    is ported from, and the thing to diff against when the ported one
    misbehaves.

So it is written the way pybridge.py will be: same framing, same codec rules,
same Proxy shape, and deliberately no ``__del__``-driven handle release, since
MicroPython finalisers are not something the design leans on.  Where CPython
would allow something MicroPython can't, this file does it the MicroPython way.

Usage:

    from fake_client import Bridge
    with Bridge() as br:
        plt = br.import_module("matplotlib.pyplot")
        fig, ax = plt.subplots()
        ax.plot([0, 1, 2], [0, 1, 4], "r-")
"""

import array
import json
import math
import os
import secrets
import socket
import struct
import subprocess
import sys

try:
    import numpy
except ImportError:  # the client codec works without it; fetch() then returns lists
    numpy = None

KIND_JSON = 0x4A
KIND_BIN = 0x42
PROTOCOL = 1
MAX_FRAME = 256 * 1024 * 1024
BULK_MIN = 256  # a numeric list this long takes the binary path
RESERVED = ("__h__", "__nd__", "__t__", "__b__", "__f__", "__c__")

WORKER = os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), "worker", "femmqt_pybridge_worker.py")

# array module typecode -> protocol dtype.  ulab reports dtype as the integer
# ord() of these same typecodes, which is why the MicroPython port maps
# ord('d') -> "f8" and so on; keep the two tables in step.
#
# ulab's complex is ord('c') == 99, two mp_float_t per element == 16 bytes,
# laid out exactly like numpy complex128 -- so it maps to "c16" and crosses
# with no conversion.  Two ulab specifics the M3 port must handle, verified
# against this build: memoryview() REFUSES a complex array ("bad typecode"),
# so the zero-copy path does not apply and .tobytes() is the only route in;
# np.frombuffer(buf, dtype=np.complex) works, and is the route back out.
TYPECODES = {"d": "f8", "f": "f4", "q": "i8", "l": "i4", "i": "i4",
             "h": "i2", "b": "i1", "B": "u1", "H": "u2", "I": "u4", "L": "u4",
             "Q": "u8"}
ULAB_DTYPES = {ord("d"): "f8", ord("f"): "f4", ord("i"): "i4",
               ord("h"): "i2", ord("b"): "i1", ord("B"): "u1",
               ord("c"): "c16"}
DTYPE_TO_TYPECODE = {"f8": "d", "f4": "f", "i8": "q", "i4": "i", "i2": "h",
                     "i1": "b", "u1": "B", "u2": "H", "u4": "I", "u8": "Q",
                     "c16": None}  # array.array has no complex typecode
NUMPY_DTYPES = {"f8": "<f8", "f4": "<f4", "i8": "<i8", "i4": "<i4",
                "i2": "<i2", "i1": "|i1", "u8": "<u8", "u4": "<u4",
                "u2": "<u2", "u1": "|u1", "c16": "<c16"}


class BridgeError(Exception):
    """Transport, protocol or lifecycle failure -- not an error from Python."""


class RemoteError(Exception):
    """An exception raised by the user's code inside the worker."""

    def __init__(self, etype, msg, tb=""):
        Exception.__init__(self, "%s: %s" % (etype, msg))
        self.etype = etype
        self.msg = msg
        self.tb = tb


# --------------------------------------------------------------------------
# Framing
# --------------------------------------------------------------------------

class FrameBuffer:
    def __init__(self):
        self._buf = bytearray()

    def feed(self, data):
        self._buf += data

    def frames(self):
        while True:
            if len(self._buf) < 4:
                return
            (n,) = struct.unpack_from("<I", self._buf, 0)
            if n < 1 or n > MAX_FRAME:
                raise BridgeError("bad frame length %d" % n)
            if len(self._buf) < 4 + n:
                return
            kind = self._buf[4]
            payload = bytes(self._buf[5:4 + n])
            del self._buf[:4 + n]
            yield kind, payload


def build_frame(msg, blob=b""):
    text = json.dumps(msg, allow_nan=False).encode("utf-8")
    if blob:
        payload = struct.pack("<I", len(text)) + text + bytes(blob)
        kind = KIND_BIN
    else:
        payload = text
        kind = KIND_JSON
    body = bytes([kind]) + payload
    return struct.pack("<I", len(body)) + body


def parse_frame(kind, payload):
    if kind == KIND_JSON:
        text, blob = payload, b""
    elif kind == KIND_BIN:
        (hlen,) = struct.unpack_from("<I", payload, 0)
        text, blob = payload[4:4 + hlen], payload[4 + hlen:]
    else:
        raise BridgeError("unknown frame kind 0x%02X" % kind)
    return json.loads(text.decode("utf-8")), blob


# --------------------------------------------------------------------------
# Proxy
# --------------------------------------------------------------------------

class Proxy:
    """Stands in for an object living in the worker.

    Internal state goes on the instance through ``object.__setattr__`` -- the
    CPython idiom, and available in femm-qt's MicroPython since the
    MICROPY_PY_DELATTR_SETATTR flag landed (M0).  Every other attribute
    assignment forwards, because a silently-dead ``ax.title.set_size(...)``
    equivalent is the worst failure mode this design could have.
    """

    def __init__(self, bridge, h, cls, gen):
        object.__setattr__(self, "_bridge", bridge)
        object.__setattr__(self, "_h", h)
        object.__setattr__(self, "_cls", cls)
        object.__setattr__(self, "_gen", gen)

    # -- attributes -------------------------------------------------------

    def __getattr__(self, name):
        if name.startswith("__") and name.endswith("__"):
            # Never fabricate dunders we did not define: letting __iter__ or
            # __len__ resolve through here would make every proxy claim to be
            # iterable and sized.
            raise AttributeError(name)
        return self._bridge.request("getattr", h=self._h, name=name)

    def __setattr__(self, name, value):
        (v,), blob = self._bridge.enc(value)
        self._bridge.request("setattr", h=self._h, name=name, value=v,
                             _blob=blob)

    # -- calling ----------------------------------------------------------

    def __call__(self, *args, **kwargs):
        return self._bridge.call(self, *args, **kwargs)

    # -- containers -------------------------------------------------------

    def __getitem__(self, key):
        (k,), blob = self._bridge.enc(key)
        return self._bridge.request("getitem", h=self._h, key=k, _blob=blob)

    def __setitem__(self, key, value):
        (k, v), blob = self._bridge.enc(key, value)
        self._bridge.request("setitem", h=self._h, key=k, value=v, _blob=blob)

    def __delitem__(self, key):
        (k,), blob = self._bridge.enc(key)
        self._bridge.request("delitem", h=self._h, key=k, _blob=blob)

    def __len__(self):
        return self._bridge.request("len", h=self._h)

    def __contains__(self, item):
        (v,), blob = self._bridge.enc(item)
        return self._bridge.request("contains", h=self._h, item=v, _blob=blob)

    def __bool__(self):
        return self._bridge.request("truth", h=self._h)

    def __iter__(self):
        return self._bridge.request("iter", h=self._h)

    def __next__(self):
        return self._bridge.next(self)

    def __enter__(self):
        return self._bridge.request("callmethod", h=self._h, name="__enter__",
                                    args=[], kwargs={})

    def __exit__(self, et, ev, tb):
        return self._bridge.request("callmethod", h=self._h, name="__exit__",
                                    args=[None, None, None], kwargs={})

    # -- text -------------------------------------------------------------

    def __repr__(self):
        try:
            return self._bridge.request("repr", h=self._h)
        except Exception:
            return "<py.%s #%d>" % (self._cls, self._h)

    def __str__(self):
        try:
            return self._bridge.request("str", h=self._h)
        except Exception:
            return self.__repr__()

    # -- operators --------------------------------------------------------

    def _bin(self, name, a, b):
        (ea, eb), blob = self._bridge.enc(a, b)
        return self._bridge.request("binop", operator=name, a=ea, b=eb,
                                    _blob=blob)

    def _un(self, name):
        (a,), blob = self._bridge.enc(self)
        return self._bridge.request("unop", operator=name, a=a, _blob=blob)

    __hash__ = object.__hash__  # defining __eq__ would otherwise unhash us


def _binop(name):
    def fwd(self, other):
        return self._bin(name, self, other)

    return fwd


def _rbinop(name):
    def rev(self, other):
        return self._bin(name, other, self)

    return rev


def _unop(name):
    def fwd(self):
        return self._un(name)

    return fwd


for _n, _dunder in (("add", "add"), ("sub", "sub"), ("mul", "mul"),
                    ("truediv", "truediv"), ("floordiv", "floordiv"),
                    ("mod", "mod"), ("pow", "pow"), ("matmul", "matmul"),
                    ("and_", "and"), ("or_", "or"), ("xor", "xor"),
                    ("lshift", "lshift"), ("rshift", "rshift")):
    setattr(Proxy, "__%s__" % _dunder, _binop(_n))
    setattr(Proxy, "__r%s__" % _dunder, _rbinop(_n))
for _n, _dunder in (("eq", "eq"), ("ne", "ne"), ("lt", "lt"), ("le", "le"),
                    ("gt", "gt"), ("ge", "ge")):
    setattr(Proxy, "__%s__" % _dunder, _binop(_n))
for _n, _dunder in (("neg", "neg"), ("pos", "pos"), ("abs", "abs"),
                    ("invert", "invert")):
    setattr(Proxy, "__%s__" % _dunder, _unop(_n))


# --------------------------------------------------------------------------
# Bridge
# --------------------------------------------------------------------------

class Bridge:
    """Listens, spawns the worker, and issues round trips.

    femm-qt is the server and the worker connects back to it; this mirrors that
    so the handshake under test is the real one.
    """

    def __init__(self, python=None, worker=None, backend="agg", token=None,
                 env=None, timeout=30.0, extra_args=(), auto_handshake=True):
        self.python = python or sys.executable
        self.worker = worker or WORKER
        self.backend = backend
        self.token = token if token is not None else secrets.token_hex(16)
        self.timeout = timeout
        self.extra_args = list(extra_args)
        self.auto_handshake = auto_handshake
        self.env_extra = dict(env or {})
        self.proc = None
        self.sock = None
        self.gen = 1
        self._id = 0
        self._buf = FrameBuffer()
        self._pending_release = []
        self.hello = None
        self.greeting = None

    # -- lifecycle --------------------------------------------------------

    def start(self):
        srv = socket.socket()
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", 0))
        srv.listen(1)
        srv.settimeout(self.timeout)
        port = srv.getsockname()[1]

        env = dict(os.environ)
        env["FEMMQT_PYBRIDGE_TOKEN"] = self.token
        if self.backend:
            env["FEMMQT_PYBRIDGE_BACKEND"] = self.backend
        env.update(self.env_extra)
        cmd = [self.python, self.worker, "--connect", "127.0.0.1:%d" % port]
        cmd += self.extra_args
        self.proc = subprocess.Popen(cmd, env=env)
        try:
            self.sock, _ = srv.accept()
        except socket.timeout:
            self.kill()
            raise BridgeError("worker never connected")
        finally:
            srv.close()
        self.sock.settimeout(self.timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

        self.greeting, _ = self._read_message()
        if self.greeting.get("t") != "greeting":
            self.kill()
            raise BridgeError("first frame was not a greeting: %r"
                              % (self.greeting,))
        if self.auto_handshake:
            # femm-qt's side of the authentication: the connection is not
            # handed to MicroPython until the worker has proved it is ours.
            if self.greeting.get("token") != self.token:
                self.kill()
                raise BridgeError("worker presented the wrong token")
            self.hello = self.request("hello", token=self.token,
                                      protocol=PROTOCOL, client="fake_client")
        return self

    def __enter__(self):
        return self.start() if self.sock is None else self

    def __exit__(self, *exc):
        self.close()

    def close(self, timeout=5.0):
        if self.sock is not None:
            try:
                self.request("shutdown")
            except Exception:
                pass
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None
        if self.proc is not None:
            try:
                self.proc.wait(timeout)
            except subprocess.TimeoutExpired:
                self.kill()
        return self.proc.returncode if self.proc else None

    def kill(self):
        if self.proc is not None and self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait(5)
        self.gen += 1

    # -- transport --------------------------------------------------------

    def _send(self, msg, blob=b""):
        self.sock.sendall(build_frame(msg, blob))

    def _read_message(self):
        """Next complete frame, as (message, binary region)."""
        while True:
            for kind, payload in self._buf.frames():
                return parse_frame(kind, payload)
            try:
                data = self.sock.recv(1 << 20)
            except socket.timeout:
                raise BridgeError("worker did not answer within %gs"
                                  % self.timeout)
            if not data:
                raise BridgeError("python worker exited unexpectedly")
            self._buf.feed(data)

    def rpc(self, msg, blob=b""):
        """One round trip.  Returns (response message, binary region)."""
        if self.sock is None:
            raise BridgeError("bridge is not started")
        self._id += 1
        msg["id"] = self._id
        try:
            self._send(msg, blob)
        except OSError as e:
            raise BridgeError("python worker exited unexpectedly (%s)" % e)
        resp, rblob = self._read_message()
        if resp.get("id") != self._id:
            raise BridgeError("response id %r does not match request %d"
                              % (resp.get("id"), self._id))
        return resp, rblob

    def request(self, op, **fields):
        """One round trip, decoded, with remote errors raised."""
        self._flush_releases()
        return self._request(op, **fields)

    def _request(self, op, **fields):
        blob = fields.pop("_blob", b"")
        fields["op"] = op
        resp, rblob = self.rpc(fields, blob)
        if not resp.get("ok"):
            err = resp.get("err") or {}
            raise RemoteError(err.get("etype", "?"), err.get("msg", ""),
                              err.get("tb", ""))
        return self.decode(resp.get("v"), rblob)

    # -- encoding (client -> worker) --------------------------------------

    def enc(self, *values):
        """Encode values together: (encoded list, shared binary region)."""
        out = bytearray()
        return [self.encoder(v, out) for v in values], bytes(out)

    def encoder(self, v, out):
        """Encode one value, appending any bulk data to ``out``."""
        if isinstance(v, Proxy):
            if v._gen != self.gen:
                raise BridgeError("stale handle from a previous worker session")
            return {"__h__": v._h}
        if v is None or isinstance(v, (bool, int, str)):
            return v
        if isinstance(v, float):
            return encode_float(v)
        if isinstance(v, complex):
            return {"__c__": [encode_float(v.real), encode_float(v.imag)]}
        if isinstance(v, (bytes, bytearray)):
            return self._blob_ref(bytes(v), out)
        if isinstance(v, array.array):
            code = TYPECODES.get(v.typecode)
            if code is None:
                raise BridgeError("array typecode %r has no protocol dtype"
                                  % v.typecode)
            return self._array_ref(memoryview(v).cast("B").tobytes(), code,
                                   [len(v)], out)
        if numpy is not None and isinstance(v, numpy.ndarray):
            return self._numpy_ref(v, out)
        if numpy is not None and isinstance(v, numpy.generic):
            return self.encoder(v.item(), out)
        if isinstance(v, (list, tuple)):
            bulk, has_complex = ((False, False) if len(v) < BULK_MIN
                                 else _scan_numeric(v))
            if bulk and has_complex:
                # A long list of complex is exactly what a frequency sweep
                # produces, so it takes the binary path like any other bulk
                # numeric data.  ulab builds the same c16 buffer natively.
                buf = array.array("d")
                for x in v:
                    z = complex(x)
                    buf.append(z.real)
                    buf.append(z.imag)
                return self._array_ref(buf.tobytes(), "c16", [len(v)], out)
            if bulk:
                return self._array_ref(
                    array.array("d", [float(x) for x in v]).tobytes(),
                    "f8", [len(v)], out)
            return [self.encoder(x, out) for x in v]
        if isinstance(v, dict):
            for k in v:
                if not isinstance(k, str):
                    raise BridgeError("dict keys must be strings, got %r"
                                      % (k,))
                if k in RESERVED:
                    raise BridgeError(
                        "dict key %r collides with a protocol tag; build the "
                        "object server-side with pybridge.put() instead" % k)
            return {k: self.encoder(x, out) for k, x in v.items()}
        raise BridgeError("cannot send a %s across the bridge"
                          % type(v).__name__)

    def _blob_ref(self, data, out):
        off = len(out)
        out += data
        return {"__b__": {"off": off, "len": len(data)}}

    def _array_ref(self, data, dtype, shape, out):
        off = len(out)
        out += data
        return {"__nd__": {"dtype": dtype, "shape": shape, "off": off,
                           "len": len(data)}}

    def _numpy_ref(self, a, out):
        a = numpy.ascontiguousarray(a)
        if a.dtype.kind == "c":
            a = a.astype("<c16")  # never fall through to the f8 cast below
            key = "c16"
        else:
            key = a.dtype.kind + str(a.dtype.itemsize)
            if key not in NUMPY_DTYPES or a.dtype.byteorder == ">":
                a = a.astype("<f8")
                key = "f8"
        return self._array_ref(a.tobytes(), key, list(a.shape), out)

    def call(self, target, *args, **kwargs):
        keys = list(kwargs)
        vals, blob = self.enc(*(list(args) + [kwargs[k] for k in keys]))
        self._flush_releases()
        return self._request("call", h=target._h, args=vals[:len(args)],
                             kwargs=dict(zip(keys, vals[len(args):])),
                             _blob=blob)

    def callmethod(self, target, name, *args, **kwargs):
        keys = list(kwargs)
        vals, blob = self.enc(*(list(args) + [kwargs[k] for k in keys]))
        self._flush_releases()
        return self._request("callmethod", h=target._h, name=name,
                             args=vals[:len(args)],
                             kwargs=dict(zip(keys, vals[len(args):])),
                             _blob=blob)

    # -- decoding (worker -> client) --------------------------------------

    def decode(self, v, blob):
        if isinstance(v, list):
            return [self.decode(x, blob) for x in v]
        if not isinstance(v, dict):
            return v
        if "__h__" in v:
            return Proxy(self, v["__h__"], v.get("cls", "?"), self.gen)
        if "__t__" in v:
            return tuple(self.decode(x, blob) for x in v["__t__"])
        if "__b__" in v:
            s = v["__b__"]
            return blob[s["off"]:s["off"] + s["len"]]
        if "__f__" in v:
            return decode_float(v["__f__"])
        if "__c__" in v:
            re, im = (self.decode(p, blob) for p in v["__c__"])
            return complex(re, im)
        if "__nd__" in v:
            return self.decode_array(v["__nd__"], blob)
        return {k: self.decode(x, blob) for k, x in v.items()}

    def decode_array(self, spec, blob):
        raw = blob[spec["off"]:spec["off"] + spec["len"]]
        name = spec["dtype"]
        if name not in NUMPY_DTYPES:
            raise BridgeError("unsupported dtype %r" % (name,))
        if numpy is not None:
            return numpy.frombuffer(raw, dtype=NUMPY_DTYPES[name]).reshape(
                spec["shape"]).copy()
        # No numpy.  ulab decodes c16 natively with np.frombuffer; plain
        # array.array has no complex typecode, so this fallback builds the
        # complex values itself rather than pretending it cannot.
        if name == "c16":
            flat = array.array("d", raw)
            return [complex(flat[i], flat[i + 1])
                    for i in range(0, len(flat), 2)]
        return array.array(DTYPE_TO_TYPECODE[name], raw)

    # -- public API (mirrors pybridge.py) ---------------------------------

    def import_module(self, name):
        return self.request("import", name=name)

    def eval(self, code):
        return self.request("eval", code=code, mode="eval")

    def exec(self, code):
        return self.request("eval", code=code, mode="exec")

    def put(self, name, value):
        (v,), blob = self.enc(value)
        self._flush_releases()
        return self._request("put", name=name, value=v, _blob=blob)

    def fetch(self, proxy):
        return self.request("fetch", h=proxy._h)

    def open(self, path, mode="r", **kw):
        builtins = self.import_module("builtins")
        return self.callmethod(builtins, "open", path, mode, **kw)

    def dir(self, proxy):
        return self.request("dir", h=proxy._h)

    def next(self, proxy):
        resp, rblob = self.rpc({"op": "next", "h": proxy._h})
        if not resp.get("ok"):
            err = resp.get("err") or {}
            raise RemoteError(err.get("etype", "?"), err.get("msg", ""),
                              err.get("tb", ""))
        v = resp.get("v")
        if isinstance(v, dict) and v.get("t") == "stopiter":
            raise StopIteration
        return self.decode(v, rblob)

    def clear(self):
        return self.request("clear")

    # -- handle release ---------------------------------------------------
    #
    # No __del__ (MicroPython finalisers are not relied on), so releases are
    # explicit and batched: the worker-side table entry is the only thing that
    # accumulates, and it drops wholesale on clear() or restart.

    def release(self, *proxies):
        for p in proxies:
            if isinstance(p, Proxy) and p._gen == self.gen:
                self._pending_release.append(p._h)
        if len(self._pending_release) > 64:
            self._flush_releases()

    def _flush_releases(self):
        if not self._pending_release:
            return
        handles, self._pending_release = self._pending_release, []
        self._request("release", handles=handles)


def _scan_numeric(seq):
    """(every element is bulk-encodable, any of them complex) in one pass --
    a second scan just to ask about complex costs milliseconds at 1e5."""
    has_complex = False
    for x in seq:
        if isinstance(x, float):  # much the commonest case: test it first
            continue
        if isinstance(x, bool) or not isinstance(x, (int, complex)):
            return False, False
        if isinstance(x, complex):
            has_complex = True
    return True, has_complex


def encode_float(f):
    if f != f:
        return {"__f__": "nan"}
    if f == math.inf:
        return {"__f__": "inf"}
    if f == -math.inf:
        return {"__f__": "-inf"}
    return f


def decode_float(tag):
    return {"nan": math.nan, "inf": math.inf, "-inf": -math.inf}[tag]
