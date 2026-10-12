// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Content ids and AIPI (ABI 1.5): IPFS CIDs and content-addressed
//! functions.
//!
//! AIPI names data and functions by WHAT they are (a hash) instead of WHERE
//! they live. A [`Cid`] is a `Copy` value holding the binary CID (version,
//! codec, multihash); every function that takes one refuses bytes that are
//! not exactly one canonical CIDv0 or CIDv1 with `ZXV_E_CID`. The CIDs are
//! byte for byte the ones Kubo makes: [`Cid::compute`] is `ipfs block put`,
//! [`Cid::unixfs`] is `ipfs add --cid-version=1`, and `Display` prints what
//! Kubo prints.
//!
//! The four AIPI identifiers (all CIDv1, full sha2-256; the `0x3A10xx`
//! codecs are multicodec PRIVATE-USE values, not registered): data (raw
//! `0x55` or dag-pb `0x70`), function ([`MC_FN`] + intent, the hash of an
//! [`AipiFunction`] record), call ([`MC_CALL`], the hash of a call record)
//! and claim (not a CID: an executor's answer, [`AipiClaim`]).
//!
//! **Honest limits:**
//! * No network transport: no Vinea mesh, no IPFS peers, no HTTP gateway,
//!   no libp2p. An [`AipiNode`] is an OFFLINE, in-process node:
//!   [`get`](AipiNode::get) finds only what was put into this node.
//! * No EXECUTE: running a function needs the Z-Wasm sandbox
//!   (`sdk/kits/aipi`), which is not part of libzxv. [`AipiNode::plan`]
//!   reports the route a call would take here; it runs nothing.
//! * No money moves in this group.
//! * A CID proves which bytes are meant. It does not prove that a claimed
//!   output of a function is right, and it does not make bytes available.
//! * Claims are the bytes an executor signs; signing and checking the
//!   signature are the caller's job.
//!
//! ```
//! use zxv::aipi::{Cid, CidBase};
//! let c = Cid::compute(zxv::aipi::CODEC_RAW, b"hello")?;
//! assert!(c.verify(b"hello").is_ok());
//! assert!(c.verify(b"hullo").is_err());
//! let text = c.text(CidBase::Default)?;
//! assert_eq!(Cid::parse(&text)?, c);
//! # Ok::<(), zxv::Error>(())
//! ```

use std::fmt;
use std::ptr::NonNull;

use zxv_sys as sys;

use crate::capability::Capability;
use crate::error::{check, Error, Result};
use crate::ffi::{cstr, local, local_with, non_null, opt_ptr, read_bytes, read_string};

/// Longest binary CID.
pub const CID_MAX: usize = sys::ZXV_CID_MAX as usize;
/// Longest CID text.
pub const CID_TEXT_MAX: usize = sys::ZXV_CID_TEXT_MAX as usize;
/// Longest multihash digest (identity).
pub const CID_DIGEST_MAX: usize = sys::ZXV_CID_DIGEST_MAX as usize;
/// Multicodec raw: plain bytes.
pub const CODEC_RAW: u32 = sys::ZXV_CID_CODEC_RAW;
/// Multicodec dag-pb: UnixFS nodes.
pub const CODEC_DAG_PB: u32 = sys::ZXV_CID_CODEC_DAG_PB;
/// Multihash identity: the bytes themselves.
pub const HASH_IDENTITY: u32 = sys::ZXV_CID_HASH_IDENTITY;
/// Multihash sha2-256.
pub const HASH_SHA2_256: u32 = sys::ZXV_CID_HASH_SHA2_256;
/// zxv-fn (intent 0).
pub const MC_FN: u32 = sys::ZXV_AIPI_MC_FN;
/// zxv-tensor-infer (intent 1).
pub const MC_TENSOR_INFER: u32 = sys::ZXV_AIPI_MC_TENSOR_INFER;
/// zxv-settle-ledger (intent 2).
pub const MC_SETTLE_LEDGER: u32 = sys::ZXV_AIPI_MC_SETTLE_LEDGER;
/// zxv-swarm-route (intent 3).
pub const MC_SWARM_ROUTE: u32 = sys::ZXV_AIPI_MC_SWARM_ROUTE;
/// zxv-aipi-call.
pub const MC_CALL: u32 = sys::ZXV_AIPI_MC_CALL;
/// Export names: `[A-Za-z0-9_]{1,63}`.
pub const NAME_MAX: usize = sys::ZXV_AIPI_NAME_MAX as usize;
/// Longest call input or claim output.
pub const IO_MAX: usize = sys::ZXV_AIPI_IO_MAX as usize;
/// Longest claim certificate.
pub const CERT_MAX: usize = sys::ZXV_AIPI_CERT_MAX as usize;
/// Largest QUORUM n.
pub const QUORUM_MAX: u32 = sys::ZXV_AIPI_QUORUM_MAX;
/// Longest function record.
pub const FN_MAX: usize = sys::ZXV_AIPI_FN_MAX as usize;
/// Longest call record.
pub const CALL_MAX: usize = sys::ZXV_AIPI_CALL_MAX as usize;
/// Longest claim record.
pub const CLAIM_MAX: usize = sys::ZXV_AIPI_CLAIM_MAX as usize;
/// Largest blob a node stores (2 MiB).
pub const BLOCK_MAX: usize = sys::ZXV_AIPI_BLOCK_MAX as usize;
/// Smallest node store, in bytes.
pub const STORE_MIN: u64 = sys::ZXV_AIPI_STORE_MIN as u64;
/// Largest node store, in bytes (256 MiB).
pub const STORE_MAX: u64 = sys::ZXV_AIPI_STORE_MAX as u64;
/// Most blocks a node indexes.
pub const BLOCKS_MAX: u32 = sys::ZXV_AIPI_BLOCKS_MAX;

