// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Vino Embed (ABI 1.6): what an app or game needs to take part in Vino
//! without ever holding anyone's value or keys.
//!
//! Two lanes, which never meet:
//!
//! * **PLAY** is a sandbox inside the app ([`VinoPlay`]). It has its own
//!   play units (never live Vino), its own issuers and holders and its own
//!   vouchers, and lives in process memory only. Nothing in this module
//!   takes a [`VinoPlay`] together with anything live, so a play unit can
//!   never be converted into, redeemed for, or moved into live value. Play
//!   units are not money and are lost when the sandbox is dropped.
//! * **LIVE** is real Vino, held in the user's own ZXV wallet and never in
//!   the app. The app builds a [`PaymentRequest`]; the user approves it in
//!   their wallet, which signs a receipt with its ML-DSA-87 key
//!   ([`sign_receipt`], the wallet's call); the app checks the receipt
//!   offline ([`verify_receipt`]). The app never sees a balance, a key or
//!   a custody account.
//!
//! **Vouchers** (play lane). A voucher is one coin whose face is a
//! Fibonacci ladder face ([`coin_face`](crate::coin::coin_face)). It is held by
//! one holder on exactly one active [`Rail`] at a time: [`Rail::Bearer`] (a
//! note in the holder's hand) or [`Rail::Ghost`] (its twin on the play
//! ledger). A transfer must name the active rail (else
//! [`RefusedKind::Rail`](crate::RefusedKind::Rail)), so one voucher can
//! never be spent twice. Switching rails moves nothing.
//!
//! **Coverage.** An issuer posts play backing ([`VinoPlay::back`]) and may
//! issue only while its backing stays at least 1.8 times everything it has
//! outstanding: `10 * backing >= 18 * outstanding`, in exact integers. An
//! issue or a withdrawal that would break this is refused
//! ([`RefusedKind::Coverage`](crate::RefusedKind::Coverage)). Redeeming a
//! voucher returns it to its issuer and retires it: the issuer's
//! outstanding falls and nothing is paid out to anyone. No call turns a
//! voucher into backing, play units or anything else, and there is no
//! interest, yield or return of any kind.
//!
//! **Requests and receipts.** A request encodes to a fixed 160-byte
//! canonical record ([`RequestRecord`]); its id is SHA-256 of those bytes
//! ([`RequestId`]). A receipt is the wallet's ML-DSA-87 signature (empty
//! context) over the 88 bytes of [`receipt_message`]. The request carries
//! its expiry; a receipt for an expired request, or one approved after the
//! expiry, is [`RefusedKind::Expired`](crate::RefusedKind::Expired). A LIVE
//! request marked as a paid chance outcome ([`RequestFlags::CHANCE`]: loot
//! boxes, paid draws) is refused when it is encoded
//! ([`RefusedKind::Chance`](crate::RefusedKind::Chance)). The allotment is a
//! separate module, not a payment request.
//!
//! **Daily cap.** A deployment may set a per-device daily cap;
//! [`SpendMeter`] counts what one device approved per day and refuses an
//! amount that would pass the cap
//! ([`RefusedKind::Limit`](crate::RefusedKind::Limit)). It is a counter,
//! not a legal check, and makes no exemption claim.
//!
//! **Capabilities.** Every play-lane call and the meter need
//! [`Caps::VINO`](crate::Caps::VINO) (else `ZXV_E_CAP`; a halted context
//! gives `ZXV_E_POISONED`). Encoding and decoding a request, building a
//! receipt message and verifying a receipt are pure and need no
//! capability. Signing a receipt needs
//! [`Caps::CRYPTO_SIGN`](crate::Caps::CRYPTO_SIGN): it is the wallet's
//! call, not the app's.
//!
//! **Not here, by design:** minting live Vino, setting pegs, any conversion
//! to or from bank money, V-Bill auctions or bids, V-Bonds, and every
//! operator or steward action. None of them is app-facing.
//!
//! **Honest limits:** the LIVE flow here is only the app's half; approving
//! a request happens in the user's wallet (zxvd), which posts the payment
//! on its own ledger. ML-DSA-87 here is the reference implementation, not
//! FIPS 140-3 validated. Whether a given deployment of Vino is store
//! credit, a prepaid access product or something else is a legal question
//! pending counsel; nothing in this module decides it.
//!
//! A [`VinoPlay`] or [`SpendMeter`] borrows the capability it was made
//! with, so it cannot outlive it (rule R6):
//!
//! ```compile_fail
//! let ctx = zxv::Context::new()?;
//! let play = {
//!     let cap = ctx.root_capability(1, None, zxv::Caps::VINO)?;
//!     zxv::VinoPlay::new(&cap)?
//! };
//! # Ok::<(), zxv::Error>(())
//! ```

