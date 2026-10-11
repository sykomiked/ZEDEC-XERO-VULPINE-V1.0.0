# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The 64-state hexagram tag and quarantine log against vectors.json key "hex"."""

from __future__ import annotations

import hashlib
import struct
from typing import Any, Dict

import pytest

import zxv
from zxv import (Cap, HexAction, HexBase, Hexagram, HexPhase, HexPolarity, QuarantineLog, Truth,
                 ZxvFatal, ZxvRefusedError, ZxvUsageError)
from conftest import needs

pytestmark = needs(zxv.Feature.HEX)

ALL = [Hexagram.from_tag(t) for t in range(64)]


def test_tags_match_vectors(vectors: Dict[str, Any]) -> None:
    hx = vectors["hex"]
    assert [b.name.lower() for b in HexBase] == hx["base_names"]
    assert [p.name.lower() for p in HexPolarity] == hx["polarity_names"]
    assert [p.name.lower() for p in HexPhase] == hx["phase_names"]
    assert [a.name.lower() for a in HexAction] == hx["action_names"]
    for v in hx["tags"]:
        h = ALL[v["tag"]]
        assert h.tag == v["tag"] and h.name == v["name"] and str(h) == v["name"]
        assert h.base is HexBase(v["base"]) and h.polarity is HexPolarity(v["polarity"])
        assert h.phase is HexPhase(v["phase"]) and h.action is HexAction(v["action"])
        assert Hexagram(h.base, h.polarity, h.phase) == h
        assert h.is_forbidden == (h.polarity is HexPolarity.FORBIDDEN)
    for p in HexPhase:
        assert p.next() is HexPhase(hx["phase_next"][p.value])


def test_logic_tables(vectors: Dict[str, Any]) -> None:
    hx = vectors["hex"]
    for a in ALL:
        assert (~a).tag == hx["not"][a.tag] and a.not_() == ~a
        for b in ALL:
            assert (a & b).tag == hx["and"][a.tag][b.tag]
            assert (a | b).tag == hx["or"][a.tag][b.tag]
    assert ALL[0].and_(ALL[63]) == ALL[0] & ALL[63] and ALL[0].or_(ALL[63]) == ALL[0] | ALL[63]


def test_step_and_set(vectors: Dict[str, Any]) -> None:
    hx = vectors["hex"]
    for h in ALL:
        for p in HexPhase:
            want = hx["step"][h.tag][p.value]
            if want < 0:
                with pytest.raises(ZxvRefusedError) as e:
                    h.step(p)
                assert e.value.status_name == "ZXV_E_STATE"
            else:
                assert h.step(p).tag == want
        assert h.advance().phase is h.phase.next()
        for b in HexBase:
            for pol in HexPolarity:
                want = hx["set"][h.tag][b.value * 4 + pol.value]
                if want < 0:
                    with pytest.raises(ZxvRefusedError):
                        h.with_state(b, pol)
                else:
                    assert h.with_state(b, pol).tag == want
        assert h.forbid() == Hexagram(h.base, HexPolarity.FORBIDDEN, h.phase)


def test_polarity_from_evidence(vectors: Dict[str, Any]) -> None:
    for c in vectors["hex"]["polarity"]:
        got = Hexagram.polarity_from_evidence(c["pos0"], c["neg0"], c["pos1"], c["neg1"],
                                              stale=c["stale"])
        assert got is HexPolarity(c["polarity"])
    with pytest.raises(TypeError):
        Hexagram.polarity_from_evidence(1, 0, 2, 0, stale=1)  # type: ignore[arg-type]


def test_truth_bridge_is_explicit(vectors: Dict[str, Any]) -> None:
    hx = vectors["hex"]
    for t in Truth:
        h = Hexagram.from_truth(t, HexPolarity.NEUTRAL, HexPhase.POTENTIAL)
        assert h.base is HexBase(hx["lp_to_base"][t.value])
        assert h.to_truth() is t
    for b in HexBase:
        assert Hexagram(b, HexPolarity.NEUTRAL, HexPhase.POTENTIAL).to_truth().value == hx["base_to_lp"][b]
    # The numbering differs on purpose: TRUE is 1 as a Truth, 3 as a base.
    assert Truth.TRUE.value == 1 and Hexagram.from_truth(Truth.TRUE, HexPolarity.NEUTRAL,
                                                         HexPhase.POTENTIAL).base is HexBase.TRUE
    with pytest.raises(TypeError):
        Hexagram.from_truth(1, HexPolarity.NEUTRAL, HexPhase.POTENTIAL)  # type: ignore[arg-type]
    with pytest.raises(TypeError):
        Hexagram(1, HexPolarity.NEUTRAL, HexPhase.POTENTIAL)  # type: ignore[arg-type]
    with pytest.raises(TypeError):
        Hexagram(HexBase.TRUE, HexPolarity.NEUTRAL, HexPhase.POTENTIAL).step(3)  # type: ignore[arg-type]


