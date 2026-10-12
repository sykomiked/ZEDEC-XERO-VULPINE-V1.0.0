# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""ABI 1.5 paradox group against the vectors. Every book step runs in the
generator's order on ONE book (log capacity 64, registry capacity 16)."""

from __future__ import annotations

import hashlib
from typing import Any, Callable, Dict, Optional

import pytest

import zxv
from zxv import Cap, Hexagram, OwnerApprovalRequiredError, ParadoxStepLimitError, ZxvError, paradox
from zxv.paradox import (Classification, NodeState, ParadoxBook, ParadoxGraph, ParadoxHold, ParadoxKind,
                         ParadoxLog, ParadoxOutcome, ParadoxPolicy, RepairProof)

pytestmark = pytest.mark.skipif(not zxv.has_feature2(zxv.Feature2.PARADOX), reason="no paradox group")

LOGS = {"ops": ParadoxLog.OPS, "frontier": ParadoxLog.FRONTIER, "quarantine": ParadoxLog.QUARANTINE}


def status_of(fn: Callable[[], Any]) -> str:
    try:
        fn()
    except ZxvError as e:
        return e.status_name
    return "ZXV_OK"


def pair(kind: int, tag: int) -> Classification:
    return Classification(ParadoxKind(kind), Hexagram.from_tag(tag))


def state_hash(state: bytes) -> bytes:
    """SHA-256("zxv/pdx-state/v1" || len u32 LE || state), as zxv_api.h documents."""
    return hashlib.sha256(b"zxv/pdx-state/v1" + len(state).to_bytes(4, "little") + state).digest()


def heads(book: ParadoxBook) -> Dict[str, Any]:
    return {n: {"count": book.log_count(w), "head": book.log_head(w).hex()} for n, w in LOGS.items()}


def test_static_tables(vectors: Dict[str, Any]) -> None:
    v = vectors["paradox"]
    for case in v["signature"]:
        assert paradox.signature(case["utf8"].encode()).hex() == case["sig"]
    assert [k.label for k in ParadoxKind] == v["kind_names"]
    assert [o.label for o in ParadoxOutcome] == v["outcome_names"]
    assert [s.label for s in NodeState] == v["node_state_names"]
    assert [paradox.kind_of_tag(Hexagram.from_tag(t)) for t in range(64)] == v["kind_of_tag"]
    for k in range(5):
        for t in range(64):
            want = v["action"][k][t]
            if want < 0:
                assert status_of(lambda: paradox.action(k, Hexagram.from_tag(t))) == "ZXV_E_ARG"
            else:
                assert paradox.action(k, Hexagram.from_tag(t)) == want
                assert pair(k, t).action == want
    assert [int(ParadoxOutcome(o).hex_action) for o in range(14)] == v["outcome_action"]


