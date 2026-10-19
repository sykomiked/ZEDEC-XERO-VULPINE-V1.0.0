# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Treaties: COBOL copybook records and Sutra text (ABI 1.5, kernel ``treaty``).

A :class:`Treaty` is one batch of settlement work: who takes part, what each
party deposits and may claim back, how the batch's dividend is shared (each
party's weight ``num/den``) and who owes whom. It has two exact spellings
that hold the same treaty:

* the **record**, a fixed :data:`TREATY_REC_LEN`-byte COBOL copybook record
  (``ZXVTRTY.cpy``), plain ASCII on every machine;
* **Sutra** text, for example::

      TREATY CORRIDOR-7 USD SETL
      PARTY ACME 5000 4000 1/2
      PARTY BETA 3000 3000 1/2
      OWE ACME BETA 1200
      END

Both spellings are canonical: parsing then emitting gives the input back
byte for byte, and the two forms of one treaty compare equal. Every parse and
emit checks the rules (weights in lowest terms adding up to exactly 1; claim
never above deposit; every amount fits COBOL ``PIC S9(18)``; 1..16 parties,
0..64 obligations). A refusal raises :class:`~zxv.TreatyError` whose
:attr:`~zxv.TreatyError.fault` is a :class:`TreatyFault` and, for a parse,
whose :attr:`~zxv.TreatyError.offset` is the byte offset of the first fault.

:meth:`Treaty.run` nets the obligations (and, for SETL, posts the legs with
the 0.08889% assurance fee and pays claims back) in a private scratch engine
that is freed before it returns. It needs no capability and changes no
ledger: its numbers are a deterministic report, not a payment.

Honest limits: a treaty is a batch description, not a contract in law;
signatures, consent and authority live elsewhere. One asset per treaty. The
dividend shares are reported only; nothing is paid out of the bucket.

