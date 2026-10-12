// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Fibonacci coins and AI tokens (ABI 1.5).
//!
//! Two Fibonacci ladders share the faces 1, 2, 3, 5, 8, 13, 21, ... and the
//! same unique fewest-pieces (Zeckendorf) form; both are integer-only.
//!
//! **VFV coins.** Rung r (1..=[`COIN_RUNGS`]) has face F(r+1), so rung 1 is
//! 1, rung 4 is 5 and rung 45 is 1,836,311,903 ([`COIN_TOP_FACE`]). Rung 0
//! is the ceremonial V0 note, worth nothing. Any other face is not a coin
//! (`ZXV_E_DENOM`). [`coin_split`] pays an arbitrary amount. Minting VFV
//! coins is not in this ABI.
//!
//! **AI tokens.** Tier k (0..64) has face F(k+2). Tier
//! [`TOKEN_SUPER_TIER`] (face 144) and up are super tokens. A
//! [`TokenHolding`] counts tokens per tier. [`Octane`] is the densest tier
//! a holding may use.
//!
//! **No premium.** A dense token is worth exactly its face in plain
//! tokens. Density earns no interest, costs no fee and buys no extra
//! allotment or priority; changing octane re-expresses the same value and
//! never changes it. Density only means fewer pieces. It does not make a
//! model faster or an amount shorter on the wire.
//!
//! The pure functions need no capability. A [`TokenWallet`] changes only
//! with [`Caps::MINT`](crate::Caps::MINT).
//!
//! ```
//! use zxv::coin::{coin_split, Octane, TokenHolding};
//! assert_eq!(coin_split(100)?, vec![10, 5, 3]); // 89 + 8 + 3
//! let h = TokenHolding::canonical(1_000_000, Octane::RACING)?;
//! assert_eq!(h.value()?, (1_000_000, 17)); // same value, 17 pieces
//! # Ok::<(), zxv::Error>(())
//! ```

use std::fmt;
use std::ptr::NonNull;

use zxv_sys as sys;

use crate::capability::Capability;
use crate::error::{check, Error, Result};
use crate::ffi::{local, non_null, opt_ptr};

/// Rungs 1..=45 carry value.
pub const COIN_RUNGS: u32 = sys::ZXV_COIN_RUNGS;
/// Face of rung 45, F(46).
pub const COIN_TOP_FACE: u64 = sys::ZXV_COIN_TOP_FACE;
/// The largest split [`coin_split`] returns as a vector.
pub const COIN_SPLIT_MAX_COINS: u64 = 1 << 24;
/// Token tiers 0..64.
pub const TOKEN_TIERS: usize = sys::ZXV_TOKEN_TIERS as usize;
/// Tier 10 (face 144) and up are super tokens.
pub const TOKEN_SUPER_TIER: u32 = sys::ZXV_TOKEN_SUPER_TIER;
/// Longest Fibonacci code, in bits.
pub const TOKEN_CODE_MAX_BITS: u32 = sys::ZXV_TOKEN_CODE_MAX_BITS;
/// Largest buffer the token-code calls address, in bytes.
pub const TOKEN_CODE_MAX_BYTES: usize = sys::ZXV_TOKEN_CODE_MAX_BYTES as usize;

/// The face of ladder `rung` (0..=[`COIN_RUNGS`]); rung 0 is the V0 note,
/// face 0. `ZXV_E_RANGE` above [`COIN_RUNGS`].
pub fn coin_face(rung: u32) -> Result<u64> {
    let mut f = 0u64;
    // SAFETY: pure; out pointer valid.
    check(unsafe { sys::zxv_coin_face(rung, &mut f) })?;
    Ok(f)
}

/// The rung (1..=[`COIN_RUNGS`]) whose face is exactly `face`; 0 and any
/// face not on the ladder are `ZXV_E_DENOM`.
pub fn coin_rung(face: u64) -> Result<u32> {
    let mut r = 0u32;
    // SAFETY: pure; out pointer valid.
    check(unsafe { sys::zxv_coin_rung(face, &mut r) })?;
    Ok(r)
}

/// How many coins [`coin_split`]`(amount)` has, without building it.
pub fn coin_split_count(amount: u64) -> Result<u64> {
    let mut n = 0usize;
    // SAFETY: NULL buffer with capacity 0 is the documented size query.
    let rc = unsafe { sys::zxv_coin_split(amount, std::ptr::null_mut(), 0, &mut n) };
    if rc != sys::ZXV_OK && rc != sys::ZXV_E_BUFFER {
        return Err(Error::from_status(rc));
    }
    Ok(n as u64)
}

