// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Hardware emulation and hardware updates (ABI 1.4).
//!
//! * [`HwProfile`]: a ZHP-1 machine profile, the "hardware ROM" (plain data,
//!   no capability): probe this host, take the public reference machine
//!   R1, present R1 on a host, encode, digest and match.
//! * [`HwMatrix`]: functions (engine contracts) served by interchangeable
//!   implementations. Every matrix starts with the standard engines
//!   ([`FN_SHA256`] ... [`FN_TENSOR`]), each served by a proven SOFT
//!   implementation, so every call works on any device. Needs
//!   [`Caps::HW_MATRIX`](crate::Caps::HW_MATRIX); applying contract
//!   packages needs [`Caps::HW_RECONF`](crate::Caps::HW_RECONF).
//! * [`HwRelease`]: release keys and signed packages (ML-DSA-87 **and**
//!   SLH-DSA-SHAKE-256s). Needs [`Caps::CRYPTO_SIGN`](crate::Caps::CRYPTO_SIGN).
//!   SLH-DSA signing takes seconds: building is a release step.
//! * [`HwRegion`]: a reconfigurable region updated with signed images:
//!   stage, trial, commit or roll back, every step an event and a record
//!   in a hash-chained audit log. Needs `HW_RECONF`.
//!
//! Everything is integers; there are no floats anywhere in this module.

use std::any::Any;
use std::ffi::c_void;
use std::fmt;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr::NonNull;
use std::sync::{Arc, Mutex};

use zxv_sys as sys;

use crate::capability::Capability;
use crate::crypto::SecretKey;
use crate::error::{check, Error, Result};
use crate::ffi;
use crate::hexagram::Hexagram;
use crate::truth::Truth;

// ----- constants -----------------------------------------------------------

/// Standard engine: SHA-256 (op 0).
pub const FN_SHA256: u32 = sys::ZXV_HW_FN_SHA256;
/// Standard engine: op 0/1 SHA3-256/512, op 2/3 SHAKE.
pub const FN_KECCAK: u32 = sys::ZXV_HW_FN_KECCAK;
/// Standard engine: HMAC-SHA256 (op 0; key-bearing, always local).
pub const FN_MAC: u32 = sys::ZXV_HW_FN_MAC;
/// Standard engine: n pairs of u64 -> n gcds (op 0).
pub const FN_GCD: u32 = sys::ZXV_HW_FN_GCD;
/// Standard engine: 64-lane four-valued AND / OR / NOT.
pub const FN_LOGIC64: u32 = sys::ZXV_HW_FN_LOGIC64;
/// Standard engine: one Q8_0 row dot (op 0).
pub const FN_TENSOR: u32 = sys::ZXV_HW_FN_TENSOR;

/// The resource every matrix starts with: this machine's CPU.
pub const RES_HOST: u32 = sys::ZXV_HW_RES_HOST;
/// The parent id of a root profile node.
pub const ROOT: u32 = sys::ZXV_HW_ROOT;
/// Largest private extension per profile node, in bytes.
pub const EXT_MAX: usize = sys::ZXV_HW_EXT_MAX as usize;
/// A release public key: the ML-DSA-87 key followed by the SLH-DSA one.
pub const PK_BYTES: usize = sys::ZXV_HW_PK_BYTES as usize;
/// The ML-DSA-87 half of a release secret key.
pub const MLDSA_SK_BYTES: usize = sys::ZXV_HW_MLDSA_SK_BYTES as usize;
/// The SLH-DSA-SHAKE-256s half of a release secret key.
pub const SLHDSA_SK_BYTES: usize = sys::ZXV_HW_SLHDSA_SK_BYTES as usize;
/// Both signatures of a package.
pub const SIG_BYTES: usize = sys::ZXV_HW_SIG_BYTES as usize;
/// A package header.
pub const HEADER_BYTES: usize = sys::ZXV_HW_HEADER_BYTES as usize;
/// Output capacity [`HwMatrix::call`] uses: the largest golden-vector
/// output. Use [`HwMatrix::call_into`] for larger outputs.
pub const CALL_OUT_DEFAULT: usize = 512;

/// Component classes (`ZXV_HW_CLASS_*`). Classes from [`class::PRIVATE`] up
/// are an implementer's own.
pub mod class {
    use zxv_sys as sys;
    /// `device`
    pub const MACHINE: u32 = sys::ZXV_HW_CLASS_MACHINE;
    /// `cluster`
    pub const CPU_CLUSTER: u32 = sys::ZXV_HW_CLASS_CPU_CLUSTER;
    /// `core`
    pub const CPU_CORE: u32 = sys::ZXV_HW_CLASS_CPU_CORE;
    /// `mem.cache`
    pub const CACHE: u32 = sys::ZXV_HW_CLASS_CACHE;
    /// `mem`
    pub const MEMORY: u32 = sys::ZXV_HW_CLASS_MEMORY;
    /// `interconnect`
    pub const INTERCONNECT: u32 = sys::ZXV_HW_CLASS_INTERCONNECT;
    /// `engine.tensor.int`
    pub const MATRIX_UNIT: u32 = sys::ZXV_HW_CLASS_MATRIX_UNIT;
    /// `engine.vector`
    pub const VECTOR_UNIT: u32 = sys::ZXV_HW_CLASS_VECTOR_UNIT;
    /// `engine.ternary`
    pub const TERNARY_UNIT: u32 = sys::ZXV_HW_CLASS_TERNARY_UNIT;
    /// `engine.hash`
    pub const HASH_UNIT: u32 = sys::ZXV_HW_CLASS_HASH_UNIT;
    /// `engine.crypto`
    pub const CRYPTO_UNIT: u32 = sys::ZXV_HW_CLASS_CRYPTO_UNIT;
    /// `engine.codec`
    pub const CODEC_UNIT: u32 = sys::ZXV_HW_CLASS_CODEC_UNIT;
    /// `fabric.reconfig`
    pub const RECONF_FABRIC: u32 = sys::ZXV_HW_CLASS_RECONF_FABRIC;
    /// `io.storage`
    pub const STORAGE: u32 = sys::ZXV_HW_CLASS_STORAGE;
    /// `io.network`
    pub const NETWORK: u32 = sys::ZXV_HW_CLASS_NETWORK;
    /// `radio`
    pub const RADIO: u32 = sys::ZXV_HW_CLASS_RADIO;
    /// `sensor`
    pub const SENSOR: u32 = sys::ZXV_HW_CLASS_SENSOR;
    /// `actuator`
    pub const ACTUATOR: u32 = sys::ZXV_HW_CLASS_ACTUATOR;
    /// `clock`
    pub const CLOCK: u32 = sys::ZXV_HW_CLASS_CLOCK;
    /// `power`
    pub const POWER: u32 = sys::ZXV_HW_CLASS_POWER;
    /// `secure.element`
    pub const SECURE_ELEMENT: u32 = sys::ZXV_HW_CLASS_SECURE_ELEMENT;
    /// `io.display`
    pub const DISPLAY: u32 = sys::ZXV_HW_CLASS_DISPLAY;
    /// `io.audio`
    pub const AUDIO: u32 = sys::ZXV_HW_CLASS_AUDIO;
    /// `io.wire`
    pub const WIRE: u32 = sys::ZXV_HW_CLASS_WIRE;
    /// `engine.gpu`
    pub const GPU: u32 = sys::ZXV_HW_CLASS_GPU;
    /// `package`
    pub const PACKAGE: u32 = sys::ZXV_HW_CLASS_PACKAGE;
    /// `lane`
    pub const LANE: u32 = sys::ZXV_HW_CLASS_LANE;
    /// `engine.logic64`
    pub const LOGIC_UNIT: u32 = sys::ZXV_HW_CLASS_LOGIC_UNIT;
    /// `engine.ntt`
    pub const NTT_UNIT: u32 = sys::ZXV_HW_CLASS_NTT_UNIT;
    /// `engine.frame.ubh168`
    pub const FRAME_UNIT: u32 = sys::ZXV_HW_CLASS_FRAME_UNIT;
    /// `engine.gcd`
    pub const GCD_UNIT: u32 = sys::ZXV_HW_CLASS_GCD_UNIT;
    /// `mem.channel`
    pub const MEM_CHANNEL: u32 = sys::ZXV_HW_CLASS_MEM_CHANNEL;
    /// `secure.island`
    pub const SECURE_ISLAND: u32 = sys::ZXV_HW_CLASS_SECURE_ISLAND;
    /// `io`
    pub const IO: u32 = sys::ZXV_HW_CLASS_IO;
    /// First implementer-private class.
    pub const PRIVATE: u32 = sys::ZXV_HW_CLASS_PRIVATE;
}

