# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Ledger lines with exact amounts, checked against A = L + E in the admin."""

from __future__ import annotations

from django.db import models

from django_zxv.fields import RationalField


class LedgerLine(models.Model):
    """One balance-sheet line. ``amount`` is exact (``"1/3"`` stays one third)."""

    class Kind(models.TextChoices):
        ASSET = "asset", "Asset"
        LIABILITY = "liability", "Liability"
        EQUITY = "equity", "Equity"

    account = models.CharField(max_length=63)
    kind = models.CharField(max_length=16, choices=Kind.choices)
    currency = models.CharField(max_length=8, default="NGN")
    amount = RationalField(help_text="Exact: 12.75, -0.1 or 1/3.")

    def __str__(self) -> str:
        return f"{self.account} {self.kind} {self.amount} {self.currency}"