Threading: a :class:`Treaty` is not thread-safe in C, so the SDK serializes
calls on one with a lock. A :class:`TreatyResult` is immutable and may be
read from any thread. Every call here is a cffi call that releases the GIL;
:meth:`Treaty.run` (about 500 KB of scratch heap) is the long one.
"""

from __future__ import annotations

import enum
from dataclasses import dataclass
from fractions import Fraction
from typing import Any, Callable, Iterator, List, Optional, Tuple

from . import _native
from ._handle import NativeHandle, SerializedHandle
from ._native import ffi
from .errors import TreatyError

__all__ = [
    "TREATY_REC_LEN", "TREATY_RESULT_LEN", "TREATY_SUTRA_MAX", "TREATY_MAX_PARTIES",
    "TREATY_MAX_OWES", "TREATY_NAME_MAX", "TREATY_ID_MAX", "TREATY_PIC18_MAX",
    "TREATY_WEIGHT_MAX", "TreatyOp", "TreatyFault", "TreatyParty", "TreatyObligation", "Treaty",
    "TreatyResult", "TreatyPartyResult", "TreatyLeg", "pic_add", "pic_sub", "pic_mul", "pic_div",
]

TREATY_REC_LEN = 2542
"""Length of a treaty record (``ZXVTRTY.cpy``), in bytes."""
TREATY_RESULT_LEN = 2422
"""Length of a result record (``ZXVRSLT.cpy``), in bytes."""
TREATY_SUTRA_MAX = 3843
"""Longest canonical Sutra text, in bytes."""
TREATY_MAX_PARTIES = 16
TREATY_MAX_OWES = 64
TREATY_NAME_MAX = 30
"""Treaty names: 1..30 of ``A-Z 0-9 -``, starting with a letter."""
TREATY_ID_MAX = 8
"""Party ids: 1..8 of the same alphabet."""
TREATY_PIC18_MAX = 999_999_999_999_999_999
"""Largest magnitude of a ``PIC S9(18)`` amount."""
TREATY_WEIGHT_MAX = 999_999_999
"""Largest weight numerator or denominator (``PIC 9(9)``)."""


class TreatyOp(enum.IntEnum):
    """What a treaty does when run (``ZXV_TREATY_OP_*``)."""

    NET = 1
    """Net the obligations only."""
    SETL = 2
    """Net, post the legs with the assurance fee, then pay claims back."""


class TreatyFault(enum.IntEnum):
    """Why a treaty was refused (``ZXV_TREATY_FAULT_*``)."""

    NONE = 0
    MAGIC = 2
    """The record does not start with ``ZXT1``."""
    SYNTAX = 3
    """Sutra text outside the grammar (also any non-ASCII byte)."""
    NAME = 4
    """Treaty name not 1..30 of ``A-Z 0-9 -`` (an empty treaty fails here)."""
    ASSET = 5
    """Asset not three capital letters."""
    OP = 6
    """Operation not NET or SETL."""
    COUNT = 7
    """Party or obligation count out of range."""
    NUMBER = 8
    """A malformed number, or one outside ``PIC S9(18)``."""
    PARTY = 9
    """A bad, duplicate or unknown party id."""
    WEIGHT = 10
    """Weights not in lowest terms, above the maximum, or not adding up to 1."""
    CLAIM = 11
    """A claim above its deposit."""
    OWE = 12
    """An obligation to oneself, or one not above zero."""
    PAD = 13
    """An unused record slot or field that is not blank."""


@dataclass(frozen=True)
class TreatyParty:
    """One party of a treaty (amounts in minor units of the treaty's asset)."""

    id: str
    """Its id (1..8 of ``A-Z 0-9 -``)."""
    deposit: int
    """What it deposits."""
    claim: int
    """What it may claim back (never above :attr:`deposit`)."""
    weight_num: int
    """Numerator of its dividend weight."""
    weight_den: int
    """Denominator of its dividend weight."""

    @property
    def weight(self) -> Fraction:
        """The dividend weight as an exact :class:`~fractions.Fraction`."""
        return Fraction(self.weight_num, self.weight_den)


@dataclass(frozen=True)
class TreatyObligation:
    """Party ``debtor`` owes party ``creditor`` ``amount`` (0-based party indices)."""

    debtor: int
    creditor: int
    amount: int


@dataclass(frozen=True)
class TreatyPartyResult:
    """What running a treaty reported for one party."""

    net: int
    """What it is owed minus what it owes (gross)."""
    balance: int
    """The deposit for NET; otherwise what it holds after the legs, the fee and its claim."""
    dividend: int
    """Its reported share of the dividend bucket (0 for NET); reported, not paid."""


@dataclass(frozen=True)
class TreatyLeg:
    """One netted leg: party ``payer`` pays party ``payee`` ``amount`` (0-based)."""

    payer: int
    payee: int
    amount: int


def _i64(v: Any, what: str) -> int:
    return _native.exact_int(v, what, *_native.I64)


def _raise_treaty(rc: int, fault: Any, offset: Any, owner: Any = None) -> None:
    """Map a status; on ZXV_E_TREATY attach the fault and offset to the exception."""
    if rc >= 0:
        return
    exc = _native.make_error(rc)
    if isinstance(exc, TreatyError):
        exc.fault = TreatyFault(fault[0]) if fault is not None else None
        exc.offset = int(offset[0]) if offset is not None else None
    if not isinstance(exc, Exception):  # pragma: no cover - no FATAL here
        _native.check(rc, owner)
    raise exc


class Treaty(SerializedHandle):
    """One treaty (``zxv_treaty``), parsed from Sutra text or a copybook record.

    Create one with :meth:`from_sutra` or :meth:`from_record`, or make an
    empty one and fill it with :meth:`parse_sutra` / :meth:`parse_record`. A
    failed parse leaves the treaty unchanged. Two treaties compare equal
    (``==``) when they hold the same treaty in every field and slot.

    Use it as a context manager, or call :meth:`close`; the handle holds no
    context and needs no capability.
    """

    def __init__(self) -> None:
        out = ffi.new("zxv_treaty **")
        _native.check(_native.lib().zxv_treaty_create(out))
        super().__init__(out[0], None, None)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_treaty_destroy(handle)

    @classmethod
    def from_sutra(cls, text: str) -> "Treaty":
        """Parse canonical Sutra text into a new treaty.

        Raises:
            TreatyError: the text breaks the grammar or a rule; ``fault`` and
                ``offset`` say why and where.
            ZxvUsageError: ``ZXV_E_TOO_LONG`` (over :data:`TREATY_SUTRA_MAX` bytes).
        """
        t = cls()
        try:
            t.parse_sutra(text)
        except BaseException:
            t.close()
            raise
        return t

    @classmethod
    def from_record(cls, record: bytes) -> "Treaty":
        """Parse a :data:`TREATY_REC_LEN`-byte copybook record into a new treaty.

        Raises:
            TreatyError: a field breaks the copybook or a rule (``offset`` is
                the byte offset in the record).
            ZxvUsageError: ``ZXV_E_ARG`` when ``record`` is not exactly
                :data:`TREATY_REC_LEN` bytes.
        """
        t = cls()
        try:
            t.parse_record(record)
        except BaseException:
            t.close()
            raise
        return t

    # ------------------------------------------------------------ parse / emit

    def parse_sutra(self, text: str) -> None:
        """Replace this treaty with the one ``text`` spells (unchanged on failure)."""
        b = _native.to_cstr(text, "text")
        off, fault = ffi.new("uint32_t *"), ffi.new("int32_t *")
        with self._lock:
            rc = _native.lib().zxv_treaty_parse_sutra(self._handle(), b, off, fault)
        _raise_treaty(rc, fault, off)

    def parse_record(self, record: bytes) -> None:
        """Replace this treaty with the one ``record`` holds (unchanged on failure)."""
        rec = _native.to_bytes(record, "record")
        off, fault = ffi.new("uint32_t *"), ffi.new("int32_t *")
        with self._lock:
            rc = _native.lib().zxv_treaty_parse_record(self._handle(), _native.cbuf(rec), len(rec),
                                                       off, fault)
        _raise_treaty(rc, fault, off)

    def clear(self) -> None:
        """Empty the treaty again (it then fails validation with ``TreatyFault.NAME``)."""
        with self._lock:
            _native.check(_native.lib().zxv_treaty_clear(self._handle()))

    def _read(self, fn: Callable[..., int], first_cap: int) -> bytes:
        with self._lock:
            h = self._handle()

            def call(buf: Any, cap: int, n: Any) -> int:
                rc = int(fn(h, buf, cap, n))
                if rc == -33:
                    fault = ffi.new("int32_t *")
                    _native.lib().zxv_treaty_validate(h, fault)
                    _raise_treaty(rc, fault, None)
                return rc

            return _native.read_bytes(call, None, first_cap)

    def to_sutra(self) -> str:
        """The canonical Sutra text.

        Raises:
            TreatyError: the treaty does not validate (an empty one, say).
        """
        L = _native.lib()
        with self._lock:
            h = self._handle()

            def call(buf: Any, cap: int, n: Any) -> int:
                rc = int(L.zxv_treaty_emit_sutra(h, buf, cap, n))
                if rc == -33:
                    fault = ffi.new("int32_t *")
                    L.zxv_treaty_validate(h, fault)
                    _raise_treaty(rc, fault, None)
                return rc

            return _native.read_text(call)

    def to_record(self) -> bytes:
        """The canonical :data:`TREATY_REC_LEN`-byte copybook record.

        Raises:
            TreatyError: the treaty does not validate.
        """
        return self._read(_native.lib().zxv_treaty_emit_record, TREATY_REC_LEN)

    # ------------------------------------------------------------ checks

    @property
    def fault(self) -> TreatyFault:
        """The first rule this treaty breaks, or ``TreatyFault.NONE`` if it validates."""
        fault = ffi.new("int32_t *")
        with self._lock:
            rc = _native.lib().zxv_treaty_validate(self._handle(), fault)
        if rc not in (0, -33):
            _native.check(rc)
        return TreatyFault(fault[0])

    def validate(self) -> None:
        """Check every rule; raise :class:`TreatyError` (with ``fault``) if one fails."""
        fault = ffi.new("int32_t *")
        with self._lock:
            rc = _native.lib().zxv_treaty_validate(self._handle(), fault)
        _raise_treaty(rc, fault, None)

    @property
    def is_valid(self) -> bool:
        """True if the treaty passes every rule."""
        return self.fault is TreatyFault.NONE

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, Treaty):
            return NotImplemented
        out = ffi.new("int32_t *")
        with self._lock, other._lock:
            _native.check(_native.lib().zxv_treaty_equal(self._handle(), other._handle(), out))
        return bool(out[0])

    __hash__ = None  # type: ignore[assignment]  # mutable

    # ------------------------------------------------------------ contents

    def _text(self, fn: Callable[..., int]) -> str:
        with self._lock:
            h = self._handle()
            return _native.read_text(lambda b, c, n: fn(h, b, c, n))

    @property
    def name(self) -> str:
        """The treaty name (``""`` when empty)."""
        return self._text(_native.lib().zxv_treaty_name)

    @property
    def asset(self) -> str:
        """The asset: three capital letters (``""`` when empty)."""
        return self._text(_native.lib().zxv_treaty_asset)

    def _header(self) -> Tuple[int, int, int]:
        op, np_, no = ffi.new("int32_t *"), ffi.new("uint32_t *"), ffi.new("uint32_t *")
        with self._lock:
            _native.check(_native.lib().zxv_treaty_header(self._handle(), op, np_, no))
        return int(op[0]), int(np_[0]), int(no[0])

    @property
    def op(self) -> Optional[TreatyOp]:
        """The :class:`TreatyOp`, or ``None`` for an empty treaty."""
        op = self._header()[0]
        return TreatyOp(op) if op else None

    @property
    def party_count(self) -> int:
        """Number of parties (0..16)."""
        return self._header()[1]

    @property
    def obligation_count(self) -> int:
        """Number of obligations (0..64)."""
        return self._header()[2]

    def party(self, index: int) -> TreatyParty:
        """Party ``index`` (0-based, treaty order).

        Raises:
            ZxvUsageError: ``ZXV_E_NOT_FOUND`` past the last party.
        """
        i = _native.exact_int(index, "index", *_native.U32)
        dep, clm = ffi.new("int64_t *"), ffi.new("int64_t *")
        wn, wd = ffi.new("uint32_t *"), ffi.new("uint32_t *")
        L = _native.lib()
        with self._lock:
            h = self._handle()
            pid = _native.read_text(lambda b, c, n: L.zxv_treaty_party_id(h, i, b, c, n))
            _native.check(L.zxv_treaty_party(h, i, dep, clm, wn, wd))
        return TreatyParty(pid, int(dep[0]), int(clm[0]), int(wn[0]), int(wd[0]))

    @property
    def parties(self) -> List[TreatyParty]:
        """Every party, in treaty order."""
        return [self.party(i) for i in range(self.party_count)]

    def obligation(self, index: int) -> TreatyObligation:
        """Obligation ``index`` (0-based).

        Raises:
            ZxvUsageError: ``ZXV_E_NOT_FOUND`` past the last obligation.
        """
        i = _native.exact_int(index, "index", *_native.U32)
        fr, to, amt = ffi.new("uint32_t *"), ffi.new("uint32_t *"), ffi.new("int64_t *")
        with self._lock:
            _native.check(_native.lib().zxv_treaty_owe(self._handle(), i, fr, to, amt))
        return TreatyObligation(int(fr[0]), int(to[0]), int(amt[0]))

    @property
    def obligations(self) -> List[TreatyObligation]:
        """Every obligation, in treaty order."""
        return [self.obligation(i) for i in range(self.obligation_count)]

    # ------------------------------------------------------------ run

    def run(self) -> "TreatyResult":
        """Run the treaty in a fresh private engine and return what it reported.

        Deterministic: the same treaty always gives the same result. Nothing
        persists and no capability is needed. Releases the GIL while it runs.

        Raises:
            TreatyError: the treaty does not validate.
            InsufficientFundsError: a payer cannot cover its legs plus the fee,
                or a claim exceeds what is left after settlement.
            ZxvUsageError: ``ZXV_E_RANGE`` (a net position leaves ``PIC S9(18)``
                or an amount passes the engine's 2**59 line limit).
        """
        out = ffi.new("zxv_treaty_result **")
        with self._lock:
            h = self._handle()
            rc = _native.lib().zxv_treaty_run(h, out)
            if rc == -33:
                fault = ffi.new("int32_t *")
                _native.lib().zxv_treaty_validate(h, fault)
                _raise_treaty(rc, fault, None)
        _native.check(rc)
        return TreatyResult(out[0])

    def __repr__(self) -> str:
        if self._closed:
            return "<zxv.Treaty closed>"
        return f"<zxv.Treaty {self.name or '(empty)'} {self.asset} parties={self.party_count}>"


