// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! The assurance fee (ABI 1.5): 0.08889% of a transaction's gross and its
//! 50/25/15/10 split, exact and integer-only.
//!
//! `fee(g) = floor(g * 8889 / 10_000_000)`, formed in 128 bits, so every
//! `g` up to `u64::MAX` is exact; `fee(1124) = 0` and `fee(1125) = 1`. The
//! fee is charged once per transaction, on its gross: never on a fee,
//! never compounded. The sub-unit remainder of an account is carried to
//! its next charge ([`assurance_fee_carry`]); the caller stores it.
//!
//! A fee is split 50% reserve floor, 25% V-Bill dividend pool, 15%
//! infrastructure / node bounties, 10% regenerative capital; buckets 1-3
//! get `floor(fee * pct / 100)` and the reserve floor the rest, so the
//! parts always sum to the fee.
//!
//! Everything here is pure. **Honest limits:** these are the arithmetic
//! rules only. How a fee, a reserve or a dividend pool is treated for tax,
//! securities or consumer law is a question for counsel; nothing here is a
//! licence or a certification.
//!
//! ```
//! use zxv::fee::{assurance_fee, split_fee};
//! assert_eq!(assurance_fee(1125)?, 1);
//! let s = split_fee(1000)?;
//! assert_eq!(s.0, [500, 250, 150, 100]);
//! # Ok::<(), zxv::Error>(())
//! ```

use zxv_sys as sys;

use crate::error::{check, Result};

/// The fee rate's numerator (8889).
pub const FEE_NUM: u64 = sys::ZXV_FEE_NUM as u64;
/// The fee rate's denominator (10^7); a carry is always below it.
pub const FEE_DEN: u64 = sys::ZXV_FEE_DEN as u64;

/// A bucket of a [`FeeSplit`].
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum FeeBucket {
    /// 50% and every remainder unit.
    ReserveFloor = sys::ZXV_FEE_RESERVE_FLOOR as isize,
    /// 25%.
    VBillDividend = sys::ZXV_FEE_VBILL_DIVIDEND as isize,
    /// 15%.
    InfraBounty = sys::ZXV_FEE_INFRA_BOUNTY as isize,
    /// 10%.
    RegenCapital = sys::ZXV_FEE_REGEN_CAPITAL as isize,
}

impl FeeBucket {
    /// The bucket's name, e.g. `"reserve_floor"`.
    pub fn name(self) -> &'static str {
        match self {
            FeeBucket::ReserveFloor => "reserve_floor",
            FeeBucket::VBillDividend => "vbill_dividend",
            FeeBucket::InfraBounty => "infra_bounty",
            FeeBucket::RegenCapital => "regen_capital",
        }
    }
}

/// The kind of unit a gross is measured in.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum FeeUnit {
    /// Minor units of a ledger asset.
    Money = sys::ZXV_FEE_UNIT_MONEY as isize,
    /// Token-cycles.
    Compute = sys::ZXV_FEE_UNIT_COMPUTE as isize,
    /// Bytes.
    Bandwidth = sys::ZXV_FEE_UNIT_BANDWIDTH as isize,
    /// Byte-hours.
    Storage = sys::ZXV_FEE_UNIT_STORAGE as isize,
}

/// A fee split into its four buckets, indexed by [`FeeBucket`]. The parts
/// always sum to the fee exactly.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
pub struct FeeSplit(pub [u64; 4]);

impl FeeSplit {
    /// The part in `bucket`.
    pub fn get(&self, bucket: FeeBucket) -> u64 {
        self.0[bucket as usize]
    }

    /// The sum of the four parts (the fee that was split).
    pub fn total(&self) -> u64 {
        self.0.iter().sum()
    }
}

/// `fee(gross)` with no carry. It fails only when the fee group is not
/// built (`ZXV_E_NOTIMPL`).
pub fn assurance_fee(gross: u64) -> Result<u64> {
    let mut f = 0u64;
    // SAFETY: pure; out pointer valid.
    check(unsafe { sys::zxv_fee_assure(gross, &mut f) })?;
    Ok(f)
}

/// The fee on `gross` for an account whose stored remainder is `rem_in`,
/// and the remainder to store for its next charge. `ZXV_E_RANGE` when
/// `rem_in >= FEE_DEN` (a corrupt carry).
pub fn assurance_fee_carry(rem_in: u64, gross: u64) -> Result<(u64, u64)> {
    let (mut f, mut r) = (0u64, 0u64);
    // SAFETY: pure; out pointers valid.
    check(unsafe { sys::zxv_fee_assure_carry(rem_in, gross, &mut f, &mut r) })?;
    Ok((f, r))
}

/// Splits `fee` into the four buckets (the reserve floor takes every
/// remainder unit). The partition is re-checked before it is returned.
pub fn split_fee(fee: u64) -> Result<FeeSplit> {
    let mut s = FeeSplit::default();
    // SAFETY: out has 4 slots.
    check(unsafe { sys::zxv_fee_split(fee, s.0.as_mut_ptr()) })?;
    Ok(s)
}

/// An exact rate `num / den`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
pub struct FeeRate {
    /// Numerator.
    pub num: u64,
    /// Denominator.
    pub den: u64,
}

/// The default fee policy ([`fee_policy_default`]).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FeePolicy {
    /// Each bucket's share in percent: 50, 25, 15, 10.
    pub percent: [u32; 4],
    /// False: a contribution below the fee is raised to it.
    pub allow_below: bool,
    /// The posted VFV store-credit rate per [`FeeUnit`] for a voluntary
    /// contribution above the fee: money 1/1, the others 0/1 (no credit
    /// until a rate is posted).
    pub credit_rate: [FeeRate; 4],
    /// The operator's credit multiplier, 1/1.
    pub multiplier: FeeRate,
    /// The no-monopoly cap on one recipient's share of the non-money
    /// commons pool, 8/21.
    pub commons_cap: FeeRate,
}

/// The default policy. It fails only when the fee group is not built.
pub fn fee_policy_default() -> Result<FeePolicy> {
    let mut pct = [0u32; 4];
    let mut below = 0i32;
    let (mut rn, mut rd) = ([0u64; 4], [0u64; 4]);
    let (mut mn, mut md, mut cn, mut cd) = (0u64, 0u64, 0u64, 0u64);
    // SAFETY: arrays have 4 slots; out pointers valid.
    check(unsafe {
        sys::zxv_fee_policy_default(
            pct.as_mut_ptr(),
            &mut below,
            rn.as_mut_ptr(),
            rd.as_mut_ptr(),
            &mut mn,
            &mut md,
            &mut cn,
            &mut cd,
        )
    })?;
    let mut credit_rate = [FeeRate::default(); 4];
    for (i, r) in credit_rate.iter_mut().enumerate() {
        *r = FeeRate {
            num: rn[i],
            den: rd[i],
        };
    }
    Ok(FeePolicy {
        percent: pct,
        allow_below: below != 0,
        credit_rate,
        multiplier: FeeRate { num: mn, den: md },
        commons_cap: FeeRate { num: cn, den: cd },
    })
}
