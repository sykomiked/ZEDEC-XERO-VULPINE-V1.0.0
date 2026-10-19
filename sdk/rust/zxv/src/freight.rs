// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Freight packets (ABI 1.5): 168 erasure-coded 21-byte rows, sets that
//! split and join larger payloads, and op streams.
//!
//! A freight carries up to 3360 payload bytes as 168 smart packets of 21
//! bytes: byte 0 is the row index 0..167, bytes 1..20 are row data. Rows
//! 0..k-1 hold the payload verbatim (zero padded); rows k..167 are Cauchy
//! Reed-Solomon parity over GF(2^8). ANY k distinct rows rebuild the
//! payload, so up to 168-k rows may be lost.
//!
//! The 64-byte [`FreightHeader`] travels separately and names the
//! payload's SHA-256. A larger payload is split into a SET of up to
//! [`MAX_SET`] freights sharing the freight id ([`split`], [`FreightJoin`]).
//! The payload may be an OP STREAM (LITERAL, COPY, CIDREF, SEED) that the
//! receiver expands; expansion checks the declared total against the
//! caller's limit before writing a byte.
//!
//! Everything here is pure: no capability, no context. [`FreightJoin`] and
//! [`FreightOpsWriter`] hold only their own memory; each is `Send` but not
//! `Sync` (mutating calls take `&mut self`).
//!
//! **Honest limits:** erasure coding only. A lost row is recovered; a
//! CORRUPTED row is detected (the payload SHA-256 fails, `ZXV_E_VERIFY`)
//! but not located or repaired: drop rows you distrust and decode again.
//! The header is neither protected by the code nor authenticated: sign it
//! if the sender matters. No network transport: moving packets is up to
//! you. Parity makes a freight larger than its payload (3528 wire bytes for
//! at most 3360). The op stream promises no compression: random data
//! encodes slightly LARGER than itself, COPY helps only data that repeats,
//! CIDREF only names bytes the receiver must already hold, and SEED only
//! reproduces the built-in generators. No information is created.
//!
//! ```
//! use zxv::freight::{decode, encode, FreightHeader, PACKET_BYTES};
//! let payload = b"cargo";
//! let h = FreightHeader::new(1, 4, 0, 0, payload)?;
//! let packets = encode(&h, payload)?;
//! // Keep only 4 rows (any 4): enough to rebuild.
//! let some: Vec<u8> = packets.chunks(PACKET_BYTES).skip(100).take(4).flatten().copied().collect();
//! assert_eq!(decode(&h, &some)?, payload);
//! # Ok::<(), zxv::Error>(())
//! ```

use std::ffi::c_void;
use std::fmt;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr::NonNull;

use zxv_sys as sys;

use crate::error::{check, Error, Result};
use crate::ffi::{non_null, opt_ptr, read_bytes};

/// One smart packet (21 bytes).
pub const PACKET_BYTES: usize = sys::ZXV_FREIGHT_PACKET_BYTES as usize;
/// Data bytes per row (20).
pub const ROW_PAYLOAD: usize = sys::ZXV_FREIGHT_ROW_PAYLOAD as usize;
/// Rows per freight (168).
pub const ROWS: usize = sys::ZXV_FREIGHT_ROWS as usize;
/// One encoded freight (3528 bytes).
pub const BYTES: usize = sys::ZXV_FREIGHT_BYTES as usize;
/// Largest payload (3360, at k = 168).
pub const MAX_PAYLOAD: usize = sys::ZXV_FREIGHT_MAX_PAYLOAD as usize;
/// Header size (64).
pub const HEADER_BYTES: usize = sys::ZXV_FREIGHT_HEADER_BYTES as usize;
/// Header version (1).
pub const VERSION: u8 = sys::ZXV_FREIGHT_VERSION as u8;
/// Freights in one set (16384).
pub const MAX_SET: u32 = sys::ZXV_FREIGHT_MAX_SET;
/// Largest set payload, `MAX_SET * MAX_PAYLOAD`.
pub const SET_MAX_BYTES: u64 = sys::ZXV_FREIGHT_SET_MAX_BYTES;
/// Header flag: the (set) payload is an op stream.
pub const FLAG_OPSTREAM: u8 = sys::ZXV_FREIGHT_F_OPSTREAM as u8;
/// Longest content id a CIDREF op may name.
pub const CID_MAX: usize = sys::ZXV_FREIGHT_CID_MAX as usize;
/// Longest SEED parameter block.
pub const GEN_PARAM_MAX: usize = sys::ZXV_FREIGHT_GEN_PARAM_MAX as usize;
/// Largest op stream or expansion handled (64 MiB).
pub const OPS_MAX: u64 = sys::ZXV_FREIGHT_OPS_MAX;