/// The coins for a whole amount: its Zeckendorf split into rungs, largest
/// first (amounts at or above the top face take `amount / COIN_TOP_FACE`
/// coins of rung 45 first). Unique, with the fewest coins possible; 0 is
/// an empty split. A split of more than [`COIN_SPLIT_MAX_COINS`] coins is
/// refused with `ZXV_E_RANGE` instead of allocating it: use
/// [`coin_split_count`].
pub fn coin_split(amount: u64) -> Result<Vec<u32>> {
    let n = coin_split_count(amount)?;
    if n == 0 {
        return Ok(Vec::new());
    }
    if n > COIN_SPLIT_MAX_COINS {
        return Err(Error::with_detail(
            sys::ZXV_E_RANGE,
            "the split has more than COIN_SPLIT_MAX_COINS coins; use coin_split_count",
        ));
    }
    let mut out = vec![0u32; n as usize];
    let mut got = 0usize;
    // SAFETY: out has n slots.
    check(unsafe { sys::zxv_coin_split(amount, out.as_mut_ptr(), out.len(), &mut got) })?;
    out.truncate(got);
    Ok(out)
}

/// The densest tier a holding may use (0..=63). Named octanes are like a
/// fuel pump's grades; any value 0..=63 is allowed.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct Octane(pub u32);

impl Octane {
    /// Face 1 only.
    pub const PLAIN: Octane = Octane(sys::ZXV_OCTANE_PLAIN);
    /// Up to face 8.
    pub const REGULAR: Octane = Octane(sys::ZXV_OCTANE_REGULAR);
    /// Up to face 89.
    pub const MIDGRADE: Octane = Octane(sys::ZXV_OCTANE_MIDGRADE);
    /// Up to face 987.
    pub const PREMIUM: Octane = Octane(sys::ZXV_OCTANE_PREMIUM);
    /// Up to face 75025.
    pub const RACING: Octane = Octane(sys::ZXV_OCTANE_RACING);
    /// Up to face F(65).
    pub const MAX: Octane = Octane(sys::ZXV_OCTANE_MAX);
}

/// The face of token `tier` and whether it is a super token
/// (`ZXV_E_RANGE` for tier >= 64).
pub fn token_face(tier: u32) -> Result<(u64, bool)> {
    let (mut f, mut s) = (0u64, 0i32);
    // SAFETY: pure; out pointers valid.
    check(unsafe { sys::zxv_token_face(tier, &mut f, &mut s) })?;
    Ok((f, s != 0))
}

/// The tier whose face is exactly `face` (`ZXV_E_DENOM` if none).
pub fn token_tier(face: u64) -> Result<u32> {
    let mut t = 0u32;
    // SAFETY: pure; out pointer valid.
    check(unsafe { sys::zxv_token_tier(face, &mut t) })?;
    Ok(t)
}

/// Token counts per tier: `0[k]` tokens of face [`token_face`]`(k)`. Its
/// value is the sum of count × face. The default value is empty.
#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct TokenHolding(pub [u64; TOKEN_TIERS]);

impl Default for TokenHolding {
    fn default() -> TokenHolding {
        TokenHolding([0; TOKEN_TIERS])
    }
}

impl fmt::Debug for TokenHolding {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let used: Vec<(usize, u64)> = self
            .0
            .iter()
            .enumerate()
            .filter(|(_, &n)| n != 0)
            .map(|(k, &n)| (k, n))
            .collect();
        write!(f, "TokenHolding{used:?}")
    }
}

impl TokenHolding {
    /// The canonical form of `value` at `octane`: `value / face(octane)`
    /// tokens on that tier and the rest in Zeckendorf form below it.
    pub fn canonical(value: u64, octane: Octane) -> Result<TokenHolding> {
        let mut h = TokenHolding::default();
        // SAFETY: h has 64 counts.
        check(unsafe { sys::zxv_token_canonical(value, octane.0, h.0.as_mut_ptr()) })?;
        Ok(h)
    }

    /// The value and the number of pieces. `ZXV_E_RANGE` past 64 bits.
    pub fn value(&self) -> Result<(u64, u64)> {
        let (mut v, mut p) = (0u64, 0u64);
        // SAFETY: 64 counts; out pointers valid.
        check(unsafe { sys::zxv_token_value(self.0.as_ptr(), &mut v, &mut p) })?;
        Ok((v, p))
    }

    /// True if this is the canonical form of its value at `octane` (false
    /// also when a tier above the octane is used).
    pub fn is_canonical(&self, octane: Octane) -> Result<bool> {
        let mut c = 0i32;
        // SAFETY: 64 counts; out pointer valid.
        check(unsafe { sys::zxv_token_is_canonical(self.0.as_ptr(), octane.0, &mut c) })?;
        Ok(c == 1)
    }