/// Capability keys of a profile node (`ZXV_HW_CAP_*`): bit 15 critical,
/// bits 14-13 comparison (MIN, EXACT, MASK, INFO), bits 12-0 id.
pub mod cap {
    use zxv_sys as sys;
    /// Word width in bits.
    pub const WORD_BITS: u32 = sys::ZXV_HW_CAP_WORD_BITS;
    /// Number of units (e.g. cores).
    pub const COUNT: u32 = sys::ZXV_HW_CAP_COUNT;
    /// Clock in kHz.
    pub const FREQ_KHZ: u32 = sys::ZXV_HW_CAP_FREQ_KHZ;
    /// Size in bytes.
    pub const BYTES: u32 = sys::ZXV_HW_CAP_BYTES;
    /// Cache line in bytes.
    pub const LINE_BYTES: u32 = sys::ZXV_HW_CAP_LINE_BYTES;
    /// Instruction set ([`ISA_X86_64`] ...).
    pub const ISA: u32 = sys::ZXV_HW_CAP_ISA;
    /// Supported integer widths (mask).
    pub const INT_WIDTHS: u32 = sys::ZXV_HW_CAP_INT_WIDTHS;
    /// Radix (3 for a ternary unit).
    pub const RADIX: u32 = sys::ZXV_HW_CAP_RADIX;
    /// Multiply-accumulates per cycle.
    pub const MACS_PER_CYCLE: u32 = sys::ZXV_HW_CAP_MACS_PER_CYCLE;
    /// SIMD extensions (mask).
    pub const SIMD: u32 = sys::ZXV_HW_CAP_SIMD;
    /// Tick in ns.
    pub const TICK_NS: u32 = sys::ZXV_HW_CAP_TICK_NS;
    /// Reconfigurable regions.
    pub const REGIONS: u32 = sys::ZXV_HW_CAP_REGIONS;
    /// Bandwidth in MB/s.
    pub const BANDWIDTH_MBPS: u32 = sys::ZXV_HW_CAP_BANDWIDTH_MBPS;
    /// Latency in ns.
    pub const LATENCY_NS: u32 = sys::ZXV_HW_CAP_LATENCY_NS;
    /// Post-quantum algorithms (mask).
    pub const PQ_ALGS: u32 = sys::ZXV_HW_CAP_PQ_ALGS;
    /// Hash algorithms (mask).
    pub const HASH_ALGS: u32 = sys::ZXV_HW_CAP_HASH_ALGS;
    /// Wire width in bits.
    pub const WIRE_BITS: u32 = sys::ZXV_HW_CAP_WIRE_BITS;
    /// Peak operations per microsecond.
    pub const PEAK_OPS_MHZ: u32 = sys::ZXV_HW_CAP_PEAK_OPS_MHZ;
    /// The engine contract (`FN_*`) a node implements.
    pub const CONTRACT_ID: u32 = sys::ZXV_HW_CAP_CONTRACT_ID;
    /// Its golden-vector version (MIN).
    pub const CONTRACT_VER: u32 = sys::ZXV_HW_CAP_CONTRACT_VER;
    /// The [`HwKind`](super::HwKind) serving the node.
    pub const KIND: u32 = sys::ZXV_HW_CAP_KIND;
    /// Coarse capacity, 0..15.
    pub const CAPACITY_CLASS: u32 = sys::ZXV_HW_CAP_CAPACITY_CLASS;
    /// Coarse performance, 0..15.
    pub const PERF_BUCKET: u32 = sys::ZXV_HW_CAP_PERF_BUCKET;
    /// Coarse power ([`HwPower`](super::HwPower) scale).
    pub const POWER_BUCKET: u32 = sys::ZXV_HW_CAP_POWER_BUCKET;
    /// [`ISA`] value: x86-64.
    pub const ISA_X86_64: u64 = sys::ZXV_HW_ISA_X86_64 as u64;
    /// [`ISA`] value: ARM64.
    pub const ISA_ARM64: u64 = sys::ZXV_HW_ISA_ARM64 as u64;
    /// [`ISA`] value: RISC-V 64.
    pub const ISA_RISCV64: u64 = sys::ZXV_HW_ISA_RISCV64 as u64;
    /// [`ISA`] value: WebAssembly 32.
    pub const ISA_WASM32: u64 = sys::ZXV_HW_ISA_WASM32 as u64;
}

