# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Currencies, ISO 4217 reference data and the triple ledger."""

from __future__ import annotations

import enum
from dataclasses import dataclass
import warnings
from typing import Any, List, Optional

from . import _native
from ._native import ffi
from .context import Capability, Context
from .errors import CapabilityDeniedError

__all__ = [
    "AccountKind",
    "Rail",
    "CurrencyFlag",
    "Iso4217Currency",
    "EnabledCurrency",
    "Currencies",
    "iso4217",
    "Balance",
    "JournalEntry",
    "Ledger",
    "AMOUNT_MAX",
]

AMOUNT_MAX = 9007199254740991
"""Largest amount in one posting and largest |balance| (2**53 - 1)."""


class AccountKind(enum.IntEnum):
    """Ledger account kinds."""

    HOLDER = 0
    """May never go below zero (no debt)."""
    ISSUER = 1
    """May carry a CREDIT (777) balance: issuance or nostro."""


class Rail(enum.IntEnum):
    """Vino rails (platform-internal; never ISO 20022 currencies)."""

    DEBIT = 0
    """555 NCR: asset / backing (sum received)."""
    CREDIT = 1
    """777 NRE: claim / liability (sum sent)."""
    EQUITY = 2
    """888 PNS: debit - credit (the spendable position)."""

    @property
    def numeric(self) -> int:
        """The rail's internal numeric code (555, 777 or 888)."""
        out = ffi.new("uint16_t *")
        _native.check(_native.lib().zxv_rail_numeric(int(self), out))
        return int(out[0])


class CurrencyFlag(enum.IntFlag):
    """ISO 4217 / platform currency flags (``ZXV_CCYF_*``)."""

    NONE = 0
    FUND = 0x01
    """A fund code (e.g. CLF, USN)."""
    MINOR_NA = 0x02
    """Minor unit not applicable (metals, XDR, XTS, XXX)."""
    NO_COUNTRY = 0x04
    """Not the currency of any ISO 3166 country."""
    PAYABLE = 0x08
    """Usable for a payment amount."""
    PRIVATE = 0x10
    """A platform-private unit; never valid in an ISO 20022 Ccy attribute."""


@dataclass(frozen=True)
class Iso4217Currency:
    """One entry of the compiled-in ISO 4217 table."""

    alpha: str
    numeric: int
    minor_units: Optional[int]
    """Decimal places, or ``None`` where ISO 4217 says N.A."""
    flags: CurrencyFlag
    name: str


@dataclass(frozen=True)
class EnabledCurrency:
    """A currency (ISO 4217 or private) enabled in a context."""

    code: str
    minor_units: int
    flags: CurrencyFlag


class _Iso4217:
    """Process-wide, read-only ISO 4217 reference data (``zxv.iso4217``)."""

    def lookup(self, alpha: str) -> Optional[Iso4217Currency]:
        """The entry for an alpha-3 code, or ``None`` (VFV, 777, ... are not ISO)."""
        L = _native.lib()
        num = ffi.new("uint16_t *")
        mu = ffi.new("uint8_t *")
        fl = ffi.new("uint32_t *")
        a = _native.to_cstr(alpha, "alpha")
        rc = L.zxv_iso4217_by_alpha(a, num, mu, fl)
        if rc == -4:
            return None
        _native.check(rc)
        name = _native.read_text(lambda b, c, n: L.zxv_iso4217_name(a, b, c, n))
        return Iso4217Currency(alpha, int(num[0]), None if mu[0] == 255 else int(mu[0]),
                               CurrencyFlag(fl[0]), name)

    def by_numeric(self, numeric: int) -> Optional[Iso4217Currency]:
        """The entry for a numeric code (566 -> NGN), or ``None``."""
        L = _native.lib()
        num = _native.exact_int(numeric, "numeric", 0, 65535)
        mu = ffi.new("uint8_t *")
        fl = ffi.new("uint32_t *")
        n = ffi.new("size_t *")
        buf = ffi.new("char[8]")
        rc = L.zxv_iso4217_by_numeric(num, buf, 8, n, mu, fl)
        if rc == -4:
            return None
        _native.check(rc)
        return self.lookup(ffi.string(buf).decode("ascii"))

    def codes(self) -> List[str]:
        """Every alpha-3 code in the table, in table order."""
        L = _native.lib()
        cnt = ffi.new("uint32_t *")
        _native.check(L.zxv_iso4217_count(cnt))
        return [_native.read_text(lambda b, c, n, i=i: L.zxv_iso4217_at(i, b, c, n))
                for i in range(cnt[0])]

    def published(self) -> str:
        """Publication date of the compiled-in list, e.g. ``"2026-01-01"``."""
        L = _native.lib()
        return _native.read_text(lambda b, c, n: L.zxv_iso4217_published(b, c, n))

    def caveat(self, numeric: int) -> str:
        """Interop caveat for a numeric code (e.g. 777 = Vino CREDIT rail), "" if none."""
        L = _native.lib()
        num = _native.exact_int(numeric, "numeric", 0, 65535)
        return _native.read_text(lambda b, c, n: L.zxv_ccy_caveat(num, b, c, n))

    def is_african_union_member(self, country_alpha2: str) -> bool:
        """True if an ISO 3166 alpha-2 country is an African Union member."""
        out = ffi.new("int32_t *")
        _native.check(_native.lib().zxv_au_is_member(_native.to_cstr(country_alpha2), out))
        return bool(out[0])


