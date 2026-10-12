# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Contexts and capabilities."""

from __future__ import annotations

import enum
import threading
from dataclasses import dataclass
from typing import Any, Optional

from . import _native
from ._handle import NativeHandle
from ._native import ffi
from .errors import FatalPolicy, ZxvFatal
from .truth import Truth

__all__ = ["Cap", "Context", "Capability", "CapabilityInfo", "IDENTITY_BYTES"]

IDENTITY_BYTES = 21
"""Length of a capability identity (168 bits)."""


class Cap(enum.IntFlag):
    """Capability bits (``ZXV_CAP_*``). Combine with ``|``."""

    NONE = 0
    LEDGER_READ = 1 << 0
    """Read balances and journals."""
    LEDGER_POST = 1 << 1
    """Post transfers (capital transfer)."""
    CRYPTO_SIGN = 1 << 2
    """Sign with ML-DSA / Ed25519."""
    CRYPTO_KEM = 1 << 3
    """ML-KEM key generation and (de)capsulation."""
    AI_INFERENCE = 1 << 4
    """Open models and generate."""
    BUDGET = 1 << 5
    """Create, consume and release rmag budgets."""
    PHASE = 1 << 6
    """Admit, defer and advance on the phase clock."""
    ATTEST = 1 << 7
    """Record attestations and evidence."""
    SWARM = 1 << 8
    """Drive the swarm token economy."""
    EVM = 1 << 9
    """EVM state-channel encoding."""
    ALL = (1 << 10) - 1
    """Every capability of ABI 1.1 (``ZXV_CAP_ALL``; this value never changes)."""
    HW_MATRIX = 1 << 10
    """Build and call the hardware emulation matrix (ABI 1.4)."""
    HW_RECONF = 1 << 11
    """Load hardware images and contract packages (ABI 1.4)."""
    ALL_1_4 = (1 << 12) - 1
    """Every capability as of ABI 1.4 (``ZXV_CAP_ALL_1_4``)."""


@dataclass(frozen=True)
class CapabilityInfo:
    """What ``zxv_cap_info`` reports about a capability."""

    caps: Cap
    """The bits this capability holds."""
    process_id: int
    """The process id it was created for."""
    attestation: Truth
    """Its attestation; anything but TRUE blocks gated calls."""
    revoked: bool
    """True if it, or an ancestor, was revoked."""


class Context(NativeHandle):
    """One ZXV platform instance: a ledger, its currencies and its state.

    Every other object is created from a context (directly, or through a
    :class:`Capability` made from it). A context is thread-safe (R6).

    Use it as a context manager::

        with zxv.Context() as ctx:
            cap = ctx.root_capability()
            ...

    Args:
        fatal_policy: what to do on a FATAL status; defaults to the process
            default (:func:`zxv.set_default_fatal_policy`), which is
            :attr:`FatalPolicy.RAISE`.
    """

    def __init__(self, *, fatal_policy: Optional[FatalPolicy] = None) -> None:
        out = ffi.new("zxv_ctx **")
        _native.check(_native.lib().zxv_ctx_create(out))
        self._dead = False
        self._fatal: Optional[ZxvFatal] = None
        self._policy = fatal_policy or _native.default_fatal_policy()
        self._state_lock = threading.Lock()
        super().__init__(out[0], None, None)
        self._context = self

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_ctx_destroy(handle)

    # ---------------------------------------------------------------- fatal

    @property
    def fatal_policy(self) -> FatalPolicy:
        """The :class:`FatalPolicy` applied when this context halts."""
        return self._policy

    @fatal_policy.setter
    def fatal_policy(self, policy: FatalPolicy) -> None:
        if not isinstance(policy, FatalPolicy):
            raise TypeError("fatal_policy must be a FatalPolicy")
        self._policy = policy

    @property
    def dead(self) -> bool:
        """True once the SDK has seen a FATAL status from this context."""
        return self._dead

    def _on_fatal(self, exc: ZxvFatal) -> FatalPolicy:
        with self._state_lock:
            self._dead = True
            if self._fatal is None:
                self._fatal = exc
        return self._policy

    def _require_alive(self) -> None:
        if self._dead:
            first = self._fatal
            exc = ZxvFatal(-18, "ZXV_E_POISONED",
                           "context halted earlier" + (f": {first.detail}" if first else ""))
            _native.raise_fatal(exc, self)

    @property
    def is_poisoned(self) -> bool:
        """Ask the C library whether the context is halted (``zxv_ctx_is_poisoned``)."""
        if self._closed:
            return self._dead
        out = ffi.new("int32_t *")
        _native.check(_native.lib().zxv_ctx_is_poisoned(self._h, out))
        return bool(out[0])

    def halt(self, reason: Optional[str] = None) -> None:
        """Halt the context on purpose (an operator kill switch).

        Afterwards the context behaves exactly like one that broke an
        invariant: the next call on it raises :class:`ZxvFatal`.
        ``reason`` is at most 150 bytes.
        """
        h = self._handle()
        r = _native.to_cstr(reason, "reason") if reason is not None else ffi.NULL
        self._check(_native.lib().zxv_ctx_halt(h, r))

    # ---------------------------------------------------------------- factories

    def root_capability(self, caps: Cap = Cap.ALL, *, process_id: int = 0,
                        identity: Optional[bytes] = None) -> "Capability":
        """Create a root capability for this context.

        Args:
            caps: the bits to grant (default: all).
            process_id: a process id recorded in the capability.
            identity: an optional 21-byte (168-bit) identity; zeros if omitted.
        """
        h = self._handle()
        caps_i = _native.exact_int(int(caps) if isinstance(caps, Cap) else caps, "caps", *_native.U64)
        pid = _native.exact_int(process_id, "process_id", *_native.U32)
        ident: Any = ffi.NULL
        if identity is not None:
            ib = _native.to_bytes(identity, "identity")
            if len(ib) != IDENTITY_BYTES:
                raise ValueError(f"identity must be {IDENTITY_BYTES} bytes")
            ident = ffi.from_buffer("uint8_t[]", ib)
        out = ffi.new("zxv_cap **")
        self._check(_native.lib().zxv_cap_create(h, pid, ident, caps_i, out))
        return Capability(out[0], self, self)

    @property
    def currencies(self) -> "Any":
        """The :class:`zxv.Currencies` enabled in this context."""
        from .ledger import Currencies

        return Currencies(self)

    def __repr__(self) -> str:
        state = "closed" if self._closed else ("dead" if self._dead else "open")
        return f"<zxv.Context {state}>"