/// A text form of a CID.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum CidBase {
    /// Kubo's form: CIDv1 `"b..."` base32, CIDv0 `"Qm..."`.
    Default,
    /// `"z"` + base58btc (the AIPI form); CIDv1 only.
    Base58Btc,
}

impl CidBase {
    fn raw(self) -> u32 {
        match self {
            CidBase::Default => sys::ZXV_CID_BASE_DEFAULT,
            CidBase::Base58Btc => sys::ZXV_CID_BASE_58BTC,
        }
    }
}

/// One binary content id: a `Copy`, comparable value. The default value
/// is "no CID"; every function given one refuses it with `ZXV_E_CID`.
#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct Cid {
    n: u8,
    b: [u8; CID_MAX],
}

impl Default for Cid {
    fn default() -> Cid {
        Cid {
            n: 0,
            b: [0; CID_MAX],
        }
    }
}

/// The parts of a CID ([`Cid::info`]).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CidInfo {
    /// 0 or 1.
    pub version: u32,
    /// The multicodec, e.g. [`CODEC_RAW`].
    pub codec: u32,
    /// The multihash code, e.g. [`HASH_SHA2_256`].
    pub hash: u32,
    /// The multihash digest.
    pub digest: Vec<u8>,
}

/// Runs a pure call that writes a CID.
fn out_cid(f: impl FnOnce(*mut u8, *mut usize) -> i32) -> Result<Cid> {
    let mut c = Cid::default();
    let mut n = 0usize;
    check(f(c.b.as_mut_ptr(), &mut n))?;
    c.n = n.min(CID_MAX) as u8;
    Ok(c)
}

impl Cid {
    fn ptr(&self) -> *const u8 {
        opt_ptr(self.as_bytes())
    }

    /// Checks that `b` is exactly one canonical binary CID and returns it
    /// (`ZXV_E_CID` otherwise).
    pub fn from_bytes(b: &[u8]) -> Result<Cid> {
        if b.is_empty() || b.len() > CID_MAX {
            return Err(Error::with_detail(
                sys::ZXV_E_CID,
                "not a binary CID (length)",
            ));
        }
        let null = std::ptr::null_mut();
        // SAFETY: b valid for b.len() bytes; NULL outs are allowed.
        check(unsafe {
            sys::zxv_cid_info(
                b.as_ptr(),
                b.len(),
                null,
                null,
                null,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
            )
        })?;
        let mut c = Cid::default();
        c.b[..b.len()].copy_from_slice(b);
        c.n = b.len() as u8;
        Ok(c)
    }

