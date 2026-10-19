# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""RFC 9457 problem details for ZXV errors, by status class, with no native text."""

from __future__ import annotations

import logging
import re
from typing import Any, Callable, Literal, Union

from django.http import HttpRequest, HttpResponse, JsonResponse
from zxv import ZxvError, ZxvFatal, ZxvRefusedError, ZxvUsageError

from .exactjson import ExactJsonError

ProblemClass = Literal["usage", "refused", "fatal", "system"]
ZxvException = Union[ZxvError, ZxvFatal]

log = logging.getLogger("django_zxv")

_TITLES: dict[ProblemClass, str] = {
    "usage": "Invalid request",
    "refused": "Operation refused",
    "fatal": "Service halted",
    "system": "Internal error",
}

_BY_STATUS = {
    -4: 404,    # ZXV_E_NOT_FOUND
    -13: 409,   # ZXV_E_DUPLICATE
    -17: 403,   # ZXV_E_CAP
    -6: 422,    # ZXV_E_FUNDS
    -7: 422,    # ZXV_E_CURRENCY
    -15: 422,   # ZXV_E_DECLINED
    -21: 422,   # ZXV_E_VERIFY
    -19: 429,   # ZXV_E_BUDGET
    -3: 501,    # ZXV_E_NOTIMPL
    -5: 503,    # ZXV_E_CAPACITY
    -14: 503,   # ZXV_E_NOMEM
}

_BY_CLASS: dict[ProblemClass, int] = {"usage": 400, "refused": 409, "fatal": 503, "system": 500}


def problem_class(e: ZxvException) -> ProblemClass:
    """The status class of a ZXV error (the SDK picked its type by ``zxv_status_class``)."""
    if isinstance(e, ZxvFatal):
        return "fatal"
    if isinstance(e, ZxvUsageError):
        return "usage"
    if isinstance(e, ZxvRefusedError):
        return "refused"
    return "system"


def http_status_for(e: ZxvException) -> int:
    """The one mapping from a ZXV status to HTTP: specific codes first, then the class."""
    return _BY_STATUS.get(e.status, _BY_CLASS[problem_class(e)])


def problem_for(e: ZxvException) -> dict[str, Any]:
    """``type``, a generic ``title``, ``status``, ``code`` and ``class``. Never the detail text."""
    cls = problem_class(e)
    code = e.status_name if re.fullmatch(r"ZXV_[A-Z0-9_]+", e.status_name or "") else "ZXV_E_INTERNAL"
    return {
        "type": f"urn:zxv:problem:{code.lower()}",
        "title": _TITLES[cls],
        "status": http_status_for(e),
        "code": code,
        "class": cls,
    }


def _problem_response(body: dict[str, Any]) -> JsonResponse:
    res = JsonResponse(body, status=int(body["status"]), content_type="application/problem+json")
    res["Cache-Control"] = "no-store"
    return res


def problem_response(e: ZxvException) -> JsonResponse:
    """The problem response for a ZXV error. Logs the detail server-side only."""
    body = problem_for(e)
    line = "%s (%s) -> %s: %s"
    args = (body["code"], body["class"], body["status"], e.detail)
    if body["class"] == "fatal":
        log.critical(line, *args)
    elif body["class"] == "system":
        log.error(line, *args)
    else:
        log.info(line, *args)
    return _problem_response(body)


def exact_json_problem(e: ExactJsonError) -> JsonResponse:
    """400 (413 for a large body, 415 for a wrong type) for an :class:`ExactJsonError`; the message is ours."""
    status = {"body_too_large": 413, "json_type": 415}.get(e.code, 400)
    titles = {"json_float": "Inexact number", "json_syntax": "Malformed JSON", "body_too_large": "Payload too large",
              "json_type": "Unsupported media type"}
    return _problem_response({
        "type": f"urn:zxv:problem:{e.code}",
        "title": titles.get(e.code, "Invalid request"),
        "status": status,
        "code": e.code.upper(),
        "detail": str(e),
    })


class ZxvProblemMiddleware:
    """Answers ZXV errors as problem details, and a halted context with 503.

    :class:`zxv.ZxvFatal` derives from ``BaseException``, so Django's own
    handler (which catches ``Exception``) lets it through; this middleware
    catches it by name, answers 503 and never a normal response. Put it
    first in ``MIDDLEWARE``. Other exceptions pass through untouched.
    """

    def __init__(self, get_response: Callable[[HttpRequest], HttpResponse]) -> None:
        self.get_response = get_response

    def __call__(self, request: HttpRequest) -> HttpResponse:
        try:
            return self.get_response(request)
        except ZxvFatal as e:
            return problem_response(e)

    def process_exception(self, request: HttpRequest, exception: Exception) -> HttpResponse | None:
        if isinstance(exception, ZxvError):
            return problem_response(exception)
        if isinstance(exception, ExactJsonError):
            return exact_json_problem(exception)
        return None