def test_value_semantics_and_refusals() -> None:
    h = Hexagram(HexBase.TRUE, HexPolarity.NEGENTROPIC, HexPhase.MANIFEST)
    assert h == Hexagram.from_tag(63) and hash(h) == hash(Hexagram.from_tag(63)) and h != 63
    assert repr(h) == "Hexagram(true/negentropic/manifest, tag=63)"
    with pytest.raises(TypeError):
        bool(h)
    with pytest.raises(AttributeError):
        h._tag = 1  # type: ignore[misc]
    for bad in (64, 100, 255):
        with pytest.raises(ZxvUsageError):
            Hexagram.from_tag(bad)
    with pytest.raises(ZxvUsageError):
        Hexagram.from_tag(-1)
    with pytest.raises(zxv.FloatRejectedError):
        Hexagram.from_tag(1.0)  # type: ignore[arg-type]
    with pytest.raises(TypeError):
        Hexagram.from_tag(True)


def test_forbidden_is_absorbing() -> None:
    h = Hexagram(HexBase.TRUE, HexPolarity.FORBIDDEN, HexPhase.POTENTIAL)
    assert h.action is HexAction.ROLLBACK
    for _ in range(4):
        h = h.advance()
        assert h.is_forbidden
    assert h.with_state(HexBase.FALSE, HexPolarity.FORBIDDEN).base is HexBase.FALSE
    with pytest.raises(ZxvRefusedError):
        h.with_state(HexBase.TRUE, HexPolarity.NEGENTROPIC)
    clean = Hexagram(HexBase.TRUE, HexPolarity.NEGENTROPIC, HexPhase.MANIFEST)
    assert (h & clean).is_forbidden and (h | clean).is_forbidden and (~h).is_forbidden


def _link(seq: int, tag: int, reason: int, who: int, subject: bytes, prev: bytes) -> bytes:
    return hashlib.sha256(b"zxv/lpx-qlog/v1" + struct.pack("<QBBI", seq, tag, reason, who) + subject
                          + prev).digest()


def test_quarantine_chain_matches_vectors(ctx: zxv.Context, cap: zxv.Capability,
                                          vectors: Dict[str, Any]) -> None:
    q = vectors["hex"]["qlog"]
    assert q["domain"] == "zxv/lpx-qlog/v1"
    with QuarantineLog(cap, 3) as log:
        assert len(log) == 0 and log.head == bytes(32)
        prev = bytes(32)
        for r in q["records"]:
            subj = bytes.fromhex(r["subject"])
            seq = log.append(Hexagram.from_tag(r["tag"]), r["reason"], r["submitter"], subj)
            assert seq == r["seq"]
            want = _link(seq, r["tag"], r["reason"], r["submitter"], subj, prev)
            assert want.hex() == r["hash"] and prev.hex() == r["prev"]
            rec = log.record(seq)
            assert rec.hash == want and rec.prev == prev and rec.subject == subj
            assert rec.tag.tag == r["tag"] and rec.reason == r["reason"] and rec.submitter == r["submitter"]
            prev = want
        assert log.head.hex() == q["head"] and len(log) == 3
        assert [r.seq for r in log] == [0, 1, 2]
        log.verify()
        with pytest.raises(ZxvRefusedError) as e:  # full: the caller must halt
            log.append(Hexagram.from_tag(q["records"][0]["tag"]), 0, 0, bytes(32))
        assert e.value.status_name == "ZXV_E_CAPACITY"


def test_quarantine_refusals(ctx: zxv.Context, cap: zxv.Capability) -> None:
    forb = Hexagram(HexBase.BOTH, HexPolarity.FORBIDDEN, HexPhase.EMERGING)
    with pytest.raises(zxv.CapabilityDeniedError):
        QuarantineLog(cap.attenuate(Cap.ALL & ~Cap.ATTEST), 4)
    with pytest.raises(ZxvUsageError):
        QuarantineLog(cap, 0)
    with pytest.raises(ZxvUsageError):
        QuarantineLog(cap, zxv.QUARANTINE_MAX_RECORDS + 1)
    log = QuarantineLog(cap, 4)
    with pytest.raises(ZxvUsageError):
        log.append(Hexagram(HexBase.TRUE, HexPolarity.NEUTRAL, HexPhase.EMERGING), 1, 1, bytes(32))
    with pytest.raises(ZxvUsageError):
        log.append(forb, 1, 1, bytes(31))
    with pytest.raises(TypeError):
        log.append(forb.tag, 1, 1, bytes(32))  # type: ignore[arg-type]
    with pytest.raises(ZxvUsageError):
        log.append(forb, 256, 1, bytes(32))
    with zxv.Context() as other:
        foreign = other.root_capability()
        log2 = QuarantineLog(foreign, 1)
        with pytest.raises(zxv.CapabilityDeniedError):
            log2._cap = cap  # type: ignore[misc]
            log2.append(forb, 0, 0, bytes(32))
        log2.close()
    assert log.append(forb, 9, 7, bytes(range(32))) == 0
    ctx.halt("test")
    with pytest.raises(ZxvFatal):
        len(log)
