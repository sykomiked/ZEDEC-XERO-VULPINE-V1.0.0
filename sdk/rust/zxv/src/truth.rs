// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Four-valued (paraconsistent) logic.

use std::fmt;

use zxv_sys as sys;

use crate::error::{check, Error, Result};

/// A four-valued truth value (Belnap's FOUR, the kernel's `lpres` states).
///
/// `Both` is a held contradiction, not an error; `Neither` is "unknown".
/// There is deliberately no `From<Truth> for bool` and no `Deref` to a
/// bool: converting needs an explicit policy, such as
/// [`is_certainly_true`](Self::is_certainly_true),
/// [`to_bool`](Self::to_bool) or [`require_true`](Self::require_true).
///
/// The discriminants are the kernel's numbers and never change.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum Truth {
    /// S0: unknown, unattested, pending.
    Neither = 0,
    /// S+: attested present and valid.
    True = 1,
    /// S-: attested absent or invalid.
    False = 2,
    /// Contradictory evidence, held.
    Both = 3,
}

impl Truth {
    /// All four values in kernel order.
    pub const ALL: [Truth; 4] = [Truth::Neither, Truth::True, Truth::False, Truth::Both];

    /// The value with kernel number `v` (0..=3).
    pub fn from_raw(v: i32) -> Result<Truth> {
        match v {
            0 => Ok(Truth::Neither),
            1 => Ok(Truth::True),
            2 => Ok(Truth::False),
            3 => Ok(Truth::Both),
            _ => Err(Error::with_detail(
                sys::ZXV_E_ARG,
                format!("{v} is not a truth value"),
            )),
        }
    }

    /// The kernel number.
    pub fn raw(self) -> i32 {
        self as i32
    }

    /// From evidence counts: both kinds give `Both`, only positive `True`,
    /// only negative `False`, none `Neither`.
    pub fn from_evidence(positive: u32, negative: u32) -> Truth {
        let mut r = 0;
        // SAFETY: pure function; out pointer valid.
        let rc = unsafe { sys::zxv_lp_from_evidence(positive, negative, &mut r) };
        debug_assert_eq!(rc, 0);
        Truth::from_raw(r).unwrap_or(Truth::Neither)
    }

    /// Negation (`True` <-> `False`; `Both` and `Neither` are fixed).
    #[allow(clippy::should_implement_trait)]
    pub fn not(self) -> Truth {
        self.lift1(sys::zxv_lp_not)
    }

    /// Conjunction, following the C tables.
    pub fn and(self, other: Truth) -> Truth {
        self.lift2(other, sys::zxv_lp_and)
    }

    /// Disjunction, following the C tables.
    pub fn or(self, other: Truth) -> Truth {
        self.lift2(other, sys::zxv_lp_or)
    }

    /// True only for `True`.
    pub fn is_certainly_true(self) -> bool {
        self == Truth::True
    }

    /// True only for `False`.
    pub fn is_certainly_false(self) -> bool {
        self == Truth::False
    }

    /// True for `True` and `False` (no contradiction, no gap).
    pub fn is_certain(self) -> bool {
        matches!(self, Truth::True | Truth::False)
    }

    /// Converts with an explicit policy for the two non-classical values.
    pub fn to_bool(self, on_both: bool, on_neither: bool) -> bool {
        match self {
            Truth::True => true,
            Truth::False => false,
            Truth::Both => on_both,
            Truth::Neither => on_neither,
        }
    }

    /// `Ok(())` for `True`, otherwise a REFUSED error naming the value.
    pub fn require_true(self) -> Result<()> {
        if self == Truth::True {
            Ok(())
        } else {
            Err(Error::with_detail(
                sys::ZXV_E_VERIFY,
                format!("required TRUE, got {}", self.name()),
            ))
        }
    }

    /// The kernel name: `"NEITHER"`, `"TRUE"`, `"FALSE"` or `"BOTH"`.
    pub fn name(self) -> &'static str {
        match self {
            Truth::Neither => "NEITHER",
            Truth::True => "TRUE",
            Truth::False => "FALSE",
            Truth::Both => "BOTH",
        }
    }

    fn lift1(self, f: unsafe extern "C" fn(i32, *mut i32) -> i32) -> Truth {
        let mut r = 0;
        // SAFETY: pure function on a valid value; out pointer valid.
        let rc = unsafe { f(self.raw(), &mut r) };
        check(rc).expect("libzxv refused a valid truth value");
        Truth::from_raw(r).expect("libzxv returned an invalid truth value")
    }

    fn lift2(self, o: Truth, f: unsafe extern "C" fn(i32, i32, *mut i32) -> i32) -> Truth {
        let mut r = 0;
        // SAFETY: pure function on valid values; out pointer valid.
        let rc = unsafe { f(self.raw(), o.raw(), &mut r) };
        check(rc).expect("libzxv refused a valid truth value");
        Truth::from_raw(r).expect("libzxv returned an invalid truth value")
    }
}

impl fmt::Display for Truth {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.pad(self.name())
    }
}
