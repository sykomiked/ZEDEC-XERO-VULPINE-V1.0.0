// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Treaties (ABI 1.5): COBOL copybook treaties and Sutra text.
//!
//! A treaty is one batch of settlement work: who takes part, what each
//! party deposits and may claim back, how the batch's dividend is shared
//! (each party's weight num/den), and who owes whom. It has two exact
//! spellings that hold the same treaty:
//!
//! * RECORD: the fixed [`REC_LEN`]-byte COBOL copybook record
//!   (`ZXVTRTY.cpy`), all DISPLAY text, so plain ASCII on every machine;
//! * SUTRA: compact text (single spaces, `\n` line ends, no leading zeros,
//!   fractions in lowest terms, nothing after `END\n`).
//!
//! Each spelling is canonical: a parse followed by an emit gives the input
//! back byte for byte, and the record and Sutra forms of one treaty compare
//! equal. A refusal is a [`TreatyError`] with the [`TreatyFault`] and, for
//! a parse, the byte offset of the first fault.
//!
//! Everything here is pure: no context and no capability. [`Treaty::run`]
//! works in a private scratch engine that is freed before it returns; its
//! numbers are a deterministic report, not a payment.
//!
//! **Honest limits:** a treaty is a batch description, not a contract in
//! law; signatures, consent and authority live elsewhere. One asset per
//! treaty. The dividend shares are reported only; nothing is paid out.
//!
//! ```
//! use zxv::treaty::Treaty;
//! let text = "TREATY CORRIDOR-7 USD SETL\nPARTY ACME 5000 3000 1/2\n\
//!             PARTY BETA 3000 3000 1/2\nOWE ACME BETA 1200\nEND\n";
//! let t = Treaty::parse_sutra(text)?;
//! assert_eq!(t.sutra()?, text);
//! let same = Treaty::parse_record(&t.record()?)?;
//! assert!(t.equal(&same)?);
//! let r = t.run()?;
//! assert_eq!(r.legs()?.len(), 1);
//! # Ok::<(), zxv::Error>(())
//! ```

use std::fmt;
use std::ptr::NonNull;

use zxv_sys as sys;

use crate::error::{check, Error, Result};
use crate::ffi::{cstr, read_bytes, read_string};

/// Bytes in a `ZXVTRTY.cpy` treaty record.
pub const REC_LEN: usize = sys::ZXV_TREATY_REC_LEN as usize;
/// Bytes in a `ZXVRSLT.cpy` result record.
pub const RESULT_LEN: usize = sys::ZXV_TREATY_RESULT_LEN as usize;
/// Longest canonical Sutra text, in bytes.
pub const SUTRA_MAX: usize = sys::ZXV_TREATY_SUTRA_MAX as usize;
/// Parties per treaty.
pub const MAX_PARTIES: u32 = sys::ZXV_TREATY_MAX_PARTIES;
/// Obligations per treaty.
pub const MAX_OWES: u32 = sys::ZXV_TREATY_MAX_OWES;
/// Longest treaty name (A-Z 0-9 '-', starting with a letter).
pub const NAME_MAX: usize = sys::ZXV_TREATY_NAME_MAX as usize;
/// Longest party id (same alphabet).
pub const ID_MAX: usize = sys::ZXV_TREATY_ID_MAX as usize;
/// Largest weight numerator or denominator (PIC 9(9)).
pub const WEIGHT_MAX: u32 = sys::ZXV_TREATY_WEIGHT_MAX;
/// Largest |amount| in PIC S9(18).
pub const PIC18_MAX: i64 = sys::ZXV_TREATY_PIC18_MAX;

/// A treaty's operation.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum TreatyOp {
    /// An empty treaty.
    None,
    /// Net the obligations only (`ZXV_TREATY_OP_NET`).
    Net,
    /// Net, post the legs with the fee, pay claims (`ZXV_TREATY_OP_SETL`).
    Setl,
}

impl TreatyOp {
    fn from_raw(v: i32) -> TreatyOp {
        match v {
            sys::ZXV_TREATY_OP_NET => TreatyOp::Net,
            sys::ZXV_TREATY_OP_SETL => TreatyOp::Setl,
            _ => TreatyOp::None,
        }
    }
}

impl fmt::Display for TreatyOp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.pad(match self {
            TreatyOp::None => "NONE",
            TreatyOp::Net => "NET",
            TreatyOp::Setl => "SETL",
        })
    }
}