/// A built-in SEED generator.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum FreightGen {
    /// 8 bytes, a nonzero u64 LE seed; xorshift64* bytes LE.
    Xorshift,
    /// 1..32 bytes, the pattern, repeated.
    Repeat,
    /// 2 distinct bytes a, b: the Fibonacci word abaababaab...
    FibWord,
    /// 3 bytes: log2 width w (1..15), on, off.
    Sierpinski,
    /// A generator number this SDK does not know (passed through; the
    /// library refuses unknown ones with `ZXV_E_ARG`).
    Other(u8),
}

impl FreightGen {
    /// The ABI number.
    pub fn raw(self) -> u8 {
        (match self {
            FreightGen::Xorshift => sys::ZXV_FREIGHT_GEN_XORSHIFT,
            FreightGen::Repeat => sys::ZXV_FREIGHT_GEN_REPEAT,
            FreightGen::FibWord => sys::ZXV_FREIGHT_GEN_FIBWORD,
            FreightGen::Sierpinski => sys::ZXV_FREIGHT_GEN_SIERPINSKI,
            FreightGen::Other(n) => i32::from(n),
        }) as u8
    }

    /// The generator for ABI number `v`.
    pub fn from_raw(v: u8) -> FreightGen {
        match i32::from(v) {
            sys::ZXV_FREIGHT_GEN_XORSHIFT => FreightGen::Xorshift,
            sys::ZXV_FREIGHT_GEN_REPEAT => FreightGen::Repeat,
            sys::ZXV_FREIGHT_GEN_FIBWORD => FreightGen::FibWord,
            sys::ZXV_FREIGHT_GEN_SIERPINSKI => FreightGen::Sierpinski,
            _ => FreightGen::Other(v),
        }
    }
}

/// A serialized 64-byte freight header: "ZXFH", version, k, flags, payload
/// length, seq, seq count, freight id, expand limit and the payload's
/// SHA-256. A `Copy` value; compare with `==`.
#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct FreightHeader(pub [u8; HEADER_BYTES]);

/// The fields of a [`FreightHeader`].
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct FreightHeaderFields {
    /// Always [`VERSION`] (ignored by [`serialize`](Self::serialize)).
    pub version: u8,
    /// Data rows, 1..=168.
    pub k: u8,
    /// 0 or [`FLAG_OPSTREAM`].
    pub flags: u8,
    /// Bytes in this freight, at most k*20.
    pub payload_len: u16,
    /// Member index in its set.
    pub seq: u16,
    /// Members in the set, 1..=16384.
    pub seq_count: u16,
    /// Shared by every member of a set.
    pub freight_id: u64,
    /// 0 unless flags has [`FLAG_OPSTREAM`].
    pub expand_limit: u64,
    /// SHA-256 of this freight's payload.
    pub payload_sha256: [u8; 32],
}

impl FreightHeader {
    /// The header of a single freight (seq 0 of 1) carrying `payload` with
    /// `k` data rows (1..=168); it hashes the payload. `expand_limit` must
    /// be 0 unless `flags` has [`FLAG_OPSTREAM`]. `ZXV_E_TOO_LONG` when the
    /// payload is over k*20 bytes.
    pub fn new(
        freight_id: u64,
        k: u8,
        flags: u8,
        expand_limit: u64,
        payload: &[u8],
    ) -> Result<FreightHeader> {
        let mut h = [0u8; HEADER_BYTES];
        // SAFETY: payload valid; out has 64 bytes.
        check(unsafe {
            sys::zxv_freight_header_init(
                freight_id,
                k,
                flags,
                expand_limit,
                opt_ptr(payload),
                payload.len(),
                h.as_mut_ptr(),
            )
        })?;
        Ok(FreightHeader(h))
    }

    /// Parses and checks the header (`ZXV_E_FORMAT` for a bad magic,
    /// nonzero reserved bytes or a field that fails the check).
    pub fn fields(&self) -> Result<FreightHeaderFields> {
        let mut f = FreightHeaderFields::default();
        // SAFETY: header 64 bytes; out pointers valid; sha has 32 bytes.
        check(unsafe {
            sys::zxv_freight_header_parse(
                self.0.as_ptr(),
                &mut f.version,
                &mut f.k,
                &mut f.flags,
                &mut f.payload_len,
                &mut f.seq,
                &mut f.seq_count,
                &mut f.freight_id,
                &mut f.expand_limit,
                f.payload_sha256.as_mut_ptr(),
            )
        })?;
        Ok(f)
    }

