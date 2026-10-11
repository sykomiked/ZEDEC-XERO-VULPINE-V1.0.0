// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Exact rational resource budgets (rmag), with an executor-agnostic async
//! consume.

use std::ffi::c_void;
use std::future::Future;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::pin::Pin;
use std::ptr::NonNull;
use std::sync::Mutex;
use std::task::{Context as TaskContext, Poll, Waker};

use zxv_sys as sys;

use crate::capability::Capability;
use crate::error::{Error, Result};
use crate::ffi;
use crate::rational::Rational;

/// The resource a budget measures (the kernel's `rmag_resource_t`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Resource {
    /// Compute cycles.
    ComputeCycles,
    /// Memory.
    Memory,
    /// Bandwidth.
    Bandwidth,
    /// Energy.
    Energy,
    /// Storage.
    Storage,
    /// Model tokens (one token = one event).
    Tokens,
    /// Another kernel resource number.
    Other(i32),
}

impl Resource {
    fn raw(self) -> i32 {
        match self {
            Resource::ComputeCycles => sys::ZXV_RES_COMPUTE_CYCLES,
            Resource::Memory => sys::ZXV_RES_MEMORY,
            Resource::Bandwidth => sys::ZXV_RES_BANDWIDTH,
            Resource::Energy => sys::ZXV_RES_ENERGY,
            Resource::Storage => sys::ZXV_RES_STORAGE,
            Resource::Tokens => sys::ZXV_RES_TOKENS,
            Resource::Other(n) => n,
        }
    }
}

/// An event reported to [`Budgets::on_event`] callbacks.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum BudgetEvent {
    /// A consume was refused (exhausted).
    Denied,
    /// A consume or release was malformed.
    Invalid,
    /// A release or a larger limit freed budget.
    Replenished,
}

/// Remaining and used amounts of a budget.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct BudgetStatus {
    /// `limit - used` (never negative).
    pub remaining: Rational,
    /// Consumed so far.
    pub used: Rational,
    /// Number of replenishments so far.
    pub epoch: u64,
}

type Callback = Box<dyn Fn(u32, BudgetEvent) + Send + Sync>;

#[derive(Default)]
struct Shared {
    wakers: Mutex<Vec<(u32, Waker)>>,
    callbacks: Mutex<Vec<Callback>>,
}

unsafe extern "C" fn listener(user: *mut c_void, budget: u32, event: i32) {
    // SAFETY: `user` is the Box<Shared> owned by the live Budgets (the
    // listener is removed before the Box is freed).
    let shared = unsafe { &*(user as *const Shared) };
    let _ = catch_unwind(AssertUnwindSafe(|| {
        let ev = match event {
            sys::ZXV_RMAG_EV_DENIED => BudgetEvent::Denied,
            sys::ZXV_RMAG_EV_INVALID => BudgetEvent::Invalid,
            _ => BudgetEvent::Replenished,
        };
        if ev == BudgetEvent::Replenished {
            let woken: Vec<Waker> = {
                let mut w = shared.wakers.lock().unwrap_or_else(|p| p.into_inner());
                let (hit, keep): (Vec<_>, Vec<_>) = w.drain(..).partition(|(b, _)| *b == budget);
                *w = keep;
                hit.into_iter().map(|(_, w)| w).collect()
            };
            woken.into_iter().for_each(Waker::wake);
        }
        let cbs = shared.callbacks.lock().unwrap_or_else(|p| p.into_inner());
        for cb in cbs.iter() {
            cb(budget, ev);
        }
    }));
}

/// A set of budgets (one `zxv_rmag`) used through a capability with
/// [`Caps::BUDGET`](crate::Caps::BUDGET).
///
/// `Send + Sync`: the C registry has its own mutex, so a [`Budget`] may be
/// consumed from many threads and awaited on any executor.
pub struct Budgets<'a> {
    raw: NonNull<sys::zxv_rmag>,
    cap: &'a Capability<'a>,
    shared: Box<Shared>,
}

// SAFETY: the C rmag registry is serialized by its own mutex; Shared is
// Mutex-protected.
unsafe impl Send for Budgets<'_> {}
// SAFETY: as above.
unsafe impl Sync for Budgets<'_> {}

