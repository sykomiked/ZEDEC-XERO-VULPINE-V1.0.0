# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Vino Embed: play-lane vouchers, payment requests and receipts (ABI 1.6,
group ``vino``).

What an app or game needs to take part in Vino **without ever holding
anyone's value or keys**. There are two lanes, and they never meet:

PLAY
    A sandbox inside your app (:class:`VinoPlay`). It has its own play units
    (asset ``"PLAY"``, never VFV), its own issuers and holders, and
    Fibonacci-face vouchers. It lives in process memory only and is lost
    when closed. No call takes a play sandbox together with anything live,
    so a play unit can never be converted into, redeemed for, or moved into
    live value. Play units are not money. Games may use them freely.

LIVE
    Real Vino, held in the user's own ZXV wallet, never in your app. Your app
    builds a :class:`PaymentRequest`; the user approves it in their wallet,
    which signs a :class:`Receipt` with its ML-DSA-87 key; your app checks
    the receipt offline with :meth:`Receipt.verify`. Your app never sees a
    balance, a key or a custody account.

Vouchers (play lane)
    A voucher is one coin. Its face is a Fibonacci ladder face (see
    :func:`zxv.coin.face`: 1, 2, 3, 5, 8, 13, ...). It is held by one holder
    on exactly ONE active rail at a time: :attr:`VinoRail.BEARER` (a note in
    the holder's hand) or :attr:`VinoRail.GHOST` (its twin on the play
    ledger). A transfer must name the active rail (else :class:`~zxv.RailError`),
    so one voucher can never be spent on both. Switching rails moves nothing.

Coverage
    An issuer posts play backing (:meth:`VinoPlay.back`) and may only issue
    while its backing stays at least 1.8 times everything it has
    outstanding, checked with exact integers: ``10 * backing >= 18 *
    outstanding``. An issue or a backing withdrawal that would break this is
    refused (:class:`~zxv.CoverageError`). Redeeming a voucher returns it to
    its issuer and retires it: the issuer's outstanding falls and **nothing
    is paid out to anyone**. No call turns a voucher into backing, play units
    or anything else, and there is no interest, yield or return of any kind.

Requests and receipts (both lanes)
    A request is a fixed :data:`VINO_REQ_BYTES`-byte canonical record; its id
    is the SHA-256 of those bytes. A receipt is the wallet's ML-DSA-87
    signature (empty FIPS 204 context) over :data:`VINO_RCPT_MSG_BYTES` bytes
    (:meth:`Receipt.message`). A receipt for an expired request, or one
    approved after expiry, is :class:`~zxv.RequestExpiredError`. A LIVE
    request marked as a paid chance outcome (loot boxes, paid draws) is
    refused (:class:`~zxv.ChanceOutcomeError`). The allotment is a separate
    module, not a payment request.

Daily cap
    A deployment may set a per-device daily cap. :class:`SpendMeter` counts
    what one device approved per day and refuses an amount that would pass
    the cap (:class:`~zxv.LimitRefusedError`). It is a counter, not a legal
    check, and makes no exemption claim.

Capabilities
    Every play-lane call and the meter need ``Cap.VINO`` (else
    :class:`~zxv.CapabilityDeniedError`; a halted context gives
    :class:`~zxv.ZxvFatal` ``ZXV_E_POISONED``). Building a receipt message
    and verifying a receipt are pure and need no capability. Signing a
    receipt needs ``Cap.CRYPTO_SIGN``: it is the wallet's call, not the app's.
    Each method uses its handle's capability unless you pass another one
    (``cap=``) from the same context.

Threading
    A :class:`VinoPlay` or :class:`SpendMeter` may be used from several
    threads; the C library serializes its calls with an internal lock.

Not in this group, by design: minting live Vino, setting pegs, V-Bill
auctions or bids, V-Bonds and derivatives, and every operator or steward
action. They are never app-facing.

