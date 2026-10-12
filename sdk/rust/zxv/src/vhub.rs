// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! The V-Bill hub (ABI 1.5): a demonstration of cross-currency clearing
//! through V-Bills, with test currencies, signed exact pegs, quotes, swaps
//! and limit rules.
//!
//! Each test currency has ONE exact peg against the V-Bill, set only by its
//! issuer with an ML-DSA-87 signature. A swap from A to B goes
//! A -> V-Bill -> B as one balanced, all-or-nothing posting on the hub's
//! own in-process ledger (`v = a*denA/numA`, `b = v*numB/denB`). Both
//! divisions must be exact or the swap is refused (`ZXV_E_RANGE`); nothing
//! is rounded and there is no float rate anywhere. An exact peg removes
//! rounding, not exchange-rate risk.
//!
//! Every rule of the sending AND the receiving currency must allow a swap;
//! a currency with no rule refuses every swap through it (fail closed,
//! `ZXV_E_STATE`). A limit refusal is `ZXV_E_LIMIT`.
//!
//! Creating a hub, adding currencies and rules, setting pegs, opening and
//! funding accounts and swapping need
//! [`Caps::LEDGER_POST`](crate::Caps::LEDGER_POST); quotes, balances,
//! currency info and the check need
//! [`Caps::LEDGER_READ`](crate::Caps::LEDGER_READ). A [`VHub`] uses the
//! capability it was created with for every call, so give it both bits.
//!
//! **Honest limits:** a demo hub with test currencies only (ISO 4217 codes
//! are refused) and no real money: its ledger lives in process memory,
//! separate from the context's ledger, and is lost on drop. No network: it
//! is connected to no settlement rail, central bank or issuer. Peg
//! signatures use ML-DSA-87 (not FIPS 140-3 validated). V-Bills are likely
//! securities in many jurisdictions; real use needs legal review and a
//! participating issuer. A rule reports what it decided; it is not itself a
//! legal check.

use std::fmt;
use std::ptr::NonNull;

use zxv_sys as sys;

use crate::capability::Capability;
use crate::crypto::sizes;
use crate::error::{check, Result};
use crate::ffi::{cstr, need_len, non_null};

/// Currency slots 0..8.
pub const MAX_CURRENCIES: u32 = sys::ZXV_VHUB_MAX_CCY;
/// Limit rules per currency.
pub const MAX_RULES: u32 = sys::ZXV_VHUB_MAX_RULES;
/// Senders one limit tracks per epoch.
pub const LIMIT_SENDERS: u32 = sys::ZXV_VHUB_LIMIT_SENDERS;
/// Size of a [`peg_message`].
pub const PEG_MSG_BYTES: usize = sys::ZXV_VHUB_PEG_MSG_BYTES as usize;
/// Longest test currency code.
pub const CODE_MAX: usize = sys::ZXV_VHUB_CODE_MAX as usize;
/// Largest single amount (2^59); balances stay below 2^62.
pub const AMOUNT_MAX: u64 = sys::ZXV_VHUB_AMOUNT_MAX;
/// The V-Bill issuer: always account 0.
pub const VBILL_ISSUER: u32 = sys::ZXV_VHUB_VBILL_ISSUER;

/// A currency's accounts and current peg ([`VHub::currency`]).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct VHubCurrency {
    /// The currency issuer's account.
    pub issuer_account: u32,
    /// The hub's pool of the currency.
    pub pool_account: u32,
    /// The currency's V-Bill float.
    pub float_account: u32,
    /// The peg: `peg_num` minor units per `peg_den` V-Bills.
    pub peg_num: u64,
    /// 0 while the peg is unset.
    pub peg_den: u64,
    /// 0 while the peg is unset.
    pub peg_epoch: u64,
}

/// One account's rails.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct VHubBalance {
    /// What the account holds.
    pub debit: u64,
    /// What it owes (issuers only).
    pub credit: u64,
    /// `debit - credit`.
    pub equity: i64,
}

/// What a swap would carry ([`VHub::quote`]).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct VHubQuote {
    /// The largest exact amount not above the request (0 if none).
    pub amount: u64,
    /// V-Bills it would move.
    pub vbills: u64,
    /// Units of the target currency it would pay out.
    pub received: u64,
}

/// What a swap moved ([`VHub::swap`]).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct VHubSwap {
    /// V-Bills moved between the two floats.
    pub vbills: u64,
    /// Units the receiver got.
    pub received: u64,
}

/// The exact 40 bytes an issuer signs to set a peg:
/// `"zxv/vhub-peg/v1" || slot || num || den || epoch` (little-endian u64s).
/// Sign them with ML-DSA-87 and an empty context.
pub fn peg_message(slot: u8, num: u64, den: u64, epoch: u64) -> Result<[u8; PEG_MSG_BYTES]> {
    let mut m = [0u8; PEG_MSG_BYTES];
    // SAFETY: pure; out has 40 bytes.
    check(unsafe { sys::zxv_vhub_peg_msg(slot, num, den, epoch, m.as_mut_ptr()) })?;
    Ok(m)
}

