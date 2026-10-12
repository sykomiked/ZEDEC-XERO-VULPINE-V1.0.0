# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The Web 4 agent layer: agents, signed tool manifests, a directory, consent
and signed envelopes (ABI 1.5, kernel ``web4``).

* **Identity.** An :class:`W4Agent` holds an ML-DSA-65 key pair (FIPS 204);
  ``agent_id = SHA3-256(public key)``. An agent is bound to the node hosting
  it by a binding the node signs (``peer_id = SHA3-256(node key)``); a
  self-bound agent signs its own (``peer_id == agent_id``). Its *card*
  (:attr:`W4Agent.card`) is what a peer needs to verify it.
* **Contexts.** Every signature carries a FIPS 204 context naming its
  purpose, so a signature for one purpose cannot be replayed as another.
* **Consent.** An agent never moves money or shares personal data without a
  token signed by the HUMAN's key (:meth:`W4Agent.issue_consent`) over the
  exact action (:func:`action_digest`). Tokens are single-use: a
  :class:`W4ConsentLog` records them, and refuses new consent when full.
* **Secrets.** A private key never leaves its :class:`W4Agent` handle: it is
  generated inside, used there, and wiped when the handle closes. No call
  returns it, and ``repr`` shows only public facts.

Capabilities: creating an agent and anything it signs (bindings, manifests,
envelopes) needs ``Cap.CRYPTO_SIGN``; issuing consent needs ``Cap.ATTEST``.
Each signing method uses the agent's own capability unless you pass another
one (``cap=``) from the same context. Parsing, verifying, digests, manifests,
the directory and the consent log need no capability.

Time: nothing here reads a clock. Every call that needs the time takes
``now_ms`` (Unix milliseconds) from you.

Honest limits: envelopes are signed, not encrypted. A consent token proves
that a key holder signed; that the holder is the right human and understood
what they signed depends on the device and UI that showed the action, which
this library cannot see. The receiving runtime (replay windows, rate
limits), carriage over freight, and the Web 2 (OIDC, OAuth, HTTP) and Web 3
bridges are NOT in this ABI. Interoperability is between ZXV instances only.

