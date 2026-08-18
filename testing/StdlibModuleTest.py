# The standard-library modules built into the interpreter. Runs inside
# femm_headless:
#
#     build/bin/femm_headless -run testing/StdlibModuleTest.py
#
# Each of these is a config flag plus a source file copied by regen.sh, and the
# flags come in layers -- `re` without RE_SUB, or `random` without EXTRA_FUNCS,
# imports happily and then fails on the call a user actually makes. So this
# checks the calls, not the imports. It also pins the four FEMM patches, which
# a regen must re-apply and which nothing else would notice losing.

import binascii
import errno
import hashlib
import json
import random
import re
import time

fails = []


def check(label, got, want):
    if got != want:
        fails.append("%s: got %r, want %r" % (label, got, want))


# --- json ---
check("json.loads", json.loads('{"a": [1, 2.5, null, true]}'),
      {"a": [1, 2.5, None, True]})
check("json.dumps", json.dumps({"b": [1, 2]}), '{"b": [1, 2]}')

# --- re, including the pieces that need the extra flags ---
m = re.match(r"([A-Za-z]+)_(\d+)", "coil_42")
check("re.match groups", (m.group(1), m.group(2)), ("coil", "42"))
check("re.match .groups()", m.groups(), ("coil", "42"))
check("re.match span", m.span(1), (0, 4))
check("re.sub", re.sub(r"\s+", " ", "a   b    c"), "a b c")
check("re.search", re.search(r"\d+", "abc 123").group(0), "123")
# split() is a method on a compiled pattern here, not a module-level function
# as in CPython. Upstream's API, not something this port chose.
check("re.compile().split", re.compile(r",").split("a,b,c"), ["a", "b", "c"])

# --- time: local clock, 1970 epoch, float seconds, monotonic ticks ---
now = time.time()
check("time.time is a float", type(now) is float, True)
check("time.time is this century", 1.7e9 < now < 2.5e9, True)
lt = time.localtime()
check("localtime year", lt[0] >= 2026, True)
check("mktime round trip",
      time.localtime(time.mktime((2026, 8, 16, 13, 30, 0, 0, 0)))[:6],
      (2026, 8, 16, 13, 30, 0))
t0 = time.ticks_ms()
time.sleep(0.2)
slept = time.ticks_diff(time.ticks_ms(), t0)
check("sleep(0.2) waits >= 200 ms", slept >= 195, True)
check("sleep(0.2) does not overshoot wildly", slept < 600, True)

# --- random: seeded repeatably, and the extra functions exist ---
random.seed(7)
a = [random.random(), random.randint(1, 6), random.choice("abcdef"),
     random.uniform(0, 1), random.getrandbits(8)]
random.seed(7)
b = [random.random(), random.randint(1, 6), random.choice("abcdef"),
     random.uniform(0, 1), random.getrandbits(8)]
check("random is repeatable when seeded", a, b)
check("random.random in range", 0.0 <= a[0] < 1.0, True)

# --- binascii and hashlib (checked against CPython's answers) ---
check("hexlify", binascii.hexlify(b"FEMM"), b"46454d4d")
check("unhexlify", binascii.unhexlify(b"46454d4d"), b"FEMM")
check("b2a_base64", binascii.b2a_base64(b"FEMM").strip(), b"RkVNTQ==")
check("bytes.hex", b"FEMM".hex(), "46454d4d")
check("sha256", binascii.hexlify(hashlib.sha256(b"the quick brown fox").digest()),
      b"9ecb36561341d18eb65484e833efea61edc74b84cf5e6ae1b81c63533e25fc8f")

# --- errno ---
check("errno.ENOENT", errno.ENOENT, 2)

# --- the FEMM patches: a regen must re-apply them, and only this notices ---
# Patch 01 makes a complex formattable under a numeric type code at all
# (MicroPython raises); the output is CPython's own, since I* notation
# belongs to the TCP wire and nowhere else.
z = 3.0 + 4.0j
check("patch 01: %g on a complex", "%g" % z, "3+4j")
check("patch 01: negative imaginary", "%.3g" % (1.5 - 2.5j), "1.5-2.5j")
# Both formatting paths, not just the % operator: the format() side reached the
# complex code only after the numeric-branch test learned about complex.
check("patch 01: {:g}", "{:g}".format(z), "3+4j")
check("patch 01: {:.3g}", "{:.3g}".format(1.5 - 2.5j), "1.5-2.5j")
check("patch 01: {:f}", "{:f}".format(z), "3.000000+4.000000j")
check("patch 01: real-valued complex", "{:g}".format(3 + 0j), "3+0j")
# -1j is complex(-0.0, -1.0), and CPython keeps that signed zero:
# format(-1j, "g") is "-0-1j" while format(0-1j, "g") is "0-1j".
check("patch 01: unit imaginary", ("{:g}".format(1j), "{:g}".format(-1j)),
      ("0+1j", "-0-1j"))
check("patch 01: signed zero real", "{:g}".format(0 - 1j), "0-1j")
# Width and alignment apply to the whole rendered value, and a complex pads the
# same way a real does -- both right-aligned by default.
check("patch 01: default align", "{:14g}".format(z), "          3+4j")
check("patch 01: same as a real", "{:14g}".format(2.5), "           2.5")
check("patch 01: explicit left", "{:<14g}".format(z), "3+4j          ")
check("patch 01: centred", "{:^14g}".format(z), "     3+4j     ")
check("patch 02: complex ordering", z > 1.0, True)
check("patch 02: int(complex)", int(z), 3)
check("patch 03: numeric tower", ((2.5).real, (2.5).imag, (7).conjugate()), (2.5, 0, 7))
check("patch 04: exact integer powers", ((1j) ** 2, (1j) ** 3), (-1, -1j))

if fails:
    print("stdlib module check: %d FAILED" % len(fails))
    for msg in fails:
        print("   " + msg)
else:
    print("stdlib module check: json, re, time, random, binascii, hashlib, "
          "errno all behave; the four FEMM patches survived the regen")
