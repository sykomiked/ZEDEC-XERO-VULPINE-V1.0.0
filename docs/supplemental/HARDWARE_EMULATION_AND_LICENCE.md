<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Why ZXV treats hardware as software, and why hardware has its own licence

The reference pages, [`sdk/docs/HW_EMULATION.md`](../../sdk/docs/HW_EMULATION.md)
and [`sdk/docs/HW_PROFILE.md`](../../sdk/docs/HW_PROFILE.md), say what the
code in `kernel/src/hwemu/` does. This page says why, and why hardware
designs are licensed differently from the rest of the repository.

## For the general reader

Every circuit is a fixed program, and every program runs as electricity in
real circuits. ZXV takes that seriously
([`DESIGN_PHILOSOPHY.md`](../../DESIGN_PHILOSOPHY.md), section 2): the line
between hardware and software is a line of convenience.

In practice this means ZXV software is written for one reference machine,
called R1. Your laptop probably lacks some of R1's parts (for example a
ternary logic unit). Those parts are filled in by software that gives
exactly the same answers. If you later get hardware that has the real part,
it takes over, and the software above does not change.

Some hardware can be rewired by loading a new image (FPGAs, microcoded
units). For those, ZXV is designed so that a "hardware upgrade" can arrive
as a signed software update: checked, tried on probation, kept only if it
passes, and rolled back if it fails, with every step written to a log.

Emulation has a cost. The aim is to be much faster than naive emulation,
not faster than the native hardware.

## For the academic reader

- **Functional, engine-level emulation.** Each component function (SHA-256,
  Keccak, HMAC, GCD, 64-lane four-valued logic, an integer tensor dot) is a
  row in a matrix (`kernel/src/hwemu/hwm.h`). Providers of a row may be
  HARD, FABRIC, MICRO, PEER, SOFT or REMOTE. Equivalence is bit-exact
  output on golden vectors, not cycle accuracy. This is closer to
  high-level emulation and to hardware/software co-design than to
  instruction-set simulation.
- **Proof by conformance vectors (M2, M9).** A provider serves only after
  reproducing the function's versioned vector set exactly; a mismatch is
  FALSE, no vectors is NEITHER, an in-service disagreement is BOTH (the
  repository's four-valued logic). The header states the limit plainly:
  vectors prove agreement on those vectors, not equivalence on every input.
- **Deterministic binding and failover (M3, M4).** Selection is by assigned
  cost, then provider kind, then index, so the same matrix binds the same
  way on every machine; with no proven provider the call is refused.
- **Shadow checks without voting (M6)**, declared acyclic dependencies
  (M5), exact-match memoisation of pure functions (M7), and placement by
  policy with key-bearing functions forced local (M8).
- **Standard engines** (`kernel/src/hwemu/hwe.h`, `hwe_ids.h`) each have a
  SOFT reference and version-1 golden vectors generated from open C code
  (`gen_hwe_vectors.c`) and cross-checked with Python's hashlib, hmac and
  math.gcd (`kernel/tools/check_hwe_vectors.py`).
- **Signed reconfiguration (R1-R7, `kernel/src/hwemu/hwr.h`).** Images are
  dual-signed with ML-DSA-87 (FIPS 204) and SLH-DSA-SHAKE-256s (FIPS 205);
  both must verify, so breaking one scheme alone is not enough. Images are
  target-bound, version-monotonic with an anti-rollback floor, loaded A/B
  with read-back, proven on probation through the matrix, and rolled back
  or quarantined on failure.
- **Audit (`kernel/src/hwemu/hwa.h`).** An append-only SHA-256 hash chain
  (domain `zxv/hw-audit/v1`). A change that cannot be recorded is not made.
- **Profiles (`kernel/src/hwemu/hwp.h`, ZHP-1).** A canonical,
  integer-only description of a machine as abstract component classes and
  capability descriptors, with one SHA-256 identity per profile. Private
  class and capability ranges and an opaque per-node extension let a vendor
  describe hardware without revealing its design.

## What actually exists

| Piece | Where |
|---|---|
| Emulation matrix (M1-M9) | `kernel/src/hwemu/hwm.h`, `hwm.c` |
| Standard engines and golden vectors | `kernel/src/hwemu/hwe.h`, `hwe_vectors.h`, `gen_hwe_vectors.c` |
| Signed region updates (R1-R7) | `kernel/src/hwemu/hwr.h`, `hwr.c`, `hwr_pq.h` |
| Hardware audit log | `kernel/src/hwemu/hwa.h`, `hwa.c` |
| ZHP-1 profiles and reference machine R1 | `kernel/src/hwemu/hwp.h`, `hwp.c` |
| Tests | `make -C kernel test-hwemu` (part of `verify-all`) |
| Benchmark | `make -C kernel bench-hwemu` (`kernel/src/hwemu/bench_hwm.c`) |
| Hardware licence | [`LICENSE-HARDWARE.md`](../../LICENSE-HARDWARE.md), [`hardware/LICENSE`](../../hardware/LICENSE), [`hardware/NOTICE`](../../hardware/NOTICE), [`hardware/PRIOR_ART.md`](../../hardware/PRIOR_ART.md) |

The device side of a region is reached only through three callbacks (load,
read back, activate); the tests use simulated regions. The policy code is
freestanding C11 with no floating point.

## Why a separate hardware licence

Software in this repository is Apache-2.0 ([`LICENSE`](../../LICENSE)).
Files under `hardware/` are under the **ZXV Hardware License 1.0**
(`LicenseRef-ZXV-Hardware-1.0`, mapped in [`REUSE.toml`](../../REUSE.toml)).
It is source-available, not open source. In plain terms
([`hardware/README.md`](../../hardware/README.md)):

- free for noncommercial use: study, build, modify and share;
- anyone may evaluate and build up to 10 prototypes internally;
- commercial manufacture or sale needs a separate agreement and an 18%
  royalty on gross revenue paid to ILLUMAHEART FOUNDATION;
- attribution is required.

The reasons, as the licence states them: the designs are published openly
as **prior art (defensive publication)** so that nobody can patent them
(section 2.1, and the licensor states it does not intend to seek patents);
the licensor will not assert any patent it holds against compliant users
(2.2); and licensees who patent derivatives must license those claims back
to everyone (2.3). Physical products and manufacturing are treated
differently from code, so the royalty applies only to commercial products.
Firmware that runs on ZXV hardware stays in `kernel/` and `sdk/` under
Apache-2.0. The full licence text governs; this summary does not replace it.

## Limits

- **No chip design files are published.** The `hardware/` subfolders
  (`rtl/`, `silicon/`, `boards/`, `chassis/`, `config/`, `disclosures/`,
  `docs/`) contain only placeholders, and the prior-art index has no
  entries yet. The licence is in place ahead of the designs.
- **Not faster than native.** Engine-level emulation is designed to be much
  faster than naive instruction-by-instruction emulation. The one published
  benchmark run shows the matrix about 2% slower than native C on a
  compute-heavy call and slower on short ones; memoisation only skips
  repeated work. Numbers vary by machine.
- **Golden vectors are a test suite, not a proof** of equivalence.
- **Host detection reports only what the OS reliably exposes**; anything
  else is emulated, never guessed.
- **Emulating others' hardware** may need rights the licence does not grant
  (`hardware/LICENSE`, section 5).
