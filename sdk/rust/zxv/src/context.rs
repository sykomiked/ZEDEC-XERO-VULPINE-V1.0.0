// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! The platform context, currency configuration and the fail-closed path.

use std::ptr::NonNull;
use std::sync::atomic::{AtomicBool, Ordering};

use zxv_sys as sys;

use crate::capability::{Capability, Caps};
use crate::error::{Error, Result};
use crate::ffi;
use crate::iso20022::{Message, MessageKind};

/// What the SDK does when a call reports a FATAL status.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum FatalPolicy {
    /// Return [`Error::Fatal`] and poison the Rust handle (the default).
    /// Every later call on the context, and on every handle derived from
    /// it, returns `Error::Fatal` without reaching the library.
    #[default]
    Return,
    /// Print the error to stderr and call [`std::process::abort`]. This is
    /// the only form nobody can catch or ignore.
    Abort,
}

/// One platform instance: a ledger, its currencies and everything created
/// from it.
///
/// A `Context` is `Send + Sync` (the C context is serialized by a mutex).
/// Handles made from it ([`Capability`], [`Budgets`](crate::Budgets),
/// [`PhaseClock`](crate::PhaseClock), ...) borrow it, so the borrow checker
/// guarantees they are dropped before it (rule R6).
///
/// When a call reports a FATAL status (a broken invariant, or a deliberate
/// [`halt`](Self::halt)), the C context refuses every later call; this
/// handle is also marked poisoned and fails fast. See [`FatalPolicy`].
#[derive(Debug)]
pub struct Context {
    raw: NonNull<sys::zxv_ctx>,
    poisoned: AtomicBool,
    policy: FatalPolicy,
}

// SAFETY: R6: a zxv_ctx is internally serialized by a mutex; any number of
// threads may call functions on it.
unsafe impl Send for Context {}
// SAFETY: as above.
unsafe impl Sync for Context {}

/// Facts about a currency enabled in a context.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EnabledCurrency {
    /// Minor units (2 for cents).
    pub minor_units: u8,
    /// `ZXV_CCYF_*` flags ([`crate::iso4217::flags`]).
    pub flags: u32,
}

impl Context {
    /// Creates a context with the default [`FatalPolicy::Return`].
    ///
    /// Fails with a SYSTEM error if the loaded libzxv has a different major
    /// ABI version, or a minor version below 2.
    pub fn new() -> Result<Context> {
        Context::with_fatal_policy(FatalPolicy::Return)
    }

    /// Creates a context with an explicit [`FatalPolicy`].
    pub fn with_fatal_policy(policy: FatalPolicy) -> Result<Context> {
        crate::ensure_compatible()?;
        let mut raw = std::ptr::null_mut();
        // SAFETY: out pointer valid.
        crate::error::check(unsafe { sys::zxv_ctx_create(&mut raw) })?;
        let raw = NonNull::new(raw)
            .ok_or_else(|| Error::with_detail(sys::ZXV_E_INTERNAL, "NULL context"))?;
        Ok(Context {
            raw,
            poisoned: AtomicBool::new(false),
            policy,
        })
    }

    /// The policy chosen at creation.
    pub fn fatal_policy(&self) -> FatalPolicy {
        self.policy
    }

    /// True once this context halted (asks the library).
    pub fn is_poisoned(&self) -> bool {
        if self.poisoned.load(Ordering::Acquire) {
            return true;
        }
        let mut p = 0;
        // SAFETY: valid handle; out pointer valid.
        let rc = unsafe { sys::zxv_ctx_is_poisoned(self.raw.as_ptr(), &mut p) };
        rc == sys::ZXV_OK && p != 0
    }

    /// Halts the context on purpose (operator kill switch). Afterwards it
    /// behaves exactly like a context that broke an invariant: every call
    /// fails with [`Error::Fatal`]. `reason` is at most 150 bytes.
    pub fn halt(&self, reason: &str) -> Result<()> {
        self.guard()?;
        let c = ffi::cstr(reason)?;
        // SAFETY: valid handle and C string.
        let rc = unsafe { sys::zxv_ctx_halt(self.raw.as_ptr(), c.as_ptr()) };
        self.check(rc)?;
        self.poisoned.store(true, Ordering::Release);
        Ok(())
    }

