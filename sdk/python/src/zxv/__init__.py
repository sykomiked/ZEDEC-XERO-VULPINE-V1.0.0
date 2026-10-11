# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""ZXV for Python: exact money, four-valued logic, budgets, post-quantum
crypto and a deterministic integer inference engine, over ``libzxv``.

Quick start::

    import zxv

    with zxv.Context() as ctx:
        cap = ctx.root_capability()
        ctx.currencies.enable("NGN")
        ledger = zxv.Ledger(cap)
        bank = ledger.open_account("bank", "NGN", zxv.AccountKind.ISSUER)
        alice = ledger.open_account("alice", "NGN")
        ledger.post(bank, alice, 50_000, "NGN", "E2E-1")
        print(ledger.balance(alice).equity)            # 50000 (kobo)

See the README for the concepts and an API overview.
"""

from __future__ import annotations

import enum
from typing import Optional, Tuple

from . import _native
from ._native import load_library, library_path, set_default_fatal_policy, default_fatal_policy
from .attest import Attestation, Attestations, Subject
from .budget import Budget, BudgetEvent, Budgets, BudgetStatus, Resource
from .context import IDENTITY_BYTES, Cap, Capability, CapabilityInfo, Context
from .crypto import (Ed25519, Encapsulation, Hashes, KeyPair, MlDsa87, MlKem1024, SecretKey,
                     random_bytes)
from .engine import (AsyncTokenStream, GenerateOptions, GenerationResult, KvType, Model, ModelArch,
                     Session, StopReason, Token, TokenStream)
from .errors import (BudgetExhaustedError, CapabilityDeniedError, FatalPolicy, FloatRejectedError,
                     InsufficientFundsError, StatusClass, VerificationFailedError, ZxvError, ZxvFatal,
                     ZxvLibraryError, ZxvNotImplementedError, ZxvRefusedError, ZxvSystemError,
                     ZxvUsageError)
from .evm import EvmState, StateChannel
from .hexagram import (QUARANTINE_MAX_RECORDS, HexAction, HexBase, Hexagram, HexPhase, HexPolarity,
                       QuarantineLog, QuarantineRecord)
from .ledger import (AMOUNT_MAX, AccountKind, Balance, Currencies, CurrencyFlag, EnabledCurrency,
                     Iso4217Currency, JournalEntry, Ledger, Rail, iso4217)
from .messages import (CardAuthorization, CardNetwork, Cards, CdtDbt, EntryStatus, MxAmount, MxField,
                       MxKind, MxMessage, NetPosition, NettingCycle, Vss, VssCheck, VssLevel)
from .phase import (PHASE_CYCLE, Admission, Decision, DeferredEvent, EventState, Health, PhaseClock,
                    RealtimeDriver, StepFlag)
from .rational import Rational, RationalLike
from .swarm import Allotment, EmotionProfile, Grant, Swarm
from .truth import Truth, UncertainTruthError
from .wire import FRAME_BYTES, Ubh168Frame, WireTruth, tag_of

__version__ = "1.3.0"
"""Version of this Python SDK (tracks the C ABI it wraps)."""


class Feature(enum.IntFlag):
    """Feature groups a ``libzxv`` build may provide (``ZXV_FEAT_*``)."""

    NONE = 0
    LEDGER = 0x00000001
    MX_PACS008 = 0x00000002
    MX_CAMT053 = 0x00000004
    ISO4217 = 0x00000008
    MX_PACS002 = 0x00000010
    MX_PACS004 = 0x00000020
    MX_PACS009 = 0x00000040
    MX_CAMT056 = 0x00000080
    MX_CAMT029 = 0x00000100
    NETTING = 0x00000200
    CARDS = 0x00000400
    CARD_CHECK = 0x00000800
    VSS = 0x00001000
    RATIONAL = 0x00002000
    LPRES = 0x00004000
    CAPS = 0x00008000
    RMAG = 0x00010000
    PHASE = 0x00020000
    WIRE = 0x00040000
    SWARM = 0x00080000
    HASH = 0x00100000
    MLKEM1024 = 0x00200000
    MLDSA87 = 0x00400000
    ED25519 = 0x00800000
    TENSOR = 0x01000000
    LORA = 0x02000000
    """LoRA adapters: not built yet in any ABI 1.1 library."""
    EVM = 0x04000000
    HEX = 0x08000000
    """The 64-state hexagram tag and quarantine log (ABI 1.3)."""


def abi_version() -> Tuple[int, int, int]:
    """(major, minor, patch) ABI version of the loaded library."""
    v = _native.ffi.new("uint32_t[3]")
    _native.check(_native.lib().zxv_version(v, v + 1, v + 2))
    return int(v[0]), int(v[1]), int(v[2])


def features() -> Feature:
    """The :class:`Feature` groups live in the loaded library."""
    out = _native.ffi.new("uint32_t *")
    _native.check(_native.lib().zxv_features(out))
    return Feature(out[0])


def has_feature(feature: Feature) -> bool:
    """True if every bit of ``feature`` is live in this build."""
    return (features() & feature) == feature


def status_name(status: int) -> str:
    """Symbolic name of a status code (``"ZXV_E_FUNDS"``)."""
    return _native.status_name(status)


def status_class(status: int) -> StatusClass:
    """The :class:`StatusClass` of a status code."""
    return _native.status_class(status)


def last_error() -> Optional[str]:
    """This thread's last C error detail (diagnostic only), or ``None`` if empty."""
    return _native.last_error() or None