use std::ffi::{c_char, CStr, CString};
use std::fmt;
use std::ptr::NonNull;

use zxv_sys as sys;

use crate::capability::Capability;
use crate::crypto::{sizes, SecretKey};
use crate::error::{check, Error, Result};
use crate::ffi::{need_len, non_null};

/// Size of an encoded request ([`RequestRecord`]): 160 bytes.
pub const REQUEST_BYTES: usize = sys::ZXV_VINO_REQ_BYTES as usize;
/// Size of a request id ([`RequestId`]): 32 bytes (SHA-256).
pub const REQUEST_ID_BYTES: usize = 32;
/// Size of a receipt message ([`ReceiptMessage`]): 88 bytes.
pub const RECEIPT_MSG_BYTES: usize = sys::ZXV_VINO_RCPT_MSG_BYTES as usize;
/// Longest memo: 64 bytes of printable ASCII.
pub const MEMO_MAX: usize = sys::ZXV_VINO_MEMO_MAX as usize;
/// Issuers plus holders one play sandbox can hold.
pub const MAX_ACCOUNTS: u32 = sys::ZXV_VINO_MAX_ACCOUNTS;
/// Active (unredeemed) vouchers one play sandbox can hold.
pub const MAX_VOUCHERS: u32 = sys::ZXV_VINO_MAX_VOUCHERS;
/// Largest amount, backing step or daily cap (2^59).
pub const AMOUNT_MAX: u64 = sys::ZXV_VINO_AMOUNT_MAX;

/// An encoded payment request: exactly 160 canonical bytes.
pub type RequestRecord = [u8; REQUEST_BYTES];
/// A request's id: SHA-256 of its 160 encoded bytes.
pub type RequestId = [u8; REQUEST_ID_BYTES];
/// The 88 bytes a wallet signs to approve a request:
/// `"zxv/vino-rcpt/v1" || request id || SHA-256(wallet public key) ||
/// approved_at` (8 bytes, little-endian).
pub type ReceiptMessage = [u8; RECEIPT_MSG_BYTES];

/// The rail a voucher is active on. A voucher is on exactly one rail at a
/// time and can only be spent on that rail.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Rail {
    /// The note in the holder's hand.
    Bearer,
    /// Its twin on the play ledger.
    Ghost,
}

impl Rail {
    /// The C value (`ZXV_VINO_RAIL_*`).
    pub fn raw(self) -> i32 {
        match self {
            Rail::Bearer => sys::ZXV_VINO_RAIL_BEARER,
            Rail::Ghost => sys::ZXV_VINO_RAIL_GHOST,
        }
    }

    /// The rail of a C value, or `None` for an unknown one.
    pub fn from_raw(raw: i32) -> Option<Rail> {
        match raw {
            sys::ZXV_VINO_RAIL_BEARER => Some(Rail::Bearer),
            sys::ZXV_VINO_RAIL_GHOST => Some(Rail::Ghost),
            _ => None,
        }
    }
}

impl fmt::Display for Rail {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            Rail::Bearer => "BEARER",
            Rail::Ghost => "GHOST",
        })
    }
}

/// The lane of a payment request.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Lane {
    /// The amount is in play units of an app's sandbox.
    Play,
    /// The amount is in VFV minor units, paid from the user's own wallet.
    Live,
}

impl Lane {
    /// The C value (`ZXV_VINO_LANE_*`).
    pub fn raw(self) -> i32 {
        match self {
            Lane::Play => sys::ZXV_VINO_LANE_PLAY,
            Lane::Live => sys::ZXV_VINO_LANE_LIVE,
        }
    }

    /// The lane of a C value, or `None` for an unknown one (which the
    /// library refuses with `ZXV_E_LANE`).
    pub fn from_raw(raw: i32) -> Option<Lane> {
        match raw {
            sys::ZXV_VINO_LANE_PLAY => Some(Lane::Play),
            sys::ZXV_VINO_LANE_LIVE => Some(Lane::Live),
            _ => None,
        }
    }
}

impl fmt::Display for Lane {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            Lane::Play => "PLAY",
            Lane::Live => "LIVE",
        })
    }
}

/// The state of a voucher.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum VoucherState {
    /// Held by a holder and spendable on its active rail.
    Active,
    /// Returned to its issuer and retired. Nothing was paid out.
    Redeemed,
}

impl VoucherState {
    fn from_raw(raw: i32) -> Result<VoucherState> {
        match raw {
            sys::ZXV_VINO_V_ACTIVE => Ok(VoucherState::Active),
            sys::ZXV_VINO_V_REDEEMED => Ok(VoucherState::Redeemed),
            _ => Err(Error::with_detail(
                sys::ZXV_E_INTERNAL,
                format!("unknown voucher state {raw}"),
            )),
        }
    }
}

