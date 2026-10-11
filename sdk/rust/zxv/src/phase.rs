// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! The phase coordinator: logical ticks, admission and deferred S0 events.

use std::future::{Future, IntoFuture};
use std::num::NonZeroU64;
use std::pin::Pin;
use std::ptr::NonNull;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Mutex;
use std::task::{Context as TaskContext, Poll, Waker};
use std::time::{Duration, Instant};

use zxv_sys as sys;

use crate::capability::Capability;
use crate::error::{Error, Result};

/// Ticks in one harmonic fundamental (27,720 = lcm(1..=11)).
pub const PHASE_CYCLE: u64 = sys::ZXV_PHASE_CYCLE as u64;

/// The coordinator's answer to an admission request.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Decision {
    /// Admitted, with a non-zero token.
    Admit(NonZeroU64),
    /// Rejected.
    Veto,
    /// Parked in S0.
    Defer,
    /// Try again.
    Retry,
}

/// Flags of a step submitted to [`PhaseClock::admit`].
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
pub struct StepFlags {
    /// The step needs its phase to be healthy.
    pub requires_health: bool,
    /// The step acts on hardware.
    pub hardware: bool,
    /// The step is waiting on an S0 (unresolved) input.
    pub s0_pending: bool,
}

impl StepFlags {
    fn bits(self) -> u32 {
        (if self.requires_health {
            sys::ZXV_STEP_REQUIRES_HEALTH
        } else {
            0
        }) | (if self.hardware {
            sys::ZXV_STEP_HARDWARE
        } else {
            0
        }) | (if self.s0_pending {
            sys::ZXV_STEP_S0_PENDING
        } else {
            0
        })
    }
}

/// Health of a phase.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Health {
    /// Not reported.
    Unknown,
    /// Healthy.
    Healthy,
    /// Degraded.
    Degraded,
    /// Unhealthy.
    Unhealthy,
}

/// The state of a deferred event.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum EventState {
    /// S0: waiting for its tick.
    Pending,
    /// The clock reached its tick.
    Cleared,
    /// Cancelled before its tick.
    Cancelled,
}

impl EventState {
    fn from_raw(v: i32) -> EventState {
        match v {
            sys::ZXV_EV_PENDING => EventState::Pending,
            sys::ZXV_EV_CLEARED => EventState::Cleared,
            _ => EventState::Cancelled,
        }
    }
}

/// How a call to [`PhaseClock::advance`] went.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Advanced {
    /// The tick after advancing.
    pub now: u64,
    /// How many deferred events cleared on this advance.
    pub cleared: u32,
}

/// A phase clock (`zxv_phase`), used through a capability with
/// [`Caps::PHASE`](crate::Caps::PHASE).
///
/// ZXV time is a logical tick count. Two drivers move it: manual
/// ([`advance`](Self::advance), for deterministic tests and simulations) and
/// real time ([`run_realtime`](Self::run_realtime), on a thread you own).
/// Deferred events ([`defer`](Self::defer)) are futures that complete when
/// the clock passes their tick, on any executor.
///
/// `Send + Sync` (the C clock is mutex-protected). Only advances made
/// through this handle wake waiters.
pub struct PhaseClock<'a> {
    raw: NonNull<sys::zxv_phase>,
    cap: &'a Capability<'a>,
    wakers: Mutex<Vec<(u64, Waker)>>,
}

// SAFETY: the C phase clock is serialized by its own mutex.
unsafe impl Send for PhaseClock<'_> {}
// SAFETY: as above.
unsafe impl Sync for PhaseClock<'_> {}

