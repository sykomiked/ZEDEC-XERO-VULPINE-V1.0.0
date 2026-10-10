<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# Fuzz report

This report covers the local campaign of 2026-10-10. Every number below was copied from the
libFuzzer `-print_final_stats=1` output or from the property driver's `FUZZ_PROP_STATS`
line. Nothing is extrapolated.

## Setup

* Machine: a shared 4-core x86-64 Linux container. Other build and test jobs ran at the
  same time (load average 5 to 29), so exec/s figures are lower than on an idle machine.
* Compiler: clang 18.1.3.
  * libFuzzer builds: `-fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all`
    with `sanitizer-ignorelist.txt`.
  * Replay builds: gcc 11.5 with ASan + UBSan.
* Flags: `-timeout=20 -rss_limit_mb=4096`, default `-max_len` growth (4096 for most
  targets). Each run starts from the committed seeds in `corpus/<target>`.
* "cov" and "ft" are libFuzzer's edge-coverage and feature counters at the end of the
  run. "corp" is the working corpus size. Line and branch coverage are in
  `docs/COVERAGE.md`.

## Core parser harnesses: 10^7 executions each

| harness | executions | seconds | avg exec/s | cov | ft | corp | peak RSS MB | crashes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| fuzz_cobol_copybook | 10,000,000 | 3,214 | 3,111 | 339 | 2,330 | 728 | 503 | 0 (after fix, below) |
| fuzz_ubh_frame | 10,000,000 | 3,798 | 2,632 | 131 | 376 | 99 | 477 | 0 |
| fuzz_gguf_loader | 10,000,000 | 3,875 | 2,580 | 430 | 2,058 | 611 | 425 | 0 |
| fuzz_fat32_mount | 10,000,000 | 4,568 | 2,189 | 116 | 710 | 169 | 729 | 0 |

Each row is one uninterrupted run with `-runs=10000000`. Earlier runs did not reach
10^7:

* `fuzz_cobol_copybook`: the first run crashed after 7,431 executions. That crash is
  finding F1.
* `fuzz_fat32_mount`: the first run was stopped at 847,525 executions. At that point it
  was too slow to reach 10^7 on the loaded machine. The harness then got a smaller
  per-file read buffer (8 KiB instead of 64 KiB, enough for the ≤ 4 KiB images), and
  the run in the table started from the first run's working corpus.

## Other parser harnesses: 300 s each

| harness | executions | avg exec/s | cov | ft | corp | crashes |
|---|---:|---:|---:|---:|---:|---:|
| fuzz_vinea | 2,973,504 | 9,878 | 495 | 1,764 | 524 | 0 |
| fuzz_pay_xml | 518,098 | 1,721 | 456 | 1,612 | 488 | 0 |
| fuzz_tls_record | 41,242,609 | 137,018 | 110 | 334 | 52 | 0 |
| fuzz_tls_handshake | 7,336 | 24 | 321 | 673 | 75 | 0 |
| fuzz_freight | 271,134 | 900 | 330 | 1,145 | 332 | 0 |
| fuzz_upcheck | 3,382,658 | 11,238 | 738 | 1,968 | 668 | 0 |
| fuzz_bootlegger | 321,874 | 1,069 | 297 | 521 | 96 | 0 |
| fuzz_ipfs_cid | 21,115,557 | 70,151 | 259 | 733 | 176 | 0 (after fix) |
| fuzz_ipfs_dag | 6,629,801 | 22,025 | 508 | 1,549 | 423 | 0 |

* `fuzz_ipfs_cid`: the first run crashed after 114,882 executions. That crash is
  finding F2. The row above is the 300 s rerun after the fix.
* `fuzz_tls_handshake`: every input runs a full X25519 ClientHello, so it is slow.
  7,336 executions is a light run.

## Ledger property harnesses

### Seeded mode (deterministic)

Command: `build/replay/<h> --random 20261010 8000 4096`, gcc ASan + UBSan,
`FUZZ_PROP_STATS=1`. Each operation is followed by the full invariant check described in
`README.md`.

| ledger | inputs | operations checked | seconds | result |
|---|---:|---:|---:|---|
| vino (`fuzz_econ_vino`) | 8,000 | 2,276,881 | 85 | pass |
| pay (`fuzz_econ_pay`) | 8,000 | 1,052,092 | 200 | pass |
| count_house (`fuzz_econ_count_house`) | 8,000 | 3,340,563 | 5 | pass |
| triple ledger (`fuzz_econ_triple`, Q32.32 build) | 8,000 | 1,842,896 | 3 | pass |

Each ledger got at least 10^6 random operations from seed 20261010. The run is
reproducible: the same command gives the same operation stream. CI runs 1,000 inputs
(`fuzz-smoke`), 500 under `-fsanitize=integer` (`sanitize-integer`) and 300 under MSan
(`sanitize-msan`). verify-all runs 300.

### libFuzzer mode, 300 s each

| harness | executions | avg exec/s | cov | ft | corp | crashes |
|---|---:|---:|---:|---:|---:|---:|
| fuzz_econ_vino | 9,664 | 32 | 167 | 1,002 | 188 | 0 |
| fuzz_econ_pay | 19,617 | 65 | 368 | 1,243 | 221 | 0 |
| fuzz_econ_count_house | 562,557 | 1,868 | 94 | 646 | 179 | 0 |
| fuzz_econ_triple | 431,871 | 1,434 | 144 | 911 | 252 | 0 |

An execution here is a whole operation sequence of up to 8 KiB, with an invariant check
after every operation. That is why vino and pay run at tens of executions per second.

## Findings

### Found and fixed in this campaign

