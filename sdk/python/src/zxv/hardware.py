# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Hardware emulation and hardware updates (ABI 1.4, kernel ``hwemu``).

Three layers, each a thin wrapper over ``zxv_hw_*``:

* :class:`HwProfile` - a ZHP-1 machine profile, the "hardware ROM": a tree of
  abstract components with integer capability descriptors.
  :meth:`HwProfile.probe_host` describes this machine,
  :meth:`HwProfile.reference` is the public VULPINE-class reference machine
  R1, and :meth:`HwProfile.present` fills R1 from a host. Plain data: no
  capability needed.
* :class:`HwMatrix` - functions (engine contracts) served by interchangeable
  implementations (HARD, FABRIC, MICRO, PEER, SOFT, REMOTE). An
  implementation serves only after it reproduces the function's golden
  vectors bit for bit. Every matrix starts with the standard engines
  (:class:`HwFn`), each with a SOFT implementation. Needs ``Cap.HW_MATRIX``.
* :class:`HwRegion` - a reconfigurable region updated by signed images
  (ML-DSA-87 **and** SLH-DSA-SHAKE-256s): staged, read back, tested,
  committed or rolled back, each step an event and a hash-chained audit
  record. Needs ``Cap.HW_RECONF``. Images and keys (:class:`HwReleaseKey`)
  need ``Cap.CRYPTO_SIGN``.

Python implementations (:meth:`HwMatrix.add_provider`) and devices
(:class:`HwDevice`) are called synchronously on the thread that made the
matrix or region call. An implementation may raise a :class:`ZxvError` (or
return a negative status) to fail with that status; any other exception
fails the C call with ``ZXV_E_IO`` and is re-raised from the outermost
SDK call that caused it.
"""

from __future__ import annotations

import enum
import threading
from dataclasses import dataclass
from typing import Any, Callable, Iterable, Iterator, List, Optional, Protocol, Sequence, Tuple, Union

from . import _native
from ._handle import NativeHandle
from ._native import ffi
from .context import Capability
from .crypto import SecretKey
from .errors import ZxvError
from .hexagram import Hexagram
from .truth import Truth

__all__ = [
    "HwKind", "HwClass", "HwCapKey", "HwIsa", "HwPairing", "HwResourceState", "HwFn", "HwPolicy",
    "HwProviderFlag", "HwPower", "HwImageKind", "HwRegionState", "HwEvent",
    "HW_RES_HOST", "HW_ROOT", "HW_EXT_MAX", "HW_CLASS_PRIVATE", "HW_PK_BYTES", "HW_MLDSA_SK_BYTES",
    "HW_SLHDSA_SK_BYTES", "HW_SIG_BYTES", "HW_HEADER_BYTES",
    "HwProfile", "ProfileNode", "ProfilePairing", "HwMatrix", "FunctionInfo", "ProviderInfo",
    "HwLogEntry", "HwReleaseKey", "HwDevice", "HwRegion", "RegionInfo", "RegionEvent",
    "Implementation", "encode_vectors",
]

HW_RES_HOST = 0xFFFFFFF0
"""The resource every matrix starts with: this machine's CPU (``ZXV_HW_RES_HOST``)."""
HW_ROOT = 0xFFFF
"""Parent id of a root profile node (``ZXV_HW_ROOT``)."""
HW_EXT_MAX = 256
"""Largest private extension per profile node, in bytes."""
HW_CLASS_PRIVATE = 0x80000000
"""Component classes from here up are an implementer's own."""
HW_PK_BYTES = 2656
"""A release public key: the ML-DSA-87 key followed by the SLH-DSA one."""
HW_MLDSA_SK_BYTES = 4896
HW_SLHDSA_SK_BYTES = 128
HW_SIG_BYTES = 34419
"""Both signatures of a package (4627 + 29792)."""
HW_HEADER_BYTES = 96


class HwKind(enum.IntEnum):
    """Implementation kinds (``ZXV_HW_KIND_*``), preferred in this order at equal cost."""

    HARD = 0
    """Silicon built for the function."""
    FABRIC = 1
    """A reconfigurable fabric region."""
    MICRO = 2
    """A microcoded unit or soft core."""
    PEER = 3
    """Another local component standing in."""
    SOFT = 4
    """A software emulator."""
    REMOTE = 5
    """Another machine (never serves key-bearing or LOCAL_ONLY functions)."""


class HwClass(enum.IntEnum):
    """ZHP-1 component classes (``ZXV_HW_CLASS_*``); see also :data:`HW_CLASS_PRIVATE`."""

    MACHINE = 1
    CPU_CLUSTER = 2
    CPU_CORE = 3
    CACHE = 4
    MEMORY = 5
    INTERCONNECT = 6
    MATRIX_UNIT = 7
    VECTOR_UNIT = 8
    TERNARY_UNIT = 9
    HASH_UNIT = 10
    CRYPTO_UNIT = 11
    CODEC_UNIT = 12
    RECONF_FABRIC = 13
    STORAGE = 14
    NETWORK = 15
    RADIO = 16
    SENSOR = 17
    ACTUATOR = 18
    CLOCK = 19
    POWER = 20
    SECURE_ELEMENT = 21
    DISPLAY = 22
    AUDIO = 23
    WIRE = 24
    GPU = 25
    PACKAGE = 26
    LANE = 27
    LOGIC_UNIT = 28
    NTT_UNIT = 29
    FRAME_UNIT = 30
    GCD_UNIT = 31
    MEM_CHANNEL = 32
    SECURE_ISLAND = 33
    IO = 34