    /// Rewrites the holding in place into the canonical form of its value at
    /// `octane`; the value never changes. Unchanged on failure.
    pub fn compress(&mut self, octane: Octane) -> Result<()> {
        // SAFETY: 64 counts, exclusively borrowed.
        check(unsafe { sys::zxv_token_compress(self.0.as_mut_ptr(), octane.0) })
    }

    /// Turns one tier-k and one tier-(k+1) token into one tier-(k+2)
    /// token. `ZXV_E_STATE` when tier k+2 is denser than `octane` allows;
    /// `ZXV_E_FUNDS` when a token is missing; `ZXV_E_RANGE` for tier > 61.
    /// Unchanged on failure.
    pub fn fuse(&mut self, octane: Octane, tier: u32) -> Result<()> {
        // SAFETY: 64 counts, exclusively borrowed.
        check(unsafe { sys::zxv_token_fuse(self.0.as_mut_ptr(), octane.0, tier) })
    }

    /// Turns one tier-k token into one tier-(k-1) and one tier-(k-2) token
    /// (tier 1 becomes two plain tokens; tier 0 is atomic, `ZXV_E_RANGE`).
    /// `ZXV_E_FUNDS` without a tier-k token.
    pub fn split(&mut self, tier: u32) -> Result<()> {
        // SAFETY: 64 counts, exclusively borrowed.
        check(unsafe { sys::zxv_token_split(self.0.as_mut_ptr(), tier) })
    }
}

/// Bits in the Fibonacci code of `value` (2..=93); `ZXV_E_RANGE` for
/// `u64::MAX`, which has no code.
pub fn token_code_bits(value: u64) -> Result<u32> {
    let mut b = 0u32;
    // SAFETY: pure; out pointer valid.
    check(unsafe { sys::zxv_token_code_bits(value, &mut b) })?;
    Ok(b)
}

/// Writes the Fibonacci code of `value` (Zeckendorf bits of value+1,
/// lowest first, then a closing 1) into `buf` at bit position `bit` (bit i
/// is bit i%8 of byte i/8); returns the bit position after it. Only the
/// code's bits are written. `ZXV_E_BUFFER` when it does not fit.
pub fn put_token_code(buf: &mut [u8], bit: u32, value: u64) -> Result<u32> {
    let mut b = bit;
    // SAFETY: buf valid for its length; inout pointer valid.
    check(unsafe { sys::zxv_token_code_put(buf.as_mut_ptr(), buf.len(), &mut b, value) })?;
    Ok(b)
}

/// Reads one Fibonacci code from `buf` at bit position `bit`; returns the
/// value and the bit position after it. `ZXV_E_FORMAT` when no closing
/// "11" comes before the end; `ZXV_E_RANGE` when `bit` is past the end.
pub fn get_token_code(buf: &[u8], bit: u32) -> Result<(u64, u32)> {
    let (mut b, mut v) = (bit, 0u64);
    // SAFETY: buf valid for its length; out pointers valid.
    check(unsafe { sys::zxv_token_code_get(opt_ptr(buf), buf.len(), &mut b, &mut v) })?;
    Ok((v, b))
}

/// The Fibonacci codes of `values`, one after another, and the number of
/// bits used.
pub fn encode_token_codes(values: &[u64]) -> Result<(Vec<u8>, u32)> {
    let mut total = 0u64;
    for &v in values {
        total += u64::from(token_code_bits(v)?);
    }
    let mut buf = vec![0u8; total.div_ceil(8) as usize];
    let mut bit = 0u32;
    for &v in values {
        bit = put_token_code(&mut buf, bit, v)?;
    }
    Ok((buf, bit))
}

/// An iterator over the Fibonacci codes in a buffer ([`token_codes`]).
#[derive(Debug, Clone)]
pub struct TokenCodes<'b> {
    buf: &'b [u8],
    bit: u32,
    left: usize,
}

impl Iterator for TokenCodes<'_> {
    type Item = Result<u64>;
    fn next(&mut self) -> Option<Result<u64>> {
        if self.left == 0 {
            return None;
        }
        self.left -= 1;
        match get_token_code(self.buf, self.bit) {
            Ok((v, next)) => {
                self.bit = next;
                Some(Ok(v))
            }
            Err(e) => {
                self.left = 0;
                Some(Err(e))
            }
        }
    }
}

/// Reads `count` Fibonacci codes from the start of `buf`, lazily.
pub fn token_codes(buf: &[u8], count: usize) -> TokenCodes<'_> {
    TokenCodes {
        buf,
        bit: 0,
        left: count,
    }
}

/// What [`TokenWallet::info`] reports.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TokenWalletInfo {
    /// The wallet's free label.
    pub model_id: u32,
    /// The octane the wallet keeps canonical.
    pub octane: Octane,
    /// Tokens held, in plain tokens.
    pub value: u64,
    /// Number of pieces at the wallet's octane.
    pub pieces: u64,
}