macro_rules! hw_enum {
    ($(#[$m:meta])* $name:ident : $t:ty { $($(#[$vm:meta])* $v:ident = $c:path),+ $(,)? }) => {
        $(#[$m])*
        #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
        pub enum $name {
            $($(#[$vm])* $v),+
        }

        impl $name {
            /// Every value, in ABI order.
            pub const ALL: &'static [$name] = &[$($name::$v),+];

            /// The ABI number.
            pub fn raw(self) -> $t {
                match self {
                    $($name::$v => $c),+
                }
            }

            /// The value with ABI number `v` (`ZXV_E_ARG` otherwise).
            pub fn from_raw(v: $t) -> Result<$name> {
                $(if v == $c {
                    return Ok($name::$v);
                })+
                Err(Error::with_detail(
                    sys::ZXV_E_ARG,
                    format!("{v} is not a {}", stringify!($name)),
                ))
            }
        }
    };
}

hw_enum! {
    /// How an implementation is built, in the order the matrix prefers them
    /// at equal cost.
    HwKind: i32 {
        /// Silicon built for the function.
        Hard = sys::ZXV_HW_KIND_HARD,
        /// A reconfigurable fabric region.
        Fabric = sys::ZXV_HW_KIND_FABRIC,
        /// A microcoded unit or soft core.
        Micro = sys::ZXV_HW_KIND_MICRO,
        /// Another local component standing in.
        Peer = sys::ZXV_HW_KIND_PEER,
        /// A software emulator.
        Soft = sys::ZXV_HW_KIND_SOFT,
        /// Another machine (never serves key-bearing work).
        Remote = sys::ZXV_HW_KIND_REMOTE,
    }
}

hw_enum! {
    /// The state of a matrix resource.
    HwResourceState: i32 {
        /// Healthy.
        Ok = sys::ZXV_HW_RES_OK,
        /// Faulted: its implementations stop serving.
        Faulted = sys::ZXV_HW_RES_FAULTED,
        /// Being reconfigured.
        Reconfiguring = sys::ZXV_HW_RES_RECONFIGURING,
        /// Offline.
        Offline = sys::ZXV_HW_RES_OFFLINE,
    }
}

hw_enum! {
    /// Coarse power classes.
    HwPower: u32 {
        /// Not declared.
        Unknown = sys::ZXV_HW_POWER_UNKNOWN,
        /// At most 10 mW.
        Mw10 = sys::ZXV_HW_POWER_10MW,
        /// At most 100 mW.
        Mw100 = sys::ZXV_HW_POWER_100MW,
        /// At most 1 W.
        W1 = sys::ZXV_HW_POWER_1W,
        /// At most 10 W.
        W10 = sys::ZXV_HW_POWER_10W,
        /// At most 100 W.
        W100 = sys::ZXV_HW_POWER_100W,
        /// Above 100 W.
        More = sys::ZXV_HW_POWER_MORE,
    }
}

hw_enum! {
    /// Image and package kinds.
    HwImageKind: u32 {
        /// A full bitstream.
        Bitstream = sys::ZXV_HW_IMG_BITSTREAM,
        /// A partial bitstream.
        Partial = sys::ZXV_HW_IMG_PARTIAL,
        /// Microcode.
        Microcode = sys::ZXV_HW_IMG_MICROCODE,
        /// Firmware.
        Firmware = sys::ZXV_HW_IMG_FIRMWARE,
        /// A new golden-vector contract for a function.
        Vectors = sys::ZXV_HW_IMG_VECTORS,
    }
}

hw_enum! {
    /// The state of a reconfigurable region.
    HwRegionState: i32 {
        /// Nothing loaded (S0).
        Empty = sys::ZXV_HW_REGION_EMPTY,
        /// An image is active.
        Active = sys::ZXV_HW_REGION_ACTIVE,
        /// An image is staged in the inactive slot.
        Staged = sys::ZXV_HW_REGION_STAGED,
        /// A tested image is on probation.
        Probation = sys::ZXV_HW_REGION_PROBATION,
        /// Halted (its audit log is full).
        Halted = sys::ZXV_HW_REGION_HALTED,
    }
}

hw_enum! {
    /// Region events and audit record kinds.
    HwEventKind: i32 {
        /// Verified and loaded into the inactive slot.
        Staged = sys::ZXV_HW_EV_STAGED,
        /// Switched to and proven: probation begins.
        Tested = sys::ZXV_HW_EV_TESTED,
        /// Committed: permanent, floor raised.
        Activated = sys::ZXV_HW_EV_ACTIVATED,
        /// Back to the previous image (or to S0).
        RolledBack = sys::ZXV_HW_EV_ROLLED_BACK,
        /// Refused before loading (signature, replay).
        Rejected = sys::ZXV_HW_EV_REJECTED,
        /// Failed in hardware; payload blocked.
        Quarantined = sys::ZXV_HW_EV_QUARANTINED,
        /// A function's golden vectors replaced.
        Contract = sys::ZXV_HW_EV_CONTRACT,
    }
}

macro_rules! hw_flags {
    ($(#[$m:meta])* $name:ident { $($(#[$vm:meta])* $v:ident = $c:path),+ $(,)? }) => {
        $(#[$m])*
        #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
        pub struct $name(u32);

        impl $name {
            /// No flag.
            pub const NONE: $name = $name(0);
            $($(#[$vm])* pub const $v: $name = $name($c);)+

            /// The raw mask.
            pub const fn bits(self) -> u32 {
                self.0
            }

            /// A set from raw bits (unknown bits are refused by the library).
            pub const fn from_bits(bits: u32) -> $name {
                $name(bits)
            }

            /// True if every bit of `other` is set.
            pub const fn contains(self, other: $name) -> bool {
                self.0 & other.0 == other.0
            }
        }

        impl std::ops::BitOr for $name {
            type Output = $name;
            fn bitor(self, o: $name) -> $name {
                $name(self.0 | o.0)
            }
        }
    };
}

hw_flags! {
    /// Placement policy flags of a function.
    HwPolicy {
        /// Always local, never memoized; sticky once set.
        KEY_BEARING = sys::ZXV_HW_POLICY_KEY_BEARING,
        /// No REMOTE implementation.
        LOCAL_ONLY = sys::ZXV_HW_POLICY_LOCAL_ONLY,
    }
}

hw_flags! {
    /// Status flags of an implementation.
    HwProviderFlags {
        /// Its resource is faulted or offline.
        FAULT = sys::ZXV_HW_PF_FAULT,
        /// A shadow check found it disagreeing.
        DISPUTED = sys::ZXV_HW_PF_DISPUTED,
        /// It failed the golden vectors.
        FAILED = sys::ZXV_HW_PF_FAILED,
        /// Not proven against the current contract.
        UNPROVEN = sys::ZXV_HW_PF_UNPROVEN,
        /// Disabled.
        DISABLED = sys::ZXV_HW_PF_DISABLED,
        /// The function's policy excludes it.
        POLICY = sys::ZXV_HW_PF_POLICY,
        /// Serving the function now.
        SERVING = sys::ZXV_HW_PF_SERVING,
    }
}

fn need_len(b: &[u8], n: usize, what: &str) -> Result<()> {
    if b.len() != n {
        return Err(Error::with_detail(
            sys::ZXV_E_ARG,
            format!("{what} must be {n} bytes, got {}", b.len()),
        ));
    }
    Ok(())
}

fn opt_ptr(b: &[u8]) -> *const u8 {
    if b.is_empty() {
        std::ptr::null()
    } else {
        b.as_ptr()
    }
}

/// The status a callback hands back to C for an error.
fn status_of(e: &Error) -> i32 {
    if e.status() < 0 {
        e.status()
    } else {
        sys::ZXV_E_INTERNAL
    }
}

fn panic_status(_p: Box<dyn Any + Send>) -> i32 {
    sys::ZXV_E_INTERNAL
}

// ----- profiles ------------------------------------------------------------

/// One node of a [`HwProfile`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HwNode {
    /// Its id (ids strictly increase).
    pub id: u32,
    /// Its parent's id ([`ROOT`] for a root).
    pub parent: u32,
    /// Its component class ([`class`]).
    pub class: u32,
    /// How many capabilities it carries.
    pub caps: u32,
}

/// How many target components a host runs natively and how many it emulates.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HwMatch {
    /// Served by a host component.
    pub native: u32,
    /// Emulated by the matrix.
    pub emulated: u32,
}

/// How one target component is served on a host.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HwPairing {
    /// A host component satisfies it.
    Native {
        /// The host node id.
        host_id: u32,
    },
    /// Nothing does: the matrix emulates it.
    Emulate {
        /// The first unmet capability key (0 if the host has no component
        /// of that class).
        unmet_key: u32,
    },
    /// A device or package node: nothing to run.
    Group,
}

/// The pairing of one target node ([`HwProfile::pair`]).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HwPair {
    /// The target node id.
    pub need_id: u32,
    /// How it is served.
    pub pairing: HwPairing,
}

/// A ZHP-1 machine profile: a canonical tree of abstract components with
/// integer capability descriptors and an opaque private extension per
/// node. Plain data: no capability is needed.
///
/// ```no_run
/// use zxv::hw::{self, HwProfile};
/// let host = HwProfile::probe_host()?;
/// let r1 = HwProfile::reference()?;
/// let m = HwProfile::match_host(&host, &r1)?;
/// println!("{} native, {} emulated", m.native, m.emulated);
/// let shown = HwProfile::present(&host, &r1)?;
/// let kind = shown.cap(3, hw::cap::KIND)?; // HwKind::Hard for the CPU
/// # let _ = kind;
/// # Ok::<(), zxv::Error>(())
/// ```
pub struct HwProfile {
    raw: NonNull<sys::zxv_hw_profile>,
}

// SAFETY: a profile is plain data owned by this handle; mutation needs
// &mut self and reads go through const pointers.
unsafe impl Send for HwProfile {}
// SAFETY: as above (no interior mutability on the read path).
unsafe impl Sync for HwProfile {}

impl HwProfile {
    fn wrap(f: impl FnOnce(*mut *mut sys::zxv_hw_profile) -> i32) -> Result<HwProfile> {
        let mut raw = std::ptr::null_mut();
        check(f(&mut raw))?;
        let raw = NonNull::new(raw)
            .ok_or_else(|| Error::with_detail(sys::ZXV_E_INTERNAL, "NULL profile"))?;
        Ok(HwProfile { raw })
    }

    fn ptr(&self) -> *const sys::zxv_hw_profile {
        self.raw.as_ptr()
    }

    /// An empty profile.
    pub fn new() -> Result<HwProfile> {
        HwProfile::wrap(|o| unsafe { sys::zxv_hw_profile_create(o) })
    }

    /// Describes this machine: CPU cluster (ISA, cores, SIMD), memory,
    /// cache line. Only what the OS reports reliably.
    pub fn probe_host() -> Result<HwProfile> {
        HwProfile::wrap(|o| unsafe { sys::zxv_hw_profile_probe_host(o) })
    }

    /// A copy of the public VULPINE-class reference machine R1.
    pub fn reference() -> Result<HwProfile> {
        HwProfile::wrap(|o| unsafe { sys::zxv_hw_profile_reference(o) })
    }

    /// `target` as served on `host`: every node that runs something
    /// carries [`cap::KIND`] (HARD where the host has it, SOFT where it is
    /// emulated).
    pub fn present(host: &HwProfile, target: &HwProfile) -> Result<HwProfile> {
        HwProfile::wrap(|o| unsafe { sys::zxv_hw_profile_present(host.ptr(), target.ptr(), o) })
    }

    /// Parses a canonical ZHP-1 encoding (anything else: `ZXV_E_FORMAT`).
    pub fn decode(bytes: &[u8]) -> Result<HwProfile> {
        HwProfile::wrap(|o| unsafe { sys::zxv_hw_profile_decode(opt_ptr(bytes), bytes.len(), o) })
    }

    /// The canonical encoding.
    pub fn encode(&self) -> Result<Vec<u8>> {
        let mut len = 0usize;
        let rc =
            unsafe { sys::zxv_hw_profile_encode(self.ptr(), std::ptr::null_mut(), 0, &mut len) };
        if rc != sys::ZXV_E_BUFFER {
            check(rc)?;
            return Ok(Vec::new());
        }
        let mut buf = vec![0u8; len];
        check(unsafe {
            sys::zxv_hw_profile_encode(self.ptr(), buf.as_mut_ptr(), buf.len(), &mut len)
        })?;
        buf.truncate(len);
        Ok(buf)
    }

    /// SHA-256 of the canonical encoding: the profile's identity.
    pub fn digest(&self) -> Result<[u8; 32]> {
        let mut d = [0u8; 32];
        check(unsafe { sys::zxv_hw_profile_digest(self.ptr(), d.as_mut_ptr()) })?;
        Ok(d)
    }

    /// Adds a node: ids strictly increase, parent first (or [`ROOT`]).
    pub fn add_node(&mut self, id: u32, parent: u32, class: u32) -> Result<()> {
        check(unsafe { sys::zxv_hw_profile_node_add(self.raw.as_ptr(), id, parent, class) })
    }

    /// Sets a capability of a node.
    pub fn set_cap(&mut self, node_id: u32, key: u32, value: u64) -> Result<()> {
        check(unsafe { sys::zxv_hw_profile_cap_set(self.raw.as_ptr(), node_id, key, value) })
    }

    /// A capability of a node (`ZXV_E_NOT_FOUND` if it lacks it).
    pub fn cap(&self, node_id: u32, key: u32) -> Result<u64> {
        let mut v = 0u64;
        check(unsafe { sys::zxv_hw_profile_cap_get(self.ptr(), node_id, key, &mut v) })?;
        Ok(v)
    }

    /// Sets a node's private extension: at most [`EXT_MAX`] opaque bytes
    /// that ZXV carries, encodes and digests but never interprets.
    pub fn set_ext(&mut self, node_id: u32, ext: &[u8]) -> Result<()> {
        check(unsafe {
            sys::zxv_hw_profile_ext_set(self.raw.as_ptr(), node_id, opt_ptr(ext), ext.len())
        })
    }

    /// A node's private extension.
    pub fn ext(&self, node_id: u32) -> Result<Vec<u8>> {
        let mut buf = vec![0u8; EXT_MAX];
        let mut len = 0usize;
        check(unsafe {
            sys::zxv_hw_profile_ext_get(self.ptr(), node_id, buf.as_mut_ptr(), buf.len(), &mut len)
        })?;
        buf.truncate(len);
        Ok(buf)
    }

    /// Number of nodes.
    pub fn node_count(&self) -> Result<u32> {
        let mut n = 0u32;
        check(unsafe { sys::zxv_hw_profile_count(self.ptr(), &mut n) })?;
        Ok(n)
    }

    /// Node `index` (0 .. [`node_count`](Self::node_count)).
    pub fn node(&self, index: u32) -> Result<HwNode> {
        let (mut id, mut parent, mut class, mut caps) = (0u32, 0u32, 0u32, 0u32);
        check(unsafe {
            sys::zxv_hw_profile_node(
                self.ptr(),
                index,
                &mut id,
                &mut parent,
                &mut class,
                &mut caps,
            )
        })?;
        Ok(HwNode {
            id,
            parent,
            class,
            caps,
        })
    }

    /// Every node, in order.
    pub fn nodes(&self) -> Result<Vec<HwNode>> {
        (0..self.node_count()?).map(|i| self.node(i)).collect()
    }

    /// Matches a target against a host.
    pub fn match_host(host: &HwProfile, target: &HwProfile) -> Result<HwMatch> {
        let (mut native, mut emulated) = (0u32, 0u32);
        check(unsafe {
            sys::zxv_hw_profile_match(host.ptr(), target.ptr(), &mut native, &mut emulated)
        })?;
        Ok(HwMatch { native, emulated })
    }

    /// The pairing of target node `index` (in target order) on `host`.
    pub fn pair(host: &HwProfile, target: &HwProfile, index: u32) -> Result<HwPair> {
        let (mut need, mut kind, mut have, mut unmet) = (0u32, 0i32, 0u32, 0u32);
        check(unsafe {
            sys::zxv_hw_profile_pair(
                host.ptr(),
                target.ptr(),
                index,
                &mut need,
                &mut kind,
                &mut have,
                &mut unmet,
            )
        })?;
        let pairing = match kind {
            sys::ZXV_HW_NATIVE => HwPairing::Native { host_id: have },
            sys::ZXV_HW_EMULATE => HwPairing::Emulate { unmet_key: unmet },
            sys::ZXV_HW_GROUP => HwPairing::Group,
            k => {
                return Err(Error::with_detail(
                    sys::ZXV_E_INTERNAL,
                    format!("unknown pairing kind {k}"),
                ))
            }
        };
        Ok(HwPair {
            need_id: need,
            pairing,
        })
    }
}

impl fmt::Debug for HwProfile {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("HwProfile")
            .field("nodes", &self.node_count().ok())
            .finish()
    }
}

impl Drop for HwProfile {
    fn drop(&mut self) {
        // SAFETY: owned handle, destroyed once.
        unsafe { sys::zxv_hw_profile_destroy(self.raw.as_ptr()) }
    }
}

// ----- the matrix ----------------------------------------------------------

/// What [`HwMatrix::function_info`] reports.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HwFunctionInfo {
    /// The current contract version.
    pub contract: u32,
    /// Its placement policy flags.
    pub policy: HwPolicy,
    /// Golden vectors of the current contract.
    pub vectors: u32,
}

/// What [`HwMatrix::provider_info`] reports about one implementation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HwProviderInfo {
    /// The function it implements.
    pub fn_id: u32,
    /// How it is built.
    pub kind: HwKind,
    /// Its proof: `True` (serves), `False` (a vector failed), `Neither`
    /// (no vectors).
    pub proof: Truth,
    /// The contract version it last proved.
    pub contract: u32,
    /// Status flags.
    pub flags: HwProviderFlags,
    /// Declared throughput class, 0 (slowest) .. 15.
    pub throughput_class: u32,
    /// Declared power class.
    pub power: HwPower,
    /// Calls served.
    pub calls: u64,
    /// Cost spent.
    pub cost_spent: u64,
}

/// The head of an audit log.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HwLog {
    /// The last record's chain hash.
    pub head: [u8; 32],
    /// Number of records.
    pub count: u32,
}

