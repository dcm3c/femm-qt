# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
"""End-to-end tests against a live worker process.  No femm-qt, no display.

    python3 test_worker_headless.py

The worker is spawned by fake_client exactly as femm-qt will spawn it (we
listen, it connects, it greets, we hello), with FEMMQT_PYBRIDGE_BACKEND=agg so
matplotlib runs headless.  Agg is never auto-selected -- it is honoured only as
an explicit override, which exists for precisely this.
"""

import os
import socket
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from fake_client import Bridge, BridgeError, Proxy, RemoteError  # noqa: E402

try:
    import numpy
except ImportError:
    numpy = None


class Live(unittest.TestCase):
    """One worker shared by the whole class; each test is independent of the
    others' handles."""

    @classmethod
    def setUpClass(cls):
        cls.br = Bridge().start()

    @classmethod
    def tearDownClass(cls):
        cls.br.close()

    # -- handshake --------------------------------------------------------

    def test_hello_reports_the_environment(self):
        h = self.br.hello
        self.assertEqual(h["protocol"], 1)
        self.assertEqual(h["backend"].lower(), "agg")
        self.assertIsNotNone(h["matplotlib"])
        self.assertGreaterEqual(tuple(h["python_version"]), (3, 9))

    # -- attributes and calls ---------------------------------------------

    def test_module_attribute_and_call(self):
        m = self.br.import_module("math")
        self.assertAlmostEqual(m.pi, 3.141592653589793)
        self.assertAlmostEqual(m.sqrt(2), 1.4142135623730951)
        self.assertAlmostEqual(m.hypot(3, 4), 5.0)

    def test_keyword_arguments(self):
        b = self.br.import_module("builtins")
        self.assertEqual(b.round(2.34567, ndigits=2), 2.35)

    def test_dotted_import(self):
        p = self.br.import_module("os.path")
        self.assertEqual(p.basename("/a/b/c.txt"), "c.txt")

    def test_setattr_forwards(self):
        self.br.exec("class T:\n    x = 1\nt = T()")
        t = self.br.eval("t")
        t.x = 42
        self.assertEqual(t.x, 42)
        self.assertEqual(self.br.eval("t.x"), 42)

    def test_missing_attribute_raises_attributeerror(self):
        m = self.br.import_module("math")
        with self.assertRaises(RemoteError) as cm:
            m.no_such_thing
        self.assertEqual(cm.exception.etype, "AttributeError")

    # -- containers -------------------------------------------------------

    def test_subscript_and_delete(self):
        d = self.br.eval("{'a': 1, 'b': 2}")
        self.assertEqual(d["a"], 1)
        d["c"] = 3
        self.assertEqual(len(d), 3)
        del d["a"]
        self.assertEqual(len(d), 2)
        self.assertIn("b", d)
        self.assertNotIn("a", d)

    def test_len_truth_repr_str(self):
        empty = self.br.eval("[]")
        full = self.br.eval("[1, 2, 3]")
        self.assertFalse(bool(empty))
        self.assertTrue(bool(full))
        self.assertEqual(len(full), 3)
        self.assertEqual(repr(full), "[1, 2, 3]")
        self.assertEqual(str(self.br.eval("'text'")), "text")

    def test_dir_is_not_length_capped(self):
        """dir() of a module is hundreds of names: it is protocol metadata,
        not a value subject to the 64-element by-value rule."""
        names = self.br.dir(self.br.import_module("math"))
        self.assertIsInstance(names, list)
        self.assertGreater(len(names), 64)
        self.assertIn("sqrt", names)

    def test_iteration_and_stopiteration(self):
        it = iter(self.br.eval("[10, 20, 30]"))
        self.assertEqual([next(it), next(it), next(it)], [10, 20, 30])
        with self.assertRaises(StopIteration):
            next(it)

    def test_for_loop_over_a_proxy(self):
        self.assertEqual(list(self.br.eval("range(5)")), [0, 1, 2, 3, 4])

    def test_short_sequences_come_back_by_value(self):
        self.assertEqual(self.br.eval("[1, 'a', None, 2.5]"),
                         [1, "a", None, 2.5])
        self.assertEqual(self.br.eval("(1, 2)"), (1, 2))

    def test_long_sequence_comes_back_as_a_handle(self):
        v = self.br.eval("list(range(65))")
        self.assertIsInstance(v, Proxy)
        self.assertEqual(len(v), 65)

    def test_dict_comes_back_as_a_handle(self):
        self.assertIsInstance(self.br.eval("{'a': 1}"), Proxy)

    # -- operators --------------------------------------------------------

    def test_binops_both_directions(self):
        n = self.br.eval("7")  # a plain int arrives by value, so use a proxy
        frac = self.br.import_module("fractions")
        half = frac.Fraction(1, 2)
        self.assertEqual(str(half + half), "1")
        self.assertEqual(str(2 * half), "1")
        self.assertEqual(str(half * 2), "1")
        self.assertEqual(str(1 - half), "1/2")
        self.assertEqual(str(-half), "-1/2")
        self.assertEqual(abs(-half), half)
        self.assertTrue(half < 1)
        self.assertTrue(half == self.br.eval("__import__('fractions')"
                                             ".Fraction(1, 2)"))
        self.assertEqual(n, 7)

    # -- errors -----------------------------------------------------------

    def test_remote_exception_carries_a_traceback(self):
        with self.assertRaises(RemoteError) as cm:
            self.br.eval("1/0")
        self.assertEqual(cm.exception.etype, "ZeroDivisionError")
        self.assertIn("division by zero", cm.exception.msg)
        self.assertIn("Traceback", cm.exception.tb)

    def test_unknown_op(self):
        with self.assertRaises(RemoteError) as cm:
            self.br.request("frobnicate")
        self.assertEqual(cm.exception.etype, "BridgeProtocolError")

    def test_unknown_handle(self):
        with self.assertRaises(RemoteError) as cm:
            self.br.request("repr", h=987654)
        self.assertEqual(cm.exception.etype, "BridgeProtocolError")
        self.assertIn("previous worker session", cm.exception.msg)

    def test_missing_field(self):
        with self.assertRaises(RemoteError) as cm:
            self.br.request("getattr", h=1)
        self.assertEqual(cm.exception.etype, "BridgeProtocolError")

    def test_the_stream_survives_errors(self):
        """Every failure above must leave the protocol usable."""
        self.assertEqual(self.br.import_module("math").floor(2.7), 2)

    @unittest.skipIf(numpy is None, "numpy required")
    def test_ambiguous_truth_surfaces_faithfully(self):
        a = self.br.eval("__import__('numpy').arange(5)")
        with self.assertRaises(RemoteError) as cm:
            bool(a)
        self.assertEqual(cm.exception.etype, "ValueError")

    # -- eval / put / fetch ------------------------------------------------

    def test_eval_namespace_persists(self):
        self.br.exec("scratch = 17")
        self.assertEqual(self.br.eval("scratch"), 17)

    def test_put_and_fetch_round_trip(self):
        self.br.put("vals", [float(i) for i in range(1000)])
        self.assertEqual(self.br.eval("len(vals)"), 1000)
        back = self.br.fetch(self.br.eval("vals"))
        self.assertEqual(len(back), 1000)
        self.assertAlmostEqual(float(back[999]), 999.0)

    def test_put_rejects_a_bad_name(self):
        with self.assertRaises(RemoteError):
            self.br.put("not an identifier", 1)

    @unittest.skipIf(numpy is None, "numpy required")
    def test_array_argument_takes_the_binary_path(self):
        a = numpy.linspace(0.0, 1.0, 5000)
        np_mod = self.br.import_module("numpy")
        total = self.br.call(np_mod.sum, a)
        self.assertAlmostEqual(total, float(a.sum()))

    @unittest.skipIf(numpy is None, "numpy required")
    def test_two_dimensional_array_survives(self):
        a = numpy.arange(12, dtype="<i4").reshape(3, 4)
        self.br.put("m", a)
        self.assertEqual(self.br.eval("m.shape"), (3, 4))
        self.assertEqual(self.br.eval("int(m.dtype.itemsize)"), 4)
        back = self.br.fetch(self.br.eval("m"))
        self.assertTrue(numpy.array_equal(back, a))

    @unittest.skipIf(numpy is None, "numpy required")
    def test_fetch_preserves_dtype(self):
        for dt in ("f8", "f4", "i4", "i8", "u1"):
            with self.subTest(dtype=dt):
                self.br.exec("import numpy")
                back = self.br.fetch(self.br.eval(
                    "numpy.arange(4, dtype='%s')" % dt))
                self.assertEqual(back.dtype.str.lstrip("<|="), dt)

    def test_non_finite_floats_survive_a_round_trip(self):
        import math
        self.br.put("odd", [math.inf, -math.inf])
        self.assertEqual(self.br.eval("odd[0]"), math.inf)
        nan = self.br.eval("float('nan')")
        self.assertNotEqual(nan, nan)

    # -- complex, which for femm-qt is an everyday scalar -----------------

    def test_complex_scalar_round_trip(self):
        z = self.br.eval("3+4j")
        self.assertEqual(z, 3 + 4j)
        self.assertIsInstance(z, complex)
        self.br.put("z", 1 - 2j)
        self.assertEqual(self.br.eval("z"), 1 - 2j)
        self.assertEqual(self.br.eval("abs(z)"), abs(1 - 2j))

    def test_complex_arithmetic_across_the_bridge(self):
        cmath = self.br.import_module("cmath")
        self.assertAlmostEqual(cmath.sqrt(-1).imag, 1.0)
        self.assertAlmostEqual(cmath.phase(1j), 1.5707963267948966)
        self.br.put("zs", [1 + 1j, 2 + 2j])
        self.assertEqual(self.br.eval("zs[0] * zs[1]"), (1 + 1j) * (2 + 2j))

    def test_complex_non_finite_parts(self):
        import math
        self.br.put("zinf", complex(math.inf, -math.inf))
        back = self.br.eval("zinf")
        self.assertEqual(back.real, math.inf)
        self.assertEqual(back.imag, -math.inf)

    @unittest.skipIf(numpy is None, "numpy required")
    def test_complex_array_round_trip(self):
        a = numpy.exp(1j * numpy.linspace(0.0, 6.0, 500))
        self.br.put("phasors", a)
        self.assertEqual(self.br.eval("str(phasors.dtype)"), "complex128")
        back = self.br.fetch(self.br.eval("phasors"))
        self.assertEqual(back.dtype, numpy.dtype("complex128"))
        self.assertTrue(numpy.allclose(back, a))
        # ...and the imaginary part is genuinely there, not a rounding ghost.
        self.assertGreater(numpy.abs(back.imag).max(), 0.9)

    @unittest.skipIf(numpy is None, "numpy required")
    def test_bulk_complex_list_takes_the_binary_path(self):
        """The shape a femm-qt frequency sweep actually has: a Python list of
        complex accumulated in a loop, handed straight to the bridge."""
        vals = [complex(i, -0.5 * i) for i in range(1000)]
        self.br.put("sweep", vals)
        self.assertEqual(self.br.eval("str(sweep.dtype)"), "complex128")
        self.assertEqual(self.br.eval("complex(sweep[10])"), 10 - 5j)
        mags = self.br.fetch(self.br.eval("__import__('numpy').abs(sweep)"))
        self.assertAlmostEqual(float(mags[10]), abs(10 - 5j))

    @unittest.skipIf(numpy is None, "numpy required")
    def test_plotting_a_phasor_sweep(self):
        """End to end in the shape a user would write it: complex results in,
        magnitude and phase plotted."""
        f = numpy.logspace(0, 4, 400)
        z = 1.0 / (1.0 + 1j * f / 100.0)
        plt = self.br.import_module("matplotlib.pyplot")
        fig, axes = self.br.call(plt.subplots, 2, 1)
        self.br.put("z", z)
        self.br.put("f", f)
        mag = self.br.eval("__import__('numpy').abs(z)")
        phase = self.br.eval("__import__('numpy').angle(z, deg=True)")
        fvals = self.br.eval("f")
        self.br.call(axes[0].loglog, fvals, mag)
        self.br.call(axes[1].semilogx, fvals, phase)
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "bode.png")
            self.br.call(fig.savefig, path)
            self.assertGreater(os.path.getsize(path), 5000)
        plt.close(fig)

    def test_bytes_round_trip(self):
        self.br.put("raw", b"\x00\x01\xfe\xff")
        self.assertEqual(self.br.eval("raw"), b"\x00\x01\xfe\xff")
        self.assertEqual(self.br.eval("len(raw)"), 4)

    # -- handles ----------------------------------------------------------

    def test_release_then_use_is_a_clean_error(self):
        p = self.br.eval("list(range(100))")
        self.br.release(p)
        self.br._flush_releases()
        with self.assertRaises(RemoteError) as cm:
            len(p)
        self.assertEqual(cm.exception.etype, "BridgeProtocolError")

    def test_clear_empties_the_table_and_the_namespace(self):
        br = Bridge().start()
        try:
            br.exec("gone = 1")
            p = br.eval("list(range(100))")
            br.clear()
            with self.assertRaises(RemoteError):
                len(p)
            with self.assertRaises(RemoteError) as cm:
                br.eval("gone")
            self.assertEqual(cm.exception.etype, "NameError")
            # ...but the worker is still usable, and reseeded.
            self.assertEqual(br.eval("2 + 2"), 4)
        finally:
            br.close()

    # -- matplotlib -------------------------------------------------------

    def test_the_documented_example(self):
        """The snippet at the top of the plan, verbatim in spirit."""
        plt = self.br.import_module("matplotlib.pyplot")
        fig, ax = plt.subplots()
        self.assertIsInstance(fig, Proxy)
        self.assertIsInstance(ax, Proxy)
        lines = ax.plot([0, 1, 2], [0, 1, 4], "r-")
        self.assertIsInstance(lines, list)
        self.assertEqual(len(lines), 1)
        ax.set_xlabel("x")
        self.assertEqual(ax.get_xlabel(), "x")
        plt.show()  # non-blocking under ion(); must not wedge the loop
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "fig.png")
            self.br.call(fig.savefig, path)
            self.assertTrue(os.path.exists(path))
            self.assertGreater(os.path.getsize(path), 1000)
        plt.close(fig)

    def test_show_block_true_does_not_wedge(self):
        plt = self.br.import_module("matplotlib.pyplot")
        plt.figure()
        self.br.call(plt.show, block=True)  # patched to ignore block
        self.assertEqual(self.br.eval("1 + 1"), 2)  # stream still in sync
        plt.close("all")

    @unittest.skipIf(numpy is None, "numpy required")
    def test_tricontourf_through_the_binary_path(self):
        n = 160
        g = numpy.linspace(0.0, 1.0, n)
        xx, yy = numpy.meshgrid(g, g)
        x, y = xx.ravel(), yy.ravel()
        z = numpy.sin(6 * x) * numpy.cos(6 * y)
        idx = numpy.arange(n * n, dtype="<i4").reshape(n, n)
        tris = numpy.empty((2 * (n - 1) * (n - 1), 3), dtype="<i4")
        tris[0::2, 0] = idx[:-1, :-1].ravel()
        tris[0::2, 1] = idx[:-1, 1:].ravel()
        tris[0::2, 2] = idx[1:, :-1].ravel()
        tris[1::2, 0] = idx[1:, 1:].ravel()
        tris[1::2, 1] = idx[1:, :-1].ravel()
        tris[1::2, 2] = idx[:-1, 1:].ravel()
        self.assertGreater(len(tris), 50000)

        plt = self.br.import_module("matplotlib.pyplot")
        fig, ax = plt.subplots()
        self.br.call(ax.tricontourf, x, y, tris, z)
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "tri.png")
            self.br.call(fig.savefig, path)
            self.assertGreater(os.path.getsize(path), 10000)
        plt.close(fig)

    # -- an arbitrary package, per plan §5.6 ------------------------------

    def test_csv_through_server_side_files(self):
        csv = self.br.import_module("csv")
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "out.csv")
            with self.br.open(path, "w", newline="") as f:
                w = csv.writer(f)
                self.br.call(w.writerow, ["x", "By"])
                self.br.call(w.writerows, [[0.0, 1.5], [1.0, 2.5]])
            rows = []
            with self.br.open(path, newline="") as f:
                for row in csv.reader(f):
                    rows.append(row)
            self.assertEqual(rows[0], ["x", "By"])
            self.assertEqual(float(rows[2][1]), 2.5)

    def test_wide_row_falls_back_to_a_handle(self):
        csv = self.br.import_module("csv")
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "wide.csv")
            with self.br.open(path, "w", newline="") as f:
                self.br.call(csv.writer(f).writerow, list(range(70)))
            with self.br.open(path, newline="") as f:
                row = next(iter(csv.reader(f)))
            self.assertIsInstance(row, Proxy)  # 70 > the 64-element rule
            self.assertEqual(len(row), 70)
            self.assertEqual(row[69], "69")