def test_book_sequence(vectors: Dict[str, Any]) -> None:
    v = vectors["paradox"]
    sig_l = bytes.fromhex(v["signature"][2]["sig"])
    with zxv.Context() as ctx:
        cap = ctx.root_capability(Cap.ALL_1_5)
        with ParadoxBook(cap, 64, 16) as book:
            assert heads(book) == v["heads_fresh"]

            # classify (with a registry_add in the middle)
            for case in v["classify"]:
                if "registry_add" in case:
                    r = case["registry_add"]
                    assert book.registry_add(sig_l, ParadoxKind(r["kind"]), reason=r["reason"], flags=r["flags"],
                                             owner_ok=r["owner_ok"]) == r["index"]
                    continue
                ev = tuple(case["evidence"]) if case["evidence"] else None
                c = book.classify(Hexagram.from_tag(case["tag"]), evidence=ev, stale=case["stale"],  # type: ignore[arg-type]
                                  signature=sig_l if case["sig"] else None)
                assert (int(c.kind), c.tag.tag) == (case["kind"], case["out_tag"]), case
                assert c.registry_index == (None if case["reg_index"] < 0 else case["reg_index"])
                if not case["sig"]:  # the book-less classify agrees when no registry is involved
                    assert paradox.classify(Hexagram.from_tag(case["tag"]), evidence=ev,  # type: ignore[arg-type]
                                            stale=case["stale"]) == Classification(c.kind, c.tag)
            assert book.registry_find(sig_l) == 0

            sig_m = paradox.signature(b"money:drain")
            for r in v["registry"]:
                def add() -> int:
                    return book.registry_add(sig_m, ParadoxKind(r["kind"]), reason=2, flags=r["flags"],
                                             owner_ok=r["owner_ok"])
                if r["status"] == "ZXV_OK":
                    assert add() == r["index"]
                else:
                    assert status_of(add) == r["status"]
            with pytest.raises(OwnerApprovalRequiredError):
                book.registry_add(paradox.signature(b"x"), ParadoxKind.NEUTRAL, flags=paradox.ParadoxFlag.DEVICE)
            e = book.registry_entry(1)
            assert (e.signature, e.kind, e.flags, e.source) == (sig_m, ParadoxKind.NEUTRAL,
                                                                paradox.ParadoxFlag.MONEY, None)
            assert heads(book) == v["heads_after_classify"]

            # constructive
            cv = v["constructive"]
            lo, hi, init = ([int(x) for x in cv[k]] for k in ("lo", "hi", "initial"))
            with ParadoxPolicy(cap, lo, hi, init) as plain, \
                    ParadoxPolicy(cap, lo, hi, init, flags=paradox.ParadoxFlag.MONEY) as money:
                for step in cv["steps"]:
                    pol = money if step["policy"] == "money" else plain
                    synth = [int(x) for x in step["synth"]] if step["synth"] else None
                    p = pair(step["kind"], step["tag"]) if step["status"] == "ZXV_OK" else \
                        Classification(ParadoxKind(step["kind"]), Hexagram.from_tag(step["tag"]))

                    def run() -> ParadoxOutcome:
                        return book.constructive(pol, p, synth, tension=step["tension"])
                    if step["status"] == "ZXV_OK":
                        assert run() == step["outcome"], step
                    else:
                        assert status_of(run) == step["status"], step
                    st = pol.state
                    assert [str(x) for x in st.values] == step["values"]
                    assert [str(x) for x in st.previous] == step["prev"]
                    assert (st.armed, st.tension_at) == (step["armed"], step["tension_at"])
                    if "proposal" in step:
                        assert synth is not None and pol.proposal(synth).hex() == step["proposal"]

            # destructive
            for d in v["destructive"]:
                state = bytearray(d["state_utf8"].encode())
                r = book.destructive(pair(d["kind"], d["tag"]), state=state,
                                     checkpoint=d["checkpoint_utf8"].encode(), steps=d["steps"],
                                     step_limit=d["step_limit"], spent=d["spent"], budget=d["budget"],
                                     trace=[int(x) for x in d["trace"]], invariant_ok=d["invariant_ok"],
                                     pattern=d["pattern_utf8"].encode(), submitter=d["submitter"])
                assert r.outcome == d["outcome"] and state.hex() == d["state_after"]
                if "repair_proof" in d:
                    assert r.proof is not None and r.proof.raw.hex() == d["repair_proof"]
                    r.proof.verify()
                    assert r.proof.state_restored == state_hash(d["checkpoint_utf8"].encode())
                    assert r.proof.state_found == state_hash(d["state_utf8"].encode())
                    assert r.proof.signature == paradox.signature(d["pattern_utf8"].encode())
                    bad = bytes([r.proof.raw[0] ^ 1]) + r.proof.raw[1:]
                    assert status_of(lambda: RepairProof.verify_bytes(bad)) == d["repair_verify_flipped_byte0"]
                else:
                    assert r.proof is None

            # neutral
            nv = v["neutral"]
            with ParadoxHold(cap, nv["capacity"], nv["decisive"]) as hold:
                b0, b1 = (bytes.fromhex(x) for x in nv["branches"])
                hold.add(b0)
                hold.add(b1)
                hold.evidence(b0, True)
                hold.evidence(b1, True)
                for step in nv["steps"][1:]:
                    for _ in range(step["add_for_branch0"]):
                        hold.evidence(b0, True)
                    pick: Optional[int] = None if step["owner_pick"] < 0 else step["owner_pick"]
                    nr = book.neutral(hold, pair(step["kind"], step["tag"]), owner_pick=pick)
                    assert nr.outcome == step["outcome"]
                    assert nr.settled == (None if step["settled"] < 0 else step["settled"])
                assert hold.info.settled == 0 and [b.positive for b in hold] == [3, 1]

            # special, drafts, review
            sv = v["special"]
            assert paradox.signature(sv["sig_utf8"].encode()).hex() == sv["sig"]
            sr = book.special(pair(sv["kind"], sv["tag"]), bytes.fromhex(sv["sig"]), bytes.fromhex(sv["subject"]))
            assert (sr.outcome, sr.record) == (sv["outcome"], sv["rec"])
            assert sr.record is not None
            drafts = {}
            for key in ("draft", "draft_money"):
                dv = sv[key]
                drafts[key] = book.draft(sr.record, ParadoxKind(dv["kind"]), reason=dv["reason"], flags=dv["flags"])
                assert drafts[key].hex() == dv["bytes"]
            for rv in sv["review"]:
                assert book.review(drafts[rv["draft"]], owner_ok=rv["owner_ok"],
                                   consensus_ok=rv["consensus_ok"]) == rv["outcome"], rv
            assert book.registry_count == sv["registry_count_after"]

            # graphs
            for gv in v["graphs"]:
                with ParadoxGraph(cap, gv["step_limit"]) as g:
                    def nid(c: str) -> bytes:
                        return c.encode() * 32
                    for n in gv["nodes"]:
                        g.add_node(nid(n["id"]), pair(n["kind"], n["tag"]), mag=n["mag"], direction=n["dir"])
                    for dep in gv["deps"]:
                        g.add_dep(nid(dep["id"]), nid(dep["dep"]))
                    for p in gv["passes"]:
                        def run_pass() -> ParadoxOutcome:
                            return book.graph_pass(g)
                        if p["status"] == "ZXV_OK":
                            assert run_pass() == p["outcome"]
                        else:
                            with pytest.raises(ParadoxStepLimitError):
                                run_pass()
                        info = g.info
                        assert (info.passes, info.total_mag, info.total_dir, info.escalated, info.chain.hex()) == \
                            (p["pass"], p["total_mag"], p["total_dir"], p["escalated"], p["chain"])
                        assert {c: int(g.state(nid(c))) for c in p["states"]} == p["states"]

            assert heads(book) == v["heads_final"]
            for name, w in LOGS.items():
                assert [r.hex() for r in book.log_records(w)] == v["logs"][name]
            for w, st in zip(LOGS.values(), v["log_verify"]):
                assert st == "ZXV_OK"
                book.verify_log(w)