Honest limits: play units are not money and are lost when the sandbox is
closed. The LIVE flow here is the app's half only: approving a request
happens in the user's wallet (``zxvd``), which posts the payment on its own
ledger. ML-DSA-87 here is the reference implementation, not FIPS 140-3
validated. Whether a given deployment of Vino is store credit, a prepaid
access product or something else is a legal question pending counsel;
nothing in this SDK decides it.
"""

from __future__ import annotations

import enum
from dataclasses import dataclass
from typing import Any, List, Optional, Tuple, Union

from . import _native
from ._handle import NativeHandle
from ._native import ffi
from .context import Capability
from .crypto import SecretKey, _secret
from .errors import LimitRefusedError

__all__ = [
    "VINO_MAX_ACCOUNTS", "VINO_MAX_VOUCHERS", "VINO_AMOUNT_MAX", "VINO_REQ_BYTES", "VINO_REQ_ID_BYTES",
    "VINO_MEMO_MAX", "VINO_RCPT_MSG_BYTES", "MLDSA87_PK_BYTES", "MLDSA87_SK_BYTES", "MLDSA87_SIG_BYTES",
    "VinoRail", "VinoLane", "VoucherState", "RequestFlag", "Voucher", "PlayAccount", "VinoPlay",
    "PaymentRequest", "Receipt", "SpendMeter",
]

VINO_MAX_ACCOUNTS = 256
"""Issuers plus holders one play sandbox can hold."""
VINO_MAX_VOUCHERS = 4096
"""Active (unredeemed) vouchers one play sandbox can hold."""
VINO_AMOUNT_MAX = 1 << 59
"""Largest single amount (backing, request amount, meter amount or cap)."""
VINO_REQ_BYTES = 160
"""Length of an encoded payment request."""
VINO_REQ_ID_BYTES = 32
"""Length of a request id (SHA-256 of the encoded request)."""
VINO_MEMO_MAX = 64
"""Longest request memo, in printable ASCII characters (0x20..0x7e)."""
VINO_RCPT_MSG_BYTES = 88
"""Length of the message a wallet signs: 16-byte tag + request id + SHA-256
of the wallet public key + approval time (8 bytes little-endian)."""
MLDSA87_PK_BYTES = 2592
"""Length of an ML-DSA-87 public key."""
MLDSA87_SK_BYTES = 4896
"""Length of an ML-DSA-87 secret key."""
MLDSA87_SIG_BYTES = 4627
"""Length of an ML-DSA-87 signature (a receipt)."""


def _u64(v: Any, what: str) -> int:
    return _native.exact_int(v, what, *_native.U64)


def _u32(v: Any, what: str) -> int:
    return _native.exact_int(v, what, *_native.U32)


def _i32(v: Any, what: str) -> int:
    return _native.exact_int(v, what, *_native.I32)


class VinoRail(enum.IntEnum):
    """Where a play voucher currently lives. Exactly one rail is active."""

    BEARER = 0
    """The note in the holder's hand (``ZXV_VINO_RAIL_BEARER``)."""
    GHOST = 1
    """Its twin on the play ledger (``ZXV_VINO_RAIL_GHOST``)."""


class VinoLane(enum.IntEnum):
    """Which lane a payment request belongs to. The two lanes never meet."""

    PLAY = 1
    """The in-app sandbox: amounts are play units, which are not money."""
    LIVE = 2
    """Real Vino, held in the user's own wallet: amounts are VFV minor units."""


class VoucherState(enum.IntEnum):
    """A play voucher's state (``ZXV_VINO_V_*``)."""

    ACTIVE = 1
    """Held by a holder; may be transferred, switched or redeemed."""
    REDEEMED = 2
    """Returned to its issuer and retired. Nothing was paid out; it is kept
    listed by id but can no longer be used."""


class RequestFlag(enum.IntFlag):
    """Flags of a payment request (``ZXV_VINO_REQ_*``)."""

    NONE = 0
    """An ordinary purchase."""
    CHANCE = 0x1
    """The purchase has a paid chance outcome (a loot box, a paid draw).
    Allowed on the PLAY lane only: a LIVE request with this flag is refused
    (:class:`~zxv.ChanceOutcomeError`)."""


@dataclass(frozen=True)
class Voucher:
    """One play voucher, as :meth:`VinoPlay.voucher` reports it."""

    id: int
    """Its id: above 0, never reused in its sandbox."""
    face: int
    """Its Fibonacci ladder face, in play units."""
    issuer: int
    """The issuer account that issued it."""
    holder: int
    """The holder account that holds it (the last holder, once redeemed)."""
    rail: VinoRail
    """Its active rail."""
    state: VoucherState
    """Active or redeemed."""