impl<'a> PhaseClock<'a> {
    /// A clock at tick 0 in the capability's context (needs
    /// [`Caps::PHASE`](crate::Caps::PHASE)).
    pub fn new(cap: &'a Capability<'a>) -> Result<PhaseClock<'a>> {
        let mut raw = std::ptr::null_mut();
        cap.call(|x| unsafe { sys::zxv_phase_create_cap(x, &mut raw) })?;
        let raw = NonNull::new(raw)
            .ok_or_else(|| Error::with_detail(sys::ZXV_E_INTERNAL, "NULL phase"))?;
        Ok(PhaseClock {
            raw,
            cap,
            wakers: Mutex::new(Vec::new()),
        })
    }

    /// The current tick.
    pub fn now(&self) -> Result<u64> {
        let mut t = 0u64;
        self.local(|p| unsafe { sys::zxv_phase_now(p, &mut t) })?;
        Ok(t)
    }

    /// Advances by `delta` ticks; events due at or before the new tick clear
    /// and their futures wake.
    pub fn advance(&self, delta: u64) -> Result<Advanced> {
        let (mut now, mut cleared) = (0u64, 0u32);
        let p = self.raw.as_ptr();
        self.cap
            .call(|cap| unsafe { sys::zxv_phase_advance(cap, p, delta, &mut now, &mut cleared) })?;
        self.wake_all();
        Ok(Advanced { now, cleared })
    }

    /// Asks the coordinator to admit step `step_id` of phase `phase_id`
    /// (1..=13).
    pub fn admit(&self, phase_id: u32, step_id: u32, flags: StepFlags) -> Result<Decision> {
        let (mut d, mut tok) = (0i32, 0u64);
        let p = self.raw.as_ptr();
        self.cap.call(|cap| unsafe {
            sys::zxv_phase_admit(cap, p, phase_id, step_id, flags.bits(), &mut d, &mut tok)
        })?;
        Ok(match d {
            sys::ZXV_ADMIT => match NonZeroU64::new(tok) {
                Some(t) => Decision::Admit(t),
                None => {
                    return Err(Error::with_detail(
                        sys::ZXV_E_INTERNAL,
                        "ADMIT without a token",
                    ))
                }
            },
            sys::ZXV_VETO => Decision::Veto,
            sys::ZXV_DEFER => Decision::Defer,
            _ => Decision::Retry,
        })
    }

    /// Reports the health of phase `phase_id` (1..=13).
    pub fn set_health(&self, phase_id: u32, health: Health) -> Result<()> {
        let h = match health {
            Health::Unknown => sys::ZXV_HEALTH_UNKNOWN,
            Health::Healthy => sys::ZXV_HEALTH_HEALTHY,
            Health::Degraded => sys::ZXV_HEALTH_DEGRADED,
            Health::Unhealthy => sys::ZXV_HEALTH_UNHEALTHY,
        };
        let p = self.raw.as_ptr();
        self.cap
            .call(|cap| unsafe { sys::zxv_phase_set_health(cap, p, phase_id, h) })
    }

    /// Parks an S0 event until absolute tick `due_tick` (> now). Await the
    /// returned [`DeferredEvent`] to wait for it.
    pub fn defer(&self, due_tick: u64) -> Result<DeferredEvent<'_>> {
        let mut id = 0u64;
        let p = self.raw.as_ptr();
        self.cap
            .call(|cap| unsafe { sys::zxv_phase_defer(cap, p, due_tick, &mut id) })?;
        Ok(DeferredEvent {
            clock: self,
            id,
            due_tick,
        })
    }

    /// The same as [`defer`](Self::defer) (the name other ZXV SDKs use).
    pub fn until_tick(&self, due_tick: u64) -> Result<DeferredEvent<'_>> {
        self.defer(due_tick)
    }

    /// An event `delta` ticks after the current tick.
    pub fn after(&self, delta: u64) -> Result<DeferredEvent<'_>> {
        let now = self.now()?;
        let due = now
            .checked_add(delta)
            .ok_or_else(|| Error::with_detail(sys::ZXV_E_RANGE, "tick overflow"))?;
        self.defer(due)
    }

    /// The earliest due tick of a pending event, `None` when nothing waits.
    pub fn next_due(&self) -> Result<Option<u64>> {
        let mut t = 0u64;
        let ctx = self.cap.context();
        ctx.guard()?;
        // SAFETY: valid handle; out pointer valid.
        match unsafe { sys::zxv_phase_next_due(self.raw.as_ptr(), &mut t) } {
            sys::ZXV_OK => Ok(Some(t)),
            sys::ZXV_E_NOT_FOUND => Ok(None),
            rc => Err(ctx.fatal(Error::from_status(rc))),
        }
    }

    /// Real-time driver: advances the clock at `ticks_per_second` until
    /// `stop` becomes true, waking deferred events as they clear. Blocks the
    /// calling thread; run it on a thread you own, e.g. with
    /// [`std::thread::scope`]. The tick count is derived from elapsed time
    /// in integer nanoseconds, so it does not drift.
    pub fn run_realtime(&self, ticks_per_second: u64, stop: &AtomicBool) -> Result<()> {
        if ticks_per_second == 0 {
            return Err(Error::with_detail(
                sys::ZXV_E_ARG,
                "ticks_per_second must be > 0",
            ));
        }
        let start = Instant::now();
        let mut issued: u128 = 0;
        let step =
            Duration::from_nanos((1_000_000_000 / ticks_per_second).clamp(1_000, 10_000_000));
        while !stop.load(Ordering::Acquire) {
            std::thread::sleep(step);
            let due = start.elapsed().as_nanos() * ticks_per_second as u128 / 1_000_000_000;
            if due > issued {
                let delta = u64::try_from(due - issued).unwrap_or(u64::MAX);
                self.advance(delta)?;
                issued += delta as u128;
            }
        }
        Ok(())
    }

    fn event_state(&self, id: u64) -> Result<(EventState, u64)> {
        let (mut st, mut due) = (0i32, 0u64);
        self.local(|p| unsafe { sys::zxv_phase_event(p, id, &mut st, &mut due) })?;
        Ok((EventState::from_raw(st), due))
    }

    fn cancel(&self, id: u64) -> Result<()> {
        let p = self.raw.as_ptr();
        self.cap
            .call(|cap| unsafe { sys::zxv_phase_cancel(cap, p, id) })?;
        self.wake_all();
        Ok(())
    }

    fn register(&self, id: u64, w: &Waker) {
        let mut ws = self.wakers.lock().unwrap_or_else(|p| p.into_inner());
        if !ws.iter().any(|(i, x)| *i == id && x.will_wake(w)) {
            ws.push((id, w.clone()));
        }
    }

    fn wake_all(&self) {
        let ws = std::mem::take(&mut *self.wakers.lock().unwrap_or_else(|p| p.into_inner()));
        ws.into_iter().for_each(|(_, w)| w.wake());
    }

    fn local(&self, f: impl FnOnce(*mut sys::zxv_phase) -> i32) -> Result<()> {
        let ctx = self.cap.context();
        ctx.guard()?;
        ctx.check(f(self.raw.as_ptr()))
    }
}

