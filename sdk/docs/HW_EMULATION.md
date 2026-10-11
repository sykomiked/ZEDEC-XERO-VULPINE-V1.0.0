<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Hardware emulation and hardware updates

ZXV treats hardware the way it treats software: every component sits
behind an interface, may be replaced by anything that provably behaves the
same, and can be updated by a signed image. This page explains the design.
The code is in [`kernel/src/hwemu`](../../kernel/src/hwemu), and the C ABI
is libzxv 1.4 (`zxv_hw_*`).

## Three layers

```
 TARGET   the machine the software is written for
          (default: the VULPINE-class reference machine R1, a ZHP-1 profile)
    |  hwp_match / hwp_present: which target components does the host have?
    v
 MATRIX   one row per component function; each row has providers:
          HARD | FABRIC | MICRO | PEER | SOFT | REMOTE
    |  the best proven provider that the placement policy allows serves
    v
 HOST     the machine that is physically here
          (a ZHP-1 profile from zxv_hw_profile_probe_host)
```

The machine is not emulated as one monolithic VM. Each component is its
own row in the matrix and is served independently, so a laptop can run the
CPU and vector work natively while the ternary unit, the hash unit and the
harmonic-wire port are emulated. When hardware that has those units
appears, its rows switch to native providers and nothing above changes.

By default ZXV detects the host and presents a VULPINE-class machine:
`zxv_hw_profile_present` copies R1 and marks every component HARD where the
host has it and SOFT where it is emulated. Emulation is functional and
engine-level: an engine is matched on what it computes, bit for bit, not on
how many cycles it takes. Cycle-accurate models belong in verification,
not in the runtime.

### Profiles: the hardware ROM

[ZHP-1](HW_PROFILE.md) describes a machine as abstract component classes
with integer capability descriptors. It is canonical (one encoding, one
SHA-256 identity) and can carry per-component state, so a profile can be
signed, pinned, snapshotted and restored. Each component can name the
engine contract it implements and its version, its provider kind, and
coarse capacity, performance and power buckets. It never describes an
implementation. A vendor can attach an opaque extension blob to any
component for its own use; the blob is covered by the profile digest but
its format is never part of the standard. Private class and capability
ranges are reserved too.

### Standard engines

Every libzxv matrix starts with one resource, the host
(`ZXV_HW_RES_HOST`), and the standard engines, each with a SOFT
implementation in libzxv and a version-1 golden-vector contract:

| Engine | Function id | Operations |
|---|---|---|
| SHA256 | `0x5A450001` | op 0: SHA-256 |
| KECCAK | `0x5A450002` | ops 0/1: SHA3-256/512; ops 2/3: SHAKE128/256 (u16 output length prefix, 1..512) |
| MAC | `0x5A450003` | op 0: HMAC-SHA256 (u16 key length, key, message); key-bearing |
| GCD | `0x5A450004` | op 0: n pairs of u64 to n u64 (n 1..32) |
| LOGIC64 | `0x5A450005` | four-valued AND, OR, NOT on 64 two-bit lanes |
| TENSOR | `0x5A450006` | op 0: the engine's integer dot product (`zt_dot_raw`); absent in builds without the tensor engine |

The vectors are generated from the open C code
(`kernel/src/hwemu/gen_hwe_vectors.c`) and cross-checked against Python's
hashlib, hmac and math.gcd. Hardware that implements an engine registers a
provider for the same function id; it serves only after reproducing the
same vectors exactly.

### The matrix: components emulating each other

Every provider of a function has the same call shape: an opcode, input
bytes, output bytes. The rules (hwm.h M1-M9):

| Rule | What it guarantees |
|---|---|
| M1 one interface | any provider can replace any other |
| M2 proven before bound | a provider serves only after reproducing the function's conformance vectors bit for bit; mismatch is FALSE, no vectors is NEITHER (S0), a disagreement in service is BOTH, and only TRUE serves |
| M3 deterministic choice | lowest cost, then HARD > FABRIC > MICRO > PEER > SOFT > REMOTE, then index: the same binding on every machine |
| M4 failover | a fault, a reconfiguration or a lost proof moves the function to the next proven provider; with none, the call is refused, never routed to an unproven one |
| M5 declared, acyclic dependencies | a provider may lean on other functions (an emulated hash using the matrix unit). It may call only what it declared, and no binding is circular, so A may emulate B and B may emulate A as cells without a live loop |
| M6 shadow checks | run the bound provider and the next-best one side by side; a mismatch takes both out of service until re-proven. Nothing votes |
| M7 memo | a pure function may skip the provider on a repeated input (exact byte compare) |
| M8 placement by policy | apps state constraints (local only, a latency ceiling, a power budget), never a provider. A key-bearing function is always local: no REMOTE provider serves it or anything it depends on, and it never gets a memo |
| M9 versioned contracts | a function's vectors form a contract with a version; a newer contract makes every provider prove itself again, and each provider records the version it proved |

A provider that has not declared its latency and power never qualifies
under a policy that limits them (`zxv_hw_provider_traits` declares them).

### Discovery

`zxv_hw_function_info` returns a function's contract version, policy flags
and vector count. `zxv_hw_provider_info` returns, for each provider, its
function, kind, proof state, the contract version it proved, flags (fault,
disputed, failed, unproven, disabled, excluded by policy, serving), a
throughput class, a power class (coarse buckets from 10 mW to above 100 W),
and how often it was called. An SDK can list every engine on a machine and
show which implementation serves each one and why.

### Regions: hardware updates delivered as software

A reconfigurable region (FPGA fabric, a microcoded unit, a soft core)
changes what the hardware *is* when it loads a new image. hwr.h R1-R7:

