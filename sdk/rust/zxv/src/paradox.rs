// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! The paradox engine (ABI 1.5).
//!
//! A PARADOX here is work whose four-valued state is Both (the evidence
//! points both ways) or Neither (no evidence). This group does not decide
//! which side is "really" true. It reads the work's [`Hexagram`], gives the
//! paradox one of four kinds, and runs a fixed, logged operator for it:
//!
//! | kind | when | operator |
//! |---|---|---|
//! | constructive | Both, margin growing | a bounded policy update ([`ParadoxBook::constructive`]) |
//! | destructive | Both, margin shrinking | contain, roll back, prove ([`ParadoxBook::destructive`]) |
//! | neutral | steady Both, or Neither | hold every branch ([`ParadoxBook::neutral`]) |
//! | special | new, unregistered | a permanent frontier record ([`ParadoxBook::special`]) |
//!
//! A registered signature overrides the trend. True and False are not
//! paradoxes ([`ParadoxKind::None`]). Every operator takes the
//! [`ParadoxClass`] that classify returned. A forbidden tag means a real
//! check found a broken invariant: every operator refuses it and rolls back.
//!
//! **Outcomes.** An operator that ran returns `Ok(outcome)`: `Refused` and
//! `ProposalForOwner` are decisions, not errors. An error means it did not
//! run and nothing changed (except: destructive always restores the state,
//! and a graph pass that reaches the step limit is logged and then returns
//! `ZXV_E_PARADOX_STEP`, meaning [`ParadoxOutcome::EscalateToOwner`]). A
//! full log is `ZXV_E_CAPACITY`: halt the context; work that cannot be
//! recorded must not continue.
//!
//! **Capabilities.** Creating a book, policy, hold or graph, and every
//! operator and registry add, needs [`Caps::PARADOX`](crate::Caps::PARADOX).
//! The book's operators use the capability the book was created with; a
//! policy, hold or graph used with a book must belong to the same context
//! (`ZXV_E_ARG` otherwise). Reading, classifying, adding branches, nodes and
//! updates need none. Nothing here signs: sign the bytes yourself.
//!
//! **Threading.** A [`ParadoxBook`] is `Send + Sync`; a policy, hold or graph
//! is `Send` only, and the calls that change it take `&mut`.
//!
//! **Honest limits:** automatic changes happen only inside published
//! bounds; an update outside them changes nothing and becomes a proposal
//! for the owner. Policies that touch money or devices are never changed
//! automatically: they need the owner, and `owner_ok` is the caller's
//! assertion (this library cannot check who the owner is). Detection is
//! only by the checks listed and only when you call. Destructive work is
//! contained, never answered: nothing replies to or acts against a sender.
//! This is contradiction-tolerant bookkeeping; it never makes both sides
//! true. Integers only.

use std::ffi::{c_char, CStr};
use std::fmt;
use std::ptr::NonNull;

use zxv_sys as sys;

use crate::capability::Capability;
use crate::error::{check, Error, Result};
use crate::ffi::{local, local_with, non_null, opt_ptr, read_bytes, same_context};
use crate::hexagram::{HexAction, Hexagram};

macro_rules! i32_enum {
    ($(#[$m:meta])* $name:ident { $($(#[$vm:meta])* $v:ident = $c:path),+ $(,)? }) => {
        $(#[$m])*
        #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
        pub enum $name {
            $($(#[$vm])* $v,)+
            /// A value this SDK does not know (passed through unchanged).
            Other(i32),
        }

        impl $name {
            /// The ABI number.
            pub fn raw(self) -> i32 {
                match self { $($name::$v => $c,)+ $name::Other(n) => n }
            }

            /// The value for ABI number `v`.
            pub fn from_raw(v: i32) -> $name {
                $(if v == $c { return $name::$v; })+
                $name::Other(v)
            }
        }
    };
}

i32_enum! {
    /// One of the four kinds, or `None`.
    ParadoxKind {
        /// Catalyst.
        Constructive = sys::ZXV_PDX_KIND_CONSTRUCTIVE,
        /// Immune response.
        Destructive = sys::ZXV_PDX_KIND_DESTRUCTIVE,
        /// Hold.
        Neutral = sys::ZXV_PDX_KIND_NEUTRAL,
        /// The frontier.
        Special = sys::ZXV_PDX_KIND_SPECIAL,
        /// True or False: not a paradox.
        None = sys::ZXV_PDX_KIND_NONE,
    }
}

i32_enum! {
    /// What to do with a classified paradox ([`ParadoxClass::action`]).
    ParadoxDo {
        /// Not a paradox: use [`Hexagram::action`].
        None = sys::ZXV_PDX_DO_NONE,
        /// Constructive.
        Synthesize = sys::ZXV_PDX_DO_SYNTHESIZE,
        /// Destructive.
        Contain = sys::ZXV_PDX_DO_CONTAIN,
        /// Neutral.
        Hold = sys::ZXV_PDX_DO_HOLD,
        /// Special (sandbox).
        Record = sys::ZXV_PDX_DO_RECORD,
        /// Forbidden: refuse, roll back, quarantine.
        Rollback = sys::ZXV_PDX_DO_ROLLBACK,
    }
}

i32_enum! {
    /// An operator's decision.
    ParadoxOutcome {
        /// Constructive update applied.
        Applied = sys::ZXV_PDX_OUT_APPLIED,
        /// An applied update survived evaluation.
        Kept = sys::ZXV_PDX_OUT_KEPT,
        /// Out of bounds or unapproved: no change.
        ProposalForOwner = sys::ZXV_PDX_OUT_PROPOSAL_FOR_OWNER,
        /// Tension grew, or forbidden: restored.
        RolledBack = sys::ZXV_PDX_OUT_ROLLED_BACK,
        /// Destructive: quarantined, rolled back.
        Contained = sys::ZXV_PDX_OUT_CONTAINED,
        /// Neutral: still holding every branch.
        Deferred = sys::ZXV_PDX_OUT_DEFERRED,
        /// Neutral: one branch chosen.
        Settled = sys::ZXV_PDX_OUT_SETTLED,
        /// Special: frontier record written.
        Recorded = sys::ZXV_PDX_OUT_RECORDED,
        /// A reviewed draft joined the registry.
        Accepted = sys::ZXV_PDX_OUT_ACCEPTED,
        /// Graph: more passes needed.
        Pending = sys::ZXV_PDX_OUT_PENDING,
        /// Graph: every node is final.
        Resolved = sys::ZXV_PDX_OUT_RESOLVED,
        /// Graph: step limit (reported as `ZXV_E_PARADOX_STEP`).
        EscalateToOwner = sys::ZXV_PDX_OUT_ESCALATE_TO_OWNER,
        /// Wrong kind: nothing changed.
        Refused = sys::ZXV_PDX_OUT_REFUSED,
        /// Never reported: `ZXV_E_CAPACITY` instead.
        LogFull = sys::ZXV_PDX_OUT_LOG_FULL,
    }
}

i32_enum! {
    /// A graph node's state. Final: `Clear`, `Contained`, `RolledBack`,
    /// `Tainted`, `Applied`.
    ParadoxNodeState {
        /// Not yet decided.
        Open = sys::ZXV_PDX_NS_OPEN,
        /// Not a paradox.
        Clear = sys::ZXV_PDX_NS_CLEAR,
        /// Contained.
        Contained = sys::ZXV_PDX_NS_CONTAINED,
        /// Rolled back.
        RolledBack = sys::ZXV_PDX_NS_ROLLED_BACK,
        /// Depends on contained work.
        Tainted = sys::ZXV_PDX_NS_TAINTED,
        /// Special: not run.
        Sandboxed = sys::ZXV_PDX_NS_SANDBOXED,
        /// Waiting on a dependency.
        Waiting = sys::ZXV_PDX_NS_WAITING,
        /// Neutral: held.
        Held = sys::ZXV_PDX_NS_HELD,
        /// Blocked by a dependency.
        Blocked = sys::ZXV_PDX_NS_BLOCKED,
        /// Constructive: applied.
        Applied = sys::ZXV_PDX_NS_APPLIED,
    }
}

i32_enum! {
    /// One of a book's three logs.
    ParadoxLog {
        /// The operator log.
        Ops = sys::ZXV_PDX_LOG_OPS,
        /// Special records, never edited.
        Frontier = sys::ZXV_PDX_LOG_FRONTIER,
        /// Contained work.
        Quarantine = sys::ZXV_PDX_LOG_QUARANTINE,
    }
}

/// # Safety
/// `p` is NULL or a static NUL-terminated string owned by libzxv.
unsafe fn static_name(p: *const c_char) -> &'static str {
    if p.is_null() {
        return "INVALID";
    }
    // SAFETY: the caller's contract.
    unsafe { CStr::from_ptr(p) }.to_str().unwrap_or("INVALID")
}

impl ParadoxKind {
    /// The library's name, e.g. `"constructive"` (`"INVALID"` if unknown).
    pub fn name(self) -> &'static str {
        // SAFETY: returns a static string.
        unsafe { static_name(sys::zxv_paradox_kind_name(self.raw())) }
    }

    /// The kind a tag reads as on its own (a forbidden tag reads as
    /// `None`: forbidden is not a kind).
    pub fn of_tag(tag: Hexagram) -> Result<ParadoxKind> {
        let mut k = 0i32;
        // SAFETY: pure; out pointer valid.
        check(unsafe { sys::zxv_paradox_kind_of_tag(tag.tag(), &mut k) })?;
        Ok(ParadoxKind::from_raw(k))
    }
}

impl ParadoxOutcome {
    /// The library's name, e.g. `"proposal-for-owner"`.
    pub fn name(self) -> &'static str {
        // SAFETY: returns a static string.
        unsafe { static_name(sys::zxv_paradox_outcome_name(self.raw())) }
    }

    /// The [`HexAction`] the outcome amounts to (`Applied` -> proceed,
    /// `Deferred` -> wait, `ProposalForOwner` -> escalate, ...).
    pub fn action(self) -> Result<HexAction> {
        let mut a = 0i32;
        // SAFETY: pure; out pointer valid.
        check(unsafe { sys::zxv_paradox_outcome_action(self.raw(), &mut a) })?;
        HexAction::from_raw(a as u8)
    }
}

impl ParadoxNodeState {
    /// The library's name, e.g. `"sandboxed"`.
    pub fn name(self) -> &'static str {
        // SAFETY: returns a static string.
        unsafe { static_name(sys::zxv_paradox_node_state_name(self.raw())) }
    }
}

