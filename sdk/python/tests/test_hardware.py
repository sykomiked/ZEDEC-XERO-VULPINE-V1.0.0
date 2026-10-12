# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""ABI 1.4: hardware emulation (profiles, the matrix) and hardware updates (regions),
mirroring sdk/libzxv/test/test_zxv_abi14.c. SLH-DSA signing takes seconds, so
the whole module signs exactly three packages (one shared release key)."""

from __future__ import annotations

import gc
import hashlib
import hmac
import struct
from typing import Any, Dict, Iterator, List, Optional

import pytest

import zxv
from zxv import (HW_PK_BYTES, HW_RES_HOST, HW_ROOT, Cap, CapabilityDeniedError, HwCapKey, HwClass,
                 HwEvent, HwFn, HwImageKind, HwKind, HwMatrix, HwPairing, HwPolicy, HwPower, HwProfile,
                 HwProviderFlag, HwRegion, HwRegionState, HwReleaseKey, HwResourceState, RegionEvent,
                 Truth, ZxvError, ZxvFatal, ZxvRefusedError, ZxvUsageError, encode_vectors)
from conftest import needs

pytestmark = needs(zxv.Feature.HW)

ABC = bytes.fromhex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")


def status(exc: Any) -> int:
    return int(exc.value.status)


@pytest.fixture
def hwcap(ctx: zxv.Context) -> zxv.Capability:
    return ctx.root_capability(Cap.ALL_1_4)


# ------------------------------------------------------------------ library facts


def test_version_features_statuses() -> None:
    assert zxv.abi_version()[:2] >= (1, 4)
    assert zxv.has_feature(zxv.Feature.HW)
    names = {-25: "ZXV_E_NOPROVIDER", -26: "ZXV_E_DISPUTE", -27: "ZXV_E_QUARANTINED",
             -28: "ZXV_E_ROLLBACK", -29: "ZXV_E_BLOCKED", -30: "ZXV_E_TARGET", -31: "ZXV_E_POLICY"}
    for code, name in names.items():
        assert zxv.status_name(code) == name
    assert zxv.status_class(-26) is zxv.StatusClass.REFUSED
    assert zxv.status_class(-30) is zxv.StatusClass.USAGE
    assert zxv.status_class(-31) is zxv.StatusClass.REFUSED
    assert Cap.ALL == (1 << 10) - 1 and Cap.ALL_1_4 == Cap.ALL | Cap.HW_MATRIX | Cap.HW_RECONF


# ------------------------------------------------------------------ profiles


def test_profiles_reference_host_present() -> None:
    with HwProfile.probe_host() as host, HwProfile.reference() as r1:
        assert len(r1) == 16
        assert len(host) >= 2
        assert host.get_cap(2, HwCapKey.WORD_BITS) == struct.calcsize("P") * 8
        assert (host.get_cap(2, HwCapKey.COUNT) or 0) >= 1
        nat, emu = HwProfile.match(host, r1)
        assert nat >= 1 and nat + emu == 14
        p = HwProfile.pair(host, r1, 1)
        assert (p.need_id, p.kind) == (2, HwPairing.GROUP)
        p = HwProfile.pair(host, r1, 2)
        assert (p.need_id, p.kind, p.have_id) == (3, HwPairing.NATIVE, 2)
        assert HwProfile.pair(host, r1, 7).kind is HwPairing.EMULATE
        with pytest.raises(ZxvUsageError) as e:
            HwProfile.pair(host, r1, 99)
        assert status(e) == -4
        assert r1.get_cap(6, HwCapKey.CONTRACT_ID) == HwFn.TENSOR
        with HwProfile.present(host, r1) as pres:
            assert pres.get_cap(3, HwCapKey.KIND) == HwKind.HARD
            assert pres.get_cap(8, HwCapKey.KIND) == HwKind.SOFT
            assert pres.get_cap(1, HwCapKey.KIND) is None
        nodes = r1.nodes()
        assert len(nodes) == 16 and nodes[0].parent == HW_ROOT and nodes[0].cls == HwClass.MACHINE


def test_profile_encoding_digest_extension() -> None:
    r1 = HwProfile.reference()
    enc = r1.encode()
    copy = HwProfile.decode(enc)
    assert copy.digest == r1.digest == hashlib.sha256(enc).digest()
    assert copy == r1
    with pytest.raises(ZxvError) as e:
        HwProfile.decode(enc[:-1])
    assert status(e) == -22  # ZXV_E_FORMAT

    mine = HwProfile.empty()
    mine.add_node(1, HW_ROOT, HwClass.MACHINE)
    mine.add_node(2, 1, HwClass.TERNARY_UNIT)
    mine.set_cap(2, HwCapKey.RADIX, 3)
    with pytest.raises(ZxvUsageError):
        mine.add_node(2, 1, HwClass.CACHE)  # ids strictly increase
    with pytest.raises(ZxvError) as e:
        mine.set_cap(9, HwCapKey.RADIX, 3)
    assert status(e) == -4
    mine.add_node(3, 1, zxv.HW_CLASS_PRIVATE | 1)
    p = HwProfile.pair(mine, r1, 7)
    assert p.kind is HwPairing.NATIVE and p.have_id == 2
    d1 = mine.digest
    assert mine.get_ext(3) == b""
    mine.set_ext(3, b"\xa5\x00\x5a")
    assert mine.digest != d1, "the extension is part of the identity"
    assert mine.get_ext(3) == b"\xa5\x00\x5a"
    with pytest.raises(ZxvError) as e:
        mine.set_ext(3, bytes(zxv.HW_EXT_MAX + 1))
    assert status(e) == -8  # ZXV_E_RANGE
    with pytest.raises(zxv.FloatRejectedError):
        mine.set_cap(2, HwCapKey.RADIX, 3.0)  # type: ignore[arg-type]
    for p_ in (r1, copy, mine):
        p_.close()
    with pytest.raises(ZxvUsageError):
        len(r1)


# ------------------------------------------------------------------ the matrix


def test_capability_gating(ctx: zxv.Context) -> None:
    with pytest.raises(CapabilityDeniedError):
        HwMatrix(ctx.root_capability())  # Cap.ALL has no HW_MATRIX bit
    only = ctx.root_capability(Cap.HW_MATRIX)
    mx = HwMatrix(only)
    with pytest.raises(CapabilityDeniedError):
        mx.trust_key(bytes(HW_PK_BYTES))  # needs HW_RECONF


def test_standard_engines_and_discovery(hwcap: zxv.Capability) -> None:
    with HwMatrix(hwcap) as mx:
        assert mx.call(HwFn.SHA256, b"abc") == ABC
        mac_in = struct.pack("<H", 4) + b"Jefe" + b"what do ya want for nothing?"
        assert mx.call(HwFn.MAC, mac_in) == hmac.new(b"Jefe", b"what do ya want for nothing?",
                                                     "sha256").digest()
        assert mx.call(HwFn.GCD, struct.pack("<QQ", 12, 18)) == struct.pack("<Q", 6)
        assert mx.call(HwFn.KECCAK, b"abc", op=0) == hashlib.sha3_256(b"abc").digest()

        infos = mx.providers()
        assert len(infos) == len(mx) >= 5
        for i in infos:
            assert i.kind is HwKind.SOFT and i.proof is Truth.TRUE and i.contract == 1
            assert not i.flags & HwProviderFlag.FAULT
        fns = {i.fn_id for i in infos}
        assert {HwFn.SHA256, HwFn.KECCAK, HwFn.MAC, HwFn.GCD, HwFn.LOGIC64} <= fns
        assert mx.bound(HwFn.SHA256) == next(i.index for i in infos if i.fn_id == HwFn.SHA256)
        with pytest.raises(ZxvUsageError):
            mx.provider_info(len(mx))
        fi = mx.function_info(HwFn.MAC)
        assert fi.contract == 1 and fi.policy & HwPolicy.KEY_BEARING and fi.vectors >= 1


def test_key_bearing_refuses_remote(hwcap: zxv.Capability) -> None:
    mx = HwMatrix(hwcap)
    mac_pv = next(i.index for i in mx.providers() if i.fn_id == HwFn.MAC)
    remote_hits: List[int] = []

    def remote(op: int, data: bytes) -> bytes:
        remote_hits.append(op)
        raise ZxvRefusedError(-9, "ZXV_E_STATE")  # must never serve key-bearing work

    mx.add_resource(7)
    prem = mx.add_provider(HwFn.MAC, 7, HwKind.REMOTE, 1, remote)
    mx.attest(prem)  # fails its vectors anyway
    mac_in = struct.pack("<H", 4) + b"Jefe" + b"hi"
    assert mx.call(HwFn.MAC, mac_in) == hmac.new(b"Jefe", b"hi", "sha256").digest()
    assert mx.bound(HwFn.MAC) == mac_pv
    assert mx.provider_info(prem).flags & HwProviderFlag.POLICY
    with pytest.raises(ZxvRefusedError) as e:
        mx.set_policy(HwFn.MAC, HwPolicy.NONE)  # sticky
    assert status(e) == -31
    with pytest.raises(ZxvRefusedError) as e:
        mx.memo(HwFn.MAC, 16)
    assert status(e) == -31
    with pytest.raises(ZxvUsageError):
        mx.set_policy(HwFn.MAC, 0x80)  # type: ignore[arg-type]

    # A power budget: undeclared traits never qualify, declared ones do.
    g = struct.pack("<QQ", 12, 18)
    gcd_pv = mx.bound(HwFn.GCD)
    mx.set_policy(HwFn.GCD, power_budget_mw=500)
    with pytest.raises(ZxvRefusedError) as e:
        mx.call(HwFn.GCD, g)
    assert status(e) == -25
    mx.set_traits(gcd_pv, latency_ns=2000, power_mw=300, throughput_class=4)
    with pytest.raises(ZxvUsageError):
        mx.set_traits(gcd_pv, latency_ns=2000, power_mw=300, throughput_class=16)
    assert mx.call(HwFn.GCD, g) == struct.pack("<Q", 6)
    i = mx.provider_info(gcd_pv)
    assert i.throughput_class == 4 and i.power_class is HwPower.W1
    mx.set_policy(HwFn.GCD, HwPolicy.LOCAL_ONLY, max_latency_ns=1000)
    with pytest.raises(ZxvRefusedError):
        mx.call(HwFn.GCD, g)
    mx.set_policy(HwFn.GCD)
    mx.close()


def test_python_providers_failover_shadow_memo_contracts(hwcap: zxv.Capability) -> None:
    mx = HwMatrix(hwcap)
    mx.add_function(0x100, "add8")
    mx.add_function(0x101, "add16")
    with pytest.raises(ZxvError) as e:
        mx.add_function(0x100, "again")
    assert status(e) == -13
    mx.add_resource(1)
    mx.add_resource(2)
    vin, ex8, ex16 = bytes([0, 1, 9, 200]), bytes([8, 9, 17, 208]), bytes([16, 17, 25, 216])
    mx.add_vector(0x100, 0, vin, ex8)
    mx.add_vector(0x101, 0, vin, ex16)

    state: Dict[str, Any] = {"hard_bad": 0, "soft_bad": False}

    def add8(bad_key: str) -> Any:
        def impl(op: int, data: bytes) -> Any:
            bad = state[bad_key]
            if bad == 2:
                return -23  # ZXV_E_IO
            return bytes((b + 8 + (1 if bad and b == 9 else 0)) & 0xFF for b in data)
        return impl

    def add_twice(op: int, data: bytes) -> bytes:
        return mx.call(0x100, mx.call(0x100, data, op=op), op=op)

    pfx = mx.add_provider(0x100, 2, HwKind.HARD, 1, add8("hard_bad"))
    psw = mx.add_provider(0x100, 1, HwKind.SOFT, 10, add8("soft_bad"))
    p16 = mx.add_provider(0x101, 1, HwKind.SOFT, 20, add_twice)
    with pytest.raises(ValueError):
        mx.add_provider(0x100, 1, 9, 1, add8("soft_bad"))  # type: ignore[arg-type]

    with pytest.raises(ZxvRefusedError) as e:
        mx.call(0x100, vin)
    assert status(e) == -25  # proof before service
    assert mx.attest(pfx) is Truth.TRUE
    assert mx.attest(psw) is Truth.TRUE
    assert mx.attest(p16) is Truth.FALSE, "an undeclared dependency must not prove"
    mx.add_dependency(p16, 0x100)
    assert mx.attest(p16) is Truth.TRUE
    assert mx.call(0x101, vin) == ex16

    assert mx.bound(0x100) == pfx
    mx.set_resource(2, HwResourceState.FAULTED)
    assert mx.call(0x100, vin) == ex8
    assert mx.bound(0x100) == psw, "failover to the emulator"
    mx.set_resource(2, HwResourceState.OK)

    state["hard_bad"] = 2
    with pytest.raises(ZxvError) as e:
        mx.call(0x100, vin)
    assert status(e) == -23, "a provider's status comes back as is"
    state["hard_bad"] = 0

    out, checked = mx.shadow(0x100, vin)
    assert out == ex8 and checked
    state["soft_bad"] = True
    with pytest.raises(ZxvRefusedError) as e:
        mx.shadow(0x100, vin)
    assert status(e) == -26  # ZXV_E_DISPUTE
    with pytest.raises(ZxvRefusedError):
        mx.call(0x100, vin)
    assert mx.attest(pfx) is Truth.TRUE
    assert mx.attest(psw) is Truth.FALSE

    with pytest.raises(ZxvUsageError):
        mx.memo(0x100, 6)
    mx.memo(0x100, 16)
    calls0 = mx.provider_info(pfx).calls
    mx.call(0x100, vin)
    assert mx.call(0x100, vin) == ex8
    hits, misses = mx.memo(0x100, 16)
    assert mx.provider_info(pfx).calls == calls0 + 1 and (hits, misses) == (1, 1)

    fi = mx.function_info(0x100)
    assert (fi.contract, fi.policy, fi.vectors) == (0, HwPolicy.NONE, 1)
    mx.set_contract(0x100, 2)
    with pytest.raises(ZxvRefusedError) as e:
        mx.set_contract(0x100, 2)
    assert status(e) == -28
    mx.add_vector(0x100, 0, vin, ex8)
    mx.attest(pfx)
    i = mx.provider_info(pfx)
    assert i.proof is Truth.TRUE and i.contract == 2
    assert mx.provider_info(psw).flags & HwProviderFlag.UNPROVEN

    # A Python exception inside an implementation is re-raised from the call.
    def broken(op: int, data: bytes) -> bytes:
        raise KeyError("boom")

    mx.add_function(0x102)
    mx.add_vector(0x102, 0, b"\x01", b"\x02")
    pb = mx.add_provider(0x102, 1, HwKind.SOFT, 1, broken)
    with pytest.raises(KeyError):
        mx.attest(pb)
    mx.close()
    gc.collect()


# ------------------------------------------------------------------ packages and regions


class Sim:
    """A simulated two-slot device; the read-back oracle is the SHA-256 engine."""

    def __init__(self, oracle: HwMatrix) -> None:
        self.slot = [b"", b""]
        self.active = -1
        self.refuse = False
        self.oracle = oracle

    def load(self, slot: int, payload: bytes) -> None:
        if slot > 1 or len(payload) > 256:
            raise ZxvUsageError(-8, "ZXV_E_RANGE")
        self.slot[slot] = payload

    def readback(self, slot: int) -> bytes:
        return self.oracle.call(HwFn.SHA256, self.slot[slot])

    def activate(self, slot: int) -> None:
        if self.refuse:
            raise ZxvError(-23, "ZXV_E_IO")
        self.active = slot

    def call(self, op: int, data: bytes) -> bytes:
        if self.active < 0:
            return -9  # type: ignore[return-value]
        table = self.slot[self.active]
        return bytes(table[b] for b in data)


@pytest.fixture(scope="module")
def release() -> Iterator[Dict[str, Any]]:
    """One release key and the module's three signed packages."""
    with zxv.Context() as c:
        cap = c.root_capability(Cap.ALL_1_4)
        key = HwReleaseKey.generate(cap)
        inc = bytes((i + 1) & 0xFF for i in range(256))
        wrong = bytearray(inc)
        wrong[77] = 0
        pkgs = {
            "good": key.build(cap, HwImageKind.PARTIAL, target_class=0xD0, fn_id=0x200, version=1, svn=1,
                              payload=inc),
            "bad": key.build(cap, HwImageKind.PARTIAL, target_class=0xD0, fn_id=0x200, version=2, svn=1,
                             payload=bytes(wrong)),
            "vectors": key.build(cap, HwImageKind.VECTORS, fn_id=0x200, version=7, svn=3,
                                 payload=encode_vectors([(0, b"\x05", b"\x06"), (0, b"\xfa", b"\xfb")])),
        }
        yield {"key": key, "pk": key.public_key, **pkgs}
        key.close()
        assert key.mldsa_secret.closed and key.slhdsa_secret.closed


