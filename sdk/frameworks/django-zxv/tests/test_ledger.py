# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""A = L + E: the exact computation and the read-only admin page."""

from __future__ import annotations

from fractions import Fraction

from django.contrib.auth.models import Permission, User
from django.test import TestCase
from treasury.models import LedgerLine
from zxv import Rational, Truth

from django_zxv.ledger import accounting_equation, overall


class EquationTests(TestCase):
    def test_thirds_add_up_exactly(self) -> None:
        lines = [("asset", "1/3", "NGN")] * 3 + [("liability", "0.5", "NGN"), ("equity", Rational(1, 2), "NGN")]
        [r] = accounting_equation(lines)
        self.assertEqual(r.assets, Fraction(1))
        self.assertEqual(r.difference, 0)
        self.assertIs(r.holds, Truth.TRUE)
        self.assertEqual(r.as_text()["assets"], "1")

    def test_a_tiny_difference_is_seen(self) -> None:
        [r] = accounting_equation([("asset", "0.3", None), ("liability", "0.1", None), ("equity", "0.2000000000000001", None)])
        self.assertIs(r.holds, Truth.FALSE)
        self.assertEqual(r.as_text()["difference"], "-0.0000000000000001")

    def test_per_currency_and_overall(self) -> None:
        rs = accounting_equation([("ASSET", 5, "NGN"), ("Equity", 5, "NGN"), ("asset", 1, "GHS")])
        self.assertEqual([r.currency for r in rs], ["GHS", "NGN"])
        self.assertEqual([r.holds for r in rs], [Truth.FALSE, Truth.TRUE])
        self.assertIs(overall(rs), Truth.FALSE)
        self.assertIs(overall([]), Truth.NEITHER)

    def test_unknown_kinds_make_it_neither(self) -> None:
        [r] = accounting_equation([("asset", 1, "NGN"), ("equity", 1, "NGN"), ("income", 1, "NGN")])
        self.assertEqual(r.unclassified, 1)
        self.assertIs(r.holds, Truth.NEITHER)

    def test_totals_beyond_int64_stay_exact(self) -> None:
        big = Rational(9223372036854775807)
        [r] = accounting_equation([("asset", big, "X"), ("asset", big, "X"), ("liability", big, "X"), ("equity", big, "X")])
        self.assertIs(r.holds, Truth.TRUE)
        self.assertEqual(r.as_text()["assets"], "18446744073709551614")

    def test_floats_are_refused(self) -> None:
        with self.assertRaises(TypeError):
            accounting_equation([("asset", 0.1, "NGN")])


class EquationAdminTests(TestCase):
    url = "/admin/treasury/ledgerline/accounting-equation/"

    def setUp(self) -> None:
        self.admin = User.objects.create_superuser("root", "root@example.invalid", "pw")
        self.client.force_login(self.admin)

    def _lines(self, *rows: tuple[str, str]) -> None:
        for kind, amount in rows:
            LedgerLine.objects.create(account=kind, kind=kind, currency="NGN", amount=amount)

    def test_holds(self) -> None:
        self._lines(("asset", "1/3"), ("asset", "2/3"), ("liability", "0.25"), ("equity", "3/4"))
        res = self.client.get(self.url)
        self.assertEqual(res.status_code, 200)
        self.assertEqual(res.context["verdict"], "TRUE")
        self.assertContains(res, "<strong>TRUE</strong>", html=False)
        self.assertEqual(res.context["results"][0]["liabilities_plus_equity"], "1")

    def test_does_not_hold(self) -> None:
        self._lines(("asset", "1"), ("liability", "1/3"), ("equity", "0.6666666666666667"))
        res = self.client.get(self.url)
        self.assertEqual(res.context["verdict"], "FALSE")
        self.assertEqual(res.context["results"][0]["difference"], "-1/30000000000000000")

    def test_no_lines_is_neither(self) -> None:
        self.assertEqual(self.client.get(self.url).context["verdict"], "NEITHER")

    def test_read_only_and_advisory(self) -> None:
        self._lines(("asset", "1"))
        self.assertEqual(self.client.post(self.url).status_code, 405)
        self.assertEqual(LedgerLine.objects.count(), 1)
        # Advisory: an unbalanced ledger still saves.
        LedgerLine.objects.create(account="x", kind="asset", currency="NGN", amount="5")
        self.assertEqual(self.client.get(self.url).context["verdict"], "FALSE")

    def test_needs_view_permission(self) -> None:
        staff = User.objects.create_user("clerk", password="pw", is_staff=True)
        self.client.force_login(staff)
        self.assertEqual(self.client.get(self.url).status_code, 403)
        staff.user_permissions.add(Permission.objects.get(codename="view_ledgerline"))
        staff = User.objects.get(pk=staff.pk)
        self.client.force_login(staff)
        self.assertEqual(self.client.get(self.url).status_code, 200)

    def test_changelist_links_to_it(self) -> None:
        res = self.client.get("/admin/treasury/ledgerline/")
        self.assertContains(res, 'href="/admin/treasury/ledgerline/accounting-equation/"')

    def test_admin_form_takes_exact_text(self) -> None:
        res = self.client.post("/admin/treasury/ledgerline/add/",
                               {"account": "cash", "kind": "asset", "currency": "NGN", "amount": "1/3"})
        self.assertEqual(res.status_code, 302)
        self.assertEqual(LedgerLine.objects.get().amount, Rational(1, 3))
        res = self.client.post("/admin/treasury/ledgerline/add/",
                               {"account": "cash", "kind": "asset", "currency": "NGN", "amount": "1e3"})
        self.assertEqual(res.status_code, 200)
        self.assertEqual(LedgerLine.objects.count(), 1)