1. **Signed twice.** `header || payload || signature`. The suite is
   ML-DSA-87 plus SLH-DSA-SHAKE-256s, and both signatures must verify, so
   a break in either scheme alone does not let an image through. The
   signature covers the header, which carries the payload's SHA-256. Only
   keys the owner trusted verify. Nothing unsigned ever loads.
2. **For this hardware only.** The header names a target class and an
   optional exact-part digest.
3. **Forward only.** Versions strictly increase, and a security version
   floor (anti-rollback) stops signed-but-vulnerable images from being
   replayed.
4. **Two slots (A/B).** The image loads into the inactive slot while the
   active one keeps working, then is read back and its digest compared.
5. **Proven on probation.** The trial switches slots and runs the
   function's conformance vectors through the matrix. While it switches,
   the matrix serves the function from an emulator, so callers do not
   stop. Only a passing image enters probation, and only a commit makes it
   permanent and raises the floor.
6. **Rollback and S0 quarantine.** Any failure after the signature checks
   out switches back to the previous image and re-proves it. If there is
   none, the region drops to S0 (unknown never acts) and the emulator keeps
   serving. The image is marked with a forbidden hexagram tag and blocked
   from loading again.
7. **Every step recorded.** Stage, test, activation, rollback, rejection
   and quarantine are each appended to a hash-chained audit log (SHA-256,
   domain `zxv/hw-audit/v1`) and reported to an event callback. An
   activation that cannot be recorded is reverted, and a full log halts
   the region and poisons the libzxv context: fail closed.

Payload kinds are bitstream, partial bitstream, microcode, firmware and
**vectors**. A vectors package is a signed contract update: it carries a
newer golden-vector set for one function and is applied to the matrix with
`zxv_hw_contract_apply`, never loaded into a region. The matrix has its own
trusted keys, anti-rollback floor per function and audit log for these.

A region can also become a *different* accelerator: an image whose header
names another function re-points the region's provider at that function,
and a rollback points it back.

The device is reached only through three callbacks (load, read back,
activate), so the policy knows nothing about any vendor's parts.

## Efficiency: what is claimed and what is measured

Engine-level emulation is much faster than naive instruction-by-instruction
emulation, because each engine runs as native code on the host. It is not
faster than the same work written natively for the host. An emulator is
normally slower than the hardware it imitates, and a
dispatch layer adds a little cost to every call. ZXV's efficiency goal is
narrower than "the emulated machine out-produces its host": it is to get
closer to the host chip's own ceiling than conventional software stacks
do, by keeping work deterministic and skipping work that has already been
done. The benchmark ([`bench_hwm.c`](../../kernel/src/hwemu/bench_hwm.c),
`make -C kernel bench-hwemu`) reports both numbers for each workload:
speed against native code, and the share of a measured per-core ceiling.

One run on a 4-vCPU cloud Xeon at 2.8 GHz (numbers vary by machine; rerun
on yours):

| int8 matrix-vector 64x64 | ns/call | share of ceiling | vs native |
|---|---|---|---|
| native C | 630 | 47% | 1.00x |
| through the matrix | 642 | 46% | 0.98x |

| SHA-256, 32-byte blocks | native | matrix | matrix + memo | native + same cache |
|---|---|---|---|---|
| no repeats | 338 ns | 421 ns | 438 ns | 421 ns |
| 50% repeats | 336 ns | 345 ns | 392 ns | 254 ns |
| 90% repeats | 324 ns | 341 ns | 160 ns | 104 ns |

What this shows:

- The matrix costs about 2% on a compute-heavy call and more on a short
  one. It never beats the native loop doing the same work.
- The memo makes repeated inputs about twice as fast as running the work
  again (at 90% repeats) and costs a little when inputs do not repeat.
  Native code with the same cache does better by the same mechanism, so
  this is work skipped, not extra throughput from the chip.
- The ceiling is measured (dependency-free int16 and int32 multiply-add
  loops on one core), not the vendor's peak. Wider SIMD, more cores and
  matrix engines would raise it.

The phase-tick scheduler and fractal-proportion layouts are ideas to
measure on this same benchmark as they land, not results yet.

## Pre-compatibility with future hardware

ZXV software targets R1, not any particular chip. Hardware built to
satisfy R1, including hardware whose design is private, publishes a ZHP-1
profile and runs the same software with nothing emulated. The profile
format reserves private class and capability ranges so such hardware can
describe its extras without the public format knowing anything about them.
This repository contains no proprietary chip designs, and none are needed
for the software to be ready for them.

## For SDK authors

The libzxv 1.4 calls cover what a hardware-update kit needs: discovering
engines and their proofs (`zxv_hw_function_info`, `zxv_hw_provider_info`),
stating placement (`zxv_hw_policy_set`), building dual-signed images and
contract packages (`zxv_hw_keygen`, `zxv_hw_image_build`), driving a region
(`zxv_hw_region_*`, with `zxv_hw_region_events`), reading the audit logs
(`zxv_hw_region_log_entry`, `zxv_hw_matrix_log_entry`), and describing and
matching machines (`zxv_hw_profile_*`). Every language SDK binds them.

The hardware group adds two capability bits (`ZXV_CAP_HW_MATRIX`,
`ZXV_CAP_HW_RECONF`). Because `ZXV_CAP_ALL` is a released constant, its value
stays `(1 << 10) - 1` forever; the new `ZXV_CAP_ALL_1_4` names every bit as of
1.4. Each later minor that adds capability bits adds its own `ZXV_CAP_ALL_1_N`
the same way. Code that prints rights by name should list single bits only,
never these groups.
