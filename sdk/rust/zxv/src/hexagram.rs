// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! The 64-state hexagram tag and the quarantine log (ABI 1.3).

use std::ffi::CStr;
use std::fmt;
use std::ptr::NonNull;

use zxv_sys as sys;

use crate::capability::Capability;
use crate::error::{check, Error, Result};
use crate::truth::Truth;

macro_rules! hex_enum {
    ($(#[$m:meta])* $name:ident { $($(#[$vm:meta])* $v:ident = $n:literal),+ $(,)? }) => {
        $(#[$m])*
        #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
        #[repr(u8)]
        pub enum $name {
            $($(#[$vm])* $v = $n),+
        }

        impl $name {
            /// Every value, in numeric order.
            pub const ALL: &'static [$name] = &[$($name::$v),+];

            /// The ABI number.
            pub fn raw(self) -> u8 {
                self as u8
            }

            /// The value with ABI number `v` (`ZXV_E_ARG` otherwise).
            pub fn from_raw(v: u8) -> Result<$name> {
                match v {
                    $($n => Ok($name::$v),)+
                    _ => Err(Error::with_detail(
                        sys::ZXV_E_ARG,
                        format!("{v} is not a {}", stringify!($name)),
                    )),
                }
            }
        }
    };
}

hex_enum! {
    /// The four-valued base of a [`Hexagram`] (bits 5-4), numbered by line
    /// pattern. This is NOT [`Truth`]'s numbering (`True` is 1 there and 3
    /// here): convert only with [`Hexagram::to_truth`] and
    /// [`Hexagram::from_truth`].
    HexBase { /// `ZXV_HEX_BASE_FALSE`
        False = 0, /// `ZXV_HEX_BASE_NEITHER`
        Neither = 1, /// `ZXV_HEX_BASE_BOTH`
        Both = 2, /// `ZXV_HEX_BASE_TRUE`
        True = 3 }
}

hex_enum! {
    /// The polarity of a [`Hexagram`]'s evidence (bits 3-2). `Forbidden`
    /// is absorbing.
    HexPolarity { /// The evidence margin is shrinking, or the evidence is stale.
        Entropic = 0, /// The margin is unchanged.
        Neutral = 1, /// Forbidden work: rolled back, quarantined, never replayed.
        Forbidden = 2, /// The margin is growing.
        Negentropic = 3 }
}

hex_enum! {
    /// The lifecycle phase of a [`Hexagram`] (bits 1-0). The cycle is
    /// `Potential -> Emerging -> Manifest -> Dissolving -> Potential`.
    HexPhase { /// Not started.
        Potential = 0, /// Starting.
        Emerging = 1, /// Winding down.
        Dissolving = 2, /// Running.
        Manifest = 3 }
}

hex_enum! {
    /// What happens next to work with a given [`Hexagram`].
    HexAction { /// Go ahead.
        Proceed = 0, /// Hold until the evidence settles.
        Wait = 1, /// Decline.
        Refuse = 2, /// Hand to a person (a held contradiction).
        Escalate = 3, /// Undo it and quarantine it.
        Rollback = 4 }
}

impl HexPhase {
    /// The next phase on the cycle (`zxv_hex_phase_next`).
    pub fn next(self) -> HexPhase {
        HexPhase::from_raw(
            out8(|o| unsafe { sys::zxv_hex_phase_next(self.raw(), o) }).expect("valid phase"),
        )
        .expect("libzxv returned a phase")
    }
}

impl HexAction {
    /// The ABI name: `"proceed"`, `"wait"`, `"refuse"`, `"escalate"` or `"rollback"`.
    pub fn name(self) -> &'static str {
        // SAFETY: returns a static NUL-terminated string.
        unsafe { static_str(sys::zxv_hex_action_name(i32::from(self.raw()))) }
    }
}

/// Calls a pure `zxv_hex_*` function with a `uint8_t` out pointer.
fn out8(f: impl FnOnce(*mut u8) -> i32) -> Result<u8> {
    let mut o = 0u8;
    check(f(&mut o))?;
    Ok(o)
}

/// # Safety
/// `p` is a static, NUL-terminated ASCII string owned by libzxv.
unsafe fn static_str(p: *const std::os::raw::c_char) -> &'static str {
    // SAFETY: the caller's contract.
    unsafe { CStr::from_ptr(p) }.to_str().unwrap_or("INVALID")
}

/// The 64-state hexagram tag of ABI 1.3 (the kernel's `lpres_hex`): an
/// immutable value tagging one unit of work with a four-valued
/// [`HexBase`], the [`HexPolarity`] of its evidence and the [`HexPhase`] of
/// its lifecycle, packed as `tag = base << 4 | polarity << 2 | phase`.
///
/// ```no_run
/// use zxv::{HexAction, HexBase, HexPhase, HexPolarity, Hexagram};
/// let h = Hexagram::new(HexBase::True, HexPolarity::Negentropic, HexPhase::Potential);
/// assert_eq!(h.action(), HexAction::Proceed);
/// let h = h.advance();                              // Emerging
/// assert!(h.step(HexPhase::Dissolving).is_err());   // a jump: ZXV_E_STATE
/// assert_eq!(h.forbid().action(), HexAction::Rollback);
/// ```
///
/// Every rule is computed by libzxv. There is no conversion to `bool` and
/// none to [`Truth`] except [`to_truth`](Self::to_truth):
///
/// ```compile_fail
/// let t: zxv::Truth = zxv::Hexagram::from_tag(0).unwrap().into();
/// ```
#[derive(Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct Hexagram(u8);

impl Hexagram {
    /// The number of tags (`ZXV_HEX_STATES`).
    pub const STATES: u32 = sys::ZXV_HEX_STATES;

    /// The tag from its three fields.
    pub fn new(base: HexBase, polarity: HexPolarity, phase: HexPhase) -> Hexagram {
        Hexagram(
            out8(|o| unsafe { sys::zxv_hex_pack(base.raw(), polarity.raw(), phase.raw(), o) })
                .expect("libzxv refused valid fields"),
        )
    }

    /// The tag with this byte value (`ZXV_E_ARG` above 63).
    pub fn from_tag(tag: u8) -> Result<Hexagram> {
        // SAFETY: pure; NULL out pointers are allowed.
        check(unsafe {
            sys::zxv_hex_unpack(
                tag,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
            )
        })?;
        Ok(Hexagram(tag))
    }

    /// All 64 tags in tag order.
    pub fn all() -> impl Iterator<Item = Hexagram> {
        (0u8..64).map(Hexagram)
    }

    /// A tag whose base is `truth` (`zxv_hex_from_lp`).
    pub fn from_truth(truth: Truth, polarity: HexPolarity, phase: HexPhase) -> Hexagram {
        let b = out8(|o| unsafe { sys::zxv_hex_from_lp(truth.raw(), o) }).expect("valid truth");
        Hexagram::new(
            HexBase::from_raw(b).expect("libzxv returned a base"),
            polarity,
            phase,
        )
    }

    /// Polarity from evidence counts before (`pos0`, `neg0`) and after
    /// (`pos1`, `neg1`): `stale` gives `Entropic`; otherwise the margin
    /// |pos - neg| growing gives `Negentropic`, shrinking `Entropic`,
    /// unchanged `Neutral`. Never `Forbidden`.
    pub fn polarity_from_evidence(
        pos0: u32,
        neg0: u32,
        pos1: u32,
        neg1: u32,
        stale: bool,
    ) -> HexPolarity {
        let p =
            out8(|o| unsafe { sys::zxv_hex_polarity(pos0, neg0, pos1, neg1, i32::from(stale), o) })
                .expect("pure function");
        HexPolarity::from_raw(p).expect("libzxv returned a polarity")
    }

    /// The byte value, 0..=63.
    pub fn tag(self) -> u8 {
        self.0
    }

    /// Bits 5-4.
    pub fn base(self) -> HexBase {
        HexBase::from_raw(self.0 >> 4).expect("tag <= 63")
    }

    /// Bits 3-2.
    pub fn polarity(self) -> HexPolarity {
        HexPolarity::from_raw((self.0 >> 2) & 3).expect("2 bits")
    }

    /// Bits 1-0.
    pub fn phase(self) -> HexPhase {
        HexPhase::from_raw(self.0 & 3).expect("2 bits")
    }

    /// True if the polarity is `Forbidden` (absorbing).
    pub fn is_forbidden(self) -> bool {
        self.polarity() == HexPolarity::Forbidden
    }

    /// What happens next: one lookup in the kernel's 64-entry table.
    pub fn action(self) -> HexAction {
        let mut a = 0i32;
        // SAFETY: pure; out pointer valid.
        check(unsafe { sys::zxv_hex_action(self.0, &mut a) }).expect("valid tag");
        HexAction::from_raw(u8::try_from(a).expect("small")).expect("libzxv returned an action")
    }

    /// `"base/polarity/phase"`, e.g. `"true/negentropic/manifest"`.
    pub fn name(self) -> &'static str {
        // SAFETY: returns a static NUL-terminated string.
        unsafe { static_str(sys::zxv_hex_name(self.0)) }
    }

    /// The base as a [`Truth`]: the explicit conversion (`zxv_hex_to_lp`).
    pub fn to_truth(self) -> Truth {
        let mut t = 0i32;
        // SAFETY: pure; out pointer valid.
        check(unsafe { sys::zxv_hex_to_lp(self.0 >> 4, &mut t) }).expect("valid base");
        Truth::from_raw(t).expect("libzxv returned a truth value")
    }

    /// The same work in `phase`: the same phase or the next one only
    /// (`ZXV_E_STATE`, a refusal, for a jump).
    pub fn step(self, phase: HexPhase) -> Result<Hexagram> {
        out8(|o| unsafe { sys::zxv_hex_step(self.0, phase.raw(), o) }).map(Hexagram)
    }

    /// One step along the cycle.
    #[must_use]
    pub fn advance(self) -> Hexagram {
        self.step(self.phase().next())
            .expect("the next phase is always allowed")
    }

    /// New base and polarity, same phase. `ZXV_E_STATE` if this tag is
    /// forbidden and `polarity` is not `Forbidden` (forbidden is absorbing).
    pub fn with_state(self, base: HexBase, polarity: HexPolarity) -> Result<Hexagram> {
        out8(|o| unsafe { sys::zxv_hex_set(self.0, base.raw(), polarity.raw(), o) }).map(Hexagram)
    }

    /// The same tag with polarity `Forbidden`.
    #[must_use]
    pub fn forbid(self) -> Hexagram {
        Hexagram(out8(|o| unsafe { sys::zxv_hex_forbid(self.0, o) }).expect("valid tag"))
    }

    /// Conjunction: four-valued AND on the base, worst polarity, least
    /// advanced phase.
    #[must_use]
    pub fn and(self, other: Hexagram) -> Hexagram {
        Hexagram(out8(|o| unsafe { sys::zxv_hex_and(self.0, other.0, o) }).expect("valid tags"))
    }

    /// Disjunction: four-valued OR on the base, worst polarity, least
    /// advanced phase.
    #[must_use]
    pub fn or(self, other: Hexagram) -> Hexagram {
        Hexagram(out8(|o| unsafe { sys::zxv_hex_or(self.0, other.0, o) }).expect("valid tags"))
    }

    /// Negation of the base; polarity and phase are kept.
    #[allow(clippy::should_implement_trait)]
    #[must_use]
    pub fn not(self) -> Hexagram {
        Hexagram(out8(|o| unsafe { sys::zxv_hex_not(self.0, o) }).expect("valid tag"))
    }
}

impl TryFrom<u8> for Hexagram {
    type Error = Error;

    fn try_from(tag: u8) -> Result<Hexagram> {
        Hexagram::from_tag(tag)
    }
}

impl std::ops::BitAnd for Hexagram {
    type Output = Hexagram;

    fn bitand(self, rhs: Hexagram) -> Hexagram {
        self.and(rhs)
    }
}

impl std::ops::BitOr for Hexagram {
    type Output = Hexagram;

    fn bitor(self, rhs: Hexagram) -> Hexagram {
        self.or(rhs)
    }
}

impl std::ops::Not for Hexagram {
    type Output = Hexagram;

    fn not(self) -> Hexagram {
        Hexagram::not(self)
    }
}

impl fmt::Debug for Hexagram {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Hexagram({}, tag={})", self.name(), self.0)
    }
}