/// Why a treaty was refused (`ZXV_TREATY_FAULT_*`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[non_exhaustive]
pub enum TreatyFault {
    /// No fault.
    None,
    /// The record does not start with "ZXT1".
    Magic,
    /// Sutra text outside the grammar (also non-ASCII text).
    Syntax,
    /// Name not 1..30 of A-Z 0-9 '-'.
    Name,
    /// Asset not three capital letters.
    Asset,
    /// Operation not NET or SETL.
    Op,
    /// Party or obligation count out of range.
    Count,
    /// Malformed number or outside PIC S9(18).
    Number,
    /// Bad, duplicate or unknown party id.
    Party,
    /// R1: the weights.
    Weight,
    /// R2: a claim above its deposit.
    Claim,
    /// An obligation to self, or not above zero.
    Owe,
    /// An unused record slot or field not blank.
    Pad,
    /// A fault code this SDK does not know.
    Other(i32),
}

impl TreatyFault {
    /// The fault for a raw `ZXV_TREATY_FAULT_*` code.
    pub fn from_raw(v: i32) -> TreatyFault {
        match v {
            sys::ZXV_TREATY_FAULT_NONE => TreatyFault::None,
            sys::ZXV_TREATY_FAULT_MAGIC => TreatyFault::Magic,
            sys::ZXV_TREATY_FAULT_SYNTAX => TreatyFault::Syntax,
            sys::ZXV_TREATY_FAULT_NAME => TreatyFault::Name,
            sys::ZXV_TREATY_FAULT_ASSET => TreatyFault::Asset,
            sys::ZXV_TREATY_FAULT_OP => TreatyFault::Op,
            sys::ZXV_TREATY_FAULT_COUNT => TreatyFault::Count,
            sys::ZXV_TREATY_FAULT_NUMBER => TreatyFault::Number,
            sys::ZXV_TREATY_FAULT_PARTY => TreatyFault::Party,
            sys::ZXV_TREATY_FAULT_WEIGHT => TreatyFault::Weight,
            sys::ZXV_TREATY_FAULT_CLAIM => TreatyFault::Claim,
            sys::ZXV_TREATY_FAULT_OWE => TreatyFault::Owe,
            sys::ZXV_TREATY_FAULT_PAD => TreatyFault::Pad,
            n => TreatyFault::Other(n),
        }
    }

    /// The raw `ZXV_TREATY_FAULT_*` code.
    pub fn raw(self) -> i32 {
        match self {
            TreatyFault::None => sys::ZXV_TREATY_FAULT_NONE,
            TreatyFault::Magic => sys::ZXV_TREATY_FAULT_MAGIC,
            TreatyFault::Syntax => sys::ZXV_TREATY_FAULT_SYNTAX,
            TreatyFault::Name => sys::ZXV_TREATY_FAULT_NAME,
            TreatyFault::Asset => sys::ZXV_TREATY_FAULT_ASSET,
            TreatyFault::Op => sys::ZXV_TREATY_FAULT_OP,
            TreatyFault::Count => sys::ZXV_TREATY_FAULT_COUNT,
            TreatyFault::Number => sys::ZXV_TREATY_FAULT_NUMBER,
            TreatyFault::Party => sys::ZXV_TREATY_FAULT_PARTY,
            TreatyFault::Weight => sys::ZXV_TREATY_FAULT_WEIGHT,
            TreatyFault::Claim => sys::ZXV_TREATY_FAULT_CLAIM,
            TreatyFault::Owe => sys::ZXV_TREATY_FAULT_OWE,
            TreatyFault::Pad => sys::ZXV_TREATY_FAULT_PAD,
            TreatyFault::Other(n) => n,
        }
    }
}

/// The error of a treaty call: the [`Error`] and, for `ZXV_E_TREATY`, the
/// fault and its byte offset. It converts into [`Error`] with `?`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TreatyError {
    /// The first rule the treaty broke ([`TreatyFault::None`] when the error
    /// is not `ZXV_E_TREATY`).
    pub fault: TreatyFault,
    /// The byte offset of the first fault in the parsed text or record (0
    /// for validate, emits and run).
    pub offset: u32,
    /// The status-class error underneath.
    pub error: Error,
}

