// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Exact rationals: the only numeric type at the ZXV boundary.

use std::cmp::Ordering;
use std::fmt;
use std::str::FromStr;

use zxv_sys as sys;

use crate::error::{check, Error, Result};
use crate::ffi;

/// An exact rational number `num / den`, always reduced with `den > 0`.
///
/// `Rational` is a small `Copy` value (two `i64`). Arithmetic goes through
/// libzxv and is checked: overflow is an error ([`Error::Usage`] with
/// `ZXV_E_RANGE`), never a silent wrap or a rounding. There are deliberately
/// no `+ - * /` operator impls; use [`checked_add`](Self::checked_add) and
/// friends, so every overflow is a `Result` you handle.
///
/// ```
/// use zxv::Rational;
/// let a: Rational = "0.1".parse()?;
/// let b: Rational = "0.2".parse()?;
/// assert_eq!(a.checked_add(b)?, Rational::new(3, 10)?);
/// assert_eq!(a.checked_add(b)?.to_string(), "0.3");
/// # Ok::<(), zxv::Error>(())
/// ```
#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct Rational {
    num: i64,
    den: i64,
}

impl Rational {
    /// Zero (`0/1`).
    pub const ZERO: Rational = Rational { num: 0, den: 1 };
    /// One (`1/1`).
    pub const ONE: Rational = Rational { num: 1, den: 1 };

    /// `num / den`, reduced. A zero denominator is a usage error
    /// (`ZXV_E_ARG`); `i64::MIN` in either place is `ZXV_E_RANGE`.
    pub fn new(num: i64, den: i64) -> Result<Rational> {
        let (mut n, mut d) = (0, 1);
        // SAFETY: pure function; out pointers are valid.
        check(unsafe { sys::zxv_rat_normalize(num, den, &mut n, &mut d) })?;
        Ok(Rational { num: n, den: d })
    }

    /// The integer `n` (refuses `i64::MIN`, whose negation is not an `i64`).
    pub fn from_integer(n: i64) -> Result<Rational> {
        Rational::new(n, 1)
    }

    /// The numerator (carries the sign).
    pub fn numer(self) -> i64 {
        self.num
    }

    /// The denominator (always > 0).
    pub fn denom(self) -> i64 {
        self.den
    }

    /// `self + other`, exactly.
    pub fn checked_add(self, other: Rational) -> Result<Rational> {
        self.op(other, sys::zxv_rat_add)
    }

    /// `self - other`, exactly.
    pub fn checked_sub(self, other: Rational) -> Result<Rational> {
        self.op(other, sys::zxv_rat_sub)
    }

    /// `self * other`, exactly.
    pub fn checked_mul(self, other: Rational) -> Result<Rational> {
        self.op(other, sys::zxv_rat_mul)
    }

    /// `self / other`, exactly. Division by zero is `ZXV_E_ARG`.
    pub fn checked_div(self, other: Rational) -> Result<Rational> {
        self.op(other, sys::zxv_rat_div)
    }

    /// `-self` (cannot overflow: the numerator is never `i64::MIN`).
    #[allow(clippy::should_implement_trait)]
    pub fn neg(self) -> Rational {
        Rational {
            num: -self.num,
            den: self.den,
        }
    }

    /// `|self|`.
    pub fn abs(self) -> Rational {
        Rational {
            num: self.num.abs(),
            den: self.den,
        }
    }

    /// -1, 0 or +1.
    pub fn signum(self) -> i32 {
        self.num.signum() as i32
    }

    /// True for zero.
    pub fn is_zero(self) -> bool {
        self.num == 0
    }

    /// True when the denominator is 1.
    pub fn is_integer(self) -> bool {
        self.den == 1
    }