iso4217 = _Iso4217()
"""ISO 4217 reference data: ``zxv.iso4217.lookup("NGN")``."""


class Currencies:
    """The currencies enabled in one :class:`Context` (``ctx.currencies``).

    A context starts with none. Enable ISO 4217 codes before opening
    accounts in them; register platform-private units (``"VFV"``) for
    ledger use only.
    """

    def __init__(self, ctx: Context) -> None:
        self._ctx = ctx

    def _cap_for(self, cap: Optional[Capability], what: str) -> Optional[Capability]:
        if cap is None:
            warnings.warn(
                f"Currencies.{what}() without cap= is deprecated since ABI 1.2: pass "
                f"cap= (a Capability with Cap.LEDGER_POST)", DeprecationWarning, stacklevel=3)
            return None
        if not isinstance(cap, Capability):
            raise TypeError("cap must be a zxv.Capability")
        if cap.context is not self._ctx:
            raise CapabilityDeniedError(-17, "ZXV_E_CAP",
                                        "the capability belongs to a different context")
        return cap

    def enable(self, alpha: str, minor_units: Optional[int] = None, *,
               cap: Optional[Capability] = None) -> None:
        """Enable an ISO 4217 currency; ``minor_units`` asserts the expected value.

        ``cap`` needs ``Cap.LEDGER_POST`` and must belong to this context
        (else :class:`CapabilityDeniedError`). Calling without ``cap`` is
        deprecated since ABI 1.2 and emits a :class:`DeprecationWarning`.
        """
        c = self._cap_for(cap, "enable")
        a = _native.to_cstr(alpha, "alpha")
        L = _native.lib()
        if c is not None:
            ch = c._handle()
            if minor_units is None:
                rc = L.zxv_ccy_enable_cap(ch, a)
            else:
                rc = L.zxv_ccy_enable_with_minor_cap(
                    ch, a, _native.exact_int(minor_units, "minor_units", 0, 255))
        else:
            h = self._ctx._handle()
            if minor_units is None:
                rc = L.zxv_ccy_enable(h, a)
            else:
                rc = L.zxv_ccy_enable_with_minor(h, a, _native.exact_int(minor_units, "minor_units", 0, 255))
        _native.check(rc, self._ctx)

    def register_private(self, code: str, minor_units: int, *,
                         cap: Optional[Capability] = None) -> None:
        """Register a platform-private unit (3..8 upper-case letters/digits).

        ``cap`` needs ``Cap.LEDGER_POST`` and must belong to this context.
        Calling without ``cap`` is deprecated since ABI 1.2.
        """
        c = self._cap_for(cap, "register_private")
        cc = _native.to_cstr(code, "code")
        mu = _native.exact_int(minor_units, "minor_units", 0, 255)
        L = _native.lib()
        if c is not None:
            rc = L.zxv_ccy_register_private_cap(c._handle(), cc, mu)
        else:
            rc = L.zxv_ccy_register_private(self._ctx._handle(), cc, mu)
        _native.check(rc, self._ctx)

    def get(self, code: str) -> Optional[EnabledCurrency]:
        """The enabled currency ``code``, or ``None`` if it is not enabled."""
        h = self._ctx._handle()
        mu = ffi.new("uint8_t *")
        fl = ffi.new("uint32_t *")
        rc = _native.lib().zxv_ccy_get(h, _native.to_cstr(code, "code"), mu, fl)
        if rc in (-4, -7):
            return None
        _native.check(rc, self._ctx)
        return EnabledCurrency(code, int(mu[0]), CurrencyFlag(fl[0]))

    def enabled(self) -> List[str]:
        """Codes enabled in this context, in the order they were enabled."""
        h = self._ctx._handle()
        L = _native.lib()
        cnt = ffi.new("uint32_t *")
        _native.check(L.zxv_ccy_enabled_count(h, cnt), self._ctx)
        return [_native.read_text(lambda b, c, n, i=i: L.zxv_ccy_enabled_at(h, i, b, c, n), self._ctx)
                for i in range(cnt[0])]

    def __contains__(self, code: object) -> bool:
        return isinstance(code, str) and self.get(code) is not None


@dataclass(frozen=True)
class Balance:
    """Rail balances of an account, in exact minor units."""

    debit: int
    """DEBIT 555: the sum received."""
    credit: int
    """CREDIT 777: the sum sent."""
    equity: int
    """EQUITY 888 = debit - credit: the spendable position."""


@dataclass(frozen=True)
class JournalEntry:
    """One line of an account's journal."""

    entry_id: int
    """Platform entry id (shared by both sides of the posting)."""
    amount: int
    """Signed minor units: positive received, negative sent."""
    counterparty: int
    """The other account's id."""
    reference: str
    """The posting's unique reference (e.g. an EndToEndId)."""


