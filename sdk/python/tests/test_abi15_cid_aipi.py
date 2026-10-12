# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""ABI 1.5 CID and AIPI groups against the conformance vectors."""

from __future__ import annotations

from typing import Any, Callable, Dict

import pytest

import zxv
from zxv import (AipiCall, AipiClaim, AipiFunction, AipiNode, AipiRoute, Cap, Cid, CidBase, CidMismatchError,
                 ZxvError, ZxvRefusedError, ZxvUsageError)
from zxv.aipi import AipiIntent, AipiPrivacy, AipiVerify

pytestmark = pytest.mark.skipif(not zxv.has_feature2(zxv.Feature2.AIPI), reason="no AIPI group")


def status_of(fn: Callable[[], Any]) -> str:
    try:
        fn()
    except ZxvError as e:
        return e.status_name
    return "ZXV_OK"


def ramp(n: int) -> bytes:
    return bytes(i & 255 for i in range(n))


def check_text(c: Cid, case: Dict[str, Any]) -> None:
    assert c.to_text() == case["text_default"] and str(c) == case["text_default"]
    if case.get("text_58btc", "").startswith("ZXV_E"):
        assert status_of(lambda: c.to_text(CidBase.BASE58BTC)) == case["text_58btc"]
    elif "text_58btc" in case:
        assert c.to_text(CidBase.BASE58BTC) == case["text_58btc"]
        assert Cid.parse(case["text_58btc"]) == c


# ---------------------------------------------------------------- CIDs


def test_compute(vectors: Dict[str, Any]) -> None:
    for case in vectors["cid"]["compute"]:
        c = Cid.compute(case["utf8"].encode(), case["codec"])
        assert c.binary.hex() == case["cid"] and bytes(c).hex() == case["cid"]
        assert (c.version, c.codec, c.hash_function, c.digest.hex()) == \
            (case["version"], case["codec"], case["hash"], case["digest"])
        check_text(c, case)
        c.verify(case["utf8"].encode())


def test_unixfs_matches_kubo(vectors: Dict[str, Any]) -> None:
    for case in vectors["cid"]["unixfs"]:
        data = ramp(case["len"]) if case.get("ramp") else case["utf8"].encode()
        c = Cid.unixfs(data)
        assert c.binary.hex() == case["cid"], case.get("len")
        assert str(c) == case["text_default"] and c.codec == case.get("codec", c.codec)


def test_text_parse_info_v1_and_provider_key(vectors: Dict[str, Any]) -> None:
    for case in vectors["cid"]["text"]:
        c = Cid.parse(case["text"])
        assert c.binary.hex() == case["cid"]
        assert (c.version, c.codec, c.hash_function, c.digest.hex()) == \
            (case["version"], case["codec"], case["hash"], case["digest"])
        check_text(c, case)
        v1 = c.to_v1()
        assert v1.binary.hex() == case["v1"] and str(v1) == case["v1_text"]
        assert c.provider_key().hex() == case["provider_key"]
        assert Cid(bytes.fromhex(case["cid"])) == c and hash(Cid(c.binary)) == hash(c)


def test_text_refusals(vectors: Dict[str, Any]) -> None:
    for case in vectors["cid"]["text_reject"]:
        assert status_of(lambda: Cid.parse(case["text"])) == case["status"], case["text"]


def test_verify(vectors: Dict[str, Any]) -> None:
    for case in vectors["cid"]["verify"]:
        def run() -> None:
            Cid(bytes.fromhex(case["cid"])).verify(case["utf8"].encode())
        assert status_of(run) == case["status"]
    good = Cid.compute(b"abc")
    assert good.matches(b"abc") and not good.matches(b"abd")
    with pytest.raises(CidMismatchError):
        good.verify(b"abd")


def test_cid_rejects_bad_binary_and_is_immutable() -> None:
    with pytest.raises(CidMismatchError):
        Cid(b"\x01\x55\x12\x20" + bytes(31))  # digest one byte short
    c = Cid.compute(b"x")
    with pytest.raises(AttributeError):
        c._b = b""  # type: ignore[misc]


# ---------------------------------------------------------------- records


def test_function_records(vectors: Dict[str, Any]) -> None:
    v = vectors["aipi"]
    module = Cid.compute(v["module_utf8"].encode())
    contract = Cid.compute(v["contract_utf8"].encode())
    assert module.binary.hex() == v["module_cid"] and contract.binary.hex() == v["contract_cid"]
    for case in v["fn"]:
        def enc() -> AipiFunction:
            return AipiFunction.encode(
                intent=case["intent"], verify=case["verify"], privacy=case["privacy"],
                determinism=case["determinism"], quorum_n=case["quorum_n"], quorum_k=case["quorum_k"],
                fuel_max=int(case["fuel_max"]), module=module, contract=contract if case["contract"] else None,
                entry=case["entry"], checker=case["checker"], certifier=case["certifier"])
        if case["status"] != "ZXV_OK":
            assert status_of(enc) == case["status"], case
            continue
        f = enc()
        assert f.record.hex() == case["record"]
        assert f.cid.binary.hex() == case["fn_cid"]
        assert f.cid.to_text(CidBase.BASE58BTC) == case["fn_cid_text"]
        d = AipiFunction.decode(bytes.fromhex(case["record"]))
        assert d == f
        assert (d.intent, d.verify, d.privacy, d.fuel_max) == \
            (case["intent"], case["verify"], case["privacy"], int(case["fuel_max"]))
        assert d.module == module and d.contract == (contract if case["contract"] else None)
        assert (d.entry, d.checker, d.certifier) == (case["entry"], case["checker"] or "", case["certifier"] or "")
    with pytest.raises(ZxvUsageError) as e:
        AipiFunction.decode(bytes.fromhex(v["fn"][0]["record"]) + b"\0")
    assert e.value.status_name == "ZXV_E_FORMAT"


