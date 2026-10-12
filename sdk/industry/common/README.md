<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# common (libzind): the shared layer of the industry kits

`libzind` is the small C11 library that every industry kit links. It is
built on the public libzxv ABI only. The whole API is
[`include/zind_common.h`](include/zind_common.h).

| Part | What it does |
|---|---|
| Status codes | `ZIND_E_HELD` -3001, `REPLAY` -3002, `CHAIN` -3003, `INEXACT` -3004, `NEGATIVE` -3005, `FROZEN` -3006, `UNTRUSTED` -3007. `zind_status_name` also names every libzxv code. `zind_last_error` gives a thread-local message for the last refusal. |
| `zind_qty` | Exact quantities on `int64` rationals, always reduced, with a positive denominator. `parse` accepts `12`, `-3/4` and `0.125` (no exponents). `format` writes the shortest exact decimal, or `n/d`. `add` / `sub` / `mul` / `cmp` refuse overflow with `ZXV_E_RANGE`. `to_minor(q, digits, mode)` converts to minor units under `EXACT` (the default, which refuses), `HALF_UP`, `HALF_EVEN`, `CEIL`, `FLOOR` or `TRUNC`, and returns the exact residual. |
| `zind_subject` | SHA3-256 of `"zind/" domain 0 data`: a domain-separated digest for identifiers. |
| `zind_seen` | A set of 32-byte digests, used for replay refusal (at most 2^24 entries). |
| `zind_signer` | An ML-DSA-87 key pair, from a 32-byte seed or the system RNG. Its key id is SHA3-256(pk). `zind_sign` and `zind_verify` take a context string. |
| `zind_chain` | An append-only, hash-chained, signed record list. Each record hash is SHA3-256 of `"zind/rec/v1"`, the domain, seq, kind, payload, previous hash and key id. It is signed with ML-DSA-87 under the chain's domain as context. `export` and `import` use a self-contained `ZIND` v1 byte form. `verify` checks every link and every signature against a trusted key list, offline, and names the first bad record. |
| `zind_qsink` | The quarantine sink for Both and Forbidden events. It accepts only those two kinds and writes each into a libzxv hexlog as a Forbidden tag with a reason, a submitter index and a subject digest. When the log is full, it halts the given context instead of dropping evidence. |
| `zind_wr` / `zind_rd` | The little-endian canonical encoding used by every signed message: fixed-width integers, length-prefixed strings and quantities. A short buffer sets a flag and never writes past the end. |
| `zind_ident_ok` | Checks an identifier: 1..n characters of `[A-Za-z0-9._:/-]`. |

## Tests

```sh
make -C sdk/libzxv all
make -C sdk/industry/common test                 # 11,237 checks, fuzz smoke, no-float check
make -C sdk/industry/common SANITIZE=1 OUT=build/san test
make -C sdk/industry/common fuzz FUZZ_ITERS=200000
```

`test_common` covers several areas:

- quantity arithmetic, overflow refusal and the `INT64_MIN` corner;
- every rounding mode against an independent reference, including at ties;
- parse/format round trips and malformed input;
- seen-sets up to capacity;
- chains, including:
  - a wrong key;
  - a flipped bit in every field;
  - truncation, reordering and a spliced record;
  - an import under another domain;
- the quarantine sink, which accepts only Both and Forbidden events and
  halts its context when the log is full;
- the reader and writer at every boundary.

`fuzz_common` is deterministic. Set `ZIND_FUZZ_SEED=n` for another stream.
It mixes arithmetic, rounding, parsing, seen-set and chain-mutation
operations, and checks invariants after each one.

## Limits

- A quantity is an `int64` over an `int64`. A sum whose exact value
  does not fit is refused (`ZXV_E_RANGE`), never rounded. Kits pass that
  refusal on to the caller.
- A chain lives in memory, up to 2^20 records with payloads of at most
  8 KiB. Persistence and replication are up to the application, which can
  store `zind_chain_export` bytes anywhere. Import checks the format, and
  `verify` checks the signatures.
- `zind_chain_verify` proves that a trusted key signed each record in
  order. It does not prove that what the record says happened in the
  physical world.
- Keys are held in process memory and zeroized on destroy. There is no
  HSM integration.
- ML-DSA-87 signing takes about 0.7 ms and verifying about 0.3 ms with the
  portable libzxv build. A chain export carries a 4,627-byte signature per
  record.
