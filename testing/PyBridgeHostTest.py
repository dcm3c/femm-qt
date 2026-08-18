# The M2 acceptance test: femm-qt's own MicroPython driving the CPython
# worker, with no pybridge.py in between.
#
#     build/bin/femm_headless -run testing/PyBridgeHostTest.py
#
# Everything here is deliberately hand-built from bytes.  The point of M2 is
# the transport -- spawn, discovery, token handshake, framing, lifecycle, the
# nested-call guard -- and a client library would hide exactly the parts under
# test.  The real client arrives in M3.
#
# Skips cleanly (exit 0, "SKIP") when no usable Python is installed: the
# bridge is an optional capability and a machine without matplotlib must not
# fail the suite.
import _pybridge
import json
import struct

fails = []


def check(label, cond, detail=""):
    if not cond:
        fails.append(label + (": " + detail if detail else ""))


def frame(msg):
    body = b"J" + json.dumps(msg).encode()
    return struct.pack("<I", len(body)) + body


def unframe(buf):
    """(message, binary region) from one complete reply frame."""
    n = struct.unpack("<I", buf[:4])[0]
    check("frame length", len(buf) == 4 + n, "%d vs %d" % (len(buf), 4 + n))
    kind = buf[4:5]
    if kind == b"J":
        return json.loads(buf[5:].decode()), b""
    hlen = struct.unpack("<I", buf[5:9])[0]
    return json.loads(buf[9:9 + hlen].decode()), buf[9 + hlen:]


nid = [1]


def call(op, **fields):
    nid[0] += 1
    fields["op"] = op
    fields["id"] = nid[0]
    msg, blob = unframe(_pybridge.rpc(frame(fields)))
    check("id echo for " + op, msg.get("id") == nid[0])
    if not msg.get("ok"):
        raise ValueError("%s failed: %s" % (op, msg.get("err", {}).get("msg")))
    return msg.get("v"), blob


# --- startup ---------------------------------------------------------------

check("not running before first use", not _pybridge.running())
try:
    _pybridge.ensure_started()
except OSError as e:
    print("SKIP: no usable Python for the matplotlib bridge")
    print("  " + str(e).split("\n")[0])
    for a in _pybridge.attempts():
        print("  tried " + a)
    raise SystemExit(0)

check("running after ensure_started", _pybridge.running())
# info() is the whole hello response frame, so the report sits under "v" --
# the C++ side keeps it verbatim rather than parsing JSON properly.
info = json.loads(_pybridge.info())["v"]
check("protocol version", info.get("protocol") == 1)
check("backend reported", bool(info.get("backend")), str(info.get("backend")))
check("matplotlib reported", bool(info.get("matplotlib")))
check("interpreter recorded", bool(_pybridge.interpreter()))

# ensure_started is idempotent: a second call must not spawn a second worker.
gen = _pybridge.session()
_pybridge.ensure_started()
check("ensure_started is idempotent", _pybridge.session() == gen)

# --- the round trip the milestone asks for ---------------------------------

h, _ = call("import", name="math")
check("import returns a handle", isinstance(h, dict) and "__h__" in h,
      str(h))
v, _ = call("repr", h=h["__h__"])
check("repr of the math module", "module" in v, v)

v, _ = call("getattr", h=h["__h__"], name="pi")
check("math.pi by value", abs(v - 3.14159265358979) < 1e-12, str(v))

v, _ = call("callmethod", h=h["__h__"], name="hypot", args=[3, 4], kwargs={})
check("callmethod hypot", v == 5.0, str(v))

# A binary frame in the reply direction: fetch of a server-side array.
call("eval", code="import numpy", mode="exec")
arr, _ = call("eval", code="numpy.arange(4, dtype='f8')", mode="eval")
v, blob = call("fetch", h=arr["__h__"])
check("fetch is a binary frame", "__nd__" in v, str(v))
check("fetch payload size", len(blob) == 32, str(len(blob)))
check("fetch dtype", v["__nd__"]["dtype"] == "f8", str(v))
vals = struct.unpack("<4d", blob)
check("fetch contents", vals == (0.0, 1.0, 2.0, 3.0), str(vals))

# Complex, since it is the reason femm-qt cares (M1 follow-up).
v, _ = call("eval", code="3+4j", mode="eval")
check("complex by value", v == {"__c__": [3.0, 4.0]}, str(v))

# An error must arrive as an error, and must not poison the stream.
try:
    call("eval", code="1/0", mode="eval")
    check("remote error raises", False)
except ValueError as e:
    check("remote error text", "division by zero" in str(e), str(e))
v, _ = call("eval", code="2+2", mode="eval")
check("stream survives an error", v == 4, str(v))

# The in-flight flag must not leak: a stuck one would reject every later call.
# (The guard itself needs a real nesting to exercise, which needs the GUI's
# event pump to re-enter script execution mid-call -- see PyBridgeNestedTest.)
v, _ = call("eval", code="'still here'", mode="eval")
check("in-flight flag does not leak", v == "still here", str(v))

# --- lifecycle -------------------------------------------------------------

gen = _pybridge.session()
_pybridge.restart()
check("restart bumps the session", _pybridge.session() > gen)
check("restart stops the worker", not _pybridge.running())

# ...and the next use respawns transparently.
h, _ = call("import", name="math")
check("respawn on next use", isinstance(h, dict) and "__h__" in h)
check("running again", _pybridge.running())

# Handles from the previous worker must not resolve against the new one.
# (Handle 1 exists in both, so use a high id the fresh worker cannot have.)
try:
    call("repr", h=99999)
    check("stale handle rejected", False)
except ValueError as e:
    check("stale handle message", "handle" in str(e), str(e))

_pybridge.shutdown()
check("shutdown stops the worker", not _pybridge.running())
gen2 = _pybridge.session()
_pybridge.shutdown()
check("shutdown twice is harmless", _pybridge.session() == gen2)

# --- report ----------------------------------------------------------------

if fails:
    print("FAIL (%d)" % len(fails))
    for f in fails:
        print("  " + f)
    raise SystemExit(1)
print("PyBridgeHost OK  [%s, %s, backend %s]"
      % (info["python"].split()[0], "matplotlib " + info["matplotlib"],
         info["backend"]))
