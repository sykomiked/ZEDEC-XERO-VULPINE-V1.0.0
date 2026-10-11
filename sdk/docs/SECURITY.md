<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# SDK security model and honest limits

This page says what the ZXV SDKs protect, how, and where the protection
stops. Every SDK README repeats the limits that apply to it.

## What the SDKs do

| Property | How |
|---|---|
| Least privilege | Every gated call carries a capability handle, checked in C on every call. Capabilities can only be narrowed; revoking one revokes all its children. A cap from one context is refused on another context's handles. The 1.0 calls without a cap remain for compatibility and are deprecated in favour of their `_cap` forms. |
| Attestation gate | A capability whose attestation is not `TRUE` (including the contradictory `BOTH`) is refused. A child cap copies its parent's attestation when it is made; a later change on the parent does not reach existing children (revocation does). |
| Fail-closed | A broken ledger invariant rolls back and poisons the context in C. Every later call on it, or on any handle made from it, fails with `ZXV_E_POISONED` before its arguments are even checked. SDKs raise a terminal error type and offer an opt-in process abort. |
| No silent float drift | No API takes a float. A host double enters only through a method named `fromDoubleExact`, which keeps its exact binary value. |
| Secret wiping | Secret keys and shared secrets live in memory the SDK allocates outside the garbage-collected heap where the language allows, and are wiped with `zxv_secure_zero` on close and by a finalizer as a backstop. Expanded keys and randomness inside libzxv are wiped after each operation. |
| Post-quantum crypto | ML-KEM-1024 (FIPS 203) and ML-DSA-87 (FIPS 204) from the kernel's vendored reference code, checked against NIST ACVP vectors in the kernel tests. |
| Memory safety of the C layer | The C conformance tests run clean under AddressSanitizer and UndefinedBehaviorSanitizer. |

## Where it stops

- **Capabilities are not a sandbox inside one process.** Any code in the
  same address space can create its own root capability or read memory.
  Capabilities become enforcement at a process boundary, when an app talks
  to the `zxvd` daemon over IPC.
- **Wiping cannot reach copies the SDK never saw.** If your code holds a
  secret in a managed array or string, the garbage collector may already
  have copied it. Swap, hibernation files and core dumps can hold secrets.
  Use the SDK's `SecretKey` type and avoid `exportBytesUnsafe()`.
- **Not certified.** The cryptography is not FIPS 140-3 validated and has
  not had an independent audit. Nothing here is certified by any payment
  scheme, regulator or standards body.
- **No network transport.** libzxv serializes messages and keeps an
  in-process ledger. It does not connect to PAPSS, SWIFT, CIPS, RTGS or any
  blockchain.
- **The EVM adapter encodes; it does not settle.** It produces the
  `abi.encode` bytes and Keccak-256 digest a state-channel contract checks.
  Deploying and auditing such a contract is your responsibility.
- **Not constant-time everywhere.** The vendored PQC and Ed25519 code is the
  reference implementation; side-channel resistance on your hardware is not
  measured.
- **LoRA is not implemented.** The calls exist and return
  `ZXV_E_NOTIMPL`.

## Advisory-only domains

SDK features that touch clinical rules, power grids or other life-safety
systems produce advice for a human to approve. They never act on their
own.

## Reporting a vulnerability

Open a private security advisory on the GitHub repository, or contact the
maintainer listed in the repository profile. Please do not open a public
issue for an unfixed vulnerability.
