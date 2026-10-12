# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Fibonacci coins and AI token denominations (ABI 1.5, kernel ``vino_stores``
and ``swarm/swarm_denom``).

Two Fibonacci ladders share the faces 1, 2, 3, 5, 8, 13, 21, ... and the same
unique fewest-pieces (Zeckendorf) form. Everything is integer-only.

**VFV coins.** Rung r (1..:data:`COIN_RUNGS`) has face F(r+1): 1, 2, 3, 5, ...
up to :data:`COIN_TOP_FACE`. Rung 0 is the ceremonial V0 note (face 0, worth
nothing). Any other face is not a coin (:class:`~zxv.DenominationError`). Pay
an arbitrary amount with :func:`split`. Minting coins is NOT in this ABI.

**AI tokens.** Tier k (0..63) has face F(k+2); tiers from :data:`TOKEN_SUPER_TIER`
(face 144) up are super tokens. A :class:`TokenHolding` holds counts per
tier. OCTANE is the densest tier a holding may use (the named grades in
:class:`Octane` are like a fuel pump's; any 0..63 is allowed). The canonical
form at an octane is unique.

**No premium.** A dense token is worth exactly its face in plain tokens.
Density earns no interest, costs no fee and buys no extra allotment or
priority; changing octane re-expresses the same value and never changes it.
It only means fewer pieces (1,000,000 tokens are 17 pieces at RACING), which
helps where tokens are handled one piece at a time. It does not make a
model faster or an amount shorter on the wire.

The pure functions need no capability. A :class:`TokenWallet` keeps a holding
canonical at its own octane; every change to it needs ``Cap.MINT`` (tokens
enter a wallet only by :meth:`TokenWallet.mint`, which creates them).
"""

from __future__ import annotations

import enum
from dataclasses import dataclass
from typing import Any, Dict, Iterable, Iterator, List, Mapping, Optional, Sequence, Tuple, Union, final

from . import _native
from ._handle import NativeHandle
from ._native import ffi
from .context import Capability

__all__ = [
    "COIN_RUNGS", "COIN_TOP_FACE", "TOKEN_TIERS", "TOKEN_SUPER_TIER", "TOKEN_CODE_MAX_BITS",
    "Octane", "face", "rung", "split", "split_count", "TokenFace", "token_face", "token_tier", "TokenHolding",
    "code_bits", "code_put", "code_get", "code_encode", "code_decode", "TokenWallet", "WalletInfo",
]

COIN_RUNGS = 45
"""Rungs 1..45 carry value."""
COIN_TOP_FACE = 1_836_311_903
"""Face of rung 45, F(46)."""
TOKEN_TIERS = 64
TOKEN_SUPER_TIER = 10
"""The first super-token tier (face 144)."""
TOKEN_CODE_MAX_BITS = 93
"""Longest Fibonacci code of a 64-bit value."""
_CODE_MAX_BYTES = 536_870_911


class Octane(enum.IntEnum):
    """Named token octanes (any int 0..63 is also accepted)."""

    PLAIN = 0
    """Face 1 only."""
    REGULAR = 4
    """Up to face 8."""
    MIDGRADE = 9
    """Up to face 89."""
    PREMIUM = 14
    """Up to face 987."""
    RACING = 23
    """Up to face 75025."""
    MAX = 63
    """Up to face F(65)."""


def _u64(v: Any, what: str) -> int:
    return _native.exact_int(v, what, *_native.U64)


def _u32(v: Any, what: str) -> int:
    return _native.exact_int(v, what, *_native.U32)


# ---------------------------------------------------------------- VFV coins


def face(rung: int) -> int:
    """The face of ladder ``rung`` (0..:data:`COIN_RUNGS`; rung 0 is the V0 note, face 0).

    Raises:
        ZxvUsageError: ``ZXV_E_RANGE`` for a rung above 45.
    """
    out = ffi.new("uint64_t *")
    _native.check(_native.lib().zxv_coin_face(_u32(rung, "rung"), out))
    return int(out[0])


def rung(face: int) -> int:
    """The rung (1..45) whose face is exactly ``face`` (face 1 is rung 1).

    Raises:
        DenominationError: 0, or any face not on the ladder.
    """
    out = ffi.new("uint32_t *")
    _native.check(_native.lib().zxv_coin_rung(_u64(face, "face"), out))
    return int(out[0])


def split_count(amount: int) -> int:
    """How many coins :func:`split` would return, without building the list
    (an amount near 2**63 needs billions of top-rung coins).

    Raises:
        ZxvUsageError: ``ZXV_E_RANGE`` for amounts of about 7.9e18 and up.
    """
    n = ffi.new("size_t *")
    rc = _native.lib().zxv_coin_split(_u64(amount, "amount"), ffi.NULL, 0, n)
    if rc not in (0, -2):
        _native.check(rc)
    return int(n[0])


def split(amount: int) -> List[int]:
    """The coins for ``amount``: its Zeckendorf split into rungs, largest first.

    Amounts at or above the top face take ``amount // COIN_TOP_FACE`` coins of
    rung 45 first; the rest uses each lower rung at most once and never two
    neighbours, so the split is unique and the fewest coins possible.
    ``split(0) == []``. Each coin is one list item, so check
    :func:`split_count` first for amounts far above the top face.

    Raises:
        ZxvUsageError: ``ZXV_E_RANGE`` for amounts of about 7.9e18 and up.
    """
    a = _u64(amount, "amount")
    L = _native.lib()
    n = ffi.new("size_t *")
    rc = L.zxv_coin_split(a, ffi.NULL, 0, n)
    while rc == -2:
        buf = ffi.new("uint32_t[]", n[0])
        rc = L.zxv_coin_split(a, buf, n[0], n)
        if rc == 0:
            return [int(buf[i]) for i in range(n[0])]
    _native.check(rc)
    return []


# ---------------------------------------------------------------- AI tokens


@dataclass(frozen=True)
class TokenFace:
    """A token tier's face and whether it is a super token."""

    face: int
    is_super: bool


def token_face(tier: int) -> TokenFace:
    """Face of ``tier`` (0..63) and whether it is a super token.

    Raises:
        ZxvUsageError: ``ZXV_E_RANGE`` for tier >= 64.
    """
    f, s = ffi.new("uint64_t *"), ffi.new("int32_t *")
    _native.check(_native.lib().zxv_token_face(_u32(tier, "tier"), f, s))
    return TokenFace(int(f[0]), bool(s[0]))


def token_tier(face: int) -> int:
    """The tier whose face is exactly ``face``.

    Raises:
        DenominationError: not one of the 64 faces.
    """
    out = ffi.new("uint32_t *")
    _native.check(_native.lib().zxv_token_tier(_u64(face, "face"), out))
    return int(out[0])


@final
class TokenHolding:
    """An immutable holding: tokens held per tier (64 counts, each 0..2**64-1).

    Its value is ``sum(count[k] * face(k))``. Build one from a mapping
    ``{tier: count}``, a sequence of up to 64 counts, or :meth:`canonical`.
    Operations return new holdings; the value is never changed by
    :meth:`compressed`, :meth:`fused` or :meth:`split`.
    """

    __slots__ = ("_c",)
    _c: Tuple[int, ...]

    def __init__(self, counts: Union[Mapping[int, int], Sequence[int]] = ()) -> None:
        c = [0] * TOKEN_TIERS
        if isinstance(counts, Mapping):
            for tier, n in counts.items():
                t = _native.exact_int(tier, "tier", 0, TOKEN_TIERS - 1)
                c[t] = _u64(n, f"count[{t}]")
        else:
            if len(counts) > TOKEN_TIERS:
                raise _native.make_error(-8, "at most 64 counts")
            for i, n in enumerate(counts):
                c[i] = _u64(n, f"count[{i}]")
        object.__setattr__(self, "_c", tuple(c))

    @classmethod
    def _from_c(cls, arr: Any) -> "TokenHolding":
        return cls([int(arr[i]) for i in range(TOKEN_TIERS)])

    def _arr(self) -> Any:
        return ffi.new("uint64_t[64]", list(self._c))

    @classmethod
    def canonical(cls, value: int, octane: int) -> "TokenHolding":
        """The canonical form of ``value`` at ``octane``: ``value // face(octane)`` tokens
        on that tier and the rest in Zeckendorf form below it.

        Raises:
            ZxvUsageError: ``ZXV_E_RANGE`` for octane >= 64.
        """
        out = ffi.new("uint64_t[64]")
        _native.check(_native.lib().zxv_token_canonical(_u64(value, "value"), _u32(octane, "octane"), out))
        return cls._from_c(out)

    @property
    def counts(self) -> Tuple[int, ...]:
        """All 64 counts, tier 0 first."""
        return self._c

    def nonzero(self) -> Dict[int, int]:
        """``{tier: count}`` for the tiers actually held."""
        return {i: n for i, n in enumerate(self._c) if n}

    def __getitem__(self, tier: int) -> int:
        return self._c[tier]

    def _value_pieces(self) -> Tuple[int, int]:
        v, p = ffi.new("uint64_t *"), ffi.new("uint64_t *")
        _native.check(_native.lib().zxv_token_value(self._arr(), v, p))
        return int(v[0]), int(p[0])

    @property
    def value(self) -> int:
        """The value in plain tokens.

        Raises:
            ZxvUsageError: ``ZXV_E_RANGE`` when it passes 64 bits.
        """
        return self._value_pieces()[0]

    @property
    def pieces(self) -> int:
        """The number of pieces (sum of the counts)."""
        return self._value_pieces()[1]

    def is_canonical(self, octane: int) -> bool:
        """True if this is the canonical form of its value at ``octane``
        (False when a tier above the octane is used)."""
        out = ffi.new("int32_t *")
        _native.check(_native.lib().zxv_token_is_canonical(self._arr(), _u32(octane, "octane"), out))
        return bool(out[0])

    def _inplace(self, fn: Any, *args: Any) -> "TokenHolding":
        arr = self._arr()
        _native.check(fn(arr, *args))
        return TokenHolding._from_c(arr)

    def compressed(self, octane: int) -> "TokenHolding":
        """The canonical form of the same value at ``octane``."""
        return self._inplace(_native.lib().zxv_token_compress, _u32(octane, "octane"))

    def fused(self, octane: int, tier: int) -> "TokenHolding":
        """One tier-k and one tier-(k+1) token fused into one tier-(k+2) token.

        Raises:
            ZxvRefusedError: ``ZXV_E_STATE`` (tier k+2 is denser than ``octane`` allows).
            InsufficientFundsError: no tier-k or no tier-(k+1) token.
            ZxvUsageError: ``ZXV_E_RANGE`` (octane, tier > 61, or a full count).
        """
        return self._inplace(_native.lib().zxv_token_fuse, _u32(octane, "octane"), _u32(tier, "tier"))

    def split(self, tier: int) -> "TokenHolding":
        """One tier-k token split into a tier-(k-1) and a tier-(k-2) token (tier 1
        becomes two plain tokens; tier 0 is atomic).

        Raises:
            InsufficientFundsError: no tier-k token.
            ZxvUsageError: ``ZXV_E_RANGE`` (tier 0, >= 64, or a full count).
        """
        return self._inplace(_native.lib().zxv_token_split, _u32(tier, "tier"))

    def __setattr__(self, name: str, value: Any) -> None:
        raise AttributeError("TokenHolding is immutable")

    def __eq__(self, other: object) -> bool:
        return isinstance(other, TokenHolding) and other._c == self._c

    def __hash__(self) -> int:
        return hash(("TokenHolding", self._c))

    def __repr__(self) -> str:
        return f"TokenHolding({self.nonzero()!r})"


# ---------------------------------------------------------------- Fibonacci codes


def code_bits(value: int) -> int:
    """Bits in the Fibonacci code of ``value`` (2..93).

    Raises:
        ZxvUsageError: ``ZXV_E_RANGE`` for 2**64 - 1 (not encodable).
    """
    out = ffi.new("uint32_t *")
    _native.check(_native.lib().zxv_token_code_bits(_u64(value, "value"), out))
    return int(out[0])


def code_put(buf: bytearray, bit: int, value: int) -> int:
    """Write the Fibonacci code of ``value`` into ``buf`` at bit ``bit`` (bit i is
    bit i%8 of byte i//8) and return the bit after it. Only the code's bits
    are written. Each code ends in ``11``, so it needs no length field.

    Raises:
        ZxvUsageError: ``ZXV_E_BUFFER`` (it does not fit; nothing written) or
            ``ZXV_E_RANGE``.
    """
    if not isinstance(buf, bytearray):
        raise TypeError("buf must be a bytearray")
    pos = ffi.new("uint32_t *", _u32(bit, "bit"))
    v = _u64(value, "value")
    cb = ffi.from_buffer("uint8_t[]", buf, require_writable=True) if buf else ffi.NULL
    _native.check(_native.lib().zxv_token_code_put(cb, len(buf), pos, v))
    return int(pos[0])


def code_get(data: bytes, bit: int = 0) -> Tuple[int, int]:
    """Read one Fibonacci code at bit ``bit``; returns ``(value, next_bit)``.

    Raises:
        ZxvUsageError: ``ZXV_E_FORMAT`` (no closing ``11``, or past 64 bits)
            or ``ZXV_E_RANGE`` (``bit`` past the end).
    """
    d = _native.to_bytes(data, "data")
    pos = ffi.new("uint32_t *", _u32(bit, "bit"))
    out = ffi.new("uint64_t *")
    _native.check(_native.lib().zxv_token_code_get(_native.cbuf(d), len(d), pos, out))
    return int(out[0]), int(pos[0])


def code_encode(values: Iterable[int]) -> Tuple[bytes, int]:
    """Fibonacci-code ``values`` one after another; returns ``(bytes, bit_count)``."""
    vals = [_u64(v, "value") for v in values]
    total = sum(code_bits(v) for v in vals)
    buf = bytearray((total + 7) // 8)
    bit = 0
    for v in vals:
        bit = code_put(buf, bit, v)
    return bytes(buf), bit


def code_decode(data: bytes, count: Optional[int] = None, bit: int = 0) -> List[int]:
    """Read ``count`` Fibonacci codes (or every whole code until the zero padding
    at the end) starting at bit ``bit``."""
    d = _native.to_bytes(data, "data")
    out: List[int] = []
    end = len(d) * 8
    while (count is None or len(out) < count) and bit < end:
        if count is None and not any(d[bit // 8 + 1:]) and (d[bit // 8] >> (bit % 8)) == 0:
            break
        v, bit = code_get(d, bit)
        out.append(v)
    return out


# ---------------------------------------------------------------- token wallet


@dataclass(frozen=True)
class WalletInfo:
    """A wallet's label, octane, value and piece count."""

    model_id: int
    octane: int
    value: int
    pieces: int


class TokenWallet(NativeHandle):
    """A token holding kept canonical at its own octane (``zxv_token_wallet``).

    Every change needs ``Cap.MINT`` (not ``Cap.SWARM``: the wallet is not tied
    to a swarm budget, and tokens enter it only by :meth:`mint`). Reads need
    no capability. Each changing method uses the wallet's own capability
    unless you pass another one (``cap=``) from the same context. Internally
    serialized: any thread may use it. Fails closed once its context halts.

    Args:
        cap: a capability holding ``Cap.MINT``.
        model_id: a free label, e.g. the swarm model that holds it.
        octane: 0..63 (see :class:`Octane`).

    Raises:
        CapabilityDeniedError: the capability lacks ``Cap.MINT``.
    """

    def __init__(self, cap: Capability, model_id: int = 0, octane: int = Octane.PLAIN) -> None:
        ctx = cap.context
        out = ffi.new("zxv_token_wallet **")
        _native.check(_native.lib().zxv_token_wallet_create(cap._handle(), _u32(model_id, "model_id"),
                                                            _u32(octane, "octane"), out), ctx)
        self._cap = cap
        super().__init__(out[0], cap, ctx)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_token_wallet_destroy(handle)

    def _c(self, cap: Optional[Capability]) -> Any:
        return (cap or self._cap)._handle()

    def mint(self, amount: int, *, cap: Optional[Capability] = None) -> None:
        """Create ``amount`` new tokens in this wallet.

        Raises:
            ZxvUsageError: ``ZXV_E_RANGE`` (the value would pass 64 bits; nothing changes).
        """
        a = _u64(amount, "amount")
        self._check(_native.lib().zxv_token_wallet_mint(self._c(cap), self._handle(), a))

    def spend(self, amount: int, *, cap: Optional[Capability] = None) -> None:
        """Spend (burn) ``amount``; the change stays canonical.

        Raises:
            InsufficientFundsError: the wallet holds less (nothing changes).
        """
        a = _u64(amount, "amount")
        self._check(_native.lib().zxv_token_wallet_spend(self._c(cap), self._handle(), a))

    def transfer(self, to: "TokenWallet", amount: int, *, cap: Optional[Capability] = None) -> None:
        """Move ``amount`` to wallet ``to``. The payer keeps its change at its octane;
        the receiver gets the payment at ITS octane; the total is conserved.

        Raises:
            InsufficientFundsError: this wallet holds less.
            CapabilityDeniedError: a wallet or the cap from another context.
        """
        if not isinstance(to, TokenWallet):
            raise TypeError("to must be a TokenWallet")
        a = _u64(amount, "amount")
        self._check(_native.lib().zxv_token_wallet_transfer(self._c(cap), self._handle(), to._handle(), a))

    def set_octane(self, octane: int, *, cap: Optional[Capability] = None) -> None:
        """Change the octane and re-express the same value canonically."""
        o = _u32(octane, "octane")
        self._check(_native.lib().zxv_token_wallet_set_octane(self._c(cap), self._handle(), o))

    @property
    def info(self) -> WalletInfo:
        """Label, octane, value and pieces (no capability needed)."""
        m, o, v, p = (ffi.new("uint32_t *"), ffi.new("uint32_t *"), ffi.new("uint64_t *"),
                      ffi.new("uint64_t *"))
        self._check(_native.lib().zxv_token_wallet_info(self._handle(), m, o, v, p))
        return WalletInfo(int(m[0]), int(o[0]), int(v[0]), int(p[0]))

    @property
    def octane(self) -> int:
        return self.info.octane

    @property
    def value(self) -> int:
        return self.info.value

    @property
    def holding(self) -> TokenHolding:
        """The counts per tier (always canonical at :attr:`octane`)."""
        out = ffi.new("uint64_t[64]")
        self._check(_native.lib().zxv_token_wallet_counts(self._handle(), out))
        return TokenHolding._from_c(out)