/// Whether a play account is an issuer or a holder.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum PlayAccountKind {
    /// Holds vouchers and spends them.
    Holder,
    /// Posts play backing and issues vouchers against it.
    Issuer,
}

impl PlayAccountKind {
    /// The C value (`is_issuer`: 1 issuer, 0 holder).
    pub fn raw(self) -> i32 {
        match self {
            PlayAccountKind::Holder => 0,
            PlayAccountKind::Issuer => 1,
        }
    }
}

/// The flags of a payment request (`ZXV_VINO_REQ_*`). Combine with `|`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
pub struct RequestFlags(u32);

impl RequestFlags {
    /// No flag.
    pub const NONE: RequestFlags = RequestFlags(0);
    /// The purchase has a paid chance outcome. Allowed on the PLAY lane
    /// only; a LIVE request with it is refused (`ZXV_E_CHANCE`).
    pub const CHANCE: RequestFlags = RequestFlags(sys::ZXV_VINO_REQ_CHANCE);
    /// Every flag this ABI defines; any other bit is refused
    /// (`ZXV_E_FORMAT`).
    pub const KNOWN: RequestFlags = RequestFlags(sys::ZXV_VINO_REQ_FLAGS_KNOWN);

    /// Flags from raw bits (unknown bits are refused by encode).
    pub const fn from_bits(bits: u32) -> RequestFlags {
        RequestFlags(bits)
    }

    /// The raw bits.
    pub const fn bits(self) -> u32 {
        self.0
    }

    /// True if every bit of `other` is set.
    pub const fn contains(self, other: RequestFlags) -> bool {
        self.0 & other.0 == other.0
    }
}

impl std::ops::BitOr for RequestFlags {
    type Output = RequestFlags;
    fn bitor(self, o: RequestFlags) -> RequestFlags {
        RequestFlags(self.0 | o.0)
    }
}

/// A payment request as plain values. [`encode`](Self::encode) it to the
/// 160 bytes a wallet approves; [`decode`](Self::decode) bytes back to it.
///
/// ```
/// use zxv::vino::{Lane, PaymentRequest, RequestFlags};
/// let req = PaymentRequest {
///     lane: Lane::Live,
///     flags: RequestFlags::NONE,
///     merchant: [1; 32],
///     nonce: [2; 16],
///     amount: 1_234_567, // VFV minor units
///     created_at: 1_700_000_000,
///     expires_at: 1_700_000_600,
///     memo: "Sword of Dawn".into(),
/// };
/// let (bytes, id) = req.encode()?;
/// let (back, id2) = PaymentRequest::decode(&bytes)?;
/// assert_eq!((back, id2), (req, id));
/// # Ok::<(), zxv::Error>(())
/// ```
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct PaymentRequest {
    /// [`Lane::Play`] or [`Lane::Live`].
    pub lane: Lane,
    /// [`RequestFlags::NONE`] or [`RequestFlags::CHANCE`] (PLAY lane only).
    pub flags: RequestFlags,
    /// The payee's id, e.g. SHA-256 of its public key.
    pub merchant: [u8; 32],
    /// Unique per request, so two requests never share an id.
    pub nonce: [u8; 16],
    /// 1 ..= [`AMOUNT_MAX`]: VFV minor units (LIVE) or play units (PLAY).
    pub amount: u64,
    /// Seconds of the caller's clock (Unix time by convention).
    pub created_at: u64,
    /// Must be above `created_at`.
    pub expires_at: u64,
    /// 0..=64 bytes of printable ASCII (0x20..=0x7e).
    pub memo: String,
}

impl PaymentRequest {
    /// Validates the request and returns its canonical 160 bytes and its id
    /// (SHA-256 of those bytes). Pure: no capability.
    ///
    /// Errors: `ZXV_E_CHANCE` (a LIVE request with
    /// [`RequestFlags::CHANCE`], REFUSED), `ZXV_E_RANGE` (amount 0 or above
    /// [`AMOUNT_MAX`], `expires_at` not above `created_at`), `ZXV_E_FORMAT`
    /// (an unknown flag, a memo byte outside printable ASCII, including a
    /// NUL), `ZXV_E_TOO_LONG` (memo above 64 bytes); all but the first are
    /// [`Error::Usage`].
    pub fn encode(&self) -> Result<(RequestRecord, RequestId)> {
        encode_raw(
            self.lane.raw(),
            self.flags.bits(),
            &self.merchant,
            &self.nonce,
            self.amount,
            self.created_at,
            self.expires_at,
            &self.memo,
        )
    }

    /// The request's id: SHA-256 of its 160 encoded bytes (the errors are
    /// [`encode`](Self::encode)'s).
    pub fn id(&self) -> Result<RequestId> {
        self.encode().map(|(_, id)| id)
    }