class HwCapKey(enum.IntEnum):
    """Capability keys (``ZXV_HW_CAP_*``): bit 15 critical, bits 14-13 comparison, bits 12-0 id."""

    WORD_BITS = 0x0001
    COUNT = 0x0002
    FREQ_KHZ = 0x6003
    BYTES = 0x0004
    LINE_BYTES = 0x6005
    ISA = 0x2006
    INT_WIDTHS = 0x4007
    RADIX = 0x2008
    MACS_PER_CYCLE = 0x0009
    SIMD = 0x400A
    TICK_NS = 0x600B
    REGIONS = 0x000C
    BANDWIDTH_MBPS = 0x000D
    LATENCY_NS = 0x600E
    PQ_ALGS = 0x400F
    HASH_ALGS = 0x4010
    WIRE_BITS = 0x2011
    PEAK_OPS_MHZ = 0x6012
    CONTRACT_ID = 0x2013
    """The engine contract (:class:`HwFn`)."""
    CONTRACT_VER = 0x0014
    KIND = 0x6015
    """The :class:`HwKind` serving the node."""
    CAPACITY_CLASS = 0x0016
    PERF_BUCKET = 0x0017
    POWER_BUCKET = 0x6018
    """A :class:`HwPower` value."""


class HwIsa(enum.IntEnum):
    """``ZXV_HW_CAP_ISA`` values."""

    X86_64 = 1
    ARM64 = 2
    RISCV64 = 3
    WASM32 = 4


class HwPairing(enum.IntEnum):
    """How a target component is served on a host (:meth:`HwProfile.pair`)."""

    NATIVE = 0
    """A host component satisfies it."""
    EMULATE = 1
    """Nothing does: the matrix emulates it."""
    GROUP = 2
    """A device or package node: nothing to run."""


class HwResourceState(enum.IntEnum):
    """``ZXV_HW_RES_*``."""

    OK = 0
    FAULTED = 1
    RECONFIGURING = 2
    OFFLINE = 3


class HwFn(enum.IntEnum):
    """The standard engines every matrix starts with (``ZXV_HW_FN_*``)."""

    SHA256 = 0x5A450001
    """op 0: SHA-256."""
    KECCAK = 0x5A450002
    """op 0/1: SHA3-256/512, op 2/3: SHAKE."""
    MAC = 0x5A450003
    """op 0: HMAC-SHA256 (u16 LE key length, key, message). Key-bearing."""
    GCD = 0x5A450004
    """op 0: n pairs of u64 -> n gcds."""
    LOGIC64 = 0x5A450005
    """64-lane four-valued AND / OR / NOT."""
    TENSOR = 0x5A450006
    """op 0: one Q8_0 row dot (absent in builds without the tensor engine)."""


class HwPolicy(enum.IntFlag):
    """Placement policy flags (``ZXV_HW_POLICY_*``)."""

    NONE = 0
    KEY_BEARING = 0x1
    """Always local, never memoized; sticky (clearing it is ``ZXV_E_POLICY``)."""
    LOCAL_ONLY = 0x2
    """No REMOTE implementation."""


class HwProviderFlag(enum.IntFlag):
    """Implementation status flags (``ZXV_HW_PF_*``)."""

    NONE = 0
    FAULT = 0x01
    DISPUTED = 0x02
    FAILED = 0x04
    UNPROVEN = 0x08
    DISABLED = 0x10
    POLICY = 0x20
    SERVING = 0x40


class HwPower(enum.IntEnum):
    """Coarse power classes (``ZXV_HW_POWER_*``)."""

    UNKNOWN = 0
    MW10 = 1
    MW100 = 2
    W1 = 3
    W10 = 4
    W100 = 5
    MORE = 6


class HwImageKind(enum.IntEnum):
    """Package kinds (``ZXV_HW_IMG_*``)."""

    BITSTREAM = 1
    PARTIAL = 2
    MICROCODE = 3
    FIRMWARE = 4
    VECTORS = 5
    """A new golden-vector contract for a function (:meth:`HwMatrix.apply_contract`)."""


class HwRegionState(enum.IntEnum):
    """``ZXV_HW_REGION_*``."""

    EMPTY = 0
    ACTIVE = 1
    STAGED = 2
    PROBATION = 3
    HALTED = 4


class HwEvent(enum.IntEnum):
    """Region events and audit records (``ZXV_HW_EV_*``)."""

    STAGED = 1
    TESTED = 2
    ACTIVATED = 3
    ROLLED_BACK = 4
    REJECTED = 5
    QUARANTINED = 6
    CONTRACT = 7


# ---------------------------------------------------------------- helpers

_E_BUFFER = -2
_E_IO = -23


class _Pending(threading.local):
    """Python exceptions raised inside callbacks, per thread, re-raised by the outermost call."""

    depth = 0
    errors: List[BaseException]

    def __init__(self) -> None:
        self.errors = []


_pending = _Pending()


def _guarded(call: Callable[[], int], owner: Any) -> None:
    """Run one C call; raise its status, or a Python exception a callback raised in it."""
    p = _pending
    if p.depth == 0:
        p.errors = []
    p.depth += 1
    try:
        rc = call()
    finally:
        p.depth -= 1
    if p.depth == 0 and p.errors:
        first, p.errors = p.errors[0], []
        if rc < 0:
            try:
                _native.check(rc, owner)
            except BaseException as status_exc:
                raise first from status_exc
        raise first
    _native.check(rc, owner)


def _status_of(exc: BaseException) -> int:
    if isinstance(exc, ZxvError):
        return exc.status
    _pending.errors.append(exc)
    return _E_IO


def _u32(v: Any, what: str) -> int:
    return _native.exact_int(v, what, *_native.U32)


def _fixed(b: Any, n: int, what: str) -> bytes:
    data = _native.to_bytes(b, what)
    if len(data) != n:
        raise _native.make_error(-1, f"{what} must be exactly {n} bytes, got {len(data)}")
    return data


def _buf(data: bytes) -> Any:
    return ffi.from_buffer("uint8_t[]", data) if data else ffi.NULL


