# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Freight: erasure-coded packets and op streams (ABI 1.5, kernel ``freight``).

A FREIGHT carries up to :data:`FREIGHT_MAX_PAYLOAD` bytes as 168 packets of
21 bytes (one UBH-168 frame each): byte 0 is the row index, bytes 1..20 row
data. Rows ``0..k-1`` hold the payload verbatim; rows ``k..167`` are parity
of a systematic Cauchy Reed-Solomon code over GF(2^8). ANY ``k`` distinct
rows rebuild the payload, so up to ``168 - k`` rows may be lost. Choosing
``k`` is the trade: ``k * 20`` payload bytes per freight against ``168 - k``
rows of loss tolerance.

The 64-byte :class:`FreightHeader` travels separately (in the outer
message). A payload larger than one freight is split into a SET of up to
:data:`FREIGHT_MAX_SET` freights sharing one ``freight_id``
(:func:`split`, :func:`iter_split`) and reassembled in any order by a
:class:`FreightJoin`.

The payload may be an OP STREAM (LITERAL, COPY, CIDREF and SEED ops) that the
receiver expands with :func:`ops_expand`, never past the limit it allows.
Build one with :func:`ops_encode` (greedy LZ77) or an :class:`OpsWriter`.

Honest limits: erasure coding only. A lost row is recovered; a CORRUPTED row
is detected (the payload SHA-256 fails, :class:`~zxv.VerificationFailedError`)
but not located or repaired: drop rows you distrust and decode again. The
header is neither protected by the code nor authenticated: sign it (or the
outer message) if the sender matters. A freight is larger than its payload
(3528 wire bytes for at most 3360). The op stream promises no compression:
random data encodes slightly LARGER, COPY helps only data that repeats,
CIDREF only names bytes the receiver must already hold or fetch, and SEED
only reproduces the built-in generators. No information is created.