/// Flag: touches money (the owner decides).
pub const FLAG_MONEY: u8 = sys::ZXV_PDX_FLAG_MONEY as u8;
/// Flag: touches devices (the owner decides).
pub const FLAG_DEVICE: u8 = sys::ZXV_PDX_FLAG_DEVICE as u8;
/// Trip: the step limit.
pub const TRIP_STEPS: u32 = sys::ZXV_PDX_TRIP_STEPS;
/// Trip: the budget.
pub const TRIP_BUDGET: u32 = sys::ZXV_PDX_TRIP_BUDGET;
/// Trip: a repeated state fingerprint.
pub const TRIP_CYCLE: u32 = sys::ZXV_PDX_TRIP_CYCLE;
/// Trip: the caller's invariant check failed.
pub const TRIP_INVARIANT: u32 = sys::ZXV_PDX_TRIP_INVARIANT;
/// Trip: a forbidden tag.
pub const TRIP_FORBIDDEN: u32 = sys::ZXV_PDX_TRIP_FORBIDDEN;
/// An operator or frontier record (144 bytes).
pub const REC_BYTES: usize = sys::ZXV_PDX_REC_BYTES as usize;
/// A quarantine record (110 bytes).
pub const QREC_BYTES: usize = sys::ZXV_PDX_QREC_BYTES as usize;
/// A [`RepairProof`] (160 bytes).
pub const REPAIR_BYTES: usize = sys::ZXV_PDX_REPAIR_BYTES as usize;
/// A [`ParadoxDraft`] (71 bytes).
pub const DRAFT_BYTES: usize = sys::ZXV_PDX_DRAFT_BYTES as usize;
/// Records per log.
pub const LOG_MAX: u32 = sys::ZXV_PDX_LOG_MAX;
/// Registry entries.
pub const REGISTRY_MAX: u32 = sys::ZXV_PDX_REGISTRY_MAX;
/// Parameters per policy.
pub const POLICY_MAX: usize = sys::ZXV_PDX_POLICY_MAX as usize;
/// Branches per hold.
pub const HOLD_MAX: u32 = sys::ZXV_PDX_HOLD_MAX;
/// Nodes per graph.
pub const GRAPH_MAX: u32 = sys::ZXV_PDX_GRAPH_MAX;
/// Dependencies per node.
pub const MAX_DEPS: u32 = sys::ZXV_PDX_MAX_DEPS;
/// Queued node updates per pass.
pub const UPDATES_MAX: u32 = sys::ZXV_PDX_UPDATES_MAX;
/// Fingerprints per detection.
pub const TRACE_MAX: usize = sys::ZXV_PDX_TRACE_MAX as usize;
/// A registry entry not learned from the frontier.
pub const SRC_SEED: u64 = sys::ZXV_PDX_SRC_SEED;

/// The size of a policy proposal of `n` values.
pub const fn proposal_bytes(n: u32) -> usize {
    sys::ZXV_PDX_PROPOSAL_BYTES(n) as usize
}

/// A classified pair: the kind and the tag carrying the kind's polarity.
/// Every operator takes the pair classify returned; a pair that breaks the
/// class rules is `ZXV_E_ARG`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct ParadoxClass {
    /// The kind.
    pub kind: ParadoxKind,
    /// The tag, carrying the kind's polarity.
    pub tag: Hexagram,
}

