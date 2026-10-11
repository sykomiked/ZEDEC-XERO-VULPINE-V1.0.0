# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The attestation registry (kernel ``lpres``)."""

from __future__ import annotations

import enum
from dataclasses import dataclass
from typing import Any, Optional

from . import _native
from ._handle import NativeHandle
from ._native import ffi
from .context import Capability
from .truth import Truth

__all__ = ["Subject", "Attestation", "Attestations"]


class Subject(enum.IntEnum):
    """What an attestation is about (the kernel's ``lpres_subject_type_t``)."""

    SOURCE = 0
    DIALECT = 1
    DEPENDENCY = 2
    RUNTIME = 3
    DATA = 4
    SYMBOL = 5
    MODEL_PACK = 6
    GEOMETRY = 7
    PARAMETER = 8
    SENSOR = 9
    ACTUATOR = 10
    CALIBRATION = 11
    FORMAT = 12
    KEY = 13
    FIRMWARE = 14


@dataclass(frozen=True)
class Attestation:
    """The state of one attestation and its evidence counts."""

    index: int
    truth: Truth
    positive: int
    negative: int


class Attestations(NativeHandle):
    """A registry of attestations with four-valued state.

    Recording needs ``Cap.ATTEST``. Each attestation's state follows its
    evidence: positive and negative evidence together give ``Truth.BOTH``,
    a held contradiction. :meth:`gate` is clear only when every attestation
    is certain (TRUE or FALSE): actuator authority requires a clear gate.
    """

    def __init__(self, cap: Capability) -> None:
        """Create an empty registry in ``cap.context``, used through ``cap``."""
        ctx = cap.context
        out = ffi.new("zxv_lpres **")
        _native.check(_native.lib().zxv_lpres_create_cap(cap._handle(), out), ctx)
        self._cap = cap
        super().__init__(out[0], cap, ctx)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_lpres_destroy(handle)

    def attest(self, name: str, subject: Subject, state: Truth, attester: str,
               digest: bytes = b"") -> int:
        """Record an attestation; returns its index.

        Args:
            name: what is attested (<= 31 bytes), e.g. ``"model.gguf"``.
            subject: the :class:`Subject` kind.
            state: the attested :class:`Truth` (TRUE counts as one positive).
            attester: who attests (<= 31 bytes).
            digest: an optional digest of the subject (<= 32 bytes).
        """
        if not isinstance(state, Truth):
            raise TypeError("state must be a Truth")
        d = _native.to_bytes(digest, "digest")
        out = ffi.new("uint32_t *")
        dp: Any = ffi.from_buffer("uint8_t[]", d) if d else ffi.NULL
        self._check(_native.lib().zxv_lpres_attest(
            self._cap._handle(), self._handle(), _native.to_cstr(name, "name"),
            int(Subject(subject)), state.value, _native.to_cstr(attester, "attester"),
            dp, len(d), out))
        return int(out[0])

    def evidence(self, index: int, positive: bool) -> Attestation:
        """Add one piece of evidence for (``True``) or against (``False``); returns the new state."""
        if not isinstance(positive, bool):
            raise TypeError("positive must be a bool")
        i = _native.exact_int(index, "index", *_native.U32)
        self._check(_native.lib().zxv_lpres_evidence(self._cap._handle(), self._handle(), i,
                                                     1 if positive else 0))
        return self.state(i)

    def find(self, name: str) -> Optional[int]:
        """The index of the attestation called ``name``, or ``None``."""
        out = ffi.new("uint32_t *")
        rc = _native.lib().zxv_lpres_find(self._handle(), _native.to_cstr(name, "name"), out)
        if rc == -4:
            return None
        self._check(rc)
        return int(out[0])

    def state(self, index: int) -> Attestation:
        """The current state and evidence counts of an attestation."""
        i = _native.exact_int(index, "index", *_native.U32)
        st = ffi.new("int32_t *")
        pos = ffi.new("uint32_t *")
        neg = ffi.new("uint32_t *")
        self._check(_native.lib().zxv_lpres_state(self._handle(), i, st, pos, neg))
        return Attestation(i, Truth(st[0]), int(pos[0]), int(neg[0]))

    def gate(self) -> bool:
        """True if every attestation is certain (no BOTH, no NEITHER)."""
        out = ffi.new("int32_t *")
        self._check(_native.lib().zxv_lpres_gate(self._handle(), out))
        return bool(out[0])