    /// The CIDv1 of `data` as one block: sha2-256 under multicodec `codec`
    /// ([`CODEC_RAW`] for plain bytes; Kubo `ipfs block put`).
    pub fn compute(codec: u32, data: &[u8]) -> Result<Cid> {
        // SAFETY: data valid; out buffer has CID_MAX bytes.
        out_cid(|o, n| unsafe { sys::zxv_cid_compute(codec, opt_ptr(data), data.len(), o, n) })
    }

    /// The root CID Kubo gives `data` as a file (`ipfs add
    /// --cid-version=1`): a raw CID up to 262144 bytes, a dag-pb root
    /// above. The blocks themselves are not kept.
    pub fn unixfs(data: &[u8]) -> Result<Cid> {
        // SAFETY: data valid; out buffer has CID_MAX bytes.
        out_cid(|o, n| unsafe { sys::zxv_cid_unixfs(opt_ptr(data), data.len(), o, n) })
    }

    /// Parses `"b..."` (CIDv1 base32), `"Qm..."` (CIDv0) or `"z..."` (CIDv1
    /// base58btc). Only the canonical text is accepted.
    pub fn parse(text: &str) -> Result<Cid> {
        let cs = cstr(text)?;
        // SAFETY: NUL-terminated text; out buffer has CID_MAX bytes.
        out_cid(|o, n| unsafe { sys::zxv_cid_from_text(cs.as_ptr(), o, n) })
    }

    /// The binary CID.
    pub fn as_bytes(&self) -> &[u8] {
        &self.b[..self.n as usize]
    }

    /// The binary length (0 for the default value).
    pub fn len(&self) -> usize {
        self.n as usize
    }

    /// True for the default value (no CID).
    pub fn is_empty(&self) -> bool {
        self.n == 0
    }

    /// The CID as text in `base`.
    pub fn text(&self, base: CidBase) -> Result<String> {
        // SAFETY: CID bytes valid; R4 buffer.
        read_string(|b, c, l| unsafe {
            sys::zxv_cid_to_text(self.ptr(), self.len(), base.raw(), b, c, l)
        })
    }

    /// The version, codec, hash code and digest.
    pub fn info(&self) -> Result<CidInfo> {
        let (mut v, mut codec, mut hash) = (0u32, 0u32, 0u32);
        let mut d = [0u8; CID_DIGEST_MAX];
        let mut dl = 0usize;
        // SAFETY: CID bytes valid; digest buffer has CID_DIGEST_MAX bytes.
        check(unsafe {
            sys::zxv_cid_info(
                self.ptr(),
                self.len(),
                &mut v,
                &mut codec,
                &mut hash,
                d.as_mut_ptr(),
                &mut dl,
            )
        })?;
        Ok(CidInfo {
            version: v,
            codec,
            hash,
            digest: d[..dl.min(CID_DIGEST_MAX)].to_vec(),
        })
    }

    /// The CIDv1 form (dag-pb, same hash) of a CIDv0; a CIDv1 is returned
    /// unchanged.
    pub fn to_v1(&self) -> Result<Cid> {
        // SAFETY: CID bytes valid; out buffer has CID_MAX bytes.
        out_cid(|o, n| unsafe { sys::zxv_cid_to_v1(self.ptr(), self.len(), o, n) })
    }

    /// `Ok(())` if `data` is exactly the block this CID names (re-hashed;
    /// identity CIDs compare the bytes), else `ZXV_E_CID`. A UnixFS root
    /// names its root block, not the file: check a file with
    /// [`Cid::unixfs`] instead.
    pub fn verify(&self, data: &[u8]) -> Result<()> {
        // SAFETY: both buffers valid for their lengths.
        check(unsafe { sys::zxv_cid_verify(self.ptr(), self.len(), opt_ptr(data), data.len()) })
    }

    /// The Kademlia key under which providers of this CID are announced on
    /// Vinea: SHA-256(`"zxv/aipi/v1/provider"` || binary CID).
    pub fn provider_key(&self) -> Result<[u8; 32]> {
        let mut k = [0u8; 32];
        // SAFETY: CID bytes valid; out has 32 bytes.
        check(unsafe { sys::zxv_aipi_provider_key(self.ptr(), self.len(), k.as_mut_ptr()) })?;
        Ok(k)
    }
}