impl fmt::Display for Hexagram {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.pad(self.name())
    }
}

/// One record of a [`QuarantineLog`].
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct QuarantineRecord {
    /// Its index.
    pub seq: u64,
    /// The forbidden tag.
    pub tag: Hexagram,
    /// The caller's reason code.
    pub reason: u8,
    /// Who submitted the work.
    pub submitter: u32,
    /// A digest of the refused work.
    pub subject: [u8; 32],
    /// The previous record's hash (zeros for the first).
    pub prev: [u8; 32],
    /// This record's hash.
    pub hash: [u8; 32],
}

/// The quarantine log of ABI 1.3 (`zxv_hexlog`): an append-only, SHA-256
/// hash-chained record of forbidden work, read for audit and never
/// replayed. Creating and appending need [`Caps::ATTEST`](crate::Caps::ATTEST).
///
/// Record k's hash is SHA-256(`"zxv/lpx-qlog/v1"` || seq (u64 LE) || tag ||
/// reason || submitter (u32 LE) || subject || prev). A full log refuses
/// the append with `ZXV_E_CAPACITY`; the caller must then halt the context
/// ([`Context::halt`](crate::Context::halt)), since forbidden work it
/// cannot record must not continue. Signing [`head`](Self::head) is the
/// caller's job.
///
/// `Send + Sync` (the log is mutex-protected in C).
pub struct QuarantineLog<'a> {
    raw: NonNull<sys::zxv_hexlog>,
    cap: &'a Capability<'a>,
}