class Lifecycle(unittest.TestCase):
    """Handshake failures and shutdown paths need their own worker each."""

    def test_ops_before_hello_are_refused(self):
        br = Bridge(auto_handshake=False).start()
        try:
            with self.assertRaises(RemoteError) as cm:
                br.request("import", name="math")
            self.assertEqual(cm.exception.etype, "BridgeProtocolError")
            self.assertIn("hello", cm.exception.msg)
        finally:
            br.close()

    def test_bad_token_locks_the_worker_out(self):
        br = Bridge(auto_handshake=False).start()
        try:
            with self.assertRaises(RemoteError) as cm:
                br.request("hello", token="wrong", protocol=1, client="t")
            self.assertIn("token", cm.exception.msg)
            # ...and nothing works afterwards.
            with self.assertRaises(RemoteError):
                br.request("import", name="math")
        finally:
            br.close()

    def test_protocol_version_mismatch(self):
        br = Bridge(auto_handshake=False).start()
        try:
            with self.assertRaises(RemoteError) as cm:
                br.request("hello", token=br.token, protocol=99, client="t")
            self.assertIn("protocol", cm.exception.msg)
        finally:
            br.close()

    def test_worker_greeting_token_is_checked_by_the_client(self):
        """femm-qt's half of the authentication: a worker that cannot present
        the token never gets to answer anything."""
        br = Bridge(env={"FEMMQT_PYBRIDGE_TOKEN": "not-the-one"})
        with self.assertRaises(BridgeError) as cm:
            br.start()
        self.assertIn("token", str(cm.exception))
        if br.sock is not None:
            br.sock.close()

    def test_unusable_backend_is_reported_not_fatal(self):
        """A backend that cannot come up must be named in hello and must not
        take the worker down with it -- everything non-plotting still works.
        Note this probes with switch_backend: matplotlib.use() alone accepts a
        backend whose GUI bindings are missing and only fails at first figure.
        """
        br = Bridge(backend="no-such-backend").start()
        try:
            self.assertIsNone(br.hello["backend"])
            self.assertIn("no-such-backend", br.hello["backend_error"])
            self.assertEqual(br.import_module("math").floor(1.5), 1)
        finally:
            br.close()

    def test_worker_exits_cleanly_on_eof(self):
        br = Bridge().start()
        br.sock.close()
        br.sock = None
        t0 = time.time()
        rc = br.proc.wait(2)
        self.assertEqual(rc, 0)
        self.assertLess(time.time() - t0, 2.0)

    def test_shutdown_op_exits_zero(self):
        br = Bridge().start()
        self.assertEqual(br.close(), 0)

    def test_client_sees_a_dead_worker_as_a_bridge_error(self):
        br = Bridge().start()
        br.proc.kill()
        br.proc.wait(5)
        with self.assertRaises(BridgeError) as cm:
            br.eval("1")
        self.assertIn("exited unexpectedly", str(cm.exception))
        br.sock.close()
        br.sock = None


if __name__ == "__main__":
    unittest.main(verbosity=2)