impl ParadoxClass {
    /// What to do with the class. A forbidden tag gives
    /// [`ParadoxDo::Rollback`] whatever the kind.
    pub fn action(&self) -> Result<ParadoxDo> {
        let mut d = 0i32;
        // SAFETY: pure; out pointer valid.
        check(unsafe { sys::zxv_paradox_action(self.kind.raw(), self.tag.tag(), &mut d) })?;
        Ok(ParadoxDo::from_raw(d))
    }
}

/// The signature of an anomaly: SHA-256(`"zxv/pdx-sig/v1"` || data), from a
/// canonical description you build.
pub fn signature(data: &[u8]) -> Result<[u8; 32]> {
    let mut s = [0u8; 32];
    // SAFETY: data valid; out 32 bytes.
    check(unsafe { sys::zxv_paradox_signature(opt_ptr(data), data.len(), s.as_mut_ptr()) })?;
    Ok(s)
}

/// A trend: the evidence counts before and after, and whether the newest
/// evidence is past your freshness limit.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct ParadoxEvidence {
    /// Evidence for, before.
    pub pos0: u32,
    /// Evidence against, before.
    pub neg0: u32,
    /// Evidence for, after.
    pub pos1: u32,
    /// Evidence against, after.
    pub neg1: u32,
    /// The newest evidence is past your freshness limit.
    pub stale: bool,
}

/// What classify reports.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ParadoxClassification {
    /// The classified pair.
    pub class: ParadoxClass,
    /// The registry entry that decided it, or -1.
    pub reg_index: i32,
}

fn classify_raw(
    book: *const sys::zxv_paradox,
    tag: Hexagram,
    ev: Option<&ParadoxEvidence>,
    sig: Option<&[u8; 32]>,
) -> Result<ParadoxClassification> {
    let e = ev.map(|e| [e.pos0, e.neg0, e.pos1, e.neg1]);
    let evp = e.as_ref().map_or(std::ptr::null(), |a| a.as_ptr());
    let stale = i32::from(ev.is_some_and(|e| e.stale));
    let sp = sig.map_or(std::ptr::null(), |s| s.as_ptr());
    let (mut k, mut t, mut ri) = (0i32, 0u8, 0i32);
    // SAFETY: book NULL or live; evidence NULL or 4 u32; sig NULL or 32.
    check(unsafe {
        sys::zxv_paradox_classify(book, tag.tag(), evp, stale, sp, &mut k, &mut t, &mut ri)
    })?;
    Ok(ParadoxClassification {
        class: ParadoxClass {
            kind: ParadoxKind::from_raw(k),
            tag: Hexagram::from_tag(t)?,
        },
        reg_index: ri,
    })
}

/// Classifies a tag without a registry. `ev` may be `None` (no trend:
/// polarity neutral).
pub fn classify(tag: Hexagram, ev: Option<&ParadoxEvidence>) -> Result<ParadoxClassification> {
    classify_raw(std::ptr::null(), tag, ev, None)
}

fn le32(b: &[u8], at: usize) -> u32 {
    u32::from_le_bytes([b[at], b[at + 1], b[at + 2], b[at + 3]])
}

fn h32(b: &[u8], at: usize) -> [u8; 32] {
    let mut h = [0u8; 32];
    h.copy_from_slice(&b[at..at + 32]);
    h
}

/// The 160-byte proof destructive writes: which checks fired, the state
/// hashes before and after rollback, the signature to share with peers and
/// a binding hash. A value type. Sign it yourself.
#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct RepairProof(pub [u8; REPAIR_BYTES]);

impl RepairProof {
    /// Recomputes the binding hash (integrity only, not a signature).
    /// `ZXV_E_VERIFY` when it does not match.
    pub fn verify(&self) -> Result<()> {
        // SAFETY: 160 bytes.
        check(unsafe { sys::zxv_paradox_repair_verify(self.0.as_ptr()) })
    }
    /// Which checks fired (`TRIP_*` bits).
    pub fn trips(&self) -> u32 {
        le32(&self.0, 0)
    }
    /// The step count reported to destructive.
    pub fn steps(&self) -> u32 {
        le32(&self.0, 4)
    }
    /// The budget spent reported to destructive.
    pub fn spent(&self) -> u32 {
        le32(&self.0, 8)
    }
    /// How many state fingerprints were checked.
    pub fn trace_len(&self) -> u32 {
        le32(&self.0, 12)
    }
    /// The submitter id.
    pub fn submitter(&self) -> u32 {
        le32(&self.0, 16)
    }
    /// The length of the restored state.
    pub fn state_len(&self) -> u32 {
        le32(&self.0, 20)
    }
    /// The hash of the state as found.
    pub fn state_found(&self) -> [u8; 32] {
        h32(&self.0, 24)
    }
    /// The hash of the state after rollback.
    pub fn state_restored(&self) -> [u8; 32] {
        h32(&self.0, 56)
    }
    /// The trap's signature to share with peers.
    pub fn signature(&self) -> [u8; 32] {
        h32(&self.0, 88)
    }
    /// The proof's binding hash.
    pub fn hash(&self) -> [u8; 32] {
        h32(&self.0, 120)
    }
    /// The index of the quarantine record.
    pub fn quarantine_index(&self) -> u64 {
        let mut b = [0u8; 8];
        b.copy_from_slice(&self.0[152..160]);
        u64::from_le_bytes(b)
    }
}

impl Default for RepairProof {
    fn default() -> RepairProof {
        RepairProof([0; REPAIR_BYTES])
    }
}

impl fmt::Debug for RepairProof {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("RepairProof")
            .field("trips", &self.trips())
            .field("quarantine_index", &self.quarantine_index())
            .finish_non_exhaustive()
    }
}

/// A 71-byte draft classification of a frontier record, for review. A
/// value type.
#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct ParadoxDraft(pub [u8; DRAFT_BYTES]);

impl ParadoxDraft {
    /// The frontier record index.
    pub fn rec(&self) -> u32 {
        le32(&self.0, 0)
    }
    /// The frontier record's hash.
    pub fn record_hash(&self) -> [u8; 32] {
        h32(&self.0, 4)
    }
    /// The frontier record's signature.
    pub fn signature(&self) -> [u8; 32] {
        h32(&self.0, 36)
    }
    /// The proposed kind.
    pub fn kind(&self) -> ParadoxKind {
        ParadoxKind::from_raw(i32::from(self.0[68]))
    }
    /// The caller's reason code.
    pub fn reason(&self) -> u8 {
        self.0[69]
    }
    /// The `FLAG_*` bits.
    pub fn flags(&self) -> u8 {
        self.0[70]
    }
}