    /// Validates 160 request bytes and returns the request and its id.
    /// Pure: no capability. Applies [`encode`](Self::encode)'s rules plus
    /// the magic `"ZXVR"`, version 1 and zero padding (`ZXV_E_FORMAT`), and
    /// `ZXV_E_LANE` for a lane byte that is neither PLAY nor LIVE.
    /// Decoding then encoding gives back the same bytes.
    pub fn decode(bytes: &RequestRecord) -> Result<(PaymentRequest, RequestId)> {
        let (mut lane, mut flags) = (0i32, 0u32);
        let (mut merchant, mut nonce) = ([0u8; 32], [0u8; 16]);
        let (mut amount, mut created, mut expires) = (0u64, 0u64, 0u64);
        let mut memo = [0 as c_char; MEMO_MAX + 1];
        let mut id = [0u8; REQUEST_ID_BYTES];
        // SAFETY: in has 160 bytes; every out has the size the header asks
        // for (merchant 32, nonce 16, memo 65, id 32).
        check(unsafe {
            sys::zxv_vino_request_decode(
                bytes.as_ptr(),
                &mut lane,
                &mut flags,
                merchant.as_mut_ptr(),
                nonce.as_mut_ptr(),
                &mut amount,
                &mut created,
                &mut expires,
                memo.as_mut_ptr(),
                id.as_mut_ptr(),
            )
        })?;
        let lane = Lane::from_raw(lane).ok_or_else(|| {
            Error::with_detail(sys::ZXV_E_INTERNAL, format!("decoded unknown lane {lane}"))
        })?;
        // SAFETY: the library wrote a NUL-terminated memo into the buffer.
        let memo = unsafe { CStr::from_ptr(memo.as_ptr()) }
            .to_str()
            .map_err(|_| Error::with_detail(sys::ZXV_E_INTERNAL, "decoded memo is not ASCII"))?
            .to_owned();
        Ok((
            PaymentRequest {
                lane,
                flags: RequestFlags(flags),
                merchant,
                nonce,
                amount,
                created_at: created,
                expires_at: expires,
                memo,
            },
            id,
        ))
    }
}

/// [`PaymentRequest::encode`] with a raw lane value, for checking that the
/// library refuses lanes the [`Lane`] enum cannot express (`ZXV_E_LANE`).
/// Prefer [`PaymentRequest::encode`].
#[allow(clippy::too_many_arguments)]
pub fn encode_raw(
    lane: i32,
    flags: u32,
    merchant: &[u8; 32],
    nonce: &[u8; 16],
    amount: u64,
    created_at: u64,
    expires_at: u64,
    memo: &str,
) -> Result<(RequestRecord, RequestId)> {
    // A NUL would end the C string early and silently cut the memo.
    let memo = CString::new(memo).map_err(|_| {
        Error::with_detail(sys::ZXV_E_FORMAT, "memo is not printable ASCII (NUL byte)")
    })?;
    let mut out = [0u8; REQUEST_BYTES];
    let mut id = [0u8; REQUEST_ID_BYTES];
    // SAFETY: merchant 32 and nonce 16 bytes; memo NUL-terminated; out 160
    // and id 32 bytes.
    check(unsafe {
        sys::zxv_vino_request_encode(
            lane,
            flags,
            merchant.as_ptr(),
            nonce.as_ptr(),
            amount,
            created_at,
            expires_at,
            memo.as_ptr(),
            out.as_mut_ptr(),
            id.as_mut_ptr(),
        )
    })?;
    Ok((out, id))
}

/// The 88 bytes a wallet with public key `wallet_pk` (2592 bytes) signs to
/// approve request `id` at `approved_at`. Pure: no capability. `ZXV_E_ARG`
/// for a public key of the wrong length.
pub fn receipt_message(
    id: &RequestId,
    wallet_pk: &[u8],
    approved_at: u64,
) -> Result<ReceiptMessage> {
    need_len(wallet_pk, sizes::MLDSA87_PK, "wallet public key")?;
    let mut m = [0u8; RECEIPT_MSG_BYTES];
    // SAFETY: id 32 bytes, pk 2592 bytes, out 88 bytes.
    check(unsafe {
        sys::zxv_vino_receipt_msg(id.as_ptr(), wallet_pk.as_ptr(), approved_at, m.as_mut_ptr())
    })?;
    Ok(m)
}