    /// `Ok(())` if the header is well formed, else `ZXV_E_FORMAT`.
    pub fn check(&self) -> Result<()> {
        // SAFETY: header 64 bytes.
        check(unsafe { sys::zxv_freight_header_check(self.0.as_ptr()) })
    }
}

impl fmt::Debug for FreightHeader {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self.fields() {
            Ok(x) => write!(f, "FreightHeader({x:?})"),
            Err(_) => write!(f, "FreightHeader(<invalid> {})", crate::ffi::hex(&self.0)),
        }
    }
}

impl FreightHeaderFields {
    /// Builds a header from these fields. A field that fails the header
    /// check is `ZXV_E_ARG`.
    pub fn serialize(&self) -> Result<FreightHeader> {
        let mut h = [0u8; HEADER_BYTES];
        // SAFETY: sha has 32 bytes; out has 64 bytes.
        check(unsafe {
            sys::zxv_freight_header_serialize(
                self.k,
                self.flags,
                self.payload_len,
                self.seq,
                self.seq_count,
                self.freight_id,
                self.expand_limit,
                self.payload_sha256.as_ptr(),
                h.as_mut_ptr(),
            )
        })?;
        Ok(FreightHeader(h))
    }
}

/// The 168 smart packets (3528 bytes, rows in order) of the freight `h`
/// describes. `payload` must be exactly what the header was built for: a
/// length mismatch is `ZXV_E_ARG`, a hash mismatch `ZXV_E_VERIFY`.
pub fn encode(h: &FreightHeader, payload: &[u8]) -> Result<Vec<u8>> {
    let mut out = vec![0u8; BYTES];
    // SAFETY: header 64 bytes; payload valid; out has BYTES bytes.
    check(unsafe {
        sys::zxv_freight_encode(
            h.0.as_ptr(),
            opt_ptr(payload),
            payload.len(),
            out.as_mut_ptr(),
        )
    })?;
    Ok(out)
}

/// Rebuilds the payload from whatever packets arrived (`len/21` packets of
/// 21 bytes, any order; duplicates and rows above 167 are ignored).
/// `ZXV_E_ERASURE`: fewer than k distinct rows (wait for more and decode
/// again). `ZXV_E_VERIFY`: the rebuilt payload fails the header's SHA-256.
pub fn decode(h: &FreightHeader, packets: &[u8]) -> Result<Vec<u8>> {
    // SAFETY: header 64 bytes; packets valid; R4 buffer.
    read_bytes(MAX_PAYLOAD, |b, c, l| unsafe {
        sys::zxv_freight_decode(h.0.as_ptr(), opt_ptr(packets), packets.len(), b, c, l)
    })
}

/// How many freights carry `n` bytes at `k` (0 bytes still needs one).
/// `ZXV_E_TOO_LONG` when more than [`MAX_SET`] are needed.
pub fn set_count(n: u64, k: u8) -> Result<u32> {
    let mut c = 0u32;
    // SAFETY: pure; out pointer valid.
    check(unsafe { sys::zxv_freight_set_count(n, k, &mut c) })?;
    Ok(c)
}

/// Member `seq` of the set that carries `data`: its header and its 168
/// packets. `ZXV_E_RANGE` when `seq` is not below the set count.
pub fn split(
    freight_id: u64,
    k: u8,
    flags: u8,
    expand_limit: u64,
    data: &[u8],
    seq: u32,
) -> Result<(FreightHeader, Vec<u8>)> {
    let mut h = [0u8; HEADER_BYTES];
    let mut out = vec![0u8; BYTES];
    // SAFETY: data valid; header 64 bytes; out has BYTES bytes.
    check(unsafe {
        sys::zxv_freight_split(
            freight_id,
            k,
            flags,
            expand_limit,
            opt_ptr(data),
            data.len(),
            seq,
            h.as_mut_ptr(),
            out.as_mut_ptr(),
        )
    })?;
    Ok((FreightHeader(h), out))
}

/// A join's progress ([`FreightJoin::status`]).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FreightJoinStatus {
    /// Members accepted so far.
    pub received: u32,
    /// Members in the set; 0 until the first is accepted.
    pub count: u32,
    /// Every member arrived.
    pub complete: bool,
    /// The joined length once complete, else 0.
    pub total: u64,
}

