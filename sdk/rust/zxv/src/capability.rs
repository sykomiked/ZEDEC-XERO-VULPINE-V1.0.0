// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Capabilities: least-privilege handles passed on every gated call.

use std::fmt;
use std::marker::PhantomData;
use std::ops::{BitAnd, BitOr};
use std::ptr::NonNull;

use zxv_sys as sys;

use crate::context::Context;
use crate::error::{Error, Result};
use crate::truth::Truth;

/// A set of capability bits (`ZXV_CAP_*`).
///
/// ```
/// use zxv::Caps;
/// let reader = Caps::LEDGER_READ | Caps::ATTEST;
/// assert!(reader.contains(Caps::LEDGER_READ));
/// assert!(!reader.contains(Caps::LEDGER_POST));
/// ```
#[derive(Clone, Copy, PartialEq, Eq, Hash, Default)]
pub struct Caps(u64);

impl Caps {
    /// No capability.
    pub const NONE: Caps = Caps(0);
    /// Read balances and journals.
    pub const LEDGER_READ: Caps = Caps(sys::ZXV_CAP_LEDGER_READ);
    /// Post transfers.
    pub const LEDGER_POST: Caps = Caps(sys::ZXV_CAP_LEDGER_POST);
    /// Sign (ML-DSA-87, Ed25519).
    pub const CRYPTO_SIGN: Caps = Caps(sys::ZXV_CAP_CRYPTO_SIGN);
    /// ML-KEM-1024 key generation and encapsulation.
    pub const CRYPTO_KEM: Caps = Caps(sys::ZXV_CAP_CRYPTO_KEM);
    /// Load models and generate.
    pub const AI_INFERENCE: Caps = Caps(sys::ZXV_CAP_AI_INFERENCE);
    /// Create and use resource budgets.
    pub const BUDGET: Caps = Caps(sys::ZXV_CAP_BUDGET);
    /// Admit, defer and advance on the phase clock.
    pub const PHASE: Caps = Caps(sys::ZXV_CAP_PHASE);
    /// Record attestations and evidence.
    pub const ATTEST: Caps = Caps(sys::ZXV_CAP_ATTEST);
    /// Drive the swarm economy.
    pub const SWARM: Caps = Caps(sys::ZXV_CAP_SWARM);
    /// EVM state-channel encoding.
    pub const EVM: Caps = Caps(sys::ZXV_CAP_EVM);
    /// Build and call the hardware emulation matrix (ABI 1.4).
    pub const HW_MATRIX: Caps = Caps(sys::ZXV_CAP_HW_MATRIX);
    /// Load hardware images and contract packages (ABI 1.4).
    pub const HW_RECONF: Caps = Caps(sys::ZXV_CAP_HW_RECONF);
    /// Every bit of ABI 1.1 (`ZXV_CAP_ALL`, which keeps this value
    /// forever). It does not include the 1.4 bits; use
    /// [`ALL_1_4`](Self::ALL_1_4) for those.
    pub const ALL: Caps = Caps(sys::ZXV_CAP_ALL);
    /// Every bit as of ABI 1.4 (`ZXV_CAP_ALL_1_4`).
    pub const ALL_1_4: Caps = Caps(sys::ZXV_CAP_ALL_1_4);

    /// The raw bit mask.
    pub const fn bits(self) -> u64 {
        self.0
    }

    /// A set from raw bits (unknown bits are refused later by the library).
    pub const fn from_bits(bits: u64) -> Caps {
        Caps(bits)
    }

    /// True if every bit of `other` is in `self`.
    pub const fn contains(self, other: Caps) -> bool {
        self.0 & other.0 == other.0
    }
}

impl BitOr for Caps {
    type Output = Caps;
    fn bitor(self, o: Caps) -> Caps {
        Caps(self.0 | o.0)
    }
}

impl BitAnd for Caps {
    type Output = Caps;
    fn bitand(self, o: Caps) -> Caps {
        Caps(self.0 & o.0)
    }
}

impl fmt::Debug for Caps {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        const NAMES: [&str; 12] = [
            "LEDGER_READ",
            "LEDGER_POST",
            "CRYPTO_SIGN",
            "CRYPTO_KEM",
            "AI_INFERENCE",
            "BUDGET",
            "PHASE",
            "ATTEST",
            "SWARM",
            "EVM",
            "HW_MATRIX",
            "HW_RECONF",
        ];
        let names: Vec<&str> = NAMES
            .iter()
            .enumerate()
            .filter(|(i, _)| self.0 & (1 << i) != 0)
            .map(|(_, n)| *n)
            .collect();
        write!(
            f,
            "Caps({})",
            if names.is_empty() {
                "NONE".to_string()
            } else {
                names.join(" | ")
            }
        )
    }
}

/// What [`Capability::info`] reports.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CapabilityInfo {
    /// The bits this capability holds.
    pub caps: Caps,
    /// The process id it was created for.
    pub process_id: u32,
    /// Its attestation: anything but `True` blocks gated calls.
    pub attestation: Truth,
    /// True once it (or an ancestor) was revoked.
    pub revoked: bool,
}

