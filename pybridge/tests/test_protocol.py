# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
"""Framing and codec tests.  No sockets, no subprocess, no femm-qt.

    python3 test_protocol.py            (or: python3 -m unittest discover)

Two implementations of the wire format exist on purpose -- the worker's and
fake_client's, which is the one the MicroPython client will be ported from --
so several tests here build with one and parse with the other.  A framing bug
that is consistent with itself would otherwise pass.
"""

import array
import math
import os
import struct
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "worker"))

import fake_client as fc  # noqa: E402
import femmqt_pybridge_worker as w  # noqa: E402

try:
    import numpy
except ImportError:
    numpy = None


def frames_of(buf, data):
    buf.feed(data)
    return list(buf.frames())


class Framing(unittest.TestCase):
    def test_round_trip_json(self):
        frame = w.build_frame({"op": "hello", "id": 1})
        (kind, payload), = frames_of(w.FrameBuffer(), frame)
        msg, blob = w.parse_frame(kind, payload)
        self.assertEqual(msg["op"], "hello")
        self.assertEqual(blob, b"")

    def test_round_trip_binary(self):
        blob = bytes(range(256)) * 4
        frame = w.build_frame({"id": 2, "v": {"__b__": {"off": 0,
                                                        "len": len(blob)}}},
                              blob)
        (kind, payload), = frames_of(w.FrameBuffer(), frame)
        self.assertEqual(kind, w.KIND_BIN)
        msg, got = w.parse_frame(kind, payload)
        self.assertEqual(got, blob)
        self.assertEqual(msg["id"], 2)

    def test_cross_implementation(self):
        """Worker frames parse in the client and vice versa."""
        blob = b"\x01\x02\x03\x04"
        f1 = w.build_frame({"id": 3, "x": 1}, blob)
        (k, p), = frames_of(fc.FrameBuffer(), f1)
        self.assertEqual(fc.parse_frame(k, p), ({"id": 3, "x": 1}, blob))
        f2 = fc.build_frame({"id": 4}, blob)
        (k, p), = frames_of(w.FrameBuffer(), f2)
        self.assertEqual(w.parse_frame(k, p), ({"id": 4}, blob))

    def test_partial_delivery(self):
        """A frame delivered one byte at a time appears exactly once, whole."""
        frame = w.build_frame({"id": 5, "s": "x" * 100})
        buf = w.FrameBuffer()
        got = []
        for i in range(len(frame)):
            got += frames_of(buf, frame[i:i + 1])
        self.assertEqual(len(got), 1)
        self.assertEqual(w.parse_frame(*got[0])[0]["id"], 5)

    def test_coalesced_frames(self):
        blob = b"abcd"
        data = (w.build_frame({"id": 1}) + w.build_frame({"id": 2}, blob)
                + w.build_frame({"id": 3}))
        got = frames_of(w.FrameBuffer(), data)
        self.assertEqual([w.parse_frame(*f)[0]["id"] for f in got], [1, 2, 3])
        self.assertEqual(w.parse_frame(*got[1])[1], blob)

    def test_trailing_partial_frame_waits(self):
        data = w.build_frame({"id": 1}) + w.build_frame({"id": 2})[:3]
        buf = w.FrameBuffer()
        self.assertEqual(len(frames_of(buf, data)), 1)
        self.assertEqual(list(buf.frames()), [])

    def test_oversize_rejected_without_allocating(self):
        header = struct.pack("<I", w.MAX_FRAME + 1) + b"J"
        buf = w.FrameBuffer()
        buf.feed(header)
        with self.assertRaises(w.FramingError):
            list(buf.frames())

    def test_zero_length_rejected(self):
        buf = w.FrameBuffer()
        buf.feed(struct.pack("<I", 0))
        with self.assertRaises(w.FramingError):
            list(buf.frames())

    def test_unknown_kind_rejected(self):
        body = b"\x5A{}"
        frame = struct.pack("<I", len(body)) + body
        (kind, payload), = frames_of(w.FrameBuffer(), frame)
        with self.assertRaises(w.FramingError):
            w.parse_frame(kind, payload)

    def test_binary_header_past_end(self):
        payload = struct.pack("<I", 1000) + b"{}"
        with self.assertRaises(w.FramingError):
            w.parse_frame(w.KIND_BIN, payload)

    def test_bad_json_rejected(self):
        with self.assertRaises(w.FramingError):
            w.parse_frame(w.KIND_JSON, b"{not json")

    def test_non_object_payload_rejected(self):
        with self.assertRaises(w.FramingError):
            w.parse_frame(w.KIND_JSON, b"[1,2,3]")