    /// Q16.16 fixed point (round half away from zero), as the engine's
    /// sampling parameters use. `ZXV_E_RANGE` outside `i32`.
    pub fn to_q16(self) -> Result<i32> {
        let mut q = 0;
        // SAFETY: pure function; out pointer valid.
        check(unsafe { sys::zxv_rat_to_q16(self.num, self.den, &mut q) })?;
        Ok(q)
    }

    /// THE FLOAT BOUNDARY: the exact value of a binary64 `f64`.
    ///
    /// Every finite double is an exact rational, and this returns that
    /// value, so `0.1` becomes `3602879701896397/36028797018963968`, not
    /// `1/10`. NaN and infinities are a usage error (`ZXV_E_FLOAT`); a value
    /// that needs more than 63 bits (e.g. `1e-300`) is `ZXV_E_RANGE`.
    /// This is the only function in the crate that accepts a float.
    pub fn from_f64_exact(x: f64) -> Result<Rational> {
        let (mut n, mut d) = (0, 1);
        // SAFETY: pure function; out pointers valid.
        check(unsafe { sys::zxv_rat_from_ieee754(x.to_bits(), &mut n, &mut d) })?;
        Ok(Rational { num: n, den: d })
    }

    /// Like [`from_f64_exact`](Self::from_f64_exact) for raw IEEE-754 bits.
    pub fn from_ieee754_bits(bits: u64) -> Result<Rational> {
        let (mut n, mut d) = (0, 1);
        // SAFETY: pure function; out pointers valid.
        check(unsafe { sys::zxv_rat_from_ieee754(bits, &mut n, &mut d) })?;
        Ok(Rational { num: n, den: d })
    }

    fn op(
        self,
        o: Rational,
        f: unsafe extern "C" fn(i64, i64, i64, i64, *mut i64, *mut i64) -> i32,
    ) -> Result<Rational> {
        let (mut n, mut d) = (0, 1);
        // SAFETY: pure function; out pointers valid.
        check(unsafe { f(self.num, self.den, o.num, o.den, &mut n, &mut d) })?;
        Ok(Rational { num: n, den: d })
    }
}

impl Default for Rational {
    fn default() -> Self {
        Rational::ZERO
    }
}

impl From<i32> for Rational {
    fn from(n: i32) -> Rational {
        Rational {
            num: n as i64,
            den: 1,
        }
    }
}

impl From<u32> for Rational {
    fn from(n: u32) -> Rational {
        Rational {
            num: n as i64,
            den: 1,
        }
    }
}

impl Ord for Rational {
    /// Exact comparison by cross-multiplication in 128 bits (no epsilon).
    fn cmp(&self, other: &Self) -> Ordering {
        (self.num as i128 * other.den as i128).cmp(&(other.num as i128 * self.den as i128))
    }
}

impl PartialOrd for Rational {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl FromStr for Rational {
    type Err = Error;

    /// Parses `"12.75"`, `"-0.1"`, `"3/4"` or `"42"` exactly. Exponents,
    /// spaces and `"NaN"` are `ZXV_E_FORMAT`.
    fn from_str(s: &str) -> Result<Rational> {
        let c = ffi::cstr(s)?;
        let (mut n, mut d) = (0, 1);
        // SAFETY: c is a valid C string; out pointers valid.
        check(unsafe { sys::zxv_rat_parse(c.as_ptr(), &mut n, &mut d) })?;
        Ok(Rational { num: n, den: d })
    }
}

impl fmt::Display for Rational {
    /// A terminating decimal when one exists (`"12.75"`), else `"n/d"`.
    /// Never rounds.
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let (n, d) = (self.num, self.den);
        // SAFETY: the closure follows R4 with the buffer read_string gives it.
        match ffi::read_string(|b, c, l| unsafe { sys::zxv_rat_format(n, d, b, c, l) }) {
            Ok(s) => f.pad(&s),
            Err(_) => write!(f, "{n}/{d}"),
        }
    }
}

impl fmt::Debug for Rational {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Rational({}/{})", self.num, self.den)
    }
}
