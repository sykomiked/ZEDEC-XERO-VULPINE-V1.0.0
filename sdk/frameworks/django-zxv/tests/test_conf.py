# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The ZXV setting: validation, the system check, and start-up failure."""

from __future__ import annotations

from typing import Any

from django.apps import apps
from django.core.exceptions import ImproperlyConfigured
from django.test import SimpleTestCase, override_settings
from zxv import Cap, FatalPolicy

from django_zxv.checks import check_settings
from django_zxv.conf import ZxvSettingsError, parse_settings


class ParseSettingsTests(SimpleTestCase):
    def test_defaults(self) -> None:
        s = parse_settings(None)
        self.assertEqual(s.root_rights, Cap.ALL)
        self.assertEqual(dict(s.capabilities), {})
        self.assertEqual(s.fatal_policy, FatalPolicy.RAISE)

    def test_names_strings_and_order(self) -> None:
        s = parse_settings({
            "CURRENCIES": ["NGN", "GHS"],
            "PRIVATE_UNITS": {"VFV": 2},
            "ROOT_RIGHTS": "ledger_read, LEDGER_POST",
            "CAPABILITIES": {"teller": ["LEDGER_READ", "LEDGER_POST"], "auditor": "LEDGER_READ"},
            "FATAL_POLICY": "abort",
        })
        self.assertEqual(s.root_rights, Cap.LEDGER_READ | Cap.LEDGER_POST)
        self.assertEqual(list(s.capabilities), ["teller", "auditor"])
        self.assertEqual(s.capabilities["auditor"], Cap.LEDGER_READ)
        self.assertEqual(s.private_units, (("VFV", 2),))
        self.assertEqual(s.fatal_policy, FatalPolicy.ABORT)

    def test_every_problem_is_listed(self) -> None:
        bad: list[tuple[Any, str]] = [
            ({"CAPABILITIES": {"teller": ["LEDGER_PRINT"]}}, "unknown right 'LEDGER_PRINT'"),
            ({"ROOT_RIGHTS": ["LEDGER_READ"], "CAPABILITIES": {"teller": ["LEDGER_POST"]}}, "which ROOT_RIGHTS does not hold"),
            ({"CAPABILITIES": {"root": ["LEDGER_READ"]}}, "'root' is reserved"),
            ({"CAPABILITIES": {"tel ler": ["LEDGER_READ"]}}, "not a valid name"),
            ({"CAPABILITIES": {"teller": []}}, "holds no right"),
            ({"CAPABILITIES": {"teller": 3}}, "not numbers"),
            ({"CURRENCIES": ["naira"]}, "not an ISO 4217"),
            ({"CURRENCIES": "NGN"}, "must be a list"),
            ({"PRIVATE_UNITS": {"vfv": 9}}, "upper-case"),
            ({"FATAL_POLICY": "ignore"}, "FATAL_POLICY"),
            ({"LIBRARY_PATH": ""}, "LIBRARY_PATH"),
            ({"CURRENCY": ["NGN"]}, "unknown key"),
            ([], "must be a dict"),
        ]
        for raw, text in bad:
            with self.subTest(raw=raw):
                with self.assertRaises(ZxvSettingsError) as cm:
                    parse_settings(raw)
                self.assertIn(text, str(cm.exception))

    @override_settings(ZXV={"CAPABILITIES": {"root": "LEDGER_READ"}, "CURRENCIES": ["x"]})
    def test_system_check_reports_each_problem(self) -> None:
        errors = check_settings()
        self.assertEqual(len(errors), 2)
        self.assertEqual({e.id for e in errors}, {"django_zxv.E001"})

    def test_system_check_passes_for_the_sample(self) -> None:
        self.assertEqual(check_settings(), [])

    @override_settings(ZXV={"CAPABILITIES": {"teller": "LEDGER_PRINT"}})
    def test_start_up_fails_on_a_bad_setting(self) -> None:
        with self.assertRaises(ImproperlyConfigured):
            apps.get_app_config("django_zxv").ready()
