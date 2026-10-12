# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The V-Bill hub: test currencies, signed exact pegs, quotes, swaps and limit
rules (ABI 1.5, kernel ``vhub``).

A demonstration of cross-currency clearing through V-Bills. Each test
currency has ONE exact peg against the V-Bill (``num`` minor units per ``den``
V-Bills), set only by its issuer with an ML-DSA-87 signature over
:meth:`VHub.peg_message`. A swap from currency A to B goes A -> V-Bill -> B
as ONE balanced, all-or-nothing posting on the hub's own in-process ledger::

    v = a * denA / numA,   b = v * numB / denB

Both divisions must be exact or the swap is refused (``ZXV_E_RANGE``):
nothing is rounded and there is no float rate anywhere. :meth:`VHub.quote`
gives the largest exact amount not above a requested one.

Every rule of the sending AND the receiving currency must allow a swap, and
a currency with no rule refuses every swap through it (fail closed,
``ZXV_E_STATE``), so add a limit (:meth:`VHub.add_limit`) to each currency
first. A limit refusal is :class:`~zxv.LimitRefusedError`.

Capabilities: creating, adding currencies and rules, pegs, accounts, issuing
and swapping need ``Cap.LEDGER_POST``; quotes, balances, currency info and the
check need ``Cap.LEDGER_READ``. Each method uses the hub's capability unless
you pass another one (``cap=``) from the same context. A hub is internally
serialized: any thread may use it.

Honest limits: a demo hub with TEST currencies only (ISO 4217 codes are
refused) and no real money; its ledger lives in process memory, separate from
the context's ledger, and is lost on close. No network: it is designed to
support settlement rails such as PAPSS but is connected to none, nor to any
central bank or issuer. Peg signatures use ML-DSA-87 (not FIPS 140-3
validated). V-Bills are likely securities in many jurisdictions; real use
needs legal review and a participating issuer. An exact peg removes rounding,
not exchange-rate risk.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Optional

from . import _native
from ._handle import NativeHandle
from ._native import ffi
from .context import Capability

__all__ = ["VHUB_MAX_CCY", "VHUB_MAX_RULES", "VHUB_LIMIT_SENDERS", "VHUB_PEG_MSG_BYTES",
           "VHUB_CODE_MAX", "VHUB_AMOUNT_MAX", "VHUB_VBILL_ISSUER", "MLDSA87_PK_BYTES",
           "MLDSA87_SIG_BYTES", "VHub", "Peg", "VHubCurrency", "VHubBalance", "VHubQuote", "VHubSwap"]

VHUB_MAX_CCY = 8
"""Currency slots 0..7."""
VHUB_MAX_RULES = 4
"""Limit rules per currency."""
VHUB_LIMIT_SENDERS = 16
"""Distinct senders one limit tracks per epoch (a 17th is refused)."""
VHUB_PEG_MSG_BYTES = 40
VHUB_CODE_MAX = 12
VHUB_AMOUNT_MAX = 1 << 59
"""Largest single amount; balances stay below 2**62."""
VHUB_VBILL_ISSUER = 0
"""The V-Bill issuer's account: always account 0."""
MLDSA87_PK_BYTES = 2592
MLDSA87_SIG_BYTES = 4627


def _u64(v: Any, what: str) -> int:
    return _native.exact_int(v, what, *_native.U64)


def _u32(v: Any, what: str) -> int:
    return _native.exact_int(v, what, *_native.U32)


def _slot(v: Any, what: str = "slot") -> int:
    return _native.exact_int(v, what, 0, 255)


@dataclass(frozen=True)
class Peg:
    """A currency's peg: ``num`` minor units per ``den`` V-Bills, at ``epoch``."""

    num: int
    den: int
    epoch: int


@dataclass(frozen=True)
class VHubCurrency:
    """A registered currency's accounts and current peg (``None`` until set)."""

    slot: int
    issuer_account: int
    pool_account: int
    vfloat_account: int
    peg: Optional[Peg]