impl fmt::Display for TreatyError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.error.status() == sys::ZXV_E_TREATY {
            write!(
                f,
                "{} (fault {:?} at byte {})",
                self.error, self.fault, self.offset
            )
        } else {
            self.error.fmt(f)
        }
    }
}

impl std::error::Error for TreatyError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        Some(&self.error)
    }
}

impl From<TreatyError> for Error {
    fn from(e: TreatyError) -> Error {
        e.error
    }
}

impl From<Error> for TreatyError {
    fn from(error: Error) -> TreatyError {
        TreatyError {
            fault: TreatyFault::None,
            offset: 0,
            error,
        }
    }
}

/// One party of a treaty.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TreatyParty {
    /// 1..8 of A-Z 0-9 '-'.
    pub id: String,
    /// Minor units of the asset it puts in.
    pub deposit: i64,
    /// What it may claim back (at most `deposit`).
    pub claim: i64,
    /// Numerator of its dividend weight.
    pub weight_num: u32,
    /// Denominator of its dividend weight.
    pub weight_den: u32,
}

/// One obligation (or netted leg): party `from` pays party `to` `amount`
/// (0-based party indices, in treaty order).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TreatyOwe {
    /// Index of the party that owes (pays).
    pub from: u32,
    /// Index of the party owed (paid).
    pub to: u32,
    /// Minor units of the asset.
    pub amount: i64,
}

/// One party's line of a [`TreatyResult`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TreatyPartyResult {
    /// What the party is owed minus what it owes (gross).
    pub net: i64,
    /// Its deposit for NET; for SETL what it holds after the legs, the fee
    /// and its claim.
    pub balance: i64,
    /// Its reported dividend share (0 for NET).
    pub dividend: i64,
}

/// One treaty (`zxv_treaty`). It holds no context. Parsing and clearing
/// take `&mut self`; reads take `&self` and may run on many threads at once
/// (the C reads take a `const` treaty), so it is `Send + Sync`.
pub struct Treaty {
    raw: NonNull<sys::zxv_treaty>,
}

// SAFETY: the treaty is plain memory owned by this value; mutation needs
// &mut self, and C allows concurrent calls on a const treaty.
unsafe impl Send for Treaty {}
// SAFETY: as above.
unsafe impl Sync for Treaty {}

/// Maps a status with a fault and offset into a [`TreatyError`].
fn treaty_err(rc: i32, fault: i32, off: u32) -> std::result::Result<(), TreatyError> {
    check(rc).map_err(|error| TreatyError {
        fault: if rc == sys::ZXV_E_TREATY {
            TreatyFault::from_raw(fault)
        } else {
            TreatyFault::None
        },
        offset: off,
        error,
    })
}

impl Treaty {
    /// An empty treaty: it fails validation ([`TreatyFault::Name`]) until a
    /// parse fills it.
    pub fn new() -> Result<Treaty> {
        crate::ensure_compatible()?;
        let mut p = std::ptr::null_mut();
        // SAFETY: out pointer valid.
        check(unsafe { sys::zxv_treaty_create(&mut p) })?;
        let raw = NonNull::new(p)
            .ok_or_else(|| Error::with_detail(sys::ZXV_E_INTERNAL, "NULL treaty"))?;
        Ok(Treaty { raw })
    }

    /// A new treaty parsed from canonical Sutra text.
    pub fn parse_sutra(text: &str) -> std::result::Result<Treaty, TreatyError> {
        let mut t = Treaty::new()?;
        t.set_sutra(text)?;
        Ok(t)
    }

    /// A new treaty parsed from a [`REC_LEN`]-byte copybook record.
    pub fn parse_record(rec: &[u8]) -> std::result::Result<Treaty, TreatyError> {
        let mut t = Treaty::new()?;
        t.set_record(rec)?;
        Ok(t)
    }

    /// Empties the treaty again.
    pub fn clear(&mut self) -> Result<()> {
        // SAFETY: owned treaty, exclusive borrow.
        check(unsafe { sys::zxv_treaty_clear(self.raw.as_ptr()) })
    }

