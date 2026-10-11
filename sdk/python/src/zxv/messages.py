# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""ISO 20022 messages, netting cycles, cards and VSS conformance.

These are the ABI 1.0 payment groups. In the current build only pacs.008,
camt.053 and card check digits are live (see :func:`zxv.features`); the
rest raise :class:`ZxvNotImplementedError` until the kernel modules land.
The library serializes messages only: it has no network transport and is
not certified by any payment scheme.
"""

from __future__ import annotations

import enum
from dataclasses import dataclass
import warnings
from typing import Any, List, Union

from . import _native
from ._handle import NativeHandle
from ._native import ffi
from .context import Capability, Context

__all__ = [
    "MxKind", "MxField", "MxAmount", "CdtDbt", "EntryStatus", "MxMessage",
    "NettingCycle", "NetPosition", "CardNetwork", "Cards", "CardAuthorization",
    "VssCheck", "VssLevel", "Vss",
]


class MxKind(enum.IntEnum):
    """ISO 20022 message kinds."""

    PACS008 = 1
    """FIToFICstmrCdtTrf pacs.008.001.09."""
    CAMT053 = 2
    """BkToCstmrStmt camt.053.001.08."""
    PACS002 = 3
    PACS004 = 4
    PACS009 = 5
    CAMT056 = 6
    CAMT029 = 7


class MxField(enum.IntEnum):
    """Text fields of a message (each kind accepts a subset)."""

    MSG_ID = 1
    CRE_DT_TM = 2
    DEBTOR_NAME = 3
    DEBTOR_ACCT = 4
    CREDITOR_NAME = 5
    CREDITOR_ACCT = 6
    END_TO_END_ID = 7
    CCY = 8
    ACCT_ID = 9


class MxAmount(enum.IntEnum):
    """Amount fields of a message."""

    SETTLEMENT = 1
    OPENING = 2
    CLOSING = 3


class CdtDbt(enum.IntEnum):
    """Credit or debit direction of a statement entry."""

    CRDT = 0
    DBIT = 1


class EntryStatus(enum.IntEnum):
    """Status of a statement entry."""

    BOOK = 0
    PDNG = 1
    INFO = 2


class MxMessage(NativeHandle):
    """One ISO 20022 message under construction. Not thread-safe (R6).

    >>> with MxMessage(cap, MxKind.PACS008) as m:           # doctest: +SKIP
    ...     m.set_text(MxField.MSG_ID, "M1")
    ...     xml = m.render()

    A message made from a :class:`Capability` (needs ``Cap.LEDGER_READ``)
    re-checks it on every call: revoking the capability, or clearing its
    attestation, stops the message. Closing the capability closes the
    message first.
    """

    def __init__(self, owner: Union[Capability, Context], kind: MxKind) -> None:
        """Create a message of ``kind`` through ``owner``, a :class:`Capability`.

        Passing a :class:`Context` is deprecated since ABI 1.2 (no
        capability) and emits a :class:`DeprecationWarning`.
        """
        out = ffi.new("zxv_msg **")
        k = int(MxKind(kind))
        if isinstance(owner, Capability):
            ctx = owner.context
            _native.check(_native.lib().zxv_msg_create_cap(owner._handle(), k, out), ctx)
            super().__init__(out[0], owner, ctx)
            return
        warnings.warn("MxMessage(ctx, kind) is deprecated since ABI 1.2: pass a Capability "
                      "with Cap.LEDGER_READ instead of the Context", DeprecationWarning, stacklevel=2)
        _native.check(_native.lib().zxv_msg_create(owner._handle(), k, out), owner)
        super().__init__(out[0], owner, owner)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_msg_destroy(handle)

    def set_text(self, field: MxField, value: str) -> "MxMessage":
        """Set a text field (plain text; the serializer escapes XML). Returns self."""
        self._check(_native.lib().zxv_msg_set_text(self._handle(), int(MxField(field)),
                                                   _native.to_cstr(value, "value")))
        return self

    def set_amount(self, field: MxAmount, units: int, frac_digits: int) -> "MxMessage":
        """Set an amount as exact ``units`` with ``frac_digits`` decimals. Returns self."""
        self._check(_native.lib().zxv_msg_set_amount(
            self._handle(), int(MxAmount(field)), _native.exact_int(units, "units", *_native.I64),
            _native.exact_int(frac_digits, "frac_digits", 0, 255)))
        return self

    def set_tx_count(self, count: int) -> "MxMessage":
        """Set GrpHdr/NbOfTxs (default 1). Returns self."""
        self._check(_native.lib().zxv_msg_set_tx_count(self._handle(),
                                                       _native.exact_int(count, "count", *_native.U32)))
        return self

    def add_entry(self, units: int, frac_digits: int, direction: CdtDbt,
                  status: EntryStatus = EntryStatus.BOOK) -> "MxMessage":
        """Append a camt.053 statement entry. Returns self."""
        self._check(_native.lib().zxv_msg_add_entry(
            self._handle(), _native.exact_int(units, "units", *_native.I64),
            _native.exact_int(frac_digits, "frac_digits", 0, 255), int(CdtDbt(direction)),
            int(EntryStatus(status))))
        return self

    def render(self) -> str:
        """Serialize to XML (validates mandatory fields and the currency)."""
        h = self._handle()
        L = _native.lib()
        return _native.read_text(lambda b, c, n: L.zxv_msg_render(h, b, c, n), self._context)


@dataclass(frozen=True)
class NetPosition:
    """A participant's net position after a netting cycle closes."""

    participant: str
    net: int
    """Minor units: positive receives, negative pays."""