/// Reassembles one set, member by member, in any order. The first member
/// accepted fixes the set (id, k, flags, count, expand limit). `Send`, not
/// `Sync`.
pub struct FreightJoin {
    raw: NonNull<sys::zxv_freight_join>,
}

// SAFETY: owns its memory; not thread-safe in C, so no Sync and mutation
// takes &mut self.
unsafe impl Send for FreightJoin {}

impl FreightJoin {
    /// A join with a buffer of `capacity` bytes (0..=[`SET_MAX_BYTES`], else
    /// `ZXV_E_RANGE`).
    pub fn new(capacity: u64) -> Result<FreightJoin> {
        let mut p = std::ptr::null_mut();
        // SAFETY: out pointer valid.
        check(unsafe { sys::zxv_freight_join_create(capacity, &mut p) })?;
        Ok(FreightJoin {
            raw: non_null(p, "freight join")?,
        })
    }

    /// Decodes one member into its place. A member already received is a
    /// no-op. `ZXV_E_STATE`: another set, or a non-last member not full.
    /// `ZXV_E_CAPACITY`: the set cannot fit. `ZXV_E_ERASURE`,
    /// `ZXV_E_VERIFY`, `ZXV_E_FORMAT` as for [`decode`]; a failed member is
    /// not recorded, so add it again when more rows arrive.
    pub fn add(&mut self, h: &FreightHeader, packets: &[u8]) -> Result<()> {
        // SAFETY: owned join, exclusive; header 64 bytes; packets valid.
        check(unsafe {
            sys::zxv_freight_join_add(
                self.raw.as_ptr(),
                h.0.as_ptr(),
                opt_ptr(packets),
                packets.len(),
            )
        })
    }

    /// The join's progress.
    pub fn status(&self) -> Result<FreightJoinStatus> {
        let (mut r, mut c, mut done, mut t) = (0u32, 0u32, 0i32, 0u64);
        // SAFETY: owned join; out pointers valid.
        check(unsafe {
            sys::zxv_freight_join_status(self.raw.as_ptr(), &mut r, &mut c, &mut done, &mut t)
        })?;
        Ok(FreightJoinStatus {
            received: r,
            count: c,
            complete: done != 0,
            total: t,
        })
    }

    /// The joined bytes. `ZXV_E_STATE` until every member arrived.
    pub fn result(&self) -> Result<Vec<u8>> {
        let p = self.raw.as_ptr();
        // SAFETY: owned join; R4 buffer.
        read_bytes(256, |b, c, l| unsafe {
            sys::zxv_freight_join_result(p, b, c, l)
        })
    }
}

impl fmt::Debug for FreightJoin {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("FreightJoin")
            .field("status", &self.status().ok())
            .finish()
    }
}

impl Drop for FreightJoin {
    fn drop(&mut self) {
        // SAFETY: owned handle destroyed once.
        unsafe { sys::zxv_freight_join_destroy(self.raw.as_ptr()) }
    }
}

/// An op stream's declared total and whether it carries an output SHA-256,
/// without expanding it. `ZXV_E_FORMAT`: a bad header.
pub fn ops_peek(ops: &[u8]) -> Result<(u64, bool)> {
    let (mut t, mut s) = (0u64, 0i32);
    // SAFETY: ops valid; out pointers valid.
    check(unsafe { sys::zxv_freight_ops_peek(opt_ptr(ops), ops.len(), &mut t, &mut s) })?;
    Ok((t, s != 0))
}

/// The resolver state handed to C: the closure and whether it panicked.
struct Resolver<F> {
    f: F,
    panicked: bool,
}