def encode_vectors(vectors: Iterable[Tuple[int, bytes, bytes]]) -> bytes:
    """Encode a golden-vector list, the payload of a :attr:`HwImageKind.VECTORS` package.

    Each vector is ``(op, input, expected_output)``; the encoding is, per
    vector, ``u32 op, u32 in_len, in, u32 out_len, out`` (little-endian).
    """
    out = bytearray()
    for op, inp, exp in vectors:
        i = _native.to_bytes(inp, "input")
        e = _native.to_bytes(exp, "expected output")
        out += _u32(op, "op").to_bytes(4, "little")
        out += len(i).to_bytes(4, "little") + i
        out += len(e).to_bytes(4, "little") + e
    return bytes(out)


# ---------------------------------------------------------------- profiles


@dataclass(frozen=True)
class ProfileNode:
    """One component of a :class:`HwProfile`."""

    id: int
    parent: int
    """The parent's id, or :data:`HW_ROOT`."""
    cls: int
    """A :class:`HwClass` value (or a private class >= :data:`HW_CLASS_PRIVATE`)."""
    ncaps: int


@dataclass(frozen=True)
class ProfilePairing:
    """How one target node is served on a host."""

    need_id: int
    """The target node id."""
    kind: HwPairing
    have_id: int
    """The host node id (NATIVE)."""
    unmet_key: int
    """The first unmet capability key (EMULATE; 0 if the host lacks the class)."""


class HwProfile(NativeHandle):
    """A ZHP-1 machine profile (``zxv_hw_profile``). Plain data: no capability needed.

    Create one with :meth:`empty`, :meth:`probe_host`, :meth:`reference`,
    :meth:`decode` or :meth:`present`. ``len(p)`` is the node count.
    """

    def __init__(self, _handle: Any) -> None:
        super().__init__(_handle, None, None)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_hw_profile_destroy(handle)

    @classmethod
    def _make(cls, fn: Callable[[Any], int]) -> "HwProfile":
        out = ffi.new("zxv_hw_profile **")
        _native.check(fn(out))
        return cls(out[0])

    @classmethod
    def empty(cls) -> "HwProfile":
        """A profile with no nodes."""
        return cls._make(_native.lib().zxv_hw_profile_create)

    @classmethod
    def probe_host(cls) -> "HwProfile":
        """This machine: CPU cluster (ISA, cores, SIMD), memory, cache line. Nothing is guessed."""
        return cls._make(_native.lib().zxv_hw_profile_probe_host)

    @classmethod
    def reference(cls) -> "HwProfile":
        """A copy of the public VULPINE-class reference machine R1 (16 nodes)."""
        return cls._make(_native.lib().zxv_hw_profile_reference)

    @classmethod
    def decode(cls, data: bytes) -> "HwProfile":
        """Parse a canonical ZHP-1 encoding (anything non-canonical: ``ZXV_E_FORMAT``)."""
        b = _native.to_bytes(data, "profile")
        return cls._make(lambda out: _native.lib().zxv_hw_profile_decode(_buf(b), len(b), out))

    @classmethod
    def present(cls, host: "HwProfile", target: "HwProfile") -> "HwProfile":
        """``target`` as served on ``host``: every running node carries ``HwCapKey.KIND``
        (HARD where the host has it, SOFT where it is emulated)."""
        return cls._make(lambda out: _native.lib().zxv_hw_profile_present(
            host._handle(), target._handle(), out))

    def encode(self) -> bytes:
        """The canonical encoding."""
        L = _native.lib()
        n = ffi.new("size_t *")
        rc = L.zxv_hw_profile_encode(self._handle(), ffi.NULL, 0, n)
        if rc != _E_BUFFER:
            self._check(rc)
        buf = ffi.new("uint8_t[]", max(n[0], 1))
        self._check(L.zxv_hw_profile_encode(self._handle(), buf, n[0], n))
        return bytes(ffi.buffer(buf, n[0]))

    @property
    def digest(self) -> bytes:
        """SHA-256 of the canonical encoding: the profile's identity."""
        out = ffi.new("uint8_t[32]")
        self._check(_native.lib().zxv_hw_profile_digest(self._handle(), out))
        return bytes(ffi.buffer(out, 32))

    def add_node(self, node_id: int, parent: int, cls: int) -> None:
        """Add a node: ids strictly increase, parent first (or :data:`HW_ROOT`)."""
        self._check(_native.lib().zxv_hw_profile_node_add(
            self._handle(), _u32(node_id, "node_id"), _u32(parent, "parent"), _u32(cls, "cls")))

    def set_cap(self, node_id: int, key: int, value: int) -> None:
        """Set capability ``key`` (:class:`HwCapKey`) of a node."""
        self._check(_native.lib().zxv_hw_profile_cap_set(
            self._handle(), _u32(node_id, "node_id"), _u32(key, "key"),
            _native.exact_int(value, "value", *_native.U64)))

    def get_cap(self, node_id: int, key: int) -> Optional[int]:
        """The node's capability ``key``, or ``None`` if it lacks it."""
        out = ffi.new("uint64_t *")
        rc = _native.lib().zxv_hw_profile_cap_get(self._handle(), _u32(node_id, "node_id"),
                                                  _u32(key, "key"), out)
        if rc == -4:  # ZXV_E_NOT_FOUND: the node exists but lacks the cap, or no such node
            return None
        self._check(rc)
        return int(out[0])

    def set_ext(self, node_id: int, ext: bytes) -> None:
        """The node's opaque private extension (at most :data:`HW_EXT_MAX` bytes)."""
        b = _native.to_bytes(ext, "ext")
        self._check(_native.lib().zxv_hw_profile_ext_set(self._handle(), _u32(node_id, "node_id"),
                                                         _buf(b), len(b)))

    def get_ext(self, node_id: int) -> bytes:
        """The node's private extension (empty if none)."""
        L = _native.lib()
        nid = _u32(node_id, "node_id")
        n = ffi.new("size_t *")
        rc = L.zxv_hw_profile_ext_get(self._handle(), nid, ffi.NULL, 0, n)
        if rc == 0:
            return b""
        if rc != _E_BUFFER:
            self._check(rc)
        buf = ffi.new("uint8_t[]", max(n[0], 1))
        self._check(L.zxv_hw_profile_ext_get(self._handle(), nid, buf, n[0], n))
        return bytes(ffi.buffer(buf, n[0]))

    def __len__(self) -> int:
        out = ffi.new("uint32_t *")
        self._check(_native.lib().zxv_hw_profile_count(self._handle(), out))
        return int(out[0])

    def node(self, index: int) -> ProfileNode:
        """Node ``index`` (in id order)."""
        i, p, c, n = (ffi.new("uint32_t *") for _ in range(4))
        self._check(_native.lib().zxv_hw_profile_node(self._handle(), _u32(index, "index"), i, p, c, n))
        return ProfileNode(int(i[0]), int(p[0]), int(c[0]), int(n[0]))

    def nodes(self) -> List[ProfileNode]:
        """Every node, in id order."""
        return [self.node(k) for k in range(len(self))]

    @staticmethod
    def match(host: "HwProfile", target: "HwProfile") -> Tuple[int, int]:
        """``(native, emulated)``: how many target components run natively on ``host``."""
        nat, emu = ffi.new("uint32_t *"), ffi.new("uint32_t *")
        _native.check(_native.lib().zxv_hw_profile_match(host._handle(), target._handle(), nat, emu))
        return int(nat[0]), int(emu[0])

    @staticmethod
    def pair(host: "HwProfile", target: "HwProfile", index: int) -> ProfilePairing:
        """How target node ``index`` (in target order) is served on ``host``."""
        need, have, unmet = ffi.new("uint32_t *"), ffi.new("uint32_t *"), ffi.new("uint32_t *")
        kind = ffi.new("int32_t *")
        _native.check(_native.lib().zxv_hw_profile_pair(host._handle(), target._handle(),
                                                        _u32(index, "index"), need, kind, have, unmet))
        return ProfilePairing(int(need[0]), HwPairing(kind[0]), int(have[0]), int(unmet[0]))

    def __eq__(self, other: object) -> bool:
        return isinstance(other, HwProfile) and other.digest == self.digest

    def __hash__(self) -> int:
        return hash(self.digest)

    def __repr__(self) -> str:
        if self._closed:
            return "<zxv.HwProfile closed>"
        return f"<zxv.HwProfile {len(self)} nodes {self.digest.hex()[:16]}>"


