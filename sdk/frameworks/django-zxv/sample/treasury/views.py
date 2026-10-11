# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""JSON endpoints of the sample (exact in, exact out)."""

from __future__ import annotations

from typing import Any

from django.conf import settings
from django.http import Http404, HttpRequest, HttpResponse
from django.views.decorators.csrf import csrf_exempt
from django.views.decorators.http import require_GET, require_POST

import django_zxv
from django_zxv.exactjson import (ExactJsonError, ExactJsonResponse, minor_units_from_json, rational_from_json,
                                  read_exact_json)

from .services import treasury


def _object(request: HttpRequest, allowed: set[str]) -> dict[str, Any]:
    body = read_exact_json(request)
    if not isinstance(body, dict):
        raise ExactJsonError("body", "must be a JSON object")
    for key in body:
        if key not in allowed:
            raise ExactJsonError(str(key), "is not a known field")
    return body


@csrf_exempt
@require_POST
def quote(request: HttpRequest) -> HttpResponse:
    """``{"price":"12.75","quantity":"1/3"}`` -> ``{"total":"4.25"}``; a JSON float is a 400."""
    body = _object(request, {"price", "quantity"})
    total = rational_from_json(body.get("price"), "price") * rational_from_json(body.get("quantity"), "quantity")
    return ExactJsonResponse({"total": total})


@require_GET
def sum_view(request: HttpRequest) -> HttpResponse:
    """``?a=0.1&b=0.2`` -> ``{"sum":"0.3"}``."""
    return ExactJsonResponse({"sum": rational_from_json(request.GET.get("a"), "a") + rational_from_json(request.GET.get("b"), "b")})


def _payment(request: HttpRequest) -> tuple[int, str]:
    body = _object(request, {"amountMinor", "reference"})
    ref = body.get("reference")
    if not isinstance(ref, str) or not ref:
        raise ExactJsonError("reference", "must be a non-empty string")
    return minor_units_from_json(body.get("amountMinor"), "amountMinor"), ref


@csrf_exempt
@require_POST
def payments(request: HttpRequest) -> HttpResponse:
    """Posted with ``teller``. Ids are strings (exact in every JSON reader)."""
    amount, ref = _payment(request)
    return ExactJsonResponse({"entryId": str(treasury().pay_customer(amount, ref))}, status=201)


@csrf_exempt
@require_POST
def refunds(request: HttpRequest) -> HttpResponse:
    """Attempted with ``auditor``: 403 problem."""
    amount, ref = _payment(request)
    return ExactJsonResponse({"entryId": str(treasury().refund_as_auditor(amount, ref))}, status=201)


@require_GET
def balance(request: HttpRequest) -> HttpResponse:
    """The customer's balance in kobo, as exact strings."""
    b = treasury().customer_balance()
    return ExactJsonResponse({"debit": str(b.debit), "credit": str(b.credit), "equity": str(b.equity)})


@csrf_exempt
@require_POST
def admin_halt(request: HttpRequest) -> HttpResponse:
    """Halts the context. Only with ``SAMPLE_HALT_ENDPOINT``; never outside a local demo."""
    if not getattr(settings, "SAMPLE_HALT_ENDPOINT", False):
        raise Http404
    django_zxv.get_context().halt("operator halt from the sample")
    return HttpResponse(status=202)