class Capability(NativeHandle):
    """A capability: what a part of the program may do, checked on every call.

    A capability carries a process id, a 168-bit identity, a set of
    :class:`Cap` bits and an attestation (:class:`Truth`). Gated calls verify
    it each time: it is not revoked (nor any ancestor), it has the needed bit,
    and its attestation is TRUE. Hand narrower ones to code that needs less
    with :meth:`attenuate`; a capability can never be widened.

    Honest limit: inside one process this is least-privilege discipline,
    not a sandbox. Code in the same process can create its own root
    capability from the context.
    """

    def __init__(self, handle: Any, parent: NativeHandle, context: Context) -> None:
        super().__init__(handle, parent, context)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_cap_destroy(handle)

    @property
    def context(self) -> Context:
        """The context this capability belongs to."""
        return self._context  # type: ignore[no-any-return]

    def attenuate(self, keep: Cap) -> "Capability":
        """A child capability holding ``self.caps & keep``.

        The child is revoked whenever this capability is revoked, and is
        closed when this capability is closed.
        """
        h = self._handle()
        out = ffi.new("zxv_cap **")
        self._check(_native.lib().zxv_cap_attenuate(h, _native.exact_int(int(keep), "keep", *_native.U64), out))
        return Capability(out[0], self, self._context)

    def revoke(self) -> None:
        """Revoke this capability and every descendant, permanently."""
        self._check(_native.lib().zxv_cap_revoke(self._handle()))

    def set_attestation(self, state: Truth) -> None:
        """Set the attestation. Anything but ``Truth.TRUE`` blocks gated calls."""
        if not isinstance(state, Truth):
            raise TypeError("state must be a Truth")
        self._check(_native.lib().zxv_cap_set_attestation(self._handle(), state.value))

    @property
    def info(self) -> CapabilityInfo:
        """The bits, process id, attestation and revocation state."""
        caps = ffi.new("uint64_t *")
        pid = ffi.new("uint32_t *")
        att = ffi.new("int32_t *")
        rev = ffi.new("int32_t *")
        self._check(_native.lib().zxv_cap_info(self._handle(), caps, pid, att, rev))
        return CapabilityInfo(Cap(caps[0]), int(pid[0]), Truth(att[0]), bool(rev[0]))

    @property
    def caps(self) -> Cap:
        """The :class:`Cap` bits held."""
        return self.info.caps

    def check(self, needed: Cap) -> bool:
        """True if a call needing ``needed`` would be allowed now."""
        rc = _native.lib().zxv_cap_check(self._handle(), int(needed))
        if rc == -17:
            return False
        self._check(rc)
        return True

    def require(self, needed: Cap) -> None:
        """Raise :class:`CapabilityDeniedError` unless :meth:`check` passes."""
        rc = _native.lib().zxv_cap_check(self._handle(), int(needed))
        self._check(rc)

    def __repr__(self) -> str:
        if self._closed:
            return "<zxv.Capability closed>"
        i = self.info
        return f"<zxv.Capability {i.caps!r} attestation={i.attestation.name} revoked={i.revoked}>"