impl fmt::Display for Cid {
    /// Kubo's text form (`"bafy..."`, `"Qm..."`), or a hex dump in angle
    /// brackets when this is not a valid CID.
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self.text(CidBase::Default) {
            Ok(s) => f.pad(&s),
            Err(_) => write!(f, "<cid {}>", crate::ffi::hex(self.as_bytes())),
        }
    }
}

impl fmt::Debug for Cid {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Cid({self})")
    }
}

impl std::str::FromStr for Cid {
    type Err = Error;
    fn from_str(s: &str) -> Result<Cid> {
        Cid::parse(s)
    }
}

// ===== AIPI records =====================================================

macro_rules! u32_enum {
    ($(#[$m:meta])* $name:ident { $($(#[$vm:meta])* $v:ident = $c:path),+ $(,)? }) => {
        $(#[$m])*
        #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
        pub enum $name {
            $($(#[$vm])* $v,)+
            /// A number this SDK does not name; the C library refuses it
            /// where it is not valid (`ZXV_E_FORMAT` or `ZXV_E_RANGE`).
            Other(u32),
        }

        impl $name {
            /// The ABI number.
            pub fn raw(self) -> u32 {
                match self { $($name::$v => $c,)+ $name::Other(v) => v }
            }

            /// The value for ABI number `v`; an unnamed number is
            /// [`Other`](Self::Other) (never an error, kept as `Result` so
            /// callers may `?` it).
            pub fn from_raw(v: u32) -> Result<$name> {
                $(if v == $c { return Ok($name::$v); })+
                Ok($name::Other(v))
            }
        }
    };
}

u32_enum! {
    /// What a function is for; it picks the function CID's codec.
    AipiIntent {
        /// A plain function (`zxv-fn`).
        Fn = sys::ZXV_AIPI_INTENT_FN,
        /// Tensor inference.
        TensorInfer = sys::ZXV_AIPI_INTENT_TENSOR_INFER,
        /// Ledger settlement.
        SettleLedger = sys::ZXV_AIPI_INTENT_SETTLE_LEDGER,
        /// Swarm routing.
        SwarmRoute = sys::ZXV_AIPI_INTENT_SWARM_ROUTE,
    }
}

u32_enum! {
    /// How a function's result is accepted.
    AipiVerify {
        /// A checker export judges (input, output).
        Checker = sys::ZXV_AIPI_VERIFY_CHECKER,
        /// As `Checker`, with a certificate.
        Cert = sys::ZXV_AIPI_VERIFY_CERT,
        /// k of n executors agree byte for byte.
        Quorum = sys::ZXV_AIPI_VERIFY_QUORUM,
        /// Never accept a remote result.
        Local = sys::ZXV_AIPI_VERIFY_LOCAL,
    }
}

u32_enum! {
    /// Whether a function may leave the node.
    AipiPrivacy {
        /// It may.
        Public = sys::ZXV_AIPI_PRIV_PUBLIC,
        /// Private or key-bearing: never leaves the node.
        Local = sys::ZXV_AIPI_PRIV_LOCAL,
    }
}

u32_enum! {
    /// Whether two runs give the same bytes.
    AipiDeterminism {
        /// They may differ.
        Nondeterministic = sys::ZXV_AIPI_DET_NONDETERMINISTIC,
        /// They must agree byte for byte.
        Deterministic = sys::ZXV_AIPI_DET_DETERMINISTIC,
    }
}

/// The content of a function record.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AipiFunction {
    /// What the function is for.
    pub intent: AipiIntent,
    /// How a result is checked.
    pub verify: AipiVerify,
    /// Where it may run.
    pub privacy: AipiPrivacy,
    /// Whether runs must agree byte for byte.
    pub determinism: AipiDeterminism,
    /// QUORUM n (2..=8; 0 for other modes).
    pub quorum_n: u32,
    /// QUORUM k (n/2 < k <= n; 0 for other modes).
    pub quorum_k: u32,
    /// The most fuel one run may use (> 0).
    pub fuel_max: u64,
    /// CIDv1 raw sha2-256 of the Z-Wasm module bytes.
    pub module: Cid,
    /// CIDv1 raw sha2-256 of the z-contract text, or `None`.
    pub contract: Option<Cid>,
    /// The export computing the result.
    pub entry: String,
    /// The checking export (Checker and Cert modes; `""` else).
    pub checker: String,
    /// The certificate export (Cert mode only; `""` else).
    pub certifier: String,
}

fn opt_cstr(s: &str) -> Result<Option<std::ffi::CString>> {
    if s.is_empty() {
        Ok(None)
    } else {
        cstr(s).map(Some)
    }
}

impl AipiFunction {
    /// The canonical function record. Every field is checked, none masked
    /// (`ZXV_E_ARG` for a field out of range or breaking a rule, `ZXV_E_CID`
    /// for a CID that is not CIDv1 raw sha2-256).
    pub fn encode(&self) -> Result<Vec<u8>> {
        let entry = cstr(&self.entry)?;
        let checker = opt_cstr(&self.checker)?;
        let certifier = opt_cstr(&self.certifier)?;
        let cp =
            |o: &Option<std::ffi::CString>| o.as_ref().map_or(std::ptr::null(), |c| c.as_ptr());
        let con = self.contract.unwrap_or_default();
        // SAFETY: every pointer is valid or NULL with length 0; R4 buffer.
        read_bytes(FN_MAX, |b, c, l| unsafe {
            sys::zxv_aipi_fn_encode(
                self.intent.raw(),
                self.verify.raw(),
                self.privacy.raw(),
                self.determinism.raw(),
                self.quorum_n,
                self.quorum_k,
                self.fuel_max,
                self.module.ptr(),
                self.module.len(),
                con.ptr(),
                con.len(),
                entry.as_ptr(),
                cp(&checker),
                cp(&certifier),
                b,
                c,
                l,
            )
        })
    }

    /// Checks a function record strictly (`ZXV_E_FORMAT` for anything that
    /// is not a valid, canonical record) and returns its fields.
    pub fn decode(rec: &[u8]) -> Result<AipiFunction> {
        let (mut it, mut ve, mut pr, mut de, mut qn, mut qk) = (0u32, 0u32, 0u32, 0u32, 0u32, 0u32);
        let mut fuel = 0u64;
        // SAFETY: rec valid; out pointers valid.
        check(unsafe {
            sys::zxv_aipi_fn_decode(
                opt_ptr(rec),
                rec.len(),
                &mut it,
                &mut ve,
                &mut pr,
                &mut de,
                &mut qn,
                &mut qk,
                &mut fuel,
            )
        })?;
        let module = fn_cid(rec, sys::ZXV_AIPI_FIELD_MODULE)?;
        let contract = match fn_cid(rec, sys::ZXV_AIPI_FIELD_CONTRACT) {
            Ok(c) => Some(c),
            Err(e) if e.status() == sys::ZXV_E_NOT_FOUND => None,
            Err(e) => return Err(e),
        };
        Ok(AipiFunction {
            intent: AipiIntent::from_raw(it)?,
            verify: AipiVerify::from_raw(ve)?,
            privacy: AipiPrivacy::from_raw(pr)?,
            determinism: AipiDeterminism::from_raw(de)?,
            quorum_n: qn,
            quorum_k: qk,
            fuel_max: fuel,
            module,
            contract,
            entry: fn_name(rec, sys::ZXV_AIPI_FIELD_ENTRY)?,
            checker: fn_name(rec, sys::ZXV_AIPI_FIELD_CHECKER)?,
            certifier: fn_name(rec, sys::ZXV_AIPI_FIELD_CERTIFIER)?,
        })
    }

    /// The function CID of a record: codec [`MC_FN`] + intent, sha2-256 of
    /// the record bytes.
    pub fn cid_of(rec: &[u8]) -> Result<Cid> {
        // SAFETY: rec valid; out buffer has CID_MAX bytes.
        out_cid(|o, n| unsafe { sys::zxv_aipi_fn_cid(opt_ptr(rec), rec.len(), o, n) })
    }
}

fn fn_cid(rec: &[u8], field: u32) -> Result<Cid> {
    // SAFETY: rec valid; out buffer has CID_MAX bytes.
    out_cid(|o, n| unsafe { sys::zxv_aipi_fn_get_cid(opt_ptr(rec), rec.len(), field, o, n) })
}

fn fn_name(rec: &[u8], field: u32) -> Result<String> {
    // SAFETY: rec valid; R4 buffer.
    read_string(|b, c, l| unsafe {
        sys::zxv_aipi_fn_get_name(opt_ptr(rec), rec.len(), field, b, c, l)
    })
}

/// The call record naming one invocation: a function CID and its input (at
/// most [`IO_MAX`] bytes).
pub fn encode_call(func: &Cid, input: &[u8]) -> Result<Vec<u8>> {
    // SAFETY: buffers valid; R4 buffer.
    read_bytes(64 + input.len(), |b, c, l| unsafe {
        sys::zxv_aipi_call_encode(func.ptr(), func.len(), opt_ptr(input), input.len(), b, c, l)
    })
}

/// Checks a call record strictly; returns its function CID and a copy of
/// its input.
pub fn decode_call(rec: &[u8]) -> Result<(Cid, Vec<u8>)> {
    let (mut fo, mut fl, mut io, mut il) = (0usize, 0usize, 0usize, 0usize);
    // SAFETY: rec valid; out pointers valid.
    check(unsafe {
        sys::zxv_aipi_call_decode(opt_ptr(rec), rec.len(), &mut fo, &mut fl, &mut io, &mut il)
    })?;
    let func = Cid::from_bytes(slice(rec, fo, fl)?)?;
    Ok((func, slice(rec, io, il)?.to_vec()))
}

/// The call CID ([`MC_CALL`], sha2-256 of the call record) of
/// `(func, input)`.
pub fn call_cid(func: &Cid, input: &[u8]) -> Result<Cid> {
    // SAFETY: buffers valid; out buffer has CID_MAX bytes.
    out_cid(|o, n| unsafe {
        sys::zxv_aipi_call_cid(func.ptr(), func.len(), opt_ptr(input), input.len(), o, n)
    })
}

/// A checked sub-slice (an offset pair the library returned).
fn slice(rec: &[u8], off: usize, len: usize) -> Result<&[u8]> {
    rec.get(off..off.saturating_add(len))
        .ok_or_else(|| Error::with_detail(sys::ZXV_E_INTERNAL, "offset out of the record"))
}

/// An executor's answer to a call: the bytes it signs.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AipiClaim {
    /// The call CID answered.
    pub call: Cid,
    /// The executor's node id.
    pub executor: [u8; 32],
    /// The answer (at most [`IO_MAX`] bytes).
    pub output: Vec<u8>,
    /// The certificate, for Cert functions (at most [`CERT_MAX`] bytes).
    pub cert: Vec<u8>,
}

impl AipiClaim {
    /// The claim record. Signing it is the caller's job.
    pub fn encode(&self) -> Result<Vec<u8>> {
        // SAFETY: buffers valid; executor has 32 bytes; R4 buffer.
        read_bytes(
            128 + self.output.len() + self.cert.len(),
            |b, c, l| unsafe {
                sys::zxv_aipi_claim_encode(
                    self.call.ptr(),
                    self.call.len(),
                    self.executor.as_ptr(),
                    opt_ptr(&self.output),
                    self.output.len(),
                    opt_ptr(&self.cert),
                    self.cert.len(),
                    b,
                    c,
                    l,
                )
            },
        )
    }

    /// Checks a claim record strictly and returns its parts (copies).
    pub fn decode(rec: &[u8]) -> Result<AipiClaim> {
        let (mut co, mut cl, mut eo, mut oo, mut ol, mut xo, mut xl) =
            (0usize, 0usize, 0usize, 0usize, 0usize, 0usize, 0usize);
        // SAFETY: rec valid; out pointers valid.
        check(unsafe {
            sys::zxv_aipi_claim_decode(
                opt_ptr(rec),
                rec.len(),
                &mut co,
                &mut cl,
                &mut eo,
                &mut oo,
                &mut ol,
                &mut xo,
                &mut xl,
            )
        })?;
        let mut executor = [0u8; 32];
        executor.copy_from_slice(slice(rec, eo, 32)?);
        Ok(AipiClaim {
            call: Cid::from_bytes(slice(rec, co, cl)?)?,
            executor,
            output: slice(rec, oo, ol)?.to_vec(),
            cert: slice(rec, xo, xl)?.to_vec(),
        })
    }
}

// ===== the AIPI node ====================================================

/// The route a call would take ([`AipiNode::plan`]). This library is
/// offline, so `plan` reports only `Local` or `Unavailable`; the others are
/// the kernel's routes for a networked host, listed so the values are
/// fixed.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum AipiRoute {
    /// A cached result.
    Cache,
    /// Run here.
    Local,
    /// Fetch the module, then run here.
    FetchLocal,
    /// Run on the mesh.
    Mesh,
    /// No route.
    Unavailable,
}

impl AipiRoute {
    fn from_raw(v: i32) -> Result<AipiRoute> {
        Ok(match v {
            sys::ZXV_AIPI_ROUTE_CACHE => AipiRoute::Cache,
            sys::ZXV_AIPI_ROUTE_LOCAL => AipiRoute::Local,
            sys::ZXV_AIPI_ROUTE_FETCH_LOCAL => AipiRoute::FetchLocal,
            sys::ZXV_AIPI_ROUTE_MESH => AipiRoute::Mesh,
            sys::ZXV_AIPI_ROUTE_UNAVAILABLE => AipiRoute::Unavailable,
            _ => return Err(Error::with_detail(sys::ZXV_E_INTERNAL, "unknown route")),
        })
    }
}

/// A node's audit counters ([`AipiNode::info`]).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct AipiNodeInfo {
    /// Blocks indexed.
    pub blocks: u32,
    /// Store bytes used.
    pub store_used: u64,
    /// GETs whose bytes re-hashed correctly.
    pub gets_verified: u32,
    /// GETs whose stored bytes failed their hash (dropped).
    pub forbidden: u32,
}

