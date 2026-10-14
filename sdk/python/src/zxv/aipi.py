# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""AIPI: content-addressed functions, calls and claims, and an offline node (ABI 1.5).

AIPI names data and functions by what they are (a :class:`~zxv.Cid`):

* **data** - CIDv1 raw (or dag-pb): blobs, modules, contracts;
* **function** - an :class:`AipiFunction` record, named by a CID whose codec
  is ``zxv-fn`` + intent; one function has exactly one CID;
* **call** - an :class:`AipiCall` record (function CID + input);
* **claim** - an :class:`AipiClaim`: an executor's answer to a call, the bytes
  it signs (signing and checking are the caller's job).

Records are decoded strictly (exact lengths, no trailing bytes, canonical
encoding). The record classes are pure values: no context, no capability.

:class:`AipiNode` is an OFFLINE, in-process IPFS node (block store + pins).
Storing needs ``Cap.AIPI``; reading takes only the handle. It is internally
serialized, so any thread may use it.

Honest limits:

* No network transport: no Vinea mesh, IPFS peers, HTTP gateway or libp2p.
  :meth:`AipiNode.get` finds only what was put into that node.
* No EXECUTE: running a function needs the Z-Wasm sandbox (``sdk/kits/aipi``),
  which is not in libzxv. :meth:`AipiNode.plan` reports the route a call
  would take here; it runs nothing.
* No money moves in this group.
* A CID proves which bytes are meant, not that a claimed output is right.
"""

from __future__ import annotations

import enum
from dataclasses import dataclass
from typing import Any, Callable, Optional, Union

from . import _native
from ._handle import NativeHandle
from ._native import ffi
from .cid import CID_MAX, Cid, CidLike, as_cid
from .context import Capability
from .crypto import SecretKey

__all__ = [
    "AIPI_NAME_MAX", "AIPI_IO_MAX", "AIPI_CERT_MAX", "AIPI_QUORUM_MAX", "AIPI_FN_MAX",
    "AIPI_CALL_MAX", "AIPI_CLAIM_MAX", "AIPI_BLOCK_MAX", "AIPI_STORE_MIN", "AIPI_STORE_MAX",
    "AIPI_BLOCKS_MAX", "AipiIntent", "AipiVerify", "AipiPrivacy", "AipiDeterminism", "AipiRoute",
    "AipiFunction", "AipiCall", "AipiClaim", "AipiNode", "AipiNodeInfo",
]

AIPI_NAME_MAX = 63
"""Export names: ``[A-Za-z0-9_]{1,63}``."""
AIPI_IO_MAX = 1024
"""Largest call input and claim output, in bytes."""
AIPI_CERT_MAX = 1024
"""Largest claim certificate, in bytes."""
AIPI_QUORUM_MAX = 8
"""Largest QUORUM n."""
AIPI_FN_MAX = 373
AIPI_CALL_MAX = 1112
AIPI_CLAIM_MAX = 2170
AIPI_BLOCK_MAX = 2_097_152
"""Largest blob a node stores (2 MiB)."""
AIPI_STORE_MIN = 4096
AIPI_STORE_MAX = 268_435_456
"""Largest node store (256 MiB)."""
AIPI_BLOCKS_MAX = 65536


class AipiIntent(enum.IntEnum):
    """What a function is for; it picks the function CID's codec."""

    FN = 0
    TENSOR_INFER = 1
    SETTLE_LEDGER = 2
    SWARM_ROUTE = 3


class AipiVerify(enum.IntEnum):
    """How a remote result is accepted."""

    CHECKER = 1
    """A checker export judges (input, output)."""
    CERT = 2
    """As CHECKER, with a certificate."""
    QUORUM = 3
    """k of n executors agree byte for byte (DETERMINISTIC only)."""
    LOCAL = 4
    """Never accept a remote result."""


class AipiPrivacy(enum.IntEnum):
    PUBLIC = 0
    LOCAL = 1
    """Private or key-bearing: never leaves the node (stored sealed)."""


class AipiDeterminism(enum.IntEnum):
    NONDETERMINISTIC = 0
    DETERMINISTIC = 1


class AipiRoute(enum.IntEnum):
    """The route a call would take (:meth:`AipiNode.plan`). This offline library
    reports only LOCAL or UNAVAILABLE; the others are fixed for networked hosts."""

    CACHE = 1
    LOCAL = 2
    FETCH_LOCAL = 3
    MESH = 4
    UNAVAILABLE = 5


_GET_FIRST = 65536


def _cid_buf(c: Optional[Cid]) -> Any:
    return (ffi.NULL, 0) if c is None else (_native.cbuf(c.binary), len(c.binary))


def _opt_name(s: Optional[str], what: str) -> Any:
    return ffi.NULL if s is None else _native.to_cstr(s, what)


def _cid_call(fn: Callable[..., int], *args: Any, owner: Any = None) -> Cid:
    out, n = ffi.new("uint8_t[]", CID_MAX), ffi.new("size_t *")
    _native.check(fn(*args, out, n), owner)
    return Cid(bytes(ffi.buffer(out, n[0])))


@dataclass(frozen=True)
class AipiFunction:
    """A canonical AIPI function record and its decoded fields.

    Build one with :meth:`encode` or check received bytes with :meth:`decode`.
    """

    record: bytes
    """The canonical record bytes (its :attr:`cid` is their hash)."""
    intent: AipiIntent
    verify: AipiVerify
    privacy: AipiPrivacy
    determinism: AipiDeterminism
    quorum_n: int
    quorum_k: int
    fuel_max: int
    """The most fuel one run may use (> 0)."""
    module: Cid
    """CIDv1 raw sha2-256 of the Z-Wasm module bytes."""
    contract: Optional[Cid]
    """CIDv1 raw sha2-256 of the z-contract text, if any."""
    entry: str
    """The export computing the result."""
    checker: str
    """CHECKER/CERT only, else ``""``."""
    certifier: str
    """CERT only, else ``""``."""

    @classmethod
    def encode(cls, *, intent: int, verify: int, module: CidLike, entry: str, fuel_max: int,
               privacy: int = AipiPrivacy.PUBLIC, determinism: int = AipiDeterminism.DETERMINISTIC,
               quorum_n: int = 0, quorum_k: int = 0, contract: Optional[CidLike] = None,
               checker: Optional[str] = None, certifier: Optional[str] = None) -> "AipiFunction":
        """Encode a function record. Every field is checked; none is masked.

        Rules: QUORUM needs 2 <= n <= 8, n/2 < k <= n and DETERMINISTIC
        (n and k are 0 otherwise); ``checker`` only for CHECKER/CERT and
        different from ``entry``; ``certifier`` only for CERT; names are
        ``[A-Za-z0-9_]{1,63}``.

        Raises:
            ZxvUsageError: ``ZXV_E_ARG`` (a field out of range or breaking a
                rule), ``ZXV_E_TOO_LONG`` (a name over 63).
            CidMismatchError: a module or contract CID that is not CIDv1 raw sha2-256.
        """
        u32 = _native.U32
        args = [_native.exact_int(v, n, *u32) for v, n in (
            (intent, "intent"), (verify, "verify"), (privacy, "privacy"),
            (determinism, "determinism"), (quorum_n, "quorum_n"), (quorum_k, "quorum_k"))]
        fuel = _native.exact_int(fuel_max, "fuel_max", *_native.U64)
        mod = as_cid(module, "module")
        con = None if contract is None else as_cid(contract, "contract")
        mb, ml = _cid_buf(mod)
        cb, cl = _cid_buf(con)
        e = _native.to_cstr(entry, "entry")
        ck, ce = _opt_name(checker, "checker"), _opt_name(certifier, "certifier")
        L = _native.lib()
        rec = _native.read_bytes(lambda b, c, n: L.zxv_aipi_fn_encode(*args, fuel, mb, ml, cb, cl, e, ck, ce,
                                                                       b, c, n), None, AIPI_FN_MAX)
        return cls.decode(rec)

    @classmethod
    def decode(cls, record: bytes) -> "AipiFunction":
        """Check a function record strictly and read every field.

        Raises:
            ZxvUsageError: ``ZXV_E_FORMAT`` - not a valid, canonical function record.
        """
        rec = _native.to_bytes(record, "record")
        rb = _native.cbuf(rec)
        o = [ffi.new("uint32_t *") for _ in range(6)]
        fuel = ffi.new("uint64_t *")
        L = _native.lib()
        _native.check(L.zxv_aipi_fn_decode(rb, len(rec), *o, fuel))
        module = _cid_call(L.zxv_aipi_fn_get_cid, rb, len(rec), 0)
        out, n = ffi.new("uint8_t[]", CID_MAX), ffi.new("size_t *")
        rc = L.zxv_aipi_fn_get_cid(rb, len(rec), 1, out, n)
        contract = None if rc == -4 else Cid(bytes(ffi.buffer(out, n[0])))
        if rc not in (0, -4):
            _native.check(rc)

        def name(field: int) -> str:
            return _native.read_text(lambda b, c, k: L.zxv_aipi_fn_get_name(rb, len(rec), field, b, c, k))

        return cls(rec, AipiIntent(o[0][0]), AipiVerify(o[1][0]), AipiPrivacy(o[2][0]),
                   AipiDeterminism(o[3][0]), int(o[4][0]), int(o[5][0]), int(fuel[0]), module,
                   contract, name(0), name(1), name(2))

    @property
    def cid(self) -> Cid:
        """The function CID: codec ``zxv-fn`` + intent, sha2-256 of :attr:`record`."""
        return _cid_call(_native.lib().zxv_aipi_fn_cid, _native.cbuf(self.record), len(self.record))


@dataclass(frozen=True)
class AipiCall:
    """A call record naming one invocation: a function CID and its input."""

    record: bytes
    function: Cid
    input: bytes

    @classmethod
    def encode(cls, function: CidLike, input: bytes = b"") -> "AipiCall":
        """Encode a call of ``function`` on ``input`` (at most :data:`AIPI_IO_MAX` bytes).

        Raises:
            CidMismatchError: ``function`` is not a CIDv1 sha2-256 with a function codec.
            ZxvUsageError: ``ZXV_E_TOO_LONG`` (input too long).
        """
        fc = as_cid(function, "function")
        data = _native.to_bytes(input, "input")
        fb, fl = _cid_buf(fc)
        db = _native.cbuf(data)
        L = _native.lib()
        rec = _native.read_bytes(lambda b, c, n: L.zxv_aipi_call_encode(fb, fl, db, len(data), b, c, n),
                                 None, AIPI_CALL_MAX)
        return cls(rec, fc, data)

    @classmethod
    def decode(cls, record: bytes) -> "AipiCall":
        """Check a call record strictly and split it.

        Raises:
            ZxvUsageError: ``ZXV_E_FORMAT``.
        """
        rec = _native.to_bytes(record, "record")
        o = [ffi.new("size_t *") for _ in range(4)]
        _native.check(_native.lib().zxv_aipi_call_decode(_native.cbuf(rec), len(rec), *o))
        fo, fl, io, il = (int(x[0]) for x in o)
        return cls(rec, Cid(rec[fo:fo + fl]), rec[io:io + il])

    @staticmethod
    def cid_of(function: CidLike, input: bytes = b"") -> Cid:
        """The call CID of (``function``, ``input``) without building the record object."""
        fc = as_cid(function, "function")
        data = _native.to_bytes(input, "input")
        fb, fl = _cid_buf(fc)
        return _cid_call(_native.lib().zxv_aipi_call_cid, fb, fl, _native.cbuf(data), len(data))

    @property
    def cid(self) -> Cid:
        """The call CID (codec ``zxv-aipi-call``, sha2-256 of :attr:`record`)."""
        return self.cid_of(self.function, self.input)


@dataclass(frozen=True)
class AipiClaim:
    """An executor's claimed answer to a call: the bytes it signs."""

    record: bytes
    call: Cid
    executor: bytes
    """The executor's 32-byte node id."""
    output: bytes
    cert: bytes
    """The certificate (CERT functions), else empty."""

    @classmethod
    def encode(cls, call: CidLike, executor: bytes, output: bytes, cert: bytes = b"") -> "AipiClaim":
        """Encode a claim. ``output`` and ``cert`` are each at most 1024 bytes.

        Raises:
            CidMismatchError: ``call`` is not a call CID.
            ZxvUsageError: ``ZXV_E_TOO_LONG`` or a bad executor id.
        """
        cc = as_cid(call, "call")
        ex = _native.fixed_bytes(executor, 32, "executor")
        out = _native.to_bytes(output, "output")
        ce = _native.to_bytes(cert, "cert")
        cb, cl = _cid_buf(cc)
        eb, ob, kb = _native.cbuf(ex), _native.cbuf(out), _native.cbuf(ce)
        L = _native.lib()
        rec = _native.read_bytes(
            lambda b, c, n: L.zxv_aipi_claim_encode(cb, cl, eb, ob, len(out), kb, len(ce), b, c, n),
            None, AIPI_CLAIM_MAX)
        return cls(rec, cc, ex, out, ce)

    @classmethod
    def decode(cls, record: bytes) -> "AipiClaim":
        """Check a claim record strictly and split it.

        Raises:
            ZxvUsageError: ``ZXV_E_FORMAT``.
        """
        rec = _native.to_bytes(record, "record")
        o = [ffi.new("size_t *") for _ in range(7)]
        _native.check(_native.lib().zxv_aipi_claim_decode(_native.cbuf(rec), len(rec), *o))
        co, cl, eo, oo, ol, ko, kl = (int(x[0]) for x in o)
        return cls(rec, Cid(rec[co:co + cl]), rec[eo:eo + 32], rec[oo:oo + ol], rec[ko:ko + kl])


@dataclass(frozen=True)
class AipiNodeInfo:
    """Audit counters of an :class:`AipiNode`."""

    blocks: int
    """Blocks indexed."""
    store_used: int
    """Store bytes used."""
    gets_verified: int
    """GETs whose bytes re-hashed to their CID."""
    forbidden: int
    """GETs whose stored bytes failed their hash (dropped)."""


class AipiNode(NativeHandle):
    """An offline, in-memory IPFS node with the AIPI context over it (``zxv_aipi``).

    Belongs to the capability's context and fails closed with it. Storing
    (:meth:`put`, :meth:`publish`) needs ``Cap.AIPI``; reading does not. Each
    storing method uses the capability the node was made with unless you pass
    another one (``cap=``) from the same context.

    Args:
        cap: a capability holding ``Cap.AIPI``.
        store_bytes: memory for blocks, :data:`AIPI_STORE_MIN`..:data:`AIPI_STORE_MAX`
            (each block costs its length plus about 100 bytes).
        max_blocks: at most this many blocks, 1..:data:`AIPI_BLOCKS_MAX`.
        node_key: 32 bytes (copied; wiped when the node closes) that seal
            private blocks with ChaCha20-Poly1305. Without it, private puts
            are refused with ``ZXV_E_STATE``.

    Raises:
        CapabilityDeniedError: the capability lacks ``Cap.AIPI``.
    """

    def __init__(self, cap: Capability, store_bytes: int = 1 << 20, max_blocks: int = 1024, *,
                 node_key: Optional[Union[SecretKey, bytes]] = None) -> None:
        ctx = cap.context
        sb = _native.exact_int(store_bytes, "store_bytes", *_native.U64)
        mb = _native.exact_int(max_blocks, "max_blocks", *_native.U32)
        key: Any = ffi.NULL
        if isinstance(node_key, SecretKey):
            if len(node_key) != 32:
                raise _native.make_error(-1, "node_key must be 32 bytes")
            key = node_key._cdata()
        elif node_key is not None:
            key = _native.cbuf(_native.fixed_bytes(node_key, 32, "node_key"))
        out = ffi.new("zxv_aipi **")
        _native.check(_native.lib().zxv_aipi_create(cap._handle(), sb, mb, key, out), ctx)
        self._cap = cap
        super().__init__(out[0], cap, ctx)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_aipi_destroy(handle)

    def _c(self, cap: Optional[Capability]) -> Any:
        return (cap or self._cap)._handle()

    def put(self, data: bytes, *, private: bool = False, cap: Optional[Capability] = None) -> Cid:
        """Store and pin ``data`` (at most :data:`AIPI_BLOCK_MAX` bytes) as one raw block.

        Putting the same bytes again is a no-op. Private blocks are sealed and
        never announceable; the same bytes public after private (or the
        reverse) is refused, because making something public is a deliberate act.

        Returns:
            The block's CID.

        Raises:
            ZxvRefusedError: ``ZXV_E_STATE`` (private without a node key, or
                the visibility conflict), ``ZXV_E_CAPACITY`` (store full).
            CapabilityDeniedError: no ``Cap.AIPI``.
        """
        d = _native.to_bytes(data, "data")
        c = self._c(cap)
        return _cid_call(_native.lib().zxv_aipi_put, c, self._handle(), _native.cbuf(d), len(d),
                         1 if private else 0, owner=self._context)

    def get(self, cid: CidLike) -> bytes:
        """The bytes ``cid`` names, re-hashed before they are returned (AIPI_GET).

        Raises:
            ZxvUsageError: ``ZXV_E_NOT_FOUND`` - not held here (there is no network).
            CidMismatchError: a bad or identity CID, or stored bytes that no
                longer match (they are dropped and counted as forbidden).
            ZxvRefusedError: ``ZXV_E_STATE`` (a private block and no node key).

        Blocks up to 64 KiB are read in one C call; a larger one takes a second
        call after the first reports its size, and each call counts in
        :attr:`AipiNodeInfo.gets_verified`.
        """
        c = as_cid(cid)
        h = self._handle()
        cb = _native.cbuf(c.binary)
        return _native.read_bytes(lambda b, k, n: _native.lib().zxv_aipi_get(h, cb, len(c.binary), b, k, n),
                                  self._context, _GET_FIRST)

    def __contains__(self, cid: object) -> bool:
        """True if this node holds the bytes ``cid`` names."""
        if not isinstance(cid, (Cid, bytes, str)):
            return False
        c = as_cid(cid)
        n = ffi.new("size_t *")
        rc = _native.lib().zxv_aipi_get(self._handle(), _native.cbuf(c.binary), len(c.binary), ffi.NULL, 0, n)
        if rc in (0, -2):
            return True
        if rc == -4:
            return False
        _native.check(rc, self._context)
        return True

    def publish(self, function: Union[AipiFunction, bytes], *, cap: Optional[Capability] = None) -> Cid:
        """Store a function record (sealed when its privacy is LOCAL) and return its
        function CID. The module and contract need not be held yet.

        Raises:
            ZxvUsageError: ``ZXV_E_FORMAT`` (a bad record).
            ZxvRefusedError: ``ZXV_E_STATE`` / ``ZXV_E_CAPACITY``.
            CapabilityDeniedError: no ``Cap.AIPI``.
        """
        rec = function.record if isinstance(function, AipiFunction) else _native.to_bytes(function, "function")
        return _cid_call(_native.lib().zxv_aipi_publish_fn, self._c(cap), self._handle(), _native.cbuf(rec),
                         len(rec), owner=self._context)

    def load(self, function_cid: CidLike) -> AipiFunction:
        """GET a function record by its CID, verified by hash and decoded strictly.

        Raises:
            CidMismatchError: not a function codec, or codec and intent disagree.
            ZxvUsageError: ``ZXV_E_NOT_FOUND`` or ``ZXV_E_FORMAT``.
        """
        c = as_cid(function_cid, "function_cid")
        h = self._handle()
        cb = _native.cbuf(c.binary)
        rec = _native.read_bytes(
            lambda b, k, n: _native.lib().zxv_aipi_load_fn(h, cb, len(c.binary), b, k, n),
            self._context, AIPI_FN_MAX)
        return AipiFunction.decode(rec)

    def plan(self, function_cid: CidLike, input: bytes = b"", *, input_private: bool = False) -> AipiRoute:
        """The route a call would take now (runs and changes nothing): LOCAL when
        the module (and contract) are held here, else UNAVAILABLE.

        Raises:
            as :meth:`load`, plus ``ZXV_E_TOO_LONG`` for input over :data:`AIPI_IO_MAX`.
        """
        c = as_cid(function_cid, "function_cid")
        data = _native.to_bytes(input, "input")
        out = ffi.new("int32_t *")
        _native.check(_native.lib().zxv_aipi_plan(self._handle(), _native.cbuf(c.binary), len(c.binary),
                                                  _native.cbuf(data), len(data), 1 if input_private else 0,
                                                  out), self._context)
        return AipiRoute(out[0])

    @property
    def info(self) -> AipiNodeInfo:
        """The audit counters."""
        b, u, g, f = (ffi.new("uint32_t *"), ffi.new("uint64_t *"), ffi.new("uint32_t *"),
                      ffi.new("uint32_t *"))
        self._check(_native.lib().zxv_aipi_info(self._handle(), b, u, g, f))
        return AipiNodeInfo(int(b[0]), int(u[0]), int(g[0]), int(f[0]))
