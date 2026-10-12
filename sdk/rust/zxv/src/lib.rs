// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! # zxv: the safe Rust SDK of the ZXV platform
//!
//! A thin, safe layer over `libzxv` (`sdk/libzxv/include/zxv_api.h`):
//!
//! * every handle is a struct with `Drop`; child handles borrow their
//!   parents, so the borrow checker enforces the C lifetime rules (R6);
//! * every fallible call returns [`Result<T, Error>`](Result), and
//!   [`Error`] is an enum by status class;
//! * numbers at the boundary are exact: [`Rational`] and integers. The only
//!   float entry point is [`Rational::from_f64_exact`];
//! * [`Truth`] is four-valued and never silently becomes a `bool`;
//! * secrets live in [`SecretKey`], wiped with `zxv_secure_zero` on drop;
//! * budgets and deferred phase events are plain [`std::future::Future`]s
//!   that work on any executor.
//!
//! ```no_run
//! use zxv::{AccountKind, Caps, Context, Ledger};
//!
//! let ctx = Context::new()?;
//! let root = ctx.root_capability(1, None, Caps::ALL)?;
//! ctx.enable_currency_cap(&root, "NGN")?;
//! let ledger = Ledger::new(&root);
//! let bank = ledger.open_account("bank", "NGN", AccountKind::Issuer)?;
//! let alice = ledger.open_account("alice", "NGN", AccountKind::Holder)?;
//! ledger.post(bank, alice, 12_500, "NGN", "E2E-0001")?;
//! assert_eq!(ledger.balance(alice)?.equity, 12_500);
//! # Ok::<(), zxv::Error>(())
//! ```
//!
//! ## What the compiler enforces
//!
//! A capability cannot outlive its context (rule R6):
//!
//! ```compile_fail
//! let cap = {
//!     let ctx = zxv::Context::new()?;
//!     ctx.root_capability(1, None, zxv::Caps::ALL)?
//! };
//! # Ok::<(), zxv::Error>(())
//! ```
//!
//! A session is `Send` but not `Sync` (one thread at a time):
//!
//! ```compile_fail
//! fn need_sync<T: Sync>() {}
//! need_sync::<zxv::Session<'static>>();
//! ```
//!
//! `Truth` never silently becomes a `bool`:
//!
//! ```compile_fail
//! let b: bool = zxv::Truth::True.into();
//! ```
//!
//! `Rational` has no panicking operators; use `checked_*`:
//!
//! ```compile_fail
//! let x = zxv::Rational::ONE + zxv::Rational::ONE;
//! ```
#![warn(missing_docs)]
#![deny(unsafe_op_in_unsafe_fn)]

mod attest;
mod budget;
mod capability;
mod context;
mod crypto;
mod engine;
mod error;
mod evm;
mod ffi;
mod hexagram;
pub mod hw;
pub mod iso20022;
pub mod iso4217;
mod ledger;
mod phase;
mod rational;
mod swarm;
mod truth;
mod wire;

use std::sync::OnceLock;

pub use attest::{AttestationState, Attestations, Subject};
pub use budget::{Budget, BudgetEvent, BudgetStatus, Budgets, ConsumeFuture, Resource};
pub use capability::{Capability, CapabilityInfo, Caps};
pub use context::{Context, EnabledCurrency, FatalPolicy};
pub use crypto::{random, sizes, Ed25519, Hashes, MlDsa87, MlKem1024, SecretKey};
pub use engine::{
    GenerateOptions, Generation, KvCache, Model, ModelInfo, Session, StopReason, Token, TokenStream,
};
pub use error::{last_error, status_name, Error, ErrorInfo, RefusedKind, Result, StatusClass};
pub use evm::{Address, EvmState, StateChannel};
pub use ffi::hex;
#[doc(hidden)]
pub use ffi::hex_decode;
pub use hexagram::{
    HexAction, HexBase, HexPhase, HexPolarity, Hexagram, QuarantineLog, QuarantineRecord,
};
pub use hw::{
    HwDevice, HwEvent, HwEventKind, HwFunctionInfo, HwImageKind, HwImageSpec, HwKind, HwLog,
    HwLogEntry, HwMatch, HwMatrix, HwNode, HwPair, HwPairing, HwPolicy, HwPower, HwProfile,
    HwProviderFlags, HwProviderInfo, HwRegion, HwRegionInfo, HwRegionState, HwRelease,
    HwReleaseKey, HwResourceState,
};
pub use ledger::{AccountId, AccountInfo, AccountKind, Balance, JournalLine, Ledger, AMOUNT_MAX};
pub use phase::{
    Advanced, Decision, DeferredEvent, EventFuture, EventState, Health, PhaseClock, StepFlags,
    PHASE_CYCLE,
};
pub use rational::Rational;
pub use swarm::{Allotment, EmotionProfile, Feeling, Grant, Swarm};
pub use truth::Truth;
pub use wire::{Ubh168Frame, WireTruth, FRAME_BYTES};

