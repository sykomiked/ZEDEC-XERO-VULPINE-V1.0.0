# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""ABI 1.5 cross-cutting behaviour: version, features2, statuses and their
exception types, capability bits, handle lifetimes, the buffer-growth path,
capability refusals and fail-closed (poisoned) contexts."""

from __future__ import annotations

import re
from typing import Any, Dict, List

import pytest

import zxv
from conftest import HEADER
from zxv import (Cap, CapabilityDeniedError, CidMismatchError, ConsentRequiredError, DenominationError,
                 ErasureError, LimitRefusedError, OwnerApprovalRequiredError, ParadoxStepLimitError, PegError,
                 StatusClass, TreatyError, ZxvFatal, ZxvRefusedError, ZxvUsageError, _native)

EXPECTED_TYPES = {
    -32: DenominationError, -33: TreatyError, -34: CidMismatchError, -35: ConsentRequiredError,
    -36: ErasureError, -37: PegError, -38: LimitRefusedError, -39: ParadoxStepLimitError,
    -40: OwnerApprovalRequiredError,
}


def test_version_and_features2() -> None:
    assert zxv.abi_version()[:2] == (1, 5)
    assert zxv.__version__ == "1.5.0"
    f2 = zxv.features2()
    assert isinstance(f2, zxv.Feature2)
    for flag in zxv.Feature2:
        if flag is not zxv.Feature2.NONE:
            assert zxv.has_feature2(flag), flag  # the in-tree build has every group


def test_feature2_and_cap_constants_match_header() -> None:
    text = HEADER.read_text(encoding="utf-8")
    for flag in zxv.Feature2:
        if flag.name == "NONE":
            continue
        m = re.search(rf"#define ZXV_FEAT2_{flag.name}\s+(0x[0-9A-Fa-f]+)u", text)
        assert m and int(m.group(1), 16) == flag.value, flag
    for c in (Cap.AIPI, Cap.MINT, Cap.PARADOX):
        m = re.search(rf"#define ZXV_CAP_{c.name}\s+\(UINT64_C\(1\) << (\d+)\)", text)
        assert m and 1 << int(m.group(1)) == c.value, c
    assert re.search(r"#define ZXV_CAP_ALL_1_5 \(\(UINT64_C\(1\) << 15\) - 1\)", text)
    assert Cap.ALL_1_5 == Cap.ALL_1_4 | Cap.AIPI | Cap.MINT | Cap.PARADOX == (1 << 15) - 1
    assert Cap.ALL == (1 << 10) - 1  # unchanged forever


def test_new_statuses_names_classes_and_exception_types(vectors: Dict[str, Any]) -> None:
    rows: List[Dict[str, Any]] = vectors["status_abi15"]
    assert [r["code"] for r in rows] == list(range(-32, -41, -1))
    for r in rows:
        code = r["code"]
        assert zxv.status_name(code) == r["name"]
        assert zxv.status_class(code) == StatusClass(r["class"])
        exc = _native.make_error(code, "detail")
        assert type(exc) is EXPECTED_TYPES[code], code
        assert isinstance(exc, ZxvUsageError if r["class"] == 1 else ZxvRefusedError)
        assert exc.status == code and exc.status_name == r["name"] and exc.detail == "detail"


def test_unknown_status_still_maps_by_class() -> None:
    # -41 .. -45 are reserved; whatever the library says, it must be an error type.
    exc = _native.make_error(-45, "x")
    assert isinstance(exc, (zxv.ZxvError, ZxvFatal))


# ---------------------------------------------------------------- buffer growth


def test_read_bytes_grows_from_a_short_first_buffer() -> None:
    data = bytes(range(256)) * 3
    calls: List[int] = []

    def call(buf: Any, cap: int, n: Any) -> int:
        calls.append(cap)
        n[0] = len(data)
        if cap < len(data):
            return -2
        _native.ffi.memmove(buf, data, len(data))
        return 0

    assert _native.read_bytes(call, None, 7) == data
    assert calls == [7, len(data)]
    calls.clear()
    assert _native.read_bytes(call) == data  # sizing call with a NULL buffer first
    assert calls == [0, len(data)]


def test_buffer_growth_through_the_real_library() -> None:
    # A UnixFS blob is read back by an AIPI node through the sizing call, and a
    # treaty record (2542 bytes) through a first buffer of exactly its size.
    with zxv.Context() as ctx:
        cap = ctx.root_capability(Cap.ALL_1_5)
        with zxv.AipiNode(cap, 1 << 20, 16) as node:
            small = b"small block"
            assert node.get(node.put(small)) == small
            assert node.info.gets_verified == 1  # fits the first buffer: one C call
            blob = bytes(i * 7 & 255 for i in range(100_000))
            c = node.put(blob)
            assert node.get(c) == blob  # too big for the first buffer: grown once
            assert node.info.gets_verified == 3
        with zxv.Treaty.from_sutra("TREATY N USD NET\nPARTY A 1 1 1/1\nEND\n") as t:
            assert len(t.to_record()) == zxv.treaty.TREATY_REC_LEN


def test_read_bytes_propagates_a_real_error() -> None:
    def call(buf: Any, cap: int, n: Any) -> int:
        return -34

    with pytest.raises(CidMismatchError):
        _native.read_bytes(call)


# ---------------------------------------------------------------- capabilities


@pytest.mark.parametrize("missing,make", [
    (Cap.AIPI, lambda cap: zxv.AipiNode(cap, 4096, 4)),
    (Cap.MINT, lambda cap: zxv.TokenWallet(cap, 1, 0)),
    (Cap.PARADOX, lambda cap: zxv.ParadoxBook(cap, 4, 4)),
    (Cap.PARADOX, lambda cap: zxv.ParadoxPolicy(cap, [0], [1], [0])),
    (Cap.PARADOX, lambda cap: zxv.ParadoxHold(cap, 2)),
    (Cap.PARADOX, lambda cap: zxv.ParadoxGraph(cap, 2)),
    (Cap.LEDGER_POST, lambda cap: zxv.VHub(cap)),
    (Cap.CRYPTO_SIGN, lambda cap: zxv.W4Agent(cap, "a", 0, seed=bytes(32))),
])
def test_each_new_handle_needs_its_capability(missing: Cap, make: Any) -> None:
    with zxv.Context() as ctx:
        cap = ctx.root_capability(Cap.ALL_1_5 & ~missing)
        with pytest.raises(CapabilityDeniedError) as e:
            make(cap)
        assert e.value.status == -17


def test_cap_all_1_1_lacks_the_new_bits() -> None:
    with zxv.Context() as ctx:
        cap = ctx.root_capability()  # Cap.ALL: the 1.1 bits only
        for c in (Cap.AIPI, Cap.MINT, Cap.PARADOX):
            assert not cap.check(c)
        with pytest.raises(CapabilityDeniedError):
            zxv.AipiNode(cap, 4096, 4)


def test_operation_with_a_weaker_cap_is_refused() -> None:
    with zxv.Context() as ctx:
        full = ctx.root_capability(Cap.ALL_1_5)
        with zxv.AipiNode(full, 1 << 16, 8) as node, zxv.TokenWallet(full, 1, 0) as w:
            reader = full.attenuate(Cap.ALL_1_5 & ~(Cap.AIPI | Cap.MINT))
            with pytest.raises(CapabilityDeniedError):
                node.put(b"x", cap=reader)
            with pytest.raises(CapabilityDeniedError):
                w.mint(5, cap=reader)
            c = node.put(b"x")
            assert node.get(c) == b"x"  # reads need no cap
            w.mint(5)
            full.revoke()
            with pytest.raises(CapabilityDeniedError):
                w.mint(1)
            assert w.value == 5


# ---------------------------------------------------------------- lifetimes


def test_use_after_close_is_refused_for_every_new_handle() -> None:
    with zxv.Context() as ctx:
        cap = ctx.root_capability(Cap.ALL_1_5)
        handles: List[Any] = [
            zxv.Treaty(), zxv.AipiNode(cap, 4096, 4), zxv.TokenWallet(cap), zxv.VHub(cap),
            zxv.FreightJoin(100), zxv.OpsWriter(0), zxv.W4Directory(2), zxv.W4ConsentLog(2),
            zxv.W4Manifest(1, 0, 1), zxv.ParadoxBook(cap, 4, 4), zxv.ParadoxHold(cap, 2), zxv.ParadoxGraph(cap, 2),
            zxv.ParadoxPolicy(cap, [0], [1], [0]),
        ]
        probes = [
            lambda h: h.name, lambda h: h.info, lambda h: h.value, lambda h: h.check(),
            lambda h: h.status, lambda h: h.finish(), lambda h: len(h), lambda h: h.prune(0),
            lambda h: h.info, lambda h: h.registry_count, lambda h: h.info, lambda h: h.info,
            lambda h: h.values,
        ]
        for h, probe in zip(handles, probes):
            probe(h)  # works while open
            h.close()
            h.close()  # idempotent
            assert h.closed
            with pytest.raises(ZxvUsageError, match="closed"):
                probe(h)


def test_closing_the_cap_closes_the_handles_made_from_it() -> None:
    with zxv.Context() as ctx:
        cap = ctx.root_capability(Cap.ALL_1_5)
        w = zxv.TokenWallet(cap)
        book = zxv.ParadoxBook(cap, 4, 4)
        agent = zxv.W4Agent(cap, "a", 0, seed=bytes(32))
        cap.close()
        assert w.closed and book.closed and agent.closed


def test_treaty_result_outlives_its_treaty() -> None:
    t = zxv.Treaty.from_sutra("TREATY N USD NET\nPARTY A 5 1 1/2\nPARTY B 5 1 1/2\nOWE A B 3\nEND\n")
    r = t.run()
    t.close()
    assert r.legs[0].amount == 3
    r.close()


# ---------------------------------------------------------------- fail closed


def test_poisoned_context_refuses_every_new_handle_group() -> None:
    ctx = zxv.Context()
    cap = ctx.root_capability(Cap.ALL_1_5)
    w = zxv.TokenWallet(cap, 1, 0)
    hub = zxv.VHub(cap, bytes(32))
    book = zxv.ParadoxBook(cap, 4, 4)
    node = zxv.AipiNode(cap, 4096, 4)
    w.mint(10)
    ctx.halt("test")
    for call in (lambda: w.value, lambda: w.mint(1), lambda: hub.check(), lambda: book.registry_count,
                 lambda: node.info):
        with pytest.raises(ZxvFatal) as e:
            call()
        assert e.value.status == -18
    assert ctx.dead
    ctx.close()


def test_poison_seen_by_the_c_library_directly() -> None:
    # Bypass the SDK's own dead flag: C must refuse with ZXV_E_POISONED by itself.
    ctx = zxv.Context()
    cap = ctx.root_capability(Cap.ALL_1_5)
    w = zxv.TokenWallet(cap, 1, 0)
    _native.lib().zxv_ctx_halt(ctx._h, b"direct")
    out = _native.ffi.new("uint64_t *")
    rc = _native.lib().zxv_token_wallet_info(w._h, _native.ffi.NULL, _native.ffi.NULL, out, _native.ffi.NULL)
    assert rc == -18
    with pytest.raises(ZxvFatal):
        _ = w.value
    ctx.close()


def test_pure_groups_work_without_a_context() -> None:
    assert zxv.coin.face(10) == 89
    assert zxv.fee.assure(1125) == 1
    assert str(zxv.Cid.compute(b"abc")).startswith("bafkrei")
