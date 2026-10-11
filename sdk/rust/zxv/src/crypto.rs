// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Hashes, randomness, secret-key storage, ML-KEM-1024, ML-DSA-87 and
//! Ed25519.
//!
//! NOT FIPS 140-3 validated.

use std::fmt;

use zxv_sys as sys;

use crate::capability::Capability;
use crate::error::{check, Error, Result};

/// SHA3-256, SHAKE256 and Keccak-256 (pure; no capability).
#[derive(Debug, Clone, Copy)]
pub struct Hashes;

impl Hashes {
    /// SHA3-256 (FIPS 202).
    pub fn sha3_256(data: &[u8]) -> [u8; 32] {
        let mut out = [0u8; 32];
        // SAFETY: data valid for its length; 32-byte output.
        let rc = unsafe { sys::zxv_sha3_256(data.as_ptr(), data.len(), out.as_mut_ptr()) };
        assert_eq!(rc, sys::ZXV_OK, "zxv_sha3_256 failed on valid input");
        out
    }

    /// SHAKE256 with `out_len` bytes of output.
    pub fn shake256(data: &[u8], out_len: usize) -> Vec<u8> {
        let mut out = vec![0u8; out_len];
        // SAFETY: data valid; out valid for out_len bytes.
        let rc = unsafe { sys::zxv_shake256(data.as_ptr(), data.len(), out.as_mut_ptr(), out_len) };
        assert_eq!(rc, sys::ZXV_OK, "zxv_shake256 failed on valid input");
        out
    }

    /// Ethereum's Keccak-256 (pre-FIPS padding).
    pub fn keccak256(data: &[u8]) -> [u8; 32] {
        let mut out = [0u8; 32];
        // SAFETY: data valid for its length; 32-byte output.
        let rc = unsafe { sys::zxv_keccak256(data.as_ptr(), data.len(), out.as_mut_ptr()) };
        assert_eq!(rc, sys::ZXV_OK, "zxv_keccak256 failed on valid input");
        out
    }
}

/// Fills `buf` from the OS CSPRNG.
pub fn random(buf: &mut [u8]) -> Result<()> {
    // SAFETY: buf valid for its length.
    check(unsafe { sys::zxv_random(buf.as_mut_ptr(), buf.len()) })
}

/// Secret bytes (a secret key or a shared secret), wiped with
/// `zxv_secure_zero` when dropped.
///
/// The bytes live in one heap allocation that is never reallocated or
/// copied by the SDK. They are not exposed except through
/// [`export_bytes_unsafe`](Self::export_bytes_unsafe), whose copy you must
/// wipe yourself. `Debug` prints only the length.
pub struct SecretKey {
    buf: Box<[u8]>,
}

impl SecretKey {
    fn zeroed(len: usize) -> SecretKey {
        SecretKey {
            buf: vec![0u8; len].into_boxed_slice(),
        }
    }

    /// Takes a copy of `bytes` (e.g. a key loaded from a vault). The caller
    /// should wipe its own copy.
    pub fn from_bytes(bytes: &[u8]) -> SecretKey {
        let mut k = SecretKey::zeroed(bytes.len());
        k.buf.copy_from_slice(bytes);
        k
    }

    /// Length in bytes.
    pub fn len(&self) -> usize {
        self.buf.len()
    }

    /// True for a zero-length secret.
    pub fn is_empty(&self) -> bool {
        self.buf.is_empty()
    }

    /// A plain copy of the secret. **Defeats the wiping**: the returned
    /// vector is ordinary memory that is not zeroed on drop.
    pub fn export_bytes_unsafe(&self) -> Vec<u8> {
        self.buf.to_vec()
    }

    /// Constant-time equality with another secret.
    pub fn ct_eq(&self, other: &SecretKey) -> bool {
        self.buf.len() == other.buf.len()
            && self
                .buf
                .iter()
                .zip(other.buf.iter())
                .fold(0u8, |a, (x, y)| a | (x ^ y))
                == 0
    }

    pub(crate) fn as_ptr(&self) -> *const u8 {
        self.buf.as_ptr()
    }

