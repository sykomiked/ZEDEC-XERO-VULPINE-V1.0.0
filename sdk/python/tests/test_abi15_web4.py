# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""ABI 1.5 web4 group (agents, cards, manifests, consent, envelopes) against the
vectors, in the generator's order. Signatures are hedged, so signed encodings
are checked by length and by the bytes before the trailing signature."""

from __future__ import annotations

import hashlib
from typing import Any, Callable, Dict, Iterator, List

import pytest

import zxv
from zxv import Cap, ConsentRequiredError, VerificationFailedError, W4Agent, ZxvError, web4
from zxv.web4 import ConsentScope, W4ConsentLog, W4Directory, W4Manifest, W4MsgFlag, W4MsgKind

pytestmark = pytest.mark.skipif(not zxv.has_feature2(zxv.Feature2.WEB4), reason="no web4 group")

SIG = web4.W4_SIG_BYTES


def status_of(fn: Callable[[], Any]) -> str:
    try:
        fn()
    except ZxvError as e:
        return e.status_name
    return "ZXV_OK"


def body(b: bytes) -> str:
    return b[:-SIG].hex()


@pytest.fixture
def world(vectors: Dict[str, Any]) -> Iterator[Dict[str, Any]]:
    """The four vector agents, in order, on one context."""
    v = vectors["web4"]
    with zxv.Context() as ctx:
        cap = ctx.root_capability(Cap.ALL_1_5)
        agents: Dict[str, W4Agent] = {}
        for a in v["agents"]:
            node = agents[a["node"]] if a["node"] else None
            agents[a["name"]] = W4Agent(cap, a["name"], int(a["now_ms"]), seed=bytes([int(a["seed_byte"], 16)]) * 32,
                                        node=node)
        yield {"v": v, "cap": cap, "agents": agents}
        for ag in reversed(list(agents.values())):
            ag.close()


def test_agents_and_cards(world: Dict[str, Any]) -> None:
    v, agents = world["v"], world["agents"]
    assert v["sig_bytes"] == SIG
    for a in v["agents"]:
        ag = agents[a["name"]]
        assert ag.agent_id.hex() == a["agent_id"] and ag.peer_id.hex() == a["peer_id"]
        assert hashlib.sha3_256(ag.public_key).hexdigest() == a["pk_sha3_256"]
        card = ag.card
        assert len(card) == a["card_len"]
        assert hashlib.sha3_256(card[:-SIG]).hexdigest() == a["card_signed_body_sha3_256"]
        c = web4.verify_card(card)
        assert (c.agent_id, c.peer_id, c.name, c.bound_ms) == (ag.agent_id, ag.peer_id, a["name"], int(a["now_ms"]))
        assert c.self_bound is (a["node"] is None)
        flipped = card[:-1] + bytes([card[-1] ^ 1])
        assert status_of(lambda: web4.verify_card(flipped)) == a["card_verify_flipped_last_byte"]
        assert ag.agent_id.hex()[:16] in repr(ag) and len(repr(ag)) < 64  # no key material in repr


def test_action_digest(vectors: Dict[str, Any]) -> None:
    d = vectors["web4"]["action_digest"]
    fr, to = bytes.fromhex(d["from"]), bytes.fromhex(d["to"])
    for case in d["cases"]:
        payload = (case["payload_utf8"].encode() if "payload_utf8" in case
                   else bytes(i & 255 for i in range(case["payload_ramp_len"])))

        def run() -> bytes:
            return web4.action_digest(case["tool"], fr, to, int(case["req_id"]), payload)
        if case["status"] != "ZXV_OK":
            assert status_of(run) == case["status"], case
        else:
            assert run().hex() == case["digest"], case


def test_manifest(world: Dict[str, Any]) -> None:
    m, agents = world["v"]["manifest"], world["agents"]
    signer = agents[m["signer"]]
    with W4Manifest(m["version"], int(m["issued_ms"]), int(m["expires_ms"])) as man:
        for t in m["tools"]:
            assert status_of(lambda: man.add_tool(t["name"], price_vfv=int(t["price_vfv"]),
                                                  rate_per_min=t["rate_per_min"], burst=t["burst"],
                                                  flags=t["flags"], max_input=t["max_input"])) == t["status"]
        assert status_of(lambda: man.add_tool("pay", price_vfv=1, rate_per_min=1, burst=1)) == m["add_duplicate"]
        assert status_of(man.encode) == m["encode_unsigned"]
        man.sign(signer)
        enc = man.encode()
        assert len(enc) == m["encoding_len"] and body(enc) == m["encoding_signed_body"]
        assert man.to_json() == m["json"]
        assert [t.name for t in man.tools] == [t["name"] for t in m["tools"]]
        assert man.find("profile.read") == 1 and man.find("nope") is None
    assert m["decode"] == "ZXV_OK"
    with W4Manifest.decode(enc) as d:
        for case in m["verify"]:
            pk = agents[case["key"]].public_key
            assert status_of(lambda: d.verify(pk, int(case["now_ms"]))) == case["status"], case
        assert len(d) == 3 and d.tool(0).price_vfv == 150
        with pytest.raises(VerificationFailedError):
            d.verify(agents["bob"].public_key, int(m["issued_ms"]))


def test_consent_sequence(world: Dict[str, Any]) -> None:
    v, agents = world["v"], world["agents"]
    c = v["consent"]
    d = v["action_digest"]
    case1 = d["cases"][1]
    action = bytes.fromhex(case1["digest"])
    human, alice = agents[c["human"]], agents[c["agent"]]
    nonce = bytes.fromhex(c["nonce"])
    tok = human.issue_consent(alice.agent_id, action, ConsentScope(c["scope"]), max_vfv=int(c["max_vfv"]),
                              issued_ms=int(c["issued_ms"]), expires_ms=int(c["expires_ms"]), nonce=nonce)
    assert len(tok) == c["token_len"] and body(tok) == c["token_signed_body"]
    info = web4.consent_info(tok)
    assert (info.human_id, info.agent_id, info.action, info.nonce) == (human.agent_id, alice.agent_id, action, nonce)
    tok2 = human.issue_consent(alice.agent_id, action, ConsentScope(c["scope"]), max_vfv=int(c["max_vfv"]),
                               issued_ms=int(c["issued_ms"]), expires_ms=int(c["expires_ms"]),
                               nonce=bytes([nonce[0] ^ 0xFF]) + nonce[1:])
    with W4ConsentLog(16) as log:
        for i, ch in enumerate(c["check"]):
            t = tok
            if i == len(c["check"]) - 1:
                log.revoke(tok2, int(ch["now_ms"]))
                t = tok2

            def run() -> None:
                log.check(t, human_public_key=human.public_key, agent_id=agents[ch["agent"]].agent_id,
                          action=action, need=ConsentScope(ch["need"]), amount=int(ch["amount"]),
                          now_ms=int(ch["now_ms"]))
            assert status_of(run) == ch["status"], ch["what"]
        with pytest.raises(ConsentRequiredError):
            log.check(None, human_public_key=human.public_key, agent_id=alice.agent_id, action=action,
                      need=ConsentScope.MONEY, amount=1, now_ms=int(c["issued_ms"]) + 1)


def test_envelopes(world: Dict[str, Any]) -> None:
    v, agents = world["v"], world["agents"]
    seal, rep = v["envelopes"]
    alice, bob = agents[seal["sender"]], agents[seal["to"]]
    req = alice.seal(bob.agent_id, W4MsgKind(seal["kind"]), req_id=int(seal["req_id"]), tool=seal["tool"],
                     payload=seal["payload_utf8"].encode(), now_ms=int(seal["now_ms"]), chunk=seal["chunk"],
                     flags=W4MsgFlag(seal["flags"]))
    assert len(req) == seal["env_len"] and body(req) == seal["env_signed_body"]
    web4.verify_envelope(req, alice.public_key)
    assert status_of(lambda: web4.verify_envelope(req, bob.public_key)) == seal["verify_other"]
    e = web4.decode_envelope(req)
    assert (e.from_id, e.to_id, e.req_id, e.tool, e.payload, e.consent) == \
        (alice.agent_id, bob.agent_id, 7, "pay", b"amount=100", None)
    rsp = bob.reply(req, W4MsgKind(rep["kind"]), payload=rep["payload_utf8"].encode(), now_ms=int(rep["now_ms"]),
                    chunk=rep["chunk"], flags=W4MsgFlag(rep["flags"]))
    assert len(rsp) == rep["env_len"] and body(rsp) == rep["env_signed_body"]
    web4.verify_envelope(rsp, bob.public_key)
    r = web4.decode_envelope(rsp)
    assert (r.from_id, r.to_id, r.flags) == (bob.agent_id, alice.agent_id, W4MsgFlag.FINAL)
    with pytest.raises(zxv.ZxvUsageError):
        alice.reply(req, W4MsgKind.RESPONSE, now_ms=1)  # not addressed to alice


def test_directory(world: Dict[str, Any]) -> None:
    agents: Dict[str, W4Agent] = world["agents"]
    with W4Directory(2) as d:
        ids: List[bytes] = []
        for name in ("alice", "bob"):
            d.add(agents[name].card)
            ids.append(agents[name].agent_id)
        assert len(d) == 2 and all(i in d for i in ids)
        assert d.find(ids[1]) == agents["bob"].card
        assert d.find(agents["human"].agent_id) is None
        with pytest.raises(zxv.ZxvRefusedError):
            d.add(agents["human"].card)


def test_capability_refusal() -> None:
    with zxv.Context() as ctx:
        cap = ctx.root_capability(Cap.ALL_1_5)
        with pytest.raises(zxv.CapabilityDeniedError):
            W4Agent(cap.attenuate(Cap.LEDGER_READ), "x", 1)
        with W4Agent(cap, "h", 1) as h, W4Agent(cap, "a", 1) as a:
            with pytest.raises(zxv.CapabilityDeniedError):
                h.issue_consent(a.agent_id, bytes(32), ConsentScope.MONEY, max_vfv=1, issued_ms=1, expires_ms=2,
                                cap=cap.attenuate(Cap.CRYPTO_SIGN))
