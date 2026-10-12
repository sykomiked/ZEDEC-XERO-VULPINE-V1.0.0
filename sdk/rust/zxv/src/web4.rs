// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! The Web 4 agent layer (ABI 1.5): agents with ML-DSA-65 keys, their
//! signed tool manifests, a directory of verified agent cards, human
//! consent tokens and signed envelopes.
//!
//! **Identity.** `agent_id = SHA3-256(agent public key)`. An agent is bound
//! to the node hosting it by a binding signed by the node's key; a
//! self-bound agent signs its own binding and has `peer_id == agent_id`.
//!
//! **Consent.** An agent never moves money or shares personal data without
//! a token signed by the HUMAN's key over {human, agent, action digest,
//! scope, max amount, validity, nonce}. The action digest
//! ([`action_digest`]) binds the exact tool, parties, request id and
//! payload. Tokens are single-use: a [`ConsentLog`] records consumed and
//! revoked tokens.
//!
//! **Secrets.** A private key never leaves its [`W4Agent`]: there is no
//! call that returns it, and dropping the agent wipes it. This crate never
//! logs keys (`Debug` prints only the public ids).
//!
//! **Capabilities.** Creating a [`W4Agent`] and everything it signs
//! (bindings, manifests, envelopes) needs
//! [`Caps::CRYPTO_SIGN`](crate::Caps::CRYPTO_SIGN); issuing a consent token
//! needs [`Caps::ATTEST`](crate::Caps::ATTEST). Parsing, verifying,
//! digests, manifests, the directory and the consent log need none.
//!
//! **Time.** There is no clock in here: every call that needs the time
//! takes `now_ms` (Unix milliseconds) from the caller.
//!
//! **Honest limits:** envelopes are signed, not encrypted. A consent token
//! proves that a key holder signed; whether that holder is the right human
//! and understood what they signed depends on the device and UI that showed
//! the action, which this library cannot see. No network transport, no
//! replay window or rate limiting on the receiving side, and no Web 2 or
//! Web 3 bridges. Interoperability is between ZXV instances only. ML-DSA-65
//! here is not FIPS 140-3 validated.

use std::ffi::c_char;
use std::fmt;
use std::ptr::NonNull;

use zxv_sys as sys;

use crate::capability::Capability;
use crate::error::{check, Result};
use crate::ffi::{
    c_name, cstr, local, local_with, need_len, non_null, opt_ptr, read_bytes, read_string,
    same_context,
};

/// ML-DSA-65 public key (1952 bytes).
pub const PK_BYTES: usize = sys::ZXV_W4_PK_BYTES as usize;
/// ML-DSA-65 signature (3309 bytes).
pub const SIG_BYTES: usize = sys::ZXV_W4_SIG_BYTES as usize;
/// Agent, peer and human ids (32 bytes).
pub const ID_BYTES: usize = sys::ZXV_W4_ID_BYTES as usize;
/// Longest name: 31 characters of `[a-z0-9._-]`.
pub const NAME_MAX: usize = sys::ZXV_W4_NAME_MAX as usize;
/// Tools per manifest.
pub const MAX_TOOLS: u32 = sys::ZXV_W4_MAX_TOOLS;
/// Payload bytes per envelope.
pub const MSG_MAX_PAYLOAD: usize = sys::ZXV_W4_MSG_MAX_PAYLOAD as usize;
/// An encoded consent token (3453 bytes).
pub const CONSENT_BYTES: usize = sys::ZXV_W4_CONSENT_BYTES as usize;
/// Longest node-bound card.
pub const CARD_MAX: usize = sys::ZXV_W4_CARD_MAX as usize;
/// Longest encoded manifest.
pub const MANIFEST_MAX: usize = sys::ZXV_W4_MANIFEST_MAX as usize;
/// Longest encoded envelope.
pub const ENV_MAX: usize = sys::ZXV_W4_ENV_MAX as usize;
/// Most directory entries.
pub const DIR_MAX: u32 = sys::ZXV_W4_DIR_MAX;
/// Most consent log entries.
pub const CONSENT_LOG_MAX: u32 = sys::ZXV_W4_CONSENT_LOG_MAX;
/// Tool flag: the tool moves money.
pub const TOOL_MONEY: u32 = sys::ZXV_W4_TOOL_MONEY;
/// Tool flag: the tool reveals personal data.
pub const TOOL_PII: u32 = sys::ZXV_W4_TOOL_PII;
/// Tool flag: answers with STREAM chunks.
pub const TOOL_STREAM: u32 = sys::ZXV_W4_TOOL_STREAM;
/// Consent scope: may move money up to the token's maximum.
pub const CONSENT_MONEY: u32 = sys::ZXV_W4_CONSENT_MONEY;
/// Consent scope: may share personal data.
pub const CONSENT_PII: u32 = sys::ZXV_W4_CONSENT_PII;
/// Envelope flag: the last STREAM chunk or final response.
pub const FLAG_FINAL: u32 = sys::ZXV_W4_MF_FINAL;
/// Envelope flag: a token is attached (set by sealing only).
pub const FLAG_CONSENT: u32 = sys::ZXV_W4_MF_CONSENT;