/// One audit record (regions and matrices).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HwLogEntry {
    /// What happened.
    pub event: HwEventKind,
    /// The failure code (0 for none).
    pub reason: u32,
    /// The region id (the function id for [`HwEventKind::Contract`]).
    pub id: u32,
    /// The image or contract version.
    pub version: u32,
    /// The hexagram tag (forbidden for rejections and quarantines).
    pub tag: Hexagram,
    /// SHA-256 of the package header.
    pub subject: [u8; 32],
    /// The record's chain hash.
    pub hash: [u8; 32],
}

type ProviderFn = dyn Fn(u32, &[u8], &mut [u8]) -> Result<usize> + Send + Sync;

struct ProviderSlot {
    f: Box<ProviderFn>,
}

unsafe extern "C" fn provider_trampoline(
    user: *mut c_void,
    op: u32,
    input: *const u8,
    in_len: usize,
    out: *mut u8,
    out_cap: usize,
    out_len: *mut usize,
) -> i32 {
    // SAFETY: `user` is a ProviderSlot owned by the live matrix (freed only
    // after zxv_hw_matrix_destroy).
    let slot = unsafe { &*(user as *const ProviderSlot) };
    let input: &[u8] = if input.is_null() || in_len == 0 {
        &[]
    } else {
        // SAFETY: the matrix passes `in_len` readable bytes for the call.
        unsafe { std::slice::from_raw_parts(input, in_len) }
    };
    let output: &mut [u8] = if out.is_null() || out_cap == 0 {
        &mut []
    } else {
        // SAFETY: the matrix passes `out_cap` writable bytes for the call.
        unsafe { std::slice::from_raw_parts_mut(out, out_cap) }
    };
    match catch_unwind(AssertUnwindSafe(|| (slot.f)(op, input, output))) {
        Ok(Ok(n)) if n <= out_cap => {
            if !out_len.is_null() {
                // SAFETY: valid out pointer from the matrix.
                unsafe { *out_len = n };
            }
            sys::ZXV_OK
        }
        Ok(Ok(_)) => sys::ZXV_E_BUFFER,
        Ok(Err(e)) => status_of(&e),
        Err(p) => panic_status(p),
    }
}