/// The raw bindings, for anything this crate does not wrap.
pub use zxv_sys as sys;

/// The ABI version of the loaded library: `(major, minor, patch)`.
pub fn version() -> (u32, u32, u32) {
    let (mut a, mut b, mut c) = (0, 0, 0);
    // SAFETY: out pointers valid.
    unsafe { sys::zxv_version(&mut a, &mut b, &mut c) };
    (a, b, c)
}

/// Feature groups live in the loaded library (`zxv_features`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Features(u32);

impl Features {
    /// Triple-ledger accounts and postings.
    pub const LEDGER: Features = Features(sys::ZXV_FEAT_LEDGER);
    /// pacs.008 messages.
    pub const MX_PACS008: Features = Features(sys::ZXV_FEAT_MX_PACS008);
    /// camt.053 messages.
    pub const MX_CAMT053: Features = Features(sys::ZXV_FEAT_MX_CAMT053);
    /// The full ISO 4217 table.
    pub const ISO4217: Features = Features(sys::ZXV_FEAT_ISO4217);
    /// Multilateral netting.
    pub const NETTING: Features = Features(sys::ZXV_FEAT_NETTING);
    /// Card issuing.
    pub const CARDS: Features = Features(sys::ZXV_FEAT_CARDS);
    /// Card check digits.
    pub const CARD_CHECK: Features = Features(sys::ZXV_FEAT_CARD_CHECK);
    /// Exact rationals.
    pub const RATIONAL: Features = Features(sys::ZXV_FEAT_RATIONAL);
    /// Four-valued logic and attestations.
    pub const LPRES: Features = Features(sys::ZXV_FEAT_LPRES);
    /// Capability handles.
    pub const CAPS: Features = Features(sys::ZXV_FEAT_CAPS);
    /// Rational budgets.
    pub const RMAG: Features = Features(sys::ZXV_FEAT_RMAG);
    /// Phase clock.
    pub const PHASE: Features = Features(sys::ZXV_FEAT_PHASE);
    /// UBH-168 wire.
    pub const WIRE: Features = Features(sys::ZXV_FEAT_WIRE);
    /// Swarm economy.
    pub const SWARM: Features = Features(sys::ZXV_FEAT_SWARM);
    /// Hashes.
    pub const HASH: Features = Features(sys::ZXV_FEAT_HASH);
    /// ML-KEM-1024.
    pub const MLKEM1024: Features = Features(sys::ZXV_FEAT_MLKEM1024);
    /// ML-DSA-87.
    pub const MLDSA87: Features = Features(sys::ZXV_FEAT_MLDSA87);
    /// Ed25519.
    pub const ED25519: Features = Features(sys::ZXV_FEAT_ED25519);
    /// The integer inference engine.
    pub const TENSOR: Features = Features(sys::ZXV_FEAT_TENSOR);
    /// LoRA adapters (not built yet).
    pub const LORA: Features = Features(sys::ZXV_FEAT_LORA);
    /// EVM state channels.
    pub const EVM: Features = Features(sys::ZXV_FEAT_EVM);
    /// The hexagram tag and quarantine log (ABI 1.3).
    pub const HEX: Features = Features(sys::ZXV_FEAT_HEX);
    /// The hardware emulation matrix, ZHP-1 profiles and hardware updates
    /// (ABI 1.4).
    pub const HW: Features = Features(sys::ZXV_FEAT_HW);

    /// The raw mask.
    pub fn bits(self) -> u32 {
        self.0
    }

    /// True if every bit of `f` is live.
    pub fn contains(self, f: Features) -> bool {
        self.0 & f.0 == f.0
    }
}

/// The feature groups live in the loaded library.
pub fn features() -> Features {
    let mut m = 0u32;
    // SAFETY: out pointer valid.
    unsafe { sys::zxv_features(&mut m) };
    Features(m)
}

/// Checks once that the loaded library speaks ABI 1.x with x >= 4.
pub(crate) fn ensure_compatible() -> Result<()> {
    static CHECK: OnceLock<Result<()>> = OnceLock::new();
    CHECK
        .get_or_init(|| {
            let (major, minor, _) = version();
            if major != sys::ZXV_ABI_VERSION_MAJOR || minor < 4 {
                return Err(Error::with_detail(
                    sys::ZXV_E_INTERNAL,
                    format!(
                        "libzxv ABI {major}.{minor} is not compatible with this SDK (needs 1.4+)"
                    ),
                ));
            }
            Ok(())
        })
        .clone()
}