class Encode(unittest.TestCase):
    """Worker -> client encoding rules (§2.3)."""

    def setUp(self):
        self.w = w.Worker("")
        self.out = w.Out()

    def enc(self, v):
        return self.w.encode(v, self.out)

    def test_scalars_native(self):
        for v in (None, True, False, 0, -17, 2 ** 40, 1.5, "text", ""):
            self.assertEqual(self.enc(v), v)

    def test_non_finite_floats_tagged(self):
        # JSON has no nan/inf and MicroPython's parser has no extensions.
        self.assertEqual(self.enc(float("nan")), {"__f__": "nan"})
        self.assertEqual(self.enc(math.inf), {"__f__": "inf"})
        self.assertEqual(self.enc(-math.inf), {"__f__": "-inf"})
        self.assertTrue(math.isnan(w.decode_special_float("nan")))
        self.assertEqual(w.decode_special_float("-inf"), -math.inf)

    def test_complex_scalars_by_value(self):
        """femm-qt is a phasor program: a complex is an ordinary scalar."""
        self.assertEqual(self.enc(3 + 4j), {"__c__": [3.0, 4.0]})
        self.assertEqual(self.enc(-1j), {"__c__": [0.0, -1.0]})
        self.assertEqual(self.w.decode(self.enc(3 + 4j), b""), 3 + 4j)

    def test_complex_carries_non_finite_parts(self):
        v = self.enc(complex(math.inf, float("nan")))
        self.assertEqual(v["__c__"][0], {"__f__": "inf"})
        back = self.w.decode(v, b"")
        self.assertEqual(back.real, math.inf)
        self.assertTrue(math.isnan(back.imag))

    def test_complex_signed_zero_survives(self):
        # -0j is not 0j, and femm-qt cares: a sign flip is a 180 degree phase.
        back = self.w.decode(self.enc(complex(0.0, -0.0)), b"")
        self.assertEqual(math.copysign(1.0, back.imag), -1.0)

    def test_malformed_complex_rejected(self):
        with self.assertRaises(w.ProtocolFault):
            self.w.decode({"__c__": [1.0]}, b"")
        with self.assertRaises(w.ProtocolFault):
            self.w.decode({"__c__": [1.0, "j"]}, b"")

    def test_tuple_by_value(self):
        v = self.enc((1, "a", None))
        self.assertEqual(v, {"__t__": [1, "a", None]})

    def test_list_by_value_with_handles(self):
        """A short sequence travels by value even when its elements can't --
        this is what makes `fig, ax = plt.subplots()` unpack."""
        obj = object()
        v = self.enc([obj])
        self.assertIsInstance(v, list)
        self.assertIn("__h__", v[0])
        self.assertIs(self.w.resolve(v[0]["__h__"]), obj)
        t = self.enc((object(), object()))
        self.assertEqual(len(t["__t__"]), 2)
        self.assertIn("__h__", t["__t__"][1])

    def test_length_boundary(self):
        self.assertIsInstance(self.enc(list(range(w.BY_VALUE_LEN))), list)
        big = self.enc(list(range(w.BY_VALUE_LEN + 1)))
        self.assertIn("__h__", big)

    def test_element_budget(self):
        """Past the total-element cap, the inner containers become handles
        rather than the response growing without bound."""
        nested = [list(range(64)) for _ in range(64)]  # 64*65 > BY_VALUE_ELEMS
        v = self.enc(nested)
        self.assertIsInstance(v, list)
        self.assertIsInstance(v[0], list)
        self.assertIn("__h__", v[-1])  # budget exhausted by the tail
        small = [list(range(10)) for _ in range(10)]
        flat = self.enc(small)
        self.assertTrue(all(isinstance(row, list) for row in flat))

    def test_dict_by_handle(self):
        d = {"a": 1}
        v = self.enc(d)
        self.assertIn("__h__", v)
        self.assertIs(self.w.resolve(v["__h__"]), d)
        self.assertEqual(v["cls"], "builtins.dict")

    def test_bytes_to_binary_region(self):
        v = self.enc(b"hello")
        self.assertEqual(v, {"__b__": {"off": 0, "len": 5}})
        self.assertEqual(bytes(self.out.blob), b"hello")

    def test_handle_ids_are_unique(self):
        a, b = object(), object()
        self.assertNotEqual(self.enc(a)["__h__"], self.enc(b)["__h__"])

    @unittest.skipIf(numpy is None, "numpy required")
    def test_numpy_scalar_is_native(self):
        w.np = numpy
        try:
            self.assertEqual(self.enc(numpy.float64(2.5)), 2.5)
            self.assertEqual(self.enc(numpy.int32(7)), 7)
            self.assertIn("__h__", self.enc(numpy.arange(3)))
        finally:
            w.np = None


