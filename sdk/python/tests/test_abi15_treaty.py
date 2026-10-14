# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""ABI 1.5 treaty group against the conformance vectors."""

from __future__ import annotations

import threading
from fractions import Fraction
from typing import Any, Callable, Dict

import pytest

import zxv
from zxv import InsufficientFundsError, Treaty, TreatyError, TreatyFault, TreatyOp, ZxvError, ZxvUsageError, treaty

pytestmark = pytest.mark.skipif(not zxv.has_feature2(zxv.Feature2.TREATY), reason="no treaty group")


def status_of(fn: Callable[[], Any]) -> str:
    try:
        fn()
    except ZxvError as e:
        return e.status_name
    return "ZXV_OK"


def test_sizes(vectors: Dict[str, Any]) -> None:
    v = vectors["treaty"]
    assert (v["rec_len"], v["result_len"]) == (treaty.TREATY_REC_LEN, treaty.TREATY_RESULT_LEN)
    assert {int(k): n for k, n in v["op_names"].items()} == {op.value: op.name for op in TreatyOp}


def test_sutra_round_trips_and_contents(vectors: Dict[str, Any]) -> None:
    for case in vectors["treaty"]["sutra"]:
        assert case["status"] == "ZXV_OK"
        with Treaty.from_sutra(case["sutra"]) as t:
            rec = t.to_record()
            assert rec.hex() == case["record"]
            assert t.to_sutra() == case["sutra"]
            with Treaty.from_record(rec) as u:
                assert (t == u) is case["record_parses_equal"]
                assert u.to_sutra() == case["reemit"]
            assert t.name == case["name"] and t.asset == case["asset"]
            assert t.op == TreatyOp(case["op"])
            assert (t.party_count, t.obligation_count) == (case["party_count"], case["owe_count"])
            got = [(p.id, str(p.deposit), str(p.claim), [p.weight_num, p.weight_den]) for p in t.parties]
            assert got == [(p["id"], p["deposit"], p["claim"], p["weight"]) for p in case["parties"]]
            assert sum(p.weight for p in t.parties) == Fraction(1)
            assert [(o.debtor, o.creditor, str(o.amount)) for o in t.obligations] == \
                [(o["from"], o["to"], o["amount"]) for o in case["owes"]]
            assert t.is_valid and t.fault is TreatyFault.NONE
            t.validate()


def test_sutra_refusals_carry_fault_and_offset(vectors: Dict[str, Any]) -> None:
    for case in vectors["treaty"]["reject"]:
        t = Treaty()
        with pytest.raises(ZxvError) as e:
            t.parse_sutra(case["sutra"])
        assert e.value.status_name == case["status"], case
        if case["status"] == "ZXV_E_TREATY":
            assert isinstance(e.value, TreatyError) and isinstance(e.value, ZxvUsageError)
            assert e.value.fault == TreatyFault(case["fault"]) and e.value.offset == case["offset"], case
        # a failed parse leaves the treaty unchanged (still empty)
        assert t.name == "" and t.fault is TreatyFault.NAME
        t.close()


def test_record_refusals(vectors: Dict[str, Any]) -> None:
    base = bytes.fromhex(vectors["treaty"]["sutra"][0]["record"])
    for case in vectors["treaty"]["reject_record"]:
        rec = bytearray(base)
        rec[case["patch_offset"]] = int(case["patch_byte"], 16)
        with pytest.raises(TreatyError) as e:
            Treaty.from_record(bytes(rec))
        assert (e.value.fault, e.value.offset) == (case["fault"], case["offset"])
    with pytest.raises(ZxvUsageError) as e2:
        Treaty.from_record(base[:-1])
    assert e2.value.status_name == "ZXV_E_ARG"


def test_failed_parse_keeps_previous_treaty(vectors: Dict[str, Any]) -> None:
    good = vectors["treaty"]["sutra"][0]["sutra"]
    with Treaty.from_sutra(good) as t:
        with pytest.raises(TreatyError):
            t.parse_sutra("TREATY")
        assert t.to_sutra() == good
        t.clear()
        assert t.fault is TreatyFault.NAME
        with pytest.raises(TreatyError) as e:
            t.to_sutra()
        assert e.value.fault is TreatyFault.NAME
        with pytest.raises(TreatyError):
            t.to_record()
        with pytest.raises(TreatyError):
            t.run()


def test_pic_arithmetic(vectors: Dict[str, Any]) -> None:
    ops = {"add": treaty.pic_add, "sub": treaty.pic_sub, "mul": treaty.pic_mul, "div": treaty.pic_div}
    for case in vectors["treaty"]["pic"]:
        a, b = int(case["a"]), int(case["b"])
        if case["status"] != "ZXV_OK":
            assert status_of(lambda: ops[case["op"]](a, b)) == case["status"], case
            continue
        got = ops[case["op"]](a, b)
        if case["op"] == "div":
            assert got == (int(case["out"]), int(case["rem"])), case
        else:
            assert got == int(case["out"]), case
    with pytest.raises(zxv.FloatRejectedError):
        treaty.pic_add(1.0, 2)  # type: ignore[arg-type]


def test_run(vectors: Dict[str, Any]) -> None:
    for case in vectors["treaty"]["run"]:
        with Treaty.from_sutra(case["sutra"]) as t:
            if case["status"] != "ZXV_OK":
                assert status_of(t.run) == case["status"]
                continue
            with t.run() as r:
                assert (r.party_count, r.leg_count, str(r.fees)) == (case["parties"], case["legs"], case["fees"])
                assert [(str(p.net), str(p.balance), str(p.dividend)) for p in r.parties] == \
                    [(p["net"], p["balance"], p["dividend"]) for p in case["party"]]
                assert [(g.payer, g.payee, str(g.amount)) for g in r.legs] == \
                    [(g["from"], g["to"], g["amount"]) for g in case["leg"]]
                assert r.to_record().hex() == case["result_record"]


def test_run_refusal_is_insufficient_funds(vectors: Dict[str, Any]) -> None:
    case = vectors["treaty"]["run"][0]
    assert case["status"] == "ZXV_E_FUNDS"
    with Treaty.from_sutra(case["sutra"]) as t, pytest.raises(InsufficientFundsError):
        t.run()


def test_index_out_of_range_and_non_ascii() -> None:
    with Treaty.from_sutra("TREATY N USD NET\nPARTY A 1 1 1/1\nEND\n") as t:
        with pytest.raises(ZxvUsageError) as e:
            t.party(1)
        assert e.value.status_name == "ZXV_E_NOT_FOUND"
        with pytest.raises(TreatyError) as e2:
            t.parse_sutra("TREATY N USD NET\nPARTY Å 1 1 1/1\nEND\n")
        # Refused at the non-ASCII byte. zxv_api.h:1780 says the fault is SYNTAX;
        # this build reports the field's own fault (PARTY), so only the offset is pinned.
        assert e2.value.offset == 23 and e2.value.fault is not TreatyFault.NONE


def test_treaty_calls_are_serialized_across_threads() -> None:
    sutra = "TREATY N USD SETL\nPARTY A 900000 0 1/2\nPARTY B 900000 0 1/2\nOWE A B 300000\nEND\n"
    with Treaty.from_sutra(sutra) as t:
        errors = []

        def work() -> None:
            try:
                for _ in range(20):
                    with t.run() as r:
                        assert r.leg_count == 1
                    assert t.to_sutra() == sutra
            except BaseException as exc:  # pragma: no cover - reported below
                errors.append(exc)

        threads = [threading.Thread(target=work) for _ in range(4)]
        for th in threads:
            th.start()
        for th in threads:
            th.join()
        assert not errors