/// The emulation matrix: functions served by interchangeable, proven
/// implementations (needs [`Caps::HW_MATRIX`](crate::Caps::HW_MATRIX)).
///
/// A new matrix holds the [`RES_HOST`] resource and the standard engines,
/// each served by its proven SOFT implementation:
///
/// ```no_run
/// use zxv::{Caps, Context, HwMatrix};
/// let ctx = Context::new()?;
/// let cap = ctx.root_capability(1, None, Caps::ALL_1_4)?;
/// let mx = HwMatrix::new(&cap)?;
/// let digest = mx.call(zxv::hw::FN_SHA256, 0, b"abc")?;
/// assert_eq!(digest.len(), 32);
/// # Ok::<(), zxv::Error>(())
/// ```
///
/// Your own implementations are closures `(op, input, output) -> length`.
/// They run on the calling thread while the matrix is locked; a panic is
/// caught and becomes `ZXV_E_INTERNAL`, and an `Err(e)` becomes `e`'s
/// status. `Send + Sync`: the C matrix serializes calls.
pub struct HwMatrix<'a> {
    raw: NonNull<sys::zxv_hw_matrix>,
    cap: &'a Capability<'a>,
    // Boxed so each slot's address (the C `user` pointer) stays put when
    // the Vec grows.
    #[allow(clippy::vec_box)]
    providers: Mutex<Vec<Box<ProviderSlot>>>,
}

// SAFETY: the C matrix is protected by its own (recursive) mutex; provider
// closures are Send + Sync; the slot list is behind a Mutex.
unsafe impl Send for HwMatrix<'_> {}
// SAFETY: as above.
unsafe impl Sync for HwMatrix<'_> {}