impl fmt::Debug for ParadoxDraft {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("ParadoxDraft")
            .field("rec", &self.rec())
            .field("kind", &self.kind())
            .field("reason", &self.reason())
            .field("flags", &self.flags())
            .finish()
    }
}

/// One registry entry ([`ParadoxBook::registry_at`]).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ParadoxRegistryEntry {
    /// The signature.
    pub sig: [u8; 32],
    /// Its kind.
    pub kind: ParadoxKind,
    /// The caller's reason code.
    pub reason: u8,
    /// `FLAG_*` bits.
    pub flags: u8,
    /// The frontier record it was learned from, or [`SRC_SEED`].
    pub src: u64,
}

/// The input to [`ParadoxBook::destructive`].
#[derive(Debug)]
pub struct ParadoxContainment<'s> {
    /// Steps taken; trips when `steps >= step_limit`.
    pub steps: u32,
    /// The step limit (0: no limit).
    pub step_limit: u32,
    /// Budget spent; trips when `spent >= budget`.
    pub spent: u32,
    /// The budget (0: no budget).
    pub budget: u32,
    /// State fingerprints in visit order; a repeat trips.
    pub trace: &'s [u64],
    /// False trips: your invariant check failed.
    pub invariant_ok: bool,
    /// Restored IN PLACE from `checkpoint` (same length) when contained.
    pub state: &'s mut [u8],
    /// The known-good state.
    pub checkpoint: &'s [u8],
    /// The canonical description of the trap (for its signature).
    pub pattern: &'s [u8],
    /// Who submitted the work (recorded, never contacted).
    pub submitter: u32,
}

fn len32(n: usize, what: &str) -> Result<u32> {
    u32::try_from(n).map_err(|_| Error::with_detail(sys::ZXV_E_ARG, format!("{what} too long")))
}

/// The three logs and the signature registry. It belongs to the context of
/// the capability that created it, uses that capability for every
/// operator, and fails closed with the context. `Send + Sync`.
///
/// ```
/// use zxv::{Caps, Context, Hexagram, HexBase, HexPhase, HexPolarity, ParadoxBook};
/// use zxv::paradox::{ParadoxKind, ParadoxOutcome};
/// let ctx = Context::new()?;
/// let cap = ctx.root_capability(1, None, Caps::ALL_1_5)?;
/// let book = ParadoxBook::new(&cap, 64, 16)?;
/// let tag = Hexagram::new(HexBase::Both, HexPolarity::Neutral, HexPhase::Potential);
/// let c = book.classify(tag, None, None)?.class;
/// assert_eq!(c.kind, ParadoxKind::Special); // new and unregistered
/// let (out, rec) = book.special(c, &[1; 32], &[2; 32])?;
/// assert_eq!((out, rec), (ParadoxOutcome::Recorded, Some(0)));
/// # Ok::<(), zxv::Error>(())
/// ```
pub struct ParadoxBook<'a> {
    raw: NonNull<sys::zxv_paradox>,
    cap: &'a Capability<'a>,
}

// SAFETY: the C book is thread-safe (its own lock).
unsafe impl Send for ParadoxBook<'_> {}
// SAFETY: as above.
unsafe impl Sync for ParadoxBook<'_> {}