/// The **wallet's** half of a payment: approves `request` at `approved_at`,
/// which must lie within `[created_at, expires_at]` of the request, and
/// returns the ML-DSA-87 signature (4627 bytes) over [`receipt_message`].
/// `cap` needs [`Caps::CRYPTO_SIGN`](crate::Caps::CRYPTO_SIGN). An app
/// never calls this: it never holds the wallet's key. `wallet_sk` is used
/// in place and never copied; `wallet_pk` must be its public key.
///
/// Errors: `ZXV_E_EXPIRED` (`approved_at` outside the window), the decode
/// errors for malformed request bytes, `ZXV_E_NOTIMPL` (built without
/// PQC), `ZXV_E_CAP`, `ZXV_E_ARG` (a key of the wrong length).
pub fn sign_receipt(
    cap: &Capability<'_>,
    request: &RequestRecord,
    wallet_sk: &SecretKey,
    wallet_pk: &[u8],
    approved_at: u64,
) -> Result<Vec<u8>> {
    wallet_sk.require_len(sizes::MLDSA87_SK, "wallet secret key")?;
    need_len(wallet_pk, sizes::MLDSA87_PK, "wallet public key")?;
    let mut sig = vec![0u8; sizes::MLDSA87_SIG];
    // SAFETY: request 160, sk 4896, pk 2592 and sig 4627 bytes; valid cap.
    cap.call(|c| unsafe {
        sys::zxv_vino_receipt_sign(
            c,
            request.as_ptr(),
            wallet_sk.as_ptr(),
            wallet_pk.as_ptr(),
            approved_at,
            sig.as_mut_ptr(),
        )
    })?;
    Ok(sig)
}

/// The **app's** half, offline and pure (no capability): `Ok(())` when
/// `sig` is the wallet's (`wallet_pk`) approval of `request` at
/// `approved_at`, `approved_at` lies within the request's window, and `now`
/// is not past its expiry. A good receipt proves the wallet approved; the
/// payment itself is posted by the wallet on its own ledger.
///
/// Errors: `ZXV_E_VERIFY` (not that wallet's signature over this request
/// and time), `ZXV_E_EXPIRED`, the decode errors for malformed request
/// bytes, `ZXV_E_NOTIMPL` (built without PQC), `ZXV_E_ARG` (a key or
/// signature of the wrong length).
pub fn verify_receipt(
    request: &RequestRecord,
    wallet_pk: &[u8],
    approved_at: u64,
    sig: &[u8],
    now: u64,
) -> Result<()> {
    need_len(wallet_pk, sizes::MLDSA87_PK, "wallet public key")?;
    need_len(sig, sizes::MLDSA87_SIG, "receipt signature")?;
    // SAFETY: request 160, pk 2592 and sig 4627 bytes.
    check(unsafe {
        sys::zxv_vino_receipt_verify(
            request.as_ptr(),
            wallet_pk.as_ptr(),
            approved_at,
            sig.as_ptr(),
            now,
        )
    })
}

// ===== the play lane ======================================================

/// What [`VinoPlay::voucher`] reports about one voucher.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct VoucherInfo {
    /// Its Fibonacci ladder face, in play units.
    pub face: u64,
    /// The issuer account that issued it.
    pub issuer: u32,
    /// The holder account that holds it (or held it when redeemed).
    pub holder: u32,
    /// Its active rail.
    pub rail: Rail,
    /// Active or redeemed.
    pub state: VoucherState,
}

/// What [`VinoPlay::account`] reports about one account.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PlayAccount {
    /// The owner id given to [`VinoPlay::open`].
    pub owner: u32,
    /// Issuer or holder.
    pub kind: PlayAccountKind,
    /// An issuer's posted play backing (0 for a holder).
    pub backing: u64,
    /// For an issuer, the sum of the faces it has outstanding; for a
    /// holder, the sum of the faces it holds.
    pub faces: u64,
    /// Active vouchers it issued (issuer) or holds (holder).
    pub count: u32,
}

/// One play-lane sandbox (`zxv_vino_play`): issuers, holders and vouchers
/// of play units, in process memory only. Play units are not money, cannot
/// become live value, and are lost on drop. `Send + Sync` (the C sandbox
/// serializes its calls). It uses the capability it was made with for
/// every call and borrows it, so it cannot outlive it.
///
/// ```
/// use zxv::vino::{PlayAccountKind, Rail, VinoPlay, VoucherState};
/// use zxv::{Caps, Context, RefusedKind};
/// let ctx = Context::new()?;
/// let cap = ctx.root_capability(1, None, Caps::VINO)?;
/// let play = VinoPlay::new(&cap)?;
/// let issuer = play.open(100, PlayAccountKind::Issuer)?;
/// let player = play.open(200, PlayAccountKind::Holder)?;
/// let shop = play.open(300, PlayAccountKind::Holder)?;
/// play.back(issuer, 18)?; // the play faucet: room for faces up to 10
/// let err = play.issue(issuer, player, 13, Rail::Bearer).unwrap_err();
/// assert_eq!(err.refused_kind(), Some(RefusedKind::Coverage));
/// let coin = play.issue(issuer, player, 8, Rail::Bearer)?;
/// play.transfer(coin, player, shop, Rail::Bearer)?; // the player spends it
/// play.redeem(coin, shop)?; // retired; nothing is paid out
/// assert_eq!(play.voucher(coin)?.state, VoucherState::Redeemed);
/// assert_eq!(play.account(issuer)?.backing, 18);
/// assert!(play.vouchers(shop)?.is_empty());
/// # Ok::<(), zxv::Error>(())
/// ```
pub struct VinoPlay<'a> {
    raw: NonNull<sys::zxv_vino_play>,
    cap: &'a Capability<'a>,
}