class FetchEncode(unittest.TestCase):
    @unittest.skipIf(numpy is None, "numpy required")
    def setUp(self):
        self.w = w.Worker("")
        self.out = w.Out()
        w.np = numpy

    def tearDown(self):
        w.np = None

    def fetch(self, v):
        spec = self.w.encode_fetch(v, self.out)
        return spec, bytes(self.out.blob)

    def test_dtype_matrix_round_trips(self):
        for name in ("f8", "f4", "i8", "i4", "i2", "i1", "u1", "u2", "u4",
                     "u8"):
            with self.subTest(dtype=name):
                self.w, self.out = w.Worker(""), w.Out()
                a = numpy.arange(6, dtype=w.DTYPES[name])
                spec, blob = self.fetch(a)
                self.assertEqual(spec["__nd__"]["dtype"], name)
                self.assertEqual(spec["__nd__"]["shape"], [6])
                back = self.w.decode(spec, blob)
                self.assertTrue(numpy.array_equal(back, a))
                self.assertEqual(back.dtype, a.dtype)

    def test_two_dimensional_shape_survives(self):
        a = numpy.arange(12, dtype="<f8").reshape(3, 4)
        spec, blob = self.fetch(a)
        self.assertEqual(spec["__nd__"]["shape"], [3, 4])
        self.assertTrue(numpy.array_equal(self.w.decode(spec, blob), a))

    def test_non_contiguous_densified(self):
        a = numpy.arange(10, dtype="<f8")[::2]
        spec, blob = self.fetch(a)
        self.assertEqual(spec["__nd__"]["len"], 5 * 8)
        self.assertTrue(numpy.array_equal(self.w.decode(spec, blob), a))

    def test_unusual_dtype_casts_to_f8(self):
        spec, blob = self.fetch(numpy.arange(4, dtype=">f4"))
        self.assertEqual(spec["__nd__"]["dtype"], "f8")

    def test_bool_becomes_u1(self):
        spec, _ = self.fetch(numpy.array([True, False]))
        self.assertEqual(spec["__nd__"]["dtype"], "u1")

    def test_complex_arrays_keep_both_parts(self):
        a = numpy.array([1 + 2j, -3.5 - 4.25j, 0j])
        spec, blob = self.fetch(a)
        self.assertEqual(spec["__nd__"]["dtype"], "c16")
        self.assertEqual(spec["__nd__"]["len"], 3 * 16)
        back = self.w.decode(spec, blob)
        self.assertTrue(numpy.array_equal(back, a))
        self.assertEqual(back.dtype, numpy.dtype("complex128"))

    def test_complex64_widens_rather_than_dropping_the_imaginary_part(self):
        a = numpy.array([1 + 2j], dtype="complex64")
        spec, blob = self.fetch(a)
        self.assertEqual(spec["__nd__"]["dtype"], "c16")
        self.assertEqual(self.w.decode(spec, blob)[0], 1 + 2j)

    def test_complex_two_dimensional(self):
        a = (numpy.arange(6) + 1j * numpy.arange(6)).reshape(2, 3)
        spec, blob = self.fetch(a)
        self.assertEqual(spec["__nd__"]["shape"], [2, 3])
        self.assertTrue(numpy.array_equal(self.w.decode(spec, blob), a))

    def test_long_complex_list_becomes_a_c16_array(self):
        spec, blob = self.fetch([complex(i, -i)
                                 for i in range(w.FETCH_ARRAY_MIN)])
        self.assertEqual(spec["__nd__"]["dtype"], "c16")
        self.assertEqual(self.w.decode(spec, blob)[7], 7 - 7j)

    def test_long_numeric_list_becomes_an_array(self):
        spec, blob = self.fetch([float(i) for i in range(w.FETCH_ARRAY_MIN)])
        self.assertEqual(spec["__nd__"]["dtype"], "f8")

    def test_short_list_stays_a_list(self):
        spec, _ = self.fetch([1.0, 2.0])
        self.assertEqual(spec, [1.0, 2.0])

    def test_unfetchable_raises(self):
        with self.assertRaises(TypeError):
            self.fetch(object())