    pub(crate) fn as_mut_ptr(&mut self) -> *mut u8 {
        self.buf.as_mut_ptr()
    }

    fn require_len(&self, n: usize, what: &str) -> Result<()> {
        if self.buf.len() != n {
            return Err(Error::with_detail(
                sys::ZXV_E_ARG,
                format!("{what} must be {n} bytes"),
            ));
        }
        Ok(())
    }
}

impl Drop for SecretKey {
    fn drop(&mut self) {
        // SAFETY: buf is valid for its length.
        unsafe { sys::zxv_secure_zero(self.buf.as_mut_ptr().cast(), self.buf.len()) };
    }
}

impl fmt::Debug for SecretKey {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "SecretKey({} bytes, redacted)", self.buf.len())
    }
}

fn need(bytes: &[u8], n: usize, what: &str) -> Result<()> {
    if bytes.len() != n {
        return Err(Error::with_detail(
            sys::ZXV_E_ARG,
            format!("{what} must be {n} bytes, got {}", bytes.len()),
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

/// Sizes of the FIPS 203 / FIPS 204 / RFC 8032 objects.
pub mod sizes {
    /// ML-KEM-1024 encapsulation key.
    pub const MLKEM1024_EK: usize = zxv_sys::ZXV_MLKEM1024_EK_BYTES as usize;
    /// ML-KEM-1024 decapsulation key.
    pub const MLKEM1024_DK: usize = zxv_sys::ZXV_MLKEM1024_DK_BYTES as usize;
    /// ML-KEM-1024 ciphertext.
    pub const MLKEM1024_CT: usize = zxv_sys::ZXV_MLKEM1024_CT_BYTES as usize;
    /// ML-KEM shared secret.
    pub const MLKEM_SS: usize = zxv_sys::ZXV_MLKEM_SS_BYTES as usize;
    /// ML-DSA-87 public key.
    pub const MLDSA87_PK: usize = zxv_sys::ZXV_MLDSA87_PK_BYTES as usize;
    /// ML-DSA-87 secret key.
    pub const MLDSA87_SK: usize = zxv_sys::ZXV_MLDSA87_SK_BYTES as usize;
    /// ML-DSA-87 signature.
    pub const MLDSA87_SIG: usize = zxv_sys::ZXV_MLDSA87_SIG_BYTES as usize;
    /// Ed25519 public key, secret key (seed) and signature.
    pub const ED25519_PK: usize = 32;
    /// Ed25519 secret key (the RFC 8032 seed).
    pub const ED25519_SK: usize = 32;
    /// Ed25519 signature.
    pub const ED25519_SIG: usize = 64;
}

/// FIPS 203 ML-KEM-1024. Needs [`Caps::CRYPTO_KEM`](crate::Caps::CRYPTO_KEM).
#[derive(Debug, Clone, Copy)]
pub struct MlKem1024<'a> {
    cap: &'a Capability<'a>,
}

impl<'a> MlKem1024<'a> {
    /// ML-KEM-1024 through `cap`.
    pub fn new(cap: &'a Capability<'a>) -> MlKem1024<'a> {
        MlKem1024 { cap }
    }

    /// A fresh key pair: (encapsulation key, decapsulation key).
    pub fn keygen(&self) -> Result<(Vec<u8>, SecretKey)> {
        let mut ek = vec![0u8; sizes::MLKEM1024_EK];
        let mut dk = SecretKey::zeroed(sizes::MLKEM1024_DK);
        self.cap
            .call(|c| unsafe { sys::zxv_mlkem1024_keygen(c, ek.as_mut_ptr(), dk.as_mut_ptr()) })?;
        Ok((ek, dk))
    }

    /// Deterministic key pair from `d` and `z` (FIPS 203 KeyGen_internal),
    /// for known-answer tests.
    pub fn keygen_derand(&self, d: &[u8; 32], z: &[u8; 32]) -> Result<(Vec<u8>, SecretKey)> {
        let mut ek = vec![0u8; sizes::MLKEM1024_EK];
        let mut dk = SecretKey::zeroed(sizes::MLKEM1024_DK);
        self.cap.call(|c| unsafe {
            sys::zxv_mlkem1024_keygen_derand(
                c,
                d.as_ptr(),
                z.as_ptr(),
                ek.as_mut_ptr(),
                dk.as_mut_ptr(),
            )
        })?;
        Ok((ek, dk))
    }

    /// Encapsulates to `ek`: (ciphertext, shared secret). A malformed key
    /// is `ZXV_E_FORMAT`.
    pub fn encapsulate(&self, ek: &[u8]) -> Result<(Vec<u8>, SecretKey)> {
        need(ek, sizes::MLKEM1024_EK, "encapsulation key")?;
        let mut ct = vec![0u8; sizes::MLKEM1024_CT];
        let mut ss = SecretKey::zeroed(sizes::MLKEM_SS);
        self.cap.call(|c| unsafe {
            sys::zxv_mlkem1024_encaps(c, ek.as_ptr(), ct.as_mut_ptr(), ss.as_mut_ptr())
        })?;
        Ok((ct, ss))
    }

    /// Deterministic encapsulation with message `m`, for known-answer tests.
    pub fn encapsulate_derand(&self, ek: &[u8], m: &[u8; 32]) -> Result<(Vec<u8>, SecretKey)> {
        need(ek, sizes::MLKEM1024_EK, "encapsulation key")?;
        let mut ct = vec![0u8; sizes::MLKEM1024_CT];
        let mut ss = SecretKey::zeroed(sizes::MLKEM_SS);
        self.cap.call(|c| unsafe {
            sys::zxv_mlkem1024_encaps_derand(
                c,
                ek.as_ptr(),
                m.as_ptr(),
                ct.as_mut_ptr(),
                ss.as_mut_ptr(),
            )
        })?;
        Ok((ct, ss))
    }

    /// Decapsulates `ct` with `dk` and returns the shared secret.
    pub fn decapsulate(&self, dk: &SecretKey, ct: &[u8]) -> Result<SecretKey> {
        dk.require_len(sizes::MLKEM1024_DK, "decapsulation key")?;
        need(ct, sizes::MLKEM1024_CT, "ciphertext")?;
        let mut ss = SecretKey::zeroed(sizes::MLKEM_SS);
        self.cap.call(|c| unsafe {
            sys::zxv_mlkem1024_decaps(c, dk.as_ptr(), ct.as_ptr(), ss.as_mut_ptr())
        })?;
        Ok(ss)
    }
}

/// FIPS 204 ML-DSA-87 signatures. Needs [`Caps::CRYPTO_SIGN`](crate::Caps::CRYPTO_SIGN).
///
/// `context` is the FIPS 204 context string (at most 255 bytes, may be
/// empty).
#[derive(Debug, Clone, Copy)]
pub struct MlDsa87<'a> {
    cap: &'a Capability<'a>,
}

impl<'a> MlDsa87<'a> {
    /// ML-DSA-87 through `cap`.
    pub fn new(cap: &'a Capability<'a>) -> MlDsa87<'a> {
        MlDsa87 { cap }
    }

    /// A fresh key pair: (public key, secret key).
    pub fn keygen(&self) -> Result<(Vec<u8>, SecretKey)> {
        let mut pk = vec![0u8; sizes::MLDSA87_PK];
        let mut sk = SecretKey::zeroed(sizes::MLDSA87_SK);
        self.cap
            .call(|c| unsafe { sys::zxv_mldsa87_keygen(c, pk.as_mut_ptr(), sk.as_mut_ptr()) })?;
        Ok((pk, sk))
    }

    /// A key pair from a 32-byte seed.
    pub fn keygen_from_seed(&self, seed: &[u8; 32]) -> Result<(Vec<u8>, SecretKey)> {
        let mut pk = vec![0u8; sizes::MLDSA87_PK];
        let mut sk = SecretKey::zeroed(sizes::MLDSA87_SK);
        self.cap.call(|c| unsafe {
            sys::zxv_mldsa87_keygen_seed(c, seed.as_ptr(), pk.as_mut_ptr(), sk.as_mut_ptr())
        })?;
        Ok((pk, sk))
    }

    /// Hedged signature (fresh randomness each call).
    pub fn sign(&self, sk: &SecretKey, msg: &[u8], context: &[u8]) -> Result<Vec<u8>> {
        self.sign_with(sk, msg, context, sys::zxv_mldsa87_sign)
    }

    /// Deterministic signature (same input, same signature).
    pub fn sign_deterministic(
        &self,
        sk: &SecretKey,
        msg: &[u8],
        context: &[u8],
    ) -> Result<Vec<u8>> {
        self.sign_with(sk, msg, context, sys::zxv_mldsa87_sign_deterministic)
    }

    /// `Ok(())` if `sig` is valid, `Refused(VerificationFailed)` if not.
    pub fn verify(&self, pk: &[u8], msg: &[u8], context: &[u8], sig: &[u8]) -> Result<()> {
        need(pk, sizes::MLDSA87_PK, "public key")?;
        need(sig, sizes::MLDSA87_SIG, "signature")?;
        self.cap.call(|c| unsafe {
            sys::zxv_mldsa87_verify(
                c,
                pk.as_ptr(),
                opt_ptr(msg),
                msg.len(),
                opt_ptr(context),
                context.len(),
                sig.as_ptr(),
            )
        })
    }

    #[allow(clippy::type_complexity)]
    fn sign_with(
        &self,
        sk: &SecretKey,
        msg: &[u8],
        context: &[u8],
        f: unsafe extern "C" fn(
            *const sys::zxv_cap,
            *const u8,
            *const u8,
            usize,
            *const u8,
            usize,
            *mut u8,
        ) -> i32,
    ) -> Result<Vec<u8>> {
        sk.require_len(sizes::MLDSA87_SK, "secret key")?;
        let mut sig = vec![0u8; sizes::MLDSA87_SIG];
        self.cap.call(|c| unsafe {
            f(
                c,
                sk.as_ptr(),
                opt_ptr(msg),
                msg.len(),
                opt_ptr(context),
                context.len(),
                sig.as_mut_ptr(),
            )
        })?;
        Ok(sig)
    }
}