class Ledger:
    """The context's triple ledger, used through a :class:`Capability`.

    Reads need ``Cap.LEDGER_READ``; posting needs ``Cap.LEDGER_POST``.
    Amounts are exact ``int`` minor units (cents, kobo, ...); floats are
    refused. Accounts are plain ``int`` ids.
    """

    def __init__(self, cap: Capability) -> None:
        """Bind the ledger of ``cap.context`` to ``cap``."""
        self._cap = cap
        self._ctx: Context = cap.context

    @property
    def capability(self) -> Capability:
        """The capability every call uses."""
        return self._cap

    def open_account(self, name: str, currency: str, kind: AccountKind = AccountKind.HOLDER) -> int:
        """Open an account (``name`` <= 63 bytes) in an enabled currency; returns its id.

        Needs ``Cap.LEDGER_POST``.
        """
        c = self._cap._handle()
        out = ffi.new("uint32_t *")
        _native.check(_native.lib().zxv_ledger_open_account_cap(
            c, _native.to_cstr(name, "name"), _native.to_cstr(currency, "currency"),
            int(AccountKind(kind)), out), self._ctx)
        return int(out[0])

    def post(self, from_account: int, to_account: int, amount: int, currency: str,
             reference: str) -> int:
        """Post a balanced transfer of ``amount`` minor units; returns the entry id.

        Raises:
            InsufficientFundsError: a holder account would go below zero.
            CapabilityDeniedError: the capability lacks ``LEDGER_POST``.
            ZxvUsageError: bad currency, range, or a duplicate reference.
        """
        c = self._cap._handle()
        out = ffi.new("uint64_t *")
        rc = _native.lib().zxv_ledger_post_cap(
            c, _native.exact_int(from_account, "from_account", *_native.U32),
            _native.exact_int(to_account, "to_account", *_native.U32),
            _native.exact_int(amount, "amount", *_native.I64),
            _native.to_cstr(currency, "currency"), _native.to_cstr(reference, "reference"), out)
        _native.check(rc, self._ctx)
        return int(out[0])

    def balance(self, account: int) -> Balance:
        """The DEBIT, CREDIT and EQUITY balances of an account."""
        c = self._cap._handle()
        v = ffi.new("int64_t[3]")
        _native.check(_native.lib().zxv_ledger_balance_cap(
            c, _native.exact_int(account, "account", *_native.U32), v, v + 1, v + 2), self._ctx)
        return Balance(int(v[0]), int(v[1]), int(v[2]))

    def check(self) -> None:
        """Verify the ledger's invariants.

        On a broken invariant the context is poisoned and this raises
        :class:`ZxvFatal` (or aborts, under ``FatalPolicy.ABORT``).
        """
        _native.check(_native.lib().zxv_ledger_check_cap(self._cap._handle()), self._ctx)

    def accounts(self) -> List[int]:
        """Every account id in the context. Needs ``Cap.LEDGER_READ``."""
        c = self._cap._handle()
        out = ffi.new("uint32_t *")
        _native.check(_native.lib().zxv_ledger_account_count_cap(c, out), self._ctx)
        return list(range(out[0]))  # ids are 0 .. count-1

    def account_name(self, account: int) -> str:
        """The account's name."""
        h = self._cap._handle()
        L = _native.lib()
        a = _native.exact_int(account, "account", *_native.U32)
        return _native.read_text(lambda b, c, n: L.zxv_ledger_account_name_cap(h, a, b, c, n), self._ctx)

    def account_currency(self, account: int) -> str:
        """The account's currency code."""
        h = self._cap._handle()
        L = _native.lib()
        a = _native.exact_int(account, "account", *_native.U32)
        return _native.read_text(lambda b, c, n: L.zxv_ledger_account_ccy_cap(h, a, b, c, n), self._ctx)

    def account_kind(self, account: int) -> AccountKind:
        """The account's :class:`AccountKind`."""
        h = self._cap._handle()
        out = ffi.new("int32_t *")
        _native.check(_native.lib().zxv_ledger_account_kind_cap(
            h, _native.exact_int(account, "account", *_native.U32), out), self._ctx)
        return AccountKind(out[0])

    def journal(self, account: int) -> List[JournalEntry]:
        """The account's journal, oldest first."""
        h = self._cap._handle()
        L = _native.lib()
        a = _native.exact_int(account, "account", *_native.U32)
        cnt = ffi.new("uint32_t *")
        _native.check(L.zxv_ledger_entry_count_cap(h, a, cnt), self._ctx)
        out: List[JournalEntry] = []
        eid = ffi.new("uint64_t *")
        amt = ffi.new("int64_t *")
        cp = ffi.new("uint32_t *")
        for i in range(cnt[0]):
            def call(b: Any, c: int, n: Any, i: int = i) -> int:
                return int(L.zxv_ledger_entry_at_cap(h, a, i, eid, amt, cp, b, c, n))
            ref = _native.read_text(call, self._ctx)
            out.append(JournalEntry(int(eid[0]), int(amt[0]), int(cp[0]), ref))
        return out