@dataclass(frozen=True)
class PlayAccount:
    """One play-sandbox account, as :meth:`VinoPlay.account` reports it."""

    account: int
    """The account number (counts up from 0)."""
    owner: int
    """The owner id given when it was opened."""
    is_issuer: bool
    """True for an issuer, False for a holder."""
    backing: int
    """An issuer's play backing; always 0 for a holder."""
    faces: int
    """An issuer's outstanding faces, or the faces a holder holds."""
    count: int
    """Active vouchers it issued (issuer) or holds (holder)."""


class VinoPlay(NativeHandle):
    """One play-lane sandbox (``zxv_vino_play``): issuers, holders and vouchers
    of play units, in process memory only.

    Play units are not money, have no value, and can never become live
    value: nothing in the SDK connects this sandbox to a wallet or a ledger.
    Everything here is lost when the sandbox is closed. A refused call
    changes nothing.

    Use it as a context manager, or call :meth:`close`. Closing its capability
    or context closes it first.

    Args:
        cap: a capability holding ``Cap.VINO``; every method uses it unless a
            call passes another one (``cap=``) from the same context.

    Raises:
        CapabilityDeniedError: ``cap`` lacks ``Cap.VINO``.
        ZxvFatal: the context is halted (``ZXV_E_POISONED``).
    """

    def __init__(self, cap: Capability) -> None:
        ctx = cap.context
        out = ffi.new("zxv_vino_play **")
        _native.check(_native.lib().zxv_vino_play_create(cap._handle(), out), ctx)
        self._cap = cap
        super().__init__(out[0], cap, ctx)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_vino_play_destroy(handle)

    def _c(self, cap: Optional[Capability]) -> Any:
        return (cap or self._cap)._handle()

    # ------------------------------------------------------------ accounts

    def open_account(self, owner: int, issuer: bool, *, cap: Optional[Capability] = None) -> int:
        """Open an account for ``owner`` and return its account number.

        Account numbers count up from 0 in opening order.

        Args:
            owner: an id of your choosing (0..2**32-1); owners need not be
                distinct.
            issuer: True opens an issuer (which can back and issue), False a
                holder (which can hold, spend and redeem vouchers).

        Raises:
            ZxvRefusedError: ``ZXV_E_CAPACITY`` (:data:`VINO_MAX_ACCOUNTS` open).
            ZxvUsageError: ``ZXV_E_ARG`` (``issuer`` not True/False).
        """
        flag = int(issuer) if isinstance(issuer, bool) else _i32(issuer, "issuer")
        out = ffi.new("uint32_t *")
        self._check(_native.lib().zxv_vino_play_open(self._c(cap), self._handle(), _u32(owner, "owner"),
                                                     flag, out))
        return int(out[0])

    def open_issuer(self, owner: int, *, cap: Optional[Capability] = None) -> int:
        """Open an issuer account for ``owner``; same as ``open_account(owner, True)``."""
        return self.open_account(owner, True, cap=cap)

    def open_holder(self, owner: int, *, cap: Optional[Capability] = None) -> int:
        """Open a holder account for ``owner``; same as ``open_account(owner, False)``."""
        return self.open_account(owner, False, cap=cap)

    def back(self, issuer: int, amount: int, *, cap: Optional[Capability] = None) -> None:
        """Post ``amount`` play units of backing to an issuer.

        This is the play faucet: play units come from nowhere and are worth
        nothing. Backing only lets the issuer issue more vouchers.

        Raises:
            ZxvUsageError: ``ZXV_E_ARG`` (not an issuer), ``ZXV_E_NOT_FOUND``
                (no such account), ``ZXV_E_RANGE`` (0, above
                :data:`VINO_AMOUNT_MAX`, or backing would pass 2**62).
        """
        self._check(_native.lib().zxv_vino_play_back(self._c(cap), self._handle(), _u32(issuer, "issuer"),
                                                     _u64(amount, "amount")))

    def unback(self, issuer: int, amount: int, *, cap: Optional[Capability] = None) -> None:
        """Withdraw ``amount`` of an issuer's play backing, only while it stays at
        1.8x coverage of what it has outstanding. A refusal changes nothing.

        Raises:
            CoverageError: the issuer would fall below 1.8x coverage.
            InsufficientFundsError: ``ZXV_E_FUNDS`` (more than its backing).
            ZxvUsageError: as :meth:`back`.
        """
        self._check(_native.lib().zxv_vino_play_unback(self._c(cap), self._handle(), _u32(issuer, "issuer"),
                                                       _u64(amount, "amount")))

    # ------------------------------------------------------------ vouchers

    def issue(self, issuer: int, holder: int, face: int, rail: VinoRail = VinoRail.BEARER, *,
              cap: Optional[Capability] = None) -> int:
        """Issue one voucher of Fibonacci face ``face`` from ``issuer`` to ``holder``
        on ``rail``; returns its id (above 0, never reused in this sandbox).

        Raises:
            DenominationError: ``face`` is not a ladder face (split it with
                :func:`zxv.coin.split` and issue one voucher per coin).
            CoverageError: the issuer would fall below 1.8x coverage.
            ZxvUsageError: ``ZXV_E_ARG`` (``issuer`` not an issuer, ``holder``
                not a holder, bad rail), ``ZXV_E_NOT_FOUND``.
            ZxvRefusedError: ``ZXV_E_CAPACITY`` (:data:`VINO_MAX_VOUCHERS` active).
        """
        out = ffi.new("uint64_t *")
        self._check(_native.lib().zxv_vino_issue(self._c(cap), self._handle(), _u32(issuer, "issuer"),
                                                 _u32(holder, "holder"), _u64(face, "face"),
                                                 _i32(rail, "rail"), out))
        return int(out[0])

    def transfer(self, voucher: int, from_holder: int, to_holder: int, rail: VinoRail, *,
                 cap: Optional[Capability] = None) -> None:
        """Spend an active voucher: move it from its holder ``from_holder`` to
        holder ``to_holder``. ``rail`` must be its active rail, which does not change.

        Raises:
            RailError: ``rail`` is not the voucher's active rail.
            ZxvRefusedError: ``ZXV_E_STATE`` (already redeemed).
            ZxvUsageError: ``ZXV_E_ARG`` (``from_holder`` is not its holder,
                ``to_holder`` not a holder, the two are equal, bad rail),
                ``ZXV_E_NOT_FOUND`` (no such voucher or account).
        """
        self._check(_native.lib().zxv_vino_transfer(self._c(cap), self._handle(), _u64(voucher, "voucher"),
                                                    _u32(from_holder, "from_holder"),
                                                    _u32(to_holder, "to_holder"), _i32(rail, "rail")))

    def set_rail(self, voucher: int, holder: int, rail: VinoRail, *, cap: Optional[Capability] = None) -> None:
        """Switch an active voucher, by its holder, to ``rail`` (BEARER <-> GHOST).
        Switching moves nothing; the voucher stays with the same holder.

        Raises:
            RailError: it is already on ``rail``.
            ZxvRefusedError: ``ZXV_E_STATE`` (redeemed).
            ZxvUsageError: ``ZXV_E_ARG`` (not its holder, bad rail), ``ZXV_E_NOT_FOUND``.
        """
        self._check(_native.lib().zxv_vino_set_rail(self._c(cap), self._handle(), _u64(voucher, "voucher"),
                                                    _u32(holder, "holder"), _i32(rail, "rail")))

    def redeem(self, voucher: int, holder: int, *, cap: Optional[Capability] = None) -> None:
        """Redeem an active voucher, by its holder, to its issuer.

        The voucher is retired and the issuer's outstanding falls by its face.
        Nothing is paid out to anyone: redeeming does not produce play units,
        backing or live value.

        Raises:
            ZxvRefusedError: ``ZXV_E_STATE`` (already redeemed).
            ZxvUsageError: ``ZXV_E_ARG`` (not its holder), ``ZXV_E_NOT_FOUND``.
        """
        self._check(_native.lib().zxv_vino_redeem(self._c(cap), self._handle(), _u64(voucher, "voucher"),
                                                  _u32(holder, "holder")))

    # ------------------------------------------------------------ reads

    def voucher(self, voucher: int, *, cap: Optional[Capability] = None) -> Voucher:
        """A voucher's face, issuer, holder, active rail and state.

        Raises:
            ZxvUsageError: ``ZXV_E_NOT_FOUND`` (no voucher with that id).
        """
        vid = _u64(voucher, "voucher")
        face = ffi.new("uint64_t *")
        iss, hold = ffi.new("uint32_t *"), ffi.new("uint32_t *")
        rail, state = ffi.new("int32_t *"), ffi.new("int32_t *")
        self._check(_native.lib().zxv_vino_voucher_info(self._c(cap), self._handle(), vid, face, iss, hold,
                                                        rail, state))
        return Voucher(vid, int(face[0]), int(iss[0]), int(hold[0]), VinoRail(rail[0]), VoucherState(state[0]))

    def account(self, account: int, *, cap: Optional[Capability] = None) -> PlayAccount:
        """An account's owner, kind and totals.

        Raises:
            ZxvUsageError: ``ZXV_E_NOT_FOUND`` (no such account).
        """
        acct = _u32(account, "account")
        owner, count = ffi.new("uint32_t *"), ffi.new("uint32_t *")
        is_issuer = ffi.new("int32_t *")
        backing, faces = ffi.new("uint64_t *"), ffi.new("uint64_t *")
        self._check(_native.lib().zxv_vino_account_info(self._c(cap), self._handle(), acct, owner, is_issuer,
                                                        backing, faces, count))
        return PlayAccount(acct, int(owner[0]), bool(is_issuer[0]), int(backing[0]), int(faces[0]),
                           int(count[0]))

    def vouchers(self, account: int, *, cap: Optional[Capability] = None) -> List[int]:
        """The ids of the active vouchers an account issued (issuer) or holds
        (holder), ascending. Redeemed vouchers are not listed.

        Raises:
            ZxvUsageError: ``ZXV_E_NOT_FOUND`` (no such account).
        """
        acct = _u32(account, "account")
        L = _native.lib()
        n = ffi.new("size_t *")
        size = 16
        while True:
            ids = ffi.new("uint64_t[]", size)
            rc = L.zxv_vino_vouchers(self._c(cap), self._handle(), acct, ids, size, n)
            if rc == 0:
                return [int(ids[i]) for i in range(n[0])]
            if rc != -2:  # anything but ZXV_E_BUFFER
                self._check(rc)
            size = max(int(n[0]), size + 1)

    def check(self, *, cap: Optional[Capability] = None) -> None:
        """Re-check every invariant of the sandbox: each active voucher has a
        ladder face, one rail, an issuer and a holder; each issuer's
        outstanding equals the sum of its active faces and keeps 1.8x
        coverage; each holder's total equals what it holds.

        Raises:
            ZxvFatal: an invariant is broken (``ZXV_E_INVARIANT``; the context
                is now halted).
        """
        self._check(_native.lib().zxv_vino_play_check(self._c(cap), self._handle()))