// SAFETY: the C log is protected by its own mutex.
unsafe impl Send for QuarantineLog<'_> {}
// SAFETY: as above.
unsafe impl Sync for QuarantineLog<'_> {}

impl<'a> QuarantineLog<'a> {
    /// Largest capacity (`ZXV_HEXLOG_MAX_RECORDS`).
    pub const MAX_RECORDS: u32 = sys::ZXV_HEXLOG_MAX_RECORDS;

    /// A log of at most `capacity` (1..=[`MAX_RECORDS`](Self::MAX_RECORDS))
    /// records in the capability's context (needs `ATTEST`).
    pub fn new(cap: &'a Capability<'a>, capacity: u32) -> Result<QuarantineLog<'a>> {
        let mut raw = std::ptr::null_mut();
        cap.call(|x| unsafe { sys::zxv_hexlog_create(x, capacity, &mut raw) })?;
        let raw =
            NonNull::new(raw).ok_or_else(|| Error::with_detail(sys::ZXV_E_INTERNAL, "NULL log"))?;
        Ok(QuarantineLog { raw, cap })
    }

    /// Quarantines forbidden work (`tag` must be forbidden, else
    /// `ZXV_E_ARG`); returns the record's sequence number.
    pub fn append(
        &self,
        tag: Hexagram,
        reason: u8,
        submitter: u32,
        subject: &[u8; 32],
    ) -> Result<u64> {
        let mut seq = 0u64;
        let log = self.raw.as_ptr();
        self.cap.call(|cap| unsafe {
            sys::zxv_hexlog_append(
                cap,
                log,
                tag.tag(),
                reason,
                submitter,
                subject.as_ptr(),
                &mut seq,
            )
        })?;
        Ok(seq)
    }

    /// Number of records.
    pub fn count(&self) -> Result<u32> {
        let mut n = 0u32;
        self.local(|log| unsafe { sys::zxv_hexlog_count(log, &mut n) })?;
        Ok(n)
    }

    /// The chain head: the last record's hash (zeros when empty).
    pub fn head(&self) -> Result<[u8; 32]> {
        let mut h = [0u8; 32];
        self.local(|log| unsafe { sys::zxv_hexlog_head(log, h.as_mut_ptr()) })?;
        Ok(h)
    }

    /// Record `index`, oldest first.
    pub fn record(&self, index: u32) -> Result<QuarantineRecord> {
        let (mut seq, mut tag, mut reason, mut who) = (0u64, 0u8, 0u8, 0u32);
        let (mut subject, mut prev, mut hash) = ([0u8; 32], [0u8; 32], [0u8; 32]);
        self.local(|log| unsafe {
            sys::zxv_hexlog_at(
                log,
                index,
                &mut seq,
                &mut tag,
                &mut reason,
                &mut who,
                subject.as_mut_ptr(),
                prev.as_mut_ptr(),
                hash.as_mut_ptr(),
            )
        })?;
        Ok(QuarantineRecord {
            seq,
            tag: Hexagram::from_tag(tag)?,
            reason,
            submitter: who,
            subject,
            prev,
            hash,
        })
    }

    /// Every record, oldest first.
    pub fn records(&self) -> Result<Vec<QuarantineRecord>> {
        (0..self.count()?).map(|i| self.record(i)).collect()
    }

    /// Recomputes every link; a broken chain is `ZXV_E_INVARIANT` (FATAL:
    /// the context halts).
    pub fn verify(&self) -> Result<()> {
        self.local(|log| unsafe { sys::zxv_hexlog_verify(log) })
    }

    fn local(&self, f: impl FnOnce(*mut sys::zxv_hexlog) -> i32) -> Result<()> {
        let ctx = self.cap.context();
        ctx.guard()?;
        ctx.check(f(self.raw.as_ptr()))
    }
}

impl fmt::Debug for QuarantineLog<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("QuarantineLog")
            .field("count", &self.count().ok())
            .finish()
    }
}

impl Drop for QuarantineLog<'_> {
    fn drop(&mut self) {
        // SAFETY: owned handle destroyed once, before its context.
        unsafe { sys::zxv_hexlog_destroy(self.raw.as_ptr()) }
    }
}