    /// A root capability over this context holding `caps`, for process id
    /// `process_id` and an optional 21-octet identity (zeros when `None`).
    /// Its attestation starts `TRUE`.
    pub fn root_capability(
        &self,
        process_id: u32,
        identity: Option<&[u8; 21]>,
        caps: Caps,
    ) -> Result<Capability<'_>> {
        self.guard()?;
        let mut raw = std::ptr::null_mut();
        let id = identity.map_or(std::ptr::null(), |i| i.as_ptr());
        // SAFETY: valid handle; identity NULL or 21 bytes; out pointer valid.
        let rc = unsafe {
            sys::zxv_cap_create(self.raw.as_ptr(), process_id, id, caps.bits(), &mut raw)
        };
        self.check(rc)?;
        // SAFETY: on ZXV_OK the out pointer holds a fresh, owned cap.
        Ok(unsafe { Capability::from_raw(raw, self) })
    }

    /// Enables an ISO 4217 currency for ledger accounts (minor units from
    /// the ISO table) through `cap`, which needs [`Caps::LEDGER_POST`].
    /// `ZXV_E_CURRENCY` if it is not payable; a capability of another
    /// context is refused with `ZXV_E_CAP`.
    pub fn enable_currency_cap(&self, cap: &Capability<'_>, alpha: &str) -> Result<()> {
        self.own(cap)?;
        let c = ffi::cstr(alpha)?;
        cap.call(|x| unsafe { sys::zxv_ccy_enable_cap(x, c.as_ptr()) })
    }

    /// Enables an ISO 4217 currency asserting its minor units, through
    /// `cap` ([`Caps::LEDGER_POST`]).
    pub fn enable_currency_with_minor_cap(
        &self,
        cap: &Capability<'_>,
        alpha: &str,
        minor_units: u8,
    ) -> Result<()> {
        self.own(cap)?;
        let c = ffi::cstr(alpha)?;
        cap.call(|x| unsafe { sys::zxv_ccy_enable_with_minor_cap(x, c.as_ptr(), minor_units) })
    }

    /// Registers a platform-private unit (e.g. `"VFV"`) through `cap`
    /// ([`Caps::LEDGER_POST`]): usable in ledger accounts, refused in
    /// ISO 20022 messages.
    pub fn register_private_currency_cap(
        &self,
        cap: &Capability<'_>,
        code: &str,
        minor_units: u8,
    ) -> Result<()> {
        self.own(cap)?;
        let c = ffi::cstr(code)?;
        cap.call(|x| unsafe { sys::zxv_ccy_register_private_cap(x, c.as_ptr(), minor_units) })
    }

    /// Enables an ISO 4217 currency for ledger accounts (minor units from
    /// the ISO table). `ZXV_E_CURRENCY` if it is not payable.
    #[deprecated(
        since = "1.2.0",
        note = "use enable_currency_cap with a LEDGER_POST capability"
    )]
    pub fn enable_currency(&self, alpha: &str) -> Result<()> {
        let c = ffi::cstr(alpha)?;
        self.call(|ctx| unsafe { sys::zxv_ccy_enable(ctx, c.as_ptr()) })
    }

    /// Enables an ISO 4217 currency asserting its minor units.
    #[deprecated(
        since = "1.2.0",
        note = "use enable_currency_with_minor_cap with a LEDGER_POST capability"
    )]
    pub fn enable_currency_with_minor(&self, alpha: &str, minor_units: u8) -> Result<()> {
        let c = ffi::cstr(alpha)?;
        self.call(|ctx| unsafe { sys::zxv_ccy_enable_with_minor(ctx, c.as_ptr(), minor_units) })
    }

    /// Registers a platform-private unit (e.g. `"VFV"`): usable in ledger
    /// accounts, refused in ISO 20022 messages.
    #[deprecated(
        since = "1.2.0",
        note = "use register_private_currency_cap with a LEDGER_POST capability"
    )]
    pub fn register_private_currency(&self, code: &str, minor_units: u8) -> Result<()> {
        let c = ffi::cstr(code)?;
        self.call(|ctx| unsafe { sys::zxv_ccy_register_private(ctx, c.as_ptr(), minor_units) })
    }

    /// Minor units and flags of an enabled currency (`ZXV_E_NOT_FOUND` if
    /// it is not enabled).
    pub fn currency(&self, code: &str) -> Result<EnabledCurrency> {
        let c = ffi::cstr(code)?;
        let (mut mu, mut fl) = (0u8, 0u32);
        self.call(|ctx| unsafe { sys::zxv_ccy_get(ctx, c.as_ptr(), &mut mu, &mut fl) })?;
        Ok(EnabledCurrency {
            minor_units: mu,
            flags: fl,
        })
    }

    /// Codes of every enabled currency, in enabling order.
    pub fn enabled_currencies(&self) -> Result<Vec<String>> {
        let mut n = 0u32;
        self.call(|ctx| unsafe { sys::zxv_ccy_enabled_count(ctx, &mut n) })?;
        (0..n)
            .map(|i| {
                self.guard()?;
                let ctx = self.raw.as_ptr();
                ffi::read_string(|b, c, l| unsafe { sys::zxv_ccy_enabled_at(ctx, i, b, c, l) })
                    .map_err(|e| self.fatal(e))
            })
            .collect()
    }

    /// Starts an ISO 20022 message of `kind` (validated against this
    /// context's currency rules).
    #[deprecated(
        since = "1.2.0",
        note = "use Capability::new_message with a LEDGER_READ capability"
    )]
    pub fn new_message(&self, kind: MessageKind) -> Result<Message<'_>> {
        Message::new(self, kind)
    }

    /// The raw `zxv_ctx` pointer, for calls this crate does not wrap
    /// (through [`crate::sys`]). It stays owned by this `Context`.
    pub fn as_raw(&self) -> *mut sys::zxv_ctx {
        self.raw.as_ptr()
    }

    // ----- crate internals ---------------------------------------------

    /// Refuses a capability of another context (`ZXV_E_CAP`): the C cap
    /// forms act on the cap's own context, not on `self`.
    pub(crate) fn own(&self, cap: &Capability<'_>) -> Result<()> {
        self.guard()?;
        if !std::ptr::eq(cap.context(), self) {
            return Err(Error::with_detail(
                sys::ZXV_E_CAP,
                "the capability belongs to a different context",
            ));
        }
        Ok(())
    }

    /// Fails fast when the Rust handle is poisoned.
    pub(crate) fn guard(&self) -> Result<()> {
        if self.poisoned.load(Ordering::Acquire) {
            let e = Error::with_detail(
                sys::ZXV_E_POISONED,
                "context halted earlier; this handle refuses every call",
            );
            return Err(self.fatal(e));
        }
        Ok(())
    }

    /// Maps a status, poisoning this handle on FATAL.
    pub(crate) fn check(&self, rc: i32) -> Result<()> {
        if rc == sys::ZXV_OK {
            return Ok(());
        }
        Err(self.fatal(Error::from_status(rc)))
    }

    /// Applies the fatal path to `e` if it is FATAL; returns it.
    pub(crate) fn fatal(&self, e: Error) -> Error {
        if e.is_fatal() {
            self.poisoned.store(true, Ordering::Release);
            if self.policy == FatalPolicy::Abort {
                eprintln!("zxv: {e}; FatalPolicy::Abort: aborting the process");
                std::process::abort();
            }
        }
        e
    }

    /// guard + call + check for a context-level call.
    pub(crate) fn call(&self, f: impl FnOnce(*mut sys::zxv_ctx) -> i32) -> Result<()> {
        self.guard()?;
        let rc = f(self.raw.as_ptr());
        self.check(rc)
    }
}

impl Drop for Context {
    fn drop(&mut self) {
        // SAFETY: every child borrows the context, so all were dropped first.
        unsafe { sys::zxv_ctx_destroy(self.raw.as_ptr()) }
    }
}