class TreatyResult(NativeHandle):
    """What :meth:`Treaty.run` returned (``zxv_treaty_result``). Immutable.

    Independent of the treaty it came from: either may be closed first.
    """

    def __init__(self, handle: Any) -> None:
        super().__init__(handle, None, None)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_treaty_result_destroy(handle)

    def _summary(self) -> Tuple[int, int, int]:
        p, legs, fees = ffi.new("uint32_t *"), ffi.new("uint32_t *"), ffi.new("int64_t *")
        _native.check(_native.lib().zxv_treaty_result_summary(self._handle(), p, legs, fees))
        return int(p[0]), int(legs[0]), int(fees[0])

    @property
    def party_count(self) -> int:
        return self._summary()[0]

    @property
    def leg_count(self) -> int:
        return self._summary()[1]

    @property
    def fees(self) -> int:
        """The total assurance fee charged (0 for NET)."""
        return self._summary()[2]

    def party(self, index: int) -> TreatyPartyResult:
        """The report for party ``index`` (treaty order)."""
        i = _native.exact_int(index, "index", *_native.U32)
        a, b, c = ffi.new("int64_t *"), ffi.new("int64_t *"), ffi.new("int64_t *")
        _native.check(_native.lib().zxv_treaty_result_party(self._handle(), i, a, b, c))
        return TreatyPartyResult(int(a[0]), int(b[0]), int(c[0]))

    @property
    def parties(self) -> List[TreatyPartyResult]:
        return [self.party(i) for i in range(self.party_count)]

    def leg(self, index: int) -> TreatyLeg:
        """Netted leg ``index``."""
        i = _native.exact_int(index, "index", *_native.U32)
        fr, to, amt = ffi.new("uint32_t *"), ffi.new("uint32_t *"), ffi.new("int64_t *")
        _native.check(_native.lib().zxv_treaty_result_leg(self._handle(), i, fr, to, amt))
        return TreatyLeg(int(fr[0]), int(to[0]), int(amt[0]))

    @property
    def legs(self) -> List[TreatyLeg]:
        """Every netted leg (at most parties - 1)."""
        return [self.leg(i) for i in range(self.leg_count)]

    def __iter__(self) -> Iterator[TreatyPartyResult]:
        return iter(self.parties)

    def to_record(self) -> bytes:
        """The ``ZXVRSLT.cpy`` result record (:data:`TREATY_RESULT_LEN` bytes; party
        numbers in it are 1-based, as the copybook says)."""
        h = self._handle()
        return _native.read_bytes(lambda b, c, n: _native.lib().zxv_treaty_result_record(h, b, c, n),
                                  None, TREATY_RESULT_LEN)