def test_release_key(release: Dict[str, Any], hwcap: zxv.Capability) -> None:
    key: HwReleaseKey = release["key"]
    assert len(key.public_key) == HW_PK_BYTES
    assert len(release["good"]) == zxv.HW_HEADER_BYTES + 256 + zxv.HW_SIG_BYTES
    with pytest.raises(TypeError):
        import pickle
        pickle.dumps(key)
    with pytest.raises(ZxvError) as e:  # not a vector list: refused before signing
        key.build(hwcap, HwImageKind.VECTORS, fn_id=0x200, version=8, svn=3, payload=b"\x00\x01")
    assert status(e) == -22


def test_region_update_cycle(release: Dict[str, Any], hwcap: zxv.Capability) -> None:
    cap = hwcap
    oracle = HwMatrix(cap)
    mx = HwMatrix(cap)
    mx.add_function(0x200, "inc")
    mx.add_resource(1)
    mx.add_resource(2)
    vin = bytes(range(256))
    vinc = bytes((i + 1) & 0xFF for i in range(256))
    mx.add_vector(0x200, 0, vin, vinc)
    sim = Sim(oracle)
    psoft = mx.add_provider(0x200, 1, HwKind.SOFT, 50, lambda op, d: bytes((b + 1) & 0xFF for b in d))
    preg = mx.add_provider(0x200, 2, HwKind.FABRIC, 1, sim.call)
    assert mx.attest(psoft) is Truth.TRUE

    nohw = cap.attenuate(Cap.ALL)
    with pytest.raises(CapabilityDeniedError):
        HwRegion(nohw, sim, region_id=1, target_class=0xD0, matrix=mx, provider=preg)
    with pytest.raises(ZxvUsageError):  # a SOFT implementation is not a region
        HwRegion(cap, sim, region_id=1, target_class=0xD0, matrix=mx, provider=psoft)
    rg = HwRegion(cap, sim, region_id=1, target_class=0xD0, matrix=mx, provider=preg, log_capacity=12)
    events: List[RegionEvent] = []
    rg.on_event(events.append)
    rg.trust_key(release["pk"])

    good: bytes = release["good"]
    wrong_hw = bytearray(good)
    wrong_hw[12:16] = struct.pack("<I", 0xD1)
    with pytest.raises(ZxvError) as e:
        rg.stage(bytes(wrong_hw))
    assert status(e) == -30  # ZXV_E_TARGET
    tampered = bytearray(good)
    tampered[zxv.HW_HEADER_BYTES + 256 + 100] ^= 1  # inside the ML-DSA signature
    with pytest.raises(zxv.VerificationFailedError):
        rg.stage(bytes(tampered))  # recorded as REJECTED

    rg.stage(good)
    with pytest.raises(ZxvRefusedError) as e:
        rg.commit()
    assert status(e) == -9
    rg.trial(5)
    assert mx.bound(0x200) == preg, "the reconfigured region serves"
    rg.commit()
    info = rg.info
    assert (info.state, info.version, info.floor, info.quarantined) == (HwRegionState.ACTIVE, 1, 1, 1)
    assert [e.event for e in events] == [HwEvent.REJECTED, HwEvent.STAGED, HwEvent.TESTED,
                                         HwEvent.ACTIVATED]
    assert events[-1].version == 1 and all(e.region_id == 1 for e in events)
    assert mx.call(0x200, b"\x00\x05") == b"\x01\x06"

    with pytest.raises(ZxvRefusedError) as e:
        rg.stage(good)  # replay
    assert status(e) == -28

    rg.stage(release["bad"])
    with pytest.raises(ZxvRefusedError) as e:
        rg.trial(5)
    assert status(e) == -27  # ZXV_E_QUARANTINED: rolled back to v1
    info = rg.info
    assert info.state is HwRegionState.ACTIVE and info.version == 1
    assert mx.bound(0x200) == preg
    blocked = bytearray(release["bad"])
    blocked[20:24] = struct.pack("<I", 3)
    with pytest.raises(ZxvRefusedError) as e:
        rg.stage(bytes(blocked))
    assert status(e) == -29  # ZXV_E_BLOCKED

    head, count = rg.log()
    assert count == 8 and count == len(events)
    last = rg.log_entry(count - 1)
    assert last.event is HwEvent.QUARANTINED and last.id == 1 and last.version == 2
    assert last.hash == head and last.tag.is_forbidden
    act = rg.log_entry(3)
    assert act.event is HwEvent.ACTIVATED and act.version == 1 and not act.tag.is_forbidden
    assert [r.event for r in rg.log_entries()] == [e.event for e in events]
    with pytest.raises(ZxvUsageError):
        rg.log_entry(count)

    # Contract packages: a newer golden-vector set for "inc", signed and applied.
    pkg: bytes = release["vectors"]
    with pytest.raises(zxv.VerificationFailedError):
        mx.apply_contract(pkg)  # no key trusted yet
    mx.trust_key(release["pk"])
    with pytest.raises(ZxvError) as e:
        mx.apply_contract(good)  # an image, not vectors
    assert status(e) == -22
    with pytest.raises(ZxvError) as e:
        rg.stage(pkg)  # and the other way
    assert status(e) == -22
    assert mx.apply_contract(pkg) == 2
    fi = mx.function_info(0x200)
    assert fi.contract == 7 and fi.vectors == 2
    with pytest.raises(ZxvRefusedError) as e:
        mx.apply_contract(pkg)
    assert status(e) == -28
    mhead, mcount = mx.log()
    assert mcount == 3
    rec = mx.log_entry(1)
    assert rec.event is HwEvent.CONTRACT and rec.id == 0x200 and rec.version == 7

    # Fill the region's log (12): the next recorded refusal halts the region
    # and poisons the context (fail closed).
    for _ in range(12 - rg.log()[1]):
        with pytest.raises(zxv.VerificationFailedError):
            rg.stage(bytes(tampered))
    with pytest.raises(ZxvFatal):
        rg.stage(bytes(tampered))
    assert cap.context.dead
    rg.close()
    mx.close()
    oracle.close()


def test_device_exception_surfaces(release: Dict[str, Any], hwcap: zxv.Capability) -> None:
    class Broken(Sim):
        def load(self, slot: int, payload: bytes) -> None:
            raise RuntimeError("device on fire")

    oracle = HwMatrix(hwcap)
    rg = HwRegion(hwcap, Broken(oracle), region_id=9, target_class=0xD0)
    rg.trust_key(release["pk"])
    seen: List[RegionEvent] = []
    rg.on_event(seen.append)
    with pytest.raises(RuntimeError, match="on fire"):
        rg.stage(release["good"])
    assert rg.info.version == 0
    rg.close()
    oracle.close()
