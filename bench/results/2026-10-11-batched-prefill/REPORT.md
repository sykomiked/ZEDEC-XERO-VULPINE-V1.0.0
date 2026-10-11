# Batched prefill on the integer engine (2026-10-11)

Prefill reads a prompt several tokens at a time. Before this change each
weight row was unpacked and dotted once per token, so prefill ran at about
generation speed. Now each weight block is unpacked once and multiplied
against a tile of up to 32 tokens.

## Results

Model: Qwen2.5-1.5B-Instruct Q4_K_M (sha256 6a1a2eb6…), the same file for
both engines. Machine: 4-core Intel Xeon (Cascade Lake, AVX-512 VNNI),
2.8 GHz, gcc 13.3, `-O2`. Every program used 4 threads. llama.cpp is commit
23b0202 on the CPU (`-ngl 0`). Each ZXV row is the best of two runs. Rows
were measured back to back, because this shared host's speed moves by about
15% over time. Script: [logs/bench.sh](logs/bench.sh); output:
[logs/bench_table.txt](logs/bench_table.txt).

Prefill, tokens/s:

| prompt | before (batch 32) | after, batch 32 | after, batch 128 (new default) | llama.cpp | after / llama.cpp |
|---|---|---|---|---|---|
| 128 tokens | 19.6 | 48.3 | **56.0** | 132.6 | 42% (was 15%) |
| 470 tokens | 16.2 | 41.1 | **49.9** | 121.0 | 41% (was 13%) |

That is 2.9x faster at 128 tokens and 3.1x at 470.

By kernel set at batch 128, tokens/s for 128 / 470 tokens:

- avx512vnni: 56.0 / 49.9
- avx2: 38.4 / 31.9
- c: 3.5 / 3.4

The C reference was not changed.

Generation was not the target here. It also rose, from 11.3 to 15.0 tokens/s
at 128 tokens, because attention now runs in threads. llama.cpp generates at
21.6. On this machine the AVX2 set has generated faster than the AVX-512 set
before (22.6 against 14 in one earlier run), so the per-token kernel choice is
worth a separate look.

## Same bits

`zt.logits_fnv` hashes every logit of every evaluated token. It is identical
for:

- the old engine and the new engine;
- the c, avx2, avx512 and avx512vnni kernel sets;
- batch sizes 16 to 256;
- 1 to 4 threads.

The values are `15a77e3f8755059c` at 128 tokens and `1394d5ac3faf38b9` at 470
tokens, -n 16. `kernel/src/tensor/test_zt_simd.c` checks every new kernel
against the C reference:

- Q4_K, Q6_K and Q8_0;
- 4 to 150 tokens;
- row ranges split the way threads split them;
- attention over the Q16 KV cache.

These checks also run under ASan/UBSan, and the NEON kernels run under
qemu-aarch64. NEON matches C bit for bit, but its speed has not been measured
on Apple silicon yet.

## How it works

- `zt_kern.h` `zt_mmb_t`: activations are repacked token-major in groups of
  16, with each block's power-of-two exponent and exact sum. Kernel sets with
  `mm_batch` take a range of weight rows and fill those rows of all tokens.
  This is used when a batch holds at least 4 tokens.
- AVX-512: a tile of 4 rows by 32 tokens per 32-weight sub-block, using
  VPDPWSSD with VNNI or VPMADDWD + add without it. The scale terms are exact
  64-bit sums, and their shift shortcuts are exact. Code:
  `zt_simd_x86_mmb.h`, compiled once per instruction choice.
- AVX2: 4 rows by 16 tokens. NEON: 4 rows by 8 tokens, using SMLAL.
- Attention is split into head x token-tile items that run in threads. The
  Q16 cache has SIMD score and value kernels, which are exact 32x32 to 64-bit.
- The per-token steps between matrix products (norms, RoPE, SiLU, residuals,
  quantising activations) also run in threads.
- The default prefill batch is now 128 in `zt_bench` and 64 in the app glue.

Time inside prefill at 470 tokens: matrix products fell from 22 s to 6.4 s, and
attention fell from 9 s to 1.5 s.

## What is left against llama.cpp

The tile reaches about 34 GMAC/s per core. The dot products alone could reach
88. Most of the gap is the per-sub-block 64-bit scale arithmetic, which keeps
the result exact and identical on every CPU. llama.cpp rounds activations to
8 bits per block and uses a float scale instead.

## Harness fixes in this change

- `run_tensor_bench.py` now passes `--threads` to `zt_bench`. The default is
  every CPU, and the same count goes to llama.cpp.
- `llama-bench` and the greedy check run with `-ngl 0`, so a Metal build no
  longer compares its GPU with our CPU.
- The greedy check runs with `-no-cnv`, so an instruct model's chat template
  no longer wraps the prompt and causes false mismatches.
- `make -C bench tensor` takes `THREADS=` and `BATCH=`.
