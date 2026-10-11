// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Errors: every negative libzxv status, sorted into four classes.

use std::ffi::CStr;
use std::fmt;

use zxv_sys as sys;

/// Convenience alias: every fallible call in this crate returns it.
pub type Result<T, E = Error> = std::result::Result<T, E>;

/// The class of a status, as `zxv_status_class` reports it.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum StatusClass {
    /// `ZXV_OK`.
    Ok,
    /// A caller bug: fix the arguments; retrying the same call fails again.
    Usage,
    /// A well-formed request the platform declined; a retry may succeed later.
    Refused,
    /// The context halted after a broken invariant; never retry.
    Fatal,
    /// Environmental: out of memory, I/O, internal.
    System,
}

impl StatusClass {
    /// The class of `status` according to the loaded library.
    pub fn of(status: i32) -> StatusClass {
        // SAFETY: pure function on an integer.
        match unsafe { sys::zxv_status_class(status) } {
            sys::ZXV_CLASS_OK => StatusClass::Ok,
            sys::ZXV_CLASS_USAGE => StatusClass::Usage,
            sys::ZXV_CLASS_REFUSED => StatusClass::Refused,
            sys::ZXV_CLASS_FATAL => StatusClass::Fatal,
            _ => StatusClass::System,
        }
    }
}

/// Why a well-formed request was refused ([`Error::Refused`]).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[non_exhaustive]
pub enum RefusedKind {
    /// `ZXV_E_CAP`: the capability lacks a bit, is revoked, or its
    /// attestation is not `TRUE`.
    CapabilityDenied,
    /// `ZXV_E_BUDGET`: an rmag or swarm budget is exhausted.
    BudgetExhausted,
    /// `ZXV_E_FUNDS`: a holder account would go below zero.
    InsufficientFunds,
    /// `ZXV_E_VERIFY`: a signature or tag did not verify.
    VerificationFailed,
    /// `ZXV_E_NOTIMPL`: the feature is not built into this library.
    NotImplemented,
    /// `ZXV_E_DECLINED`: a card authorization was declined.
    Declined,
    /// `ZXV_E_CAPACITY`: a fixed-size table is full.
    Capacity,
    /// `ZXV_E_STATE`: not valid in the current state.
    State,
    /// Another REFUSED status (added by a newer library).
    Other,
}

impl RefusedKind {
    fn of(status: i32) -> RefusedKind {
        match status {
            sys::ZXV_E_CAP => RefusedKind::CapabilityDenied,
            sys::ZXV_E_BUDGET => RefusedKind::BudgetExhausted,
            sys::ZXV_E_FUNDS => RefusedKind::InsufficientFunds,
            sys::ZXV_E_VERIFY => RefusedKind::VerificationFailed,
            sys::ZXV_E_NOTIMPL => RefusedKind::NotImplemented,
            sys::ZXV_E_DECLINED => RefusedKind::Declined,
            sys::ZXV_E_CAPACITY => RefusedKind::Capacity,
            sys::ZXV_E_STATE => RefusedKind::State,
            _ => RefusedKind::Other,
        }
    }
}

/// The status code, its symbolic name and the library's detail text.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ErrorInfo {
    /// The negative `ZXV_E_*` code.
    pub status: i32,
    /// Its symbolic name, e.g. `"ZXV_E_BUDGET"`.
    pub name: &'static str,
    /// Diagnostic text from `zxv_last_error`. Never branch on it.
    pub detail: String,
}

/// An error from libzxv, by status class.
///
/// Branch on the variant (and on [`RefusedKind`]); the detail text is for
/// people. `Fatal` means the context is halted: the handle that produced it
/// refuses every further call, and a retry can never succeed.
#[derive(Debug, Clone, PartialEq, Eq)]
#[non_exhaustive]
pub enum Error {
    /// A caller bug (bad argument, malformed text, float rejected, ...).
    Usage(ErrorInfo),
    /// The platform declined a well-formed request.
    Refused(RefusedKind, ErrorInfo),
    /// The context halted (`ZXV_E_INVARIANT` / `ZXV_E_POISONED`). Terminal.
    Fatal(ErrorInfo),
    /// Environmental failure (memory, I/O, internal).
    System(ErrorInfo),
}

impl Error {
    /// Builds the error for `status`, reading the thread's last-error detail.
    pub(crate) fn from_status(status: i32) -> Error {
        Error::with_detail(status, last_error())
    }