class Decode(unittest.TestCase):
    """Client -> worker decoding, including the error paths."""

    def setUp(self):
        self.w = w.Worker("")

    def test_handle_resolution(self):
        obj = object()
        h = self.w.alloc(obj)
        self.assertIs(self.w.decode({"__h__": h}, b""), obj)

    def test_unknown_handle(self):
        with self.assertRaises(w.ProtocolFault):
            self.w.decode({"__h__": 999}, b"")

    def test_bool_is_not_a_handle(self):
        with self.assertRaises(w.ProtocolFault):
            self.w.decode({"__h__": True}, b"")

    def test_nested_structures(self):
        got = self.w.decode({"a": [1, {"__t__": [2, 3]}]}, b"")
        self.assertEqual(got, {"a": [1, (2, 3)]})

    def test_blob_slice(self):
        self.assertEqual(self.w.decode({"__b__": {"off": 1, "len": 2}},
                                       b"abcd"), b"bc")

    def test_blob_slice_out_of_range(self):
        with self.assertRaises(w.ProtocolFault):
            self.w.decode({"__b__": {"off": 3, "len": 4}}, b"abcd")

    @unittest.skipIf(numpy is None, "numpy required")
    def test_array_decode_and_length_check(self):
        w.np = numpy
        try:
            raw = array.array("d", [1.0, 2.0, 3.0]).tobytes()
            a = self.w.decode({"__nd__": {"dtype": "f8", "shape": [3],
                                          "off": 0, "len": len(raw)}}, raw)
            self.assertTrue(numpy.array_equal(a, [1.0, 2.0, 3.0]))
            self.assertTrue(a.flags.writeable)  # matplotlib may write in place
            with self.assertRaises(w.ProtocolFault):
                self.w.decode({"__nd__": {"dtype": "f8", "shape": [4],
                                          "off": 0, "len": len(raw)}}, raw)
            with self.assertRaises(w.ProtocolFault):
                self.w.decode({"__nd__": {"dtype": "f16", "shape": [3],
                                          "off": 0, "len": len(raw)}}, raw)
        finally:
            w.np = None


