# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The 64-state hexagram tag (ABI 1.3, kernel ``lpres_hex``) and its quarantine log.

A :class:`Hexagram` tags one unit of *work* (an attestation, a posting, an
agent task, a model request) with three 2-bit fields: the four-valued
:class:`HexBase`, the :class:`HexPolarity` of its evidence and the
:class:`HexPhase` of its lifecycle. What happens next is :attr:`Hexagram.action`.
Every rule is computed by ``libzxv``; this module only wraps it.

The base codes are the hexagram line patterns (False 00, Neither 01, Both 10,
True 11) and are **not** the :class:`~zxv.Truth` values; convert with
:meth:`Hexagram.to_truth` and :meth:`Hexagram.from_truth`, never with ``int``.
"""

from __future__ import annotations

import enum
from dataclasses import dataclass
from typing import Any, Iterator, final

from . import _native
from ._handle import NativeHandle
from ._native import ffi
from .context import Capability
from .truth import Truth

__all__ = ["HexBase", "HexPolarity", "HexPhase", "HexAction", "Hexagram", "QuarantineLog",
           "QuarantineRecord", "QUARANTINE_MAX_RECORDS"]

QUARANTINE_MAX_RECORDS = 65536
"""Largest capacity of a :class:`QuarantineLog` (``ZXV_HEXLOG_MAX_RECORDS``)."""


class HexBase(enum.IntEnum):
    """The four-valued base, bits 5-4 (``ZXV_HEX_BASE_*``). Not the Truth numbering."""

    FALSE = 0
    NEITHER = 1
    BOTH = 2
    TRUE = 3


class HexPolarity(enum.IntEnum):
    """How the evidence is moving, bits 3-2 (``ZXV_HEX_POL_*``)."""

    ENTROPIC = 0
    """Diverging or stale evidence."""
    NEUTRAL = 1
    """Steady."""
    FORBIDDEN = 2
    """A wall: absorbing, the work rolls back and is quarantined."""
    NEGENTROPIC = 3
    """Converging evidence."""


class HexPhase(enum.IntEnum):
    """The work's lifecycle, bits 1-0 (``ZXV_HEX_PHASE_*``), a Gray cycle:
    POTENTIAL -> EMERGING -> MANIFEST -> DISSOLVING -> POTENTIAL."""

    POTENTIAL = 0
    EMERGING = 1
    DISSOLVING = 2
    MANIFEST = 3

    def next(self) -> "HexPhase":
        """The next phase on the cycle (``zxv_hex_phase_next``)."""
        out = ffi.new("uint8_t *")
        _native.check(_native.lib().zxv_hex_phase_next(self.value, out))
        return HexPhase(out[0])


class HexAction(enum.IntEnum):
    """What happens next (``ZXV_HEX_ACT_*``)."""

    PROCEED = 0
    WAIT = 1
    REFUSE = 2
    ESCALATE = 3
    ROLLBACK = 4
    """Roll back and quarantine (:class:`QuarantineLog`)."""


@final
class Hexagram:
    """An immutable 64-state tag: ``tag = base << 4 | polarity << 2 | phase``.

    Operators ``&``, ``|`` and ``~`` combine tags (the four-valued tables on
    the base, the worst polarity, the least advanced phase). A forbidden tag
    stays forbidden. A ``Hexagram`` has no implicit ``bool`` value.
    """

    __slots__ = ("_tag",)
    _tag: int

    def __init__(self, base: HexBase, polarity: HexPolarity, phase: HexPhase) -> None:
        for v, t, n in ((base, HexBase, "base"), (polarity, HexPolarity, "polarity"),
                        (phase, HexPhase, "phase")):
            if not isinstance(v, t):
                raise TypeError(f"{n} must be a {t.__name__}, not {type(v).__name__}")
        out = ffi.new("uint8_t *")
        _native.check(_native.lib().zxv_hex_pack(base.value, polarity.value, phase.value, out))
        object.__setattr__(self, "_tag", int(out[0]))

    @classmethod
    def from_tag(cls, tag: int) -> "Hexagram":
        """The tag with this byte value (0..63); anything else raises :class:`ZxvUsageError`."""
        t = _native.exact_int(tag, "tag", 0, 255)
        _native.check(_native.lib().zxv_hex_unpack(t, ffi.NULL, ffi.NULL, ffi.NULL))
        h = object.__new__(cls)
        object.__setattr__(h, "_tag", t)
        return h

    @classmethod
    def from_truth(cls, truth: Truth, polarity: HexPolarity, phase: HexPhase) -> "Hexagram":
        """A tag whose base is ``truth`` (mapped by ``zxv_hex_from_lp``)."""
        if not isinstance(truth, Truth):
            raise TypeError("truth must be a Truth")
        out = ffi.new("uint8_t *")
        _native.check(_native.lib().zxv_hex_from_lp(truth.value, out))
        return cls(HexBase(out[0]), polarity, phase)

    @staticmethod
    def polarity_from_evidence(pos0: int, neg0: int, pos1: int, neg1: int, *,
                               stale: bool) -> HexPolarity:
        """Polarity from evidence counts before and after: stale -> ENTROPIC; else the
        margin ``|pos - neg|`` growing -> NEGENTROPIC, shrinking -> ENTROPIC, same -> NEUTRAL."""
        if not isinstance(stale, bool):
            raise TypeError("stale must be a bool")
        args = [_native.exact_int(v, n, *_native.U32)
                for v, n in ((pos0, "pos0"), (neg0, "neg0"), (pos1, "pos1"), (neg1, "neg1"))]
        out = ffi.new("uint8_t *")
        _native.check(_native.lib().zxv_hex_polarity(*args, 1 if stale else 0, out))
        return HexPolarity(out[0])

    def __setattr__(self, name: str, value: Any) -> None:
        raise AttributeError("Hexagram is immutable")

    # ------------------------------------------------------------ fields

    @property
    def tag(self) -> int:
        """The byte value, 0..63."""
        return self._tag

    @property
    def base(self) -> HexBase:
        return HexBase(self._tag >> 4)

    @property
    def polarity(self) -> HexPolarity:
        return HexPolarity((self._tag >> 2) & 3)

    @property
    def phase(self) -> HexPhase:
        return HexPhase(self._tag & 3)

    @property
    def is_forbidden(self) -> bool:
        return self.polarity is HexPolarity.FORBIDDEN

    @property
    def action(self) -> HexAction:
        """What happens next (one lookup in the kernel's 64-entry table)."""
        out = ffi.new("int32_t *")
        _native.check(_native.lib().zxv_hex_action(self._tag, out))
        return HexAction(out[0])

    @property
    def name(self) -> str:
        """``"base/polarity/phase"``, e.g. ``"true/negentropic/manifest"``."""
        return str(ffi.string(_native.lib().zxv_hex_name(self._tag)).decode("ascii"))

    def to_truth(self) -> Truth:
        """The base as a :class:`Truth` (``zxv_hex_to_lp``)."""
        out = ffi.new("int32_t *")
        _native.check(_native.lib().zxv_hex_to_lp(self._tag >> 4, out))
        return Truth(out[0])

    # ------------------------------------------------------------ transitions

    def _call(self, fn: Any, *args: Any) -> "Hexagram":
        out = ffi.new("uint8_t *")
        _native.check(fn(*args, out))
        return Hexagram.from_tag(int(out[0]))

    def step(self, phase: HexPhase) -> "Hexagram":
        """The same work in ``phase``: the same phase or the next one only.

        Raises:
            ZxvRefusedError: (``ZXV_E_STATE``) the move skips a phase.
        """
        if not isinstance(phase, HexPhase):
            raise TypeError("phase must be a HexPhase")
        return self._call(_native.lib().zxv_hex_step, self._tag, phase.value)

    def advance(self) -> "Hexagram":
        """One step along the cycle (``step(phase.next())``)."""
        return self.step(self.phase.next())

    def with_state(self, base: HexBase, polarity: HexPolarity) -> "Hexagram":
        """New base and polarity, same phase.

        Raises:
            ZxvRefusedError: (``ZXV_E_STATE``) this tag is forbidden and
                ``polarity`` is not FORBIDDEN (forbidden is absorbing).
        """
        if not isinstance(base, HexBase) or not isinstance(polarity, HexPolarity):
            raise TypeError("base must be a HexBase and polarity a HexPolarity")
        return self._call(_native.lib().zxv_hex_set, self._tag, base.value, polarity.value)

    def forbid(self) -> "Hexagram":
        """The same tag with polarity FORBIDDEN."""
        return self._call(_native.lib().zxv_hex_forbid, self._tag)

    # ------------------------------------------------------------ logic

    def __and__(self, other: "Hexagram") -> "Hexagram":
        if not isinstance(other, Hexagram):
            return NotImplemented
        return self._call(_native.lib().zxv_hex_and, self._tag, other._tag)

    def __or__(self, other: "Hexagram") -> "Hexagram":
        if not isinstance(other, Hexagram):
            return NotImplemented
        return self._call(_native.lib().zxv_hex_or, self._tag, other._tag)

    def __invert__(self) -> "Hexagram":
        return self._call(_native.lib().zxv_hex_not, self._tag)

    def and_(self, other: "Hexagram") -> "Hexagram":
        return self & other

    def or_(self, other: "Hexagram") -> "Hexagram":
        return self | other

    def not_(self) -> "Hexagram":
        return ~self

    # ------------------------------------------------------------ value semantics

    def __bool__(self) -> bool:
        raise TypeError("Hexagram has no implicit bool value; use .action or .to_truth()")

    def __eq__(self, other: object) -> bool:
        return isinstance(other, Hexagram) and other._tag == self._tag

    def __hash__(self) -> int:
        return hash(("Hexagram", self._tag))

    def __repr__(self) -> str:
        return f"Hexagram({self.name}, tag={self._tag})"

    def __str__(self) -> str:
        return self.name


@dataclass(frozen=True)
class QuarantineRecord:
    """One record of a :class:`QuarantineLog`."""

    seq: int
    tag: Hexagram
    reason: int
    submitter: int
    subject: bytes
    prev: bytes
    hash: bytes


class QuarantineLog(NativeHandle):
    """An append-only, SHA-256 hash-chained log of forbidden work (``zxv_hexlog``).

    Creating and appending need ``Cap.ATTEST``. A full log refuses the append
    with ``ZXV_E_CAPACITY`` (:class:`ZxvRefusedError`); the caller must then
    halt the context (:meth:`Context.halt`), because forbidden work it cannot
    record must not continue. Signing :attr:`head` is the caller's job.
    """

    def __init__(self, cap: Capability, capacity: int) -> None:
        ctx = cap.context
        n = _native.exact_int(capacity, "capacity", *_native.U32)
        out = ffi.new("zxv_hexlog **")
        _native.check(_native.lib().zxv_hexlog_create(cap._handle(), n, out), ctx)
        self._cap = cap
        super().__init__(out[0], cap, ctx)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_hexlog_destroy(handle)

    def append(self, tag: Hexagram, reason: int, submitter: int, subject: bytes) -> int:
        """Quarantine forbidden work; returns its sequence number.

        Args:
            tag: a forbidden :class:`Hexagram` (any other is ``ZxvUsageError``).
            reason: the caller's reason code, 0..255.
            submitter: who submitted the work, 0..2**32-1.
            subject: a 32-byte digest of the refused work.
        """
        if not isinstance(tag, Hexagram):
            raise TypeError("tag must be a Hexagram")
        r = _native.exact_int(reason, "reason", 0, 255)
        who = _native.exact_int(submitter, "submitter", *_native.U32)
        subj = _native.to_bytes(subject, "subject")
        if len(subj) != 32:
            raise _native.make_error(-1, "subject must be exactly 32 bytes")
        seq = ffi.new("uint64_t *")
        self._check(_native.lib().zxv_hexlog_append(self._cap._handle(), self._handle(), tag.tag,
                                                    r, who, ffi.from_buffer("uint8_t[]", subj), seq))
        return int(seq[0])

    def __len__(self) -> int:
        out = ffi.new("uint32_t *")
        self._check(_native.lib().zxv_hexlog_count(self._handle(), out))
        return int(out[0])

    @property
    def head(self) -> bytes:
        """The chain head: the last record's hash (32 zero bytes when empty)."""
        out = ffi.new("uint8_t[32]")
        self._check(_native.lib().zxv_hexlog_head(self._handle(), out))
        return bytes(ffi.buffer(out, 32))

    def record(self, index: int) -> QuarantineRecord:
        """Record ``index`` (oldest first)."""
        i = _native.exact_int(index, "index", *_native.U32)
        seq = ffi.new("uint64_t *")
        tag = ffi.new("uint8_t *")
        reason = ffi.new("uint8_t *")
        who = ffi.new("uint32_t *")
        subj, prev, h = ffi.new("uint8_t[32]"), ffi.new("uint8_t[32]"), ffi.new("uint8_t[32]")
        self._check(_native.lib().zxv_hexlog_at(self._handle(), i, seq, tag, reason, who, subj,
                                                prev, h))
        return QuarantineRecord(int(seq[0]), Hexagram.from_tag(int(tag[0])), int(reason[0]),
                                int(who[0]), bytes(ffi.buffer(subj, 32)),
                                bytes(ffi.buffer(prev, 32)), bytes(ffi.buffer(h, 32)))

    def __iter__(self) -> Iterator[QuarantineRecord]:
        for i in range(len(self)):
            yield self.record(i)

    def verify(self) -> None:
        """Recompute every link. A broken chain raises :class:`ZxvFatal` (the context halts)."""
        self._check(_native.lib().zxv_hexlog_verify(self._handle()))

