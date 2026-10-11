# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The health view: fail-closed."""

from __future__ import annotations

from typing import Any

from django.http import HttpRequest, JsonResponse
from django.views.decorators.cache import never_cache
from django.views.decorators.http import require_safe
from zxv import ZxvFatal, abi_version

from . import runtime
from .conf import right_names


def _capabilities(rt: runtime.ZxvRuntime) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for name in rt.names:
        try:
            info = rt.capability(name).info
            out[name] = {"rights": right_names(info.caps), "attestation": info.attestation.name, "revoked": info.revoked}
        except (Exception, ZxvFatal):
            out[name] = "unavailable"
    return out


@require_safe
@never_cache
def health(request: HttpRequest) -> JsonResponse:
    """200 ``{"status": "up", ...}`` only when libzxv answers that the context is live;
    503 ``{"status": "down", "state": "halted" | "closed" | "unknown"}`` otherwise,
    including when anything at all goes wrong while asking. No keys, ledger data or halt reason.
    """
    try:
        rt = runtime.current()
        state = rt.state()
        if state == "live":
            major, minor, patch = abi_version()
            return JsonResponse({"status": "up", "state": state, "abi": f"{major}.{minor}.{patch}",
                                 "capabilities": _capabilities(rt)})
    except (Exception, ZxvFatal):
        state = "unknown"
    return JsonResponse({"status": "down", "state": state}, status=503)