/// An envelope kind.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum W4MsgKind {
    /// A request.
    Request,
    /// A response.
    Response,
    /// A stream chunk.
    Stream,
    /// A cancellation.
    Cancel,
    /// An error.
    Error,
    /// A kind this SDK does not know.
    Other(i32),
}

impl W4MsgKind {
    /// The ABI number.
    pub fn raw(self) -> i32 {
        match self {
            W4MsgKind::Request => sys::ZXV_W4_MSG_REQUEST,
            W4MsgKind::Response => sys::ZXV_W4_MSG_RESPONSE,
            W4MsgKind::Stream => sys::ZXV_W4_MSG_STREAM,
            W4MsgKind::Cancel => sys::ZXV_W4_MSG_CANCEL,
            W4MsgKind::Error => sys::ZXV_W4_MSG_ERROR,
            W4MsgKind::Other(n) => n,
        }
    }

    /// The kind for ABI number `v`.
    pub fn from_raw(v: i32) -> W4MsgKind {
        match v {
            sys::ZXV_W4_MSG_REQUEST => W4MsgKind::Request,
            sys::ZXV_W4_MSG_RESPONSE => W4MsgKind::Response,
            sys::ZXV_W4_MSG_STREAM => W4MsgKind::Stream,
            sys::ZXV_W4_MSG_CANCEL => W4MsgKind::Cancel,
            sys::ZXV_W4_MSG_ERROR => W4MsgKind::Error,
            n => W4MsgKind::Other(n),
        }
    }
}

/// An agent's public facts ([`W4Agent::info`]).
#[derive(Clone, PartialEq, Eq)]
pub struct W4AgentInfo {
    /// SHA3-256 of `public_key`.
    pub agent_id: [u8; 32],
    /// The hosting node's id (`== agent_id` when self-bound).
    pub peer_id: [u8; 32],
    /// The ML-DSA-65 public key ([`PK_BYTES`]).
    pub public_key: Vec<u8>,
}

impl fmt::Debug for W4AgentInfo {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("W4AgentInfo")
            .field("agent_id", &crate::ffi::hex(&self.agent_id))
            .field("peer_id", &crate::ffi::hex(&self.peer_id))
            .finish_non_exhaustive()
    }
}

/// A key handle: an ML-DSA-65 key pair and the agent's signed card. A
/// human's key is a `W4Agent` too (`human_id` is its agent id). The private
/// key never leaves the handle and `Drop` wipes it. It belongs to the
/// context of the capability that created it, signs with that capability,
/// and fails closed with that context. `Send` but not `Sync`; sealing takes
/// `&mut self` (it advances the envelope sequence).
///
/// ```
/// use zxv::{Caps, Context, W4Agent};
/// let ctx = Context::new()?;
/// let cap = ctx.root_capability(1, None, Caps::ALL_1_5)?;
/// let alice = W4Agent::new(&cap, Some(&[1u8; 32]), "alice", None, 1_000)?;
/// let card = alice.card()?;
/// let seen = zxv::web4::verify_card(&card)?;
/// assert_eq!(seen.name, "alice");
/// assert_eq!(seen.agent_id, alice.info()?.agent_id);
/// # Ok::<(), zxv::Error>(())
/// ```
pub struct W4Agent<'a> {
    raw: NonNull<sys::zxv_w4_agent>,
    cap: &'a Capability<'a>,
}

// SAFETY: the handle owns its key; C is not thread-safe for it, so it is
// not Sync and the mutating C calls take &mut self.
unsafe impl Send for W4Agent<'_> {}