/// A capability over a [`Context`]: the right to make some gated calls.
///
/// Every object that makes gated calls holds a `&Capability` and passes it
/// on every call; the library checks it every time (live, bit present,
/// attestation `TRUE`). Make narrower ones with
/// [`attenuate`](Self::attenuate) and hand them to the parts of the program
/// that need less. A child borrows its parent, so the borrow checker
/// guarantees the parent outlives it (as the C ABI requires).
///
/// A `Capability` is `Send + Sync`: its state is atomic and the context is
/// serialized.
///
/// **Honest limit:** inside one process this is least-privilege
/// discipline, not a sandbox. Code in the same address space can create its
/// own root capability.
pub struct Capability<'c> {
    raw: NonNull<sys::zxv_cap>,
    ctx: &'c Context,
    _parent: PhantomData<&'c ()>,
}

// SAFETY: zxv_cap state (attestation, revoked) is atomic in C, and every
// gated call serializes on the context mutex.
unsafe impl Send for Capability<'_> {}
// SAFETY: as above; all methods take &self and the C functions are safe to
// call concurrently on one cap.
unsafe impl Sync for Capability<'_> {}

impl<'c> Capability<'c> {
    /// SAFETY: `raw` must be a fresh, owned, non-NULL zxv_cap of `ctx`.
    pub(crate) unsafe fn from_raw(raw: *mut sys::zxv_cap, ctx: &'c Context) -> Capability<'c> {
        Capability {
            raw: unsafe { NonNull::new_unchecked(raw) },
            ctx,
            _parent: PhantomData,
        }
    }

    /// A child holding `self.caps & keep`. It can never be wider than its
    /// parent, and revoking the parent revokes it.
    pub fn attenuate(&self, keep: Caps) -> Result<Capability<'_>> {
        self.ctx.guard()?;
        let mut raw = std::ptr::null_mut();
        // SAFETY: valid cap; out pointer valid.
        let rc = unsafe { sys::zxv_cap_attenuate(self.raw.as_ptr(), keep.bits(), &mut raw) };
        self.ctx.check(rc)?;
        // SAFETY: fresh owned cap of the same context.
        Ok(unsafe { Capability::from_raw(raw, self.ctx) })
    }

    /// Permanently revokes this capability and every descendant.
    pub fn revoke(&self) -> Result<()> {
        // SAFETY: valid cap; revocation is an atomic store in C.
        self.call(|c| unsafe { sys::zxv_cap_revoke(c as *mut _) })
    }

    /// Sets the attestation. Anything but [`Truth::True`] blocks gated
    /// calls; `Both` reports a contradictory attestation.
    pub fn set_attestation(&self, state: Truth) -> Result<()> {
        // SAFETY: valid cap; the attestation is atomic in C.
        self.call(|c| unsafe { sys::zxv_cap_set_attestation(c as *mut _, state.raw()) })
    }

    /// Bits, process id, attestation and revocation state.
    pub fn info(&self) -> Result<CapabilityInfo> {
        let (mut caps, mut pid, mut att, mut rev) = (0u64, 0u32, 0i32, 0i32);
        // SAFETY: valid cap; out pointers valid.
        self.call(|c| unsafe { sys::zxv_cap_info(c, &mut caps, &mut pid, &mut att, &mut rev) })?;
        Ok(CapabilityInfo {
            caps: Caps(caps),
            process_id: pid,
            attestation: Truth::from_raw(att)?,
            revoked: rev != 0,
        })
    }

    /// `Ok(())` if a call needing `needed` would pass the gate, else
    /// [`Error::Refused`] with [`RefusedKind::CapabilityDenied`](crate::RefusedKind::CapabilityDenied).
    pub fn check(&self, needed: Caps) -> Result<()> {
        // SAFETY: valid cap.
        self.call(|c| unsafe { sys::zxv_cap_check(c, needed.bits()) })
    }

    /// Starts an ISO 20022 message of `kind` through this capability
    /// ([`Caps::LEDGER_READ`]); the library re-checks it on every message
    /// call, so revoking it stops the message too. The message borrows the
    /// capability, which therefore outlives it.
    pub fn new_message(
        &self,
        kind: crate::iso20022::MessageKind,
    ) -> Result<crate::iso20022::Message<'_>> {
        crate::iso20022::Message::with_cap(self, kind)
    }

    /// The context this capability governs.
    pub fn context(&self) -> &'c Context {
        self.ctx
    }

    /// The raw `zxv_cap` pointer, for calls this crate does not wrap. It
    /// stays owned by this `Capability`.
    pub fn as_raw(&self) -> *const sys::zxv_cap {
        self.raw.as_ptr()
    }

    pub(crate) fn raw(&self) -> *const sys::zxv_cap {
        self.raw.as_ptr()
    }

    /// guard + gated call + check (poisons on FATAL).
    pub(crate) fn call(&self, f: impl FnOnce(*const sys::zxv_cap) -> i32) -> Result<()> {
        self.ctx.guard()?;
        let rc = f(self.raw.as_ptr());
        self.ctx.check(rc)
    }

    /// Applies the context's fatal path to an error from a gated call.
    pub(crate) fn fatal(&self, e: Error) -> Error {
        self.ctx.fatal(e)
    }
}

impl fmt::Debug for Capability<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self.info() {
            Ok(i) => f
                .debug_struct("Capability")
                .field("caps", &i.caps)
                .field("process_id", &i.process_id)
                .field("attestation", &i.attestation)
                .field("revoked", &i.revoked)
                .finish(),
            Err(_) => f.write_str("Capability(<unavailable>)"),
        }
    }
}

impl Drop for Capability<'_> {
    fn drop(&mut self) {
        // SAFETY: children borrow this cap, so all were dropped first.
        unsafe { sys::zxv_cap_destroy(self.raw.as_ptr()) }
    }
}