@dataclass(frozen=True)
class VHubBalance:
    """One account's rails: DEBIT (held), CREDIT (owed), EQUITY = DEBIT - CREDIT."""

    debit: int
    credit: int
    equity: int


@dataclass(frozen=True)
class VHubQuote:
    """The largest exact amount not above the request, and what it would carry."""

    amount: int
    vbills: int
    received: int


@dataclass(frozen=True)
class VHubSwap:
    """What a swap moved."""

    vbills: int
    received: int


class VHub(NativeHandle):
    """One demo hub and its in-process ledger (``zxv_vhub``).

    Its ledger starts with the V-Bill asset (``"VBL"``, 0 minor units) and the
    V-Bill issuer's account (:data:`VHUB_VBILL_ISSUER`); the rule epoch starts at 0.

    Args:
        cap: a capability holding ``Cap.LEDGER_POST`` (reads also need
            ``Cap.LEDGER_READ``, from this cap or one passed per call).
        seed: 32 bytes from which every posting's idempotency key and UETR
            are derived; ``None`` draws a fresh random seed.

    Raises:
        CapabilityDeniedError: no ``Cap.LEDGER_POST``.
    """

    def __init__(self, cap: Capability, seed: Optional[bytes] = None) -> None:
        ctx = cap.context
        s = ffi.NULL if seed is None else _native.cbuf(_native.fixed_bytes(seed, 32, "seed"))
        out = ffi.new("zxv_vhub **")
        _native.check(_native.lib().zxv_vhub_create(cap._handle(), s, out), ctx)
        self._cap = cap
        super().__init__(out[0], cap, ctx)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_vhub_destroy(handle)

    def _c(self, cap: Optional[Capability]) -> Any:
        return (cap or self._cap)._handle()

    # ------------------------------------------------------------ currencies

    def add_currency(self, slot: int, code: str, minor_units: int, issuer_public_key: bytes, *,
                     pool_funding: int = 0, vbill_backing: int = 0,
                     cap: Optional[Capability] = None) -> None:
        """Register test currency ``slot`` (0..7); read it back with :meth:`currency`.

        Opens the issuer's account, the hub's pool of the currency and its
        V-Bill float, funds the pool with ``pool_funding`` and the float with
        ``vbill_backing`` V-Bills. The peg starts unset. ``issuer_public_key``
        (an ML-DSA-87 key, :data:`MLDSA87_PK_BYTES`) alone may set the peg.

        Args:
            code: 3..12 of ``A-Z 0-9``; not an ISO 4217 code, not VBL or VFV.
            minor_units: 0..4.

        Raises:
            ZxvUsageError: ``ZXV_E_CURRENCY`` (an ISO 4217 code), ``ZXV_E_DUPLICATE``
                (slot or code taken), ``ZXV_E_FORMAT`` / ``ZXV_E_RANGE`` (bad fields).
        """
        pk = _native.fixed_bytes(issuer_public_key, MLDSA87_PK_BYTES, "issuer_public_key")
        s = _slot(slot)
        self._check(_native.lib().zxv_vhub_add_ccy(
            self._c(cap), self._handle(), s, _native.to_cstr(code, "code"),
            _native.exact_int(minor_units, "minor_units", 0, 255), _native.cbuf(pk),
            _u64(pool_funding, "pool_funding"), _u64(vbill_backing, "vbill_backing")))

    def currency(self, slot: int, *, cap: Optional[Capability] = None) -> VHubCurrency:
        """A currency's accounts and current peg (needs ``Cap.LEDGER_READ``).

        Raises:
            ZxvUsageError: ``ZXV_E_NOT_FOUND`` (slot not registered).
        """
        s = _slot(slot)
        ia, pa, va = ffi.new("uint32_t *"), ffi.new("uint32_t *"), ffi.new("uint32_t *")
        pn, pd, pe = ffi.new("uint64_t *"), ffi.new("uint64_t *"), ffi.new("uint64_t *")
        self._check(_native.lib().zxv_vhub_ccy_info(self._c(cap), self._handle(), s, ia, pa, va, pn, pd, pe))
        peg = None if pd[0] == 0 else Peg(int(pn[0]), int(pd[0]), int(pe[0]))
        return VHubCurrency(s, int(ia[0]), int(pa[0]), int(va[0]), peg)

    # ------------------------------------------------------------ pegs

    @staticmethod
    def peg_message(slot: int, num: int, den: int, epoch: int) -> bytes:
        """The exact 40 bytes an issuer signs to set a peg: ``"zxv/vhub-peg/v1"`` ||
        slot || num || den || epoch (each 8 bytes little-endian). Sign them with
        :meth:`zxv.MlDsa87.sign` and an empty context. Pure."""
        out = ffi.new("uint8_t[40]")
        _native.check(_native.lib().zxv_vhub_peg_msg(_slot(slot), _u64(num, "num"), _u64(den, "den"),
                                                     _u64(epoch, "epoch"), out))
        return bytes(ffi.buffer(out, 40))

    def set_peg(self, slot: int, num: int, den: int, epoch: int, signature: bytes, *,
                cap: Optional[Capability] = None) -> None:
        """Set a currency's peg to ``num/den`` at ``epoch`` (which must be above the
        current one), given the issuer's ML-DSA-87 signature over
        :meth:`peg_message`. Applies to the next quote and swap.

        Raises:
            PegError: the epoch is not above the current one (stale or replayed).
            VerificationFailedError: not the issuer's signature over these numbers.
            ZxvUsageError: ``ZXV_E_RANGE`` (num or den 0), ``ZXV_E_NOT_FOUND``.
        """
        sig = _native.fixed_bytes(signature, MLDSA87_SIG_BYTES, "signature")
        self._check(_native.lib().zxv_vhub_set_peg(self._c(cap), self._handle(), _slot(slot), _u64(num, "num"),
                                                   _u64(den, "den"), _u64(epoch, "epoch"), _native.cbuf(sig)))

    # ------------------------------------------------------------ accounts

    def open_account(self, slot: int, owner: int, *, cap: Optional[Capability] = None) -> int:
        """Open a holder account in currency ``slot`` for ``owner`` (an id of your
        choosing); returns its account number.

        Raises:
            ZxvRefusedError: ``ZXV_E_CAPACITY`` (the ledger holds 512 accounts).
        """
        out = ffi.new("uint32_t *")
        self._check(_native.lib().zxv_vhub_open_account(self._c(cap), self._handle(), _slot(slot),
                                                        _u32(owner, "owner"), out))
        return int(out[0])

    def issue(self, account: int, amount: int, *, cap: Optional[Capability] = None) -> None:
        """Test faucet: issue ``amount`` to a holder account or a currency's pool (by
        its issuer), or V-Bills to a V-Bill float (by the V-Bill issuer).

        Raises:
            ZxvUsageError: ``ZXV_E_RANGE`` (0, above :data:`VHUB_AMOUNT_MAX`,
                or a balance would reach 2**62), ``ZXV_E_NOT_FOUND``, ``ZXV_E_ARG``
                (an issuer account).
        """
        self._check(_native.lib().zxv_vhub_issue(self._c(cap), self._handle(), _u32(account, "account"),
                                                 _u64(amount, "amount")))

    def balance(self, account: int, *, cap: Optional[Capability] = None) -> VHubBalance:
        """One account's rails (needs ``Cap.LEDGER_READ``).

        Raises:
            ZxvUsageError: ``ZXV_E_NOT_FOUND``.
        """
        d, c, e = ffi.new("uint64_t *"), ffi.new("uint64_t *"), ffi.new("int64_t *")
        self._check(_native.lib().zxv_vhub_balance(self._c(cap), self._handle(), _u32(account, "account"),
                                                   d, c, e))
        return VHubBalance(int(d[0]), int(c[0]), int(e[0]))

    # ------------------------------------------------------------ rules

    def add_limit(self, slot: int, limit: int, *, cap: Optional[Capability] = None) -> None:
        """Add the built-in limit rule to currency ``slot``: at most ``limit`` minor
        units out per sender per rule epoch (inflows not counted). Several limits
        on one currency all apply.

        Raises:
            ZxvRefusedError: ``ZXV_E_CAPACITY`` (4 rules already).
        """
        self._check(_native.lib().zxv_vhub_add_limit(self._c(cap), self._handle(), _slot(slot),
                                                     _u64(limit, "limit")))

    def next_epoch(self, *, cap: Optional[Capability] = None) -> int:
        """Start the next rule epoch (every limit restarts every sender at zero);
        returns the new epoch."""
        out = ffi.new("uint64_t *")
        self._check(_native.lib().zxv_vhub_next_epoch(self._c(cap), self._handle(), out))
        return int(out[0])

    # ------------------------------------------------------------ quotes and swaps

    def quote(self, from_slot: int, to_slot: int, want: int, *, cap: Optional[Capability] = None) -> VHubQuote:
        """The largest exact amount of ``from_slot`` not above ``want`` (0 if none) and
        what it would carry. Does not run rules or check balances, so a quoted
        swap may still be refused. Needs ``Cap.LEDGER_READ``.

        Raises:
            PegError: either peg unset.
            ZxvUsageError: ``ZXV_E_ARG`` (``from_slot == to_slot``), ``ZXV_E_NOT_FOUND``.
        """
        a, v, r = ffi.new("uint64_t *"), ffi.new("uint64_t *"), ffi.new("uint64_t *")
        self._check(_native.lib().zxv_vhub_quote(self._c(cap), self._handle(), _slot(from_slot, "from_slot"),
                                                 _slot(to_slot, "to_slot"), _u64(want, "want"), a, v, r))
        return VHubQuote(int(a[0]), int(v[0]), int(r[0]))

    def swap(self, from_slot: int, to_slot: int, sender: int, receiver: int, amount: int, tick: int, *,
             cap: Optional[Capability] = None) -> VHubSwap:
        """Swap ``amount`` of ``from_slot`` from holder ``sender`` to holder ``receiver``
        in ``to_slot``, as one six-line posting at ``tick`` (your time value; the
        hub never reads a clock). A refused swap changes nothing.

        Raises:
            ZxvRefusedError: ``ZXV_E_STATE`` (a currency has no rule).
            LimitRefusedError: a limit rule refused it.
            InsufficientFundsError: the sender, A's V-Bill float or B's pool would go below zero.
            PegError: either peg unset.
            ZxvUsageError: ``ZXV_E_RANGE`` (0, too large, or not exact under the
                pegs), ``ZXV_E_CURRENCY`` (an account of another currency).
            ZxvFatal: the ledger broke an invariant (the context is poisoned).
        """
        v, r = ffi.new("uint64_t *"), ffi.new("uint64_t *")
        self._check(_native.lib().zxv_vhub_swap(self._c(cap), self._handle(), _slot(from_slot, "from_slot"),
                                                _slot(to_slot, "to_slot"), _u32(sender, "sender"),
                                                _u32(receiver, "receiver"), _u64(amount, "amount"),
                                                _u64(tick, "tick"), v, r))
        return VHubSwap(int(v[0]), int(r[0]))

    def check(self, *, cap: Optional[Capability] = None) -> int:
        """Verify the whole hub ledger (A = L + E per account, DEBIT == CREDIT per
        asset, the posting hash chain); returns the postings made so far.

        Raises:
            ZxvFatal: a failure (``ZXV_E_INVARIANT``; the context is poisoned).
        """
        out = ffi.new("uint64_t *")
        self._check(_native.lib().zxv_vhub_check(self._c(cap), self._handle(), out))
        return int(out[0])