class NettingCycle(NativeHandle):
    """A multilateral netting cycle (kernel cbank; pending in this build)."""

    def __init__(self, ctx: Context, cycle_id: str, currency: str) -> None:
        """Open a cycle in one settlement currency."""
        out = ffi.new("zxv_netting **")
        _native.check(_native.lib().zxv_netting_open(
            ctx._handle(), _native.to_cstr(cycle_id), _native.to_cstr(currency), out), ctx)
        super().__init__(out[0], ctx, ctx)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_netting_destroy(handle)

    def add_participant(self, participant: str) -> None:
        """Add a participant (e.g. a BIC)."""
        self._check(_native.lib().zxv_netting_add_participant(self._handle(), _native.to_cstr(participant)))

    def submit(self, payment_id: str, debtor: str, creditor: str, amount: int) -> None:
        """Submit an obligation (idempotent on ``payment_id``)."""
        self._check(_native.lib().zxv_netting_submit(
            self._handle(), _native.to_cstr(payment_id), _native.to_cstr(debtor),
            _native.to_cstr(creditor), _native.exact_int(amount, "amount", *_native.I64)))

    def close_cycle(self) -> List[NetPosition]:
        """Close the cycle and return the net positions (they sum to zero)."""
        h = self._handle()
        L = _native.lib()
        self._check(L.zxv_netting_close(h))
        cnt = ffi.new("uint32_t *")
        self._check(L.zxv_netting_position_count(h, cnt))
        net = ffi.new("int64_t *")
        out = []
        for i in range(cnt[0]):
            name = _native.read_text(lambda b, c, n, i=i: L.zxv_netting_position_at(h, i, b, c, n, net),
                                     self._context)
            out.append(NetPosition(name, int(net[0])))
        return out


class CardNetwork(enum.IntEnum):
    """Card networks and their check-digit schemes."""

    PHOENIX = 1
    """Damm check digit."""
    DRAGON = 2
    """Luhn check digit."""
    THUNDERBIRD = 3
    """Verhoeff check digit."""


@dataclass(frozen=True)
class CardAuthorization:
    """Result of :meth:`Cards.authorize`."""

    approved: bool
    response_code: int
    """ISO 8583 response code (e.g. 51 insufficient funds)."""
    approval_code: str


class Cards:
    """Charge cards bound to ledger accounts (issuing is pending in this build)."""

    def __init__(self, ctx: Context) -> None:
        self._ctx = ctx

    @staticmethod
    def check_digit_valid(network: CardNetwork, pan: str) -> bool:
        """Validate a decimal PAN's check digit for a network."""
        out = ffi.new("int32_t *")
        _native.check(_native.lib().zxv_card_check_digit(int(CardNetwork(network)),
                                                         _native.to_cstr(pan, "pan"), out))
        return bool(out[0])

    def issue(self, network: CardNetwork, account: int, limit: int) -> "tuple[int, str]":
        """Issue a card; returns ``(card_id, masked_pan)``."""
        h = self._ctx._handle()
        L = _native.lib()
        cid = ffi.new("uint64_t *")
        acc = _native.exact_int(account, "account", *_native.U32)
        lim = _native.exact_int(limit, "limit", *_native.I64)
        masked = _native.read_text(
            lambda b, c, n: L.zxv_card_issue(h, int(CardNetwork(network)), acc, lim, cid, b, c, n), self._ctx)
        return int(cid[0]), masked

    def reissue(self, card_id: int) -> int:
        """Block a card and issue a replacement; returns the new id."""
        out = ffi.new("uint64_t *")
        _native.check(_native.lib().zxv_card_reissue(
            self._ctx._handle(), _native.exact_int(card_id, "card_id", *_native.U64), out), self._ctx)
        return int(out[0])

    def authorize(self, card_id: int, amount: int, currency: str) -> CardAuthorization:
        """Authorize an amount. A decline is a result, not an exception."""
        h = self._ctx._handle()
        L = _native.lib()
        resp = ffi.new("int32_t *")
        n = ffi.new("size_t *")
        buf = ffi.new("char[64]")
        rc = L.zxv_card_authorize(h, _native.exact_int(card_id, "card_id", *_native.U64),
                                  _native.exact_int(amount, "amount", *_native.I64),
                                  _native.to_cstr(currency), resp, buf, 64, n)
        if rc == -15:
            return CardAuthorization(False, int(resp[0]), "")
        _native.check(rc, self._ctx)
        return CardAuthorization(True, int(resp[0]), ffi.string(buf).decode("utf-8"))


class VssCheck(enum.IntEnum):
    """VSS conformance checks."""

    C1_BALANCE = 1
    C2_RECEIPTS = 2
    C3_ROUNDTRIP = 3
    C4_BACKING = 4


class VssLevel(enum.IntEnum):
    """VSS conformance levels."""

    NONE = 0
    BRONZE = 1
    SILVER = 2
    GOLD = 3


class Vss:
    """VSS conformance checks over a context (pending in this build)."""

    def __init__(self, ctx: Context) -> None:
        self._ctx = ctx

    def run(self, check: VssCheck) -> "tuple[bool, str]":
        """Run one check; returns ``(passed, detail)``."""
        h = self._ctx._handle()
        L = _native.lib()
        passed = ffi.new("int32_t *")
        detail = _native.read_text(lambda b, c, n: L.zxv_vss_run(h, int(VssCheck(check)), passed, b, c, n),
                                   self._ctx)
        return bool(passed[0]), detail

    def level(self) -> VssLevel:
        """The overall conformance level."""
        out = ffi.new("int32_t *")
        _native.check(_native.lib().zxv_vss_level_get(self._ctx._handle(), out), self._ctx)
        return VssLevel(out[0])