impl<'a> W4Agent<'a> {
    /// A key handle named `name` (1..=31 of `[a-z0-9._-]`). `seed` gives a
    /// reproducible key, `None` a fresh one from the OS. `node` is the agent
    /// hosting this one (it signs the binding; same context, `ZXV_E_ARG`
    /// otherwise), or `None` for a self-bound agent. `now_ms` is the
    /// binding time. `cap` needs [`Caps::CRYPTO_SIGN`](crate::Caps::CRYPTO_SIGN).
    pub fn new(
        cap: &'a Capability<'a>,
        seed: Option<&[u8; 32]>,
        name: &str,
        node: Option<&W4Agent<'_>>,
        now_ms: u64,
    ) -> Result<W4Agent<'a>> {
        let cn = cstr(name)?;
        let sp = seed.map_or(std::ptr::null(), |s| s.as_ptr());
        let np = match node {
            Some(n) => {
                same_context(cap, n.cap, "node")?;
                n.raw.as_ptr().cast_const()
            }
            None => std::ptr::null(),
        };
        let mut raw = std::ptr::null_mut();
        // SAFETY: valid cap; seed NULL or 32 bytes; name NUL-terminated;
        // node NULL or a live agent of the same context.
        cap.call(|c| unsafe {
            sys::zxv_w4_agent_create(c, sp, cn.as_ptr(), np, now_ms, &mut raw)
        })?;
        Ok(W4Agent {
            raw: non_null(raw, "web4 agent")?,
            cap,
        })
    }

    /// The agent's ids and public key.
    pub fn info(&self) -> Result<W4AgentInfo> {
        let (mut id, mut peer) = ([0u8; 32], [0u8; 32]);
        let mut pk = vec![0u8; PK_BYTES];
        let a = self.raw.as_ptr();
        // SAFETY: valid agent; outs have 32, 32 and PK_BYTES bytes.
        local(self.cap, || unsafe {
            sys::zxv_w4_agent_info(a, id.as_mut_ptr(), peer.as_mut_ptr(), pk.as_mut_ptr())
        })?;
        Ok(W4AgentInfo {
            agent_id: id,
            peer_id: peer,
            public_key: pk,
        })
    }

    /// The agent's card: what a peer needs to verify it ("W4CD" format; at
    /// most [`CARD_MAX`] bytes).
    pub fn card(&self) -> Result<Vec<u8>> {
        let a = self.raw.as_ptr();
        // SAFETY: valid agent; R4 buffer.
        local_with(self.cap, || {
            read_bytes(CARD_MAX, |b, c, l| unsafe {
                sys::zxv_w4_agent_card(a, b, c, l)
            })
        })
    }

    /// Builds, signs and encodes an envelope from this agent with the next
    /// sequence number, using the agent's capability (`CRYPTO_SIGN`).
    /// `ZXV_E_TOO_LONG`: payload over [`MSG_MAX_PAYLOAD`].
    pub fn seal(&mut self, m: &W4Message<'_>, now_ms: u64) -> Result<Vec<u8>> {
        let cn = cstr(m.tool)?;
        let tp = m.consent.map_or(std::ptr::null(), |t| t.0.as_ptr());
        let a = self.raw.as_ptr();
        let cap = self.cap;
        local_with(cap, || {
            read_bytes(ENV_MAX, |b, c, l| {
                // SAFETY: valid cap and agent (exclusive); every buffer is
                // valid or NULL with length 0; token NULL or 3453 bytes.
                unsafe {
                    sys::zxv_w4_env_seal(
                        cap.raw(),
                        a,
                        m.kind.raw(),
                        m.to.as_ptr(),
                        m.req_id,
                        m.chunk,
                        cn.as_ptr(),
                        m.flags,
                        opt_ptr(m.payload),
                        m.payload.len(),
                        tp,
                        now_ms,
                        b,
                        c,
                        l,
                    )
                }
            })
        })
    }

    /// Seals a reply to a REQUEST envelope addressed to this agent: to and
    /// from swapped, the same request id and tool. `kind`: `Response`,
    /// `Stream` or `Error`. The request's signature is not checked here
    /// (use [`verify_envelope`] first).
    pub fn reply(
        &mut self,
        request: &[u8],
        kind: W4MsgKind,
        chunk: u32,
        flags: u32,
        payload: &[u8],
        now_ms: u64,
    ) -> Result<Vec<u8>> {
        let a = self.raw.as_ptr();
        let cap = self.cap;
        local_with(cap, || {
            read_bytes(ENV_MAX, |b, c, l| {
                // SAFETY: valid cap and agent (exclusive); buffers valid.
                unsafe {
                    sys::zxv_w4_env_reply(
                        cap.raw(),
                        a,
                        opt_ptr(request),
                        request.len(),
                        kind.raw(),
                        chunk,
                        flags,
                        opt_ptr(payload),
                        payload.len(),
                        now_ms,
                        b,
                        c,
                        l,
                    )
                }
            })
        })
    }
}

impl fmt::Debug for W4Agent<'_> {
    /// Prints the public ids only; never the key.
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self.info() {
            Ok(i) => write!(f, "W4Agent({})", crate::ffi::hex(&i.agent_id)),
            Err(_) => f.write_str("W4Agent(<unavailable>)"),
        }
    }
}

impl Drop for W4Agent<'_> {
    fn drop(&mut self) {
        // SAFETY: owned handle destroyed once; C wipes the private key.
        unsafe { sys::zxv_w4_agent_destroy(self.raw.as_ptr()) }
    }
}

/// What a verified card says ([`verify_card`]).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct W4CardInfo {
    /// Derived from the agent key in the card.
    pub agent_id: [u8; 32],
    /// Derived from the node key (or the agent key when self-bound).
    pub peer_id: [u8; 32],
    /// The agent's name.
    pub name: String,
    /// When the binding was signed (Unix ms).
    pub bound_ms: u64,
}