# ---------------------------------------------------------------- matrix

Implementation = Callable[[int, bytes], Union[bytes, bytearray, memoryview, int]]
"""An implementation: ``fn(op, input) -> output bytes`` (or a negative status)."""


@dataclass(frozen=True)
class FunctionInfo:
    """What ``zxv_hw_function_info`` reports."""

    contract: int
    """The current contract (golden-vector) version."""
    policy: HwPolicy
    vectors: int


@dataclass(frozen=True)
class ProviderInfo:
    """One implementation, as discovery reports it (``zxv_hw_provider_info``)."""

    index: int
    fn_id: int
    kind: HwKind
    proof: Truth
    """TRUE serves; FALSE failed a vector; NEITHER unproven or no vectors; BOTH disputed."""
    contract: int
    """The contract version it last proved."""
    flags: HwProviderFlag
    throughput_class: int
    power_class: HwPower
    calls: int
    cost_spent: int


@dataclass(frozen=True)
class HwLogEntry:
    """One audit record (a region's or a matrix's)."""

    event: HwEvent
    reason: int
    """The failure code (0 for none)."""
    id: int
    """The region id (the function id for :attr:`HwEvent.CONTRACT`)."""
    version: int
    tag: Hexagram
    subject: bytes
    """SHA-256 of the package header."""
    hash: bytes
    """The record's chain hash."""


def _read_log(fn: Callable[..., int], owner: NativeHandle, index: int) -> HwLogEntry:
    ev = ffi.new("int32_t *")
    reason, rid, ver, tag = (ffi.new("uint32_t *") for _ in range(4))
    subj, h = ffi.new("uint8_t[32]"), ffi.new("uint8_t[32]")
    _guarded(lambda: fn(_u32(index, "index"), ev, reason, rid, ver, tag, subj, h), owner._context)
    return HwLogEntry(HwEvent(ev[0]), int(reason[0]), int(rid[0]), int(ver[0]),
                      Hexagram.from_tag(int(tag[0])), bytes(ffi.buffer(subj, 32)),
                      bytes(ffi.buffer(h, 32)))


def _call_tramp(user: Any, op: int, inp: Any, n: int, out: Any, cap: int, out_len: Any) -> int:
    try:
        impl: Implementation = ffi.from_handle(user)
        data = bytes(ffi.buffer(inp, n)) if n else b""
        r = impl(int(op), data)
        if isinstance(r, int) and not isinstance(r, bool):
            return r if r < 0 else _E_IO
        b = _native.to_bytes(r, "implementation output")
        out_len[0] = len(b)
        if len(b) > cap:
            return _E_BUFFER
        if b:
            ffi.memmove(out, b, len(b))
        return 0
    except BaseException as exc:  # never unwind through C
        return _status_of(exc)


_C_call_tramp: Any = ffi.callback("zxv_hw_call_fn", _call_tramp)