    /// Replaces the treaty with one parsed from Sutra text (at most
    /// [`SUTRA_MAX`] bytes, else `ZXV_E_TOO_LONG`). On any failure the
    /// treaty is unchanged. Non-ASCII text is a [`TreatyFault::Syntax`] at
    /// its offset.
    pub fn set_sutra(&mut self, text: &str) -> std::result::Result<(), TreatyError> {
        let cs = cstr(text)?;
        let (mut off, mut fault) = (0u32, 0i32);
        // SAFETY: owned treaty; cs is NUL-terminated; out pointers valid.
        let rc = unsafe {
            sys::zxv_treaty_parse_sutra(self.raw.as_ptr(), cs.as_ptr(), &mut off, &mut fault)
        };
        treaty_err(rc, fault, off)
    }

    /// Replaces the treaty with one parsed from a copybook record of exactly
    /// [`REC_LEN`] bytes (another length is `ZXV_E_ARG`). Unused slots must
    /// be exactly what COBOL's INITIALIZE leaves. On any failure the treaty
    /// is unchanged.
    pub fn set_record(&mut self, rec: &[u8]) -> std::result::Result<(), TreatyError> {
        let (mut off, mut fault) = (0u32, 0i32);
        // SAFETY: owned treaty; rec valid for rec.len() bytes.
        let rc = unsafe {
            sys::zxv_treaty_parse_record(
                self.raw.as_ptr(),
                rec.as_ptr(),
                rec.len(),
                &mut off,
                &mut fault,
            )
        };
        treaty_err(rc, fault, off)
    }

    /// Adds the validation fault to a `ZXV_E_TREATY` error of an emit/run.
    fn with_fault(&self, e: Error) -> TreatyError {
        let fault = if e.status() == sys::ZXV_E_TREATY {
            self.validate().err().map_or(TreatyFault::None, |v| v.fault)
        } else {
            TreatyFault::None
        };
        TreatyError {
            fault,
            offset: 0,
            error: e,
        }
    }

    /// The canonical Sutra text (a [`TreatyError`] if the treaty does not
    /// validate, an empty one say).
    pub fn sutra(&self) -> std::result::Result<String, TreatyError> {
        let p = self.raw.as_ptr();
        // SAFETY: owned treaty; R4 buffer from read_string.
        read_string(|b, c, l| unsafe { sys::zxv_treaty_emit_sutra(p, b, c, l) })
            .map_err(|e| self.with_fault(e))
    }

    /// The canonical [`REC_LEN`]-byte copybook record.
    pub fn record(&self) -> std::result::Result<Vec<u8>, TreatyError> {
        let p = self.raw.as_ptr();
        // SAFETY: owned treaty; R4 byte buffer from read_bytes.
        read_bytes(REC_LEN, |b, c, l| unsafe {
            sys::zxv_treaty_emit_record(p, b, c, l)
        })
        .map_err(|e| self.with_fault(e))
    }

    /// Checks R1-R4 and the spelling rules. A parsed treaty always
    /// validates; an empty one fails with [`TreatyFault::Name`].
    pub fn validate(&self) -> std::result::Result<(), TreatyError> {
        let mut fault = 0i32;
        // SAFETY: owned treaty; out pointer valid.
        let rc = unsafe { sys::zxv_treaty_validate(self.raw.as_ptr(), &mut fault) };
        treaty_err(rc, fault, 0)
    }

    /// True if `self` and `other` hold the same treaty (every field, every
    /// slot). Two empty treaties are equal.
    pub fn equal(&self, other: &Treaty) -> Result<bool> {
        let mut eq = 0i32;
        // SAFETY: two valid treaties, read only.
        check(unsafe { sys::zxv_treaty_equal(self.raw.as_ptr(), other.raw.as_ptr(), &mut eq) })?;
        Ok(eq != 0)
    }

    /// The treaty name (`""` when empty).
    pub fn name(&self) -> Result<String> {
        let p = self.raw.as_ptr();
        // SAFETY: owned treaty; R4 buffer.
        read_string(|b, c, l| unsafe { sys::zxv_treaty_name(p, b, c, l) })
    }

    /// The asset: three capital letters (`""` when empty).
    pub fn asset(&self) -> Result<String> {
        let p = self.raw.as_ptr();
        // SAFETY: owned treaty; R4 buffer.
        read_string(|b, c, l| unsafe { sys::zxv_treaty_asset(p, b, c, l) })
    }

