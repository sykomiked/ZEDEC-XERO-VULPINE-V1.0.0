# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""ABI 1.5 freight group (168 x 21 packets, sets, op streams) against the vectors."""

from __future__ import annotations

import hashlib
from typing import Any, Callable, Dict, List, Optional

import pytest

import zxv
from zxv import Cid, ErasureError, VerificationFailedError, ZxvError, ZxvUsageError, freight
from zxv.freight import FreightHeader, FreightJoin, OpsWriter

pytestmark = pytest.mark.skipif(not zxv.has_feature2(zxv.Feature2.FREIGHT), reason="no freight group")


def status_of(fn: Callable[[], Any]) -> str:
    try:
        fn()
    except ZxvError as e:
        return e.status_name
    return "ZXV_OK"


def ramp(n: int) -> bytes:
    return bytes(i & 255 for i in range(n))


def test_constants() -> None:
    assert freight.FREIGHT_ROWS * freight.FREIGHT_PACKET_BYTES == freight.FREIGHT_BYTES
    assert freight.FREIGHT_ROWS * freight.FREIGHT_ROW_PAYLOAD == freight.FREIGHT_MAX_PAYLOAD
    assert freight.FREIGHT_MAX_SET * freight.FREIGHT_MAX_PAYLOAD == freight.FREIGHT_SET_MAX_BYTES


def test_header_serialize(vectors: Dict[str, Any]) -> None:
    for case in vectors["freight"]["header_serialize"]:
        h = FreightHeader(case["k"], case["flags"], case["payload_len"], case["seq"], case["seq_count"],
                          int(case["freight_id"]), int(case["expand_limit"]), bytes.fromhex(case["payload_sha256"]))
        if case["status"] != "ZXV_OK":
            assert status_of(h.to_bytes) == case["status"], case
            continue
        raw = h.to_bytes()
        assert raw.hex() == case["header"] and bytes(h) == raw
        FreightHeader.check(raw)
        assert FreightHeader.parse(raw) == h


def test_header_init_reject(vectors: Dict[str, Any]) -> None:
    for case in vectors["freight"]["header_init_reject"]:
        st = status_of(lambda: FreightHeader.for_payload(1, case["k"], ramp(case["len"]),
                                                         flags=case.get("flags", 0),
                                                         expand_limit=int(case.get("expand_limit", "0"))))
        assert st == case["status"], case


def test_codec_decodes_any_k_rows(vectors: Dict[str, Any]) -> None:
    for case in vectors["freight"]["codec"]:
        payload = bytes.fromhex(case["payload"])
        h = FreightHeader.for_payload(int(case["freight_id"]), case["k"], payload)
        assert h.to_bytes().hex() == case["header"]
        assert h.payload_sha256 == hashlib.sha256(payload).digest()
        enc = freight.encode(h, payload)
        assert enc.hex() == case["packets"]
        rows = freight.packets(enc)
        assert len(rows) == freight.FREIGHT_ROWS
        for d in case["decode"]:
            got: List[bytes] = [rows[i] for i in d["rows"]]
            if "flip" in d:
                f = d["flip"]
                b = bytearray(got[f["index_in_rows"]])
                b[f["byte"]] ^= int(f["xor"], 16)
                got[f["index_in_rows"]] = bytes(b)

            def run() -> bytes:
                return freight.decode(h, got)
            if d["status"] != "ZXV_OK":
                assert status_of(run) == d["status"], (case["label"], d)
                continue
            assert (run() == payload) is d["matches_payload"]
            assert freight.decode(h.to_bytes(), b"".join(got)) == payload


def test_decode_typed_errors(vectors: Dict[str, Any]) -> None:
    case = vectors["freight"]["codec"][0]
    payload = bytes.fromhex(case["payload"])
    h = FreightHeader.for_payload(1, case["k"], payload)
    rows = freight.packets(freight.encode(h, payload))
    with pytest.raises(ErasureError):
        freight.decode(h, rows[:9])
    other = FreightHeader.for_payload(1, case["k"], payload[:-1] + b"?")
    with pytest.raises(VerificationFailedError):
        freight.decode(other, rows[:10])
    with pytest.raises(ZxvUsageError):
        freight.decode(h, b"\0" * 22)
    with pytest.raises(VerificationFailedError):
        freight.encode(other, payload)


def test_set_count(vectors: Dict[str, Any]) -> None:
    for case in vectors["freight"]["set_count"]:
        if case["status"] != "ZXV_OK":
            assert status_of(lambda: freight.set_count(int(case["len"]), case["k"])) == case["status"]
        else:
            assert freight.set_count(int(case["len"]), case["k"]) == case["count"]