impl<'a> ParadoxBook<'a> {
    /// A book with three empty logs of `log_capacity` records each
    /// (1..=[`LOG_MAX`]) and a registry of `registry_capacity` entries
    /// (1..=[`REGISTRY_MAX`]). `cap` needs [`Caps::PARADOX`](crate::Caps::PARADOX).
    pub fn new(
        cap: &'a Capability<'a>,
        log_capacity: u32,
        registry_capacity: u32,
    ) -> Result<ParadoxBook<'a>> {
        let mut raw = std::ptr::null_mut();
        // SAFETY: valid cap; out pointer valid.
        cap.call(|c| unsafe {
            sys::zxv_paradox_create(c, log_capacity, registry_capacity, &mut raw)
        })?;
        Ok(ParadoxBook {
            raw: non_null(raw, "paradox book")?,
            cap,
        })
    }

    fn gated(
        &self,
        f: impl FnOnce(*const sys::zxv_cap, *mut sys::zxv_paradox) -> i32,
    ) -> Result<()> {
        let b = self.raw.as_ptr();
        self.cap.call(|c| f(c, b))
    }

    /// Classifies a tag with this book's registry. `ev` may be `None` (no
    /// trend); `sig` may be `None`. A registered signature overrides the
    /// trend for Both and Neither. Reads only.
    pub fn classify(
        &self,
        tag: Hexagram,
        ev: Option<&ParadoxEvidence>,
        sig: Option<&[u8; 32]>,
    ) -> Result<ParadoxClassification> {
        let b = self.raw.as_ptr().cast_const();
        local_with(self.cap, || classify_raw(b, tag, ev, sig))
    }

    /// Adds a reviewed signature as kind constructive, destructive or
    /// neutral with your reason code and `FLAG_*` flags; returns its index.
    /// An entry flagged money or device needs `owner_ok`, else
    /// `ZXV_E_PARADOX_OWNER`. `ZXV_E_DUPLICATE`: already registered.
    pub fn registry_add(
        &self,
        sig: &[u8; 32],
        kind: ParadoxKind,
        reason: u8,
        flags: u8,
        owner_ok: bool,
    ) -> Result<u32> {
        let mut i = 0u32;
        // SAFETY: valid cap and book; sig 32 bytes.
        self.gated(|c, b| unsafe {
            sys::zxv_paradox_registry_add(
                c,
                b,
                sig.as_ptr(),
                kind.raw(),
                reason,
                flags,
                i32::from(owner_ok),
                &mut i,
            )
        })?;
        Ok(i)
    }

    /// The index of a signature (`ZXV_E_NOT_FOUND` if absent).
    pub fn registry_find(&self, sig: &[u8; 32]) -> Result<u32> {
        let mut i = 0u32;
        let b = self.raw.as_ptr();
        // SAFETY: valid book; sig 32 bytes.
        local(self.cap, || unsafe {
            sys::zxv_paradox_registry_find(b, sig.as_ptr(), &mut i)
        })?;
        Ok(i)
    }

    /// The number of registry entries.
    pub fn registry_count(&self) -> Result<u32> {
        let mut n = 0u32;
        let b = self.raw.as_ptr();
        // SAFETY: valid book; out pointer valid.
        local(self.cap, || unsafe {
            sys::zxv_paradox_registry_count(b, &mut n)
        })?;
        Ok(n)
    }

    /// Entry `index` (`ZXV_E_NOT_FOUND` past the last).
    pub fn registry_at(&self, index: u32) -> Result<ParadoxRegistryEntry> {
        let mut sig = [0u8; 32];
        let (mut k, mut r, mut f, mut s) = (0i32, 0u8, 0u8, 0u64);
        let b = self.raw.as_ptr();
        // SAFETY: valid book; out pointers valid.
        local(self.cap, || unsafe {
            sys::zxv_paradox_registry_at(b, index, sig.as_mut_ptr(), &mut k, &mut r, &mut f, &mut s)
        })?;
        Ok(ParadoxRegistryEntry {
            sig,
            kind: ParadoxKind::from_raw(k),
            reason: r,
            flags: f,
            src: s,
        })
    }

    /// Every entry, in index order (a lazy iterator of
    /// [`registry_at`](Self::registry_at)).
    pub fn registry(&self) -> Result<impl Iterator<Item = Result<ParadoxRegistryEntry>> + '_> {
        let n = self.registry_count()?;
        Ok((0..n).map(move |i| self.registry_at(i)))
    }

    /// The record count of a log.
    pub fn log_count(&self, which: ParadoxLog) -> Result<u32> {
        let mut n = 0u32;
        let b = self.raw.as_ptr();
        // SAFETY: valid book; out pointer valid.
        local(self.cap, || unsafe {
            sys::zxv_paradox_log_count(b, which.raw(), &mut n)
        })?;
        Ok(n)
    }

    /// A log's chain head (32 zeros when empty).
    pub fn log_head(&self, which: ParadoxLog) -> Result<[u8; 32]> {
        let mut h = [0u8; 32];
        let b = self.raw.as_ptr();
        // SAFETY: valid book; out 32 bytes.
        local(self.cap, || unsafe {
            sys::zxv_paradox_log_head(b, which.raw(), h.as_mut_ptr())
        })?;
        Ok(h)
    }

    /// Record `index` of a log as bytes ([`REC_BYTES`], or [`QREC_BYTES`]
    /// for the quarantine log; layouts in `zxv_api.h`). `ZXV_E_NOT_FOUND`
    /// past the last record.
    pub fn log_record(&self, which: ParadoxLog, index: u32) -> Result<Vec<u8>> {
        let b = self.raw.as_ptr();
        local_with(self.cap, || {
            // SAFETY: valid book; R4 buffer.
            read_bytes(REC_BYTES, |p, c, l| unsafe {
                sys::zxv_paradox_log_record(b, which.raw(), index, p, c, l)
            })
        })
    }

    /// Every record of a log, in order (a lazy iterator).
    pub fn log_records(
        &self,
        which: ParadoxLog,
    ) -> Result<impl Iterator<Item = Result<Vec<u8>>> + '_> {
        let n = self.log_count(which)?;
        Ok((0..n).map(move |i| self.log_record(which, i)))
    }

    /// Recomputes every link of a log. `ZXV_E_INVARIANT` if any record no
    /// longer matches; that poisons the context (fail closed).
    pub fn log_verify(&self, which: ParadoxLog) -> Result<()> {
        let b = self.raw.as_ptr();
        // SAFETY: valid book.
        local(self.cap, || unsafe {
            sys::zxv_paradox_log_verify(b, which.raw())
        })
    }

    /// Runs one evaluation of `policy` and then, if `synth` is given, one
    /// proposed update (one value per parameter). `tension` is your integer
    /// measure of the paradox (higher is worse). Outcomes: `Applied`,
    /// `Kept`, `ProposalForOwner` (flagged policy, or a value outside its
    /// bounds: nothing changes), `RolledBack` (tension grew, or forbidden),
    /// `Refused` (kind not constructive).
    pub fn constructive(
        &self,
        policy: &mut ParadoxPolicy<'_>,
        c: ParadoxClass,
        synth: Option<&[i64]>,
        tension: u32,
    ) -> Result<ParadoxOutcome> {
        same_context(self.cap, policy.cap, "policy")?;
        let s = synth.unwrap_or(&[]);
        let n = len32(s.len(), "synth")?;
        let sp = if s.is_empty() {
            std::ptr::null()
        } else {
            s.as_ptr()
        };
        let pl = policy.raw.as_ptr();
        let mut o = 0i32;
        // SAFETY: valid cap, book and policy (exclusive); synth n values.
        self.gated(|cp, b| unsafe {
            sys::zxv_paradox_constructive(
                cp,
                b,
                pl,
                c.kind.raw(),
                c.tag.tag(),
                sp,
                n,
                tension,
                &mut o,
            )
        })?;
        Ok(ParadoxOutcome::from_raw(o))
    }

    /// Detects and contains. When the kind is destructive or any check
    /// fires, the work is quarantined, `state` is restored from
    /// `checkpoint`, and the outcome is `Contained` with a [`RepairProof`].
    /// Otherwise the outcome is `Refused`, nothing changes and the proof is
    /// zero. It has no way to reply to or act against the submitter.
    pub fn destructive(
        &self,
        c: ParadoxClass,
        input: ParadoxContainment<'_>,
    ) -> Result<(ParadoxOutcome, RepairProof)> {
        if input.state.len() != input.checkpoint.len() {
            return Err(Error::with_detail(
                sys::ZXV_E_ARG,
                "state and checkpoint must have the same length",
            ));
        }
        let tn = len32(input.trace.len(), "trace")?;
        let sl = len32(input.state.len(), "state")?;
        let pn = len32(input.pattern.len(), "pattern")?;
        let tp = if input.trace.is_empty() {
            std::ptr::null()
        } else {
            input.trace.as_ptr()
        };
        let st = if input.state.is_empty() {
            std::ptr::null_mut()
        } else {
            input.state.as_mut_ptr()
        };
        let mut proof = RepairProof::default();
        let mut o = 0i32;
        // SAFETY: valid cap and book; every buffer valid for its length;
        // state exclusively borrowed; proof 160 bytes.
        self.gated(|cp, b| unsafe {
            sys::zxv_paradox_destructive(
                cp,
                b,
                c.kind.raw(),
                c.tag.tag(),
                input.steps,
                input.step_limit,
                input.spent,
                input.budget,
                tp,
                tn,
                i32::from(input.invariant_ok),
                st,
                opt_ptr(input.checkpoint),
                sl,
                opt_ptr(input.pattern),
                pn,
                input.submitter,
                &mut o,
                proof.0.as_mut_ptr(),
            )
        })?;
        Ok((ParadoxOutcome::from_raw(o), proof))
    }

    /// Settles `hold` on decisive evidence or on `owner_pick` (a branch
    /// index); otherwise keeps every branch. Outcomes: `Settled`,
    /// `Deferred`, `RolledBack` (forbidden: nothing settles), `Refused`
    /// (kind not neutral). Also returns the settled branch, if any.
    pub fn neutral(
        &self,
        hold: &mut ParadoxHold<'_>,
        c: ParadoxClass,
        owner_pick: Option<u32>,
    ) -> Result<(ParadoxOutcome, Option<u32>)> {
        same_context(self.cap, hold.cap, "hold")?;
        let pick = match owner_pick {
            Some(p) => i32::try_from(p)
                .map_err(|_| Error::with_detail(sys::ZXV_E_ARG, "owner pick out of range"))?,
            None => -1,
        };
        let h = hold.raw.as_ptr();
        let (mut o, mut s) = (0i32, -1i32);
        // SAFETY: valid cap, book and hold (exclusive); out pointers valid.
        self.gated(|cp, b| unsafe {
            sys::zxv_paradox_neutral(cp, b, h, c.kind.raw(), c.tag.tag(), pick, &mut o, &mut s)
        })?;
        Ok((ParadoxOutcome::from_raw(o), u32::try_from(s).ok()))
    }

    /// Records an unclassified paradox: a permanent frontier record (`sig`,
    /// and `subject` = the work's digest) plus an operator record.
    /// Outcomes: `Recorded` (with the frontier index), `Refused` (kind not
    /// special) or `RolledBack` (forbidden), with no index. The work stays
    /// sandboxed until a reviewed classification exists.
    pub fn special(
        &self,
        c: ParadoxClass,
        sig: &[u8; 32],
        subject: &[u8; 32],
    ) -> Result<(ParadoxOutcome, Option<u32>)> {
        let (mut o, mut r) = (0i32, u32::MAX);
        // SAFETY: valid cap and book; sig and subject 32 bytes.
        self.gated(|cp, b| unsafe {
            sys::zxv_paradox_special(
                cp,
                b,
                c.kind.raw(),
                c.tag.tag(),
                sig.as_ptr(),
                subject.as_ptr(),
                &mut o,
                &mut r,
            )
        })?;
        Ok((ParadoxOutcome::from_raw(o), (r != u32::MAX).then_some(r)))
    }

    /// Drafts a classification of frontier record `rec` for review: kind
    /// constructive, destructive or neutral, your reason code and `FLAG_*`
    /// flags. `ZXV_E_NOT_FOUND`: no such record.
    pub fn draft(
        &self,
        rec: u32,
        kind: ParadoxKind,
        reason: u8,
        flags: u8,
    ) -> Result<ParadoxDraft> {
        let mut d = ParadoxDraft([0; DRAFT_BYTES]);
        // SAFETY: valid cap and book; out 71 bytes.
        self.gated(|cp, b| unsafe {
            sys::zxv_paradox_draft(cp, b, rec, kind.raw(), reason, flags, d.0.as_mut_ptr())
        })?;
        Ok(d)
    }

    /// Reviews a draft (ours, or a peer's for the same frontier record). A
    /// money or device draft needs `owner_ok`; others `owner_ok` or
    /// `consensus_ok` (your quorum result). Outcomes: `Accepted` (a new
    /// registry entry), `ProposalForOwner` (not approved), `Refused`
    /// (already registered, or the registry is full). `ZXV_E_VERIFY`: the
    /// draft does not match the record.
    pub fn review(
        &self,
        d: &ParadoxDraft,
        owner_ok: bool,
        consensus_ok: bool,
    ) -> Result<ParadoxOutcome> {
        let mut o = 0i32;
        // SAFETY: valid cap and book; draft 71 bytes.
        self.gated(|cp, b| unsafe {
            sys::zxv_paradox_review(
                cp,
                b,
                d.0.as_ptr(),
                i32::from(owner_ok),
                i32::from(consensus_ok),
                &mut o,
            )
        })?;
        Ok(ParadoxOutcome::from_raw(o))
    }

    /// Runs one pass of `graph` in the fixed order (contain, sandbox, hold,
    /// apply; nodes in id order) and logs it: `Resolved` or `Pending`. When
    /// the pass count reaches the step limit unresolved, the pass is logged
    /// and the call returns `ZXV_E_PARADOX_STEP`
    /// ([`RefusedKind::ParadoxStep`](crate::RefusedKind::ParadoxStep)),
    /// which means [`ParadoxOutcome::EscalateToOwner`]; the graph stays
    /// readable.
    pub fn graph_pass(&self, graph: &mut ParadoxGraph<'_>) -> Result<ParadoxOutcome> {
        same_context(self.cap, graph.cap, "graph")?;
        let g = graph.raw.as_ptr();
        let mut o = -1i32;
        // SAFETY: valid cap, book and graph (exclusive); out pointer valid.
        self.gated(|cp, b| unsafe { sys::zxv_paradox_graph_pass(cp, b, g, &mut o) })?;
        Ok(ParadoxOutcome::from_raw(o))
    }
}