impl<'a> Budgets<'a> {
    /// An empty budget set in the capability's context (needs
    /// [`Caps::BUDGET`](crate::Caps::BUDGET)).
    pub fn new(cap: &'a Capability<'a>) -> Result<Budgets<'a>> {
        let mut raw = std::ptr::null_mut();
        cap.call(|x| unsafe { sys::zxv_rmag_create_cap(x, &mut raw) })?;
        let raw = NonNull::new(raw)
            .ok_or_else(|| Error::with_detail(sys::ZXV_E_INTERNAL, "NULL rmag"))?;
        let shared = Box::<Shared>::default();
        let user = &*shared as *const Shared as *mut c_void;
        // SAFETY: the Box outlives the registration (removed in Drop).
        let rc = unsafe { sys::zxv_rmag_listen(raw.as_ptr(), Some(listener), user) };
        if rc != sys::ZXV_OK {
            // SAFETY: owned handle, never shared.
            unsafe { sys::zxv_rmag_destroy(raw.as_ptr()) };
            return Err(Error::from_status(rc));
        }
        Ok(Budgets { raw, cap, shared })
    }

    /// Creates a budget with an exact limit (>= 0). With `enforce == false`
    /// it only counts.
    pub fn create(
        &self,
        name: &str,
        resource: Resource,
        limit: Rational,
        enforce: bool,
    ) -> Result<Budget<'_>> {
        let n = ffi::cstr(name)?;
        let mut id = 0u32;
        let rm = self.raw.as_ptr();
        self.cap.call(|cap| unsafe {
            sys::zxv_rmag_budget(
                cap,
                rm,
                n.as_ptr(),
                resource.raw(),
                limit.numer(),
                limit.denom(),
                enforce as i32,
                &mut id,
            )
        })?;
        Ok(Budget { set: self, id })
    }

    /// The budget with id `id` (as returned by [`Budget::id`]).
    pub fn get(&self, id: u32) -> Budget<'_> {
        Budget { set: self, id }
    }

    /// Registers a callback for every budget event of this set (denied
    /// consumes, invalid amounts, replenishments).
    ///
    /// Callbacks run synchronously on the thread whose call caused the
    /// event, before that call returns. Keep them short; they must not
    /// register further callbacks.
    pub fn on_event(&self, f: impl Fn(u32, BudgetEvent) + Send + Sync + 'static) {
        self.shared
            .callbacks
            .lock()
            .unwrap_or_else(|p| p.into_inner())
            .push(Box::new(f));
    }

    /// Registers a callback for denied consumes only.
    pub fn on_exhausted(&self, f: impl Fn(u32) + Send + Sync + 'static) {
        self.on_event(move |b, ev| {
            if ev == BudgetEvent::Denied {
                f(b)
            }
        })
    }

    pub(crate) fn raw(&self) -> *mut sys::zxv_rmag {
        self.raw.as_ptr()
    }
}

impl Drop for Budgets<'_> {
    fn drop(&mut self) {
        // SAFETY: owned handle; every Budget/Session borrowing it is gone.
        unsafe {
            sys::zxv_rmag_listen(self.raw.as_ptr(), None, std::ptr::null_mut());
            sys::zxv_rmag_destroy(self.raw.as_ptr());
        }
    }
}

/// One budget of a [`Budgets`] set. A cheap `Copy` handle.
#[derive(Clone, Copy)]
pub struct Budget<'b> {
    set: &'b Budgets<'b>,
    id: u32,
}

impl std::fmt::Debug for Budget<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Budget").field("id", &self.id).finish()
    }
}

impl<'b> Budget<'b> {
    /// The budget id inside its set.
    pub fn id(&self) -> u32 {
        self.id
    }

    /// Consumes `amount` (> 0).
    ///
    /// * [`Error::Refused`] with `BudgetExhausted` if it would pass an
    ///   enforced limit (nothing is consumed); a later call may succeed.
    /// * [`Error::Usage`] (`ZXV_E_RMAG_INVALID`) for a zero, negative or
    ///   overflowing amount; never retry the same value.
    pub fn consume(&self, amount: Rational) -> Result<()> {
        let (rm, id) = (self.set.raw(), self.id);
        self.set.cap.call(|cap| unsafe {
            sys::zxv_rmag_consume(cap, rm, id, amount.numer(), amount.denom())
        })
    }