impl<'a> HwMatrix<'a> {
    /// A matrix with the standard engines (needs `HW_MATRIX`).
    pub fn new(cap: &'a Capability<'a>) -> Result<HwMatrix<'a>> {
        let mut raw = std::ptr::null_mut();
        cap.call(|c| unsafe { sys::zxv_hw_matrix_create_cap(c, &mut raw) })?;
        let raw = NonNull::new(raw)
            .ok_or_else(|| Error::with_detail(sys::ZXV_E_INTERNAL, "NULL matrix"))?;
        Ok(HwMatrix {
            raw,
            cap,
            providers: Mutex::new(Vec::new()),
        })
    }

    fn call_mx(
        &self,
        f: impl FnOnce(*const sys::zxv_cap, *mut sys::zxv_hw_matrix) -> i32,
    ) -> Result<()> {
        let mx = self.raw.as_ptr();
        self.cap.call(|c| f(c, mx))
    }

    /// Adds a function (`name`: UTF-8, at most 23 bytes).
    pub fn add_function(&self, fn_id: u32, name: Option<&str>) -> Result<()> {
        let n = name.map(ffi::cstr).transpose()?;
        let np = n.as_ref().map_or(std::ptr::null(), |s| s.as_ptr());
        self.call_mx(|c, mx| unsafe { sys::zxv_hw_function_add(c, mx, fn_id, np) })
    }

    /// Adds a golden vector of the function's current contract (output at
    /// most 512 bytes). Every implementation of it becomes unproven.
    pub fn add_vector(&self, fn_id: u32, op: u32, input: &[u8], expect: &[u8]) -> Result<()> {
        self.call_mx(|c, mx| unsafe {
            sys::zxv_hw_vector_add(
                c,
                mx,
                fn_id,
                op,
                opt_ptr(input),
                input.len(),
                opt_ptr(expect),
                expect.len(),
            )
        })
    }

    /// Starts contract `version` (strictly newer, else `ZXV_E_ROLLBACK`):
    /// the vectors are cleared and every implementation must prove itself
    /// again.
    pub fn set_contract(&self, fn_id: u32, version: u32) -> Result<()> {
        self.call_mx(|c, mx| unsafe { sys::zxv_hw_function_contract(c, mx, fn_id, version) })
    }

    /// Placement: policy flags, a latency ceiling and a power budget (0:
    /// none). Clearing `KEY_BEARING` once set: `ZXV_E_POLICY`.
    pub fn set_policy(
        &self,
        fn_id: u32,
        flags: HwPolicy,
        max_latency_ns: u32,
        power_budget_mw: u32,
    ) -> Result<()> {
        self.call_mx(|c, mx| unsafe {
            sys::zxv_hw_policy_set(c, mx, fn_id, flags.bits(), max_latency_ns, power_budget_mw)
        })
    }

    /// Contract version, policy and vector count of a function.
    pub fn function_info(&self, fn_id: u32) -> Result<HwFunctionInfo> {
        let (mut contract, mut policy, mut vectors) = (0u32, 0u32, 0u32);
        self.call_mx(|c, mx| unsafe {
            sys::zxv_hw_function_info(c, mx, fn_id, &mut contract, &mut policy, &mut vectors)
        })?;
        Ok(HwFunctionInfo {
            contract,
            policy: HwPolicy::from_bits(policy),
            vectors,
        })
    }

    /// Adds a resource (a component implementations run on).
    pub fn add_resource(&self, res_id: u32) -> Result<()> {
        self.call_mx(|c, mx| unsafe { sys::zxv_hw_resource_add(c, mx, res_id) })
    }

    /// Sets a resource's state.
    pub fn set_resource(&self, res_id: u32, state: HwResourceState) -> Result<()> {
        self.call_mx(|c, mx| unsafe { sys::zxv_hw_resource_set(c, mx, res_id, state.raw()) })
    }

    /// Adds an implementation of `fn_id` on resource `res_id`; returns its
    /// index. It serves only after [`attest`](Self::attest) proves it.
    ///
    /// `f(op, input, output)` writes at most `output.len()` bytes and
    /// returns how many.
    pub fn add_provider(
        &self,
        fn_id: u32,
        res_id: u32,
        kind: HwKind,
        cost: u32,
        f: impl Fn(u32, &[u8], &mut [u8]) -> Result<usize> + Send + Sync + 'static,
    ) -> Result<u32> {
        let slot = Box::new(ProviderSlot { f: Box::new(f) });
        let user = &*slot as *const ProviderSlot as *mut c_void;
        let mut idx = 0u32;
        let mut slots = self.providers.lock().unwrap_or_else(|p| p.into_inner());
        self.call_mx(|c, mx| unsafe {
            sys::zxv_hw_provider_add(
                c,
                mx,
                fn_id,
                res_id,
                kind.raw(),
                cost,
                Some(provider_trampoline),
                user,
                &mut idx,
            )
        })?;
        slots.push(slot);
        Ok(idx)
    }

    /// Declares an implementation's traits: typical latency per call,
    /// power while serving, throughput class 0 (slowest) .. 15.
    pub fn set_traits(
        &self,
        provider: u32,
        latency_ns: u32,
        power_mw: u32,
        throughput_class: u32,
    ) -> Result<()> {
        self.call_mx(|c, mx| unsafe {
            sys::zxv_hw_provider_traits(c, mx, provider, latency_ns, power_mw, throughput_class)
        })
    }

    /// Declares that an implementation calls `dep_fn_id`.
    pub fn add_dependency(&self, provider: u32, dep_fn_id: u32) -> Result<()> {
        self.call_mx(|c, mx| unsafe { sys::zxv_hw_provider_dep(c, mx, provider, dep_fn_id) })
    }

    /// Enables or disables an implementation.
    pub fn enable(&self, provider: u32, on: bool) -> Result<()> {
        self.call_mx(|c, mx| unsafe { sys::zxv_hw_provider_enable(c, mx, provider, on as i32) })
    }

    /// Runs the golden vectors: `True` (serves), `False` (a vector
    /// failed) or `Neither` (no vectors).
    pub fn attest(&self, provider: u32) -> Result<Truth> {
        let mut lp = 0i32;
        self.call_mx(|c, mx| unsafe { sys::zxv_hw_attest(c, mx, provider, &mut lp) })?;
        Truth::from_raw(lp)
    }

    /// Number of implementations (indexed 0 .. count-1).
    pub fn provider_count(&self) -> Result<u32> {
        let mut n = 0u32;
        self.call_mx(|c, mx| unsafe { sys::zxv_hw_provider_count(c, mx, &mut n) })?;
        Ok(n)
    }

    /// Discovery: everything about implementation `provider`.
    pub fn provider_info(&self, provider: u32) -> Result<HwProviderInfo> {
        let (mut fn_id, mut kind, mut lp, mut contract, mut flags) = (0u32, 0i32, 0i32, 0u32, 0u32);
        let (mut tput, mut power, mut calls, mut spent) = (0u32, 0u32, 0u64, 0u64);
        self.call_mx(|c, mx| unsafe {
            sys::zxv_hw_provider_info(
                c,
                mx,
                provider,
                &mut fn_id,
                &mut kind,
                &mut lp,
                &mut contract,
                &mut flags,
                &mut tput,
                &mut power,
                &mut calls,
                &mut spent,
            )
        })?;
        Ok(HwProviderInfo {
            fn_id,
            kind: HwKind::from_raw(kind)?,
            proof: Truth::from_raw(lp)?,
            contract,
            flags: HwProviderFlags::from_bits(flags),
            throughput_class: tput,
            power: HwPower::from_raw(power)?,
            calls,
            cost_spent: spent,
        })
    }

    /// Every implementation, in index order.
    pub fn providers(&self) -> Result<Vec<HwProviderInfo>> {
        (0..self.provider_count()?)
            .map(|i| self.provider_info(i))
            .collect()
    }

    /// The index of the first implementation of `fn_id`, if any.
    pub fn find_provider(&self, fn_id: u32) -> Result<Option<u32>> {
        for i in 0..self.provider_count()? {
            if self.provider_info(i)?.fn_id == fn_id {
                return Ok(Some(i));
            }
        }
        Ok(None)
    }

    /// The implementation serving `fn_id` now (`ZXV_E_NOPROVIDER` if none).
    pub fn bound(&self, fn_id: u32) -> Result<u32> {
        let mut p = 0u32;
        self.call_mx(|c, mx| unsafe { sys::zxv_hw_bound(c, mx, fn_id, &mut p) })?;
        Ok(p)
    }

    /// Calls `fn_id` with an output of at most [`CALL_OUT_DEFAULT`] bytes.
    pub fn call(&self, fn_id: u32, op: u32, input: &[u8]) -> Result<Vec<u8>> {
        let mut out = vec![0u8; CALL_OUT_DEFAULT];
        let n = self.call_into(fn_id, op, input, &mut out)?;
        out.truncate(n);
        Ok(out)
    }

    /// Calls `fn_id`, writing into `out`; returns the output length
    /// (`ZXV_E_BUFFER` if it does not fit).
    pub fn call_into(&self, fn_id: u32, op: u32, input: &[u8], out: &mut [u8]) -> Result<usize> {
        let mut n = 0usize;
        let (op_, cap_) = (out.as_mut_ptr(), out.len());
        self.call_mx(|c, mx| unsafe {
            sys::zxv_hw_call(
                c,
                mx,
                fn_id,
                op,
                opt_ptr(input),
                input.len(),
                op_,
                cap_,
                &mut n,
            )
        })?;
        Ok(n)
    }

    /// [`call`](Self::call) that also runs the next-best implementation
    /// and compares; returns the output and whether a second one ran. On a
    /// mismatch both stop serving: `ZXV_E_DISPUTE`.
    pub fn shadow(&self, fn_id: u32, op: u32, input: &[u8]) -> Result<(Vec<u8>, bool)> {
        let mut out = vec![0u8; CALL_OUT_DEFAULT];
        let (mut n, mut checked) = (0usize, 0i32);
        let (op_, cap_) = (out.as_mut_ptr(), out.len());
        self.call_mx(|c, mx| unsafe {
            sys::zxv_hw_shadow(
                c,
                mx,
                fn_id,
                op,
                opt_ptr(input),
                input.len(),
                op_,
                cap_,
                &mut n,
                &mut checked,
            )
        })?;
        out.truncate(n);
        Ok((out, checked != 0))
    }

    /// Declares `fn_id` pure with a memo of `entries` (a power of two, at
    /// most 65536; 0 removes it); returns (hits, misses). Key-bearing
    /// functions: `ZXV_E_POLICY`.
    pub fn memo(&self, fn_id: u32, entries: u32) -> Result<(u64, u64)> {
        let (mut hits, mut misses) = (0u64, 0u64);
        self.call_mx(|c, mx| unsafe {
            sys::zxv_hw_memo(c, mx, fn_id, entries, &mut hits, &mut misses)
        })?;
        Ok((hits, misses))
    }

    /// Trusts a release key ([`PK_BYTES`]; at most 4) for contract packages
    /// (needs `HW_RECONF`).
    pub fn trust_key(&self, pk: &[u8]) -> Result<()> {
        need_len(pk, PK_BYTES, "release public key")?;
        self.call_mx(|c, mx| unsafe { sys::zxv_hw_matrix_trust_key(c, mx, pk.as_ptr()) })
    }

    /// Applies a signed [`HwImageKind::Vectors`] package: verifies both
    /// signatures, requires a newer version and svn >= the floor
    /// (`ZXV_E_ROLLBACK`), installs the vectors, proves every
    /// implementation again and records it. Returns how many
    /// implementations prove the new contract (needs `HW_RECONF`).
    pub fn apply_contract(&self, pkg: &[u8]) -> Result<u32> {
        let mut proven = 0u32;
        self.call_mx(|c, mx| unsafe {
            sys::zxv_hw_contract_apply(c, mx, opt_ptr(pkg), pkg.len(), &mut proven)
        })?;
        Ok(proven)
    }

    /// The matrix audit log's head and length (`ZXV_E_INVARIANT`, FATAL,
    /// if the chain does not verify).
    pub fn log(&self) -> Result<HwLog> {
        let (mut head, mut count) = ([0u8; 32], 0u32);
        self.call_mx(|c, mx| unsafe {
            sys::zxv_hw_matrix_log(c, mx, head.as_mut_ptr(), &mut count)
        })?;
        Ok(HwLog { head, count })
    }

    /// Audit record `index` (0 = oldest).
    pub fn log_entry(&self, index: u32) -> Result<HwLogEntry> {
        read_entry(|ev, reason, id, ver, tag, subj, hash| {
            self.call_mx(|c, mx| unsafe {
                sys::zxv_hw_matrix_log_entry(c, mx, index, ev, reason, id, ver, tag, subj, hash)
            })
        })
    }

    /// Every audit record, oldest first.
    pub fn log_entries(&self) -> Result<Vec<HwLogEntry>> {
        (0..self.log()?.count).map(|i| self.log_entry(i)).collect()
    }

    /// The raw handle, for calls this crate does not wrap. It stays owned
    /// by this `HwMatrix`.
    pub fn as_raw(&self) -> *mut sys::zxv_hw_matrix {
        self.raw.as_ptr()
    }
}

#[allow(clippy::type_complexity)]
fn read_entry(
    f: impl FnOnce(*mut i32, *mut u32, *mut u32, *mut u32, *mut u32, *mut u8, *mut u8) -> Result<()>,
) -> Result<HwLogEntry> {
    let (mut ev, mut reason, mut id, mut ver, mut tag) = (0i32, 0u32, 0u32, 0u32, 0u32);
    let (mut subject, mut hash) = ([0u8; 32], [0u8; 32]);
    f(
        &mut ev,
        &mut reason,
        &mut id,
        &mut ver,
        &mut tag,
        subject.as_mut_ptr(),
        hash.as_mut_ptr(),
    )?;
    let tag = u8::try_from(tag)
        .map_err(|_| Error::with_detail(sys::ZXV_E_INTERNAL, "audit tag out of range"))?;
    Ok(HwLogEntry {
        event: HwEventKind::from_raw(ev)?,
        reason,
        id,
        version: ver,
        tag: Hexagram::from_tag(tag)?,
        subject,
        hash,
    })
}

impl fmt::Debug for HwMatrix<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("HwMatrix")
            .field("providers", &self.provider_count().ok())
            .finish()
    }
}

impl Drop for HwMatrix<'_> {
    fn drop(&mut self) {
        // SAFETY: regions borrow the matrix, so all were dropped first; the
        // provider slots are freed after this, when the fields drop.
        unsafe { sys::zxv_hw_matrix_destroy(self.raw.as_ptr()) }
    }
}

// ----- release keys and packages -------------------------------------------

/// A release key pair: the public key ([`PK_BYTES`]) and both secret
/// halves, wiped with `zxv_secure_zero` on drop. `Debug` redacts them.
pub struct HwReleaseKey {
    public: Vec<u8>,
    mldsa: SecretKey,
    slhdsa: SecretKey,
}

