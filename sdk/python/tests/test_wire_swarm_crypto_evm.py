# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""UBH-168 frames, the swarm economy, hashes/crypto and EVM encoding vs vectors.json."""

from __future__ import annotations

import gc
from typing import Any, Dict

import pytest

import zxv
from zxv import Cap, Hashes, SecretKey, Ubh168Frame, WireTruth
from zxv._native import ffi, lib

# ------------------------------------------------------------------ wire


def test_ubh168_vectors_byte_for_byte(vectors: Dict[str, Any]) -> None:
    for v in vectors["ubh168"]:
        f = Ubh168Frame.build(v["trunk"], v["shell"], v["sync"], tuple(v["s_plus"]), tuple(v["s_minus"]))
        assert f.tag == v["tag"]
        assert f.to_bytes().hex() == v["frame"]
        assert f.to_bytes_native() == f.to_bytes(), "pure Python == zxv_wire_pack"
        assert Ubh168Frame.from_bytes(bytes.fromhex(v["frame"])) == f
        assert f.resolve() == v["truth"] and f.resolve_native() == f.resolve()
        tag = ffi.new("uint8_t *")
        assert lib().zxv_wire_tag(v["trunk"], v["shell"], v["sync"], tag) == 0 and tag[0] == f.tag


def test_idle_frames(vectors: Dict[str, Any]) -> None:
    out = ffi.new("uint8_t[21]")
    for trunk, hexframe in enumerate(vectors["ubh168_idle"]):
        f = Ubh168Frame.idle(trunk)
        assert f.to_bytes().hex() == hexframe and f.is_idle
        assert lib().zxv_wire_idle(trunk, out) == 0 and bytes(ffi.buffer(out, 21)) == f.to_bytes()
        assert f.resolve() is WireTruth.NEUTRAL


def test_resolver_agrees_with_c_on_many_frames() -> None:
    import random

    rng = random.Random(168)
    specials = [0, 1, 0x8000, 0x10000, 0xFFFF0000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x1000, 0xFFFFF000]
    for _ in range(3000):
        pick = lambda: rng.choice(specials) if rng.random() < 0.5 else rng.getrandbits(32)  # noqa: E731
        f = Ubh168Frame(rng.randrange(256), (pick(), pick(), pick()), (pick(), pick()))
        assert f.resolve_native() == f.resolve(), f
        if f.tag <= 199:
            assert f.to_bytes_native() == f.to_bytes()
        else:  # libzxv 1.2: zxv_wire_pack refuses a tag no (trunk, shell, sync) makes
            with pytest.raises(zxv.ZxvUsageError):
                f.to_bytes_native()


def test_frame_validation() -> None:
    with pytest.raises(ValueError):
        Ubh168Frame.from_bytes(b"\x00" * 20)
    with pytest.raises(ValueError):
        zxv.tag_of(10, 0, 0)
    with pytest.raises(ValueError):
        Ubh168Frame(0, (2**32, 0, 0), (0, 0))


# ------------------------------------------------------------------ swarm


def test_swarm_allotments_match_vectors(cap: zxv.Capability, vectors: Dict[str, Any]) -> None:
    v = vectors["swarm"]
    with zxv.Swarm(cap, v["levels"], v["tokens_per_cycle"]) as sw:
        for model, level in v["models"]:
            sw.register(model, level)
        sw.set_mood(v["mood"][0], v["mood"][1])
        for model, emoji, intensity in v["feelings"]:
            sw.set_feeling(model, chr(emoji), intensity)  # emoji as a str works too
        sw.begin_cycle()
        total = 0
        for model, allotted, imaginary in v["allotments"]:
            a = sw.allotment(model)
            assert (a.allotted, a.imaginary, a.used) == (allotted, imaginary, 0)
            total += a.allotted
        assert total == v["tokens_per_cycle"]
        first = v["allotments"][0]
        assert sw.consume(first[0], 10).complete
        g = sw.consume(first[0], first[1])
        assert not g.complete and g.granted == first[1] - 10, "over-ask grants the rest"
        with pytest.raises(zxv.BudgetExhaustedError):
            sw.consume_exact(first[0], 1)
        assert sw.end_cycle() >= 0
    for level, capacity in enumerate(v["level_capacity"]):
        assert zxv.Swarm.level_capacity(level) == capacity


