# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The sample's URLs plus views that raise each kind of ZXV error."""

from __future__ import annotations

from django.http import HttpRequest, HttpResponse
from django.urls import URLPattern, URLResolver, include, path
from zxv import (BudgetExhaustedError, CapabilityDeniedError, InsufficientFundsError, VerificationFailedError, ZxvError,
                 ZxvFatal, ZxvNotImplementedError, ZxvRefusedError, ZxvSystemError, ZxvUsageError)

SECRET = "acct-4471-ADA-OKAFOR"

ERRORS: dict[str, tuple[type[ZxvError] | type[ZxvFatal], int, str]] = {
    "arg": (ZxvUsageError, -1, "ZXV_E_ARG"),
    "notfound": (ZxvUsageError, -4, "ZXV_E_NOT_FOUND"),
    "duplicate": (ZxvUsageError, -13, "ZXV_E_DUPLICATE"),
    "cap": (CapabilityDeniedError, -17, "ZXV_E_CAP"),
    "funds": (InsufficientFundsError, -6, "ZXV_E_FUNDS"),
    "verify": (VerificationFailedError, -21, "ZXV_E_VERIFY"),
    "budget": (BudgetExhaustedError, -19, "ZXV_E_BUDGET"),
    "notimpl": (ZxvNotImplementedError, -3, "ZXV_E_NOTIMPL"),
    "capacity": (ZxvRefusedError, -5, "ZXV_E_CAPACITY"),
    "state": (ZxvRefusedError, -9, "ZXV_E_STATE"),
    "io": (ZxvSystemError, -23, "ZXV_E_IO"),
    "unknown": (ZxvSystemError, -77, "UNKNOWN(-77)"),
    "fatal": (ZxvFatal, -18, "ZXV_E_POISONED"),
}


def raise_error(request: HttpRequest, kind: str) -> HttpResponse:
    if kind not in ERRORS:
        raise RuntimeError("not a ZXV error")
    cls, status, name = ERRORS[kind]
    raise cls(status, name, SECRET)


urlpatterns: list[URLPattern | URLResolver] = [
    path("raise/<str:kind>", raise_error),
    path("", include("treasury_site.urls")),
]