/// Checks that `card` is well formed and its binding signature valid.
/// `ZXV_E_FORMAT`: malformed. `ZXV_E_VERIFY`: the signature fails. Pure.
pub fn verify_card(card: &[u8]) -> Result<W4CardInfo> {
    let (mut id, mut peer) = ([0u8; 32], [0u8; 32]);
    let mut name = [0 as c_char; 32];
    let mut ms = 0u64;
    // SAFETY: card valid; outs have 32, 32, 32 bytes.
    check(unsafe {
        sys::zxv_w4_card_verify(
            opt_ptr(card),
            card.len(),
            id.as_mut_ptr(),
            peer.as_mut_ptr(),
            name.as_mut_ptr(),
            &mut ms,
        )
    })?;
    Ok(W4CardInfo {
        agent_id: id,
        peer_id: peer,
        name: c_name(&name),
        bound_ms: ms,
    })
}

/// Verified agent cards keyed by agent id. Pure (no context). `Send`, not
/// `Sync`; adding takes `&mut self`.
pub struct W4Directory {
    raw: NonNull<sys::zxv_w4_dir>,
}

// SAFETY: owns its memory; not thread-safe in C, so no Sync.
unsafe impl Send for W4Directory {}

impl W4Directory {
    /// A directory of `capacity` entries (1..=[`DIR_MAX`]).
    pub fn new(capacity: u32) -> Result<W4Directory> {
        let mut p = std::ptr::null_mut();
        // SAFETY: out pointer valid.
        check(unsafe { sys::zxv_w4_dir_create(capacity, &mut p) })?;
        Ok(W4Directory {
            raw: non_null(p, "web4 directory")?,
        })
    }

    /// Verifies `card`, then adds it or replaces the entry for the same
    /// agent. `ZXV_E_STATE` when its binding is older than the one held (no
    /// rollback); `ZXV_E_CAPACITY` when full.
    pub fn add(&mut self, card: &[u8]) -> Result<()> {
        // SAFETY: owned directory, exclusive; card valid.
        check(unsafe { sys::zxv_w4_dir_add(self.raw.as_ptr(), opt_ptr(card), card.len()) })
    }

    /// The card held for `agent_id` (`ZXV_E_NOT_FOUND` if none).
    pub fn find(&self, agent_id: &[u8; 32]) -> Result<Vec<u8>> {
        let d = self.raw.as_ptr();
        // SAFETY: owned directory; id 32 bytes; R4 buffer.
        read_bytes(CARD_MAX, |b, c, l| unsafe {
            sys::zxv_w4_dir_find(d, agent_id.as_ptr(), b, c, l)
        })
    }

    /// How many cards the directory holds.
    pub fn count(&self) -> Result<u32> {
        let mut n = 0u32;
        // SAFETY: owned directory; out pointer valid.
        check(unsafe { sys::zxv_w4_dir_count(self.raw.as_ptr(), &mut n) })?;
        Ok(n)
    }
}

impl fmt::Debug for W4Directory {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("W4Directory")
            .field("count", &self.count().ok())
            .finish()
    }
}

impl Drop for W4Directory {
    fn drop(&mut self) {
        // SAFETY: owned handle destroyed once.
        unsafe { sys::zxv_w4_dir_destroy(self.raw.as_ptr()) }
    }
}

/// One entry of a tool manifest.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct W4Tool {
    /// 1..=31 of `[a-z0-9._-]`.
    pub name: String,
    /// Price in VFV minor units (2 digits).
    pub price_vfv: u64,
    /// Calls per minute, at least 1.
    pub rate_per_min: u32,
    /// Burst, at least 1.
    pub burst: u32,
    /// `TOOL_*` bits.
    pub flags: u32,
    /// Largest input payload, at most [`MSG_MAX_PAYLOAD`].
    pub max_input: u32,
}

/// A manifest's header ([`W4Manifest::info`]).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct W4ManifestInfo {
    /// Zero until signed.
    pub agent_id: [u8; 32],
    /// Zero until signed.
    pub peer_id: [u8; 32],
    /// The manifest version.
    pub version: u32,
    /// Valid from (Unix ms).
    pub issued_ms: u64,
    /// Valid until, exclusive (Unix ms).
    pub expires_ms: u64,
    /// Tools listed.
    pub tool_count: u32,
    /// Whether it carries a signature (not checked here).
    pub signed: bool,
}

/// A tool manifest: up to 16 tools, valid in `[issued, expires)`, signed by
/// its agent. Adding a tool after signing drops the signature. Pure (no
/// context). `Send`, not `Sync`; changes take `&mut self`.
pub struct W4Manifest {
    raw: NonNull<sys::zxv_w4_manifest>,
}

// SAFETY: owns its memory; not thread-safe in C, so no Sync.
unsafe impl Send for W4Manifest {}