# ---------------------------------------------------------------- payment requests


@dataclass(frozen=True)
class PaymentRequest:
    """A payment request: what the app asks the user's wallet to approve.

    A plain value. :meth:`encode` gives its canonical :data:`VINO_REQ_BYTES`
    bytes, which is what travels to the wallet (for example in a QR code or a
    deep link), and :attr:`id` the SHA-256 of those bytes. :meth:`decode`
    reads and validates bytes back. Both are pure and need no capability.
    Fields are checked when the request is encoded, not when it is built.

    Encoded layout (offsets in bytes, integers little-endian): 0 ``"ZXVR"``,
    4 version (1), 5 lane, 6 flags (2), 8 merchant (32), 40 nonce (16),
    56 amount (8), 64 created_at (8), 72 expires_at (8), 80 memo length (1),
    81 memo (NUL padded to 64), 145 zero (15).

    Example::

        req = PaymentRequest(VinoLane.LIVE, merchant_id, nonce, 1_250,
                             created_at=now, expires_at=now + 600, memo="Sword of Dawn")
        data = req.encode()           # 160 bytes for the wallet
        assert PaymentRequest.decode(data) == req
    """

    lane: VinoLane
    """:attr:`VinoLane.LIVE` for real Vino in the user's wallet, or
    :attr:`VinoLane.PLAY` for play units."""
    merchant: bytes
    """The payee's 32-byte id (for example the SHA-256 of its public key)."""
    nonce: bytes
    """16 bytes, unique per request (for example from :func:`zxv.random_bytes`)."""
    amount: int
    """1..:data:`VINO_AMOUNT_MAX`: VFV minor units on the LIVE lane, play units
    on the PLAY lane."""
    created_at: int
    """When the request was made, in seconds of your clock (Unix time is the
    convention)."""
    expires_at: int
    """When it expires; must be after ``created_at``. A wallet may approve it
    only within ``[created_at, expires_at]``."""
    memo: str = ""
    """0..:data:`VINO_MEMO_MAX` printable ASCII characters (space to ``~``),
    shown to the user."""
    flags: RequestFlag = RequestFlag.NONE
    """:attr:`RequestFlag.CHANCE` for a paid chance outcome (PLAY lane only)."""

    def _encode(self) -> Tuple[bytes, bytes]:
        merchant = _native.fixed_bytes(self.merchant, 32, "merchant")
        nonce = _native.fixed_bytes(self.nonce, 16, "nonce")
        memo = _native.to_cstr(self.memo, "memo")
        out = ffi.new("uint8_t[160]")
        out_id = ffi.new("uint8_t[32]")
        _native.check(_native.lib().zxv_vino_request_encode(
            _i32(self.lane, "lane"), _u32(self.flags, "flags"), _native.cbuf(merchant), _native.cbuf(nonce),
            _u64(self.amount, "amount"), _u64(self.created_at, "created_at"), _u64(self.expires_at, "expires_at"),
            memo, out, out_id))
        return bytes(ffi.buffer(out, VINO_REQ_BYTES)), bytes(ffi.buffer(out_id, VINO_REQ_ID_BYTES))

    def encode(self) -> bytes:
        """The canonical :data:`VINO_REQ_BYTES` bytes of this request.

        Raises:
            LaneError: ``lane`` is not PLAY or LIVE.
            ChanceOutcomeError: a LIVE request with :attr:`RequestFlag.CHANCE`.
            ZxvUsageError: ``ZXV_E_RANGE`` (amount 0 or above
                :data:`VINO_AMOUNT_MAX`, ``expires_at <= created_at``),
                ``ZXV_E_FORMAT`` (an unknown flag, a memo that is not printable
                ASCII), ``ZXV_E_TOO_LONG`` (memo above 64 characters),
                ``ZXV_E_ARG`` (merchant not 32 bytes, nonce not 16 bytes, a
                NUL in the memo).
        """
        return self._encode()[0]

    @property
    def id(self) -> bytes:
        """The request id: SHA-256 of :meth:`encode` (32 bytes). A wallet's
        receipt names the request by this id. Raises as :meth:`encode`."""
        return self._encode()[1]

    @classmethod
    def decode(cls, data: bytes) -> "PaymentRequest":
        """Read and validate encoded request bytes.

        The same rules as :meth:`encode` apply, plus the magic, the version
        and the zero padding. ``decode(r.encode()) == r`` for every valid ``r``.

        Raises:
            ZxvUsageError: ``ZXV_E_ARG`` (not exactly :data:`VINO_REQ_BYTES`
                bytes), ``ZXV_E_FORMAT``, ``ZXV_E_RANGE``, ``ZXV_E_TOO_LONG``.
            LaneError: the lane byte is not 1 or 2.
            ChanceOutcomeError: a LIVE request marked as a paid chance outcome.
        """
        raw = _native.fixed_bytes(data, VINO_REQ_BYTES, "data")
        lane, flags = ffi.new("int32_t *"), ffi.new("uint32_t *")
        merchant, nonce = ffi.new("uint8_t[32]"), ffi.new("uint8_t[16]")
        amount, created, expires = ffi.new("uint64_t *"), ffi.new("uint64_t *"), ffi.new("uint64_t *")
        memo = ffi.new("char[]", VINO_MEMO_MAX + 1)
        _native.check(_native.lib().zxv_vino_request_decode(_native.cbuf(raw), lane, flags, merchant, nonce,
                                                            amount, created, expires, memo, ffi.NULL))
        return cls(VinoLane(lane[0]), bytes(ffi.buffer(merchant, 32)), bytes(ffi.buffer(nonce, 16)),
                   int(amount[0]), int(created[0]), int(expires[0]), ffi.string(memo).decode("ascii"),
                   RequestFlag(flags[0]))