// SAFETY: the C sandbox serializes every call with an internal lock.
unsafe impl Send for VinoPlay<'_> {}
// SAFETY: as above.
unsafe impl Sync for VinoPlay<'_> {}

impl<'a> VinoPlay<'a> {
    /// An empty play sandbox on `cap`'s context (`cap` needs
    /// [`Caps::VINO`](crate::Caps::VINO)). Errors: `ZXV_E_CAP`,
    /// `ZXV_E_POISONED`, `ZXV_E_NOMEM`.
    pub fn new(cap: &'a Capability<'a>) -> Result<VinoPlay<'a>> {
        let mut raw = std::ptr::null_mut();
        // SAFETY: valid cap; out pointer valid.
        cap.call(|c| unsafe { sys::zxv_vino_play_create(c, &mut raw) })?;
        Ok(VinoPlay {
            raw: non_null(raw, "vino play sandbox")?,
            cap,
        })
    }

    /// The raw handle, for calls this crate does not wrap. It stays owned
    /// by `self`.
    pub fn as_raw(&self) -> *mut sys::zxv_vino_play {
        self.raw.as_ptr()
    }

    fn call(
        &self,
        f: impl FnOnce(*const sys::zxv_cap, *mut sys::zxv_vino_play) -> i32,
    ) -> Result<()> {
        let p = self.raw.as_ptr();
        self.cap.call(|c| f(c, p))
    }

    /// Opens an account of `kind` for `owner` (an id of your choosing;
    /// owners need not be distinct) and returns its number. Account numbers
    /// count up from 0. `ZXV_E_CAPACITY` when [`MAX_ACCOUNTS`] are open.
    pub fn open(&self, owner: u32, kind: PlayAccountKind) -> Result<u32> {
        let mut a = 0u32;
        // SAFETY: valid cap and sandbox; out pointer valid.
        self.call(|c, p| unsafe { sys::zxv_vino_play_open(c, p, owner, kind.raw(), &mut a) })?;
        Ok(a)
    }

    /// Posts `amount` play units of backing to an issuer. This is the play
    /// faucet: play units come from nowhere and are worth nothing. Errors:
    /// `ZXV_E_ARG` (not an issuer), `ZXV_E_NOT_FOUND`, `ZXV_E_RANGE`
    /// (amount 0, above [`AMOUNT_MAX`], or backing would pass 2^62).
    pub fn back(&self, issuer: u32, amount: u64) -> Result<()> {
        // SAFETY: valid cap and sandbox.
        self.call(|c, p| unsafe { sys::zxv_vino_play_back(c, p, issuer, amount) })
    }

    /// Withdraws `amount` of an issuer's backing, only while it stays at
    /// 1.8x coverage. Errors: `ZXV_E_COVERAGE` (it would not), `ZXV_E_FUNDS`
    /// (more than its backing), and [`back`](Self::back)'s. A refusal
    /// changes nothing.
    pub fn unback(&self, issuer: u32, amount: u64) -> Result<()> {
        // SAFETY: valid cap and sandbox.
        self.call(|c, p| unsafe { sys::zxv_vino_play_unback(c, p, issuer, amount) })
    }

    /// Issues one voucher of Fibonacci `face` from `issuer` to `holder`,
    /// active on `rail`, and returns its id (above 0, never reused in this
    /// sandbox). Errors: `ZXV_E_DENOM` (not a ladder face),
    /// `ZXV_E_COVERAGE` (the issuer would fall below 1.8x), `ZXV_E_ARG`
    /// (issuer not an issuer, holder not a holder), `ZXV_E_NOT_FOUND`,
    /// `ZXV_E_CAPACITY` ([`MAX_VOUCHERS`] active).
    pub fn issue(&self, issuer: u32, holder: u32, face: u64, rail: Rail) -> Result<u64> {
        let mut id = 0u64;
        // SAFETY: valid cap and sandbox; out pointer valid.
        self.call(|c, p| unsafe {
            sys::zxv_vino_issue(c, p, issuer, holder, face, rail.raw(), &mut id)
        })?;
        Ok(id)
    }

    /// Spends an active voucher: it moves from holder `from` to holder
    /// `to`, on `rail`, which must be its active rail (the rail does not
    /// change). Errors: `ZXV_E_RAIL` (not the active rail), `ZXV_E_STATE`
    /// (already redeemed), `ZXV_E_ARG` (`from` is not its holder, `to` is
    /// not a holder, `from == to`), `ZXV_E_NOT_FOUND`.
    pub fn transfer(&self, voucher: u64, from: u32, to: u32, rail: Rail) -> Result<()> {
        // SAFETY: valid cap and sandbox.
        self.call(|c, p| unsafe { sys::zxv_vino_transfer(c, p, voucher, from, to, rail.raw()) })
    }

    /// Switches an active voucher, by its holder, to `rail` (bearer <->
    /// ghost). Nothing moves. Errors: `ZXV_E_RAIL` (already on that rail),
    /// `ZXV_E_STATE` (redeemed), `ZXV_E_ARG` (not its holder),
    /// `ZXV_E_NOT_FOUND`.
    pub fn set_rail(&self, voucher: u64, holder: u32, rail: Rail) -> Result<()> {
        // SAFETY: valid cap and sandbox.
        self.call(|c, p| unsafe { sys::zxv_vino_set_rail(c, p, voucher, holder, rail.raw()) })
    }

    /// Redeems an active voucher, by its holder, to its issuer: it is
    /// retired and the issuer's outstanding falls by its face. Nothing is
    /// paid out to anyone. Errors: `ZXV_E_STATE` (already redeemed),
    /// `ZXV_E_ARG` (not its holder), `ZXV_E_NOT_FOUND`.
    pub fn redeem(&self, voucher: u64, holder: u32) -> Result<()> {
        // SAFETY: valid cap and sandbox.
        self.call(|c, p| unsafe { sys::zxv_vino_redeem(c, p, voucher, holder) })
    }

    /// One voucher, active or redeemed (`ZXV_E_NOT_FOUND` for an id never
    /// issued).
    pub fn voucher(&self, id: u64) -> Result<VoucherInfo> {
        let (mut face, mut iss, mut hold, mut rail, mut state) = (0u64, 0u32, 0u32, 0i32, 0i32);
        // SAFETY: valid cap and sandbox; out pointers valid.
        self.call(|c, p| unsafe {
            sys::zxv_vino_voucher_info(
                c, p, id, &mut face, &mut iss, &mut hold, &mut rail, &mut state,
            )
        })?;
        Ok(VoucherInfo {
            face,
            issuer: iss,
            holder: hold,
            rail: Rail::from_raw(rail).ok_or_else(|| {
                Error::with_detail(sys::ZXV_E_INTERNAL, format!("unknown rail {rail}"))
            })?,
            state: VoucherState::from_raw(state)?,
        })
    }

    /// One account's owner, kind and totals (`ZXV_E_NOT_FOUND` past the
    /// last account).
    pub fn account(&self, account: u32) -> Result<PlayAccount> {
        let (mut owner, mut is_issuer, mut backing, mut faces, mut count) =
            (0u32, 0i32, 0u64, 0u64, 0u32);
        // SAFETY: valid cap and sandbox; out pointers valid.
        self.call(|c, p| unsafe {
            sys::zxv_vino_account_info(
                c,
                p,
                account,
                &mut owner,
                &mut is_issuer,
                &mut backing,
                &mut faces,
                &mut count,
            )
        })?;
        Ok(PlayAccount {
            owner,
            kind: if is_issuer != 0 {
                PlayAccountKind::Issuer
            } else {
                PlayAccountKind::Holder
            },
            backing,
            faces,
            count,
        })
    }

    /// The ids of the active vouchers an account issued (issuer) or holds
    /// (holder), ascending; empty when there are none. `ZXV_E_NOT_FOUND`
    /// past the last account. The C two-call buffer is handled here.
    pub fn vouchers(&self, account: u32) -> Result<Vec<u64>> {
        let ctx = self.cap.context();
        ctx.guard()?;
        let (c, p) = (self.cap.raw(), self.raw.as_ptr());
        let mut buf = vec![0u64; 8];
        for _ in 0..4 {
            let mut n = 0usize;
            // SAFETY: valid cap and sandbox; buf valid for buf.len() ids.
            let rc = unsafe {
                sys::zxv_vino_vouchers(c, p, account, buf.as_mut_ptr(), buf.len(), &mut n)
            };
            if rc == sys::ZXV_OK {
                buf.truncate(n);
                return Ok(buf);
            }
            if rc == sys::ZXV_E_BUFFER && n > buf.len() {
                buf = vec![0u64; n];
                continue;
            }
            return Err(self.cap.fatal(Error::from_status(rc)));
        }
        Err(Error::with_detail(
            sys::ZXV_E_INTERNAL,
            "the voucher list kept growing",
        ))
    }

    /// Re-checks every invariant: each active voucher has a ladder face, one
    /// rail, an issuer and a holder; each issuer's outstanding equals the
    /// sum of its active faces and `10 * backing >= 18 * outstanding`; each
    /// holder's total equals what it holds. A failure is
    /// [`Error::Fatal`] (`ZXV_E_INVARIANT`) and poisons the context.
    pub fn check(&self) -> Result<()> {
        // SAFETY: valid cap and sandbox.
        self.call(|c, p| unsafe { sys::zxv_vino_play_check(c, p) })
    }
}