impl W4Manifest {
    /// An empty, unsigned manifest valid in `[issued_ms, expires_ms)`.
    /// `ZXV_E_ARG` when `expires_ms <= issued_ms`.
    pub fn new(version: u32, issued_ms: u64, expires_ms: u64) -> Result<W4Manifest> {
        let mut p = std::ptr::null_mut();
        // SAFETY: out pointer valid.
        check(unsafe { sys::zxv_w4_manifest_create(version, issued_ms, expires_ms, &mut p) })?;
        Ok(W4Manifest {
            raw: non_null(p, "web4 manifest")?,
        })
    }

    /// Parses a manifest's wire form. The signature is kept but NOT
    /// checked: call [`verify`](Self::verify). `ZXV_E_FORMAT`: malformed.
    pub fn decode(b: &[u8]) -> Result<W4Manifest> {
        let mut p = std::ptr::null_mut();
        // SAFETY: b valid; out pointer valid.
        check(unsafe { sys::zxv_w4_manifest_decode(opt_ptr(b), b.len(), &mut p) })?;
        Ok(W4Manifest {
            raw: non_null(p, "web4 manifest")?,
        })
    }

    /// Adds a tool. `ZXV_E_ARG`, `ZXV_E_TOO_LONG` (name),
    /// `ZXV_E_DUPLICATE` (already listed), `ZXV_E_CAPACITY` (16 tools).
    pub fn add_tool(&mut self, t: &W4Tool) -> Result<()> {
        let cn = cstr(&t.name)?;
        // SAFETY: owned manifest, exclusive; name NUL-terminated.
        check(unsafe {
            sys::zxv_w4_manifest_add_tool(
                self.raw.as_ptr(),
                cn.as_ptr(),
                t.price_vfv,
                t.rate_per_min,
                t.burst,
                t.flags,
                t.max_input,
            )
        })
    }

    /// Signs the manifest with `agent`'s key, using the agent's capability
    /// (`CRYPTO_SIGN`).
    pub fn sign(&mut self, agent: &W4Agent<'_>) -> Result<()> {
        let (a, m) = (agent.raw.as_ptr().cast_const(), self.raw.as_ptr());
        // SAFETY: valid cap and agent; owned manifest, exclusive.
        agent
            .cap
            .call(|c| unsafe { sys::zxv_w4_manifest_sign(c, a, m) })
    }

    /// `Ok(())` if the manifest is signed by `public_key`, that key is the
    /// manifest's agent, and it is valid at `now_ms`. `ZXV_E_VERIFY`: wrong
    /// key or a bad (or no) signature. `ZXV_E_STATE`: not valid at `now_ms`.
    pub fn verify(&self, public_key: &[u8], now_ms: u64) -> Result<()> {
        need_len(public_key, PK_BYTES, "public key")?;
        // SAFETY: owned manifest; key PK_BYTES bytes.
        check(unsafe {
            sys::zxv_w4_manifest_verify(self.raw.as_ptr(), public_key.as_ptr(), now_ms)
        })
    }

    /// The signed wire form. `ZXV_E_STATE` if not signed.
    pub fn encode(&self) -> Result<Vec<u8>> {
        let m = self.raw.as_ptr();
        // SAFETY: owned manifest; R4 buffer.
        read_bytes(MANIFEST_MAX, |b, c, l| unsafe {
            sys::zxv_w4_manifest_encode(m, b, c, l)
        })
    }

    /// The Web 2 discovery face (e.g. `/.well-known/web4-agent.json`).
    /// Prices stay integers in minor units.
    pub fn to_json(&self) -> Result<String> {
        let m = self.raw.as_ptr();
        // SAFETY: owned manifest; R4 buffer.
        read_string(|b, c, l| unsafe { sys::zxv_w4_manifest_to_json(m, b, c, l) })
    }

    /// The manifest's header.
    pub fn info(&self) -> Result<W4ManifestInfo> {
        let (mut id, mut peer) = ([0u8; 32], [0u8; 32]);
        let (mut v, mut is, mut ex, mut n, mut sg) = (0u32, 0u64, 0u64, 0u32, 0i32);
        // SAFETY: owned manifest; out pointers valid.
        check(unsafe {
            sys::zxv_w4_manifest_info(
                self.raw.as_ptr(),
                id.as_mut_ptr(),
                peer.as_mut_ptr(),
                &mut v,
                &mut is,
                &mut ex,
                &mut n,
                &mut sg,
            )
        })?;
        Ok(W4ManifestInfo {
            agent_id: id,
            peer_id: peer,
            version: v,
            issued_ms: is,
            expires_ms: ex,
            tool_count: n,
            signed: sg != 0,
        })
    }

