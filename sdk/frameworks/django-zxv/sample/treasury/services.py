# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""A bank account and one customer account in NGN, in the libzxv ledger."""

from __future__ import annotations

import threading
from typing import Optional

import zxv

import django_zxv


class Treasury:
    """Posts with ``teller``; reads (and tries to refund) with ``auditor``."""

    def __init__(self) -> None:
        self._teller = zxv.Ledger(django_zxv.get_capability("teller"))
        self._auditor = zxv.Ledger(django_zxv.get_capability("auditor"))
        self.bank = self._teller.open_account("bank", "NGN", zxv.AccountKind.ISSUER)
        self.customer = self._teller.open_account("ada", "NGN", zxv.AccountKind.HOLDER)
        self.context = django_zxv.get_context()

    def pay_customer(self, amount_minor: int, reference: str) -> int:
        """Pays the customer from the bank; returns the journal entry id."""
        return self._teller.post(self.bank, self.customer, amount_minor, "NGN", reference)

    def refund_as_auditor(self, amount_minor: int, reference: str) -> int:
        """Always refused: the auditor may only read (CapabilityDeniedError, 403)."""
        return self._auditor.post(self.customer, self.bank, amount_minor, "NGN", reference)

    def customer_balance(self) -> zxv.Balance:
        """Read with the auditor capability."""
        return self._auditor.balance(self.customer)


_treasury: Optional[Treasury] = None
_lock = threading.Lock()


def treasury() -> Treasury:
    """The treasury of the current ZXV context (made again if the context was replaced)."""
    global _treasury
    with _lock:
        if _treasury is None or _treasury.context is not django_zxv.get_context():
            _treasury = Treasury()
        return _treasury