class ClientEncode(unittest.TestCase):
    """fake_client's encoder -- the rules the MicroPython port must match."""

    def setUp(self):
        self.br = fc.Bridge.__new__(fc.Bridge)  # no worker needed
        self.br.gen = 1

    def test_scalars_and_containers(self):
        vals, blob = self.br.enc(1, "s", None, True, [1, 2], (3, 4),
                                 {"k": 1.5})
        self.assertEqual(vals, [1, "s", None, True, [1, 2], [3, 4],
                                {"k": 1.5}])
        self.assertEqual(blob, b"")

    def test_non_finite_floats(self):
        (v,), _ = self.br.enc(float("inf"))
        self.assertEqual(v, {"__f__": "inf"})

    def test_reserved_tag_collision_is_a_clear_error(self):
        with self.assertRaises(fc.BridgeError) as cm:
            self.br.enc({"__h__": 3})
        self.assertIn("put()", str(cm.exception))

    def test_non_string_dict_key_refused(self):
        with self.assertRaises(fc.BridgeError):
            self.br.enc({1: "a"})

    def test_unencodable_type_refused(self):
        with self.assertRaises(fc.BridgeError):
            self.br.enc(object())

    def test_bytes_take_the_binary_region(self):
        (v,), blob = self.br.enc(b"xyz")
        self.assertEqual(v, {"__b__": {"off": 0, "len": 3}})
        self.assertEqual(blob, b"xyz")

    def test_array_typecodes(self):
        (v,), blob = self.br.enc(array.array("d", [1.0, 2.0]))
        self.assertEqual(v["__nd__"]["dtype"], "f8")
        self.assertEqual(v["__nd__"]["shape"], [2])
        self.assertEqual(len(blob), 16)

    def test_bulk_list_threshold(self):
        (small,), _ = self.br.enc([1.0] * (fc.BULK_MIN - 1))
        self.assertIsInstance(small, list)
        (big,), blob = self.br.enc([1.0] * fc.BULK_MIN)
        self.assertEqual(big["__nd__"]["dtype"], "f8")
        self.assertEqual(len(blob), fc.BULK_MIN * 8)

    def test_shared_binary_region_offsets(self):
        (a, b), blob = self.br.enc(b"aa", b"bbb")
        self.assertEqual(a["__b__"], {"off": 0, "len": 2})
        self.assertEqual(b["__b__"], {"off": 2, "len": 3})
        self.assertEqual(blob, b"aabbb")

    def test_complex_client_side(self):
        (v,), _ = self.br.enc(2 - 3j)
        self.assertEqual(v, {"__c__": [2.0, -3.0]})
        (v,), _ = self.br.enc([1j] * 3)
        self.assertEqual(v, [{"__c__": [0.0, 1.0]}] * 3)

    def test_bulk_complex_list_takes_the_binary_path(self):
        vals = [complex(i, i + 0.5) for i in range(fc.BULK_MIN)]
        (v,), blob = self.br.enc(vals)
        self.assertEqual(v["__nd__"]["dtype"], "c16")
        self.assertEqual(len(blob), fc.BULK_MIN * 16)
        wk = w.Worker("")
        if numpy is not None:
            w.np = numpy
            try:
                back = wk.decode(v, blob)
                self.assertEqual(back[3], 3 + 3.5j)
                self.assertEqual(back.dtype, numpy.dtype("complex128"))
            finally:
                w.np = None

    def test_client_encoding_decodes_in_the_worker(self):
        """The two implementations agree end to end."""
        wk = w.Worker("")
        vals, blob = self.br.enc([1, 2], {"k": "v"}, b"raw", math.inf)
        self.assertEqual(wk.decode(vals[0], blob), [1, 2])
        self.assertEqual(wk.decode(vals[1], blob), {"k": "v"})
        self.assertEqual(wk.decode(vals[2], blob), b"raw")
        self.assertEqual(wk.decode(vals[3], blob), math.inf)

    @unittest.skipIf(numpy is None, "numpy required")
    def test_numpy_arrays_round_trip_through_the_worker(self):
        wk = w.Worker("")
        w.np = numpy
        try:
            a = numpy.arange(12, dtype="<f4").reshape(3, 4)
            (v,), blob = self.br.enc(a)
            back = wk.decode(v, blob)
            self.assertTrue(numpy.array_equal(back, a))
            self.assertEqual(back.dtype, a.dtype)
        finally:
            w.np = None


if __name__ == "__main__":
    unittest.main(verbosity=2)
