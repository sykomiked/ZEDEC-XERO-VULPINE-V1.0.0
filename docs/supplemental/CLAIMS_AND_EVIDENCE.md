<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Claims and evidence

This page is for academic and skeptical readers. It lists the project's main
claims, how strong the evidence for each one is, where that evidence is in
the repository, and how to check it yourself. Every number is copied from the
file named next to it. If you find a number here that does not match its
source, the source wins, and please open an issue.

## How to read the status column

| Status | Meaning |
|---|---|
| **Measured** | A number was measured on a stated machine, and the report gives the setup and logs. |
| **Tested in CI** | An automated test checks the claim against an external reference (a standard's test vectors, another implementation, a conservation identity) on every relevant push. |
| **Implemented, not benchmarked** | The code exists and has tests for correctness, but nothing has measured its performance or real-world benefit. |
| **Design intent** | Described in the documents; not built, or built only in part. |

"CI" means the jobs in `.github/workflows/ci.yml` and the related workflows.
Which job runs when is explained in `docs/CI.md`. Most checks can also be run
locally with `scripts/verify-local.sh --all` (or `--docker --all`).

## 1. The AI runtime

| Claim | Status | Evidence | How to reproduce |
|---|---|---|---|
| The tensor engine uses integer arithmetic only, with no floating point. | Tested in CI | `kernel/src/tensor/` (`zt.h` rules T1 to T16), `test_zt.c`; `docs/TENSOR_ENGINE.md` | `make -C kernel verify-all` |
| The engine gives the same output bits on every CPU and kernel set. | Measured; kernel equivalence tested in CI | `bench/results/2026-10-10-qwen2.5-1.5b/REPORT.md` §1.2 and §1.5: the C, AVX2 and AVX-512 kernels on x86_64 and the C and NEON kernels on arm64 gave the same hash over every logit; `kernel/src/tensor/test_zt_simd.c` checks each SIMD kernel against the C reference | `make -C bench qwen ARM64=1` (downloads the model, about 1.1 GB, outside the repo). The arm64 run is under qemu emulation, not on arm64 hardware. |
| A real model runs on the engine: Qwen2.5-1.5B-Instruct (Q4_K_M) gives the same greedy answers as llama.cpp on the test prompts. | Measured | `README.md`; `docs/release/HUGGING_FACE_PAGE.md`; `bench/results/2026-10-10-qwen2.5-1.5b/REPORT.md` §1.1 (8 of 8 prompts, token for token) | `make -C bench qwen`; prompts in `bench/tensor/real_model_test.py` |
| Perplexity 9.371 against llama.cpp's 9.362. | Measured | `README.md`; `docs/release/HUGGING_FACE_PAGE.md`; `bench/results/2026-10-10-qwen2.5-1.5b/REPORT.md` §1.3 (first 512 tokens of WikiText-2, 511 predictions) | REPORT.md §4 (needs a separately built llama.cpp) |
| Against an exact float64 forward pass, native K-quant ZXV is closer than llama.cpp (mean KL 0.000008 against 0.0073; top-1 agreement 99.8% against 95.9%). | Measured | `bench/results/2026-10-10-qwen2.5-1.5b/REPORT.md` §1.5, `bench/results/2026-10-10-qwen2.5-1.5b/logs/vs_float64_reference.txt` | `bench/tensor/compare/ref_f64.py` and `cmp_ref64.py`, as listed in REPORT.md §4 |
| E8 lattice quantiser (1.875 bits a weight) beats a plain 2-bit scalar quantiser on the MLP output matrices (perplexity 13.97 against 20.52). | Measured | `bench/results/2026-10-10-qwen2.5-1.5b/REPORT.md` §2. The same section says 2-bit weights of any kind break a model this small without retraining. | `sh bench/tensor/compare/run_golden.sh`, then `cmp_golden.py` |
| Batched prefill is about 3x faster with the same output bits. | Measured | `bench/results/2026-10-11-batched-prefill/REPORT.md`: 2.9x at 128 tokens and 3.1x at 470 tokens; `zt.logits_fnv` identical across kernel sets, batch sizes 16 to 256 and 1 to 4 threads. `README.md` lists this as "landing" (#28). | `bench/results/2026-10-11-batched-prefill/logs/bench.sh` |
| GGUF loader, byte-level BPE tokenizer and rotary positions match their references. | Tested in CI | `docs/TENSOR_ENGINE.md` (T18 to T20): GGUF values within 1 Q16 unit of a gguf-py file; tokenizer ids match Hugging Face `tokenizers` on 172 texts; RoPE within 1 Q16 unit of a long double reference up to position 4 million | `make -C kernel verify-all` |
| Fuzzed parsers (GGUF, UBH frame, COBOL copybook, FAT32 and others) run 10^7 executions without crashes. | Measured | `fuzz/FUZZ_REPORT.md` (campaign of 2026-10-10; one COBOL crash found and fixed) | `make -C fuzz smoke`; longer runs per `fuzz/README.md` |

### Negative results for the AI runtime

- **ZXV is slower than llama.cpp.** On an M3 Max with 8 threads, ZXV ran
  prompt processing (prefill) at 70 tokens/s and generation at 37 tokens/s,
  against llama.cpp's CPU backend at 202 and 91
  (`docs/release/HUGGING_FACE_PAGE.md`). `README.md` says the engine "is still
  slower than llama.cpp". On a 4-vCPU cloud container the gap was larger for
  prompt reading: about 22 against 80 to 200 tokens/s
  (`bench/results/2026-10-10-qwen2.5-1.5b/REPORT.md`, Summary). After batched
  prefill, ZXV reached 42% and 41% of llama.cpp's prefill speed at 128 and 470
  tokens (`bench/results/2026-10-11-batched-prefill/REPORT.md`).
- **Model load is slower** in the converted path: about 36 s against under 1 s
  for llama.cpp (REPORT.md §1.4). The native K-quant path removed the
  conversion (set-up 0.03 s, §1.5).
- **Most golden-ratio parts do not help a real model.** Phi-power block
  scales did slight harm; lossless coil and holographic coding changed nothing
  and made the weights harder to compress (8.12 against 7.31 bits a weight of
  entropy); lossy holographic decoding broke the model (REPORT.md §2).
- **Speed of the coil layout and Fibonacci tiling is not measured**
  (`docs/TENSOR_ENGINE.md`, "Limits, stated plainly").
- `bench/README.md` states that "no performance results are claimed here" and
  that nothing in that directory has been run on real target hardware. The
  M3 Max figures above come from the release page, not from a report under
  `bench/results/`.

## 2. Post-quantum security

| Claim | Status | Evidence | How to reproduce |
|---|---|---|---|
| ML-KEM, ML-DSA, SLH-DSA and HQC come from vendored upstream reference code and match the official known-answer vectors. | Tested in CI | `docs/PQ_MATRIX.md` (NIST ACVP vectors, the official HQC v5.0.0 KATs, RFC 7748); `kernel/src/pqsec/test_pq_matrix.c` | `make -C kernel verify-all` |
| The KATs pass on several CPUs, including a big-endian one. | Tested in CI | `kernel/Makefile` target `kat-cross`: aarch64, riscv64, armhf, i686 and s390x (big-endian), cross-compiled and run under qemu-user; CI job `kat-cross` | `KAT_CROSS_STRICT=1 make -C kernel kat-cross` |
| The MATRIX level costs about 13 ms per ephemeral handshake (about 5.3 ms with `-mpclmul`) and about 1.6 s / 2.5 ms to sign / verify. | Measured | `docs/PQ_MATRIX.md` §3 (one core, `-O2`, measured by `test_pq_matrix`) | run `test_pq_matrix` without `--no-bench` |
| One PQ session layer (ML-KEM-1024 + ML-DSA-87) for peer links. | Tested in CI | `README.md`; `docs/GAP_AUDIT.md` progress row A3; `kernel/src/vinea/vna_session.h` | `make -C kernel verify-all` |
| A secret-dependent branch check runs on ML-KEM-768 and ML-DSA-65. | Tested in CI | `kernel/Makefile` target `ct-valgrind`; `kernel/src/pqsec/test_ct_valgrind.c` | `make -C kernel ct-valgrind` |

### Negative results for security

- The implementations are reference grade. They "have not been hardened
  against every side channel or against fault injection" (`docs/PQ_MATRIX.md`
  §4). The valgrind check is "not a proof of constant time" (`kernel/Makefile`).
- HQC is selected by NIST but not yet a final FIPS standard
  (`docs/PQ_MATRIX.md` §4).
- The `pmull` HQC backend for AArch64 "builds; not yet timed on hardware"
  (`docs/PQ_MATRIX.md` §3).

## 3. The operating system

| Claim | Status | Evidence | How to reproduce |
|---|---|---|---|
| The kernel builds for arm64, x86_64, riscv64, riscv32 and arm32 and boots under emulation. | Tested in CI | `README.md`; CI jobs `boot` and `boot-extra` through `build_system/ci_boot_check.sh`, which requires `[BOOT_OK]` and fails on any fault marker (`docs/AUDIT_REPORT.md`) | `scripts/verify-local.sh --boot` (arm64 and x86_64) or `--all` |
| Floating point is banned in kernel images at compile time. | Tested in CI | `build_system/Makefile.arm64` (`-mgeneral-regs-only`, `-Werror=double-promotion`, `-Werror=float-conversion`); `docs/ARCHITECTURE.md` rule R1 | `./build.sh arm64` |
| Bare-metal UEFI disc images for x86_64 and arm64 boot in QEMU and reach `[AI_OK]`. | Tested in CI | `docs/BAREMETAL.md`; CI job `uefi-disc`. `README.md` lists the discs as "landing" (#25). | `bash build_system/uefi_boot_check.sh x86_64 "" 90 "[AI_OK]"` inside the `Dockerfile.baremetal` image |
| The kernel image contains the swarm and tensor engine. | Implemented, not benchmarked | `docs/AUDIT_REPORT.md`: swarm and tensor are linked into the arm64 and x86_64 images "but only run a boot self-check ([AI_OK])" | boot check above |

### Negative results for the OS

- **ZXV has not yet booted on a physical machine.** `README.md`: "It has not
  yet booted on a physical board", and "Booting a physical machine is still to
  do". `docs/BAREMETAL.md` gives the steps for a first physical boot on an old
  x86-64 UEFI PC; the result is not yet recorded.
- No model runs inside the kernel image, and riscv and arm32 images do not
  link the swarm (`docs/AUDIT_REPORT.md`, requirement 11).
- 36 modules under `kernel/src` are linked by no test or fuzz harness and are
  effectively at 0% coverage (`docs/COVERAGE.md`).

## 4. Swarm and economy

| Claim | Status | Evidence | How to reproduce |
|---|---|---|---|
| Tokens-per-cycle budget with Fibonacci scaling across swarm levels; `used <= allotted` after every consume. | Tested in CI; lemma checked in Lean on a model | `kernel/src/swarm/swarm_budget.{h,c}`, `test_swarm_budget`; `docs/FORMAL_INVARIANTS.md` H1, H2 | `make -C kernel verify-all`; `proofs/check.sh lean` |
| The pay ledger conserves value: debits equal credits per group, and supply equals minted minus burned, including with the 0.08889% fee. | Tested in CI; invariants model-checked by TLC over small bounds | `kernel/src/pay/test_pay.c`; `docs/FORMAL_INVARIANTS.md` P1 to P11 | `proofs/check.sh tla` |
| The assurance fee is exactly `floor(price × 8889 / 10,000,000)`, split 50/25/15/10. | Tested in CI | `kernel/src/pay/pay_assure.h`; `docs/TWO_NODE_DEMO.md` (1,250.00 VFV price gives a 1.11 VFV fee) | `make -C kernel verify-all` |
| Two nodes can ask, answer and pay each other over a PQ session. | Tested in CI (two loopback addresses on one machine) | `docs/TWO_NODE_DEMO.md`; `.github/workflows/zxvd.yml` runs `tools/zxv_two_node_demo.py ... local --check`. `README.md` lists the demo as "landing" (#31). | `python3 tools/zxv_two_node_demo.py local` |
| VFV coins are minted in Fibonacci denominations only. | Tested in CI | `docs/AUDIT_REPORT.md` (owner decision 2026-10-11; `vino_mint` refuses other faces); `kernel/src/swarm/swarm_denom.h` for token denominations | `make -C kernel verify-all` |

### Negative results for the economy

- The VFV in the two-node demo "is not backed by anything", there is no
  network-wide treasury, and the CI run tests real sockets but "not a real
  network" (`docs/TWO_NODE_DEMO.md`, "Honest limits").
- The formal models are hand-written from the C code, with "no refinement
  proof linking them to the C source or the binary, so this is not formal
  verification of the kernel" (`docs/FORMAL_INVARIANTS.md`). Without a nonce,
  a replayed request is accepted again once it leaves the journal window, and
  TLC is required to find that counterexample (P10/P11 there).
- In the triple ledger, "trial balance always zero" is false; the model
  requires the counterexample (`docs/FORMAL_INVARIANTS.md` §2).
- V-Bill issuance, the solvency vault, custodian receipts and estate
  succession are now implemented and host-tested in CI (`kernel/src/vauction`,
  `kernel/src/svault`, `kernel/src/estate`; `make -C kernel test-econ`), but
  the app does not call them yet. `docs/GAP_AUDIT.md` C2 to C6 predate this
  code and still call them design intent.
- `docs/GAP_AUDIT.md` found the economy modules were "not in the app" on the
  audited commit; its Progress table records which links have since been made.

## 5. Quality process

| Claim | Status | Evidence | How to reproduce |
|---|---|---|---|
| The host test suite passes in a clean checkout. | Tested in CI | `docs/AUDIT_REPORT.md`: 3,984 passing checks after the audit fixes | `make -C kernel verify-all` |
| Line coverage 87.0%, branch 67.9%, function 88.5%, MC/DC 32.2% over the modules that tests link. | Measured | `docs/COVERAGE.md` (clang 18.1.3, 2026-10-10; it says these imply no compliance claim) | `make -C fuzz coverage` |
| Sanitizers (ASan, UBSan, MSan, integer) run on the tests. | Tested in CI | `docs/CI.md` (jobs `sanitize-asan-ubsan`, `sanitize-integer`, `sanitize-msan`) | `scripts/verify-local.sh --san` |
| A mutation gate fails if a target's score drops below its baseline. | Tested in CI | `tests/MUTATION_REPORT.md`; baselines in `tests/harness/mutator/targets.json`; `docs/ARCHITECTURE.md` gives scores such as 89.6% for the settlement spine | `python3 tests/harness/mutator/mutate.py` (see `tests/MUTATION_REPORT.md`) |
| The audit fixed every critical and high finding it raised. | Measured (by audit) | `docs/AUDIT_REPORT.md`: 15 CRIT and 41 HIGH fixed, 0 open; 74 MED and 53 LOW not fixed | read the report; each finding names the file and line |
| libzxv keeps a stable C ABI that can only grow. | Tested in CI | `sdk/libzxv/tools/abi_check.py`, run in `.github/workflows/sdk-release.yml` | `python3 sdk/libzxv/tools/abi_check.py check ...` as in that workflow |

## 6. Design intent, not yet built

These appear in the documents but should not be read as working features.

- A supercomputer (HPC) version: deferred by the owner (`docs/AUDIT_REPORT.md`, requirement 9).
- One LoRA adapter per swarm agent: no LoRA loading exists (`docs/AUDIT_REPORT.md`, requirement 15).
- A PQ-signed model manifest and the `tools/zxv_pack` converter (`docs/AUDIT_REPORT.md`, requirement 14).
- The MegaROM, a game ROM combining mechanics from many games; its own plan calls the full scope "a decades-scale effort" (`MEGAROM_PLAN.md`).
- A CI layering check over the seven strata (`docs/ARCHITECTURE.md` rule R2; `docs/GAP_AUDIT.md` B1).
- The Trebago, Eve, KRON and MYRA pillars are marked as planned (📋) in `docs/ARCHITECTURE.md` §12, while `sdk/README.md` lists each with its own tests. Check `sdk/pillars/` for the current state.

## Where the documents disagree

Some documents were written at different times. Known differences, so a
reader is not misled:

- `docs/TENSOR_ENGINE.md` ("Limits") says the model runtime "is in pieces" and
  that speed "is not yet measured". The later benchmark reports under
  `bench/results/` show a full forward pass on a real model with measured
  speed. The reports are newer.
- `README.md` lists the bare-metal discs (#25) and the two-node demo (#31) as
  "landing", while this checkout already has the CI jobs that test them
  (`uefi-disc` in `ci.yml`, the demo check in `zxvd.yml`).
- `docs/GAP_AUDIT.md` C1 says Zenny chips are "not in code"; the module
  `kernel/src/zenny/` now exists with a test.
- `docs/AUDIT_REPORT.md` has two lines on Fibonacci-only coins: one says the
  question is answered for VFV coins, the next says it is still open for them.
  The first (2026-10-11, `vino_mint` refuses other faces) is the later one.
- Two speed comparisons with llama.cpp exist, on different machines: a cloud
  container (`bench/results/`) and an M3 Max (`docs/release/HUGGING_FACE_PAGE.md`).
  Compare numbers only within one source.

## How to cite

Use [`CITATION.cff`](../../CITATION.cff) at the repository root; GitHub shows
it as "Cite this repository". When citing a number, cite the report file it
comes from and the commit you read it at, since several of these figures were
updated during development.