    /// Tool `index` (0-based); `ZXV_E_NOT_FOUND` past the last tool.
    pub fn tool(&self, index: u32) -> Result<W4Tool> {
        let mut name = [0 as c_char; 32];
        let (mut price, mut rate, mut burst, mut flags, mut max_in) =
            (0u64, 0u32, 0u32, 0u32, 0u32);
        // SAFETY: owned manifest; name has 32 bytes; out pointers valid.
        check(unsafe {
            sys::zxv_w4_manifest_tool(
                self.raw.as_ptr(),
                index,
                name.as_mut_ptr(),
                &mut price,
                &mut rate,
                &mut burst,
                &mut flags,
                &mut max_in,
            )
        })?;
        Ok(W4Tool {
            name: c_name(&name),
            price_vfv: price,
            rate_per_min: rate,
            burst,
            flags,
            max_input: max_in,
        })
    }

    /// Every tool, in order (a lazy iterator of [`tool`](Self::tool)).
    pub fn tools(&self) -> Result<impl Iterator<Item = Result<W4Tool>> + '_> {
        let n = self.info()?.tool_count;
        Ok((0..n).map(move |i| self.tool(i)))
    }

    /// The index of the tool called `name` (`ZXV_E_NOT_FOUND` if not listed).
    pub fn find(&self, name: &str) -> Result<u32> {
        let cn = cstr(name)?;
        let mut i = 0u32;
        // SAFETY: owned manifest; name NUL-terminated; out pointer valid.
        check(unsafe { sys::zxv_w4_manifest_find(self.raw.as_ptr(), cn.as_ptr(), &mut i) })?;
        Ok(i)
    }
}

impl fmt::Debug for W4Manifest {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("W4Manifest")
            .field("info", &self.info().ok())
            .finish()
    }
}

impl Drop for W4Manifest {
    fn drop(&mut self) {
        // SAFETY: owned handle destroyed once.
        unsafe { sys::zxv_w4_manifest_destroy(self.raw.as_ptr()) }
    }
}

/// The digest a consent token for one request must carry: SHA3-256 over
/// the tool, from, to, request id and payload. Pure. `ZXV_E_TOO_LONG`:
/// payload over [`MSG_MAX_PAYLOAD`].
pub fn action_digest(
    tool: &str,
    from: &[u8; 32],
    to: &[u8; 32],
    req_id: u64,
    payload: &[u8],
) -> Result<[u8; 32]> {
    let cn = cstr(tool)?;
    let mut d = [0u8; 32];
    // SAFETY: tool NUL-terminated; ids 32 bytes; payload valid; out 32 bytes.
    check(unsafe {
        sys::zxv_w4_action_digest(
            cn.as_ptr(),
            from.as_ptr(),
            to.as_ptr(),
            req_id,
            opt_ptr(payload),
            payload.len(),
            d.as_mut_ptr(),
        )
    })?;
    Ok(d)
}

/// An encoded consent token ("W4CS", [`CONSENT_BYTES`]). A value type; it
/// carries a signature, not a secret.
#[derive(Clone, PartialEq, Eq, Hash)]
pub struct ConsentToken(pub [u8; CONSENT_BYTES]);

/// A token's fields ([`ConsentToken::info`]).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ConsentInfo {
    /// The signing human's id.
    pub human_id: [u8; 32],
    /// The agent allowed to act.
    pub agent_id: [u8; 32],
    /// The action digest.
    pub action: [u8; 32],
    /// `CONSENT_*` bits.
    pub scope: u32,
    /// Money ceiling, VFV minor units.
    pub max_vfv: u64,
    /// Valid from (Unix ms).
    pub issued_ms: u64,
    /// Valid until, exclusive (Unix ms).
    pub expires_ms: u64,
    /// Makes the token single-use.
    pub nonce: [u8; 16],
}

impl ConsentToken {
    /// A token from its encoded bytes (`ZXV_E_ARG` for another length; the
    /// content is checked when it is used).
    pub fn from_bytes(b: &[u8]) -> Result<ConsentToken> {
        need_len(b, CONSENT_BYTES, "consent token")?;
        let mut t = [0u8; CONSENT_BYTES];
        t.copy_from_slice(b);
        Ok(ConsentToken(t))
    }

    /// Reads the fields without checking the signature. `ZXV_E_FORMAT`:
    /// malformed.
    pub fn info(&self) -> Result<ConsentInfo> {
        let mut i = ConsentInfo {
            human_id: [0; 32],
            agent_id: [0; 32],
            action: [0; 32],
            scope: 0,
            max_vfv: 0,
            issued_ms: 0,
            expires_ms: 0,
            nonce: [0; 16],
        };
        // SAFETY: token 3453 bytes; outs sized as the header says.
        check(unsafe {
            sys::zxv_w4_consent_info(
                self.0.as_ptr(),
                i.human_id.as_mut_ptr(),
                i.agent_id.as_mut_ptr(),
                i.action.as_mut_ptr(),
                &mut i.scope,
                &mut i.max_vfv,
                &mut i.issued_ms,
                &mut i.expires_ms,
                i.nonce.as_mut_ptr(),
            )
        })?;
        Ok(i)
    }
}

