# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Exact JSON helpers: floats refused before they become binary."""

from __future__ import annotations

from decimal import Decimal
from fractions import Fraction

from django.test import SimpleTestCase
from zxv import FloatRejectedError, Rational

from django_zxv.exactjson import (ExactJsonError, ExactJsonResponse, dumps_exact, loads_exact, minor_units_from_json,
                                  rational_from_json)


class LoadsExactTests(SimpleTestCase):
    def test_refuses_every_float_token(self) -> None:
        for text in ['{"a":0.1}', '{"a":1.0}', '{"a":1e3}', '[1,[2,{"b":-2E-2}]]', '{"a":1.00000000000000001}', '{"a":NaN}',
                     '{"a":Infinity}']:
            with self.subTest(text=text), self.assertRaises(ExactJsonError) as cm:
                loads_exact(text)
            self.assertEqual(cm.exception.code, "json_float")

    def test_keeps_integers_and_strings_exact(self) -> None:
        self.assertEqual(loads_exact('{"a":"0.1","b":9007199254740993,"c":-7}'), {"a": "0.1", "b": 9007199254740993, "c": -7})

    def test_malformed(self) -> None:
        with self.assertRaises(ExactJsonError) as cm:
            loads_exact('{"a":')
        self.assertEqual(cm.exception.code, "json_syntax")


class FieldHelperTests(SimpleTestCase):
    def test_rational_from_json(self) -> None:
        self.assertEqual(rational_from_json("12.75"), Rational(51, 4))
        self.assertEqual(rational_from_json("1/3"), Rational(1, 3))
        self.assertEqual(rational_from_json(42), Rational(42))
        bad: list[object] = [0.1, None, True, "1e3", " 1", [], {}, 1 << 70]
        for v in bad:
            with self.subTest(v=v), self.assertRaises(ExactJsonError):
                rational_from_json(v, "price")

    def test_minor_units_from_json(self) -> None:
        self.assertEqual(minor_units_from_json(15000), 15000)
        self.assertEqual(minor_units_from_json("9007199254740993"), 9007199254740993)
        for v in [1.5, "1.0", "1e3", 1 << 63, None, True, "١٢"]:
            with self.subTest(v=v), self.assertRaises(ExactJsonError):
                minor_units_from_json(v, "amountMinor")

    def test_messages_are_ours(self) -> None:
        with self.assertRaises(ExactJsonError) as cm:
            rational_from_json("1e3", "price")
        self.assertTrue(str(cm.exception).startswith("price: "))
        self.assertNotIn("ZXV_E_", str(cm.exception))


class DumpsExactTests(SimpleTestCase):
    def test_writes_exact_text(self) -> None:
        out = dumps_exact({"r": Rational(1, 3), "f": Fraction(1, 4), "d": Decimal("0.10"), "n": [1, "x"]})
        self.assertEqual(out, '{"r": "1/3", "f": "0.25", "d": "0.1", "n": [1, "x"]}')

    def test_refuses_floats(self) -> None:
        with self.assertRaises(FloatRejectedError):
            dumps_exact({"a": [0.5]})
        with self.assertRaises(FloatRejectedError):
            ExactJsonResponse({"a": 1.0})

    def test_response(self) -> None:
        r = ExactJsonResponse({"total": Rational(17, 4)}, status=201)
        self.assertEqual(r.status_code, 201)
        self.assertEqual(r["Content-Type"], "application/json")
        self.assertEqual(r.content, b'{"total": "4.25"}')