# ---------------------------------------------------------------- PIC S9(18)


def _pic(fn: Callable[..., int], a: int, b: int) -> int:
    out = ffi.new("int64_t *")
    _native.check(fn(_i64(a, "a"), _i64(b, "b"), out))
    return int(out[0])


def pic_add(a: int, b: int) -> int:
    """``a + b`` exactly as COBOL ``ADD`` with ``ON SIZE ERROR`` on ``PIC S9(18)``.

    Raises:
        ZxvUsageError: ``ZXV_E_RANGE`` (a size error) when an operand or the
            result is outside +-:data:`TREATY_PIC18_MAX`.
    """
    return _pic(_native.lib().zxv_treaty_pic_add, a, b)


def pic_sub(a: int, b: int) -> int:
    """``a - b`` on ``PIC S9(18)`` (``ZXV_E_RANGE`` on a size error)."""
    return _pic(_native.lib().zxv_treaty_pic_sub, a, b)


def pic_mul(a: int, b: int) -> int:
    """``a * b`` on ``PIC S9(18)`` (``ZXV_E_RANGE`` on a size error)."""
    return _pic(_native.lib().zxv_treaty_pic_mul, a, b)


def pic_div(a: int, b: int) -> Tuple[int, int]:
    """``DIVIDE a BY b GIVING q REMAINDER r``: truncates toward zero and the
    remainder has the dividend's sign (``-7 / 2 = (-3, -1)``, ``7 / -2 = (-3, 1)``).

    Raises:
        ZxvUsageError: ``ZXV_E_RANGE`` on a size error or division by zero.
    """
    q, r = ffi.new("int64_t *"), ffi.new("int64_t *")
    _native.check(_native.lib().zxv_treaty_pic_div(_i64(a, "a"), _i64(b, "b"), q, r))
    return int(q[0]), int(r[0])