class HwMatrix(NativeHandle):
    """The emulation matrix (``zxv_hw_matrix``): functions x implementations.

    Every call needs ``Cap.HW_MATRIX``; :meth:`trust_key` and
    :meth:`apply_contract` need ``Cap.HW_RECONF``. A new matrix holds the
    :data:`HW_RES_HOST` resource and the standard engines (:class:`HwFn`),
    each served by its proven SOFT implementation.
    """

    def __init__(self, cap: Capability) -> None:
        ctx = cap.context
        out = ffi.new("zxv_hw_matrix **")
        _native.check(_native.lib().zxv_hw_matrix_create_cap(cap._handle(), out), ctx)
        self._cap = cap
        self._impls: List[Any] = []  # new_handle objects C may call back into
        super().__init__(out[0], cap, ctx)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_hw_matrix_destroy(handle)
        self._impls = []

    @property
    def capability(self) -> Capability:
        return self._cap

    def _run(self, fn: Callable[..., int], *args: Any) -> None:
        c, h = self._cap._handle(), self._handle()
        _guarded(lambda: fn(c, h, *args), self._context)

    # --------------------------------------------------- functions

    def add_function(self, fn_id: int, name: Optional[str] = None) -> None:
        """Declare a function (``name``: UTF-8, at most 23 bytes)."""
        n = _native.to_cstr(name, "name") if name is not None else ffi.NULL
        self._run(_native.lib().zxv_hw_function_add, _u32(fn_id, "fn_id"), n)

    def add_vector(self, fn_id: int, op: int, input: bytes, expect: bytes) -> None:
        """Add a golden vector to the current contract (output at most 512 bytes).
        Every implementation of the function becomes unproven."""
        i = _native.to_bytes(input, "input")
        e = _native.to_bytes(expect, "expect")
        self._run(_native.lib().zxv_hw_vector_add, _u32(fn_id, "fn_id"), _u32(op, "op"), _buf(i),
                  len(i), _buf(e), len(e))

    def set_contract(self, fn_id: int, version: int) -> None:
        """Start contract ``version`` (strictly newer, else ``ZXV_E_ROLLBACK``); clears the vectors."""
        self._run(_native.lib().zxv_hw_function_contract, _u32(fn_id, "fn_id"), _u32(version, "version"))

    def set_policy(self, fn_id: int, flags: HwPolicy = HwPolicy.NONE, *, max_latency_ns: int = 0,
                   power_budget_mw: int = 0) -> None:
        """Placement constraints (0: none). Clearing KEY_BEARING once set: ``ZXV_E_POLICY``."""
        self._run(_native.lib().zxv_hw_policy_set, _u32(fn_id, "fn_id"), _u32(int(flags), "flags"),
                  _u32(max_latency_ns, "max_latency_ns"), _u32(power_budget_mw, "power_budget_mw"))

    def function_info(self, fn_id: int) -> FunctionInfo:
        """Contract version, policy and vector count of a function."""
        c, p, v = (ffi.new("uint32_t *") for _ in range(3))
        self._run(_native.lib().zxv_hw_function_info, _u32(fn_id, "fn_id"), c, p, v)
        return FunctionInfo(int(c[0]), HwPolicy(p[0]), int(v[0]))

    # --------------------------------------------------- resources and implementations

    def add_resource(self, res_id: int) -> None:
        self._run(_native.lib().zxv_hw_resource_add, _u32(res_id, "res_id"))

    def set_resource(self, res_id: int, state: HwResourceState) -> None:
        self._run(_native.lib().zxv_hw_resource_set, _u32(res_id, "res_id"), int(HwResourceState(state)))

    def add_provider(self, fn_id: int, res_id: int, kind: HwKind, cost: int,
                     impl: Implementation) -> int:
        """Add a Python implementation; returns its index. It serves only after :meth:`attest`.

        ``impl(op, input)`` returns the output bytes. It may call :meth:`call`
        on this matrix for functions declared with :meth:`add_dependency`.
        """
        if not callable(impl):
            raise TypeError("impl must be callable")
        handle = ffi.new_handle(impl)
        out = ffi.new("uint32_t *")
        self._run(_native.lib().zxv_hw_provider_add, _u32(fn_id, "fn_id"), _u32(res_id, "res_id"),
                  int(HwKind(kind)), _u32(cost, "cost"), _C_call_tramp, handle, out)
        self._impls.append(handle)
        return int(out[0])

    def set_traits(self, provider: int, *, latency_ns: int, power_mw: int, throughput_class: int) -> None:
        """Declared traits used by placement and discovery (throughput class 0..15)."""
        self._run(_native.lib().zxv_hw_provider_traits, _u32(provider, "provider"),
                  _u32(latency_ns, "latency_ns"), _u32(power_mw, "power_mw"),
                  _u32(throughput_class, "throughput_class"))

    def add_dependency(self, provider: int, dep_fn_id: int) -> None:
        """Let ``provider`` call function ``dep_fn_id`` of this matrix."""
        self._run(_native.lib().zxv_hw_provider_dep, _u32(provider, "provider"), _u32(dep_fn_id, "dep_fn_id"))

    def enable(self, provider: int, on: bool = True) -> None:
        if not isinstance(on, bool):
            raise TypeError("on must be a bool")
        self._run(_native.lib().zxv_hw_provider_enable, _u32(provider, "provider"), 1 if on else 0)

    def attest(self, provider: int) -> Truth:
        """Run the golden vectors: TRUE (serves), FALSE (a vector failed) or NEITHER (no vectors)."""
        lp = ffi.new("int32_t *")
        self._run(_native.lib().zxv_hw_attest, _u32(provider, "provider"), lp)
        return Truth(lp[0])

    def __len__(self) -> int:
        """The number of implementations (discovery indexes 0 .. len-1)."""
        out = ffi.new("uint32_t *")
        self._run(_native.lib().zxv_hw_provider_count, out)
        return int(out[0])

    def provider_info(self, provider: int) -> ProviderInfo:
        """Discovery: one implementation."""
        i = _u32(provider, "provider")
        fn, con, fl, tp, pw = (ffi.new("uint32_t *") for _ in range(5))
        kind, lp = ffi.new("int32_t *"), ffi.new("int32_t *")
        calls, spent = ffi.new("uint64_t *"), ffi.new("uint64_t *")
        self._run(_native.lib().zxv_hw_provider_info, i, fn, kind, lp, con, fl, tp, pw, calls, spent)
        return ProviderInfo(i, int(fn[0]), HwKind(kind[0]), Truth(lp[0]), int(con[0]),
                            HwProviderFlag(fl[0]), int(tp[0]), HwPower(pw[0]), int(calls[0]),
                            int(spent[0]))

    def providers(self) -> List[ProviderInfo]:
        """Discovery: every implementation, in index order."""
        return [self.provider_info(k) for k in range(len(self))]

    def bound(self, fn_id: int) -> int:
        """The implementation serving ``fn_id`` now (``ZXV_E_NOPROVIDER`` if none)."""
        out = ffi.new("uint32_t *")
        self._run(_native.lib().zxv_hw_bound, _u32(fn_id, "fn_id"), out)
        return int(out[0])

    # --------------------------------------------------- calls

    def _out_call(self, fn: Callable[..., int], fn_id: int, input: bytes, op: int, max_out: int,
                  extra: Sequence[Any]) -> bytes:
        f = _u32(fn_id, "fn_id")
        o = _u32(op, "op")
        i = _native.to_bytes(input, "input")
        cap_ = _native.exact_int(max_out, "max_out", 1, 1 << 24)
        buf = ffi.new("uint8_t[]", cap_)
        n = ffi.new("size_t *")
        try:
            self._run(fn, f, o, _buf(i), len(i), buf, cap_, n, *extra)
            return bytes(ffi.buffer(buf, n[0]))
        finally:
            _native.lib().zxv_secure_zero(buf, cap_)  # may hold key-bearing output

    def call(self, fn_id: int, input: bytes, *, op: int = 0, max_out: int = 4096) -> bytes:
        """Run function ``fn_id`` on its best proven, policy-allowed implementation."""
        return self._out_call(_native.lib().zxv_hw_call, fn_id, input, op, max_out, ())

    def shadow(self, fn_id: int, input: bytes, *, op: int = 0, max_out: int = 4096) -> Tuple[bytes, bool]:
        """:meth:`call` that also runs the next-best implementation and compares.
        Returns ``(output, checked)``. On a mismatch both stop serving: ``ZXV_E_DISPUTE``."""
        checked = ffi.new("int32_t *")
        out = self._out_call(_native.lib().zxv_hw_shadow, fn_id, input, op, max_out, (checked,))
        return out, bool(checked[0])

    def memo(self, fn_id: int, entries: int) -> Tuple[int, int]:
        """Declare the function pure with a memo of ``entries`` (power of two <= 65536; 0 removes;
        the current size only queries). Returns ``(hits, misses)`` so far."""
        hits, misses = ffi.new("uint64_t *"), ffi.new("uint64_t *")
        self._run(_native.lib().zxv_hw_memo, _u32(fn_id, "fn_id"), _u32(entries, "entries"), hits, misses)
        return int(hits[0]), int(misses[0])

    # --------------------------------------------------- contract packages and the log

    def trust_key(self, public_key: bytes) -> None:
        """Trust a release key for contract packages (at most 4). Needs ``Cap.HW_RECONF``."""
        pk = _fixed(public_key, HW_PK_BYTES, "public_key")
        self._run(_native.lib().zxv_hw_matrix_trust_key, ffi.from_buffer("uint8_t[]", pk))

    def apply_contract(self, package: bytes) -> int:
        """Apply a signed :attr:`HwImageKind.VECTORS` package; returns how many implementations
        prove the new contract. Needs ``Cap.HW_RECONF``."""
        p = _native.to_bytes(package, "package")
        out = ffi.new("uint32_t *")
        self._run(_native.lib().zxv_hw_contract_apply, _buf(p), len(p), out)
        return int(out[0])

    def log(self) -> Tuple[bytes, int]:
        """``(head, count)`` of the matrix audit log (contract changes)."""
        head = ffi.new("uint8_t[32]")
        n = ffi.new("uint32_t *")
        self._run(_native.lib().zxv_hw_matrix_log, head, n)
        return bytes(ffi.buffer(head, 32)), int(n[0])

    def log_entry(self, index: int) -> HwLogEntry:
        """Record ``index`` (0 = oldest) of the matrix audit log."""
        L = _native.lib()
        c, h = self._cap._handle(), self._handle()
        return _read_log(lambda *a: L.zxv_hw_matrix_log_entry(c, h, *a), self, index)

    def log_entries(self) -> Iterator[HwLogEntry]:
        for k in range(self.log()[1]):
            yield self.log_entry(k)

    def __repr__(self) -> str:
        return "<zxv.HwMatrix closed>" if self._closed else f"<zxv.HwMatrix {len(self)} implementations>"


