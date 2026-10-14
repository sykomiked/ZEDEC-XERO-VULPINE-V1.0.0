# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The 0.08889% assurance fee and its 50/25/15/10 split (ABI 1.5, kernel ``pay_assure``).

Exact and integer-only::

    fee(g) = floor(g * 8889 / 10_000_000)

``g`` is in the unit's own minor measure (money minor units, compute
token-cycles, bandwidth bytes, storage byte-hours). The product is formed in
128 bits, so every ``g`` up to 2**64 - 1 is exact; ``fee(1124) == 0`` and
``fee(1125) == 1``. The fee is charged ONCE per transaction, on its gross:
never on a fee, never compounded.

**Carry.** The sub-unit remainder of an account (``0 <= r < 10**7``) is
carried to its next charge (:func:`assure_carry`, or a :class:`FeeAccount`
that stores it for you). Starting from 0, many small charges then pay exactly
what one charge of their sum pays.

**Buckets.** :func:`split` divides a fee 50% reserve floor (backs Vino), 25%
V-Bill dividend pool, 15% infrastructure / node bounties, 10% regenerative
capital. The last three get ``floor(fee * pct / 100)``; the reserve floor
gets the rest, so the four parts always sum to the fee exactly.

All functions are pure: no context, no capability. Honest limits: these are
the arithmetic rules only. How a fee, a reserve or a dividend pool is treated
for tax, securities or consumer law is a question for counsel; nothing here
is a licence or a certification.
"""

from __future__ import annotations

import enum
import threading
from dataclasses import dataclass
from typing import Any, Iterator, Tuple

from . import _native
from ._native import ffi

__all__ = ["FEE_NUM", "FEE_DEN", "FeeBucket", "FeeUnit", "FeeCharge", "FeeSplit", "FeePolicy",
           "FeeAccount", "assure", "assure_carry", "split", "default_policy"]

FEE_NUM = 8889
"""Fee numerator: 0.08889% = 8889 / 10**7."""
FEE_DEN = 10_000_000
"""Fee denominator; a carry is always below it."""


class FeeBucket(enum.IntEnum):
    """Where a fee goes (``ZXV_FEE_*``), the index into :class:`FeeSplit`."""

    RESERVE_FLOOR = 0
    """50% plus every remainder unit."""
    VBILL_DIVIDEND = 1
    """25%."""
    INFRA_BOUNTY = 2
    """15%."""
    REGEN_CAPITAL = 3
    """10%."""


class FeeUnit(enum.IntEnum):
    """Unit kinds a fee is charged in (``ZXV_FEE_UNIT_*``)."""

    MONEY = 0
    """Minor units of a ledger asset."""
    COMPUTE = 1
    """Token-cycles."""
    BANDWIDTH = 2
    """Bytes."""
    STORAGE = 3
    """Byte-hours."""


def _u64(v: Any, what: str) -> int:
    return _native.exact_int(v, what, *_native.U64)


def assure(gross: int) -> int:
    """The assurance fee on ``gross`` with no carry: ``gross * 8889 // 10**7``."""
    out = ffi.new("uint64_t *")
    _native.check(_native.lib().zxv_fee_assure(_u64(gross, "gross"), out))
    return int(out[0])


@dataclass(frozen=True)
class FeeCharge:
    """A fee and the remainder to carry to the account's next charge."""

    fee: int
    remainder: int


def assure_carry(remainder: int, gross: int) -> FeeCharge:
    """The fee on ``gross`` given the account's incoming ``remainder``.

    Raises:
        ZxvUsageError: ``ZXV_E_RANGE`` when ``remainder >= FEE_DEN`` (a corrupt carry).
    """
    fee, rem = ffi.new("uint64_t *"), ffi.new("uint64_t *")
    _native.check(_native.lib().zxv_fee_assure_carry(_u64(remainder, "remainder"), _u64(gross, "gross"),
                                                     fee, rem))
    return FeeCharge(int(fee[0]), int(rem[0]))


@dataclass(frozen=True)
class FeeSplit:
    """A fee divided into its four buckets; the parts sum to the fee exactly."""

    reserve_floor: int
    vbill_dividend: int
    infra_bounty: int
    regen_capital: int

    @property
    def parts(self) -> Tuple[int, int, int, int]:
        """The four parts in :class:`FeeBucket` order."""
        return (self.reserve_floor, self.vbill_dividend, self.infra_bounty, self.regen_capital)

    def __getitem__(self, bucket: int) -> int:
        return self.parts[bucket]

    def __iter__(self) -> Iterator[int]:
        return iter(self.parts)

    @property
    def total(self) -> int:
        return sum(self.parts)


def split(fee: int) -> FeeSplit:
    """Split ``fee`` into its four buckets (the reserve floor takes every remainder unit)."""
    out = ffi.new("uint64_t[4]")
    _native.check(_native.lib().zxv_fee_split(_u64(fee, "fee"), out))
    return FeeSplit(int(out[0]), int(out[1]), int(out[2]), int(out[3]))


@dataclass(frozen=True)
class FeePolicy:
    """The default fee policy (:func:`default_policy`). Ratios are exact ``(num, den)`` pairs."""

    bucket_percent: Tuple[int, int, int, int]
    """Bucket shares in percent: 50, 25, 15, 10."""
    allow_below: bool
    """False: a contribution below the fee is raised to it."""
    credit_rate: Tuple[Tuple[int, int], ...]
    """Posted VFV store-credit rate per :class:`FeeUnit` for a voluntary contribution
    above the fee: money 1/1, the others 0/1 (no credit until a rate is posted)."""
    credit_multiplier: Tuple[int, int]
    """The operator's credit multiplier, 1/1."""
    commons_cap: Tuple[int, int]
    """The no-monopoly cap on one recipient's share of the non-money commons pool, 8/21."""


def default_policy() -> FeePolicy:
    """The default policy the kernel ships with."""
    pct = ffi.new("uint32_t[4]")
    allow = ffi.new("int32_t *")
    rn, rd = ffi.new("uint64_t[4]"), ffi.new("uint64_t[4]")
    mn, md, cn, cd = (ffi.new("uint64_t *") for _ in range(4))
    _native.check(_native.lib().zxv_fee_policy_default(pct, allow, rn, rd, mn, md, cn, cd))
    return FeePolicy((int(pct[0]), int(pct[1]), int(pct[2]), int(pct[3])), bool(allow[0]),
                     tuple((int(rn[i]), int(rd[i])) for i in range(4)), (int(mn[0]), int(md[0])),
                     (int(cn[0]), int(cd[0])))


class FeeAccount:
    """One account's fee carry, kept for you: :meth:`charge` returns each fee and
    stores the remainder for the next one. Thread-safe.

    Args:
        remainder: the carry to start from (0 for a new account; persist
            :attr:`remainder` and pass it back to resume).
    """

    def __init__(self, remainder: int = 0) -> None:
        r = _u64(remainder, "remainder")
        if r >= FEE_DEN:
            raise _native.make_error(-8, f"remainder must be below {FEE_DEN}")
        self._rem = r
        self._lock = threading.Lock()

    @property
    def remainder(self) -> int:
        """The sub-unit remainder carried to the next charge (0..FEE_DEN-1)."""
        return self._rem

    def charge(self, gross: int) -> int:
        """The fee on ``gross``, carrying the remainder."""
        with self._lock:
            c = assure_carry(self._rem, gross)
            self._rem = c.remainder
            return c.fee
