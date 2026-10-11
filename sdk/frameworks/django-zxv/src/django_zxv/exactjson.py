# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Exact JSON without DRF: parse refusing floats, write Rationals as text.

Python's ``json`` hands every number with a fraction or exponent to
``parse_float`` as its source text, before any conversion; :func:`loads_exact`
refuses it there, so ``0.1``, ``1.0`` and ``1e3`` never become binary floats.
JSON integers are Python ints (exact at any size).
"""

from __future__ import annotations

import json
from decimal import Decimal
from fractions import Fraction
from typing import Any, NoReturn, Union

from django.http import HttpRequest, HttpResponse
from zxv import FloatRejectedError, Rational, ZxvError

INT64_MAX = (1 << 63) - 1


class ExactJsonError(ValueError):
    """A request body or field that is not exact JSON. ``field`` names where; the text is ours."""

    def __init__(self, field: str, message: str, *, code: str = "invalid") -> None:
        super().__init__(f"{field}: {message}")
        self.field = field
        self.code = code


def _refuse_float(token: str) -> NoReturn:
    raise ExactJsonError(
        "body", f"the JSON number {token[:40]} has a fraction or exponent and is not exact; "
        "send it as a string such as \"0.1\" or \"1/3\"", code="json_float")


def _refuse_constant(token: str) -> NoReturn:
    raise ExactJsonError("body", f"{token} is not a JSON number", code="json_float")


def loads_exact(text: Union[str, bytes, bytearray]) -> Any:
    """``json.loads`` that refuses every JSON float (and NaN/Infinity).

    Raises:
        ExactJsonError: code ``json_float`` for a float, ``json_syntax`` for malformed JSON.
    """
    try:
        return json.loads(text, parse_float=_refuse_float, parse_constant=_refuse_constant)
    except ExactJsonError:
        raise
    except (ValueError, UnicodeDecodeError):
        raise ExactJsonError("body", "is not valid JSON", code="json_syntax") from None


def read_exact_json(request: HttpRequest, *, limit: int = 100 * 1024) -> Any:
    """The request body as exact JSON. Requires ``application/json`` (or ``+json``).

    Raises:
        ExactJsonError: wrong content type (``json_type``), too large (``body_too_large``),
            a float (``json_float``) or malformed JSON (``json_syntax``).
    """
    ctype = (request.content_type or "").lower()
    if ctype != "application/json" and not ctype.endswith("+json"):
        raise ExactJsonError("body", "must be application/json", code="json_type")
    body = request.body
    if len(body) > limit:
        raise ExactJsonError("body", f"exceeds {limit} bytes", code="body_too_large")
    return loads_exact(body)


def rational_from_json(value: Any, field: str = "value") -> Rational:
    """An exact rational from a parsed JSON value: a string in the SDK's exact syntax
    (``"12.75"``, ``"-0.1"``, ``"3/4"``, ``"42"``) or an int. Anything else is refused.

    Raises:
        ExactJsonError: missing, a float, malformed or out of range.
    """
    if value is None:
        raise ExactJsonError(field, "is required", code="required")
    if isinstance(value, float):
        raise ExactJsonError(field, "a float is not exact; send it as a string", code="json_float")
    if isinstance(value, bool) or not isinstance(value, (int, str, Rational)):
        raise ExactJsonError(field, "must be an exact number as a string, e.g. \"12.75\" or \"1/3\"")
    try:
        return value if isinstance(value, Rational) else Rational(value) if isinstance(value, int) else Rational.parse(value)
    except (ZxvError, ValueError, TypeError):
        raise ExactJsonError(
            field, "is not an exact number (use 12.75, -0.1, 3/4 or 42; no exponent, no spaces; 64-bit parts)") from None


def minor_units_from_json(value: Any, field: str = "value") -> int:
    """Whole minor units (kobo, cents) from an int or a string of digits, within int64.

    Raises:
        ExactJsonError: missing, a float or fraction, or out of range.
    """
    if value is None:
        raise ExactJsonError(field, "is required", code="required")
    if isinstance(value, float):
        raise ExactJsonError(field, "must be whole minor units, not a float", code="json_float")
    if isinstance(value, str) and value.lstrip("+-").isdigit() and value.isascii():
        value = int(value)
    if isinstance(value, bool) or not isinstance(value, int):
        raise ExactJsonError(field, "must be whole minor units: an integer, or a string of digits")
    if not -INT64_MAX <= value <= INT64_MAX:
        raise ExactJsonError(field, "does not fit 64 bits")
    return value


def to_exact_jsonable(obj: Any) -> Any:
    """Prepares ``obj`` for ``json.dumps``: Rational, Fraction and Decimal become exact text
    (``"12.75"``, ``"1/3"``); a float anywhere raises :class:`zxv.FloatRejectedError`."""
    if isinstance(obj, float):
        raise FloatRejectedError("JSON output", "a float in a response is not exact; use zxv.Rational")
    if isinstance(obj, Rational):
        return str(obj)
    if isinstance(obj, Fraction):
        return str(Rational.from_fraction(obj))
    if isinstance(obj, Decimal):
        return str(Rational.from_decimal(obj))
    if isinstance(obj, dict):
        return {str(k): to_exact_jsonable(v) for k, v in obj.items()}
    if isinstance(obj, (list, tuple)):
        return [to_exact_jsonable(v) for v in obj]
    return obj


def dumps_exact(obj: Any, **kwargs: Any) -> str:
    """``json.dumps`` after :func:`to_exact_jsonable`."""
    return json.dumps(to_exact_jsonable(obj), **kwargs)


class ExactJsonResponse(HttpResponse):
    """A JSON response written with :func:`dumps_exact` (floats refused, Rationals as text)."""

    def __init__(self, data: Any, *, status: int = 200, **kwargs: Any) -> None:
        # Always JSON, never sniffed as HTML: the body can echo exact values
        # parsed from the request.
        super().__init__(content=dumps_exact(data), status=status, content_type="application/json", **kwargs)
        self["X-Content-Type-Options"] = "nosniff"