def test_split_and_join(vectors: Dict[str, Any]) -> None:
    sp = vectors["freight"]["split"]
    data = ramp(sp["len"])
    members = []
    for m in sp["members"]:
        if m["status"] != "ZXV_OK":
            assert status_of(lambda: freight.split(int(sp["freight_id"]), sp["k"], data, m["seq"])) == m["status"]
            continue
        h, pk = freight.split(int(sp["freight_id"]), sp["k"], data, m["seq"])
        assert h.to_bytes().hex() == m["header"]
        assert hashlib.sha3_256(pk).hexdigest() == m["packets_sha3_256"]
        members.append((h, pk))
    assert [h for h, _ in freight.iter_split(int(sp["freight_id"]), sp["k"], data)] == [h for h, _ in members]
    with FreightJoin(1000) as j:
        assert not j.complete
        with pytest.raises(zxv.ZxvRefusedError):
            j.result()
        for h, pk in reversed(members):  # any order, rows from the far end
            j.add(h, freight.packets(pk)[-sp["k"]:])
        j.add(members[0][0], members[0][1])  # a repeat is accepted silently
        st = j.status
        assert (st.received, st.count, st.complete, st.total) == (3, 3, True, 100)
        assert j.result() == data
    with FreightJoin(50) as small, pytest.raises(zxv.ZxvRefusedError) as e:
        small.add(*members[0])
    assert e.value.status_name == "ZXV_E_CAPACITY"


def test_ops_encode_and_bound(vectors: Dict[str, Any]) -> None:
    for case in vectors["freight"]["ops_encode"]:
        data = case["utf8"].encode()
        ops = freight.ops_encode(data, with_sha256=case["with_sha"])
        assert ops.hex() == case["ops"]
        assert freight.ops_bound(len(data), with_sha256=case["with_sha"]) == int(case["bound"])
        assert case["expand_status"] == "ZXV_OK"
        assert freight.ops_expand(ops, 256) == data
        info = freight.ops_peek(ops)
        assert (info.total, info.has_sha256) == (len(data), case["with_sha"])


def test_ops_writer(vectors: Dict[str, Any]) -> None:
    case = vectors["freight"]["ops_writer"][0]
    with OpsWriter(int(case["declared_total"])) as w:
        for op in case["ops_list"]:
            assert op["status"] == "ZXV_OK"
            if op["op"] == "literal":
                w.literal(op["utf8"].encode())
            elif op["op"] == "copy":
                w.copy(int(op["distance"]), int(op["len"]))
            else:
                w.seed(op["gen"], bytes.fromhex(op["params"]), int(op["len"]))
        ops = w.finish()
    assert ops.hex() == case["ops"]
    assert freight.ops_expand(ops, 1024).hex() == case["expanded"]
    info = freight.ops_peek(ops)
    assert (str(info.total), info.has_sha256) == (case["peek_total"], case["peek_has_sha"])
    total = int(case["declared_total"])
    assert status_of(lambda: freight.ops_expand(ops, total - 1)) == case["expand_limit_too_small"]
    assert status_of(lambda: freight.ops_expand(ops[:-1], 1024)) == case["expand_truncated"]


def test_ops_writer_cidref_with_resolver(vectors: Dict[str, Any]) -> None:
    case = vectors["freight"]["ops_writer"][1]
    ref = case["ops_list"][1]
    store = {bytes.fromhex(ref["cid"]): ref["resolves_to_utf8"].encode()}
    with OpsWriter(int(case["declared_total"])) as w:
        w.literal(b"<").cidref(Cid(bytes.fromhex(ref["cid"])), int(ref["len"])).literal(b">")
        ops = w.finish()
    assert ops.hex() == case["ops"]
    assert status_of(lambda: freight.ops_expand(ops, 1024)) == case["expand_without_resolver"]
    calls: List[int] = []

    def resolver(cid: bytes, n: int) -> Optional[bytes]:
        calls.append(n)
        return store.get(cid)
    assert freight.ops_expand(ops, 1024, resolver).decode() == case["expanded_with_resolver_utf8"]
    assert calls == [3]
    assert status_of(lambda: freight.ops_expand(ops, 1024, lambda c, n: None)) == "ZXV_E_NOT_FOUND"


def test_resolver_exceptions_never_cross_c() -> None:
    c = Cid.compute(b"abc")
    with OpsWriter(3) as w:
        ops = w.cidref(c, 3).finish()

    def boom(cid: bytes, n: int) -> Optional[bytes]:
        raise KeyError("resolver failed")
    with pytest.raises(KeyError):
        freight.ops_expand(ops, 16, boom)
    with pytest.raises(ZxvUsageError):  # wrong length is refused, not copied
        freight.ops_expand(ops, 16, lambda cid, n: b"ab")
    assert freight.ops_expand(ops, 16, lambda cid, n: b"abc") == b"abc"  # still usable


def test_gen_run(vectors: Dict[str, Any]) -> None:
    for case in vectors["freight"]["gen_run"]:
        def run() -> bytes:
            return freight.gen_run(case["gen"], bytes.fromhex(case["params"]), case["len"])
        if case["status"] != "ZXV_OK":
            assert status_of(run) == case["status"], case
        else:
            assert run().hex() == case["out"], case


def test_writer_errors_are_sticky_and_short_streams_refused() -> None:
    with OpsWriter(4) as w:
        w.literal(b"ab")
        with pytest.raises(zxv.ZxvRefusedError) as e:
            w.finish()
        assert e.value.status_name == "ZXV_E_STATE"
        with pytest.raises(ZxvUsageError):
            w.literal(b"xyz")  # more than is left
        with pytest.raises(ZxvError):
            w.literal(b"cd")  # sticky
    with pytest.raises(ZxvUsageError, match="closed"):
        w.finish()