impl fmt::Debug for ConsentToken {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "ConsentToken({:?})", self.info().ok())
    }
}

/// What a human consents to ([`issue_consent`]).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ConsentRequest {
    /// The agent allowed to act.
    pub agent_id: [u8; 32],
    /// [`action_digest`] of the exact request.
    pub action: [u8; 32],
    /// Nonzero `CONSENT_*` bits.
    pub scope: u32,
    /// Money ceiling, VFV minor units.
    pub max_vfv: u64,
    /// Valid from.
    pub issued_ms: u64,
    /// Valid until (exclusive), above `issued_ms`.
    pub expires_ms: u64,
    /// Makes the token single-use; `None` for a random one.
    pub nonce: Option<[u8; 16]>,
}

/// Issues a consent token on the HUMAN's side, after showing them the
/// action: signed by `human`'s key. `cap` needs
/// [`Caps::ATTEST`](crate::Caps::ATTEST) and must belong to `human`'s
/// context (`ZXV_E_ARG` otherwise).
pub fn issue_consent(
    cap: &Capability<'_>,
    human: &W4Agent<'_>,
    r: &ConsentRequest,
) -> Result<ConsentToken> {
    same_context(cap, human.cap, "human key")?;
    let mut t = ConsentToken([0u8; CONSENT_BYTES]);
    let np = r.nonce.as_ref().map_or(std::ptr::null(), |n| n.as_ptr());
    let h = human.raw.as_ptr().cast_const();
    // SAFETY: valid cap and agent; ids 32 bytes; nonce NULL or 16 bytes;
    // out has CONSENT_BYTES bytes.
    cap.call(|c| unsafe {
        sys::zxv_w4_consent_issue(
            c,
            h,
            r.agent_id.as_ptr(),
            r.action.as_ptr(),
            r.scope,
            r.max_vfv,
            r.issued_ms,
            r.expires_ms,
            np,
            t.0.as_mut_ptr(),
        )
    })?;
    Ok(t)
}

/// What an action needs consent for ([`ConsentLog::check`]).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ConsentNeed {
    /// The agent acting.
    pub agent_id: [u8; 32],
    /// [`action_digest`] of the request.
    pub action: [u8; 32],
    /// Nonzero `CONSENT_*` bits.
    pub need: u32,
    /// VFV minor units (checked when `need` has [`CONSENT_MONEY`]).
    pub amount: u64,
    /// The time of the check.
    pub now_ms: u64,
}

/// A single-use log of consumed and revoked consent tokens. Pure (no
/// context). `Send`, not `Sync`; every call takes `&mut self`.
pub struct ConsentLog {
    raw: NonNull<sys::zxv_w4_consent_log>,
}

// SAFETY: owns its memory; not thread-safe in C, so no Sync.
unsafe impl Send for ConsentLog {}

impl ConsentLog {
    /// A log of `capacity` entries (1..=[`CONSENT_LOG_MAX`]).
    pub fn new(capacity: u32) -> Result<ConsentLog> {
        let mut p = std::ptr::null_mut();
        // SAFETY: out pointer valid.
        check(unsafe { sys::zxv_w4_consent_log_create(capacity, &mut p) })?;
        Ok(ConsentLog {
            raw: non_null(p, "web4 consent log")?,
        })
    }

    /// Checks that `token` (`None`: none presented) lets `n.agent_id` do
    /// `n.action`, signed by the human whose key is `human_public_key`,
    /// valid at `n.now_ms`; then records it as used. `Ok(())` means consent
    /// was given and is now spent. `ZXV_E_CONSENT`: no token, a mismatch,
    /// expired, a failing signature, or a token already used or revoked.
    /// `ZXV_E_CAPACITY`: the log is full even after pruning (fail closed).
    pub fn check(
        &mut self,
        token: Option<&ConsentToken>,
        human_public_key: &[u8],
        n: &ConsentNeed,
    ) -> Result<()> {
        need_len(human_public_key, PK_BYTES, "human public key")?;
        let tp = token.map_or(std::ptr::null(), |t| t.0.as_ptr());
        // SAFETY: owned log, exclusive; token NULL or 3453 bytes; key
        // PK_BYTES; ids 32 bytes.
        check(unsafe {
            sys::zxv_w4_consent_check(
                self.raw.as_ptr(),
                tp,
                human_public_key.as_ptr(),
                n.agent_id.as_ptr(),
                n.action.as_ptr(),
                n.need,
                n.amount,
                n.now_ms,
            )
        })
    }

    /// Revokes `token` before use: later checks of it fail with
    /// `ZXV_E_CONSENT`. Needs no capability (it only takes authority away).
    pub fn revoke(&mut self, token: &ConsentToken, now_ms: u64) -> Result<()> {
        // SAFETY: owned log, exclusive; token 3453 bytes.
        check(unsafe { sys::zxv_w4_consent_revoke(self.raw.as_ptr(), token.0.as_ptr(), now_ms) })
    }