/// One demo hub and its ledger (`zxv_vhub`). `Send + Sync` (the C hub
/// serializes its calls).
///
/// ```
/// use zxv::{Caps, Context, VHub};
/// let ctx = Context::new()?;
/// let cap = ctx.root_capability(1, None, Caps::ALL_1_5)?;
/// let hub = VHub::new(&cap, Some(&[7u8; 32]))?;
/// assert_eq!(hub.check()?, 0); // no postings yet
/// # Ok::<(), zxv::Error>(())
/// ```
pub struct VHub<'a> {
    raw: NonNull<sys::zxv_vhub>,
    cap: &'a Capability<'a>,
}

// SAFETY: the C hub serializes every call.
unsafe impl Send for VHub<'_> {}
// SAFETY: as above.
unsafe impl Sync for VHub<'_> {}

impl<'a> VHub<'a> {
    /// A hub on `cap`'s context (`LEDGER_POST`; give it `LEDGER_READ` too).
    /// Its ledger starts with the V-Bill asset "VBL" and the V-Bill
    /// issuer's account ([`VBILL_ISSUER`]). `seed` derives every posting's
    /// idempotency key and UETR; `None` draws a fresh random seed.
    pub fn new(cap: &'a Capability<'a>, seed: Option<&[u8; 32]>) -> Result<VHub<'a>> {
        let sp = seed.map_or(std::ptr::null(), |s| s.as_ptr());
        let mut raw = std::ptr::null_mut();
        // SAFETY: valid cap; seed NULL or 32 bytes; out pointer valid.
        cap.call(|c| unsafe { sys::zxv_vhub_create(c, sp, &mut raw) })?;
        Ok(VHub {
            raw: non_null(raw, "vhub")?,
            cap,
        })
    }

    fn call(&self, f: impl FnOnce(*const sys::zxv_cap, *mut sys::zxv_vhub) -> i32) -> Result<()> {
        let h = self.raw.as_ptr();
        self.cap.call(|c| f(c, h))
    }

    /// Registers test currency `slot` (0..8): its code (3..12 of A-Z and
    /// 0-9; not ISO 4217, not "VBL" or "VFV"), its minor units (0..4) and
    /// its issuer's ML-DSA-87 public key (it alone may set the peg). Opens
    /// the issuer's account, the pool and the V-Bill float, and funds the
    /// pool with `pool_funding` and the float with `vbill_backing`. The peg
    /// starts unset. `ZXV_E_CURRENCY` for an ISO code, `ZXV_E_DUPLICATE`
    /// for a taken slot or code. A failure registers nothing.
    pub fn add_currency(
        &self,
        slot: u8,
        code: &str,
        minor_units: u8,
        issuer_pk: &[u8],
        pool_funding: u64,
        vbill_backing: u64,
    ) -> Result<()> {
        need_len(issuer_pk, sizes::MLDSA87_PK, "issuer public key")?;
        let cs = cstr(code)?;
        // SAFETY: valid cap and hub; code NUL-terminated; key 2592 bytes.
        self.call(|c, h| unsafe {
            sys::zxv_vhub_add_ccy(
                c,
                h,
                slot,
                cs.as_ptr(),
                minor_units,
                issuer_pk.as_ptr(),
                pool_funding,
                vbill_backing,
            )
        })
    }

    /// A currency's accounts and current peg (`ZXV_E_NOT_FOUND` for an
    /// unregistered slot).
    pub fn currency(&self, slot: u8) -> Result<VHubCurrency> {
        let (mut ia, mut pa, mut va) = (0u32, 0u32, 0u32);
        let (mut n, mut d, mut e) = (0u64, 0u64, 0u64);
        // SAFETY: valid cap and hub; out pointers valid.
        self.call(|c, h| unsafe {
            sys::zxv_vhub_ccy_info(
                c, h, slot, &mut ia, &mut pa, &mut va, &mut n, &mut d, &mut e,
            )
        })?;
        Ok(VHubCurrency {
            issuer_account: ia,
            pool_account: pa,
            float_account: va,
            peg_num: n,
            peg_den: d,
            peg_epoch: e,
        })
    }

    /// Sets a currency's peg to `num/den` at `epoch` (above the current
    /// one), given the issuer's ML-DSA-87 signature (empty context) over
    /// [`peg_message`]. `ZXV_E_PEG` for a stale or replayed epoch,
    /// `ZXV_E_VERIFY` for a wrong signature, `ZXV_E_RANGE` for a zero num
    /// or den. A refused peg changes nothing.
    pub fn set_peg(&self, slot: u8, num: u64, den: u64, epoch: u64, sig: &[u8]) -> Result<()> {
        need_len(sig, sizes::MLDSA87_SIG, "peg signature")?;
        // SAFETY: valid cap and hub; sig has the signature length.
        self.call(|c, h| unsafe {
            sys::zxv_vhub_set_peg(c, h, slot, num, den, epoch, sig.as_ptr())
        })
    }

    /// Opens a holder account in currency `slot` for `owner` (a caller's
    /// id) and returns its number. `ZXV_E_CAPACITY` at 512 accounts.
    pub fn open_account(&self, slot: u8, owner: u32) -> Result<u32> {
        let mut a = 0u32;
        // SAFETY: valid cap and hub; out pointer valid.
        self.call(|c, h| unsafe { sys::zxv_vhub_open_account(c, h, slot, owner, &mut a) })?;
        Ok(a)
    }

    /// The test faucet: issues `amount` to a holder account or a pool (by
    /// that currency's issuer), or V-Bills to a float (by the V-Bill
    /// issuer). `ZXV_E_RANGE` for 0, above [`AMOUNT_MAX`], or a balance
    /// that would reach 2^62.
    pub fn issue(&self, account: u32, amount: u64) -> Result<()> {
        // SAFETY: valid cap and hub.
        self.call(|c, h| unsafe { sys::zxv_vhub_issue(c, h, account, amount) })
    }

    /// One account's rails (`ZXV_E_NOT_FOUND` past the last account).
    pub fn balance(&self, account: u32) -> Result<VHubBalance> {
        let (mut d, mut cr, mut e) = (0u64, 0u64, 0i64);
        // SAFETY: valid cap and hub; out pointers valid.
        self.call(|c, h| unsafe { sys::zxv_vhub_balance(c, h, account, &mut d, &mut cr, &mut e) })?;
        Ok(VHubBalance {
            debit: d,
            credit: cr,
            equity: e,
        })
    }

    /// Adds the built-in limit rule to currency `slot`: at most `limit`
    /// minor units out per sender per rule epoch. One limit tracks
    /// [`LIMIT_SENDERS`] senders per epoch and refuses the next (fail
    /// closed). `ZXV_E_CAPACITY` after [`MAX_RULES`] rules.
    pub fn add_limit(&self, slot: u8, limit: u64) -> Result<()> {
        // SAFETY: valid cap and hub.
        self.call(|c, h| unsafe { sys::zxv_vhub_add_limit(c, h, slot, limit) })
    }

    /// Starts the next rule epoch (every limit starts every sender at zero)
    /// and returns it.
    pub fn next_epoch(&self) -> Result<u64> {
        let mut e = 0u64;
        // SAFETY: valid cap and hub; out pointer valid.
        self.call(|c, h| unsafe { sys::zxv_vhub_next_epoch(c, h, &mut e) })?;
        Ok(e)
    }

    /// The largest exact amount of currency `from` not above `want`, and
    /// the V-Bills and `to` units it would carry. It runs no rules and
    /// checks no balances. `ZXV_E_PEG` when a peg is unset.
    pub fn quote(&self, from: u8, to: u8, want: u64) -> Result<VHubQuote> {
        let (mut a, mut v, mut r) = (0u64, 0u64, 0u64);
        // SAFETY: valid cap and hub; out pointers valid.
        self.call(|c, h| unsafe {
            sys::zxv_vhub_quote(c, h, from, to, want, &mut a, &mut v, &mut r)
        })?;
        Ok(VHubQuote {
            amount: a,
            vbills: v,
            received: r,
        })
    }

    /// Moves `amount` minor units of currency `from` out of holder account
    /// `sender` and pays holder account `receiver` in currency `to`, as one
    /// six-line posting at `tick` (a caller time, never the wall clock).
    /// `ZXV_E_STATE` (no rule), `ZXV_E_LIMIT`, `ZXV_E_FUNDS`, `ZXV_E_PEG`,
    /// `ZXV_E_RANGE` (not exact), `ZXV_E_CURRENCY`. A refused swap changes
    /// nothing.
    pub fn swap(
        &self,
        from: u8,
        to: u8,
        sender: u32,
        receiver: u32,
        amount: u64,
        tick: u64,
    ) -> Result<VHubSwap> {
        let (mut v, mut r) = (0u64, 0u64);
        // SAFETY: valid cap and hub; out pointers valid.
        self.call(|c, h| unsafe {
            sys::zxv_vhub_swap(
                c, h, from, to, sender, receiver, amount, tick, &mut v, &mut r,
            )
        })?;
        Ok(VHubSwap {
            vbills: v,
            received: r,
        })
    }

    /// Verifies the whole hub ledger and returns the postings made so far.
    /// A failure is FATAL and poisons the context.
    pub fn check(&self) -> Result<u64> {
        let mut n = 0u64;
        // SAFETY: valid cap and hub; out pointer valid.
        self.call(|c, h| unsafe { sys::zxv_vhub_check(c, h, &mut n) })?;
        Ok(n)
    }
}

impl fmt::Debug for VHub<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("VHub")
    }
}

impl Drop for VHub<'_> {
    fn drop(&mut self) {
        // SAFETY: owned handle destroyed once, before its capability.
        unsafe { sys::zxv_vhub_destroy(self.raw.as_ptr()) }
    }
}