/// The CIDREF trampoline: `user` is a `&mut Resolver<F>`. A panic never
/// unwinds into C; it is caught, recorded and reported as not resolved
/// (C collapses every resolver failure to `ZXV_E_NOT_FOUND`; the caller
/// turns a recorded panic into `ZXV_E_INTERNAL`).
unsafe extern "C" fn resolve_trampoline<F>(
    user: *mut c_void,
    cid: *const u8,
    cid_len: usize,
    out: *mut u8,
    len: u64,
) -> i32
where
    F: FnMut(&[u8], &mut [u8]) -> bool,
{
    let r = catch_unwind(AssertUnwindSafe(|| {
        // SAFETY: `user` is the &mut Resolver<F> passed for this call.
        let f = unsafe { &mut (*user.cast::<Resolver<F>>()).f };
        let c: &[u8] = if cid.is_null() || cid_len == 0 {
            &[]
        } else {
            // SAFETY: C passes cid_len readable bytes for this call.
            unsafe { std::slice::from_raw_parts(cid, cid_len) }
        };
        let o: &mut [u8] = match usize::try_from(len) {
            Ok(n) if !out.is_null() && n > 0 => {
                // SAFETY: C passes len writable bytes for this call.
                unsafe { std::slice::from_raw_parts_mut(out, n) }
            }
            _ => &mut [],
        };
        f(c, o)
    }));
    match r {
        Ok(true) => sys::ZXV_OK,
        Ok(false) => sys::ZXV_E_NOT_FOUND,
        Err(_) => {
            // SAFETY: as above; the closure borrow has ended.
            unsafe { (*user.cast::<Resolver<F>>()).panicked = true };
            sys::ZXV_E_INTERNAL
        }
    }
}

/// Expands an op stream, accepting at most `limit` bytes (at most
/// [`OPS_MAX`], else `ZXV_E_RANGE`): a declared total above `limit` is
/// `ZXV_E_TOO_LONG` before anything is produced. A CIDREF fails with
/// `ZXV_E_NOT_FOUND` (use [`ops_expand_with`] to resolve them).
/// `ZXV_E_FORMAT`: a malformed stream. `ZXV_E_VERIFY`: the output fails the
/// stream's SHA-256.
pub fn ops_expand(ops: &[u8], limit: u64) -> Result<Vec<u8>> {
    let hint = expand_hint(ops, limit);
    // SAFETY: ops valid; no resolver; R4 buffer.
    read_bytes(hint, |b, c, l| unsafe {
        sys::zxv_freight_ops_expand(
            opt_ptr(ops),
            ops.len(),
            limit,
            None,
            std::ptr::null_mut(),
            b,
            c,
            l,
        )
    })
}

/// [`ops_expand`] with a CIDREF resolver: `resolve(cid, out)` fills `out`
/// (whose length is the op's length) with the bytes content id `cid` names
/// and returns true, or returns false if it cannot (`ZXV_E_NOT_FOUND`). It
/// runs on the calling thread during the call only. A panic in it is
/// caught (it never unwinds through C) and the call fails with
/// `ZXV_E_INTERNAL`.
pub fn ops_expand_with<F>(ops: &[u8], limit: u64, resolve: F) -> Result<Vec<u8>>
where
    F: FnMut(&[u8], &mut [u8]) -> bool,
{
    let hint = expand_hint(ops, limit);
    let mut state = Resolver {
        f: resolve,
        panicked: false,
    };
    let user = (&mut state as *mut Resolver<F>).cast::<c_void>();
    // SAFETY: ops valid; `state` outlives the call; R4 buffer.
    let r = read_bytes(hint, |b, c, l| unsafe {
        sys::zxv_freight_ops_expand(
            opt_ptr(ops),
            ops.len(),
            limit,
            Some(resolve_trampoline::<F>),
            user,
            b,
            c,
            l,
        )
    });
    if state.panicked {
        return Err(Error::with_detail(
            sys::ZXV_E_INTERNAL,
            "freight: the CIDREF resolver panicked",
        ));
    }
    r
}

fn expand_hint(ops: &[u8], limit: u64) -> usize {
    match ops_peek(ops) {
        Ok((t, _)) if t <= limit && t <= OPS_MAX => t as usize,
        _ => 1,
    }
}

/// Runs one SEED generator directly and returns `n` bytes. `ZXV_E_ARG`:
/// params over 32 bytes, or params the generator refuses.
pub fn gen_run(gen: FreightGen, params: &[u8], n: usize) -> Result<Vec<u8>> {
    let mut out = vec![0u8; n];
    // SAFETY: params valid; out has n bytes.
    check(unsafe {
        sys::zxv_freight_gen_run(
            gen.raw(),
            opt_ptr(params),
            params.len(),
            out.as_mut_ptr(),
            n,
        )
    })?;
    Ok(out)
}

/// Encodes `input` as a LITERAL/COPY op stream (greedy LZ77,
/// deterministic), with the output SHA-256 when `with_sha`. The result may
/// be LARGER than the input. `ZXV_E_TOO_LONG` over [`OPS_MAX`].
pub fn ops_encode(input: &[u8], with_sha: bool) -> Result<Vec<u8>> {
    // SAFETY: input valid; R4 buffer.
    read_bytes(input.len() / 2 + 64, |b, c, l| unsafe {
        sys::zxv_freight_ops_encode(opt_ptr(input), input.len(), i32::from(with_sha), b, c, l)
    })
}