    /// An error for `status` with your own detail text, classified by
    /// `zxv_status_class` (useful in tests and fakes).
    pub fn new(status: i32, detail: impl Into<String>) -> Error {
        Error::with_detail(status, detail)
    }

    /// Builds an error with a detail chosen by the SDK.
    pub(crate) fn with_detail(status: i32, detail: impl Into<String>) -> Error {
        let info = ErrorInfo {
            status,
            name: status_name(status),
            detail: detail.into(),
        };
        match StatusClass::of(status) {
            StatusClass::Usage => Error::Usage(info),
            StatusClass::Refused => Error::Refused(RefusedKind::of(status), info),
            StatusClass::Fatal => Error::Fatal(info),
            StatusClass::Ok | StatusClass::System => Error::System(info),
        }
    }

    /// The status code, its name and the detail text.
    pub fn info(&self) -> &ErrorInfo {
        match self {
            Error::Usage(i) | Error::Refused(_, i) | Error::Fatal(i) | Error::System(i) => i,
        }
    }

    /// The negative `ZXV_E_*` status code.
    pub fn status(&self) -> i32 {
        self.info().status
    }

    /// The symbolic status name, e.g. `"ZXV_E_CAP"`.
    pub fn name(&self) -> &'static str {
        self.info().name
    }

    /// The library's diagnostic text (never branch on it).
    pub fn detail(&self) -> &str {
        &self.info().detail
    }

    /// The status class.
    pub fn class(&self) -> StatusClass {
        match self {
            Error::Usage(_) => StatusClass::Usage,
            Error::Refused(..) => StatusClass::Refused,
            Error::Fatal(_) => StatusClass::Fatal,
            Error::System(_) => StatusClass::System,
        }
    }

    /// The refusal kind, if this is [`Error::Refused`].
    pub fn refused_kind(&self) -> Option<RefusedKind> {
        match self {
            Error::Refused(k, _) => Some(*k),
            _ => None,
        }
    }

    /// True for [`Error::Fatal`].
    pub fn is_fatal(&self) -> bool {
        matches!(self, Error::Fatal(_))
    }

    /// True for a refused budget (`ZXV_E_BUDGET`).
    pub fn is_budget_exhausted(&self) -> bool {
        self.refused_kind() == Some(RefusedKind::BudgetExhausted)
    }

    /// True for a capability denial (`ZXV_E_CAP`).
    pub fn is_capability_denied(&self) -> bool {
        self.refused_kind() == Some(RefusedKind::CapabilityDenied)
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let i = self.info();
        let class = match self {
            Error::Usage(_) => "usage error",
            Error::Refused(..) => "refused",
            Error::Fatal(_) => "FATAL (context halted)",
            Error::System(_) => "system error",
        };
        if i.detail.is_empty() {
            write!(f, "{class}: {} ({})", i.name, i.status)
        } else {
            write!(f, "{class}: {} ({}): {}", i.name, i.status, i.detail)
        }
    }
}

impl std::error::Error for Error {}

/// The symbolic name of any status code (`"ZXV_E_UNKNOWN"` for unknown ones).
pub fn status_name(status: i32) -> &'static str {
    // SAFETY: zxv_status_name returns a static, never-NULL, NUL-terminated
    // ASCII string owned by the library.
    unsafe { CStr::from_ptr(sys::zxv_status_name(status)) }
        .to_str()
        .unwrap_or("ZXV_E_UNKNOWN")
}

/// The calling thread's last-error detail (R7). Empty when none.
pub fn last_error() -> String {
    let mut buf = vec![0u8; 256];
    for _ in 0..2 {
        let mut len = 0usize;
        // SAFETY: buf is valid for buf.len() bytes; len is a valid out pointer.
        let rc = unsafe { sys::zxv_last_error(buf.as_mut_ptr().cast(), buf.len(), &mut len) };
        if rc == sys::ZXV_OK {
            buf.truncate(len);
            return String::from_utf8_lossy(&buf).into_owned();
        }
        if rc != sys::ZXV_E_BUFFER {
            break;
        }
        buf = vec![0u8; len + 1];
    }
    String::new()
}

/// Checks a status from a pure (context-free) call.
pub(crate) fn check(rc: i32) -> Result<()> {
    if rc == sys::ZXV_OK {
        Ok(())
    } else {
        Err(Error::from_status(rc))
    }
}