Everything here is pure (no context, no capability). Long calls (encode,
decode, joins, expansion) release the GIL. A :class:`FreightJoin` and an
:class:`OpsWriter` are not thread-safe in C; the SDK serializes calls on
each with a lock.
"""

from __future__ import annotations

import enum
import threading
from dataclasses import dataclass
from typing import Any, Callable, Iterator, List, Optional, Sequence, Tuple, Union

from . import _native
from ._handle import SerializedHandle
from ._native import ffi
from .cid import Cid
from .errors import ZxvError

__all__ = [
    "FREIGHT_PACKET_BYTES", "FREIGHT_ROW_PAYLOAD", "FREIGHT_ROWS", "FREIGHT_BYTES",
    "FREIGHT_MAX_PAYLOAD", "FREIGHT_HEADER_BYTES", "FREIGHT_VERSION", "FREIGHT_MAX_SET",
    "FREIGHT_SET_MAX_BYTES", "FREIGHT_CID_MAX", "FREIGHT_GEN_PARAM_MAX", "FREIGHT_OPS_MAX",
    "FreightFlag", "FreightGen", "FreightHeader", "encode", "decode", "packets", "set_count", "split",
    "iter_split", "FreightJoin", "JoinStatus", "OpsInfo", "ops_peek", "ops_expand", "ops_encode",
    "ops_bound", "gen_run", "OpsWriter", "Resolver",
]

FREIGHT_PACKET_BYTES = 21
FREIGHT_ROW_PAYLOAD = 20
FREIGHT_ROWS = 168
FREIGHT_BYTES = 3528
"""One encoded freight: 168 packets of 21 bytes."""
FREIGHT_MAX_PAYLOAD = 3360
"""Payload of one freight at k = 168 (no loss tolerated)."""
FREIGHT_HEADER_BYTES = 64
FREIGHT_VERSION = 1
FREIGHT_MAX_SET = 16384
"""Freights in one set."""
FREIGHT_SET_MAX_BYTES = 55_050_240
"""Largest set payload."""
FREIGHT_CID_MAX = 128
"""Longest content id in a CIDREF op."""
FREIGHT_GEN_PARAM_MAX = 32
FREIGHT_OPS_MAX = 67_108_864
"""Largest op stream or expansion handled (64 MiB)."""


class FreightFlag(enum.IntFlag):
    """Header flags (``ZXV_FREIGHT_F_*``)."""

    NONE = 0
    OPSTREAM = 0x01
    """The (set's) payload is an op stream; ``expand_limit`` may be set."""


class FreightGen(enum.IntEnum):
    """Built-in SEED generators (``ZXV_FREIGHT_GEN_*``) and their params."""

    XORSHIFT = 1
    """8 bytes: a nonzero u64 LE seed; xorshift64* output bytes, little-endian."""
    REPEAT = 2
    """1..32 bytes: the pattern, repeated."""
    FIBWORD = 3
    """2 distinct bytes a, b: the Fibonacci word abaababaab..."""
    SIERPINSKI = 4
    """3 bytes: log2 width w (1..15), on, off: byte i is ``on`` iff (x & y) == 0."""


def _u64(v: Any, what: str) -> int:
    return _native.exact_int(v, what, *_native.U64)


def _u8(v: Any, what: str) -> int:
    return _native.exact_int(v, what, 0, 255)


def _u16(v: Any, what: str) -> int:
    return _native.exact_int(v, what, 0, 65535)


@dataclass(frozen=True)
class FreightHeader:
    """The 64-byte freight header, field by field.

    Serialized form (:meth:`to_bytes`): ``"ZXFH"`` version k flags 0 |
    payload_len seq seq_count 0 0 (u16 LE) | freight_id (u64 LE) |
    expand_limit (u64 LE) | SHA-256 of the payload.
    """

    k: int
    """Data rows, 1..168."""
    flags: int
    """0 or :attr:`FreightFlag.OPSTREAM`."""
    payload_len: int
    """At most ``k * 20``."""
    seq: int
    """Member index in its set."""
    seq_count: int
    """Freights in the set, 1..16384."""
    freight_id: int
    expand_limit: int
    """0 unless OPSTREAM."""
    payload_sha256: bytes
    version: int = FREIGHT_VERSION

    @classmethod
    def for_payload(cls, freight_id: int, k: int, payload: bytes, *, flags: int = 0,
                    expand_limit: int = 0) -> "FreightHeader":
        """The header of a single freight (seq 0 of 1) carrying ``payload`` with ``k``
        data rows; it hashes the payload.

        Raises:
            ZxvUsageError: ``ZXV_E_ARG`` (k out of range, unknown flags, or
                ``expand_limit`` without OPSTREAM), ``ZXV_E_TOO_LONG`` (payload over ``k * 20``).
        """
        p = _native.to_bytes(payload, "payload")
        out = ffi.new("uint8_t[64]")
        _native.check(_native.lib().zxv_freight_header_init(_u64(freight_id, "freight_id"), _u8(k, "k"),
                                                            _u8(flags, "flags"), _u64(expand_limit, "expand_limit"),
                                                            _native.cbuf(p), len(p), out))
        return cls.parse(bytes(ffi.buffer(out, 64)))

    @classmethod
    def parse(cls, header: bytes) -> "FreightHeader":
        """Parse and check a serialized header.

        Raises:
            ZxvUsageError: ``ZXV_E_FORMAT`` (bad magic, reserved bytes, or a field
                that fails the check) or ``ZXV_E_ARG`` (not 64 bytes).
        """
        h = _native.fixed_bytes(header, FREIGHT_HEADER_BYTES, "header")
        ver, k, fl = ffi.new("uint8_t *"), ffi.new("uint8_t *"), ffi.new("uint8_t *")
        pl, sq, sc = ffi.new("uint16_t *"), ffi.new("uint16_t *"), ffi.new("uint16_t *")
        fid, lim = ffi.new("uint64_t *"), ffi.new("uint64_t *")
        sha = ffi.new("uint8_t[32]")
        _native.check(_native.lib().zxv_freight_header_parse(_native.cbuf(h), ver, k, fl, pl, sq, sc, fid,
                                                             lim, sha))
        return cls(int(k[0]), int(fl[0]), int(pl[0]), int(sq[0]), int(sc[0]), int(fid[0]), int(lim[0]),
                   bytes(ffi.buffer(sha, 32)), int(ver[0]))

    @staticmethod
    def check(header: bytes) -> None:
        """Raise ``ZxvUsageError`` (``ZXV_E_FORMAT``) unless ``header`` is well formed."""
        h = _native.fixed_bytes(header, FREIGHT_HEADER_BYTES, "header")
        _native.check(_native.lib().zxv_freight_header_check(_native.cbuf(h)))

    def to_bytes(self) -> bytes:
        """The 64 serialized bytes (the version is always :data:`FREIGHT_VERSION`).

        Raises:
            ZxvUsageError: ``ZXV_E_ARG`` for any field that breaks the header rules.
        """
        sha = _native.fixed_bytes(self.payload_sha256, 32, "payload_sha256")
        out = ffi.new("uint8_t[64]")
        _native.check(_native.lib().zxv_freight_header_serialize(
            _u8(self.k, "k"), _u8(self.flags, "flags"), _u16(self.payload_len, "payload_len"),
            _u16(self.seq, "seq"), _u16(self.seq_count, "seq_count"), _u64(self.freight_id, "freight_id"),
            _u64(self.expand_limit, "expand_limit"), _native.cbuf(sha), out))
        return bytes(ffi.buffer(out, 64))

    def __bytes__(self) -> bytes:
        return self.to_bytes()


HeaderLike = Union[FreightHeader, bytes]


def _hdr(h: HeaderLike) -> bytes:
    if isinstance(h, FreightHeader):
        return h.to_bytes()
    return _native.fixed_bytes(h, FREIGHT_HEADER_BYTES, "header")


def _pkts(p: Union[bytes, Sequence[bytes]]) -> bytes:
    if isinstance(p, (bytes, bytearray, memoryview)):
        return bytes(p)
    return b"".join(_native.fixed_bytes(x, FREIGHT_PACKET_BYTES, "packet") for x in p)


def encode(header: HeaderLike, payload: bytes) -> bytes:
    """The 168 packets (3528 bytes, rows in order) of the freight ``header`` describes.

    Raises:
        ZxvUsageError: ``ZXV_E_ARG`` (payload length differs from the header),
            ``ZXV_E_FORMAT`` (bad header).
        VerificationFailedError: the payload's hash differs from the header's.
    """
    h = _hdr(header)
    p = _native.to_bytes(payload, "payload")
    out = ffi.new("uint8_t[]", FREIGHT_BYTES)
    _native.check(_native.lib().zxv_freight_encode(_native.cbuf(h), _native.cbuf(p), len(p), out))
    return bytes(ffi.buffer(out, FREIGHT_BYTES))


def packets(encoded: bytes) -> List[bytes]:
    """Cut encoded bytes into their 21-byte packets (row ``i`` is ``packets(x)[i]``)."""
    e = _native.to_bytes(encoded, "encoded")
    if len(e) % FREIGHT_PACKET_BYTES:
        raise _native.make_error(-1, "length is not a multiple of 21")
    return [e[i:i + FREIGHT_PACKET_BYTES] for i in range(0, len(e), FREIGHT_PACKET_BYTES)]


def decode(header: HeaderLike, received: Union[bytes, Sequence[bytes]]) -> bytes:
    """Rebuild the payload from whatever packets arrived, in any order.

    ``received`` is concatenated 21-byte packets, or a sequence of them.
    Duplicates and row indices above 167 are ignored; the first ``k``
    distinct rows are used.

    Raises:
        ErasureError: fewer than ``k`` distinct rows.
        VerificationFailedError: the rebuilt payload fails the header's SHA-256
            (a corrupted row, or a header from another freight).
        ZxvUsageError: ``ZXV_E_ARG`` (not a multiple of 21), ``ZXV_E_FORMAT``.
    """
    h = _hdr(header)
    p = _pkts(received)
    hb, pb = _native.cbuf(h), _native.cbuf(p)
    return _native.read_bytes(lambda b, c, n: _native.lib().zxv_freight_decode(hb, pb, len(p), b, c, n),
                              None, FREIGHT_MAX_PAYLOAD)


def set_count(length: int, k: int) -> int:
    """Freights needed to carry ``length`` bytes at ``k`` (0 bytes still needs one).

    Raises:
        ZxvUsageError: ``ZXV_E_TOO_LONG`` (more than :data:`FREIGHT_MAX_SET`).
    """
    out = ffi.new("uint32_t *")
    _native.check(_native.lib().zxv_freight_set_count(_u64(length, "length"), _u8(k, "k"), out))
    return int(out[0])


def split(freight_id: int, k: int, data: bytes, seq: int, *, flags: int = 0,
          expand_limit: int = 0) -> Tuple[FreightHeader, bytes]:
    """Member ``seq`` of the set carrying ``data``: its header and its 168 packets.

    Raises:
        ZxvUsageError: ``ZXV_E_RANGE`` (seq not below the set count),
            ``ZXV_E_TOO_LONG`` (the set would be too large).
    """
    d = _native.to_bytes(data, "data")
    h = ffi.new("uint8_t[64]")
    out = ffi.new("uint8_t[]", FREIGHT_BYTES)
    _native.check(_native.lib().zxv_freight_split(_u64(freight_id, "freight_id"), _u8(k, "k"), _u8(flags, "flags"),
                                                  _u64(expand_limit, "expand_limit"), _native.cbuf(d), len(d),
                                                  _native.exact_int(seq, "seq", *_native.U32), h, out))
    return FreightHeader.parse(bytes(ffi.buffer(h, 64))), bytes(ffi.buffer(out, FREIGHT_BYTES))


def iter_split(freight_id: int, k: int, data: bytes, *, flags: int = 0,
               expand_limit: int = 0) -> Iterator[Tuple[FreightHeader, bytes]]:
    """Every member of the set carrying ``data``, in order."""
    for seq in range(set_count(len(data), k)):
        yield split(freight_id, k, data, seq, flags=flags, expand_limit=expand_limit)


@dataclass(frozen=True)
class JoinStatus:
    """Progress of a :class:`FreightJoin`."""

    received: int
    count: int
    """Members in the set (0 until the first is accepted)."""
    complete: bool
    total: int
    """The joined length once complete, else 0."""


class FreightJoin(SerializedHandle):
    """Reassembles one freight set, member by member, in any order (``zxv_freight_join``).

    The first member accepted fixes the set (freight_id, k, flags, count,
    expand_limit). A member already received is accepted again silently. A
    failed member is not recorded: add it again when more rows arrive.

    Args:
        capacity: the most bytes the set may hold, 0..:data:`FREIGHT_SET_MAX_BYTES`
            (allocated up front).
    """

    def __init__(self, capacity: int) -> None:
        out = ffi.new("zxv_freight_join **")
        _native.check(_native.lib().zxv_freight_join_create(_u64(capacity, "capacity"), out))
        super().__init__(out[0], None, None)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_freight_join_destroy(handle)

    def add(self, header: HeaderLike, received: Union[bytes, Sequence[bytes]]) -> None:
        """Decode one member into its place.

        Raises:
            ZxvRefusedError: ``ZXV_E_STATE`` (a member of another set, or a
                non-last member that is not full), ``ZXV_E_CAPACITY`` (the set
                cannot fit this join).
            ErasureError, VerificationFailedError: as :func:`decode`.
        """
        h = _hdr(header)
        p = _pkts(received)
        with self._lock:
            _native.check(_native.lib().zxv_freight_join_add(self._handle(), _native.cbuf(h), _native.cbuf(p),
                                                             len(p)))

    @property
    def status(self) -> JoinStatus:
        r, c, done, t = ffi.new("uint32_t *"), ffi.new("uint32_t *"), ffi.new("int32_t *"), ffi.new("uint64_t *")
        with self._lock:
            _native.check(_native.lib().zxv_freight_join_status(self._handle(), r, c, done, t))
        return JoinStatus(int(r[0]), int(c[0]), bool(done[0]), int(t[0]))

    @property
    def complete(self) -> bool:
        """True once every member arrived."""
        return self.status.complete

    def result(self) -> bytes:
        """The joined bytes.

        Raises:
            ZxvRefusedError: ``ZXV_E_STATE`` until every member arrived.
        """
        with self._lock:
            h = self._handle()
            return _native.read_bytes(lambda b, c, n: _native.lib().zxv_freight_join_result(h, b, c, n))


# ---------------------------------------------------------------- op streams

Resolver = Callable[[bytes, int], Optional[bytes]]
"""A CIDREF resolver: ``resolver(content_id, length)`` returns exactly ``length``
bytes, or ``None`` if it cannot (the expansion then fails with ``ZXV_E_NOT_FOUND``).
``content_id`` is the raw id bytes from the stream (often a binary :class:`~zxv.Cid`)."""


@dataclass(frozen=True)
class OpsInfo:
    """What an op stream's header declares."""

    total: int
    has_sha256: bool


def ops_peek(ops: bytes) -> OpsInfo:
    """Read a stream's declared total and whether it carries an output SHA-256,
    without expanding it.

    Raises:
        ZxvUsageError: ``ZXV_E_FORMAT`` (bad header).
    """
    o = _native.to_bytes(ops, "ops")
    t, s = ffi.new("uint64_t *"), ffi.new("int32_t *")
    _native.check(_native.lib().zxv_freight_ops_peek(_native.cbuf(o), len(o), t, s))
    return OpsInfo(int(t[0]), bool(s[0]))


class _ResolveCall:
    """One expansion's resolver and the exception it raised, if any."""

    def __init__(self, fn: Resolver) -> None:
        self.fn = fn
        self.error: Optional[BaseException] = None


def _resolve_tramp(user: Any, cid: Any, cid_len: int, out: Any, length: int) -> int:
    call: _ResolveCall = ffi.from_handle(user)
    try:
        r = call.fn(bytes(ffi.buffer(cid, cid_len)), int(length))
        if r is None:
            return -4
        b = _native.to_bytes(r, "resolver result")
        if len(b) != length:
            raise _native.make_error(-1, f"resolver returned {len(b)} bytes, expected {length}")
        if b:
            ffi.memmove(out, b, len(b))
        return 0
    except BaseException as exc:  # never unwind through C
        if call.error is None:
            call.error = exc
        return -4


_C_resolve_tramp: Any = ffi.callback("zxv_freight_resolve_fn", _resolve_tramp)


def ops_expand(ops: bytes, limit: int, resolver: Optional[Resolver] = None) -> bytes:
    """Expand an op stream into the bytes it describes.

    The declared total is checked against ``limit`` before a byte is written.
    ``resolver`` supplies CIDREF bytes; it is called on this thread while the
    C call runs and must not expand another stream on the same thread. An
    exception it raises fails the expansion and is re-raised here.

    Raises:
        ZxvUsageError: ``ZXV_E_TOO_LONG`` (total above ``limit``),
            ``ZXV_E_RANGE`` (``limit`` above :data:`FREIGHT_OPS_MAX`),
            ``ZXV_E_FORMAT`` (a malformed stream), ``ZXV_E_NOT_FOUND`` (a CIDREF
            was not resolved).
        VerificationFailedError: the output fails the stream's SHA-256.
    """
    o = _native.to_bytes(ops, "ops")
    lim = _u64(limit, "limit")
    total = ops_peek(o).total
    cap = max(min(total, lim, FREIGHT_OPS_MAX), 1)
    buf = ffi.new("uint8_t[]", cap)
    n = ffi.new("size_t *")
    call = _ResolveCall(resolver) if resolver is not None else None
    handle = ffi.new_handle(call) if call is not None else ffi.NULL
    fn = _C_resolve_tramp if call is not None else ffi.NULL
    rc = _native.lib().zxv_freight_ops_expand(_native.cbuf(o), len(o), lim, fn, handle, buf, cap, n)
    if call is not None and call.error is not None:
        err = call.error
        if rc < 0:
            try:
                _native.check(rc)
            except ZxvError as status_exc:
                raise err from status_exc
        raise err
    _native.check(rc)
    return bytes(ffi.buffer(buf, n[0]))


def ops_encode(data: bytes, *, with_sha256: bool = False) -> bytes:
    """Greedy LZ77 encoding of ``data`` as a LITERAL/COPY op stream (deterministic),
    optionally carrying the output SHA-256. The result may be LARGER than the
    input (see the module's honest limits)."""
    d = _native.to_bytes(data, "data")
    db = _native.cbuf(d)
    ws = 1 if with_sha256 else 0
    return _native.read_bytes(lambda b, c, n: _native.lib().zxv_freight_ops_encode(db, len(d), ws, b, c, n),
                              None, ops_bound(len(d), with_sha256=with_sha256))


def ops_bound(n: int, *, with_sha256: bool = False) -> int:
    """Worst-case encoded size of ``n`` input bytes (all literals)."""
    out = ffi.new("uint64_t *")
    _native.check(_native.lib().zxv_freight_ops_bound(_u64(n, "n"), 1 if with_sha256 else 0, out))
    return int(out[0])


def gen_run(gen: int, params: bytes, length: int) -> bytes:
    """Run one SEED generator directly and return ``length`` bytes.

    Raises:
        ZxvUsageError: ``ZXV_E_ARG`` (an unknown generator or params it refuses).
    """
    p = _native.to_bytes(params, "params")
    ln = _native.exact_int(length, "length", 0, FREIGHT_OPS_MAX)
    out = ffi.new("uint8_t[]", max(ln, 1))
    _native.check(_native.lib().zxv_freight_gen_run(_u8(gen, "gen"), _native.cbuf(p), len(p),
                                                    out if ln else ffi.NULL, ln))
    return bytes(ffi.buffer(out, ln))


class OpsWriter(SerializedHandle):
    """Builds an op stream op by op (``zxv_freight_ops_writer``).

    The first error is sticky: every later put and :meth:`finish` raise it
    again. The writer stays usable after :meth:`finish` (add more ops and
    finish again).

    Args:
        declared_total: what the ops must produce in all.
        max_bytes: the stream buffer size, 14 (46 with ``sha256``)..:data:`FREIGHT_OPS_MAX`.
        sha256: the SHA-256 of the output, which expansion will verify; optional.
    """

    def __init__(self, declared_total: int, max_bytes: int = 4096, *, sha256: Optional[bytes] = None) -> None:
        sha = ffi.NULL if sha256 is None else _native.cbuf(_native.fixed_bytes(sha256, 32, "sha256"))
        out = ffi.new("zxv_freight_ops_writer **")
        _native.check(_native.lib().zxv_freight_ops_writer_create(_u64(declared_total, "declared_total"), sha,
                                                                  _u64(max_bytes, "max_bytes"), out))
        super().__init__(out[0], None, None)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_freight_ops_writer_destroy(handle)

    def _put(self, fn: Callable[..., int], *args: Any) -> "OpsWriter":
        with self._lock:
            _native.check(fn(self._handle(), *args))
        return self

    def literal(self, data: bytes) -> "OpsWriter":
        """Append a LITERAL op carrying ``data`` (at least one byte). Returns ``self``.

        Raises:
            ZxvUsageError: ``ZXV_E_ARG`` (empty, or more than is left of the total).
            ZxvRefusedError: ``ZXV_E_CAPACITY`` (the buffer is full).
        """
        d = _native.to_bytes(data, "data")
        return self._put(_native.lib().zxv_freight_ops_put_literal, _native.cbuf(d), len(d))

    def copy(self, distance: int, length: int) -> "OpsWriter":
        """Append a COPY op: ``length`` bytes from ``distance`` back (overlap allowed)."""
        return self._put(_native.lib().zxv_freight_ops_put_copy, _u64(distance, "distance"), _u64(length, "length"))

    def cidref(self, content_id: Union[Cid, bytes], length: int) -> "OpsWriter":
        """Append a CIDREF op: ``length`` bytes the receiver resolves by ``content_id`` (1..128 bytes)."""
        c = content_id.binary if isinstance(content_id, Cid) else _native.to_bytes(content_id, "content_id")
        return self._put(_native.lib().zxv_freight_ops_put_cidref, _native.cbuf(c), len(c), _u64(length, "length"))

    def seed(self, gen: int, params: bytes, length: int) -> "OpsWriter":
        """Append a SEED op: ``length`` bytes of built-in generator ``gen`` (:class:`FreightGen`)."""
        p = _native.to_bytes(params, "params")
        return self._put(_native.lib().zxv_freight_ops_put_seed, _u8(gen, "gen"), _native.cbuf(p), len(p),
                         _u64(length, "length"))

    def finish(self) -> bytes:
        """The finished stream.

        Raises:
            ZxvRefusedError: ``ZXV_E_STATE`` (the ops produce fewer bytes than
                declared), or the sticky error of an earlier put.
        """
        with self._lock:
            h = self._handle()
            return _native.read_bytes(lambda b, c, n: _native.lib().zxv_freight_ops_writer_finish(h, b, c, n))
