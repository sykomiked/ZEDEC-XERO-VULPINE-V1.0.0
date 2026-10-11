# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Exceptions raised by the ZXV SDK.

Every negative status from ``libzxv`` is mapped through ``zxv_status_class``:

========  =================================  ==================================
Class     Python type                        Meaning
========  =================================  ==================================
USAGE     :class:`ZxvUsageError`             a caller bug (also a ``ValueError``)
REFUSED   :class:`ZxvRefusedError` family    the platform declined; handle it
FATAL     :class:`ZxvFatal`                  context halted; never retry
SYSTEM    :class:`ZxvSystemError`            environment (memory, I/O)
========  =================================  ==================================

:class:`ZxvFatal` derives from :class:`BaseException`, not :class:`Exception`,
so an ``except Exception:`` block cannot swallow it by accident. Set
:attr:`FatalPolicy.ABORT` to terminate the process instead of raising.
"""

from __future__ import annotations

import enum
from typing import Optional

__all__ = [
    "StatusClass",
    "FatalPolicy",
    "ZxvError",
    "ZxvUsageError",
    "FloatRejectedError",
    "ZxvRefusedError",
    "CapabilityDeniedError",
    "BudgetExhaustedError",
    "InsufficientFundsError",
    "VerificationFailedError",
    "ZxvNotImplementedError",
    "ZxvSystemError",
    "ZxvLibraryError",
    "ZxvFatal",
]


class StatusClass(enum.IntEnum):
    """The class of a status code, as ``zxv_status_class`` reports it."""

    OK = 0
    """Success."""
    USAGE = 1
    """A caller bug: retrying the same call fails again."""
    REFUSED = 2
    """A well-formed request the platform declined; a retry may succeed later."""
    FATAL = 3
    """The context halted (invariant broken or halted on purpose). Never retry."""
    SYSTEM = 4
    """Out of memory, I/O, or an internal error."""


class FatalPolicy(enum.Enum):
    """What the SDK does when a call reports a FATAL status.

    ``RAISE`` (the default) marks the context dead and raises :class:`ZxvFatal`.
    ``ABORT`` calls :func:`os.abort` immediately: the only form nothing in the
    process can catch.
    """

    RAISE = "raise"
    """Raise :class:`ZxvFatal` (a ``BaseException``)."""
    ABORT = "abort"
    """Terminate the process with ``os.abort()`` (SIGABRT)."""


class _StatusMixin:
    """Fields shared by every status-carrying error."""

    status: int
    status_name: str
    detail: str

    def _init_status(self, status: int, status_name: str, detail: str) -> str:
        self.status = status
        self.status_name = status_name
        self.detail = detail
        return f"{status_name} ({status}): {detail}" if detail else f"{status_name} ({status})"


class ZxvError(_StatusMixin, Exception):
    """Base class of every ordinary (non-fatal) SDK error.

    Attributes:
        status: the negative ``ZXV_E_*`` code.
        status_name: its symbolic name, e.g. ``"ZXV_E_FUNDS"``.
        detail: the diagnostic text from ``zxv_last_error`` (never branch on it).
    """

    def __init__(self, status: int, status_name: str, detail: str = "") -> None:
        super().__init__(self._init_status(status, status_name, detail))


class ZxvUsageError(ZxvError, ValueError):
    """A caller bug (status class USAGE): fix the arguments, do not retry."""


class FloatRejectedError(ZxvUsageError, TypeError):
    """A Python ``float`` was passed where only exact values are accepted.

    ZXV never converts floats silently. Use :meth:`zxv.Rational.from_float_exact`
    to convert a float to the exact rational its bits denote, or pass an
    ``int``, :class:`fractions.Fraction`, :class:`decimal.Decimal` or text.
    """

    def __init__(self, what: str = "value", detail: Optional[str] = None) -> None:
        super().__init__(
            -24,
            "ZXV_E_FLOAT",
            detail
            or (
                f"float rejected for {what}: ZXV takes exact values only. "
                "Use Rational.from_float_exact(x) to convert a float explicitly, "
                "or pass an int, Fraction, Decimal or text such as '0.1'."
            ),
        )


class ZxvRefusedError(ZxvError):
    """The platform declined a well-formed request (status class REFUSED)."""


class CapabilityDeniedError(ZxvRefusedError):
    """The capability lacks the needed bit, is revoked, or is not attested TRUE."""


class BudgetExhaustedError(ZxvRefusedError):
    """An rmag or swarm budget is exhausted. Retry after it is replenished."""


class InsufficientFundsError(ZxvRefusedError):
    """A holder account would go below zero."""


class VerificationFailedError(ZxvRefusedError):
    """A signature or tag did not verify."""


class ZxvNotImplementedError(ZxvRefusedError, NotImplementedError):
    """The feature is declared in the ABI but not built into this library."""


class ZxvSystemError(ZxvError):
    """An environmental failure: memory, I/O, or an internal kernel status."""


class ZxvLibraryError(ImportError):
    """``libzxv`` could not be found or loaded, or its ABI version is unsupported."""


class ZxvFatal(_StatusMixin, BaseException):
    """The context halted (status class FATAL). Terminal: never retry.

    The C library poisons the context, so every later call on it fails with
    ``ZXV_E_POISONED`` whatever the host does; the SDK also marks its own
    handle dead. This derives from :class:`BaseException` so that
    ``except Exception`` does not catch it. Catch it by name only at the very
    top of a program, to log and shut down.
    """

    def __init__(self, status: int, status_name: str, detail: str = "") -> None:
        super().__init__(self._init_status(status, status_name, detail))


_REFUSED_BY_STATUS = {
    -3: ZxvNotImplementedError,
    -6: InsufficientFundsError,
    -17: CapabilityDeniedError,
    -19: BudgetExhaustedError,
    -21: VerificationFailedError,
}


def error_type_for(status: int, status_class: int) -> type:
    """Return the exception type for a status and its class (internal helper)."""
    if status_class == StatusClass.USAGE:
        return FloatRejectedError if status == -24 else ZxvUsageError
    if status_class == StatusClass.REFUSED:
        return _REFUSED_BY_STATUS.get(status, ZxvRefusedError)
    if status_class == StatusClass.FATAL:
        return ZxvFatal
    return ZxvSystemError