/// A holding kept canonical at its own octane (`zxv_token_wallet`). Every
/// change uses the capability it was created with, which needs
/// [`Caps::MINT`](crate::Caps::MINT) (tokens enter only by
/// [`mint`](Self::mint)); reads need none. Fails closed with its context.
/// `Send + Sync`.
pub struct TokenWallet<'a> {
    raw: NonNull<sys::zxv_token_wallet>,
    cap: &'a Capability<'a>,
}

// SAFETY: the C wallet is thread-safe (serialized on its context).
unsafe impl Send for TokenWallet<'_> {}
// SAFETY: as above.
unsafe impl Sync for TokenWallet<'_> {}

impl<'a> TokenWallet<'a> {
    /// An empty wallet at `octane`, labelled `model_id` (a free label).
    /// `cap` needs [`Caps::MINT`](crate::Caps::MINT).
    pub fn new(cap: &'a Capability<'a>, model_id: u32, octane: Octane) -> Result<TokenWallet<'a>> {
        let mut raw = std::ptr::null_mut();
        // SAFETY: valid cap; out pointer valid.
        cap.call(|c| unsafe { sys::zxv_token_wallet_create(c, model_id, octane.0, &mut raw) })?;
        Ok(TokenWallet {
            raw: non_null(raw, "token wallet")?,
            cap,
        })
    }

    /// Creates `amount` new tokens. `ZXV_E_RANGE` (nothing changes) past 64
    /// bits.
    pub fn mint(&self, amount: u64) -> Result<()> {
        let w = self.raw.as_ptr();
        // SAFETY: valid cap and wallet.
        self.cap
            .call(|c| unsafe { sys::zxv_token_wallet_mint(c, w, amount) })
    }

    /// Burns `amount`; the change stays canonical. `ZXV_E_FUNDS` (nothing
    /// changes) when the wallet holds less.
    pub fn spend(&self, amount: u64) -> Result<()> {
        let w = self.raw.as_ptr();
        // SAFETY: valid cap and wallet.
        self.cap
            .call(|c| unsafe { sys::zxv_token_wallet_spend(c, w, amount) })
    }

    /// Moves `amount` to `to`. The payer keeps its change at its octane;
    /// the receiver gets the payment at its own octane; the total is
    /// conserved exactly. Both wallets must belong to this wallet's context
    /// (`ZXV_E_CAP` otherwise). A transfer to itself only checks the funds.
    /// Neither wallet changes on failure.
    pub fn transfer(&self, to: &TokenWallet<'_>, amount: u64) -> Result<()> {
        let (w, t) = (self.raw.as_ptr(), to.raw.as_ptr());
        // SAFETY: valid cap and wallets (C locks both).
        self.cap
            .call(|c| unsafe { sys::zxv_token_wallet_transfer(c, w, t, amount) })
    }

    /// Changes the octane (up = denser, down = plainer) and re-expresses
    /// the same value canonically.
    pub fn set_octane(&self, octane: Octane) -> Result<()> {
        let w = self.raw.as_ptr();
        // SAFETY: valid cap and wallet.
        self.cap
            .call(|c| unsafe { sys::zxv_token_wallet_set_octane(c, w, octane.0) })
    }

    /// The label, octane, value and piece count.
    pub fn info(&self) -> Result<TokenWalletInfo> {
        let (mut m, mut o, mut v, mut p) = (0u32, 0u32, 0u64, 0u64);
        let w = self.raw.as_ptr();
        // SAFETY: valid wallet; out pointers valid.
        local(self.cap, || unsafe {
            sys::zxv_token_wallet_info(w, &mut m, &mut o, &mut v, &mut p)
        })?;
        Ok(TokenWalletInfo {
            model_id: m,
            octane: Octane(o),
            value: v,
            pieces: p,
        })
    }

    /// The counts per tier.
    pub fn counts(&self) -> Result<TokenHolding> {
        let mut h = TokenHolding::default();
        let w = self.raw.as_ptr();
        // SAFETY: valid wallet; h has 64 counts.
        local(self.cap, || unsafe {
            sys::zxv_token_wallet_counts(w, h.0.as_mut_ptr())
        })?;
        Ok(h)
    }
}

impl fmt::Debug for TokenWallet<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("TokenWallet")
            .field("info", &self.info().ok())
            .finish()
    }
}

impl Drop for TokenWallet<'_> {
    fn drop(&mut self) {
        // SAFETY: owned handle destroyed once, before its capability.
        unsafe { sys::zxv_token_wallet_destroy(self.raw.as_ptr()) }
    }
}
