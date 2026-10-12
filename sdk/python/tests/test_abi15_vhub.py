# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""ABI 1.5 V-Bill hub: the conformance scenario, step by step, on one hub."""

from __future__ import annotations

import hashlib
from typing import Any, Callable, Dict, List

import pytest

import zxv
from zxv import (Cap, CapabilityDeniedError, InsufficientFundsError, LimitRefusedError, PegError, VHub,
                 VerificationFailedError, ZxvError)

pytestmark = pytest.mark.skipif(not zxv.has_feature2(zxv.Feature2.VHUB), reason="no vhub group")


def status_of(fn: Callable[[], Any]) -> str:
    try:
        fn()
    except ZxvError as e:
        return e.status_name
    return "ZXV_OK"


def test_peg_message(vectors: Dict[str, Any]) -> None:
    for case in vectors["vhub"]["peg_msg"]:
        msg = VHub.peg_message(case["slot"], int(case["num"]), int(case["den"]), int(case["epoch"]))
        assert msg.hex() == case["msg"] and len(msg) == zxv.vhub.VHUB_PEG_MSG_BYTES


def test_scenario(vectors: Dict[str, Any]) -> None:
    sc = vectors["vhub"]["scenario"]
    with zxv.Context() as ctx:
        cap = ctx.root_capability(Cap.ALL_1_5)
        dsa = zxv.MlDsa87(cap)
        keys: List[zxv.KeyPair] = []
        for k in sc["keys"]:
            with zxv.SecretKey.from_bytes(bytes.fromhex(k["seed"])) as seed:
                kp = dsa.generate_from_seed(seed)
            assert hashlib.sha3_256(kp.public_key).hexdigest() == k["pk_sha3_256"]
            keys.append(kp)
        with VHub(cap, bytes.fromhex(sc["hub_seed"])) as hub:
            for c in sc["currencies"]:
                key = keys[c.get("issuer_key", 0)]

                def add() -> None:
                    hub.add_currency(c["slot"], c["code"], c.get("minor_units", 2), key.public_key,
                                     pool_funding=int(c.get("pool_funding", "0")),
                                     vbill_backing=int(c.get("vbill_backing", "0")))
                assert status_of(add) == c["status"], c
                if c["status"] == "ZXV_OK":
                    info = hub.currency(c["slot"])
                    assert (info.issuer_account, info.pool_account, info.vfloat_account) == \
                        (c["issuer_account"], c["pool_account"], c["vfloat_account"])
                    assert info.peg is None
            for p in sc["pegs"]:
                msg = VHub.peg_message(p["slot"], int(p["num"]), int(p["den"]), int(p["epoch"]))
                sig = dsa.sign_deterministic(keys[p["signer"]].secret_key, msg)
                assert hashlib.sha3_256(sig).hexdigest() == p["sig_sha3_256"]
                st = status_of(lambda: hub.set_peg(p["slot"], int(p["num"]), int(p["den"]), int(p["epoch"]), sig))
                assert st == p["status"], p
            peg0 = hub.currency(0).peg
            assert peg0 is not None and (peg0.num, peg0.den, peg0.epoch) == (350, 1, 1)  # refusals changed nothing
            acct = {}
            for a in sc["accounts"]:
                n = hub.open_account(a["slot"], a["owner"])
                assert n == a["account"]
                if "issued" in a:
                    hub.issue(n, int(a["issued"]))
                acct[n] = a
            for q in sc["quotes"]:
                if q["status"] != "ZXV_OK":
                    assert status_of(lambda: hub.quote(q["from"], q["to"], int(q["want"]))) == q["status"], q
                    continue
                got = hub.quote(q["from"], q["to"], int(q["want"]))
                assert (str(got.amount), str(got.vbills), str(got.received)) == (q["amount"], q["vbills"],
                                                                                 q["received"]), q
            for s in sc["swaps"]:
                if "add_limit" in s:
                    hub.add_limit(s["add_limit"]["slot"], int(s["add_limit"]["limit"]))

                def swap() -> zxv.vhub.VHubSwap:
                    return hub.swap(s["from"], s["to"], s["sender"], s["receiver"], int(s["amount"]), int(s["tick"]))
                if s["status"] != "ZXV_OK":
                    assert status_of(swap) == s["status"], s
                    continue
                r = swap()
                assert (str(r.vbills), str(r.received)) == (s["vbills"], s["received"]), s
            assert hub.check() == int(sc["postings"])
            for b in sc["balances"]:
                bal = hub.balance(b["account"])
                assert (str(bal.debit), str(bal.credit), str(bal.equity)) == (b["debit"], b["credit"], b["equity"])
            assert status_of(lambda: hub.balance(len(sc["balances"]))) == "ZXV_E_NOT_FOUND"
        for kp in keys:
            kp.close()


def test_typed_refusals_and_epochs() -> None:
    with zxv.Context() as ctx:
        cap = ctx.root_capability(Cap.ALL_1_5)
        dsa = zxv.MlDsa87(cap)
        with dsa.generate() as ka, dsa.generate() as kb, VHub(cap) as hub:
            hub.add_currency(0, "TSTA", 2, ka.public_key, pool_funding=10_000, vbill_backing=1000)
            hub.add_currency(1, "TSTB", 2, kb.public_key, pool_funding=10_000, vbill_backing=1000)
            with pytest.raises(PegError):
                hub.quote(0, 1, 100)
            hub.set_peg(0, 2, 1, 1, dsa.sign(ka.secret_key, VHub.peg_message(0, 2, 1, 1)))
            with pytest.raises(VerificationFailedError):
                hub.set_peg(1, 2, 1, 1, dsa.sign(ka.secret_key, VHub.peg_message(1, 2, 1, 1)))
            stale = dsa.sign(ka.secret_key, VHub.peg_message(0, 3, 1, 1))
            with pytest.raises(PegError):
                hub.set_peg(0, 3, 1, 1, stale)  # replay at the same epoch
            hub.set_peg(1, 2, 1, 1, dsa.sign(kb.secret_key, VHub.peg_message(1, 2, 1, 1)))
            alice, bob = hub.open_account(0, 1), hub.open_account(1, 2)
            hub.issue(alice, 1000)
            with pytest.raises(zxv.ZxvRefusedError) as e:
                hub.swap(0, 1, alice, bob, 100, 1)
            assert e.value.status_name == "ZXV_E_STATE"  # no rule: fail closed
            hub.add_limit(0, 150)
            hub.add_limit(1, 10**9)
            assert hub.swap(0, 1, alice, bob, 100, 1).received == 100
            with pytest.raises(LimitRefusedError):
                hub.swap(0, 1, alice, bob, 100, 2)
            assert hub.next_epoch() == 1
            hub.swap(0, 1, alice, bob, 100, 3)
            with pytest.raises(InsufficientFundsError):
                hub.swap(1, 0, bob, alice, 300, 4)  # bob holds 200 of slot 1
            assert hub.balance(alice).debit == 800 and hub.balance(bob).debit == 200
            reader = cap.attenuate(Cap.LEDGER_READ)
            assert hub.balance(bob, cap=reader).equity == 200
            with pytest.raises(CapabilityDeniedError):
                hub.issue(alice, 1, cap=reader)
            assert hub.check(cap=reader) >= 4


def test_hub_with_a_cap_of_another_context_is_refused() -> None:
    with zxv.Context() as c1, zxv.Context() as c2:
        k1, k2 = c1.root_capability(Cap.ALL_1_5), c2.root_capability(Cap.ALL_1_5)
        with VHub(k1) as hub, pytest.raises(CapabilityDeniedError):
            hub.check(cap=k2)
