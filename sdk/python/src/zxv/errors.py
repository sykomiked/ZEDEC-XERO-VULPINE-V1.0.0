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

ABI 1.5 adds named subclasses for its statuses: :class:`DenominationError`
and :class:`TreatyError` (USAGE); :class:`CidMismatchError`,
:class:`ConsentRequiredError`, :class:`ErasureError`, :class:`PegError`,
:class:`LimitRefusedError`, :class:`ParadoxStepLimitError` and
:class:`OwnerApprovalRequiredError` (REFUSED). ABI 1.6 (Vino Embed) adds
:class:`LaneError` (USAGE) and :class:`RailError`, :class:`CoverageError`,
:class:`RequestExpiredError` and :class:`ChanceOutcomeError` (REFUSED). A
status this SDK does not know still maps by its class (R8).

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
    # ABI 1.5
    "DenominationError",
    "TreatyError",
    "CidMismatchError",
    "ConsentRequiredError",
    "ErasureError",
    "PegError",
    "LimitRefusedError",
    "ParadoxStepLimitError",
    "OwnerApprovalRequiredError",
    # ABI 1.6
    "RailError",
    "CoverageError",
    "LaneError",
    "RequestExpiredError",
    "ChanceOutcomeError",
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


class DenominationError(ZxvUsageError):
    """``ZXV_E_DENOM`` (ABI 1.5, class USAGE): the amount is not a face of the
    Fibonacci ladder (a coin rung or a token tier). Split it instead."""


class TreatyError(ZxvUsageError):
    """``ZXV_E_TREATY`` (ABI 1.5, class USAGE): a treaty failed validation.

    Attributes:
        fault: the :class:`zxv.TreatyFault` reason, when the call reports one
            (parsing and validating do; running does not).
        offset: for a parse, the byte offset of the first fault in the input.
    """

    fault: Optional[int] = None
    offset: Optional[int] = None


class CidMismatchError(ZxvRefusedError):
    """``ZXV_E_CID`` (ABI 1.5): bytes that do not match their CID, or bytes
    that are not one canonical CID. Never retry with the same bytes."""


class ConsentRequiredError(ZxvRefusedError):
    """``ZXV_E_CONSENT`` (ABI 1.5): no valid, unused consent token covers this
    Web 4 agent action. The reason is in :attr:`detail` (diagnostic only)."""


class ErasureError(ZxvRefusedError):
    """``ZXV_E_ERASURE`` (ABI 1.5): too few distinct freight rows arrived to
    rebuild the payload. Decode again when more rows arrive."""


class PegError(ZxvRefusedError):
    """``ZXV_E_PEG`` (ABI 1.5): no valid signed peg for this currency pair, or
    a peg whose epoch is not above the current one (stale or replayed)."""


class LimitRefusedError(ZxvRefusedError):
    """``ZXV_E_LIMIT`` (ABI 1.5): a limit refused the call. Not a fault.

    From a V-Bill hub swap: a limit rule refused it; it may pass in the next
    rule epoch. From :meth:`zxv.vino.SpendMeter.admit` (ABI 1.6): the amount
    would take the day's total past the daily cap; nothing was counted.

    Attributes:
        left: for a spend meter, what is still left today (an exact ``int``);
            ``None`` for every other source.
    """

    left: Optional[int] = None


class ParadoxStepLimitError(ZxvRefusedError):
    """``ZXV_E_PARADOX_STEP`` (ABI 1.5): a paradox graph reached its step limit
    unresolved. The pass was logged and the graph now goes to the owner."""


class OwnerApprovalRequiredError(ZxvRefusedError):
    """``ZXV_E_PARADOX_OWNER`` (ABI 1.5): the change touches money or devices
    and needs the owner's approval (``owner_ok``)."""


class RailError(ZxvRefusedError):
    """``ZXV_E_RAIL`` (ABI 1.6): the call named a rail that is not the play
    voucher's active rail (BEARER or GHOST), or switched a voucher to the rail
    it is already on. A voucher lives on exactly one rail at a time, so it
    can never be spent twice. Read its rail with
    :meth:`zxv.vino.VinoPlay.voucher` and retry on that rail."""


class CoverageError(ZxvRefusedError):
    """``ZXV_E_COVERAGE`` (ABI 1.6): the issue or the backing withdrawal would
    take a play-lane issuer below 1.8x coverage (``10 * backing >= 18 *
    outstanding``). Nothing changed. Post more play backing, or wait for
    vouchers to be redeemed."""


class LaneError(ZxvUsageError):
    """``ZXV_E_LANE`` (ABI 1.6, class USAGE): a lane that is not PLAY (1) or
    LIVE (2), or the wrong lane for this call. A caller bug."""


class RequestExpiredError(ZxvRefusedError):
    """``ZXV_E_EXPIRED`` (ABI 1.6): a payment request has expired, or a receipt
    says it was approved outside the request's ``[created_at, expires_at]``
    window. Build a new request."""


class ChanceOutcomeError(ZxvRefusedError):
    """``ZXV_E_CHANCE`` (ABI 1.6): a LIVE payment request marked as a paid
    chance outcome (loot boxes, paid draws). Such requests are never encoded
    or accepted. The raffle is a separate module, not a payment request."""


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
    -34: CidMismatchError,
    -35: ConsentRequiredError,
    -36: ErasureError,
    -37: PegError,
    -38: LimitRefusedError,
    -39: ParadoxStepLimitError,
    -40: OwnerApprovalRequiredError,
    -46: RailError,
    -47: CoverageError,
    -49: RequestExpiredError,
    -50: ChanceOutcomeError,
}

_USAGE_BY_STATUS = {
    -24: FloatRejectedError,
    -32: DenominationError,
    -33: TreatyError,
    -48: LaneError,
}


def error_type_for(status: int, status_class: int) -> type:
    """Return the exception type for a status and its class (internal helper)."""
    if status_class == StatusClass.USAGE:
        return _USAGE_BY_STATUS.get(status, ZxvUsageError)
    if status_class == StatusClass.REFUSED:
        return _REFUSED_BY_STATUS.get(status, ZxvRefusedError)
    if status_class == StatusClass.FATAL:
        return ZxvFatal
    return ZxvSystemError