def _request_bytes(request: Union[bytes, PaymentRequest]) -> bytes:
    if isinstance(request, PaymentRequest):
        return request.encode()
    return _native.fixed_bytes(request, VINO_REQ_BYTES, "request")


# ---------------------------------------------------------------- receipts


@dataclass(frozen=True)
class Receipt:
    """A wallet's approval of one payment request: its ML-DSA-87 signature over
    :meth:`message` (empty FIPS 204 context).

    The app receives a receipt from the user's wallet and checks it offline
    with :meth:`verify`; it never needs the wallet's secret key, balance or
    ledger. A receipt says the wallet approved the request; posting the
    payment happens on the wallet's own ledger, not here.

    ML-DSA-87 here is the reference implementation, not FIPS 140-3 validated.
    """

    request: bytes
    """The encoded request (:data:`VINO_REQ_BYTES` bytes) that was approved."""
    wallet_public_key: bytes
    """The approving wallet's ML-DSA-87 public key (:data:`MLDSA87_PK_BYTES`)."""
    approved_at: int
    """When the wallet approved it, in seconds of the caller's clock (Unix
    time is the convention); within the request's ``[created_at, expires_at]``."""
    signature: bytes
    """The wallet's signature (:data:`MLDSA87_SIG_BYTES` bytes)."""

    def __post_init__(self) -> None:
        object.__setattr__(self, "request", _request_bytes(self.request))
        object.__setattr__(self, "wallet_public_key",
                           _native.fixed_bytes(self.wallet_public_key, MLDSA87_PK_BYTES, "wallet_public_key"))
        object.__setattr__(self, "approved_at", _u64(self.approved_at, "approved_at"))
        object.__setattr__(self, "signature",
                           _native.fixed_bytes(self.signature, MLDSA87_SIG_BYTES, "signature"))

    @staticmethod
    def message(request_id: bytes, wallet_public_key: bytes, approved_at: int) -> bytes:
        """The exact :data:`VINO_RCPT_MSG_BYTES` bytes a wallet signs to approve
        request ``request_id`` with ``wallet_public_key`` at ``approved_at``:
        ``"zxv/vino-rcpt/v1"`` || request id || SHA-256(wallet public key) ||
        approved_at (8 bytes little-endian). Pure; needs no capability.

        Raises:
            ZxvUsageError: ``ZXV_E_ARG`` (wrong lengths).
        """
        rid = _native.fixed_bytes(request_id, VINO_REQ_ID_BYTES, "request_id")
        pk = _native.fixed_bytes(wallet_public_key, MLDSA87_PK_BYTES, "wallet_public_key")
        out = ffi.new("uint8_t[88]")
        _native.check(_native.lib().zxv_vino_receipt_msg(_native.cbuf(rid), _native.cbuf(pk),
                                                         _u64(approved_at, "approved_at"), out))
        return bytes(ffi.buffer(out, VINO_RCPT_MSG_BYTES))

    @property
    def payment_request(self) -> PaymentRequest:
        """The approved request, decoded."""
        return PaymentRequest.decode(self.request)

    @classmethod
    def sign(cls, cap: Capability, request: Union[bytes, PaymentRequest], wallet_secret_key: SecretKey, wallet_public_key: bytes,
             approved_at: int) -> "Receipt":
        """Wallet side: approve ``request`` at ``approved_at`` and sign it.

        This is the user's wallet's call, not the app's: an app never holds a
        wallet secret key. The key stays in its :class:`~zxv.SecretKey`; it is
        not copied or kept by the receipt. Signing is hedged (randomized), so
        two receipts of the same approval differ but both verify.

        Args:
            cap: a capability holding ``Cap.CRYPTO_SIGN``.
            request: the request, or its encoded :data:`VINO_REQ_BYTES` bytes.
            wallet_secret_key: the wallet's ML-DSA-87 secret key
                (:data:`MLDSA87_SK_BYTES`).
            wallet_public_key: its public key (:data:`MLDSA87_PK_BYTES`).
            approved_at: the approval time, within the request's
                ``[created_at, expires_at]``.

        Raises:
            RequestExpiredError: ``approved_at`` is outside the request's window.
            CapabilityDeniedError: no ``Cap.CRYPTO_SIGN``.
            ZxvUsageError: the request bytes do not decode (``ZXV_E_FORMAT``,
                ``ZXV_E_RANGE``, ...), or ``ZXV_E_ARG``.
            ZxvNotImplementedError: a library built without post-quantum crypto.
        """
        req = _request_bytes(request)
        pk = _native.fixed_bytes(wallet_public_key, MLDSA87_PK_BYTES, "wallet_public_key")
        when = _u64(approved_at, "approved_at")
        sig = ffi.new("uint8_t[]", MLDSA87_SIG_BYTES)
        _native.check(_native.lib().zxv_vino_receipt_sign(
            cap._handle(), _native.cbuf(req), _secret(wallet_secret_key, MLDSA87_SK_BYTES, "wallet_secret_key"),
            _native.cbuf(pk), when, sig), cap.context)
        return cls(req, pk, when, bytes(ffi.buffer(sig, MLDSA87_SIG_BYTES)))

    def verify(self, now: int) -> None:
        """App side, offline: check that this is the wallet's approval of the
        request at :attr:`approved_at`, that :attr:`approved_at` lies within
        the request's window, and that ``now`` is not past its expiry.
        Returns normally when it is; pure, needs no capability.

        Args:
            now: the current time on the same clock as the request.

        Raises:
            VerificationFailedError: not that wallet's signature over this
                request and time.
            RequestExpiredError: the request has expired at ``now``, or was
                approved outside its window.
            ZxvUsageError: the request bytes do not decode.
            ZxvNotImplementedError: a library built without post-quantum crypto.
        """
        _native.check(_native.lib().zxv_vino_receipt_verify(
            _native.cbuf(self.request), _native.cbuf(self.wallet_public_key), self.approved_at,
            _native.cbuf(self.signature), _u64(now, "now")))

    def is_valid(self, now: int) -> bool:
        """True if :meth:`verify` passes at ``now``; False if the signature does
        not verify or the request has expired. Other errors (bad request bytes,
        no post-quantum build) still raise."""
        rc = _native.lib().zxv_vino_receipt_verify(
            _native.cbuf(self.request), _native.cbuf(self.wallet_public_key), self.approved_at,
            _native.cbuf(self.signature), _u64(now, "now"))
        if rc in (-21, -49):  # ZXV_E_VERIFY, ZXV_E_EXPIRED
            return False
        _native.check(rc)
        return True