impl Drop for PhaseClock<'_> {
    fn drop(&mut self) {
        // SAFETY: owned handle; every DeferredEvent borrowing it is gone.
        unsafe { sys::zxv_phase_destroy(self.raw.as_ptr()) }
    }
}

/// An S0 event parked on a [`PhaseClock`] until its tick.
///
/// `.await` it (or call [`cleared`](Self::cleared)) to wait: it resolves
/// `Ok(EventState::Cleared)` once the clock reaches the due tick, or
/// `Ok(EventState::Cancelled)` if it was cancelled.
#[derive(Debug, Clone, Copy)]
pub struct DeferredEvent<'p> {
    clock: &'p PhaseClock<'p>,
    id: u64,
    due_tick: u64,
}

impl std::fmt::Debug for PhaseClock<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("PhaseClock")
            .field("now", &self.now().ok())
            .finish()
    }
}

impl<'p> DeferredEvent<'p> {
    /// The event id.
    pub fn id(&self) -> u64 {
        self.id
    }

    /// The absolute tick it waits for.
    pub fn due_tick(&self) -> u64 {
        self.due_tick
    }

    /// Its current state.
    pub fn state(&self) -> Result<EventState> {
        Ok(self.clock.event_state(self.id)?.0)
    }

    /// Cancels it (`ZXV_E_STATE` if no longer pending).
    pub fn cancel(&self) -> Result<()> {
        self.clock.cancel(self.id)
    }

    /// A future that resolves when the event clears or is cancelled.
    pub fn cleared(&self) -> EventFuture<'p> {
        EventFuture { event: *self }
    }
}

impl<'p> IntoFuture for DeferredEvent<'p> {
    type Output = Result<EventState>;
    type IntoFuture = EventFuture<'p>;

    fn into_future(self) -> EventFuture<'p> {
        EventFuture { event: self }
    }
}

/// The future of a [`DeferredEvent`].
#[must_use = "futures do nothing unless awaited"]
#[derive(Debug)]
pub struct EventFuture<'p> {
    event: DeferredEvent<'p>,
}

impl Future for EventFuture<'_> {
    type Output = Result<EventState>;

    fn poll(self: Pin<&mut Self>, cx: &mut TaskContext<'_>) -> Poll<Result<EventState>> {
        let ev = self.event;
        // Register first, then look, so an advance in between is not lost.
        ev.clock.register(ev.id, cx.waker());
        match ev.state() {
            Ok(EventState::Pending) => Poll::Pending,
            other => Poll::Ready(other),
        }
    }
}
