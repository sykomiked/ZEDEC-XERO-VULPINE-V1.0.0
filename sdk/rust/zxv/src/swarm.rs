// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! The swarm token economy: Fibonacci levels plus an emotional share.

use std::ptr::NonNull;

use zxv_sys as sys;

use crate::capability::Capability;
use crate::error::{check, Error, Result};

/// A feeling: an emoji of the kernel palette with intensity 0..=5
/// (charge 0, 1, 2, 3, 5, 8).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Feeling {
    /// The emoji, e.g. `'\u{1F914}'` (thinking face).
    pub emoji: char,
    /// 0..=5.
    pub intensity: u32,
}

impl Feeling {
    /// A feeling from an emoji and an intensity.
    pub fn new(emoji: char, intensity: u32) -> Feeling {
        Feeling { emoji, intensity }
    }
}

/// One model's share of the current cycle.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Allotment {
    /// Tokens allotted (real + imaginary).
    pub allotted: u64,
    /// The imaginary (emotional) part of `allotted`.
    pub imaginary: u64,
    /// Tokens consumed so far.
    pub used: u64,
}

/// The outcome of [`Swarm::consume`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Grant {
    /// Tokens asked for.
    pub requested: u64,
    /// Tokens granted (and now used).
    pub granted: u64,
}

impl Grant {
    /// True when fewer tokens than requested were granted (the model's
    /// allotment for this cycle is now exhausted).
    pub fn is_partial(&self) -> bool {
        self.granted < self.requested
    }
}

/// An emotion's behaviour profile.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct EmotionProfile {
    /// Valence (negative to positive).
    pub valence: i32,
    /// Suggested sampling temperature in thousandths.
    pub temperature_milli: u32,
    /// Exploration share in per mille.
    pub explore_permille: u32,
    /// Verification passes.
    pub verify_passes: u32,
    /// Scheduling priority.
    pub priority: u32,
}

/// A swarm (`zxv_swarm`) used through a capability with
/// [`Caps::SWARM`](crate::Caps::SWARM). All amounts are exact integers.
///
/// `Send + Sync` (the C swarm is mutex-protected).
pub struct Swarm<'a> {
    raw: NonNull<sys::zxv_swarm>,
    cap: &'a Capability<'a>,
}

// SAFETY: the C swarm is serialized by its own mutex.
unsafe impl Send for Swarm<'_> {}
// SAFETY: as above.
unsafe impl Sync for Swarm<'_> {}