# ---------------------------------------------------------------- packages


class HwReleaseKey:
    """A release key pair: :attr:`public_key` (2656 bytes) and two wiped-on-close secrets.

    Every package is signed with ML-DSA-87 **and** SLH-DSA-SHAKE-256s; both
    must verify. Use it as a context manager (or call :meth:`close`) so the
    secret keys are wiped with ``zxv_secure_zero``.
    """

    def __init__(self, public_key: bytes, mldsa: SecretKey, slhdsa: SecretKey) -> None:
        self.public_key = _fixed(public_key, HW_PK_BYTES, "public_key")
        if len(mldsa) != HW_MLDSA_SK_BYTES or len(slhdsa) != HW_SLHDSA_SK_BYTES:
            raise _native.make_error(-1, "secret key lengths are 4896 (ML-DSA) and 128 (SLH-DSA)")
        self.mldsa_secret = mldsa
        self.slhdsa_secret = slhdsa

    @classmethod
    def generate(cls, cap: Capability) -> "HwReleaseKey":
        """A fresh release key pair (needs ``Cap.CRYPTO_SIGN``)."""
        pk = ffi.new("uint8_t[]", HW_PK_BYTES)
        sk = SecretKey(HW_MLDSA_SK_BYTES, "ml-dsa-87 hw release")
        ssk = SecretKey(HW_SLHDSA_SK_BYTES, "slh-dsa-shake-256s hw release")
        try:
            _native.check(_native.lib().zxv_hw_keygen(cap._handle(), pk, sk._cdata(), ssk._cdata()),
                          cap.context)
        except BaseException:
            sk.close()
            ssk.close()
            raise
        return cls(bytes(ffi.buffer(pk, HW_PK_BYTES)), sk, ssk)

    def build(self, cap: Capability, kind: HwImageKind, *, fn_id: int, version: int, svn: int,
              payload: bytes, target_class: int = 0, target_digest: Optional[bytes] = None) -> bytes:
        """Build and sign a package: header || payload || both signatures (needs ``Cap.CRYPTO_SIGN``).

        For an image, ``fn_id`` is the function the region will serve; for
        :attr:`HwImageKind.VECTORS`, it is the function, ``version`` the
        contract version, and ``payload`` a vector list (:func:`encode_vectors`).
        ``svn`` is the anti-rollback floor. SLH-DSA signing takes seconds: it
        is a release step.
        """
        p = _native.to_bytes(payload, "payload")
        td: Any = ffi.NULL
        if target_digest is not None:
            td = ffi.from_buffer("uint8_t[]", _fixed(target_digest, 32, "target_digest"))
        args = (int(HwImageKind(kind)), _u32(target_class, "target_class"), td, _u32(fn_id, "fn_id"),
                _u32(version, "version"), _u32(svn, "svn"), _buf(p), len(p))
        size = HW_HEADER_BYTES + len(p) + HW_SIG_BYTES
        n = ffi.new("size_t *")
        L = _native.lib()
        for _ in range(2):
            buf = ffi.new("uint8_t[]", size)
            rc = L.zxv_hw_image_build(cap._handle(), *args, self.mldsa_secret._cdata(),
                                      self.slhdsa_secret._cdata(), buf, size, n)
            if rc == _E_BUFFER and n[0] > size:
                size = n[0]
                continue
            _native.check(rc, cap.context)
            return bytes(ffi.buffer(buf, n[0]))
        _native.check(_E_BUFFER, cap.context)
        raise AssertionError("unreachable")  # pragma: no cover

    def close(self) -> None:
        """Wipe both secret keys. Idempotent."""
        self.mldsa_secret.close()
        self.slhdsa_secret.close()

    def __enter__(self) -> "HwReleaseKey":
        return self

    def __exit__(self, *exc: Any) -> None:
        self.close()

    def __repr__(self) -> str:
        state = "wiped" if self.mldsa_secret.closed else "live"
        return f"<zxv.HwReleaseKey {self.public_key[:8].hex()}... {state}>"

    def __reduce__(self) -> Any:
        raise TypeError("HwReleaseKey cannot be pickled")