# ---------------------------------------------------------------- the daily meter


class SpendMeter(NativeHandle):
    """One device's daily spend counter (``zxv_vino_meter``) with a fixed daily cap.

    :meth:`admit` counts an amount against a day index and refuses one that
    would take the day's total past the cap. It is a counter, not a legal
    check, and makes no exemption claim.

    Args:
        cap: a capability holding ``Cap.VINO``.
        daily_cap: the most one day may admit, 1..:data:`VINO_AMOUNT_MAX`.

    Raises:
        CapabilityDeniedError: ``cap`` lacks ``Cap.VINO``.
        ZxvUsageError: ``ZXV_E_RANGE`` (``daily_cap`` out of range).
    """

    def __init__(self, cap: Capability, daily_cap: int) -> None:
        ctx = cap.context
        limit = _u64(daily_cap, "daily_cap")
        out = ffi.new("zxv_vino_meter **")
        _native.check(_native.lib().zxv_vino_meter_create(cap._handle(), limit, out), ctx)
        self._cap = cap
        self._daily_cap = limit
        super().__init__(out[0], cap, ctx)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_vino_meter_destroy(handle)

    @property
    def daily_cap(self) -> int:
        """The daily cap this meter was created with."""
        return self._daily_cap

    def admit(self, day: int, amount: int, *, cap: Optional[Capability] = None) -> int:
        """Count ``amount`` against day ``day`` and return what is left today.

        A day above the last admitted one starts a fresh count. A refused
        call counts nothing and does not move the day forward.

        Args:
            day: a day index, e.g. Unix time // 86400. It may not go back.
            amount: 1..:data:`VINO_AMOUNT_MAX`.

        Raises:
            LimitRefusedError: the day's total would pass the cap; its
                :attr:`~zxv.LimitRefusedError.left` says what is still left today.
            ZxvUsageError: ``ZXV_E_RANGE`` (``day`` before the last day, i.e.
                the clock went back; or ``amount`` 0 or too large).
        """
        left = ffi.new("uint64_t *")
        rc = _native.lib().zxv_vino_meter_admit((cap or self._cap)._handle(), self._handle(),
                                                _u64(day, "day"), _u64(amount, "amount"), left)
        if rc == -38:  # ZXV_E_LIMIT
            exc = _native.make_error(rc)
            if isinstance(exc, LimitRefusedError):
                exc.left = int(left[0])
            raise exc
        self._check(rc)
        return int(left[0])
