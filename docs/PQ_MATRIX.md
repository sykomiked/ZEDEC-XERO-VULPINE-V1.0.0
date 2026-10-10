<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# The ZXV post-quantum matrix

The post-quantum matrix is ZXV's layered key exchange and dual signature
scheme. It lives in `kernel/src/pqsec/pq_matrix.{h,c}` and is tested by
`kernel/src/pqsec/test_pq_matrix.c`. The tests check it against NIST ACVP
vectors, the official HQC v5.0.0 known-answer tests, and RFC 7748. The design,
the combiner, the wire format and the limits are in the header comment of
`pq_matrix.h`. This document covers what each level is for and what it costs.

## 1. Levels

| Level | Key exchange | Signatures |
|---|---|---|
| STANDARD | ML-KEM-768 + X25519 | ML-DSA-65 |
| HIGH | ML-KEM-1024 + X25519 | ML-DSA-87 |
| MATRIX (default for economy keys) | ML-KEM-1024 + HQC-5 + X25519 | ML-DSA-87 + SLH-DSA-SHAKE-256s |

Every post-quantum part of HIGH and MATRIX uses its NIST category 5
parameters. MATRIX also rests on unrelated hard problems:

* Key exchange: lattices (ML-KEM), codes (HQC) and elliptic curves (X25519).
* Signatures: lattices (ML-DSA) and hash functions alone (SLH-DSA).

An attacker has to break every layer, not just one.

## 2. What MATRIX signatures are for

A MATRIX dual signature takes about 1.5 s to make, because SLH-DSA-SHAKE-256s
is slow to sign. Checking one takes about 2.5 ms. The slow half is the
hash-based one, and it stays valid even if lattice cryptography falls.
So MATRIX signatures are only for **long-lived anchors**, which are signed
rarely and verified often:

| Purpose (`pqm_sig_purpose_t`) | Examples | Level (`pqm_sig_level_for`) |
|---|---|---|
| `PQM_SIG_PURPOSE_IDENTITY_KEY` | identity and account keys; certifying a long-term KEM public key | MATRIX |
| `PQM_SIG_PURPOSE_RELEASE` | release and update signing | MATRIX |
| `PQM_SIG_PURPOSE_SETTLEMENT_BATCH` | settlement batches | MATRIX |
| `PQM_SIG_PURPOSE_CHARTER_RECORD` | treaty and charter records | MATRIX |
| `PQM_SIG_PURPOSE_SESSION_MESSAGE` | anything sent inside a session | none (returns 0): use the session AEAD |

**No action a user waits on is signed with SLH-DSA.** A session works like
this:

1. **The session is authenticated by its KEM, not by a signature.** The
   identity key signs the long-term pqm KEM public key once, when it is made.
   That signature is a certificate, and it is checked in 2.5 ms. A peer then
   encapsulates to the certified key. Only the holder of the matching secret
   key can derive the session keys.
2. **Each message is authenticated by AEAD.** ChaCha20-Poly1305 runs under
   keys derived from the hybrid-KEM shared secret. A 256-byte message costs
   about 2.5 µs to seal and open. Between the two parties, the AEAD tag gives
   what a per-message signature would, at a tiny fraction of the cost.

Code that needs non-repudiation for a third party, such as a settlement
record, signs the batch. It does not sign each message.

## 3. What it costs

Measured on one core with `-O2`, by `test_pq_matrix` (`make verify-all` runs
it with `--no-bench`, so the benchmarks are skipped there):

| | STANDARD | HIGH | MATRIX (default build) | MATRIX (`-mpclmul`) |
|---|---|---|---|---|
| Ephemeral handshake (keygen + encaps + decaps) | ~2.3 ms | ~2.0 ms | ~13 ms | ~5.3 ms |
| Session to a certified static KEM key | | | ~10.7 ms | ~6.1 ms |
| Sign / verify | 0.3 / 0.2 ms | 0.9 / 0.3 ms | ~1.6 s / 2.5 ms | same |
| Bulk ChaCha20-Poly1305 | ~250 MB/s | same | same | same |

The cost lands only on handshakes and anchor signatures. Bulk data costs the
same at every level.

Most of HQC's time goes on one polynomial multiplication. ZXV replaces the
reference version, which works one bit at a time, with
`kernel/src/pqsec/pq_hqc5_gf2x.c`. That file uses Karatsuba over a
constant-time 64×64-bit carry-less multiply. The compiler flags choose the
backend:

| Backend | When | HQC-5 encaps |
|---|---|---|
| `pclmulqdq` | x86-64 with `-mpclmul` (or a `-march` that includes it) | ~1.1 ms |
| `pmull` | AArch64 with `-march=armv8-a+crypto` | builds; not yet timed on hardware |
| `mulholes` | x86-64 / AArch64 without those flags (the default here) | ~3.5 ms |
| `shiftmask` | any other CPU, or `-DPQM_HQC_SHIFTMASK` | ~10 ms |

The reference multiply took about 27 ms. Every backend reproduces the official
HQC known-answer tests bit for bit. `pqm_hqc5_mul_selftest()` also checks each
one against the reference multiply directly.

`mulholes` multiplies integers whose bits are spaced four apart. It is
constant time on the CPUs it is selected for, because their integer
multipliers are. Builds for x86-64 or Arm that can use PCLMULQDQ or PMULL
should turn them on.

## 4. Limits

These are reference-grade implementations. They have not been hardened
against every side channel or against fault injection. "Highest available"
means category 5 parameters plus a mix of hard problems. It is not proven
unbreakability.

HQC is selected by NIST but not yet a final FIPS, so a future standard may
differ from v5.0.0. The full list is under HONEST LIMITS in `pq_matrix.h`.