/// RFC 8032 Ed25519. Needs [`Caps::CRYPTO_SIGN`](crate::Caps::CRYPTO_SIGN).
#[derive(Debug, Clone, Copy)]
pub struct Ed25519<'a> {
    cap: &'a Capability<'a>,
}

impl<'a> Ed25519<'a> {
    /// Ed25519 through `cap`.
    pub fn new(cap: &'a Capability<'a>) -> Ed25519<'a> {
        Ed25519 { cap }
    }

    /// A fresh key pair: (public key, secret seed).
    pub fn keygen(&self) -> Result<([u8; 32], SecretKey)> {
        let mut pk = [0u8; 32];
        let mut sk = SecretKey::zeroed(32);
        self.cap
            .call(|c| unsafe { sys::zxv_ed25519_keygen(c, pk.as_mut_ptr(), sk.as_mut_ptr()) })?;
        Ok((pk, sk))
    }

    /// The public key of a secret seed.
    pub fn public_key(&self, sk: &SecretKey) -> Result<[u8; 32]> {
        sk.require_len(32, "Ed25519 secret key")?;
        let mut pk = [0u8; 32];
        self.cap
            .call(|c| unsafe { sys::zxv_ed25519_public(c, sk.as_ptr(), pk.as_mut_ptr()) })?;
        Ok(pk)
    }

    /// Signs `msg`.
    pub fn sign(&self, sk: &SecretKey, msg: &[u8]) -> Result<[u8; 64]> {
        sk.require_len(32, "Ed25519 secret key")?;
        let mut sig = [0u8; 64];
        self.cap.call(|c| unsafe {
            sys::zxv_ed25519_sign(c, sk.as_ptr(), opt_ptr(msg), msg.len(), sig.as_mut_ptr())
        })?;
        Ok(sig)
    }

    /// `Ok(())` if valid, `Refused(VerificationFailed)` if not.
    pub fn verify(&self, pk: &[u8; 32], msg: &[u8], sig: &[u8; 64]) -> Result<()> {
        self.cap.call(|c| unsafe {
            sys::zxv_ed25519_verify(c, pk.as_ptr(), opt_ptr(msg), msg.len(), sig.as_ptr())
        })
    }
}