def test_swarm_capacity_palette_and_caps(cap: zxv.Capability) -> None:
    with zxv.Swarm(cap, 3, 1000) as sw:
        sw.register(1, 0)
        with pytest.raises(zxv.ZxvRefusedError) as e:
            sw.register(4, 0)
        assert e.value.status_name == "ZXV_E_CAPACITY"
        with pytest.raises(zxv.ZxvUsageError):
            sw.set_mood(0x1F600, 1)  # outside the palette
    p = zxv.Swarm.emotion_profile("\U0001F914")
    assert isinstance(p.temperature, zxv.Rational)
    with pytest.raises(zxv.CapabilityDeniedError):
        zxv.Swarm(cap.attenuate(Cap.LEDGER_READ), 2, 10)  # ABI 1.2: creating needs SWARM
    s = cap.attenuate(Cap.SWARM)
    with zxv.Swarm(s, 2, 10) as sw:
        s.revoke()
        with pytest.raises(zxv.CapabilityDeniedError):
            sw.register(1, 0)


# ------------------------------------------------------------------ hashes


def test_hash_vectors(vectors: Dict[str, Any]) -> None:
    for v in vectors["hash"]:
        data = v["utf8"].encode()
        assert Hashes.keccak256(data).hex() == v["keccak256"]
        assert Hashes.sha3_256(data).hex() == v["sha3_256"]
        assert Hashes.shake256(data, 64).hex() == v["shake256_64"]
    assert len(zxv.random_bytes(32)) == 32 and zxv.random_bytes(32) != zxv.random_bytes(32)
    with pytest.raises(TypeError):
        Hashes.sha3_256("text")  # type: ignore[arg-type]


# ------------------------------------------------------------------ signatures / KEM


def test_ed25519_vectors(cap: zxv.Capability, vectors: Dict[str, Any]) -> None:
    v = vectors["ed25519"]
    ed = zxv.Ed25519(cap)
    with SecretKey.from_bytes(bytes.fromhex(v["sk"]), "ed25519") as sk:
        assert ed.public_key(sk).hex() == v["pk"]
        sig = ed.sign(sk, v["msg_utf8"].encode())
        assert sig.hex() == v["sig"]
    pk = bytes.fromhex(v["pk"])
    assert ed.verify(pk, v["msg_utf8"].encode(), sig)
    assert not ed.verify(pk, b"tampered", sig)
    with ed.generate() as kp:
        assert ed.verify(kp.public_key, b"m", ed.sign(kp.secret_key, b"m"))


def test_signing_needs_capability(cap: zxv.Capability) -> None:
    kem_only = cap.attenuate(Cap.CRYPTO_KEM)
    with SecretKey(32) as sk, pytest.raises(zxv.CapabilityDeniedError):
        zxv.Ed25519(kem_only).sign(sk, b"x")


def test_mldsa87_vectors(cap: zxv.Capability, vectors: Dict[str, Any]) -> None:
    v = vectors["mldsa87"]
    d = zxv.MlDsa87(cap)
    with SecretKey.from_bytes(bytes.fromhex(v["seed"])) as seed, d.generate_from_seed(seed) as kp:
        assert Hashes.sha3_256(kp.public_key).hex() == v["pk_sha3_256"]
        assert Hashes.sha3_256(kp.secret_key.export_bytes_unsafe()).hex() == v["sk_sha3_256"]
        msg, c = v["msg_utf8"].encode(), v["ctx_utf8"].encode()
        sig = d.sign_deterministic(kp.secret_key, msg, c)
        assert Hashes.sha3_256(sig).hex() == v["sig_deterministic_sha3_256"]
        assert d.verify(kp.public_key, msg, sig, c)
        assert not d.verify(kp.public_key, msg, sig, b"zxw"), "wrong context rejected"
        hedged = d.sign(kp.secret_key, msg, c)
        assert d.verify(kp.public_key, msg, hedged, c)


