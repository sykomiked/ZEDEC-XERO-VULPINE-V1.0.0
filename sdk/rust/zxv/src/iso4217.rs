// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! ISO 4217 reference data, Vino rails and card check digits (process-wide,
//! read-only; no context or capability needed).

use zxv_sys as sys;

use crate::error::{check, Result};
use crate::ffi;

/// `ZXV_CCYF_*` currency flags.
pub mod flags {
    /// Fund code (e.g. CLF, USN).
    pub const FUND: u32 = zxv_sys::ZXV_CCYF_FUND;
    /// Minor unit N.A. (metals, XDR, XTS, XXX).
    pub const MINOR_NA: u32 = zxv_sys::ZXV_CCYF_MINOR_NA;
    /// Not the currency of any ISO 3166 country.
    pub const NO_COUNTRY: u32 = zxv_sys::ZXV_CCYF_NOCTRY;
    /// Usable for a payment amount.
    pub const PAYABLE: u32 = zxv_sys::ZXV_CCYF_PAYABLE;
    /// Platform-private unit registered in a context.
    pub const PRIVATE: u32 = zxv_sys::ZXV_CCYF_PRIVATE;
}

/// One ISO 4217 entry.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Currency {
    /// Alpha-3 code (`"NGN"`).
    pub alpha: String,
    /// Numeric code (566).
    pub numeric: u16,
    /// Minor units, `None` when ISO says N.A.
    pub minor_units: Option<u8>,
    /// [`flags`] bits.
    pub flags: u32,
}

fn minor(m: u8) -> Option<u8> {
    if m as u32 == sys::ZXV_MINOR_NA {
        None
    } else {
        Some(m)
    }
}

/// Looks up a currency by alpha-3 code (`ZXV_E_NOT_FOUND` for non-ISO
/// codes, including platform units such as VFV).
pub fn by_alpha(alpha: &str) -> Result<Currency> {
    let c = ffi::cstr(alpha)?;
    let (mut num, mut mu, mut fl) = (0u16, 0u8, 0u32);
    // SAFETY: valid C string and out pointers.
    check(unsafe { sys::zxv_iso4217_by_alpha(c.as_ptr(), &mut num, &mut mu, &mut fl) })?;
    Ok(Currency {
        alpha: alpha.to_string(),
        numeric: num,
        minor_units: minor(mu),
        flags: fl,
    })
}

/// Looks up a currency by numeric code (566 -> NGN). 555/777/888 are not
/// ISO codes and give `ZXV_E_NOT_FOUND`.
pub fn by_numeric(numeric: u16) -> Result<Currency> {
    let (mut mu, mut fl) = (0u8, 0u32);
    // SAFETY: R4 closure; out pointers valid.
    let alpha = ffi::read_string(|b, c, l| unsafe {
        sys::zxv_iso4217_by_numeric(numeric, b, c, l, &mut mu, &mut fl)
    })?;
    Ok(Currency {
        alpha,
        numeric,
        minor_units: minor(mu),
        flags: fl,
    })
}

/// English name of a currency (`"Naira"`).
pub fn name(alpha: &str) -> Result<String> {
    let a = ffi::cstr(alpha)?;
    // SAFETY: R4 closure.
    ffi::read_string(|b, c, l| unsafe { sys::zxv_iso4217_name(a.as_ptr(), b, c, l) })
}

/// Every alpha-3 code in the compiled table.
pub fn all() -> Result<Vec<String>> {
    let mut n = 0u32;
    // SAFETY: out pointer valid.
    check(unsafe { sys::zxv_iso4217_count(&mut n) })?;
    // SAFETY: R4 closure.
    (0..n)
        .map(|i| ffi::read_string(|b, c, l| unsafe { sys::zxv_iso4217_at(i, b, c, l) }))
        .collect()
}

/// Publication date of the compiled list (`"2026-01-01"`).
pub fn published() -> Result<String> {
    // SAFETY: R4 closure.
    ffi::read_string(|b, c, l| unsafe { sys::zxv_iso4217_published(b, c, l) })
}

/// Interop caveat for a numeric code (e.g. 777, the Vino CREDIT rail);
/// empty when none.
pub fn caveat(numeric: u16) -> Result<String> {
    // SAFETY: R4 closure.
    ffi::read_string(|b, c, l| unsafe { sys::zxv_ccy_caveat(numeric, b, c, l) })
}

/// True if the ISO 3166 alpha-2 country is an African Union member.
pub fn is_au_member(country: &str) -> Result<bool> {
    let c = ffi::cstr(country)?;
    let mut m = 0;
    // SAFETY: valid C string and out pointer.
    check(unsafe { sys::zxv_au_is_member(c.as_ptr(), &mut m) })?;
    Ok(m != 0)
}

/// The three Vino rails of every account.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Rail {
    /// 555 NCR: asset / backing.
    Debit,
    /// 777 NRE: claim / liability.
    Credit,
    /// 888 PNS: debit - credit.
    Equity,
}

impl Rail {
    /// The platform-internal numeric (555, 777, 888). Never an ISO code.
    pub fn numeric(self) -> u16 {
        let r = match self {
            Rail::Debit => sys::ZXV_RAIL_DEBIT,
            Rail::Credit => sys::ZXV_RAIL_CREDIT,
            Rail::Equity => sys::ZXV_RAIL_EQUITY,
        };
        let mut n = 0u16;
        // SAFETY: valid rail; out pointer valid.
        let rc = unsafe { sys::zxv_rail_numeric(r as i32, &mut n) };
        debug_assert_eq!(rc, 0);
        n
    }
}

/// Card networks and their check-digit schemes.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum CardNetwork {
    /// Damm check digit.
    Phoenix,
    /// Luhn check digit.
    Dragon,
    /// Verhoeff check digit.
    Thunderbird,
}

impl CardNetwork {
    /// True if the decimal `pan` carries a valid check digit for this network.
    pub fn check_digit_valid(self, pan: &str) -> Result<bool> {
        let n = match self {
            CardNetwork::Phoenix => sys::ZXV_NET_PHOENIX,
            CardNetwork::Dragon => sys::ZXV_NET_DRAGON,
            CardNetwork::Thunderbird => sys::ZXV_NET_THUNDERBIRD,
        };
        let p = ffi::cstr(pan)?;
        let mut v = 0;
        // SAFETY: valid C string and out pointer.
        check(unsafe { sys::zxv_card_check_digit(n as i32, p.as_ptr(), &mut v) })?;
        Ok(v != 0)
    }
}
