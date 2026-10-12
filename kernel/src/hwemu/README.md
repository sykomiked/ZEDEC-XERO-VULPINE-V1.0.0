<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# hwemu: hardware emulation matrix, profiles and hardware updates

| File | What it is |
|---|---|
| `hwp.h/.c` | ZHP-1 hardware profiles ("the hardware ROM"), the VULPINE-class reference machine R1, `hwp_present` |
| `hwm.h/.c` | the emulation matrix: functions served by HARD, FABRIC, MICRO, PEER, SOFT or REMOTE providers (M1-M9: proof, failover, placement policy, versioned contracts) |
| `hwe.h/.c`, `hwe_ids.h` | the standard engines (SHA-256, Keccak, HMAC, GCD, LOGIC64, tensor dot), each with a SOFT implementation and golden vectors |
| `hwe_vectors.h`, `gen_hwe_vectors.c` | the version-1 golden vectors, generated from the open C code (`make hwe-vectors`) |
| `hwr.h/.c` | hardware updates as signed images: two slots, probation, rollback, S0 quarantine, contract packages, every step recorded (R1-R7) |
| `hwa.h/.c` | the hash-chained hardware audit log that R7 writes |
| `hwr_pq.h/.c` | ML-DSA-87 and the dual ML-DSA-87 + SLH-DSA-SHAKE-256s suite for image signatures |
| `test_*.c` | host tests (`make test-hwemu`, part of `verify-all`) |
| `bench_hwm.c` | emulated paths against native code and a measured ceiling (`make bench-hwemu`) |

The policy files are freestanding C11 with no float; `test-hwemu` builds
them with `-mgeneral-regs-only` to keep it that way.
`tools/check_hwe_vectors.py` re-derives a sample of the golden vectors with
Python's hashlib, hmac and math.gcd. Design and spec:
[`sdk/docs/HW_EMULATION.md`](../../../sdk/docs/HW_EMULATION.md),
[`sdk/docs/HW_PROFILE.md`](../../../sdk/docs/HW_PROFILE.md).
