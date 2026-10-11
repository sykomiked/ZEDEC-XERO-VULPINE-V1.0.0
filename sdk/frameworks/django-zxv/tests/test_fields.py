# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""RationalField: exact storage, canonical text, floats refused, no lexicographic lookups."""

from __future__ import annotations

from decimal import Decimal
from fractions import Fraction

from django.core.exceptions import FieldError, ValidationError
from django.db import connection, transaction
from django.test import TestCase
from treasury.models import LedgerLine
from zxv import Rational

from django_zxv.fields import RationalField, RationalFormField, from_canonical, to_canonical


class RationalFieldTests(TestCase):
    def _line(self, amount: object) -> LedgerLine:
        return LedgerLine.objects.create(account="a", kind="asset", currency="NGN", amount=amount)  # type: ignore[misc]  # bad input on purpose

    def test_round_trips_exactly(self) -> None:
        values = [Rational.parse("12.75"), "1/3", 7, Decimal("0.1"), Fraction(-2, 6), "-0.000000000000000001",
                  Rational(9223372036854775807, 9223372036854775806)]
        for v in values:
            with self.subTest(v=v):
                line = self._line(v)
                got = LedgerLine.objects.get(pk=line.pk).amount
                self.assertIsInstance(got, Rational)
                self.assertEqual(got, Rational.coerce(v))

    def test_stores_canonical_text(self) -> None:
        line = self._line("12.75")
        self._line("6/2")
        with connection.cursor() as c:
            c.execute("SELECT amount FROM treasury_ledgerline ORDER BY id")
            self.assertEqual([r[0] for r in c.fetchall()], ["51/4", "3/1"])
        self.assertEqual(str(LedgerLine.objects.get(pk=line.pk).amount), "12.75")

    def test_equal_values_match_exactly(self) -> None:
        self._line("0.5")
        self._line("1/3")
        self.assertEqual(LedgerLine.objects.filter(amount="2/4").count(), 1)
        self.assertEqual(LedgerLine.objects.filter(amount=Fraction(1, 3)).count(), 1)
        self.assertEqual(LedgerLine.objects.filter(amount__in=["1/2", "1/3"]).count(), 2)
        self.assertEqual(LedgerLine.objects.filter(amount__isnull=True).count(), 0)

    def test_floats_are_refused(self) -> None:
        with self.assertRaises(ValidationError) as cm, transaction.atomic():
            self._line(0.1)
        self.assertEqual(cm.exception.code, "float")
        self.assertEqual(LedgerLine.objects.count(), 0)

    def test_inexact_text_is_refused(self) -> None:
        for v in ["1e-1", "0.1 ", "NaN", "1/0", ".5", "99999999999999999999", True]:
            with self.subTest(v=v), self.assertRaises(ValidationError), transaction.atomic():
                self._line(v)

    def test_ordering_lookups_are_refused(self) -> None:
        for lookup in ["amount__gt", "amount__lte", "amount__range", "amount__startswith", "amount__icontains"]:
            with self.subTest(lookup=lookup), self.assertRaises(FieldError):
                list(LedgerLine.objects.filter(**{lookup: "1"}))

    def test_full_clean_and_serialization(self) -> None:
        line = LedgerLine(account="a", kind="asset", currency="NGN", amount="1/3")
        line.full_clean()
        self.assertEqual(line.amount, Rational(1, 3))
        field = LedgerLine._meta.get_field("amount")
        assert isinstance(field, RationalField)
        self.assertEqual(field.value_to_string(line), "1/3")
        name, path, args, kwargs = field.deconstruct()
        self.assertEqual(path, "django_zxv.fields.RationalField")
        self.assertNotIn("max_length", kwargs)

    def test_canonical_helpers(self) -> None:
        self.assertEqual(to_canonical(Rational.parse("-0.25")), "-1/4")
        self.assertEqual(from_canonical("-1/4"), Rational(-1, 4))
        for bad in ["2/4", "1/-4", "1", "a/b", "1/0x"]:
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                from_canonical(bad)


class RationalFormFieldTests(TestCase):
    def test_cleans_exact_text(self) -> None:
        f = RationalFormField()
        self.assertEqual(f.clean("12.75"), Rational(51, 4))
        self.assertEqual(f.clean(" 1/3 "), Rational(1, 3))
        self.assertEqual(f.prepare_value(Rational(1, 3)), "1/3")
        self.assertIsNone(RationalFormField(required=False).clean(""))

    def test_refuses_floats_and_inexact_text(self) -> None:
        f = RationalFormField()
        with self.assertRaises(ValidationError) as cm:
            f.clean(0.1)
        self.assertEqual(cm.exception.code, "float")
        for v in ["1e3", "abc", "1/0"]:
            with self.subTest(v=v), self.assertRaises(ValidationError):
                f.clean(v)

    def test_model_form_uses_it(self) -> None:
        from django.forms import modelform_factory

        form_cls = modelform_factory(LedgerLine, fields=["account", "kind", "currency", "amount"])
        form = form_cls(data={"account": "cash", "kind": "asset", "currency": "NGN", "amount": "2/3"})
        self.assertTrue(form.is_valid(), form.errors)
        self.assertEqual(form.save().amount, Rational(2, 3))
        bad = form_cls(data={"account": "cash", "kind": "asset", "currency": "NGN", "amount": "6.02e23"})
        self.assertFalse(bad.is_valid())
