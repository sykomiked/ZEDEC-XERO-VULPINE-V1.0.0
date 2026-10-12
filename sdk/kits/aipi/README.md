<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# AIPI kit: content-addressed functions on Z-Wasm

This kit runs AIPI functions in the [Z-Wasm](../z-wasm/) sandbox, inside
their [z-contract](../z-contract/) boundary. It supplies the executor
(`aipi_exec_ops_t`) that the AIPI core in
[`kernel/src/aipi`](../../../kernel/src/aipi/aipi.h) calls to run a function,
check someone else's answer, and make a certificate.

The design, the CID formats, the verification modes and the limits are in
[docs/AIPI.md](../../../docs/AIPI.md).

Kit API version 0.1 (`include/aipi_zwasm.h`). It may still change before 1.0.

> **Exception to the kit rules.** Kits normally call only the libzxv ABI.
> The AIPI core is kernel code (`kernel/src/aipi`, with `src/ipfs_node` and
> `src/lpres`), and this kit compiles it in directly until AIPI calls are
> part of the libzxv ABI. That is a change for the SDK track, not made here.

## Quickstart

```sh
make -C sdk/libzxv
make -C sdk/kits/aipi test demo
```

The tests and the demo need no Wasm toolchain: their modules are assembled
in C with z-wasm's `test/wasm_build.h`.

The demo:

```text
function CID  zyu3uqS8N5tr3FvViFgu28ZTQ43Chnekt1mjB1ruxGAxhKVTzBvGL
  codec 0x3A1000 (zxv-fn, private-use range), sha2-256 of the descriptor
  Vinea provider key f390b441...

alice: AIPI_EXECUTE(half, 2026, bounty 10): route mesh, escrow 10
  fetched by CID and verified: descriptor, module (99 bytes), contract
  tag neither/neutral/emerging -> wait
mallory claims 1234: REJECTED by half_ok, paid 0
  tag both/negentropic/emerging -> wait
bob runs the Wasm and claims 1013: ACCEPTED, paid 10
  tag true/negentropic/manifest -> proceed

offline: half(99) = 49, run locally from the cached module (route local)
offline: half(2026) answered from the memo of verified results (route cache)
alice's balance: 90 (10 paid to bob, nothing to mallory)
```

## The call ABI

| Part | Form |
|---|---|
| input | `n` signed 64-bit integers, little-endian, `0 <= n <= 8` |
| `entry(args...) -> i64` | the result, 8 bytes little-endian |
| `checker(args..., result) -> i32/i64` | 1 means the result is right for the input; anything else, or a trap, means it is not |
| `certifier(args..., result) -> i64` | CERT functions: the certificate |
| `checker(args..., result, cert)` | CERT functions: checks the certificate |
| contract | if the descriptor names one, `fn <entry>` in it is enforced on every run, and a claimed result outside its result type is refused before the checker runs |
| fuel | each call gets the descriptor's `fuel_max`, capped by the `rmag` budget |

Modules get no host-function grants. Every load goes through the Z-Wasm
validator, so floats, SIMD and unbounded memory are refused.

## Tests

`make test` runs 60 end-to-end checks: local and offline runs, a call
refused by its contract before any fuel is spent, a runaway loop stopped at
its fuel limit, a float module refused, a forged answer refused by the
function's own inverse in the requester's sandbox, an out-of-contract
answer refused before the checker, a certificate (8633 = 89 x 97) checked,
bad certificates refused (including one that makes the checker trap), and
payment only after verification. CI runs them with gcc and clang on Linux,
clang on macOS, and under ASan + UBSan.

The core's own tests (617 checks, including quorum, disputes, expiry,
privacy and an escrow invariant over random runs) are `make -C kernel
test-aipi`.

## Limits

- **A checker is only as good as its author.** `half_ok` really is the
  inverse of `half`. A checker that returns 1 for everything accepts
  everything. Review the checker, not just the entry.
- **CERT proves only answers that have a certificate.** "This number is
  composite" has one (a factor); "this number is prime" does not in this
  ABI, so such a call under CERT can only expire and be refunded.
- **Results are one i64 in this ABI.** Bigger outputs need a
  memory-based ABI, planned for a later kit version.
- **Each call reloads the module.** Correct and simple; a module cache
  keyed by CID is a later optimisation.