Threading: agent, directory, manifest and consent-log handles are not
thread-safe in C; the SDK serializes calls on each with a lock. Signing
releases the GIL.
"""

from __future__ import annotations

import enum
from dataclasses import dataclass
from typing import Any, Iterator, List, Optional, Tuple, Union

from . import _native
from ._handle import SerializedHandle
from ._native import ffi
from .context import Capability
from .crypto import SecretKey
from .errors import ZxvError

__all__ = [
    "W4_PK_BYTES", "W4_SIG_BYTES", "W4_ID_BYTES", "W4_NAME_MAX", "W4_MAX_TOOLS", "W4_MSG_MAX_PAYLOAD",
    "W4_CONSENT_BYTES", "W4_CARD_MAX", "W4_MANIFEST_MAX", "W4_ENV_MAX", "W4_DIR_MAX", "W4_CONSENT_LOG_MAX",
    "W4ToolFlag", "ConsentScope", "W4MsgKind", "W4MsgFlag", "W4Agent", "W4Card", "verify_card",
    "W4Directory", "W4Manifest", "W4ManifestInfo", "W4Tool", "action_digest", "W4Consent",
    "consent_info", "W4ConsentLog", "W4Envelope", "decode_envelope", "verify_envelope",
]

W4_PK_BYTES = 1952
"""An ML-DSA-65 public key."""
W4_SIG_BYTES = 3309
"""An ML-DSA-65 signature."""
W4_ID_BYTES = 32
"""agent_id, peer_id, human_id."""
W4_NAME_MAX = 31
"""Agent and tool names: 1..31 of ``[a-z0-9._-]``."""
W4_MAX_TOOLS = 16
W4_MSG_MAX_PAYLOAD = 2048
W4_CONSENT_BYTES = 3453
"""An encoded consent token."""
W4_CARD_MAX = 7259
W4_MANIFEST_MAX = 4349
W4_ENV_MAX = 8970
W4_DIR_MAX = 4096
W4_CONSENT_LOG_MAX = 65536


class W4ToolFlag(enum.IntFlag):
    """What a tool does (``ZXV_W4_TOOL_*``)."""

    NONE = 0
    MONEY = 0x01
    """The tool moves money."""
    PII = 0x02
    """The tool reveals personal data."""
    STREAM = 0x04
    """It answers with STREAM chunks."""


class ConsentScope(enum.IntFlag):
    """What a consent token allows (``ZXV_W4_CONSENT_*``)."""

    NONE = 0
    MONEY = 0x01
    PII = 0x02


class W4MsgKind(enum.IntEnum):
    """Envelope kinds (``ZXV_W4_MSG_*``)."""

    REQUEST = 1
    RESPONSE = 2
    STREAM = 3
    CANCEL = 4
    ERROR = 5


class W4MsgFlag(enum.IntFlag):
    """Envelope flags (``ZXV_W4_MF_*``)."""

    NONE = 0
    FINAL = 0x01
    """Last STREAM chunk / final response."""
    CONSENT = 0x02
    """A consent token is attached (set by sealing, never by the caller)."""


def _u64(v: Any, what: str) -> int:
    return _native.exact_int(v, what, *_native.U64)


def _u32(v: Any, what: str) -> int:
    return _native.exact_int(v, what, *_native.U32)


def _id(v: Any, what: str) -> bytes:
    return _native.fixed_bytes(v, W4_ID_BYTES, what)


def _pk(v: Any, what: str = "public_key") -> bytes:
    return _native.fixed_bytes(v, W4_PK_BYTES, what)


def _token(v: Any) -> bytes:
    return _native.fixed_bytes(v, W4_CONSENT_BYTES, "token")


def _b(data: bytes) -> Any:
    return _native.cbuf(data)


# ---------------------------------------------------------------- agents


class W4Agent(SerializedHandle):
    """An agent's (or a human's) ML-DSA-65 key pair and signed card (``zxv_w4_agent``).

    The private key is generated inside the handle and wiped when it closes;
    it can never be read out. A human's key is an agent handle too: its
    :attr:`agent_id` is the human_id.

    Args:
        cap: a capability holding ``Cap.CRYPTO_SIGN`` (also used, unless
            overridden, for everything this handle signs).
        name: 1..31 of ``[a-z0-9._-]``.
        now_ms: the binding time (Unix milliseconds).
        seed: 32 bytes for a reproducible key (a :class:`~zxv.SecretKey`
            or bytes), or ``None`` for a fresh random one.
        node: the handle of the node hosting this agent (it signs the
            binding), from the same context; ``None`` for a self-bound agent.

    Raises:
        CapabilityDeniedError: no ``Cap.CRYPTO_SIGN``.
        ZxvUsageError: ``ZXV_E_ARG`` (bad name characters), ``ZXV_E_TOO_LONG``.
    """

    def __init__(self, cap: Capability, name: str, now_ms: int, *,
                 seed: Optional[Union[SecretKey, bytes]] = None, node: Optional["W4Agent"] = None) -> None:
        ctx = cap.context
        s: Any = ffi.NULL
        if isinstance(seed, SecretKey):
            if len(seed) != 32:
                raise _native.make_error(-1, "seed must be 32 bytes")
            s = seed._cdata()
        elif seed is not None:
            s = _b(_native.fixed_bytes(seed, 32, "seed"))
        n = _native.to_cstr(name, "name")
        out = ffi.new("zxv_w4_agent **")
        if node is not None:
            with node._lock:
                rc = _native.lib().zxv_w4_agent_create(cap._handle(), s, n, node._handle(), _u64(now_ms, "now_ms"), out)
        else:
            rc = _native.lib().zxv_w4_agent_create(cap._handle(), s, n, ffi.NULL, _u64(now_ms, "now_ms"), out)
        _native.check(rc, ctx)
        self._cap = cap
        super().__init__(out[0], cap, ctx)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_w4_agent_destroy(handle)

    def _c(self, cap: Optional[Capability]) -> Any:
        return (cap or self._cap)._handle()

    def _info(self) -> Tuple[bytes, bytes, bytes]:
        a, p = ffi.new("uint8_t[32]"), ffi.new("uint8_t[32]")
        k = ffi.new("uint8_t[]", W4_PK_BYTES)
        with self._lock:
            self._check(_native.lib().zxv_w4_agent_info(self._handle(), a, p, k))
        return bytes(ffi.buffer(a, 32)), bytes(ffi.buffer(p, 32)), bytes(ffi.buffer(k, W4_PK_BYTES))

    @property
    def agent_id(self) -> bytes:
        """SHA3-256 of the public key (the human_id for a human's handle)."""
        return self._info()[0]

    @property
    def peer_id(self) -> bytes:
        """SHA3-256 of the hosting node's key (== :attr:`agent_id` when self-bound)."""
        return self._info()[1]

    @property
    def public_key(self) -> bytes:
        """The ML-DSA-65 public key (:data:`W4_PK_BYTES`)."""
        return self._info()[2]

    @property
    def card(self) -> bytes:
        """The signed agent card a peer verifies with :func:`verify_card`."""
        with self._lock:
            h = self._handle()
            return _native.read_bytes(lambda b, c, n: _native.lib().zxv_w4_agent_card(h, b, c, n),
                                      self._context, W4_CARD_MAX)

    def issue_consent(self, agent_id: bytes, action: bytes, scope: ConsentScope, *, max_vfv: int,
                      issued_ms: int, expires_ms: int, nonce: Optional[bytes] = None,
                      cap: Optional[Capability] = None) -> bytes:
        """Issue a consent token, on the HUMAN's side, after showing the human the action.

        This handle is the human's key. The token lets ``agent_id`` do ``action``
        (an :func:`action_digest`) within ``scope``, moving money up to
        ``max_vfv`` minor units, valid in ``[issued_ms, expires_ms)``.

        Args:
            nonce: 16 bytes, or ``None`` for a random one; it makes the token single-use.
            cap: needs ``Cap.ATTEST`` (the handle's own cap is used by default).

        Returns:
            The :data:`W4_CONSENT_BYTES`-byte token.

        Raises:
            CapabilityDeniedError: no ``Cap.ATTEST``.
            ZxvUsageError: ``ZXV_E_ARG`` (empty scope, ``expires_ms <= issued_ms``).
        """
        aid, act = _id(agent_id, "agent_id"), _id(action, "action")
        nb = ffi.NULL if nonce is None else _b(_native.fixed_bytes(nonce, 16, "nonce"))
        out = ffi.new("uint8_t[]", W4_CONSENT_BYTES)
        with self._lock:
            self._check(_native.lib().zxv_w4_consent_issue(self._c(cap), self._handle(), _b(aid), _b(act),
                                                           _u32(int(scope), "scope"), _u64(max_vfv, "max_vfv"),
                                                           _u64(issued_ms, "issued_ms"),
                                                           _u64(expires_ms, "expires_ms"), nb, out))
        return bytes(ffi.buffer(out, W4_CONSENT_BYTES))

    def seal(self, to: bytes, kind: W4MsgKind, *, req_id: int, tool: str, payload: bytes = b"",
             now_ms: int, chunk: int = 0, flags: W4MsgFlag = W4MsgFlag.NONE,
             consent: Optional[bytes] = None, cap: Optional[Capability] = None) -> bytes:
        """Build, sign and encode an envelope from this agent to ``to`` (an agent id).

        Sealing uses the agent's next sequence number.

        Args:
            kind: the :class:`W4MsgKind`.
            tool: a valid tool name.
            payload: at most :data:`W4_MSG_MAX_PAYLOAD` bytes.
            flags: ``NONE`` or ``FINAL``.
            consent: a consent token to attach.
            cap: needs ``Cap.CRYPTO_SIGN``.

        Raises:
            ZxvUsageError: ``ZXV_E_ARG`` (bad kind, flags or tool),
                ``ZXV_E_TOO_LONG`` (payload), ``ZXV_E_FORMAT`` (a malformed token).
        """
        t = _id(to, "to")
        p = _native.to_bytes(payload, "payload")
        tok = ffi.NULL if consent is None else _b(_token(consent))
        tl = _native.to_cstr(tool, "tool")
        k = _native.exact_int(int(kind), "kind", *_native.I32)
        with self._lock:
            c, h = self._c(cap), self._handle()
            args = (_u64(req_id, "req_id"), _u32(chunk, "chunk"), tl, _u32(int(flags), "flags"))
            return _native.read_bytes(
                lambda b, bc, n: _native.lib().zxv_w4_env_seal(c, h, k, _b(t), args[0], args[1], args[2], args[3],
                                                               _b(p), len(p), tok, _u64(now_ms, "now_ms"),
                                                               b, bc, n), self._context, W4_ENV_MAX)

    def reply(self, request: bytes, kind: W4MsgKind, *, payload: bytes = b"", now_ms: int, chunk: int = 0,
              flags: W4MsgFlag = W4MsgFlag.NONE, cap: Optional[Capability] = None) -> bytes:
        """Seal a reply (RESPONSE, STREAM or ERROR) to a REQUEST envelope addressed to
        this agent: from/to swapped, same request id and tool. The request's
        signature is NOT checked here: call :func:`verify_envelope` first.

        Raises:
            ZxvUsageError: ``ZXV_E_ARG`` (not a REQUEST, not addressed to this
                agent, a bad kind or flags), ``ZXV_E_FORMAT`` (malformed request).
        """
        r = _native.to_bytes(request, "request")
        p = _native.to_bytes(payload, "payload")
        k = _native.exact_int(int(kind), "kind", *_native.I32)
        ch, fl, now = _u32(chunk, "chunk"), _u32(int(flags), "flags"), _u64(now_ms, "now_ms")
        with self._lock:
            c, h = self._c(cap), self._handle()
            return _native.read_bytes(
                lambda b, bc, n: _native.lib().zxv_w4_env_reply(c, h, _b(r), len(r), k, ch, fl, _b(p), len(p), now,
                                                                b, bc, n), self._context, W4_ENV_MAX)

    def __repr__(self) -> str:
        if self._closed:
            return "<zxv.W4Agent closed>"
        return f"<zxv.W4Agent id={self.agent_id.hex()[:16]}...>"


@dataclass(frozen=True)
class W4Card:
    """A verified agent card's public facts."""

    agent_id: bytes
    peer_id: bytes
    name: str
    bound_ms: int

    @property
    def self_bound(self) -> bool:
        return self.agent_id == self.peer_id


def verify_card(card: bytes) -> W4Card:
    """Verify an agent card (well formed, binding signature valid) and read it.

    Raises:
        ZxvUsageError: ``ZXV_E_FORMAT`` (malformed).
        VerificationFailedError: the signature fails.
    """
    c = _native.to_bytes(card, "card")
    a, p = ffi.new("uint8_t[32]"), ffi.new("uint8_t[32]")
    nm, t = ffi.new("char[32]"), ffi.new("uint64_t *")
    _native.check(_native.lib().zxv_w4_card_verify(_b(c), len(c), a, p, nm, t))
    return W4Card(bytes(ffi.buffer(a, 32)), bytes(ffi.buffer(p, 32)), ffi.string(nm).decode("utf-8"), int(t[0]))


# ---------------------------------------------------------------- directory


class W4Directory(SerializedHandle):
    """A directory of verified agent cards keyed by agent id (``zxv_w4_dir``).

    Args:
        capacity: 1..:data:`W4_DIR_MAX` cards.
    """

    def __init__(self, capacity: int = 64) -> None:
        out = ffi.new("zxv_w4_dir **")
        _native.check(_native.lib().zxv_w4_dir_create(_u32(capacity, "capacity"), out))
        super().__init__(out[0], None, None)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_w4_dir_destroy(handle)

    def add(self, card: bytes) -> None:
        """Verify a card, then add it or replace the entry for the same agent.

        Raises:
            VerificationFailedError, ZxvUsageError: as :func:`verify_card`.
            ZxvRefusedError: ``ZXV_E_STATE`` (the binding is older than the one
                held: no rollback), ``ZXV_E_CAPACITY`` (full).
        """
        c = _native.to_bytes(card, "card")
        with self._lock:
            _native.check(_native.lib().zxv_w4_dir_add(self._handle(), _b(c), len(c)))

    def find(self, agent_id: bytes) -> Optional[bytes]:
        """The card held for ``agent_id``, or ``None``."""
        a = _b(_id(agent_id, "agent_id"))
        with self._lock:
            h = self._handle()
            try:
                return _native.read_bytes(lambda b, c, n: _native.lib().zxv_w4_dir_find(h, a, b, c, n), None,
                                          W4_CARD_MAX)
            except ZxvError as e:
                if e.status == -4:
                    return None
                raise

    def __contains__(self, agent_id: object) -> bool:
        return isinstance(agent_id, bytes) and len(agent_id) == W4_ID_BYTES and self.find(agent_id) is not None

    def __len__(self) -> int:
        out = ffi.new("uint32_t *")
        with self._lock:
            _native.check(_native.lib().zxv_w4_dir_count(self._handle(), out))
        return int(out[0])


# ---------------------------------------------------------------- manifests


@dataclass(frozen=True)
class W4Tool:
    """One tool in a manifest."""

    name: str
    price_vfv: int
    """Price in VFV minor units (2 digits)."""
    rate_per_min: int
    burst: int
    flags: W4ToolFlag
    max_input: int


@dataclass(frozen=True)
class W4ManifestInfo:
    """A manifest's header fields (ids are zero until signed)."""

    agent_id: bytes
    peer_id: bytes
    version: int
    issued_ms: int
    expires_ms: int
    tool_count: int
    signed: bool


class W4Manifest(SerializedHandle):
    """A tool manifest (``zxv_w4_manifest``): up to 16 tools, valid in
    ``[issued_ms, expires_ms)``. Signing fixes the agent and peer ids; adding a
    tool afterwards drops the signature.

    Raises:
        ZxvUsageError: ``ZXV_E_ARG`` when ``expires_ms <= issued_ms``.
    """

    def __init__(self, version: int, issued_ms: int, expires_ms: int) -> None:
        out = ffi.new("zxv_w4_manifest **")
        _native.check(_native.lib().zxv_w4_manifest_create(_u32(version, "version"), _u64(issued_ms, "issued_ms"),
                                                            _u64(expires_ms, "expires_ms"), out))
        super().__init__(out[0], None, None)

    @classmethod
    def _wrap(cls, h: Any) -> "W4Manifest":
        m = cls.__new__(cls)
        SerializedHandle.__init__(m, h, None, None)
        return m

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_w4_manifest_destroy(handle)

    @classmethod
    def decode(cls, data: bytes) -> "W4Manifest":
        """Parse the wire form. The signature is kept but NOT checked: call :meth:`verify`.

        Raises:
            ZxvUsageError: ``ZXV_E_FORMAT`` (malformed, trailing bytes, an invalid tool).
        """
        d = _native.to_bytes(data, "data")
        out = ffi.new("zxv_w4_manifest **")
        _native.check(_native.lib().zxv_w4_manifest_decode(_b(d), len(d), out))
        return cls._wrap(out[0])

    def add_tool(self, name: str, *, price_vfv: int, rate_per_min: int, burst: int,
                 flags: W4ToolFlag = W4ToolFlag.NONE, max_input: int = 0) -> None:
        """Add a tool (``rate_per_min`` and ``burst`` >= 1; ``max_input`` <= 2048).

        Raises:
            ZxvUsageError: ``ZXV_E_DUPLICATE`` (name listed), ``ZXV_E_ARG``, ``ZXV_E_TOO_LONG``.
            ZxvRefusedError: ``ZXV_E_CAPACITY`` (16 tools).
        """
        n = _native.to_cstr(name, "name")
        with self._lock:
            _native.check(_native.lib().zxv_w4_manifest_add_tool(self._handle(), n, _u64(price_vfv, "price_vfv"),
                                                                 _u32(rate_per_min, "rate_per_min"),
                                                                 _u32(burst, "burst"), _u32(int(flags), "flags"),
                                                                 _u32(max_input, "max_input")))

    def sign(self, agent: W4Agent, *, cap: Optional[Capability] = None) -> None:
        """Sign with ``agent``'s key (``Cap.CRYPTO_SIGN``, the agent's cap by default)."""
        with self._lock, agent._lock:
            c = (cap or agent._cap)._handle()
            _native.check(_native.lib().zxv_w4_manifest_sign(c, agent._handle(), self._handle()), agent._context)

    def verify(self, public_key: bytes, now_ms: int) -> None:
        """Check the signature is ``public_key``'s, that key is the manifest's agent,
        and ``issued_ms <= now_ms < expires_ms``.

        Raises:
            VerificationFailedError: wrong key, or a bad or missing signature.
            ZxvRefusedError: ``ZXV_E_STATE`` (valid signature, not valid at ``now_ms``).
        """
        k = _pk(public_key)
        with self._lock:
            _native.check(_native.lib().zxv_w4_manifest_verify(self._handle(), _b(k), _u64(now_ms, "now_ms")))

    def encode(self) -> bytes:
        """The signed wire form.

        Raises:
            ZxvRefusedError: ``ZXV_E_STATE`` if not signed.
        """
        with self._lock:
            h = self._handle()
            return _native.read_bytes(lambda b, c, n: _native.lib().zxv_w4_manifest_encode(h, b, c, n), None,
                                      W4_MANIFEST_MAX)

    def to_json(self) -> str:
        """The Web 2 discovery face (e.g. ``/.well-known/web4-agent.json``); prices stay integers."""
        with self._lock:
            h = self._handle()
            return _native.read_text(lambda b, c, n: _native.lib().zxv_w4_manifest_to_json(h, b, c, n))

    @property
    def info(self) -> W4ManifestInfo:
        a, p = ffi.new("uint8_t[32]"), ffi.new("uint8_t[32]")
        v, tc = ffi.new("uint32_t *"), ffi.new("uint32_t *")
        i, e = ffi.new("uint64_t *"), ffi.new("uint64_t *")
        s = ffi.new("int32_t *")
        with self._lock:
            _native.check(_native.lib().zxv_w4_manifest_info(self._handle(), a, p, v, i, e, tc, s))
        return W4ManifestInfo(bytes(ffi.buffer(a, 32)), bytes(ffi.buffer(p, 32)), int(v[0]), int(i[0]), int(e[0]),
                              int(tc[0]), bool(s[0]))

    def tool(self, index: int) -> W4Tool:
        """Tool ``index`` (0-based).

        Raises:
            ZxvUsageError: ``ZXV_E_NOT_FOUND`` past the last tool.
        """
        nm = ffi.new("char[32]")
        pr = ffi.new("uint64_t *")
        r, b, f, m = (ffi.new("uint32_t *") for _ in range(4))
        with self._lock:
            _native.check(_native.lib().zxv_w4_manifest_tool(self._handle(), _u32(index, "index"), nm, pr, r, b,
                                                             f, m))
        return W4Tool(ffi.string(nm).decode("utf-8"), int(pr[0]), int(r[0]), int(b[0]), W4ToolFlag(f[0]),
                      int(m[0]))

    @property
    def tools(self) -> List[W4Tool]:
        return [self.tool(i) for i in range(self.info.tool_count)]

    def __len__(self) -> int:
        return self.info.tool_count

    def __iter__(self) -> Iterator[W4Tool]:
        return iter(self.tools)

    def find(self, name: str) -> Optional[int]:
        """The index of tool ``name``, or ``None``."""
        out = ffi.new("uint32_t *")
        n = _native.to_cstr(name, "name")
        with self._lock:
            rc = _native.lib().zxv_w4_manifest_find(self._handle(), n, out)
        if rc == -4:
            return None
        _native.check(rc)
        return int(out[0])


# ---------------------------------------------------------------- consent


def action_digest(tool: str, from_id: bytes, to_id: bytes, req_id: int, payload: bytes = b"") -> bytes:
    """The 32-byte digest a consent token for one request must carry: SHA3-256
    over ``"web4/v1/action"``, tool, from, to, req_id and payload.

    Raises:
        ZxvUsageError: ``ZXV_E_ARG`` (a bad tool name), ``ZXV_E_TOO_LONG`` (payload over 2048).
    """
    p = _native.to_bytes(payload, "payload")
    out = ffi.new("uint8_t[32]")
    _native.check(_native.lib().zxv_w4_action_digest(_native.to_cstr(tool, "tool"), _b(_id(from_id, "from_id")),
                                                     _b(_id(to_id, "to_id")), _u64(req_id, "req_id"), _b(p),
                                                     len(p), out))
    return bytes(ffi.buffer(out, 32))


@dataclass(frozen=True)
class W4Consent:
    """A consent token's fields (read without checking the signature)."""

    human_id: bytes
    agent_id: bytes
    action: bytes
    scope: ConsentScope
    max_vfv: int
    issued_ms: int
    expires_ms: int
    nonce: bytes


def consent_info(token: bytes) -> W4Consent:
    """Read a token's fields (no signature check).

    Raises:
        ZxvUsageError: ``ZXV_E_FORMAT`` (malformed).
    """
    t = _token(token)
    h, a, ac = ffi.new("uint8_t[32]"), ffi.new("uint8_t[32]"), ffi.new("uint8_t[32]")
    sc = ffi.new("uint32_t *")
    mx, i, e = ffi.new("uint64_t *"), ffi.new("uint64_t *"), ffi.new("uint64_t *")
    nn = ffi.new("uint8_t[16]")
    _native.check(_native.lib().zxv_w4_consent_info(_b(t), h, a, ac, sc, mx, i, e, nn))
    return W4Consent(bytes(ffi.buffer(h, 32)), bytes(ffi.buffer(a, 32)), bytes(ffi.buffer(ac, 32)),
                     ConsentScope(sc[0]), int(mx[0]), int(i[0]), int(e[0]), bytes(ffi.buffer(nn, 16)))


class W4ConsentLog(SerializedHandle):
    """A single-use log of consumed and revoked consent tokens (``zxv_w4_consent_log``).

    Args:
        capacity: 1..:data:`W4_CONSENT_LOG_MAX` entries.
    """

    def __init__(self, capacity: int = 1024) -> None:
        out = ffi.new("zxv_w4_consent_log **")
        _native.check(_native.lib().zxv_w4_consent_log_create(_u32(capacity, "capacity"), out))
        super().__init__(out[0], None, None)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_w4_consent_log_destroy(handle)

    def check(self, token: Optional[bytes], *, human_public_key: bytes, agent_id: bytes, action: bytes,
              need: ConsentScope, amount: int, now_ms: int) -> None:
        """Check that ``token`` lets ``agent_id`` do ``action`` with scope ``need`` for
        ``amount`` VFV minor units (checked against ``max_vfv`` when ``need`` has
        MONEY), signed by the human whose key is ``human_public_key`` and valid
        at ``now_ms``; then record it as used. Returning means consent was
        given and is now spent.

        Raises:
            ConsentRequiredError: no token (``None``), a mismatch, expired or
                not yet valid, a bad signature, or a token already used or revoked.
            ZxvUsageError: ``ZXV_E_FORMAT`` (malformed token).
            ZxvRefusedError: ``ZXV_E_CAPACITY`` (the log is full even after
                pruning expired entries; nothing is allowed).
        """
        tb = ffi.NULL if token is None else _b(_token(token))
        k, a, ac = _pk(human_public_key, "human_public_key"), _id(agent_id, "agent_id"), _id(action, "action")
        with self._lock:
            _native.check(_native.lib().zxv_w4_consent_check(self._handle(), tb, _b(k), _b(a), _b(ac),
                                                             _u32(int(need), "need"), _u64(amount, "amount"),
                                                             _u64(now_ms, "now_ms")))

    def revoke(self, token: bytes, now_ms: int) -> None:
        """Revoke a token before use (no capability, no signature check; twice is fine)."""
        t = _token(token)
        with self._lock:
            _native.check(_native.lib().zxv_w4_consent_revoke(self._handle(), _b(t), _u64(now_ms, "now_ms")))

    def prune(self, now_ms: int) -> int:
        """Drop entries whose token expired at or before ``now_ms``; returns what is left."""
        out = ffi.new("uint32_t *")
        with self._lock:
            _native.check(_native.lib().zxv_w4_consent_log_prune(self._handle(), _u64(now_ms, "now_ms"), out))
        return int(out[0])


# ---------------------------------------------------------------- envelopes


@dataclass(frozen=True)
class W4Envelope:
    """A decoded envelope (its signature not checked: see :func:`verify_envelope`)."""

    kind: W4MsgKind
    flags: W4MsgFlag
    from_id: bytes
    to_id: bytes
    seq: int
    ts_ms: int
    req_id: int
    chunk: int
    tool: str
    payload: bytes
    consent: Optional[bytes]
    """The attached consent token, if any."""


def decode_envelope(envelope: bytes) -> W4Envelope:
    """Parse an envelope (no signature check).

    Raises:
        ZxvUsageError: ``ZXV_E_FORMAT`` (malformed).
    """
    e = _native.to_bytes(envelope, "envelope")
    eb = _b(e)
    L = _native.lib()
    k, fl, ch, pl = ffi.new("int32_t *"), ffi.new("uint32_t *"), ffi.new("uint32_t *"), ffi.new("uint32_t *")
    fr, to = ffi.new("uint8_t[32]"), ffi.new("uint8_t[32]")
    sq, ts, rq = ffi.new("uint64_t *"), ffi.new("uint64_t *"), ffi.new("uint64_t *")
    tool = ffi.new("char[32]")
    _native.check(L.zxv_w4_env_decode(eb, len(e), k, fl, fr, to, sq, ts, rq, ch, tool, pl))
    payload = _native.read_bytes(lambda b, c, n: L.zxv_w4_env_payload(eb, len(e), b, c, n), None, int(pl[0]))
    tok = ffi.new("uint8_t[]", W4_CONSENT_BYTES)
    rc = L.zxv_w4_env_consent(eb, len(e), tok)
    consent = None if rc == -4 else bytes(ffi.buffer(tok, W4_CONSENT_BYTES))
    if rc not in (0, -4):
        _native.check(rc)
    return W4Envelope(W4MsgKind(k[0]), W4MsgFlag(fl[0]), bytes(ffi.buffer(fr, 32)), bytes(ffi.buffer(to, 32)),
                      int(sq[0]), int(ts[0]), int(rq[0]), int(ch[0]), ffi.string(tool).decode("utf-8"), payload,
                      consent)


def verify_envelope(envelope: bytes, sender_public_key: bytes) -> None:
    """Check ``sender_public_key`` is the envelope's sender and its signature verifies.
    Replay, timestamp and rate checks are the receiver's job.

    Raises:
        VerificationFailedError: otherwise.
    """
    e = _native.to_bytes(envelope, "envelope")
    _native.check(_native.lib().zxv_w4_env_verify(_b(e), len(e), _b(_pk(sender_public_key, "sender_public_key"))))