/// An offline, in-memory IPFS node (block store and pins) with the AIPI
/// context over it (`zxv_aipi`). It belongs to the context of the
/// capability that created it and fails closed with it. Calls that store
/// ([`put`](Self::put), [`publish_function`](Self::publish_function)) use
/// that capability, which needs [`Caps::AIPI`](crate::Caps::AIPI); reads
/// need none. `Send + Sync`.
///
/// It cannot outlive its capability:
///
/// ```compile_fail
/// let ctx = zxv::Context::new()?;
/// let node = {
///     let cap = ctx.root_capability(1, None, zxv::Caps::ALL_1_5)?;
///     zxv::AipiNode::new(&cap, 1 << 20, 64, None)?
/// };
/// # Ok::<(), zxv::Error>(())
/// ```
pub struct AipiNode<'a> {
    raw: NonNull<sys::zxv_aipi>,
    cap: &'a Capability<'a>,
}

// SAFETY: the C node serializes its own state (thread-safe per the header).
unsafe impl Send for AipiNode<'_> {}
// SAFETY: as above.
unsafe impl Sync for AipiNode<'_> {}

impl<'a> AipiNode<'a> {
    /// A node holding at most `max_blocks` blocks (1..=[`BLOCKS_MAX`]) in
    /// `store_bytes` of memory ([`STORE_MIN`]..=[`STORE_MAX`]; each block
    /// costs its length plus about 100 bytes). `node_key` (copied) seals
    /// private blocks with ChaCha20-Poly1305; without it, private puts are
    /// refused with `ZXV_E_STATE`. `cap` needs [`Caps::AIPI`](crate::Caps::AIPI).
    pub fn new(
        cap: &'a Capability<'a>,
        store_bytes: u64,
        max_blocks: u32,
        node_key: Option<&[u8; 32]>,
    ) -> Result<AipiNode<'a>> {
        let store = usize::try_from(store_bytes)
            .map_err(|_| Error::with_detail(sys::ZXV_E_ARG, "store_bytes too large"))?;
        let kp = node_key.map_or(std::ptr::null(), |k| k.as_ptr());
        let mut raw = std::ptr::null_mut();
        // SAFETY: valid cap; key NULL or 32 bytes; out pointer valid.
        cap.call(|c| unsafe { sys::zxv_aipi_create(c, store, max_blocks, kp, &mut raw) })?;
        Ok(AipiNode {
            raw: non_null(raw, "aipi node")?,
            cap,
        })
    }

    /// Stores and pins `data` (at most [`BLOCK_MAX`] bytes) as one raw
    /// block and returns its CID. `private` stores it sealed and never
    /// announceable. Putting the same bytes again is a no-op; the same
    /// bytes public after private (or the reverse) is `ZXV_E_STATE`.
    /// `ZXV_E_CAPACITY` when the store is full.
    pub fn put(&self, data: &[u8], private: bool) -> Result<Cid> {
        let mut c = Cid::default();
        let mut n = 0usize;
        let a = self.raw.as_ptr();
        // SAFETY: valid cap and node; buffers valid; out has CID_MAX bytes.
        self.cap.call(|cap| unsafe {
            sys::zxv_aipi_put(
                cap,
                a,
                opt_ptr(data),
                data.len(),
                i32::from(private),
                c.b.as_mut_ptr(),
                &mut n,
            )
        })?;
        c.n = n.min(CID_MAX) as u8;
        Ok(c)
    }

    /// The bytes `cid` names, re-hashed before they are returned.
    /// `ZXV_E_NOT_FOUND` when the block is not held here (there is no
    /// network); `ZXV_E_CID` for a bad or identity CID, or stored bytes that
    /// no longer match (dropped and counted as forbidden); `ZXV_E_STATE`
    /// for a private block and no node key.
    pub fn get(&self, cid: &Cid) -> Result<Vec<u8>> {
        let a = self.raw.as_ptr();
        local_with(self.cap, || {
            // SAFETY: valid node; CID valid; R4 buffer.
            read_bytes(256, |b, c, l| unsafe {
                sys::zxv_aipi_get(a, cid.ptr(), cid.len(), b, c, l)
            })
        })
    }

    /// Stores a function record (checked strictly; sealed when its privacy
    /// is [`AipiPrivacy::Local`]) and returns its function CID. The module
    /// and contract need not be held yet.
    pub fn publish_function(&self, rec: &[u8]) -> Result<Cid> {
        let mut c = Cid::default();
        let mut n = 0usize;
        let a = self.raw.as_ptr();
        // SAFETY: valid cap and node; rec valid; out has CID_MAX bytes.
        self.cap.call(|cap| unsafe {
            sys::zxv_aipi_publish_fn(cap, a, opt_ptr(rec), rec.len(), c.b.as_mut_ptr(), &mut n)
        })?;
        c.n = n.min(CID_MAX) as u8;
        Ok(c)
    }

    /// The function record `func` names: fetched, verified by hash, decoded
    /// strictly, and its codec checked against its intent.
    pub fn load_function(&self, func: &Cid) -> Result<Vec<u8>> {
        let a = self.raw.as_ptr();
        local_with(self.cap, || {
            // SAFETY: valid node; CID valid; R4 buffer.
            read_bytes(FN_MAX, |b, c, l| unsafe {
                sys::zxv_aipi_load_fn(a, func.ptr(), func.len(), b, c, l)
            })
        })
    }

    /// The route a call of `func` on `input` would take now. It runs and
    /// changes nothing. Offline: [`AipiRoute::Local`] when the module (and
    /// contract) are held, else [`AipiRoute::Unavailable`].
    pub fn plan(&self, func: &Cid, input: &[u8], input_private: bool) -> Result<AipiRoute> {
        let mut r = 0i32;
        let a = self.raw.as_ptr();
        // SAFETY: valid node; buffers valid; out pointer valid.
        local(self.cap, || unsafe {
            sys::zxv_aipi_plan(
                a,
                func.ptr(),
                func.len(),
                opt_ptr(input),
                input.len(),
                i32::from(input_private),
                &mut r,
            )
        })?;
        AipiRoute::from_raw(r)
    }

    /// The audit counters.
    pub fn info(&self) -> Result<AipiNodeInfo> {
        let (mut b, mut u, mut g, mut f) = (0u32, 0u64, 0u32, 0u32);
        let a = self.raw.as_ptr();
        // SAFETY: valid node; out pointers valid.
        local(self.cap, || unsafe {
            sys::zxv_aipi_info(a, &mut b, &mut u, &mut g, &mut f)
        })?;
        Ok(AipiNodeInfo {
            blocks: b,
            store_used: u,
            gets_verified: g,
            forbidden: f,
        })
    }
}

impl fmt::Debug for AipiNode<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("AipiNode")
            .field("info", &self.info().ok())
            .finish()
    }
}

impl Drop for AipiNode<'_> {
    fn drop(&mut self) {
        // SAFETY: owned handle destroyed once, before its capability; C
        // wipes the node key.
        unsafe { sys::zxv_aipi_destroy(self.raw.as_ptr()) }
    }
}