    /// The operation and the party and obligation counts.
    pub fn header(&self) -> Result<(TreatyOp, u32, u32)> {
        let (mut op, mut np, mut no) = (0i32, 0u32, 0u32);
        // SAFETY: owned treaty; out pointers valid.
        check(unsafe { sys::zxv_treaty_header(self.raw.as_ptr(), &mut op, &mut np, &mut no) })?;
        Ok((TreatyOp::from_raw(op), np, no))
    }

    /// Party `index` (0-based, in treaty order); `ZXV_E_NOT_FOUND` past the
    /// last one.
    pub fn party(&self, index: u32) -> Result<TreatyParty> {
        let p = self.raw.as_ptr();
        // SAFETY: owned treaty; R4 buffer.
        let id = read_string(|b, c, l| unsafe { sys::zxv_treaty_party_id(p, index, b, c, l) })?;
        let (mut dep, mut clm, mut wn, mut wd) = (0i64, 0i64, 0u32, 0u32);
        // SAFETY: owned treaty; out pointers valid.
        check(unsafe { sys::zxv_treaty_party(p, index, &mut dep, &mut clm, &mut wn, &mut wd) })?;
        Ok(TreatyParty {
            id,
            deposit: dep,
            claim: clm,
            weight_num: wn,
            weight_den: wd,
        })
    }

    /// Every party in treaty order.
    pub fn parties(&self) -> Result<Vec<TreatyParty>> {
        (0..self.header()?.1).map(|i| self.party(i)).collect()
    }

    /// Obligation `index` (0-based); `ZXV_E_NOT_FOUND` past the last one.
    pub fn owe(&self, index: u32) -> Result<TreatyOwe> {
        let (mut from, mut to, mut amount) = (0u32, 0u32, 0i64);
        // SAFETY: owned treaty; out pointers valid.
        check(unsafe {
            sys::zxv_treaty_owe(self.raw.as_ptr(), index, &mut from, &mut to, &mut amount)
        })?;
        Ok(TreatyOwe { from, to, amount })
    }

    /// Every obligation in treaty order.
    pub fn owes(&self) -> Result<Vec<TreatyOwe>> {
        (0..self.header()?.2).map(|i| self.owe(i)).collect()
    }

    /// Runs the treaty in a fresh private engine for its asset: funds each
    /// deposit, nets the obligations multilaterally (at most parties - 1
    /// legs), and for SETL posts the legs with the 0.08889% assurance fee
    /// (all or nothing), pays each claim back out, and reports each party's
    /// weight share of the dividend bucket (reported, not paid). Nothing
    /// persists and no capability is needed; the same treaty always gives
    /// the same result. It takes about 500 KB of heap for the call.
    ///
    /// Errors: `ZXV_E_TREATY` (does not validate), `ZXV_E_FUNDS` (a payer
    /// cannot cover its legs plus the fee, or a claim exceeds what is
    /// left), `ZXV_E_RANGE` (a net position leaves PIC S9(18), or an amount
    /// above the engine's per-line limit of 2^59).
    pub fn run(&self) -> std::result::Result<TreatyResult, TreatyError> {
        let mut r = std::ptr::null_mut();
        // SAFETY: owned treaty; out pointer valid.
        check(unsafe { sys::zxv_treaty_run(self.raw.as_ptr(), &mut r) })
            .map_err(|e| self.with_fault(e))?;
        let raw = NonNull::new(r)
            .ok_or_else(|| Error::with_detail(sys::ZXV_E_INTERNAL, "NULL treaty result"))?;
        Ok(TreatyResult { raw })
    }
}

impl fmt::Debug for Treaty {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Treaty")
            .field("name", &self.name().ok())
            .field("header", &self.header().ok())
            .finish()
    }
}

impl Drop for Treaty {
    fn drop(&mut self) {
        // SAFETY: owned handle, destroyed once.
        unsafe { sys::zxv_treaty_destroy(self.raw.as_ptr()) }
    }
}

/// What running a treaty returned (`zxv_treaty_result`). Immutable,
/// `Send + Sync`, and independent of the treaty it came from.
pub struct TreatyResult {
    raw: NonNull<sys::zxv_treaty_result>,
}

// SAFETY: immutable once made; C allows any number of concurrent readers.
unsafe impl Send for TreatyResult {}
// SAFETY: as above.
unsafe impl Sync for TreatyResult {}