| id | found by | module | problem | fix | regression |
|---|---|---|---|---|---|
| F1 | fuzz_cobol_copybook (libFuzzer: a 10 s probe run, then the first long run after 7,431 execs) | kernel/src/legacy/cobol.c | An unsigned `PIC 9` field decoded a negative sign nibble as -n. `cobol_set_int` then wrote it back as +n, so a get/set round trip changed the value. The same held for unsigned COMP-3, and for 8-byte unsigned COMP past INT64_MAX. | `cobol_get_int` refuses a negative value for an unsigned picture. `cobol_set_int` refuses to write one. | `corpus/fuzz_cobol_copybook/regress-unsigned-negative-sign` and `-2`; 10 checks in `kernel/src/legacy/test_cobol.c` |
| F2 | fuzz_ipfs_cid (crash after 114,882 execs) | kernel/src/ipfs_node/ipfsn_multiformats.c | `ipfsn_base58_decode` applied its 96-byte bound without the leading '1's. It returned up to 96 + zeros bytes, which `ipfsn_base58_encode` then refused. | Leading zeros count toward the bound. | `corpus/fuzz_ipfs_cid/regress-base58-leading-zeros-bound`; check in `test_ipfs_node.c` |
| F3 | fuzz_econ_vino under `-fsanitize=integer` (seeded mode, input 11) | kernel/src/vino/vino.c | `total_volume[cap] += amount` wrapped back toward zero. | The statistic saturates. The harness now also checks that volume never decreases (V5). | `corpus/fuzz_econ_vino/regress-volume-wrap`; check in `test_vino.c` |
| F4 | MSan job (test_zmedia) | kernel/src/invproof/invproof.c | `zxi_build` never wrote header bytes 80..83, yet the seal covered them. The witness therefore carried stale bytes from the caller's buffer. | Written as zero. The header comment now names the reserved field. | check in `test_invproof.c` |
| F5 | ASan + UBSan job (clang float-cast-overflow) | kernel/src/cotier/test_ct_reconcile.c (test code) | The test converted out-of-range doubles to int32, which is undefined behaviour. | Clamped conversion in the test. | the test itself |
| F6 | integer job | pay_util.c, pay_ledger.c, cbank/cb_util.c | Correct code that relied on unsigned wrap: wrap-then-compare overflow checks, `while (n--)`, and negating via `0 - x`. | Rewritten to check before the operation. No behaviour change. | the existing pay/cbank tests under the integer job |
| F7 | ASan + UBSan job (root OS-layer tests: test_physics, test_integration, test_audiogenomics) | axiom_matrix/axiom_matrix_core.h, audiogenomics/audiogenomics_core.c, physics_sim/physics_core.c | Hash code cast negative doubles straight to `uint64_t`. The physics conservation check turned large kinetic energies into int64 rationals (×10^6), which overflowed. Both are undefined behaviour. | The hashes cast through `int64_t` (same values on x86-64). Kinetic energy is summed in double. | the OS-layer tests under the ASan + UBSan job |

### Found earlier in this branch's work (before the runs above)

These fixes were already in the working tree when this campaign resumed. Their crash inputs
are committed as regression seeds. The execution counts of those earlier runs were not
recorded, so none are claimed here.

| module | problem | regression seeds |
|---|---|---|
| kernel/src/vino/vino.c | `vino_get_balance` read past `balance[]` for an out-of-range capital. The RMAG quota shadow could signed-overflow (including an INT64_MIN result that `rational_normalize` negates). | `corpus/fuzz_econ_vino/regress-get-balance-oob`, `regress-rmag-quota-overflow`, `regress-rmag-quota-int64-min` |
| kernel/src/count_house/count_house.c | A deposit top-up could wrap a bucket's balance. It now returns -3 and changes nothing. | `corpus/fuzz_econ_count_house/regress-deposit-balance-wrap` |
| kernel/src/finance/triple_ledger.c | In the Q32.32 build, balance, total, health and report sums could signed-overflow on edge values. Such postings are now refused, and report sums saturate. | `corpus/fuzz_econ_triple/regress-post-total-overflow`, `regress-health-sum-overflow`, `regress-export-sum-overflow` |

## Sanitizer jobs (local reproduction of the CI commands)

Each job was run locally with the same command as CI. Runs used a private `/tmp` (other
workers share the machine's `/tmp/test_*` paths).

| job | result |
|---|---|
| sanitize-asan-ubsan: verify-all, every hosted test rebuilt with clang ASan + UBSan | pass (after F5 and F7; all 3,987 PASS lines of verify-all). LeakSanitizer off (sched_yield clash, docs/AUDIT_REPORT.md) |
| sanitize-integer: verify-all, economy/pay/rational/RMAG/cbank/zcapital/porter_house/surplus tests under `-fsanitize=integer` | pass |
| sanitize-integer: ledger property harnesses under ASan + UBSan + integer (500 inputs each) | pass |
| sanitize-msan: corpus replay + properties under MSan | pass |
| sanitize-msan: host test subset (`tools/msan_tests.sh`) | pass (after F4) |
| TSan | no job. Nothing in kernel/src or kernel/arch/hosted creates threads; the hosted notifier forks a separate process. |

## What this does not show

* There is no formal verification and no MC/DC coverage target. `docs/COVERAGE.md`
  reports measured MC/DC numbers, and they are low.
* The 300 s targets got light fuzzing. tls_handshake, econ_vino and econ_pay had fewer
  than 20,000 executions each.
* Most harnesses run each module in isolation. Cross-module protocol state (multi-message
  TLS sessions, peer-to-peer exchanges) is fuzzed only as far as one input reaches.