impl fmt::Debug for VinoPlay<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("VinoPlay")
    }
}

impl Drop for VinoPlay<'_> {
    fn drop(&mut self) {
        // SAFETY: owned handle destroyed once, before its capability (the
        // borrow guarantees that), after every call (it takes &self).
        unsafe { sys::zxv_vino_play_destroy(self.raw.as_ptr()) }
    }
}

// ===== the daily meter ====================================================

/// Counts what one device approved per day against a daily cap
/// (`zxv_vino_meter`). It is a counter, not a legal check, and makes no
/// exemption claim. `Send + Sync`.
///
/// ```
/// use zxv::vino::SpendMeter;
/// use zxv::{Caps, Context, RefusedKind};
/// let ctx = Context::new()?;
/// let cap = ctx.root_capability(1, None, Caps::VINO)?;
/// let meter = SpendMeter::new(&cap, 100)?;
/// assert_eq!(meter.admit(19_700, 60)?, 40); // 40 left today
/// let (r, left) = meter.admit_with_left(19_700, 41);
/// assert_eq!(r.unwrap_err().refused_kind(), Some(RefusedKind::Limit));
/// assert_eq!(left, Some(40)); // nothing was counted
/// # Ok::<(), zxv::Error>(())
/// ```
pub struct SpendMeter<'a> {
    raw: NonNull<sys::zxv_vino_meter>,
    cap: &'a Capability<'a>,
}