impl HwReleaseKey {
    /// Assembles a key from stored parts (lengths checked).
    pub fn from_parts(public: &[u8], mldsa: SecretKey, slhdsa: SecretKey) -> Result<HwReleaseKey> {
        need_len(public, PK_BYTES, "release public key")?;
        mldsa.require_len(MLDSA_SK_BYTES, "ML-DSA secret key")?;
        slhdsa.require_len(SLHDSA_SK_BYTES, "SLH-DSA secret key")?;
        Ok(HwReleaseKey {
            public: public.to_vec(),
            mldsa,
            slhdsa,
        })
    }

    /// The public key: the ML-DSA-87 key followed by the SLH-DSA one.
    pub fn public(&self) -> &[u8] {
        &self.public
    }

    /// The ML-DSA-87 secret half.
    pub fn mldsa_secret(&self) -> &SecretKey {
        &self.mldsa
    }

    /// The SLH-DSA-SHAKE-256s secret half.
    pub fn slhdsa_secret(&self) -> &SecretKey {
        &self.slhdsa
    }
}

impl fmt::Debug for HwReleaseKey {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "HwReleaseKey(public {} bytes, secrets redacted)",
            self.public.len()
        )
    }
}

/// The header fields of a package ([`HwRelease::build`]).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HwImageSpec {
    /// The package kind.
    pub kind: HwImageKind,
    /// The hardware class the image is for (0 for a contract package).
    pub target_class: u32,
    /// The target profile digest (`None`: zeros).
    pub target_digest: Option<[u8; 32]>,
    /// For an image the function the region will serve; for
    /// [`HwImageKind::Vectors`] the function whose contract it replaces.
    pub fn_id: u32,
    /// The image or contract version.
    pub version: u32,
    /// The anti-rollback floor.
    pub svn: u32,
}

/// Release keys and signed packages (needs
/// [`Caps::CRYPTO_SIGN`](crate::Caps::CRYPTO_SIGN)). SLH-DSA signing takes
/// seconds: building is a release step, not a hot path.
#[derive(Debug, Clone, Copy)]
pub struct HwRelease<'a> {
    cap: &'a Capability<'a>,
}

impl<'a> HwRelease<'a> {
    /// Packages through `cap`.
    pub fn new(cap: &'a Capability<'a>) -> HwRelease<'a> {
        HwRelease { cap }
    }

    /// A fresh release key pair.
    pub fn keygen(&self) -> Result<HwReleaseKey> {
        let mut public = vec![0u8; PK_BYTES];
        let mut mldsa = SecretKey::zeroed(MLDSA_SK_BYTES);
        let mut slhdsa = SecretKey::zeroed(SLHDSA_SK_BYTES);
        self.cap.call(|c| unsafe {
            sys::zxv_hw_keygen(
                c,
                public.as_mut_ptr(),
                mldsa.as_mut_ptr(),
                slhdsa.as_mut_ptr(),
            )
        })?;
        Ok(HwReleaseKey {
            public,
            mldsa,
            slhdsa,
        })
    }

    /// The length of a package with a `payload_len`-byte payload.
    pub fn package_len(payload_len: usize) -> usize {
        HEADER_BYTES + payload_len + SIG_BYTES
    }

    /// Builds and signs a package: header || payload || signatures.
    pub fn build(&self, spec: &HwImageSpec, payload: &[u8], key: &HwReleaseKey) -> Result<Vec<u8>> {
        let mut buf = vec![0u8; HwRelease::package_len(payload.len())];
        let mut len = 0usize;
        let td = spec.target_digest;
        let tdp = td.as_ref().map_or(std::ptr::null(), |d| d.as_ptr());
        let (bp, bc) = (buf.as_mut_ptr(), buf.len());
        self.cap.call(|c| unsafe {
            sys::zxv_hw_image_build(
                c,
                spec.kind.raw(),
                spec.target_class,
                tdp,
                spec.fn_id,
                spec.version,
                spec.svn,
                opt_ptr(payload),
                payload.len(),
                key.mldsa.as_ptr(),
                key.slhdsa.as_ptr(),
                bp,
                bc,
                &mut len,
            )
        })?;
        buf.truncate(len);
        Ok(buf)
    }
}

// ----- regions -------------------------------------------------------------

/// The device side of a [`HwRegion`]: a two-slot reconfigurable unit.
///
/// Each method runs on the thread of the region call that needs it, with
/// the region (and its matrix) locked. A panic is caught and reported to
/// the library as `ZXV_E_INTERNAL`; an `Err(e)` as `e`'s status.
pub trait HwDevice: Send + Sync {
    /// Loads `payload` into `slot`.
    fn load(&self, slot: u32, payload: &[u8]) -> Result<()>;
    /// The SHA-256 of what `slot` really holds.
    fn readback(&self, slot: u32) -> Result<[u8; 32]>;
    /// Switches the unit to `slot`.
    fn activate(&self, slot: u32) -> Result<()>;
}

impl<T: HwDevice + ?Sized> HwDevice for Arc<T> {
    fn load(&self, slot: u32, payload: &[u8]) -> Result<()> {
        (**self).load(slot, payload)
    }
    fn readback(&self, slot: u32) -> Result<[u8; 32]> {
        (**self).readback(slot)
    }
    fn activate(&self, slot: u32) -> Result<()> {
        (**self).activate(slot)
    }
}

impl<T: HwDevice + ?Sized> HwDevice for Box<T> {
    fn load(&self, slot: u32, payload: &[u8]) -> Result<()> {
        (**self).load(slot, payload)
    }
    fn readback(&self, slot: u32) -> Result<[u8; 32]> {
        (**self).readback(slot)
    }
    fn activate(&self, slot: u32) -> Result<()> {
        (**self).activate(slot)
    }
}

/// One region event.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HwEvent {
    /// The region.
    pub region_id: u32,
    /// What happened.
    pub kind: HwEventKind,
    /// The failure code (0 for none).
    pub reason: u32,
    /// The image version.
    pub version: u32,
}

/// What [`HwRegion::info`] reports.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HwRegionInfo {
    /// The region state.
    pub state: HwRegionState,
    /// The active image version.
    pub version: u32,
    /// The anti-rollback floor.
    pub floor: u32,
    /// Images quarantined or refused so far.
    pub quarantined: u32,
}

type EventFn = dyn Fn(HwEvent) + Send + Sync;

#[derive(Default)]
struct EventShared {
    callbacks: Mutex<Vec<Arc<EventFn>>>,
}

type DeviceBox<'a> = Box<dyn HwDevice + 'a>;