/// The worst-case encoded size of `n` input bytes.
pub fn ops_bound(n: u64, with_sha: bool) -> Result<u64> {
    let mut b = 0u64;
    // SAFETY: pure; out pointer valid.
    check(unsafe { sys::zxv_freight_ops_bound(n, i32::from(with_sha), &mut b) })?;
    Ok(b)
}

/// Builds an op stream op by op. The first error of a put is sticky: every
/// later put and [`finish`](Self::finish) return it again. `Send`, not
/// `Sync`.
pub struct FreightOpsWriter {
    raw: NonNull<sys::zxv_freight_ops_writer>,
}

// SAFETY: owns its memory; not thread-safe in C, so no Sync and mutation
// takes &mut self.
unsafe impl Send for FreightOpsWriter {}

impl FreightOpsWriter {
    /// A writer for a stream whose ops must produce `declared_total` bytes,
    /// into a buffer of `max_bytes` (14, or 46 with a SHA, up to
    /// [`OPS_MAX`]). `sha256`, if given, is the output's SHA-256, which
    /// expansion will verify.
    pub fn new(
        declared_total: u64,
        sha256: Option<&[u8; 32]>,
        max_bytes: u64,
    ) -> Result<FreightOpsWriter> {
        let sp = sha256.map_or(std::ptr::null(), |s| s.as_ptr());
        let mut p = std::ptr::null_mut();
        // SAFETY: sha NULL or 32 bytes; out pointer valid.
        check(unsafe {
            sys::zxv_freight_ops_writer_create(declared_total, sp, max_bytes, &mut p)
        })?;
        Ok(FreightOpsWriter {
            raw: non_null(p, "freight ops writer")?,
        })
    }

    /// Appends a LITERAL op carrying `b`. `ZXV_E_ARG` for an empty `b` or
    /// more than is left of the declared total; `ZXV_E_CAPACITY` when full.
    pub fn put_literal(&mut self, b: &[u8]) -> Result<()> {
        // SAFETY: owned writer, exclusive; b valid.
        check(unsafe { sys::zxv_freight_ops_put_literal(self.raw.as_ptr(), opt_ptr(b), b.len()) })
    }

    /// Appends a COPY op: `n` bytes starting `distance` bytes back (overlap
    /// allowed). `ZXV_E_ARG` for a distance of 0 or beyond what was
    /// produced.
    pub fn put_copy(&mut self, distance: u64, n: u64) -> Result<()> {
        // SAFETY: owned writer, exclusive.
        check(unsafe { sys::zxv_freight_ops_put_copy(self.raw.as_ptr(), distance, n) })
    }

    /// Appends a CIDREF op: `n` bytes the receiver resolves by content id
    /// (1..=[`CID_MAX`] bytes).
    pub fn put_cidref(&mut self, cid: &[u8], n: u64) -> Result<()> {
        // SAFETY: owned writer, exclusive; cid valid.
        check(unsafe {
            sys::zxv_freight_ops_put_cidref(self.raw.as_ptr(), opt_ptr(cid), cid.len(), n)
        })
    }

    /// Appends a SEED op: `n` bytes of generator `gen` with `params`.
    pub fn put_seed(&mut self, gen: FreightGen, params: &[u8], n: u64) -> Result<()> {
        // SAFETY: owned writer, exclusive; params valid.
        check(unsafe {
            sys::zxv_freight_ops_put_seed(
                self.raw.as_ptr(),
                gen.raw(),
                opt_ptr(params),
                params.len(),
                n,
            )
        })
    }

    /// The stream so far. `ZXV_E_STATE` while the ops produce fewer bytes
    /// than declared, or the sticky error of an earlier put. The writer
    /// stays usable.
    pub fn finish(&self) -> Result<Vec<u8>> {
        let p = self.raw.as_ptr();
        // SAFETY: owned writer; R4 buffer.
        read_bytes(256, |b, c, l| unsafe {
            sys::zxv_freight_ops_writer_finish(p, b, c, l)
        })
    }
}

impl fmt::Debug for FreightOpsWriter {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("FreightOpsWriter")
    }
}

impl Drop for FreightOpsWriter {
    fn drop(&mut self) {
        // SAFETY: owned handle destroyed once.
        unsafe { sys::zxv_freight_ops_writer_destroy(self.raw.as_ptr()) }
    }
}