    /// Drops entries whose token expired at or before `now_ms`; returns how
    /// many remain.
    pub fn prune(&mut self, now_ms: u64) -> Result<u32> {
        let mut n = 0u32;
        // SAFETY: owned log, exclusive; out pointer valid.
        check(unsafe { sys::zxv_w4_consent_log_prune(self.raw.as_ptr(), now_ms, &mut n) })?;
        Ok(n)
    }
}

impl fmt::Debug for ConsentLog {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("ConsentLog")
    }
}

impl Drop for ConsentLog {
    fn drop(&mut self) {
        // SAFETY: owned handle destroyed once.
        unsafe { sys::zxv_w4_consent_log_destroy(self.raw.as_ptr()) }
    }
}

/// An envelope to seal ([`W4Agent::seal`]).
#[derive(Debug, Clone, Copy)]
pub struct W4Message<'m> {
    /// The kind.
    pub kind: W4MsgKind,
    /// The receiver's agent id.
    pub to: [u8; 32],
    /// Request id.
    pub req_id: u64,
    /// Chunk index.
    pub chunk: u32,
    /// A valid tool name.
    pub tool: &'m str,
    /// 0 or [`FLAG_FINAL`].
    pub flags: u32,
    /// At most [`MSG_MAX_PAYLOAD`] bytes.
    pub payload: &'m [u8],
    /// A token to attach, or `None`.
    pub consent: Option<&'m ConsentToken>,
}

/// A decoded envelope's header ([`decode_envelope`]).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct W4Envelope {
    /// The kind.
    pub kind: W4MsgKind,
    /// `FLAG_*` bits.
    pub flags: u32,
    /// The sender's agent id.
    pub from: [u8; 32],
    /// The receiver's agent id.
    pub to: [u8; 32],
    /// The sender's sequence number.
    pub seq: u64,
    /// The sender's timestamp (Unix ms).
    pub ts_ms: u64,
    /// Request id.
    pub req_id: u64,
    /// Chunk index.
    pub chunk: u32,
    /// Tool name.
    pub tool: String,
    /// Payload bytes.
    pub payload_len: u32,
}

/// Parses an envelope without checking its signature. `ZXV_E_FORMAT`:
/// malformed.
pub fn decode_envelope(env: &[u8]) -> Result<W4Envelope> {
    let (mut kind, mut flags, mut chunk, mut pl) = (0i32, 0u32, 0u32, 0u32);
    let (mut seq, mut ts, mut req) = (0u64, 0u64, 0u64);
    let (mut from, mut to) = ([0u8; 32], [0u8; 32]);
    let mut tool = [0 as c_char; 32];
    // SAFETY: env valid; outs sized as the header says.
    check(unsafe {
        sys::zxv_w4_env_decode(
            opt_ptr(env),
            env.len(),
            &mut kind,
            &mut flags,
            from.as_mut_ptr(),
            to.as_mut_ptr(),
            &mut seq,
            &mut ts,
            &mut req,
            &mut chunk,
            tool.as_mut_ptr(),
            &mut pl,
        )
    })?;
    Ok(W4Envelope {
        kind: W4MsgKind::from_raw(kind),
        flags,
        from,
        to,
        seq,
        ts_ms: ts,
        req_id: req,
        chunk,
        tool: c_name(&tool),
        payload_len: pl,
    })
}

/// An envelope's payload. `ZXV_E_FORMAT`: malformed.
pub fn envelope_payload(env: &[u8]) -> Result<Vec<u8>> {
    // SAFETY: env valid; R4 buffer.
    read_bytes(MSG_MAX_PAYLOAD, |b, c, l| unsafe {
        sys::zxv_w4_env_payload(opt_ptr(env), env.len(), b, c, l)
    })
}

/// The consent token attached to an envelope (`ZXV_E_NOT_FOUND`: none).
pub fn envelope_consent(env: &[u8]) -> Result<ConsentToken> {
    let mut t = ConsentToken([0u8; CONSENT_BYTES]);
    // SAFETY: env valid; out 3453 bytes.
    check(unsafe { sys::zxv_w4_env_consent(opt_ptr(env), env.len(), t.0.as_mut_ptr()) })?;
    Ok(t)
}

/// `Ok(())` if `sender_public_key` is the envelope's sender and its
/// signature verifies, else `ZXV_E_VERIFY`. Replay, timestamp and rate
/// checks are the receiver's job.
pub fn verify_envelope(env: &[u8], sender_public_key: &[u8]) -> Result<()> {
    need_len(sender_public_key, PK_BYTES, "sender public key")?;
    // SAFETY: env valid; key PK_BYTES bytes.
    check(unsafe { sys::zxv_w4_env_verify(opt_ptr(env), env.len(), sender_public_key.as_ptr()) })
}