def test_mlkem1024_vectors(cap: zxv.Capability, vectors: Dict[str, Any]) -> None:
    v = vectors["mlkem1024"]
    kem = zxv.MlKem1024(cap.attenuate(Cap.CRYPTO_KEM))
    with SecretKey.from_bytes(bytes.fromhex(v["d"])) as d, SecretKey.from_bytes(bytes.fromhex(v["z"])) as z, \
            SecretKey.from_bytes(bytes.fromhex(v["m"])) as m:
        with kem.generate_derand(d, z) as kp:
            assert Hashes.sha3_256(kp.public_key).hex() == v["ek_sha3_256"]
            with kem.encapsulate(kp.public_key, m=m) as enc:
                assert Hashes.sha3_256(enc.ciphertext).hex() == v["ct_sha3_256"]
                assert enc.shared_secret.export_bytes_unsafe().hex() == v["ss"]
                with kem.decapsulate(kp.secret_key, enc.ciphertext) as ss:
                    assert ss.equals(enc.shared_secret)
    with kem.generate() as kp:
        bad = b"\xff" * 32 + kp.public_key[32:]
        with pytest.raises(zxv.ZxvUsageError) as e:
            kem.encapsulate(bad)
        assert e.value.status_name == "ZXV_E_FORMAT"


def test_secret_key_is_wiped_on_close() -> None:
    sk = SecretKey.from_bytes(b"\x5a" * 32)
    raw = sk._cdata()  # keep the cffi buffer alive to inspect it after close
    assert bytes(ffi.buffer(raw, 32)) == b"\x5a" * 32
    sk.close()
    assert bytes(ffi.buffer(raw, 32)) == b"\x00" * 32, "zxv_secure_zero wiped the buffer"
    assert sk.closed and "wiped" in repr(sk)
    with pytest.raises(ValueError):
        sk.export_bytes_unsafe()
    sk.close()  # idempotent
    # __del__ backstop
    sk2 = SecretKey.from_bytes(b"\x33" * 16)
    raw2 = sk2._cdata()
    del sk2
    gc.collect()
    assert bytes(ffi.buffer(raw2, 16)) == b"\x00" * 16
    with pytest.raises(TypeError):
        import pickle

        pickle.dumps(SecretKey(4))


# ------------------------------------------------------------------ EVM


def test_evm_state_vectors(cap: zxv.Capability, vectors: Dict[str, Any]) -> None:
    v = vectors["evm_state"]
    ch = zxv.StateChannel(cap, bytes.fromhex(v["channel_id"]))
    st = ch.encode_state(v["nonce"], [bytes.fromhex(a) for a in v["addresses"]], v["balances"])
    assert st.encoding.hex() == v["encoding"]
    assert st.digest.hex() == v["digest"]
    assert Hashes.keccak256(st.encoding) == st.digest


def test_evm_ledger_state_equals_explicit(ctx: zxv.Context, cap: zxv.Capability) -> None:
    ctx.currencies.enable("USD", 2, cap=cap)
    ledger = zxv.Ledger(cap)
    a = ledger.open_account("a", "USD", zxv.AccountKind.ISSUER)
    b = ledger.open_account("b", "USD")
    ledger.post(a, b, 250, "USD", "x")
    addrs = [bytes(range(1, 21)), bytes(range(21, 41))]
    ch = zxv.StateChannel(cap, bytes(32))
    s1 = ch.ledger_state(1, "USD", [a, b], addrs)
    s2 = ch.encode_state(1, addrs, [-250, 250])
    assert s1 == s2 and len(s1.encoding) == 10 * 32
    with pytest.raises(zxv.CapabilityDeniedError):
        zxv.StateChannel(cap.attenuate(Cap.LEDGER_READ), bytes(32)).encode_state(1, addrs, [1, 2])
