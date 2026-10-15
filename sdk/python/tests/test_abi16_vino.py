# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""ABI 1.6 Vino Embed: every ``vino`` conformance vector replayed through the
binding (requests, decode refusals, the fixed-seed receipt, the play scenario
and the meter scenario), plus statuses and exception types, capability bits,
handle lifetimes, the vouchers buffer path and a poisoned context."""

from __future__ import annotations

import re
import threading
from typing import Any, Callable, Dict, List, Optional

import pytest

import zxv
from conftest import HEADER
from zxv import (Cap, CapabilityDeniedError, ChanceOutcomeError, CoverageError, LaneError, LimitRefusedError,
                 RailError, RequestExpiredError, StatusClass, ZxvError, ZxvFatal, ZxvRefusedError, ZxvUsageError,
                 _native)
from zxv.vino import (PaymentRequest, PlayAccount, Receipt, RequestFlag, SpendMeter, VinoLane, VinoPlay,
                      VinoRail, Voucher, VoucherState)

EXPECTED_TYPES = {-46: RailError, -47: CoverageError, -48: LaneError, -49: RequestExpiredError,
                  -50: ChanceOutcomeError}


def _status(fn: Callable[[], Any]) -> str:
    """Run ``fn`` and return the status name it ended with ("ZXV_OK" on success)."""
    try:
        fn()
    except ZxvError as e:
        return e.status_name
    return "ZXV_OK"


def _request(v: Dict[str, Any]) -> PaymentRequest:
    return PaymentRequest(v["lane"], bytes.fromhex(v["merchant"]), bytes.fromhex(v["nonce"]), int(v["amount"]),
                          int(v["created_at"]), int(v["expires_at"]), v["memo"], v["flags"])


# ---------------------------------------------------------------- constants and statuses


def test_feature_cap_and_constants_match_header() -> None:
    text = HEADER.read_text(encoding="utf-8")
    assert zxv.abi_version()[:2] == (1, 6)
    assert zxv.has_feature2(zxv.Feature2.VINO)
    assert re.search(r"#define ZXV_FEAT2_VINO 0x00000100u", text) and int(zxv.Feature2.VINO) == 0x100
    assert re.search(r"#define ZXV_CAP_VINO \(UINT64_C\(1\) << 15\)", text) and Cap.VINO == 1 << 15
    assert re.search(r"#define ZXV_CAP_ALL_1_6 \(\(UINT64_C\(1\) << 16\) - 1\)", text)
    assert Cap.ALL_1_6 == Cap.ALL_1_5 | Cap.VINO == (1 << 16) - 1
    assert Cap.ALL_1_5 == (1 << 15) - 1 and Cap.ALL == (1 << 10) - 1  # older constants unchanged
    defines = dict(re.findall(r"#define\s+(ZXV_VINO_[A-Z_]+)\s+\(?(?:UINT64_C\(1\) << )?(0x[0-9a-fA-F]+|\d+)u?",
                              text))
    v = zxv.vino
    assert int(defines["ZXV_VINO_RAIL_BEARER"]) == VinoRail.BEARER
    assert int(defines["ZXV_VINO_RAIL_GHOST"]) == VinoRail.GHOST
    assert int(defines["ZXV_VINO_LANE_PLAY"]) == VinoLane.PLAY
    assert int(defines["ZXV_VINO_LANE_LIVE"]) == VinoLane.LIVE
    assert int(defines["ZXV_VINO_V_ACTIVE"]) == VoucherState.ACTIVE
    assert int(defines["ZXV_VINO_V_REDEEMED"]) == VoucherState.REDEEMED
    assert int(defines["ZXV_VINO_MAX_ACCOUNTS"]) == v.VINO_MAX_ACCOUNTS
    assert int(defines["ZXV_VINO_MAX_VOUCHERS"]) == v.VINO_MAX_VOUCHERS
    assert 1 << int(defines["ZXV_VINO_AMOUNT_MAX"]) == v.VINO_AMOUNT_MAX
    assert int(defines["ZXV_VINO_REQ_BYTES"]) == v.VINO_REQ_BYTES
    assert int(defines["ZXV_VINO_MEMO_MAX"]) == v.VINO_MEMO_MAX
    assert int(defines["ZXV_VINO_RCPT_MSG_BYTES"]) == v.VINO_RCPT_MSG_BYTES
    assert int(defines["ZXV_VINO_REQ_CHANCE"], 0) == RequestFlag.CHANCE


def test_cap_bit_15_is_now_accepted_and_16_refused() -> None:
    with zxv.Context() as ctx:
        assert ctx.root_capability(Cap.VINO).caps == Cap.VINO
        assert ctx.root_capability(Cap.ALL_1_6).caps == Cap.ALL_1_6
        with pytest.raises(ZxvUsageError) as e:
            ctx.root_capability(Cap.ALL_1_6 + 1)
        assert e.value.status_name == "ZXV_E_ARG"


def test_new_statuses_names_classes_and_exception_types(vectors: Dict[str, Any]) -> None:
    rows: List[Dict[str, Any]] = vectors["status_abi16"]
    assert [r["code"] for r in rows] == list(range(-46, -51, -1))
    for r in rows:
        code = r["code"]
        assert zxv.status_name(code) == r["name"]
        assert zxv.status_class(code) == StatusClass(r["class"])
        exc = _native.make_error(code, "detail")
        assert type(exc) is EXPECTED_TYPES[code], code
        assert isinstance(exc, ZxvUsageError if r["class"] == 1 else ZxvRefusedError)
        assert exc.status == code and exc.status_name == r["name"] and exc.detail == "detail"
    for code in range(-51, -60, -1):  # reserved for the group: still an error type
        assert isinstance(_native.make_error(code, "x"), (ZxvError, ZxvFatal))


# ---------------------------------------------------------------- requests


def test_request_vectors(vectors: Dict[str, Any]) -> None:
    rows = vectors["vino"]["requests"]
    assert len(rows) >= 16
    for v in rows:
        req = _request(v)
        assert _status(req.encode) == v["status"], v
        if v["status"] != "ZXV_OK":
            continue
        data = req.encode()
        assert data.hex() == v["bytes"]
        assert req.id.hex() == v["id"]
        back = PaymentRequest.decode(data)
        assert back == req
        assert isinstance(back.lane, VinoLane) and isinstance(back.flags, RequestFlag)
        assert back.amount == int(v["amount"]) and back.expires_at == int(v["expires_at"])


def test_request_typed_refusals() -> None:
    base = dict(merchant=bytes(32), nonce=bytes(16), amount=5, created_at=1, expires_at=2)
    with pytest.raises(ChanceOutcomeError):
        PaymentRequest(VinoLane.LIVE, flags=RequestFlag.CHANCE, **base).encode()  # type: ignore[arg-type]
    # The same chance flag is fine on the play lane.
    assert len(PaymentRequest(VinoLane.PLAY, flags=RequestFlag.CHANCE, **base).encode()) == 160  # type: ignore[arg-type]
    with pytest.raises(LaneError):
        PaymentRequest(0, **base).encode()  # type: ignore[arg-type]
    with pytest.raises(ZxvUsageError, match="merchant"):
        PaymentRequest(VinoLane.LIVE, bytes(31), bytes(16), 5, 1, 2).encode()
    with pytest.raises(ZxvUsageError, match="NUL"):
        PaymentRequest(VinoLane.LIVE, bytes(32), bytes(16), 5, 1, 2, "a\0b").encode()
    with pytest.raises(ZxvUsageError) as e:
        PaymentRequest(VinoLane.LIVE, bytes(32), bytes(16), 5, 1, 2, "café").encode()  # not ASCII
    assert e.value.status_name == "ZXV_E_FORMAT"
    with pytest.raises(zxv.FloatRejectedError):
        PaymentRequest(VinoLane.LIVE, bytes(32), bytes(16), 5.0, 1, 2).encode()  # type: ignore[arg-type]
    with pytest.raises(ZxvUsageError, match="exactly 160"):
        PaymentRequest.decode(bytes(159))


def test_decode_vectors(vectors: Dict[str, Any]) -> None:
    good = bytes.fromhex(vectors["vino"]["requests"][0]["bytes"])
    assert PaymentRequest.decode(good).memo == "Sword of Dawn"
    for v in vectors["vino"]["decode"]:
        bad = bytearray(good)
        bad[v["offset"]] = v["value"]
        assert _status(lambda: PaymentRequest.decode(bytes(bad))) == v["status"], v


# ---------------------------------------------------------------- receipts


def test_receipt_vector(vectors: Dict[str, Any]) -> None:
    rc = vectors["vino"]["receipt"]
    request = bytes.fromhex(vectors["vino"]["requests"][0]["bytes"])
    req_id = bytes.fromhex(vectors["vino"]["requests"][0]["id"])
    with zxv.Context() as ctx:
        cap = ctx.root_capability(Cap.ALL_1_6)
        dsa = zxv.MlDsa87(cap)
        with dsa.generate_from_seed(zxv.SecretKey.from_bytes(bytes.fromhex(rc["seed"]))) as kp:
            assert zxv.Hashes.sha3_256(kp.public_key).hex() == rc["pk_sha3_256"]
            approved = int(rc["approved_at"])
            msg = Receipt.message(req_id, kp.public_key, approved)
            assert msg.hex() == rc["msg"]
            sig = dsa.sign_deterministic(kp.secret_key, msg)
            assert sig.hex() == rc["sig"]
            for c in rc["checks"]:
                s = bytearray(sig)
                if c["flip_sig"] >= 0:
                    s[c["flip_sig"]] ^= 1
                r = Receipt(request, kp.public_key, int(c["approved_at"]), bytes(s))
                assert _status(lambda: r.verify(int(c["now"]))) == c["status"], c
                assert r.is_valid(int(c["now"])) == (c["status"] == "ZXV_OK")

            # The wallet-side signer (hedged) produces a receipt that verifies.
            signed = Receipt.sign(cap, PaymentRequest.decode(request), kp.secret_key, kp.public_key, approved)
            assert signed.request == request and signed.approved_at == approved
            signed.verify(approved + 1)
            assert signed.payment_request.id == req_id
            with pytest.raises(RequestExpiredError):
                Receipt.sign(cap, request, kp.secret_key, kp.public_key, int(vectors["vino"]["requests"][0]
                                                                              ["expires_at"]) + 1)
            # Signing is the wallet's call: it needs CRYPTO_SIGN, not VINO.
            no_sign = ctx.root_capability(Cap.ALL_1_6 & ~Cap.CRYPTO_SIGN)
            with pytest.raises(CapabilityDeniedError):
                Receipt.sign(no_sign, request, kp.secret_key, kp.public_key, approved)


def test_receipt_rejects_wrong_lengths() -> None:
    with pytest.raises(ZxvUsageError):
        Receipt(bytes(160), bytes(2591), 0, bytes(4627))
    with pytest.raises(ZxvUsageError):
        Receipt.message(bytes(31), bytes(2592), 0)


# ---------------------------------------------------------------- play scenario


def _run_step(play: VinoPlay, s: Dict[str, Any]) -> Any:
    op = s["op"]
    if op == "open":
        return play.open_account(s["owner"], s["is_issuer"] if s["is_issuer"] not in (0, 1)
                                 else bool(s["is_issuer"]))
    if op == "back":
        return play.back(s["issuer"], int(s["amount"]))
    if op == "unback":
        return play.unback(s["issuer"], int(s["amount"]))
    if op == "issue":
        return play.issue(s["issuer"], s["holder"], int(s["face"]), VinoRail(s["rail"]))
    if op == "transfer":
        return play.transfer(int(s["voucher"]), s["from"], s["to"], VinoRail(s["rail"]))
    if op == "set_rail":
        return play.set_rail(int(s["voucher"]), s["holder"], VinoRail(s["rail"]))
    if op == "redeem":
        return play.redeem(int(s["voucher"]), s["holder"])
    raise AssertionError(f"unknown op {op}")


def test_play_scenario(vectors: Dict[str, Any]) -> None:
    sc = vectors["vino"]["play"]
    with zxv.Context() as ctx, VinoPlay(ctx.root_capability(Cap.VINO)) as play:
        for i, s in enumerate(sc["steps"]):
            result: List[Any] = []
            status = _status(lambda: result.append(_run_step(play, s)))
            assert status == s["status"], (i, s)
            if status == "ZXV_OK" and s["op"] == "open":
                assert result[0] == s["account"]
            if status == "ZXV_OK" and s["op"] == "issue":
                assert result[0] == int(s["id"])
        for a in sc["accounts"]:
            assert play.account(a["account"]) == PlayAccount(a["account"], a["owner"], bool(a["is_issuer"]),
                                                             int(a["backing"]), int(a["faces"]), a["count"])
            assert play.vouchers(a["account"]) == [int(x) for x in a["vouchers"]]
        for v in sc["vouchers"]:
            assert play.voucher(int(v["id"])) == Voucher(int(v["id"]), int(v["face"]), v["issuer"], v["holder"],
                                                         VinoRail(v["rail"]), VoucherState(v["state"]))
        play.check()


def test_typed_play_errors_and_redeem_pays_nothing() -> None:
    with zxv.Context() as ctx, VinoPlay(ctx.root_capability(Cap.VINO)) as play:
        shop, alice, bob = play.open_issuer(1), play.open_holder(2), play.open_holder(3)
        play.back(shop, 18)
        with pytest.raises(CoverageError):
            play.issue(shop, alice, 13)  # 10*18 < 18*13
        vid = play.issue(shop, alice, 8, VinoRail.GHOST)
        with pytest.raises(RailError):
            play.transfer(vid, alice, bob, VinoRail.BEARER)
        with pytest.raises(RailError):
            play.set_rail(vid, alice, VinoRail.GHOST)  # already there
        with pytest.raises(zxv.DenominationError):
            play.issue(shop, alice, 4)
        with pytest.raises(zxv.InsufficientFundsError):
            play.unback(shop, 19)
        play.transfer(vid, alice, bob, VinoRail.GHOST)
        play.redeem(vid, bob)
        assert play.account(shop).faces == 0 and play.account(shop).backing == 18  # backing unchanged
        assert play.account(bob).faces == 0  # nothing paid out to the holder
        assert play.voucher(vid).state is VoucherState.REDEEMED
        with pytest.raises(ZxvRefusedError) as e:
            play.redeem(vid, bob)
        assert e.value.status_name == "ZXV_E_STATE"


def test_vouchers_buffer_grows_past_the_first_guess() -> None:
    with zxv.Context() as ctx, VinoPlay(ctx.root_capability(Cap.VINO)) as play:
        shop, alice, bob = play.open_issuer(1), play.open_holder(2), play.open_holder(3)
        assert play.vouchers(alice) == []  # count 0
        play.back(shop, 1000)
        ids = [play.issue(shop, alice, 1) for _ in range(40)]  # more than the first 16-slot buffer
        assert play.vouchers(alice) == ids == sorted(ids)
        assert play.vouchers(shop) == ids
        play.transfer(ids[3], alice, bob, VinoRail.BEARER)
        assert play.vouchers(bob) == [ids[3]]
        assert play.vouchers(alice) == ids[:3] + ids[4:]
        with pytest.raises(ZxvUsageError) as e:
            play.vouchers(99)
        assert e.value.status_name == "ZXV_E_NOT_FOUND"


# ---------------------------------------------------------------- meter


def test_meter_scenario(vectors: Dict[str, Any]) -> None:
    sc = vectors["vino"]["meter"]
    with zxv.Context() as ctx, SpendMeter(ctx.root_capability(Cap.VINO), int(sc["daily_cap"])) as m:
        assert m.daily_cap == int(sc["daily_cap"])
        for i, s in enumerate(sc["steps"]):
            left: Optional[int]
            try:
                left = m.admit(int(s["day"]), int(s["amount"]))
                status = "ZXV_OK"
            except LimitRefusedError as e:
                status, left = e.status_name, e.left
            except ZxvError as e:
                status, left = e.status_name, None
            assert status == s["status"], (i, s)
            if "left" in s:
                assert left == int(s["left"]), (i, s)


def test_meter_create_refusals() -> None:
    with zxv.Context() as ctx:
        cap = ctx.root_capability(Cap.VINO)
        with pytest.raises(ZxvUsageError):
            SpendMeter(cap, 0)
        with pytest.raises(ZxvUsageError):
            SpendMeter(cap, zxv.vino.VINO_AMOUNT_MAX + 1)


# ---------------------------------------------------------------- capabilities, lifetimes, poison


@pytest.mark.parametrize("make", [lambda cap: VinoPlay(cap), lambda cap: SpendMeter(cap, 10)])
def test_handles_need_cap_vino(make: Any) -> None:
    with zxv.Context() as ctx:
        with pytest.raises(CapabilityDeniedError) as e:
            make(ctx.root_capability(Cap.ALL_1_5))  # every bit but VINO
        assert e.value.status == -17


def test_each_call_honours_a_weaker_cap() -> None:
    with zxv.Context() as ctx:
        full = ctx.root_capability(Cap.ALL_1_6)
        weak = full.attenuate(Cap.ALL_1_6 & ~Cap.VINO)
        with VinoPlay(full) as play, SpendMeter(full, 10) as m:
            calls: List[Callable[[], Any]] = [
                lambda: play.open_issuer(1, cap=weak), lambda: play.vouchers(0, cap=weak),
                lambda: play.check(cap=weak), lambda: m.admit(1, 1, cap=weak)]
            for call in calls:
                with pytest.raises(CapabilityDeniedError):
                    call()
            play.open_issuer(1)
            full.revoke()
            with pytest.raises(CapabilityDeniedError):
                play.account(0)
            with pytest.raises(CapabilityDeniedError):
                m.admit(1, 1)


def test_pure_calls_need_no_cap() -> None:
    req = PaymentRequest(VinoLane.LIVE, bytes(32), bytes(16), 7, 10, 20)
    assert PaymentRequest.decode(req.encode()) == req
    assert len(Receipt.message(req.id, bytes(2592), 15)) == 88


def test_use_after_close_is_refused() -> None:
    with zxv.Context() as ctx:
        cap = ctx.root_capability(Cap.VINO)
        play, meter = VinoPlay(cap), SpendMeter(cap, 10)
        play.open_issuer(1)
        meter.admit(1, 1)
        for h, probe in ((play, lambda: play.account(0)), (meter, lambda: meter.admit(1, 1))):
            h.close()
            h.close()  # idempotent
            assert h.closed
            with pytest.raises(ZxvUsageError, match="closed"):
                probe()
        # Closing the capability closes the handles made from it.
        cap2 = ctx.root_capability(Cap.VINO)
        p2, m2 = VinoPlay(cap2), SpendMeter(cap2, 5)
        cap2.close()
        assert p2.closed and m2.closed


def test_poisoned_context_refuses_play_and_meter() -> None:
    ctx = zxv.Context()
    cap = ctx.root_capability(Cap.ALL_1_6)
    play, meter = VinoPlay(cap), SpendMeter(cap, 10)
    play.open_issuer(1)
    # Halt through C directly so the SDK's own dead flag is not what refuses.
    _native.lib().zxv_ctx_halt(ctx._h, b"test")
    out = _native.ffi.new("uint32_t *")
    assert _native.lib().zxv_vino_play_open(cap._h, play._h, 1, 0, out) == -18
    with pytest.raises(ZxvFatal) as e:
        play.open_holder(2)
    assert e.value.status == -18
    for call in (lambda: play.account(0), lambda: play.check(), lambda: meter.admit(1, 1),
                 lambda: VinoPlay(cap)):
        with pytest.raises(ZxvFatal):
            call()
    assert ctx.dead
    ctx.close()


def test_play_is_usable_from_several_threads() -> None:
    with zxv.Context() as ctx, VinoPlay(ctx.root_capability(Cap.VINO)) as play:
        shop, alice = play.open_issuer(1), play.open_holder(2)
        play.back(shop, 10_000)
        errors: List[BaseException] = []

        def work() -> None:
            try:
                for _ in range(50):
                    play.issue(shop, alice, 1)
            except BaseException as e:  # pragma: no cover - reported below
                errors.append(e)

        threads = [threading.Thread(target=work) for _ in range(4)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        assert not errors
        assert len(play.vouchers(alice)) == 200 and play.account(shop).faces == 200
        play.check()
