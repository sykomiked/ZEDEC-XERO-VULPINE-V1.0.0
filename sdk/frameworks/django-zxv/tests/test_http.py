# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Over HTTP: problem details by status class, the health view, and the sample end to end."""

from __future__ import annotations

import json
from typing import Any

from django.test import Client, SimpleTestCase, TestCase

from .support import isolated_runtime
from .urls import ERRORS, SECRET

EXPECTED = {
    "arg": (400, "usage"), "notfound": (404, "usage"), "duplicate": (409, "usage"), "cap": (403, "refused"),
    "funds": (422, "refused"), "verify": (422, "refused"), "budget": (429, "refused"), "notimpl": (501, "refused"),
    "capacity": (503, "refused"), "state": (409, "refused"), "io": (500, "system"), "unknown": (500, "system"),
    "fatal": (503, "fatal"),
}


class ProblemMiddlewareTests(SimpleTestCase):
    def test_every_status_class(self) -> None:
        self.assertEqual(set(EXPECTED), set(ERRORS))
        for kind, (status, cls) in EXPECTED.items():
            with self.subTest(kind=kind):
                res = self.client.get(f"/raise/{kind}")
                self.assertEqual(res.status_code, status)
                self.assertEqual(res["Content-Type"], "application/problem+json")
                self.assertEqual(res["Cache-Control"], "no-store")
                body = res.json()
                self.assertEqual(set(body), {"type", "title", "status", "code", "class"})
                self.assertEqual(body["class"], cls)
                self.assertEqual(body["status"], status)
                code = ERRORS[kind][2] if kind != "unknown" else "ZXV_E_INTERNAL"
                self.assertEqual(body["code"], code)
                self.assertEqual(body["type"], f"urn:zxv:problem:{code.lower()}")
                self.assertNotIn(SECRET, res.content.decode())

    def test_other_exceptions_pass_through(self) -> None:
        client = Client(raise_request_exception=False)
        res = client.get("/raise/other")
        self.assertEqual(res.status_code, 500)
        self.assertNotIn(b"problem", res.content)


class HealthTests(SimpleTestCase):
    def test_up_when_live(self) -> None:
        res = self.client.get("/health/")
        self.assertEqual(res.status_code, 200)
        self.assertIn("no-cache", res["Cache-Control"])
        body = res.json()
        self.assertEqual(body["status"], "up")
        self.assertRegex(body["abi"], r"^1\.\d+\.\d+$")
        self.assertEqual(body["capabilities"]["teller"], {"rights": ["LEDGER_READ", "LEDGER_POST"], "attestation": "TRUE", "revoked": False})

    def test_down_when_halted(self) -> None:
        with isolated_runtime() as rt:
            rt.context.halt("test")
            res = self.client.get("/health/")
            self.assertEqual(res.status_code, 503)
            self.assertEqual(res.json(), {"status": "down", "state": "halted"})

    def test_down_when_closed_or_missing(self) -> None:
        with isolated_runtime() as rt:
            rt.close()
            self.assertEqual(self.client.get("/health/").json(), {"status": "down", "state": "closed"})
        from django_zxv import runtime

        saved = runtime._runtime
        runtime._runtime = None
        try:
            res = self.client.get("/health/")
            self.assertEqual((res.status_code, res.json()["state"]), (503, "unknown"))
        finally:
            runtime._runtime = saved

    def test_get_only(self) -> None:
        self.assertEqual(self.client.post("/health/").status_code, 405)


def post(client: Client, path: str, body: str) -> Any:
    return client.post(path, data=body, content_type="application/json")


class SampleTests(TestCase):
    def test_quote_is_exact(self) -> None:
        self.assertEqual(post(self.client, "/quote", '{"price":"12.75","quantity":"1/3"}').json(), {"total": "4.25"})
        self.assertEqual(post(self.client, "/quote", '{"price":"1","quantity":"1/3"}').json(), {"total": "1/3"})
        self.assertEqual(post(self.client, "/quote", '{"price":3,"quantity":"0.5"}').json(), {"total": "1.5"})

    def test_json_floats_are_refused(self) -> None:
        for body in ['{"price":12.75,"quantity":"1"}', '{"price":"1","quantity":1.0}', '{"price":"1","quantity":1e3}',
                     '{"price":"1","quantity":"1","x":[0.5]}']:
            with self.subTest(body=body):
                res = post(self.client, "/quote", body)
                self.assertEqual(res.status_code, 400)
                self.assertEqual(res.json()["type"], "urn:zxv:problem:json_float")

    def test_bad_bodies(self) -> None:
        self.assertEqual(post(self.client, "/quote", '{"price":').json()["type"], "urn:zxv:problem:json_syntax")
        self.assertEqual(post(self.client, "/quote", '{"price":"1e1","quantity":"1"}').status_code, 400)
        self.assertEqual(post(self.client, "/quote", '{"price":"1","quantity":"1","discount":"0"}').status_code, 400)
        self.assertEqual(post(self.client, "/quote", '{"price":"1"}').status_code, 400)
        self.assertEqual(self.client.post("/quote", data="price=1", content_type="text/plain").status_code, 415)

    def test_sum(self) -> None:
        self.assertEqual(self.client.get("/sum?a=0.1&b=0.2").json(), {"sum": "0.3"})
        self.assertEqual(self.client.get("/sum?a=1e-1&b=0.2").status_code, 400)
        self.assertEqual(self.client.get("/sum?a=0.1").status_code, 400)

    def test_teller_posts_auditor_is_refused(self) -> None:
        with isolated_runtime():
            paid = post(self.client, "/payments", '{"amountMinor":15000,"reference":"E2E-1"}')
            self.assertEqual(paid.status_code, 201)
            self.assertRegex(paid.json()["entryId"], r"^\d+$")
            self.assertEqual(post(self.client, "/payments", '{"amountMinor":"25","reference":"E2E-3"}').status_code, 201)

            refused = post(self.client, "/refunds", '{"amountMinor":100,"reference":"E2E-2"}')
            self.assertEqual(refused.status_code, 403)
            self.assertEqual(refused.json(), {"type": "urn:zxv:problem:zxv_e_cap", "title": "Operation refused", "status": 403,
                                              "code": "ZXV_E_CAP", "class": "refused"})
            dup = post(self.client, "/payments", '{"amountMinor":1,"reference":"E2E-1"}')
            self.assertEqual(dup.status_code, 409)
            self.assertNotIn("E2E-1", dup.content.decode())
            self.assertEqual(post(self.client, "/payments", '{"amountMinor":1.5,"reference":"X"}').status_code, 400)

            self.assertEqual(self.client.get("/balance").json(), {"debit": "15025", "credit": "0", "equity": "15025"})

    def test_after_a_halt(self) -> None:
        with isolated_runtime():
            self.assertEqual(self.client.get("/health/").status_code, 200)
            self.assertEqual(self.client.post("/admin-halt").status_code, 202)
            res = self.client.get("/balance")
            self.assertEqual(res.status_code, 503)
            self.assertEqual(json.loads(res.content)["code"], "ZXV_E_POISONED")
            self.assertEqual(post(self.client, "/payments", '{"amountMinor":1,"reference":"AFTER"}').status_code, 503)
            self.assertEqual(self.client.get("/health/").status_code, 503)
            self.assertEqual(self.client.get("/sum?a=1&b=2").json(), {"sum": "3"})