impl fmt::Debug for ParadoxBook<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("ParadoxBook")
            .field("registry", &self.registry_count().ok())
            .field("ops", &self.log_count(ParadoxLog::Ops).ok())
            .finish()
    }
}

impl Drop for ParadoxBook<'_> {
    fn drop(&mut self) {
        // SAFETY: owned handle destroyed once, before its capability.
        unsafe { sys::zxv_paradox_destroy(self.raw.as_ptr()) }
    }
}

/// A policy's live state ([`ParadoxPolicy::values`]).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ParadoxPolicyValues {
    /// The live values.
    pub values: Vec<i64>,
    /// The values before the last applied update.
    pub prev: Vec<i64>,
    /// An applied update awaits evaluation.
    pub armed: bool,
    /// The tension it was applied at.
    pub tension_at: u32,
}

/// A set of n bounded parameters (swarm parameters such as budget shares,
/// routing weights or escrow timing; never balances). The bounds never
/// change. A policy flagged money or device is never changed
/// automatically. `Send`, not `Sync`.
pub struct ParadoxPolicy<'a> {
    raw: NonNull<sys::zxv_paradox_policy>,
    cap: &'a Capability<'a>,
    n: u32,
}

// SAFETY: owns its handle; not thread-safe in C, so no Sync and the
// operators that change it take &mut.
unsafe impl Send for ParadoxPolicy<'_> {}