fn device_call(user: *mut c_void, f: impl FnOnce(&dyn HwDevice) -> Result<()>) -> i32 {
    // SAFETY: `user` is the DeviceBox owned by the live region (freed only
    // after zxv_hw_region_destroy).
    let dev = unsafe { &*(user as *const DeviceBox<'_>) };
    match catch_unwind(AssertUnwindSafe(|| f(&**dev))) {
        Ok(Ok(())) => sys::ZXV_OK,
        Ok(Err(e)) => status_of(&e),
        Err(p) => panic_status(p),
    }
}

unsafe extern "C" fn dev_load(user: *mut c_void, slot: u32, payload: *const u8, len: usize) -> i32 {
    let p: &[u8] = if payload.is_null() || len == 0 {
        &[]
    } else {
        // SAFETY: the region passes `len` readable bytes for the call.
        unsafe { std::slice::from_raw_parts(payload, len) }
    };
    device_call(user, |d| d.load(slot, p))
}

unsafe extern "C" fn dev_readback(user: *mut c_void, slot: u32, digest: *mut u8) -> i32 {
    device_call(user, |d| {
        let h = d.readback(slot)?;
        // SAFETY: the region passes a 32-byte output.
        unsafe { std::ptr::copy_nonoverlapping(h.as_ptr(), digest, 32) };
        Ok(())
    })
}

unsafe extern "C" fn dev_activate(user: *mut c_void, slot: u32) -> i32 {
    device_call(user, |d| d.activate(slot))
}

unsafe extern "C" fn on_region_event(
    user: *mut c_void,
    region_id: u32,
    event: i32,
    reason: u32,
    version: u32,
) {
    // SAFETY: `user` is the EventShared owned by the live region.
    let shared = unsafe { &*(user as *const EventShared) };
    let _ = catch_unwind(AssertUnwindSafe(|| {
        let Ok(kind) = HwEventKind::from_raw(event) else {
            return;
        };
        let ev = HwEvent {
            region_id,
            kind,
            reason,
            version,
        };
        let cbs: Vec<Arc<EventFn>> = shared
            .callbacks
            .lock()
            .unwrap_or_else(|p| p.into_inner())
            .clone();
        for cb in cbs {
            cb(ev);
        }
    }));
}

/// A reconfigurable region: hardware updates shipped as signed images
/// (needs [`Caps::HW_RECONF`](crate::Caps::HW_RECONF)).
///
/// [`stage`](Self::stage) verifies an image (ML-DSA-87 and SLH-DSA, both
/// required) and loads it into the inactive slot, [`trial`](Self::trial)
/// switches to it and proves it against the golden vectors of the
/// matrix implementation it serves, [`commit`](Self::commit) makes it
/// permanent. Every step is an [`HwEvent`] and a record in a hash-chained
/// audit log; a failing image is rolled back and quarantined forever. When
/// the log fills, the region halts and the context is poisoned (fail
/// closed).
///
/// `Send + Sync`: the C region serializes calls.
pub struct HwRegion<'a> {
    raw: NonNull<sys::zxv_hw_region>,
    cap: &'a Capability<'a>,
    _matrix: Option<&'a HwMatrix<'a>>,
    _device: Box<DeviceBox<'a>>,
    events: Box<EventShared>,
}

// SAFETY: the C region has its own mutex; the device is Send + Sync and the
// event list is behind a Mutex.
unsafe impl Send for HwRegion<'_> {}
// SAFETY: as above.
unsafe impl Sync for HwRegion<'_> {}

impl<'a> HwRegion<'a> {
    /// A region `region_id` for hardware class `target_class`.
    ///
    /// `serving` is the matrix and the FABRIC or MICRO implementation
    /// index whose golden vectors test each new image (`None`: no test).
    /// `log_cap` is the audit log size (1 .. 2^20).
    pub fn new(
        cap: &'a Capability<'a>,
        serving: Option<(&'a HwMatrix<'a>, u32)>,
        region_id: u32,
        target_class: u32,
        target_digest: Option<&[u8; 32]>,
        device: impl HwDevice + 'a,
        log_cap: u32,
    ) -> Result<HwRegion<'a>> {
        let device: Box<DeviceBox<'a>> = Box::new(Box::new(device));
        let user = &*device as *const DeviceBox<'a> as *mut c_void;
        let (mx, provider) = serving.map_or((std::ptr::null_mut(), 0), |(m, p)| (m.as_raw(), p));
        let td = target_digest.map_or(std::ptr::null(), |d| d.as_ptr());
        let mut raw = std::ptr::null_mut();
        cap.call(|c| unsafe {
            sys::zxv_hw_region_create_cap(
                c,
                mx,
                provider,
                region_id,
                target_class,
                td,
                Some(dev_load),
                Some(dev_readback),
                Some(dev_activate),
                user,
                log_cap,
                &mut raw,
            )
        })?;
        let raw = NonNull::new(raw)
            .ok_or_else(|| Error::with_detail(sys::ZXV_E_INTERNAL, "NULL region"))?;
        let events = Box::<EventShared>::default();
        let eu = &*events as *const EventShared as *mut c_void;
        if let Err(e) = cap.call(|c| unsafe {
            sys::zxv_hw_region_events(c, raw.as_ptr(), Some(on_region_event), eu)
        }) {
            // SAFETY: owned handle, never shared.
            unsafe { sys::zxv_hw_region_destroy(raw.as_ptr()) };
            return Err(e);
        }
        Ok(HwRegion {
            raw,
            cap,
            _matrix: serving.map(|(m, _)| m),
            _device: device,
            events,
        })
    }

    fn call_rg(
        &self,
        f: impl FnOnce(*const sys::zxv_cap, *mut sys::zxv_hw_region) -> i32,
    ) -> Result<()> {
        let rg = self.raw.as_ptr();
        self.cap.call(|c| f(c, rg))
    }

    /// Registers a callback for every event, delivered after each recorded
    /// step once the region call has released its locks, in order. It runs
    /// on the thread of that call; it must not register further callbacks.
    pub fn on_event(&self, f: impl Fn(HwEvent) + Send + Sync + 'static) {
        self.events
            .callbacks
            .lock()
            .unwrap_or_else(|p| p.into_inner())
            .push(Arc::new(f));
    }

    /// Trusts a release key ([`PK_BYTES`]; at most 4 per region).
    pub fn trust_key(&self, pk: &[u8]) -> Result<()> {
        need_len(pk, PK_BYTES, "release public key")?;
        self.call_rg(|c, rg| unsafe { sys::zxv_hw_region_trust_key(c, rg, pk.as_ptr()) })
    }

    /// Verifies an image and loads it into the inactive slot. Refusals
    /// (`ZXV_E_VERIFY`, `ZXV_E_ROLLBACK`, `ZXV_E_TARGET`) are recorded; a
    /// read-back mismatch is `ZXV_E_QUARANTINED` and blocks the image.
    pub fn stage(&self, image: &[u8]) -> Result<()> {
        self.call_rg(|c, rg| unsafe {
            sys::zxv_hw_region_stage(c, rg, opt_ptr(image), image.len())
        })
    }

    /// Switches to the staged image and proves it; passing starts a
    /// probation of `ticks`. A failure is `ZXV_E_QUARANTINED` with the
    /// previous image (or S0) back in place.
    pub fn trial(&self, ticks: u32) -> Result<()> {
        self.call_rg(|c, rg| unsafe { sys::zxv_hw_region_trial(c, rg, ticks) })
    }

    /// Makes the tested image permanent and raises the floor.
    pub fn commit(&self) -> Result<()> {
        self.call_rg(|c, rg| unsafe { sys::zxv_hw_region_commit(c, rg) })
    }

    /// Returns to the previous image (or S0).
    pub fn rollback(&self) -> Result<()> {
        self.call_rg(|c, rg| unsafe { sys::zxv_hw_region_rollback(c, rg) })
    }

    /// One health tick; an expiring probation rolls back
    /// (`ZXV_E_QUARANTINED`).
    pub fn tick(&self) -> Result<()> {
        self.call_rg(|c, rg| unsafe { sys::zxv_hw_region_tick(c, rg) })
    }

    /// State, active version, floor and quarantine count.
    pub fn info(&self) -> Result<HwRegionInfo> {
        let (mut st, mut ver, mut floor, mut nq) = (0i32, 0u32, 0u32, 0u32);
        self.call_rg(|c, rg| unsafe {
            sys::zxv_hw_region_info(c, rg, &mut st, &mut ver, &mut floor, &mut nq)
        })?;
        Ok(HwRegionInfo {
            state: HwRegionState::from_raw(st)?,
            version: ver,
            floor,
            quarantined: nq,
        })
    }

    /// The audit log's head and length (`ZXV_E_INVARIANT`, FATAL, if the
    /// chain does not verify).
    pub fn log(&self) -> Result<HwLog> {
        let (mut head, mut count) = ([0u8; 32], 0u32);
        self.call_rg(|c, rg| unsafe {
            sys::zxv_hw_region_log(c, rg, head.as_mut_ptr(), &mut count)
        })?;
        Ok(HwLog { head, count })
    }

    /// Audit record `index` (0 = oldest).
    pub fn log_entry(&self, index: u32) -> Result<HwLogEntry> {
        read_entry(|ev, reason, id, ver, tag, subj, hash| {
            self.call_rg(|c, rg| unsafe {
                sys::zxv_hw_region_log_entry(c, rg, index, ev, reason, id, ver, tag, subj, hash)
            })
        })
    }

    /// Every audit record, oldest first.
    pub fn log_entries(&self) -> Result<Vec<HwLogEntry>> {
        (0..self.log()?.count).map(|i| self.log_entry(i)).collect()
    }
}

impl fmt::Debug for HwRegion<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("HwRegion")
            .field("info", &self.info().ok())
            .finish()
    }
}

impl Drop for HwRegion<'_> {
    fn drop(&mut self) {
        // SAFETY: owned handle destroyed once, before its matrix and context;
        // the device and event boxes are freed after this.
        unsafe { sys::zxv_hw_region_destroy(self.raw.as_ptr()) }
    }
}
