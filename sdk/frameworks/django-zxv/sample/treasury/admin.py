# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
from django.contrib import admin

from django_zxv.admin import LedgerEquationAdmin

from .models import LedgerLine


@admin.register(LedgerLine)
class LedgerLineAdmin(LedgerEquationAdmin[LedgerLine]):
    """Ledger lines, with the read-only "Accounting equation" page."""

    list_display = ("account", "kind", "amount", "currency")
    list_filter = ("kind", "currency")
    equation_kind_field = "kind"
    equation_amount_field = "amount"
    equation_currency_field = "currency"