impl<'a> ParadoxPolicy<'a> {
    /// A policy with bounds `lo[i] <= hi[i]` and initial values inside them
    /// (1..=[`POLICY_MAX`] parameters, all three slices the same length).
    /// `cap` needs `PARADOX`. `ZXV_E_RANGE` for `lo > hi` or a value outside
    /// its bounds.
    pub fn new(
        cap: &'a Capability<'a>,
        lo: &[i64],
        hi: &[i64],
        initial: &[i64],
        flags: u8,
    ) -> Result<ParadoxPolicy<'a>> {
        let n = lo.len();
        if n == 0 || hi.len() != n || initial.len() != n {
            return Err(Error::with_detail(
                sys::ZXV_E_ARG,
                "lo, hi and initial must have the same nonzero length",
            ));
        }
        let n = len32(n, "policy")?;
        let mut raw = std::ptr::null_mut();
        // SAFETY: valid cap; three arrays of n values; out pointer valid.
        cap.call(|c| unsafe {
            sys::zxv_paradox_policy_create(
                c,
                n,
                lo.as_ptr(),
                hi.as_ptr(),
                initial.as_ptr(),
                u32::from(flags),
                &mut raw,
            )
        })?;
        Ok(ParadoxPolicy {
            raw: non_null(raw, "paradox policy")?,
            cap,
            n,
        })
    }

    /// The number of parameters.
    pub fn len(&self) -> usize {
        self.n as usize
    }

    /// Always false (a policy has at least one parameter).
    pub fn is_empty(&self) -> bool {
        self.n == 0
    }

    /// The live values and the update state.
    pub fn values(&self) -> Result<ParadoxPolicyValues> {
        let (mut v, mut p) = (vec![0i64; self.len()], vec![0i64; self.len()]);
        let (mut armed, mut ta) = (0i32, 0u32);
        let pl = self.raw.as_ptr();
        // SAFETY: valid policy; arrays of n values.
        local(self.cap, || unsafe {
            sys::zxv_paradox_policy_values(
                pl,
                v.as_mut_ptr(),
                p.as_mut_ptr(),
                self.n,
                &mut armed,
                &mut ta,
            )
        })?;
        Ok(ParadoxPolicyValues {
            values: v,
            prev: p,
            armed: armed != 0,
            tension_at: ta,
        })
    }

    /// The published bounds `(lo, hi)` and the flags.
    pub fn bounds(&self) -> Result<(Vec<i64>, Vec<i64>, u8)> {
        let (mut lo, mut hi) = (vec![0i64; self.len()], vec![0i64; self.len()]);
        let mut f = 0u32;
        let pl = self.raw.as_ptr();
        // SAFETY: valid policy; arrays of n values.
        local(self.cap, || unsafe {
            sys::zxv_paradox_policy_bounds(pl, lo.as_mut_ptr(), hi.as_mut_ptr(), self.n, &mut f)
        })?;
        Ok((lo, hi, f as u8))
    }

    /// A proposed update as bytes for the owner or for signing
    /// ([`proposal_bytes`]`(n)`: n, how many values are out of bounds,
    /// flags, then lo, hi, current, proposed per value). Reads only.
    pub fn proposal(&self, synth: &[i64]) -> Result<Vec<u8>> {
        if synth.len() != self.len() {
            return Err(Error::with_detail(
                sys::ZXV_E_ARG,
                "synth must have one value per parameter",
            ));
        }
        let pl = self.raw.as_ptr();
        local_with(self.cap, || {
            // SAFETY: valid policy; synth n values; R4 buffer.
            read_bytes(proposal_bytes(self.n), |b, c, l| unsafe {
                sys::zxv_paradox_policy_proposal(pl, synth.as_ptr(), self.n, b, c, l)
            })
        })
    }
}

impl fmt::Debug for ParadoxPolicy<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("ParadoxPolicy")
            .field("values", &self.values().ok())
            .finish()
    }
}

impl Drop for ParadoxPolicy<'_> {
    fn drop(&mut self) {
        // SAFETY: owned handle destroyed once, before its capability.
        unsafe { sys::zxv_paradox_policy_destroy(self.raw.as_ptr()) }
    }
}

/// A hold's state ([`ParadoxHold::info`]).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ParadoxHoldInfo {
    /// Branches.
    pub count: u32,
    /// The settled branch, or `None` while deferred.
    pub settled: Option<u32>,
    /// The branch with decisive evidence now, or `None`.
    pub decisive: Option<u32>,
}

/// One candidate branch and its evidence.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ParadoxBranch {
    /// The candidate's digest.
    pub id: [u8; 32],
    /// Evidence for.
    pub pos: u32,
    /// Evidence against.
    pub neg: u32,
}

/// Up to `capacity` candidate branches. A branch settles when its net
/// evidence leads every other by at least `decisive`, or alone reaches it,
/// or the owner picks it. `Send`, not `Sync`.
pub struct ParadoxHold<'a> {
    raw: NonNull<sys::zxv_paradox_hold>,
    cap: &'a Capability<'a>,
}

// SAFETY: owns its handle; not thread-safe in C, so no Sync.
unsafe impl Send for ParadoxHold<'_> {}

impl<'a> ParadoxHold<'a> {
    /// A hold of `capacity` branches (1..=[`HOLD_MAX`]); `decisive` 0 means
    /// 1. `cap` needs `PARADOX`.
    pub fn new(cap: &'a Capability<'a>, capacity: u32, decisive: u32) -> Result<ParadoxHold<'a>> {
        let mut raw = std::ptr::null_mut();
        // SAFETY: valid cap; out pointer valid.
        cap.call(|c| unsafe { sys::zxv_paradox_hold_create(c, capacity, decisive, &mut raw) })?;
        Ok(ParadoxHold {
            raw: non_null(raw, "paradox hold")?,
            cap,
        })
    }

    /// Adds a candidate branch (a 32-byte digest); the same id again is the
    /// same candidate. `ZXV_E_STATE` once settled; `ZXV_E_CAPACITY` when
    /// full.
    pub fn add(&mut self, id: &[u8; 32]) -> Result<()> {
        let h = self.raw.as_ptr();
        // SAFETY: owned hold, exclusive; id 32 bytes.
        local(self.cap, || unsafe {
            sys::zxv_paradox_hold_add(h, id.as_ptr())
        })
    }

    /// Records one piece of evidence for (`positive`) or against a
    /// candidate. `ZXV_E_NOT_FOUND`: unknown id. `ZXV_E_STATE` once settled.
    pub fn evidence(&mut self, id: &[u8; 32], positive: bool) -> Result<()> {
        let h = self.raw.as_ptr();
        // SAFETY: owned hold, exclusive; id 32 bytes.
        local(self.cap, || unsafe {
            sys::zxv_paradox_hold_evidence(h, id.as_ptr(), i32::from(positive))
        })
    }

    /// The hold's state.
    pub fn info(&self) -> Result<ParadoxHoldInfo> {
        let (mut n, mut s, mut d) = (0u32, -1i32, -1i32);
        let h = self.raw.as_ptr();
        // SAFETY: owned hold; out pointers valid.
        local(self.cap, || unsafe {
            sys::zxv_paradox_hold_info(h, &mut n, &mut s, &mut d)
        })?;
        Ok(ParadoxHoldInfo {
            count: n,
            settled: u32::try_from(s).ok(),
            decisive: u32::try_from(d).ok(),
        })
    }

    /// Branch `index`.
    pub fn branch(&self, index: u32) -> Result<ParadoxBranch> {
        let mut id = [0u8; 32];
        let (mut pos, mut neg) = (0u32, 0u32);
        let h = self.raw.as_ptr();
        // SAFETY: owned hold; out pointers valid.
        local(self.cap, || unsafe {
            sys::zxv_paradox_hold_branch(h, index, id.as_mut_ptr(), &mut pos, &mut neg)
        })?;
        Ok(ParadoxBranch { id, pos, neg })
    }

    /// Every branch, in order (a lazy iterator).
    pub fn branches(&self) -> Result<impl Iterator<Item = Result<ParadoxBranch>> + '_> {
        let n = self.info()?.count;
        Ok((0..n).map(move |i| self.branch(i)))
    }
}