impl TreatyResult {
    /// The party and leg counts and the total fee charged (0 for NET).
    pub fn summary(&self) -> Result<(u32, u32, i64)> {
        let (mut np, mut nl, mut fees) = (0u32, 0u32, 0i64);
        // SAFETY: owned result; out pointers valid.
        check(unsafe {
            sys::zxv_treaty_result_summary(self.raw.as_ptr(), &mut np, &mut nl, &mut fees)
        })?;
        Ok((np, nl, fees))
    }

    /// Party `index`'s line (treaty order).
    pub fn party(&self, index: u32) -> Result<TreatyPartyResult> {
        let (mut net, mut balance, mut dividend) = (0i64, 0i64, 0i64);
        // SAFETY: owned result; out pointers valid.
        check(unsafe {
            sys::zxv_treaty_result_party(
                self.raw.as_ptr(),
                index,
                &mut net,
                &mut balance,
                &mut dividend,
            )
        })?;
        Ok(TreatyPartyResult {
            net,
            balance,
            dividend,
        })
    }

    /// Every party's line.
    pub fn parties(&self) -> Result<Vec<TreatyPartyResult>> {
        (0..self.summary()?.0).map(|i| self.party(i)).collect()
    }

    /// Netted leg `index`.
    pub fn leg(&self, index: u32) -> Result<TreatyOwe> {
        let (mut from, mut to, mut amount) = (0u32, 0u32, 0i64);
        // SAFETY: owned result; out pointers valid.
        check(unsafe {
            sys::zxv_treaty_result_leg(self.raw.as_ptr(), index, &mut from, &mut to, &mut amount)
        })?;
        Ok(TreatyOwe { from, to, amount })
    }

    /// Every netted leg.
    pub fn legs(&self) -> Result<Vec<TreatyOwe>> {
        (0..self.summary()?.1).map(|i| self.leg(i)).collect()
    }

    /// The [`RESULT_LEN`]-byte `ZXVRSLT.cpy` result record (party numbers in
    /// it are 1-based, as the copybook says).
    pub fn record(&self) -> Result<Vec<u8>> {
        let p = self.raw.as_ptr();
        // SAFETY: owned result; R4 byte buffer.
        read_bytes(RESULT_LEN, |b, c, l| unsafe {
            sys::zxv_treaty_result_record(p, b, c, l)
        })
    }
}

impl fmt::Debug for TreatyResult {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("TreatyResult")
            .field("summary", &self.summary().ok())
            .finish()
    }
}

impl Drop for TreatyResult {
    fn drop(&mut self) {
        // SAFETY: owned handle, destroyed once.
        unsafe { sys::zxv_treaty_result_destroy(self.raw.as_ptr()) }
    }
}

/// `a + b` in PIC S9(18), as COBOL does it with ON SIZE ERROR: an operand or
/// exact result outside ±[`PIC18_MAX`] is `ZXV_E_RANGE`.
pub fn pic_add(a: i64, b: i64) -> Result<i64> {
    let mut o = 0i64;
    // SAFETY: pure; out pointer valid.
    check(unsafe { sys::zxv_treaty_pic_add(a, b, &mut o) })?;
    Ok(o)
}

/// `a - b` in PIC S9(18) (`ZXV_E_RANGE` on a size error).
pub fn pic_sub(a: i64, b: i64) -> Result<i64> {
    let mut o = 0i64;
    // SAFETY: pure; out pointer valid.
    check(unsafe { sys::zxv_treaty_pic_sub(a, b, &mut o) })?;
    Ok(o)
}

/// `a * b` in PIC S9(18) (`ZXV_E_RANGE` on a size error).
pub fn pic_mul(a: i64, b: i64) -> Result<i64> {
    let mut o = 0i64;
    // SAFETY: pure; out pointer valid.
    check(unsafe { sys::zxv_treaty_pic_mul(a, b, &mut o) })?;
    Ok(o)
}

/// `a / b` truncated toward zero and the remainder with the dividend's sign
/// (COBOL DIVIDE ... REMAINDER): `-7 / 2 = -3 rem -1`. Division by zero is
/// `ZXV_E_RANGE`.
pub fn pic_div(a: i64, b: i64) -> Result<(i64, i64)> {
    let (mut q, mut r) = (0i64, 0i64);
    // SAFETY: pure; out pointers valid.
    check(unsafe { sys::zxv_treaty_pic_div(a, b, &mut q, &mut r) })?;
    Ok((q, r))
}