// SAFETY: the C meter serializes every call with an internal lock.
unsafe impl Send for SpendMeter<'_> {}
// SAFETY: as above.
unsafe impl Sync for SpendMeter<'_> {}

impl<'a> SpendMeter<'a> {
    /// A meter with cap `daily_cap` (1 ..= [`AMOUNT_MAX`]) per day (`cap`
    /// needs [`Caps::VINO`](crate::Caps::VINO)). Errors: `ZXV_E_RANGE`,
    /// `ZXV_E_CAP`, `ZXV_E_POISONED`.
    pub fn new(cap: &'a Capability<'a>, daily_cap: u64) -> Result<SpendMeter<'a>> {
        let mut raw = std::ptr::null_mut();
        // SAFETY: valid cap; out pointer valid.
        cap.call(|c| unsafe { sys::zxv_vino_meter_create(c, daily_cap, &mut raw) })?;
        Ok(SpendMeter {
            raw: non_null(raw, "vino meter")?,
            cap,
        })
    }

    /// Counts `amount` against `day` (for example Unix time / 86400) and
    /// returns what is left of that day's cap. A day above the last one
    /// starts a fresh count. Errors: `ZXV_E_LIMIT` (the day's total would
    /// pass the cap: nothing is counted; use
    /// [`admit_with_left`](Self::admit_with_left) to see what is left),
    /// `ZXV_E_RANGE` (amount 0 or too large, or a day below the last one:
    /// the clock went back).
    pub fn admit(&self, day: u64, amount: u64) -> Result<u64> {
        let (r, left) = self.admit_with_left(day, amount);
        r.map(|()| left.unwrap_or(0))
    }

    /// [`admit`](Self::admit), also returning what is left today after the
    /// call: `Some` on success and on `ZXV_E_LIMIT`, `None` otherwise.
    pub fn admit_with_left(&self, day: u64, amount: u64) -> (Result<()>, Option<u64>) {
        let mut left = 0u64;
        let m = self.raw.as_ptr();
        // SAFETY: valid cap and meter; out pointer valid.
        let r = self
            .cap
            .call(|c| unsafe { sys::zxv_vino_meter_admit(c, m, day, amount, &mut left) });
        let known = match &r {
            Ok(()) => true,
            Err(e) => e.status() == sys::ZXV_E_LIMIT,
        };
        (r, known.then_some(left))
    }
}

impl fmt::Debug for SpendMeter<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("SpendMeter")
    }
}

impl Drop for SpendMeter<'_> {
    fn drop(&mut self) {
        // SAFETY: owned handle destroyed once, before its capability.
        unsafe { sys::zxv_vino_meter_destroy(self.raw.as_ptr()) }
    }
}
