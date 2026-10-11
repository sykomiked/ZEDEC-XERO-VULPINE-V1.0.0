# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
from django.apps import AppConfig


class TreasuryConfig(AppConfig):
    """The sample app: exact quotes, a libzxv ledger, and ledger lines checked in the admin."""

    name = "treasury"
    default_auto_field = "django.db.models.BigAutoField"