impl<'a> Swarm<'a> {
    /// A swarm of `levels` Fibonacci levels sharing `tokens_per_cycle`
    /// (needs [`Caps::SWARM`](crate::Caps::SWARM)).
    pub fn new(cap: &'a Capability<'a>, levels: u32, tokens_per_cycle: u64) -> Result<Swarm<'a>> {
        let mut raw = std::ptr::null_mut();
        cap.call(|x| unsafe { sys::zxv_swarm_create_cap(x, levels, tokens_per_cycle, &mut raw) })?;
        let raw = NonNull::new(raw)
            .ok_or_else(|| Error::with_detail(sys::ZXV_E_INTERNAL, "NULL swarm"))?;
        Ok(Swarm { raw, cap })
    }

    /// Registers a model at a level (0-based). Level `d` holds at most
    /// `F(d+2)` models; a full level is `Refused(Capacity)`.
    pub fn register(&self, model_id: u32, level: u32) -> Result<()> {
        self.gated(|c, s| unsafe { sys::zxv_swarm_register(c, s, model_id, level) })
    }

    /// Marks a model active or idle.
    pub fn set_active(&self, model_id: u32, active: bool) -> Result<()> {
        self.gated(|c, s| unsafe { sys::zxv_swarm_set_active(c, s, model_id, active as i32) })
    }

    /// Changes the tokens shared each cycle.
    pub fn set_rate(&self, tokens_per_cycle: u64) -> Result<()> {
        self.gated(|c, s| unsafe { sys::zxv_swarm_set_rate(c, s, tokens_per_cycle) })
    }

    /// Sets the swarm's mood (its charge sizes the imaginary share). An
    /// emoji outside the palette is `ZXV_E_NOT_FOUND`.
    pub fn set_mood(&self, mood: Feeling) -> Result<()> {
        self.gated(|c, s| unsafe {
            sys::zxv_swarm_set_mood(c, s, mood.emoji as u32, mood.intensity)
        })
    }

    /// Sets one model's feeling (it splits the imaginary share).
    pub fn set_feeling(&self, model_id: u32, feeling: Feeling) -> Result<()> {
        self.gated(|c, s| unsafe {
            sys::zxv_swarm_set_feeling(c, s, model_id, feeling.emoji as u32, feeling.intensity)
        })
    }

    /// Starts a cycle and returns its number.
    pub fn begin_cycle(&self) -> Result<u64> {
        let mut n = 0u64;
        self.gated(|c, s| unsafe { sys::zxv_swarm_begin_cycle(c, s, &mut n) })?;
        Ok(n)
    }

    /// Grants up to `requested` tokens to a model. A partial grant is not an
    /// error: check [`Grant::is_partial`]. The granted tokens are used.
    pub fn consume(&self, model_id: u32, requested: u64) -> Result<Grant> {
        let mut granted = 0u64;
        let ctx = self.cap.context();
        ctx.guard()?;
        // SAFETY: valid handles; out pointer valid.
        let rc = unsafe {
            sys::zxv_swarm_consume(
                self.cap.raw(),
                self.raw.as_ptr(),
                model_id,
                requested,
                &mut granted,
            )
        };
        if rc == sys::ZXV_E_BUDGET {
            return Ok(Grant { requested, granted });
        }
        ctx.check(rc)?;
        Ok(Grant { requested, granted })
    }

    /// A model's share of the current cycle.
    pub fn allotment(&self, model_id: u32) -> Result<Allotment> {
        let (mut a, mut i, mut u) = (0u64, 0u64, 0u64);
        let ctx = self.cap.context();
        ctx.guard()?;
        // SAFETY: valid handle; out pointers valid.
        ctx.check(unsafe {
            sys::zxv_swarm_allotment(self.raw.as_ptr(), model_id, &mut a, &mut i, &mut u)
        })?;
        Ok(Allotment {
            allotted: a,
            imaginary: i,
            used: u,
        })
    }

    /// Closes the cycle and returns the unused tokens (they never carry over).
    pub fn end_cycle(&self) -> Result<u64> {
        let mut e = 0u64;
        self.gated(|c, s| unsafe { sys::zxv_swarm_end_cycle(c, s, &mut e) })?;
        Ok(e)
    }

    /// Capacity of level `level`: `F(level + 2)`.
    pub fn level_capacity(level: u32) -> Result<u32> {
        let mut c = 0u32;
        // SAFETY: pure function; out pointer valid.
        check(unsafe { sys::zxv_swarm_level_capacity(level, &mut c) })?;
        Ok(c)
    }

    /// The behaviour profile of an emotion of the palette.
    pub fn emotion_profile(emoji: char) -> Result<EmotionProfile> {
        let (mut v, mut t, mut x, mut p, mut pr) = (0i32, 0u32, 0u32, 0u32, 0u32);
        // SAFETY: pure function; out pointers valid.
        check(unsafe {
            sys::zxv_swarm_emotion_profile(emoji as u32, &mut v, &mut t, &mut x, &mut p, &mut pr)
        })?;
        Ok(EmotionProfile {
            valence: v,
            temperature_milli: t,
            explore_permille: x,
            verify_passes: p,
            priority: pr,
        })
    }

    fn gated(&self, f: impl FnOnce(*const sys::zxv_cap, *mut sys::zxv_swarm) -> i32) -> Result<()> {
        let s = self.raw.as_ptr();
        self.cap.call(|c| f(c, s))
    }
}

impl Drop for Swarm<'_> {
    fn drop(&mut self) {
        // SAFETY: owned handle destroyed once, before its context.
        unsafe { sys::zxv_swarm_destroy(self.raw.as_ptr()) }
    }
}
