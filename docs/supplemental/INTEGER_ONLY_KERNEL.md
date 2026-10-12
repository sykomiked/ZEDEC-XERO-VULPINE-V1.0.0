<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Why the kernel bans floating point

Kernel images in ZXV are built so that any use of `float`, `double` or
`complex` fails to compile or link. Everything that would normally use
floating point, including running a language model, is done with whole
numbers instead. This note explains why.

## For the general reader

A computer can do arithmetic on decimals in two ways. Floating point is fast
and convenient, but it rounds a little at almost every step. Different chips,
compilers and orders of adding things up round differently, so the same
calculation can give slightly different answers on a laptop and on a phone.

Whole-number (integer) arithmetic does not have that problem. 7 + 5 is 12 on
every machine ever built. ZXV wants a swarm of AI agents to be checkable: one
machine does some work, another re-runs it and confirms the result bit for bit.
That only works if both machines get exactly the same answer. So the kernel
stores fractional values as scaled whole numbers ("fixed point", for example
1.5 stored as 98304 / 65536) or as exact fractions (1/10 stored as 1 and 10).

A second, plainer reason: on arm64, the kernel's interrupt handler saves only
the general registers, not the floating-point ones. Floating-point state in
kernel code could be overwritten by an interrupt. Banning it removes that
class of bug.

## For the academic reader

IEEE 754 makes each basic operation correctly rounded, but it does not make a
whole program reproducible across platforms. Reassociation, FMA contraction,
vector-width-dependent reduction order, x87 extended precision and differing
`libm` implementations all change results. Parallel reductions in ML runtimes
are a well-known source of run-to-run and machine-to-machine drift.

ZXV takes the route long used in fixed-point DSP and in lockstep game
simulation: restrict the arithmetic to integers so that the result is a pure
function of the input. The tensor engine (`kernel/src/tensor/zt.h`, rules T1
to T20) uses Q16.16 activations, 8-bit weight blocks with integer scales,
integer square root, table-driven 2^x for softmax and SiLU, and 64-bit
fixed-point rotary angles. This is in the same family as integer-only
quantised inference (for example Jacob et al., CVPR 2018; I-BERT, Kim et al.,
ICML 2021), with bit-exact reproducibility as the main goal rather than
speed on integer accelerators.

Where exactness matters more than range, the kernel uses rationals:
`kernel/src/rational/rational.h` gives exact add, subtract, multiply and
divide over int64 ratios with 128-bit overflow detection, and the Sutra
language uses that type for amounts (`kernel/src/sutra/sutra.h`). Its header
states the limit: exact over the rationals, not over the reals.

Determinism enables deterministic replay, witness re-execution, and lockstep
consensus: peers agree on a result because they compute identical bits, not
because they agree on a tolerance.

## What actually exists

| Piece | Where | What it does |
|---|---|---|
| Compile-time ban (arm64, x86_64, arm32) | `build_system/Makefile.arm64`, `Makefile.x86_64`, `Makefile.arm32` | `-mgeneral-regs-only` makes any FP use a compile error; `-Werror=double-promotion -Werror=float-conversion` on top. arm64 also disables auto-vectorisation; the comment there gives the IRQ register-save reason. |
| Ban on riscv64 / riscv32 | `build_system/Makefile.riscv`, `Makefile.riscv32` | Integer-only ISA and ABI (`-march=rv64ima_zicsr -mabi=lp64`, `rv32ima_zicsr -mabi=ilp32`). |
| No soft-float escape hatch | `kernel/compat/softfloat_stubs.c` | Deliberately carries no soft-float helpers, so a float use on riscv fails at link instead of linking silently. |
| Module checks | `kernel/Makefile` (zt_model, paradox, hwemu targets) | Compile modules freestanding for aarch64 with `-mgeneral-regs-only` and for i386, and reject unexpected external symbols. Also checks no kernel image links the hosted SIMD kernels (`zt_simd*.c`). |
| Integer tensor engine | `kernel/src/tensor/`, described in `docs/TENSOR_ENGINE.md` | Fixed-point arithmetic, GGUF loader, BPE tokenizer, RoPE, lattice quantisers. |
| Exact rationals | `kernel/src/rational/` | `rational_t` with detected overflow; tested against floating point in `kernel/Makefile`. |
| Fixed-point replacements | `tests/host/test_fixed_point.c` | Checks fixed-point code against a host `double` oracle ("zero-float boundary" in `kernel/Makefile`). |

## Measured result

From `bench/results/2026-10-10-qwen2.5-1.5b/REPORT.md` (summarised in
`README.md`): Qwen2.5-1.5B-Instruct Q4_K_M on the integer engine answered
8 of 8 fixed prompts with the same greedy tokens as llama.cpp. The C, AVX2 and
AVX-512 kernels on x86_64 and the C and NEON kernels on arm64 produced the
same FNV-1a hash over every logit vector. Perplexity on 512 WikiText-2 tokens
was 9.371 against llama.cpp's 9.362. The same report notes that llama.cpp's
own logits changed with batch size on that machine; the integer engine's did
not.

Two precisions on that result. It was run as a hosted program (the arm64 build
is a static Linux binary under `qemu-aarch64`), not inside a booted ZXV
kernel. And the SIMD kernels it used are hosted-only; kernel images link the
portable C path.

## Limits

- **Speed.** The same report measures ZXV as slower than llama.cpp, for
  example about 11 against 15 to 18 tokens/s generating, and far slower at
  reading the prompt. Integer arithmetic is not free.
- **Not the same numbers as float.** The engine re-rounds weights into its own
  blocks. Determinism means "the same on every machine", not "equal to an
  exact float64 answer"; the report measures that gap separately.
- **Range.** Q16.16 saturates. The engine clamps rather than wraps, and
  `kernel/arch/hosted/zxv_answer_tag.c` withholds an answer whose logits hit
  the rails, but a saturated value is still a lost value.
- **Floats still exist outside kernel images.** Host tests use `double`
  oracles and `-lm`, the comparison tooling uses float64 numpy
  (`bench/tensor/compare/ref_f64.py`), Python fixture generators use floats,
  and model files themselves contain F16/F32 data that the loader converts.
- **Kernels have not booted on physical hardware** (see `README.md`); the
  cross-CPU claim rests on hosted and emulated runs.