# ---------------------------------------------------------------- regions


class HwDevice(Protocol):
    """The device side of a region. Raise a :class:`ZxvError` (or return a negative
    status) to fail a step with that status."""

    def load(self, slot: int, payload: bytes) -> Optional[int]:
        """Load ``payload`` into ``slot`` (0 or 1)."""
        ...

    def readback(self, slot: int) -> bytes:
        """The SHA-256 of what ``slot`` really holds (32 bytes)."""
        ...

    def activate(self, slot: int) -> Optional[int]:
        """Switch the region to ``slot``."""
        ...


@dataclass(frozen=True)
class RegionInfo:
    state: HwRegionState
    version: int
    floor: int
    """The anti-rollback floor."""
    quarantined: int


@dataclass(frozen=True)
class RegionEvent:
    """One step of a region, delivered to :meth:`HwRegion.on_event` callbacks."""

    region_id: int
    event: HwEvent
    reason: int
    version: int


def _ret(r: Any) -> int:
    if r is None:
        return 0
    if isinstance(r, int) and not isinstance(r, bool) and r <= 0:
        return int(r)
    return _E_IO


def _load_tramp(user: Any, slot: int, payload: Any, n: int) -> int:
    try:
        rg: HwRegion = ffi.from_handle(user)
        return _ret(rg._device.load(int(slot), bytes(ffi.buffer(payload, n)) if n else b""))
    except BaseException as exc:
        return _status_of(exc)


_C_load_tramp: Any = ffi.callback("zxv_hw_load_fn", _load_tramp)


def _readback_tramp(user: Any, slot: int, digest: Any) -> int:
    try:
        rg: HwRegion = ffi.from_handle(user)
        r = rg._device.readback(int(slot))
        if isinstance(r, int) and not isinstance(r, bool):
            return r if r < 0 else _E_IO
        d = _fixed(r, 32, "readback digest")
        ffi.memmove(digest, d, 32)
        return 0
    except BaseException as exc:
        return _status_of(exc)


_C_readback_tramp: Any = ffi.callback("zxv_hw_readback_fn", _readback_tramp)


def _activate_tramp(user: Any, slot: int) -> int:
    try:
        rg: HwRegion = ffi.from_handle(user)
        return _ret(rg._device.activate(int(slot)))
    except BaseException as exc:
        return _status_of(exc)


_C_activate_tramp: Any = ffi.callback("zxv_hw_activate_fn", _activate_tramp)


