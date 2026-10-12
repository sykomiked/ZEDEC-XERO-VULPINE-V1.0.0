# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""ABI 1.5 coin (Fibonacci coins, AI tokens, token wallet) and fee groups
against the conformance vectors."""

from __future__ import annotations

from typing import Any, Callable, Dict, List

import pytest

import zxv
from zxv import (Cap, DenominationError, InsufficientFundsError, Octane, TokenHolding, TokenWallet, ZxvError,
                 ZxvUsageError, coin, fee)

pytestmark = pytest.mark.skipif(not zxv.has_feature2(zxv.Feature2.COIN | zxv.Feature2.FEE),
                                reason="no coin/fee group")


def status_of(fn: Callable[[], Any]) -> str:
    try:
        fn()
    except ZxvError as e:
        return e.status_name
    return "ZXV_OK"


def holding(sparse: List[List[Any]]) -> TokenHolding:
    return TokenHolding({int(t): int(n) for t, n in sparse})


def sparse(h: TokenHolding) -> List[List[Any]]:
    return [[t, str(n)] for t, n in sorted(h.nonzero().items())]


# ---------------------------------------------------------------- VFV coins


def test_ladder_constants(vectors: Dict[str, Any]) -> None:
    v = vectors["coin"]
    assert v["rungs"] == coin.COIN_RUNGS and int(v["top_face"]) == coin.COIN_TOP_FACE
    assert coin.face(coin.COIN_RUNGS) == coin.COIN_TOP_FACE


def test_face_rung_split(vectors: Dict[str, Any]) -> None:
    v = vectors["coin"]
    for case in v["face"]:
        if case["status"] == "ZXV_OK":
            assert coin.face(case["rung"]) == int(case["face"])
        else:
            assert status_of(lambda: coin.face(case["rung"])) == case["status"]
    for case in v["rung_of"]:
        if case["status"] == "ZXV_OK":
            assert coin.rung(int(case["face"])) == case["rung"]
        else:
            with pytest.raises(DenominationError):
                coin.rung(int(case["face"]))
            assert status_of(lambda: coin.rung(int(case["face"]))) == case["status"]
    for case in v["split"]:
        a = int(case["amount"])
        if case["status"] != "ZXV_OK":
            assert status_of(lambda: coin.split_count(a)) == case["status"]
            continue
        assert coin.split_count(a) == int(case["count"])
        if "rungs" in case:
            got = coin.split(a)
            assert got == case["rungs"]
            assert sum(coin.face(r) for r in got) == a


# ---------------------------------------------------------------- AI tokens


def test_token_faces_and_tiers(vectors: Dict[str, Any]) -> None:
    v = vectors["coin"]
    for case in v["token_face"]:
        if case["status"] == "ZXV_OK":
            f = coin.token_face(case["tier"])
            assert (str(f.face), f.is_super) == (case["face"], case["super"])
        else:
            assert status_of(lambda: coin.token_face(case["tier"])) == case["status"]
    for case in v["token_tier"]:
        if case["status"] == "ZXV_OK":
            assert coin.token_tier(int(case["face"])) == case["tier"]
        else:
            assert status_of(lambda: coin.token_tier(int(case["face"]))) == case["status"]


def test_token_value_canonical_compress(vectors: Dict[str, Any]) -> None:
    v = vectors["coin"]
    for case in v["token_value"]:
        h = holding(case["counts"])
        if case["status"] == "ZXV_OK":
            assert (str(h.value), str(h.pieces)) == (case["value"], case["pieces"])
        else:
            assert status_of(lambda: h.value) == case["status"]
    for case in v["token_canonical"]:
        if case["status"] != "ZXV_OK":
            assert status_of(lambda: TokenHolding.canonical(int(case["value"]), case["octane"])) == case["status"]
            continue
        h = TokenHolding.canonical(int(case["value"]), case["octane"])
        assert sparse(h) == case["counts"] and str(h.pieces) == case["pieces"]
        assert h.value == int(case["value"]) and h.is_canonical(case["octane"])
    for case in v["token_is_canonical"]:
        assert holding(case["counts"]).is_canonical(case["octane"]) is case["canonical"]
    for case in v["token_compress"]:
        h = holding(case["counts"])
        out = h.compressed(case["octane"])
        assert sparse(out) == case["out"] and out.value == h.value  # never changes the value


def test_token_fuse_and_split(vectors: Dict[str, Any]) -> None:
    for case in vectors["coin"]["token_fuse_split"]:
        h = holding(case["counts"])

        def run() -> TokenHolding:
            if case["op"] == "fuse":
                return h.fused(case["octane"], case["tier"])
            return h.split(case["tier"])
        if case["status"] == "ZXV_OK":
            out = run()
            assert sparse(out) == case["out"], case
            assert out.value == h.value
        else:
            assert status_of(run) == case["status"], case
            assert sparse(h) == case["out"]  # unchanged
    with pytest.raises(InsufficientFundsError):
        TokenHolding({0: 1}).fused(4, 0)


def test_fibonacci_codes(vectors: Dict[str, Any]) -> None:
    v = vectors["coin"]
    for case in v["token_code"]:
        val = int(case["value"])
        if case["status"] != "ZXV_OK":
            assert status_of(lambda: coin.code_bits(val)) == case["status"]
            continue
        assert coin.code_bits(val) == case["bits"]
        data, bits = coin.code_encode([val])
        assert bits == case["bits"] and data.hex() == case["bytes"]
        assert "".join(str(data[i // 8] >> (i % 8) & 1) for i in range(bits)) == case["code"]
        assert coin.code_get(data) == (val, bits)
    s = v["token_code_stream"]
    vals = [int(x) for x in s["values"]]
    data, bits = coin.code_encode(vals)
    assert (data.hex(), bits) == (s["bytes"], s["bits"])
    assert coin.code_decode(data) == vals
    assert coin.code_decode(data, count=2) == vals[:2]
    for case in v["token_code_get_reject"]:
        assert status_of(lambda: coin.code_get(bytes.fromhex(case["bytes"]), case["bit"])) == case["status"]


def test_code_put_does_not_fit() -> None:
    buf = bytearray(1)
    with pytest.raises(ZxvUsageError) as e:
        coin.code_put(buf, 0, 100)  # 11 bits
    assert e.value.status_name == "ZXV_E_BUFFER" and buf == bytearray(1)
    assert coin.code_put(buf, 0, 0) == 2 and buf == bytearray(b"\x03")


def test_holding_is_a_value() -> None:
    a = TokenHolding({0: 3, 4: 2})
    assert a == TokenHolding([3, 0, 0, 0, 2]) and hash(a) == hash(TokenHolding([3, 0, 0, 0, 2]))
    with pytest.raises(AttributeError):
        a._c = ()  # type: ignore[misc]
    with pytest.raises(zxv.FloatRejectedError):
        TokenHolding({0: 1.5})  # type: ignore[dict-item]


# ---------------------------------------------------------------- wallet


def test_wallet_mint_spend_transfer_octane() -> None:
    with zxv.Context() as ctx:
        cap = ctx.root_capability(Cap.ALL_1_5)
        with TokenWallet(cap, 7, Octane.RACING) as a, TokenWallet(cap, 8, Octane.PLAIN) as b:
            a.mint(1_000_000)
            info = a.info
            assert (info.model_id, info.octane, info.value, info.pieces) == (7, Octane.RACING, 1_000_000, 17)
            assert a.holding == TokenHolding.canonical(1_000_000, Octane.RACING)
            a.transfer(b, 1000)
            assert (a.value, b.value) == (999_000, 1000)
            assert b.holding.pieces == 1000  # the receiver gets it at ITS octane
            assert a.holding.is_canonical(Octane.RACING)
            with pytest.raises(InsufficientFundsError):
                b.spend(1001)
            assert b.value == 1000
            b.set_octane(Octane.MIDGRADE)
            assert b.value == 1000 and b.holding.pieces < 1000 and b.octane == Octane.MIDGRADE
            b.spend(1000)
            assert b.value == 0
            with pytest.raises(ZxvUsageError) as e:
                a.mint(2**64 - 1)
            assert e.value.status_name == "ZXV_E_RANGE" and a.value == 999_000


def test_wallets_of_another_context_are_refused() -> None:
    with zxv.Context() as c1, zxv.Context() as c2:
        k1, k2 = c1.root_capability(Cap.ALL_1_5), c2.root_capability(Cap.ALL_1_5)
        with TokenWallet(k1) as a, TokenWallet(k2) as b:
            a.mint(10)
            with pytest.raises(zxv.CapabilityDeniedError):
                a.transfer(b, 5)
            with pytest.raises(zxv.CapabilityDeniedError):
                a.mint(1, cap=k2)
            assert (a.value, b.value) == (10, 0)


# ---------------------------------------------------------------- fee


def test_fee_assure_carry_split_policy(vectors: Dict[str, Any]) -> None:
    v = vectors["fee"]
    assert (v["num"], v["den"]) == (fee.FEE_NUM, fee.FEE_DEN)
    for case in v["assure"]:
        assert fee.assure(int(case["gross"])) == int(case["fee"])
    for seq in v["carry"]:
        acct = fee.FeeAccount(int(seq["rem_in"]))
        rem = int(seq["rem_in"])
        for step in seq["steps"]:
            c = fee.assure_carry(rem, int(step["gross"]))
            assert (str(c.fee), str(c.remainder)) == (step["fee"], step["rem"])
            rem = c.remainder
            assert acct.charge(int(step["gross"])) == c.fee and acct.remainder == rem
    for case in v["carry_reject"]:
        assert status_of(lambda: fee.assure_carry(int(case["rem_in"]), int(case["gross"]))) == case["status"]
    with pytest.raises(ZxvUsageError):
        fee.FeeAccount(fee.FEE_DEN)
    assert [b.name.lower() for b in fee.FeeBucket] == v["buckets"]
    for case in v["split"]:
        s = fee.split(int(case["fee"]))
        assert [str(p) for p in s] == case["parts"] and s.total == int(case["fee"])
        assert s[fee.FeeBucket.RESERVE_FLOOR] == s.reserve_floor
    p = fee.default_policy()
    d = v["policy_default"]
    assert list(p.bucket_percent) == d["pct"] and int(p.allow_below) == d["allow_below"]
    assert [[str(n), str(m)] for n, m in p.credit_rate] == d["rate"]
    assert [str(x) for x in p.credit_multiplier] == d["multiplier"]
    assert [str(x) for x in p.commons_cap] == d["commons_cap"]


def test_fee_has_no_floats() -> None:
    with pytest.raises(zxv.FloatRejectedError):
        fee.assure(100.0)  # type: ignore[arg-type]
