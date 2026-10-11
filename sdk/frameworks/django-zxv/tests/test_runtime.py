# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The context the app config owns: capabilities, the root never handed out, R6 teardown."""

from __future__ import annotations

from typing import Any
from unittest import mock

import zxv
from django.test import SimpleTestCase
from zxv import Cap, Capability, Context

import django_zxv
from django_zxv import runtime
from django_zxv.conf import parse_settings

from .support import isolated_runtime


class RuntimeTests(SimpleTestCase):
    def test_live_context_with_narrow_capabilities(self) -> None:
        ctx = django_zxv.get_context()
        self.assertFalse(ctx.is_poisoned)
        self.assertEqual(ctx.currencies.enabled(), ["NGN"])
        self.assertEqual(django_zxv.capability_names(), ["teller", "auditor"])
        teller = django_zxv.get_capability("teller")
        auditor = django_zxv.get_capability("auditor")
        self.assertEqual(teller.caps, Cap.LEDGER_READ | Cap.LEDGER_POST)
        self.assertEqual(auditor.caps, Cap.LEDGER_READ)
        with self.assertRaises(zxv.CapabilityDeniedError):
            zxv.Ledger(auditor).open_account("x", "NGN")

    def test_root_is_never_handed_out(self) -> None:
        with self.assertRaises(KeyError):
            django_zxv.get_capability("root")
        rt = runtime.current()
        public = [getattr(rt, n) for n in dir(rt) if not n.startswith("_") and not callable(getattr(rt, n))]
        self.assertFalse(any(isinstance(v, Capability) for v in public))
        for name in django_zxv.capability_names():
            self.assertNotEqual(django_zxv.get_capability(name).caps, Cap.LEDGER_READ | Cap.LEDGER_POST | Cap.CRYPTO_SIGN)

    def test_start_up_fails_for_a_currency_libzxv_refuses(self) -> None:
        with self.assertRaises(zxv.ZxvError):
            runtime.ZxvRuntime(parse_settings({"CURRENCIES": ["XXX"]}))

    def test_close_follows_abi_r6(self) -> None:
        order: list[str] = []
        with isolated_runtime() as rt:
            names: dict[int, str] = {id(rt.capability(n)._h): n for n in rt.names}
            names[id(rt.context._h)] = "context"
            cap_destroy = Capability._destroy
            ctx_destroy = Context._destroy

            def cap(self: Capability, h: Any) -> None:
                order.append(names.get(id(h), "root"))
                cap_destroy(self, h)

            def ctx(self: Context, h: Any) -> None:
                order.append(names.get(id(h), "?"))
                ctx_destroy(self, h)

            with mock.patch.object(Capability, "_destroy", cap), mock.patch.object(Context, "_destroy", ctx):
                rt.close()
                rt.close()  # idempotent
            self.assertTrue(rt.context.closed)
        self.assertEqual(order, ["auditor", "teller", "root", "context"])

    def test_shutdown_then_install_again(self) -> None:
        saved = runtime._runtime
        try:
            fresh = runtime.ZxvRuntime(parse_settings({"CAPABILITIES": {"reader": "LEDGER_READ"}}))
            runtime._runtime = fresh
            django_zxv.shutdown()
            self.assertTrue(fresh.context.closed)
            with self.assertRaises(RuntimeError):
                django_zxv.get_context()
            django_zxv.shutdown()  # twice is fine
        finally:
            runtime._runtime = saved