def test_capability_refusal_and_full_log() -> None:
    with zxv.Context() as ctx:
        cap = ctx.root_capability(Cap.ALL_1_5)
        with pytest.raises(zxv.CapabilityDeniedError):
            ParadoxBook(cap.attenuate(Cap.LEDGER_READ), 4, 4)
        with ParadoxBook(cap, 1, 4) as book:
            sp = pair(ParadoxKind.SPECIAL, 36)
            book.special(sp, bytes(32), bytes(32))  # frontier 1 + ops 1: the ops log is now full
            with pytest.raises(zxv.ZxvRefusedError) as e:
                book.special(sp, bytes([1]) * 32, bytes(32))
            assert e.value.status_name == "ZXV_E_CAPACITY"
            reader = cap.attenuate(Cap.LEDGER_READ)
            with pytest.raises(zxv.CapabilityDeniedError):
                book.special(sp, bytes(32), bytes(32), cap=reader)


def test_destructive_requires_a_mutable_state() -> None:
    with zxv.Context() as ctx:
        cap = ctx.root_capability(Cap.ALL_1_5)
        with ParadoxBook(cap, 8, 4) as book:
            with pytest.raises(TypeError):
                book.destructive(pair(1, 35), state=b"xx", checkpoint=b"aa")  # type: ignore[arg-type]
            with pytest.raises(zxv.ZxvUsageError):
                book.destructive(pair(1, 35), state=bytearray(b"xx"), checkpoint=b"a")