def test_call_and_claim_records(vectors: Dict[str, Any]) -> None:
    v = vectors["aipi"]
    fn_cid = Cid(bytes.fromhex(v["fn"][0]["fn_cid"]))
    calls = {}
    for i, case in enumerate(v["call"]):
        if "status" in case:
            src = Cid(bytes.fromhex(v[case["fn"]]))
            assert status_of(lambda: AipiCall.encode(src, case["input_utf8"].encode())) == case["status"]
            continue
        call = AipiCall.encode(fn_cid, case["input_utf8"].encode())
        assert call.record.hex() == case["record"]
        assert call.cid.binary.hex() == case["call_cid"]
        assert AipiCall.cid_of(fn_cid, case["input_utf8"].encode()).to_text(CidBase.BASE58BTC) == case["call_cid_text"]
        back = AipiCall.decode(call.record)
        assert back.function == fn_cid and back.input == case["input_utf8"].encode()
        calls[f"call[{i}].call_cid"] = call.cid
    for case in v["claim"]:
        cl = AipiClaim.encode(calls[case["call"]], bytes.fromhex(case["executor"]), case["output_utf8"].encode(),
                              case["cert_utf8"].encode())
        assert cl.record.hex() == case["record"]
        claim_back = AipiClaim.decode(cl.record)
        assert claim_back == cl
    with pytest.raises(ZxvUsageError):
        AipiClaim.encode(calls["call[1].call_cid"], bytes(31), b"x")


# ---------------------------------------------------------------- the node


def test_node_put_get_publish_load_plan() -> None:
    with zxv.Context() as ctx:
        cap = ctx.root_capability(Cap.ALL_1_5)
        with AipiNode(cap, 1 << 20, 64, node_key=bytes(range(32))) as node:
            mod_bytes = b"zxv module bytes"
            module = Cid.compute(mod_bytes)
            f = AipiFunction.encode(intent=AipiIntent.FN, verify=AipiVerify.LOCAL, module=module, entry="main",
                                    fuel_max=10)
            fc = node.publish(f)
            assert fc == f.cid
            assert node.load(fc) == f
            assert node.plan(fc, b"in") is AipiRoute.UNAVAILABLE  # module not held: nothing runs here
            assert node.put(mod_bytes) == module
            assert module in node and str(module) in node
            assert node.plan(fc, b"in") is AipiRoute.LOCAL
            assert node.get(module) == mod_bytes
            # identity of put: the same bytes again is a no-op, public after private is refused
            secret = node.put(b"private", private=True)
            assert node.get(secret) == b"private"
            before = node.info
            with pytest.raises(ZxvRefusedError) as e:
                node.put(b"private")
            assert e.value.status_name == "ZXV_E_STATE"
            assert node.info.blocks == before.blocks and node.info.store_used == before.store_used
            with pytest.raises(ZxvUsageError) as e2:
                node.get(Cid.compute(b"never stored"))
            assert e2.value.status_name == "ZXV_E_NOT_FOUND"
            assert Cid.compute(b"never stored") not in node
            info = node.info
            assert info.blocks >= 3 and info.forbidden == 0 and info.gets_verified >= 3


def test_node_without_key_refuses_private_puts() -> None:
    with zxv.Context() as ctx:
        cap = ctx.root_capability(Cap.ALL_1_5)
        with AipiNode(cap, 4096, 4) as node:
            with pytest.raises(ZxvRefusedError) as e:
                node.put(b"s", private=True)
            assert e.value.status_name == "ZXV_E_STATE"
            f = AipiFunction.encode(intent=0, verify=4, module=Cid.compute(b"m"), entry="e", fuel_max=1,
                                    privacy=AipiPrivacy.LOCAL)
            with pytest.raises(ZxvRefusedError):
                node.publish(f)


def test_node_key_from_secret_key() -> None:
    with zxv.Context() as ctx, zxv.SecretKey.from_bytes(bytes(32)) as key:
        cap = ctx.root_capability(Cap.ALL_1_5)
        with AipiNode(cap, 4096, 4, node_key=key) as node:
            assert node.get(node.put(b"p", private=True)) == b"p"