def _event_tramp(user: Any, region_id: int, event: int, reason: int, version: int) -> None:
    try:
        rg: HwRegion = ffi.from_handle(user)
        rg._dispatch(RegionEvent(int(region_id), HwEvent(event), int(reason), int(version)))
    except BaseException as exc:
        _pending.errors.append(exc)


_C_event_tramp: Any = ffi.callback("zxv_hw_event_fn", _event_tramp)


class HwRegion(NativeHandle):
    """A reconfigurable region updated by signed images (``zxv_hw_region``). Needs ``Cap.HW_RECONF``.

    Args:
        cap: the capability every call uses.
        device: the :class:`HwDevice` that loads, reads back and activates slots.
        region_id: the region's id (in events and audit records).
        target_class: the hardware class images must name.
        matrix, provider: the region's FABRIC or MICRO implementation in a
            matrix, whose golden vectors test each new image (``None``: no test).
        target_digest: 32 bytes images must name (default zeros).
        log_capacity: audit log size; when it fills, the region halts and
            the context is poisoned (a change that cannot be recorded is not made).
    """

    def __init__(self, cap: Capability, device: HwDevice, *, region_id: int, target_class: int,
                 matrix: Optional[HwMatrix] = None, provider: int = 0,
                 target_digest: Optional[bytes] = None, log_capacity: int = 64) -> None:
        ctx = cap.context
        for m in ("load", "readback", "activate"):
            if not callable(getattr(device, m, None)):
                raise TypeError(f"device must have a callable {m}()")
        td: Any = ffi.NULL
        if target_digest is not None:
            td = ffi.from_buffer("uint8_t[]", _fixed(target_digest, 32, "target_digest"))
        self._device = device
        self._cap = cap
        self._listeners: List[Callable[[RegionEvent], None]] = []
        self._self_handle = ffi.new_handle(self)
        mx = matrix._handle() if matrix is not None else ffi.NULL
        out = ffi.new("zxv_hw_region **")
        L = _native.lib()
        _guarded(lambda: L.zxv_hw_region_create_cap(
            cap._handle(), mx, _u32(provider, "provider"), _u32(region_id, "region_id"),
            _u32(target_class, "target_class"), td, _C_load_tramp, _C_readback_tramp, _C_activate_tramp,
            self._self_handle, _native.exact_int(log_capacity, "log_capacity", 1, 1 << 20), out), ctx)
        super().__init__(out[0], matrix if matrix is not None else cap, ctx)
        self._run(L.zxv_hw_region_events, _C_event_tramp, self._self_handle)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_hw_region_destroy(handle)
        self._listeners = []

    def _run(self, fn: Callable[..., int], *args: Any) -> None:
        c, h = self._cap._handle(), self._handle()
        _guarded(lambda: fn(c, h, *args), self._context)

    def _dispatch(self, ev: RegionEvent) -> None:
        for cb in list(self._listeners):
            cb(ev)

    def on_event(self, callback: Callable[[RegionEvent], None]) -> None:
        """Call ``callback`` after each recorded step (on the thread of the region call)."""
        if not callable(callback):
            raise TypeError("callback must be callable")
        self._listeners.append(callback)

    def trust_key(self, public_key: bytes) -> None:
        """Trust a release key (at most 4 per region)."""
        pk = _fixed(public_key, HW_PK_BYTES, "public_key")
        self._run(_native.lib().zxv_hw_region_trust_key, ffi.from_buffer("uint8_t[]", pk))

    def stage(self, image: bytes) -> None:
        """Verify and load into the inactive slot. Rejections are recorded."""
        b = _native.to_bytes(image, "image")
        self._run(_native.lib().zxv_hw_region_stage, _buf(b), len(b))

    def trial(self, ticks: int) -> None:
        """Switch to the staged image and prove it; passing starts a probation of ``ticks``.
        A failure rolls back and raises ``ZXV_E_QUARANTINED``."""
        self._run(_native.lib().zxv_hw_region_trial, _u32(ticks, "ticks"))

    def commit(self) -> None:
        """Make the tested image permanent and raise the floor."""
        self._run(_native.lib().zxv_hw_region_commit)

    def rollback(self) -> None:
        self._run(_native.lib().zxv_hw_region_rollback)

    def tick(self) -> None:
        """One health tick; an expiring probation rolls back (``ZXV_E_QUARANTINED``)."""
        self._run(_native.lib().zxv_hw_region_tick)

    @property
    def info(self) -> RegionInfo:
        st = ffi.new("int32_t *")
        v, f, q = (ffi.new("uint32_t *") for _ in range(3))
        self._run(_native.lib().zxv_hw_region_info, st, v, f, q)
        return RegionInfo(HwRegionState(st[0]), int(v[0]), int(f[0]), int(q[0]))

    def log(self) -> Tuple[bytes, int]:
        """``(head, count)`` of the region's audit log (verified; a broken chain is fatal)."""
        head = ffi.new("uint8_t[32]")
        n = ffi.new("uint32_t *")
        self._run(_native.lib().zxv_hw_region_log, head, n)
        return bytes(ffi.buffer(head, 32)), int(n[0])

    def log_entry(self, index: int) -> HwLogEntry:
        """Record ``index`` (0 = oldest)."""
        L = _native.lib()
        c, h = self._cap._handle(), self._handle()
        return _read_log(lambda *a: L.zxv_hw_region_log_entry(c, h, *a), self, index)

    def log_entries(self) -> Iterator[HwLogEntry]:
        for k in range(self.log()[1]):
            yield self.log_entry(k)

    def __repr__(self) -> str:
        if self._closed:
            return "<zxv.HwRegion closed>"
        i = self.info
        return f"<zxv.HwRegion {i.state.name} v{i.version} floor={i.floor}>"