    /// Gives back `amount` (clamped at zero consumed) and wakes waiters.
    pub fn release(&self, amount: Rational) -> Result<()> {
        let (rm, id) = (self.set.raw(), self.id);
        self.set.cap.call(|cap| unsafe {
            sys::zxv_rmag_release(cap, rm, id, amount.numer(), amount.denom())
        })
    }

    /// Replaces the limit; a larger limit wakes waiters.
    pub fn set_limit(&self, limit: Rational) -> Result<()> {
        let (rm, id) = (self.set.raw(), self.id);
        self.set.cap.call(|cap| unsafe {
            sys::zxv_rmag_set_limit(cap, rm, id, limit.numer(), limit.denom())
        })
    }

    /// Remaining, used and the replenish epoch.
    pub fn status(&self) -> Result<BudgetStatus> {
        let (mut rn, mut rd, mut un, mut ud, mut ep) = (0, 1, 0, 1, 0u64);
        let ctx = self.set.cap.context();
        ctx.guard()?;
        // SAFETY: valid handle and out pointers.
        let rc = unsafe {
            sys::zxv_rmag_status(
                self.set.raw(),
                self.id,
                &mut rn,
                &mut rd,
                &mut un,
                &mut ud,
                &mut ep,
            )
        };
        ctx.check(rc)?;
        Ok(BudgetStatus {
            remaining: Rational::new(rn, rd)?,
            used: Rational::new(un, ud)?,
            epoch: ep,
        })
    }

    /// A future that consumes `amount`, waiting while the budget is
    /// exhausted.
    ///
    /// It resolves `Ok(())` once the consume succeeds. On exhaustion it
    /// parks until the budget is replenished (a [`release`](Self::release)
    /// or larger [`set_limit`](Self::set_limit) through this SDK) and
    /// retries. Any other error (an invalid amount, a denied capability, a
    /// fatal halt) resolves immediately.
    ///
    /// It works on any executor. Cancel it by dropping it; add a timeout
    /// with your runtime (e.g. `tokio::time::timeout`).
    pub fn consume_async(&self, amount: Rational) -> ConsumeFuture<'b> {
        ConsumeFuture {
            budget: *self,
            amount,
        }
    }

    pub(crate) fn set_raw(&self) -> *mut sys::zxv_rmag {
        self.set.raw()
    }

    fn register(&self, w: &Waker) {
        let mut ws = self
            .set
            .shared
            .wakers
            .lock()
            .unwrap_or_else(|p| p.into_inner());
        if !ws.iter().any(|(b, x)| *b == self.id && x.will_wake(w)) {
            ws.push((self.id, w.clone()));
        }
    }
}

/// The future returned by [`Budget::consume_async`].
#[must_use = "futures do nothing unless awaited"]
#[derive(Debug)]
pub struct ConsumeFuture<'b> {
    budget: Budget<'b>,
    amount: Rational,
}

impl Future for ConsumeFuture<'_> {
    type Output = Result<()>;

    fn poll(self: Pin<&mut Self>, cx: &mut TaskContext<'_>) -> Poll<Result<()>> {
        let b = self.budget;
        loop {
            let before = match b.status() {
                Ok(s) => s.epoch,
                Err(e) => return Poll::Ready(Err(e)),
            };
            match b.consume(self.amount) {
                Ok(()) => return Poll::Ready(Ok(())),
                Err(e) if e.is_budget_exhausted() => {
                    b.register(cx.waker());
                    // A replenish between the consume and the registration
                    // would be missed; the epoch tells.
                    match b.status() {
                        Ok(s) if s.epoch != before => continue,
                        Ok(_) => return Poll::Pending,
                        Err(e) => return Poll::Ready(Err(e)),
                    }
                }
                Err(e) => return Poll::Ready(Err(e)),
            }
        }
    }
}