__all__ = [
    # library
    "__version__", "load_library", "library_path", "abi_version", "features", "has_feature", "Feature",
    "status_name", "status_class", "last_error", "set_default_fatal_policy", "default_fatal_policy",
    # errors
    "StatusClass", "FatalPolicy", "ZxvError", "ZxvUsageError", "FloatRejectedError", "ZxvRefusedError",
    "CapabilityDeniedError", "BudgetExhaustedError", "InsufficientFundsError", "VerificationFailedError",
    "ZxvNotImplementedError", "ZxvSystemError", "ZxvLibraryError", "ZxvFatal",
    # values
    "Rational", "RationalLike", "Truth", "UncertainTruthError",
    # hexagram tags
    "Hexagram", "HexBase", "HexPolarity", "HexPhase", "HexAction", "QuarantineLog", "QuarantineRecord",
    "QUARANTINE_MAX_RECORDS",
    # context
    "Context", "Capability", "CapabilityInfo", "Cap", "IDENTITY_BYTES",
    # ledger
    "Ledger", "AccountKind", "Balance", "JournalEntry", "Rail", "Currencies", "CurrencyFlag",
    "EnabledCurrency", "Iso4217Currency", "iso4217", "AMOUNT_MAX",
    # payments
    "MxMessage", "MxKind", "MxField", "MxAmount", "CdtDbt", "EntryStatus", "NettingCycle",
    "NetPosition", "Cards", "CardNetwork", "CardAuthorization", "Vss", "VssCheck", "VssLevel",
    # attestations, budgets, phase
    "Attestations", "Attestation", "Subject", "Budgets", "Budget", "BudgetStatus", "BudgetEvent",
    "Resource", "PhaseClock", "DeferredEvent", "RealtimeDriver", "Admission", "Decision", "StepFlag",
    "Health", "EventState", "PHASE_CYCLE",
    # wire, swarm
    "Ubh168Frame", "WireTruth", "tag_of", "FRAME_BYTES", "Swarm", "Allotment", "Grant", "EmotionProfile",
    # crypto
    "Hashes", "random_bytes", "SecretKey", "KeyPair", "Ed25519", "MlDsa87", "MlKem1024", "Encapsulation",
    # engine
    "Model", "Session", "GenerateOptions", "GenerationResult", "Token", "TokenStream", "AsyncTokenStream",
    "KvType", "StopReason", "ModelArch",
    # evm
    "StateChannel", "EvmState",
]