impl fmt::Debug for ParadoxHold<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("ParadoxHold")
            .field("info", &self.info().ok())
            .finish()
    }
}

impl Drop for ParadoxHold<'_> {
    fn drop(&mut self) {
        // SAFETY: owned handle destroyed once, before its capability.
        unsafe { sys::zxv_paradox_hold_destroy(self.raw.as_ptr()) }
    }
}

/// One graph node ([`ParadoxGraph::node_at`]).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ParadoxNode {
    /// The hash of its signed input.
    pub id: [u8; 32],
    /// Its current kind.
    pub kind: ParadoxKind,
    /// Its state.
    pub state: ParadoxNodeState,
}

/// A graph's progress ([`ParadoxGraph::info`]).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ParadoxGraphInfo {
    /// Node count.
    pub nodes: u32,
    /// Passes run.
    pub pass: u32,
    /// Net tension of the open nodes after the last pass.
    pub total_mag: u32,
    /// Its direction, -1, 0 or +1.
    pub total_dir: i32,
    /// The step limit was reached.
    pub escalated: bool,
    /// SHA-256 chain over every pass (zeros before the first).
    pub chain: [u8; 32],
}

/// A dependency graph of up to [`GRAPH_MAX`] nodes that escalates to the
/// owner after `step_limit` passes without resolving. `Send`, not `Sync`.
pub struct ParadoxGraph<'a> {
    raw: NonNull<sys::zxv_paradox_graph>,
    cap: &'a Capability<'a>,
}

// SAFETY: owns its handle; not thread-safe in C, so no Sync.
unsafe impl Send for ParadoxGraph<'_> {}

impl<'a> ParadoxGraph<'a> {
    /// A graph (`step_limit >= 1`). `cap` needs `PARADOX`.
    pub fn new(cap: &'a Capability<'a>, step_limit: u32) -> Result<ParadoxGraph<'a>> {
        let mut raw = std::ptr::null_mut();
        // SAFETY: valid cap; out pointer valid.
        cap.call(|c| unsafe { sys::zxv_paradox_graph_create(c, step_limit, &mut raw) })?;
        Ok(ParadoxGraph {
            raw: non_null(raw, "paradox graph")?,
            cap,
        })
    }

    /// Adds a node before the first pass: `id` = the hash of its signed
    /// input (the sort key), a classified pair, tension magnitude `mag` and
    /// direction `dir` (-1, 0 or +1). `ZXV_E_DUPLICATE`, `ZXV_E_CAPACITY`,
    /// `ZXV_E_STATE` (after the first pass).
    pub fn add_node(&mut self, id: &[u8; 32], c: ParadoxClass, mag: u32, dir: i32) -> Result<()> {
        let g = self.raw.as_ptr();
        // SAFETY: owned graph, exclusive; id 32 bytes.
        local(self.cap, || unsafe {
            sys::zxv_paradox_graph_add_node(g, id.as_ptr(), c.kind.raw(), c.tag.tag(), mag, dir)
        })
    }

    /// Makes node `id` depend on node `dep` (which may be added later).
    /// `ZXV_E_NOT_FOUND`: no node `id`. `ZXV_E_CAPACITY`: more than
    /// [`MAX_DEPS`].
    pub fn add_dep(&mut self, id: &[u8; 32], dep: &[u8; 32]) -> Result<()> {
        let g = self.raw.as_ptr();
        // SAFETY: owned graph, exclusive; ids 32 bytes.
        local(self.cap, || unsafe {
            sys::zxv_paradox_graph_add_dep(g, id.as_ptr(), dep.as_ptr())
        })
    }

    /// Queues new evidence for a node, applied at the start of the next
    /// pass ("worst wins" within a batch, so order never matters).
    pub fn update(&mut self, id: &[u8; 32], c: ParadoxClass, mag: u32, dir: i32) -> Result<()> {
        let g = self.raw.as_ptr();
        // SAFETY: owned graph, exclusive; id 32 bytes.
        local(self.cap, || unsafe {
            sys::zxv_paradox_graph_update(g, id.as_ptr(), c.kind.raw(), c.tag.tag(), mag, dir)
        })
    }

    /// A node's state (`Open` before the first pass).
    pub fn state(&self, id: &[u8; 32]) -> Result<ParadoxNodeState> {
        let mut s = 0i32;
        let g = self.raw.as_ptr();
        // SAFETY: owned graph; id 32 bytes.
        local(self.cap, || unsafe {
            sys::zxv_paradox_graph_state(g, id.as_ptr(), &mut s)
        })?;
        Ok(ParadoxNodeState::from_raw(s))
    }

    /// Node `index` in id order (after the first pass, else `ZXV_E_STATE`).
    pub fn node_at(&self, index: u32) -> Result<ParadoxNode> {
        let mut id = [0u8; 32];
        let (mut k, mut s) = (0i32, 0i32);
        let g = self.raw.as_ptr();
        // SAFETY: owned graph; out pointers valid.
        local(self.cap, || unsafe {
            sys::zxv_paradox_graph_node_at(g, index, id.as_mut_ptr(), &mut k, &mut s)
        })?;
        Ok(ParadoxNode {
            id,
            kind: ParadoxKind::from_raw(k),
            state: ParadoxNodeState::from_raw(s),
        })
    }

    /// Every node in id order (after the first pass; a lazy iterator).
    pub fn nodes(&self) -> Result<impl Iterator<Item = Result<ParadoxNode>> + '_> {
        let n = self.info()?.nodes;
        Ok((0..n).map(move |i| self.node_at(i)))
    }

    /// The graph's progress.
    pub fn info(&self) -> Result<ParadoxGraphInfo> {
        let (mut n, mut ps, mut m, mut d, mut e) = (0u32, 0u32, 0u32, 0i32, 0i32);
        let mut chain = [0u8; 32];
        let g = self.raw.as_ptr();
        // SAFETY: owned graph; out pointers valid.
        local(self.cap, || unsafe {
            sys::zxv_paradox_graph_info(
                g,
                &mut n,
                &mut ps,
                &mut m,
                &mut d,
                &mut e,
                chain.as_mut_ptr(),
            )
        })?;
        Ok(ParadoxGraphInfo {
            nodes: n,
            pass: ps,
            total_mag: m,
            total_dir: d,
            escalated: e != 0,
            chain,
        })
    }
}

impl fmt::Debug for ParadoxGraph<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("ParadoxGraph")
            .field("info", &self.info().ok())
            .finish()
    }
}

impl Drop for ParadoxGraph<'_> {
    fn drop(&mut self) {
        // SAFETY: owned handle destroyed once, before its capability.
        unsafe { sys::zxv_paradox_graph_destroy(self.raw.as_ptr()) }
    }
}
