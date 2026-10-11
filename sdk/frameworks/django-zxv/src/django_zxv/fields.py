# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""``RationalField``: an exact rational in one database column.

Storage: the canonical text ``"<numerator>/<denominator>"`` (reduced,
denominator > 0, always with the slash: ``12.75`` is stored ``"51/4"``,
``3`` as ``"3/1"``) in a ``varchar(41)``, the longest int64 pair.

Why text and not two integer columns or a decimal:

- A decimal column cannot hold ``1/3`` and rounds on some databases; a float
  column is out of the question.
- Two integer columns would make one model field own two database columns,
  which Django has no public API for (forms, ``values()``, migrations,
  serialization and the admin all assume one column per field).
- Because the text is canonical, equality, ``in``, ``unique`` and indexes are
  exact: equal values always have equal text, on every database backend.

What text cannot do: order or add in SQL. ``<``, ``>``, ``range`` and other
lookups are refused (``FieldError``) instead of silently comparing strings;
sort and sum in Python with :class:`zxv.Rational` (or ``Fraction``). Do not
``order_by`` a RationalField: Django cannot stop it, and the order would be
lexicographic.
"""

from __future__ import annotations

import re
from decimal import Decimal
from fractions import Fraction
from typing import TYPE_CHECKING, Any, Optional, Union

from django import forms
from django.core.exceptions import ValidationError
from django.db import models
from django.db.models import Lookup, Transform
from zxv import Rational, ZxvError

RationalInput = Union[Rational, int, str, Fraction, Decimal]

_CANONICAL = re.compile(r"^(-?[0-9]{1,19})/([0-9]{1,19})$")
_MAX_LENGTH = 41  # len("-9223372036854775807/9223372036854775807")
_ALLOWED_LOOKUPS = frozenset({"exact", "in", "isnull"})


def to_canonical(value: Rational) -> str:
    """``"n/d"``: reduced, denominator > 0, always with the slash."""
    return f"{value.numerator}/{value.denominator}"


def from_canonical(text: str) -> Rational:
    """Reads a stored value. Raises ``ValueError`` for text that is not canonical."""
    m = _CANONICAL.match(text)
    if m is None:
        raise ValueError(f"{text!r} is not a canonical rational (n/d)")
    r = Rational(int(m.group(1)), int(m.group(2)))
    if to_canonical(r) != text:
        raise ValueError(f"{text!r} is not reduced")
    return r


def coerce_exact(value: Any) -> Rational:
    """An exact value as :class:`zxv.Rational`; a ``float`` or ``bool`` is refused.

    Raises:
        ValidationError: code ``float`` for a float, ``invalid`` for anything inexact or malformed.
    """
    if isinstance(value, float):
        raise ValidationError(
            "A float is not exact; pass a string such as '0.1' or '1/3', an int, a Decimal or a Fraction.",
            code="float")
    if isinstance(value, bool):
        raise ValidationError("A bool is not a number.", code="invalid")
    try:
        if isinstance(value, str):
            m = _CANONICAL.match(value)
            return Rational(int(m.group(1)), int(m.group(2))) if m else Rational.parse(value)
        return Rational.coerce(value)
    except (ZxvError, TypeError, ValueError):
        raise ValidationError(
            "Enter an exact number such as 12.75, -0.1, 3/4 or 42 (no exponent, no spaces; 64-bit parts).",
            code="invalid") from None


class RationalFormField(forms.CharField):
    """A text input for an exact rational: ``12.75``, ``-0.1``, ``1/3``.

    Cleans to :class:`zxv.Rational`. A float (from ``initial`` or code) is
    refused instead of being turned into text.
    """

    def __init__(self, **kwargs: Any) -> None:
        # No max_length: CharField's MaxLengthValidator would call len() on the
        # cleaned Rational. coerce_exact already bounds the parts to 64 bits.
        kwargs.pop("max_length", None)
        kwargs.pop("min_length", None)
        super().__init__(**kwargs)
        self.widget.attrs.setdefault("inputmode", "decimal")
        self.widget.attrs.setdefault("maxlength", "64")

    def to_python(self, value: Any) -> Optional[Rational]:  # type: ignore[override]
        if isinstance(value, float):
            raise ValidationError(
                "A float is not exact; enter the number as text, e.g. 0.1 or 1/3.", code="float")
        if isinstance(value, Rational):
            return value
        text = super().to_python(value)
        if text in self.empty_values:
            return None
        return coerce_exact(text)

    def prepare_value(self, value: Any) -> Any:
        return str(value) if isinstance(value, Rational) else value


if TYPE_CHECKING:
    _FieldBase = models.Field[Optional[RationalInput], Optional[Rational]]
else:  # Django's Field is generic only in django-stubs
    _FieldBase = models.Field


class RationalField(_FieldBase):
    """An exact rational model field (see the module docs for the storage choice).

    Accepts :class:`zxv.Rational`, ``int``, ``Fraction``, ``Decimal`` and exact
    text; refuses ``float`` with ``ValidationError`` (code ``float``).
    """

    description = "An exact rational number (stored as canonical 'numerator/denominator' text)"

    def __init__(self, *args: Any, **kwargs: Any) -> None:
        kwargs["max_length"] = _MAX_LENGTH
        super().__init__(*args, **kwargs)

    def deconstruct(self) -> Any:
        name, path, args, kwargs = super().deconstruct()
        kwargs.pop("max_length", None)
        return name, path, args, kwargs

    def get_internal_type(self) -> str:
        return "CharField"

    def from_db_value(self, value: Optional[str], expression: Any, connection: Any) -> Optional[Rational]:
        return None if value is None else from_canonical(value)

    def to_python(self, value: Any) -> Optional[Rational]:
        if value is None:
            return None
        if isinstance(value, Rational):
            return value
        return coerce_exact(value)

    def get_prep_value(self, value: Any) -> Optional[str]:
        value = super().get_prep_value(value)
        r = self.to_python(value)
        return None if r is None else to_canonical(r)

    def value_to_string(self, obj: models.Model) -> str:
        v = self.value_from_object(obj)
        return "" if v is None else to_canonical(self.to_python(v) or Rational(0))

    def formfield(self, **kwargs: Any) -> Any:
        defaults: dict[str, Any] = {"form_class": RationalFormField}
        defaults.update(kwargs)
        return super().formfield(**defaults)

    def get_lookup(self, lookup_name: str) -> Optional[type[Lookup[Any]]]:
        if lookup_name not in _ALLOWED_LOOKUPS:
            return None  # Django raises FieldError: ordering text would be wrong
        return super().get_lookup(lookup_name)

    def get_transform(self, lookup_name: str) -> Optional[type[Transform]]:
        return None
