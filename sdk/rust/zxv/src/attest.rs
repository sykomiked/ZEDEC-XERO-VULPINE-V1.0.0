// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! The attestation registry (four-valued evidence about artifacts).

use std::ptr::NonNull;

use zxv_sys as sys;

use crate::capability::Capability;
use crate::error::{Error, Result};
use crate::ffi;
use crate::truth::Truth;

/// What an attestation is about (the kernel's `lpres_subject_type_t`,
/// 0 = SOURCE .. 14 = FIRMWARE). Pass the kernel number.
pub type Subject = i32;

/// The state of one attestation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct AttestationState {
    /// The four-valued state.
    pub state: Truth,
    /// Positive evidence count.
    pub positive: u32,
    /// Negative evidence count.
    pub negative: u32,
}

/// A registry of attestations. Recording needs [`Caps::ATTEST`](crate::Caps::ATTEST).
///
/// `Send + Sync` (the registry is mutex-protected in C).
pub struct Attestations<'a> {
    raw: NonNull<sys::zxv_lpres>,
    cap: &'a Capability<'a>,
}

// SAFETY: the C registry is protected by its own mutex.
unsafe impl Send for Attestations<'_> {}
// SAFETY: as above.
unsafe impl Sync for Attestations<'_> {}

impl<'a> Attestations<'a> {
    /// An empty registry in the capability's context (needs
    /// [`Caps::ATTEST`](crate::Caps::ATTEST)).
    pub fn new(cap: &'a Capability<'a>) -> Result<Attestations<'a>> {
        let mut raw = std::ptr::null_mut();
        cap.call(|x| unsafe { sys::zxv_lpres_create_cap(x, &mut raw) })?;
        let raw = NonNull::new(raw)
            .ok_or_else(|| Error::with_detail(sys::ZXV_E_INTERNAL, "NULL registry"))?;
        Ok(Attestations { raw, cap })
    }

    /// Records an attestation and returns its index. `name` and `attester`
    /// are at most 31 bytes; `digest` at most 32 bytes (may be empty).
    /// Attesting `True` counts as one positive piece of evidence.
    pub fn attest(
        &self,
        name: &str,
        subject: Subject,
        state: Truth,
        attester: &str,
        digest: &[u8],
    ) -> Result<u32> {
        let (n, a) = (ffi::cstr(name)?, ffi::cstr(attester)?);
        let mut idx = 0u32;
        let dp = if digest.is_empty() {
            std::ptr::null()
        } else {
            digest.as_ptr()
        };
        let dl = u32::try_from(digest.len()).unwrap_or(u32::MAX);
        let reg = self.raw.as_ptr();
        self.cap.call(|cap| unsafe {
            sys::zxv_lpres_attest(
                cap,
                reg,
                n.as_ptr(),
                subject,
                state.raw(),
                a.as_ptr(),
                dp,
                dl,
                &mut idx,
            )
        })?;
        Ok(idx)
    }

    /// Adds one piece of evidence; the state follows [`Truth::from_evidence`].
    pub fn evidence(&self, index: u32, positive: bool) -> Result<()> {
        let reg = self.raw.as_ptr();
        self.cap
            .call(|cap| unsafe { sys::zxv_lpres_evidence(cap, reg, index, positive as i32) })
    }

    /// Index of a named attestation (`ZXV_E_NOT_FOUND` if none).
    pub fn find(&self, name: &str) -> Result<u32> {
        let n = ffi::cstr(name)?;
        let mut idx = 0u32;
        self.local(|reg| unsafe { sys::zxv_lpres_find(reg, n.as_ptr(), &mut idx) })?;
        Ok(idx)
    }

    /// State and evidence counts of an attestation.
    pub fn state(&self, index: u32) -> Result<AttestationState> {
        let (mut st, mut p, mut n) = (0i32, 0u32, 0u32);
        self.local(|reg| unsafe { sys::zxv_lpres_state(reg, index, &mut st, &mut p, &mut n) })?;
        Ok(AttestationState {
            state: Truth::from_raw(st)?,
            positive: p,
            negative: n,
        })
    }

    /// True when every attestation is certain (no `Both`, no `Neither`).
    /// Actuator authority requires a clear gate.
    pub fn gate_clear(&self) -> Result<bool> {
        let mut c = 0;
        self.local(|reg| unsafe { sys::zxv_lpres_gate(reg, &mut c) })?;
        Ok(c != 0)
    }

    fn local(&self, f: impl FnOnce(*mut sys::zxv_lpres) -> i32) -> Result<()> {
        let ctx = self.cap.context();
        ctx.guard()?;
        ctx.check(f(self.raw.as_ptr()))
    }
}

impl Drop for Attestations<'_> {
    fn drop(&mut self) {
        // SAFETY: owned handle destroyed once, before its context.
        unsafe { sys::zxv_lpres_destroy(self.raw.as_ptr()) }
    }
}
