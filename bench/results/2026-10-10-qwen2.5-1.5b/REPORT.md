# Qwen2.5-1.5B-Instruct on the ZXV integer engine (2026-10-10)

This is the first run of a real open-weights model through the ZXV tensor engine
(`kernel/src/tensor`). It compares the engine with llama.cpp, the usual way such a model
is run, and then plugs the engine's golden-ratio parts (zt.h T3, T7, T15 and zt_lattice.h
T17) into the model to see how each one changes the output. Every number here was
measured on the machine described below. Where something is inferred and not measured,
the text says so.

## Setup

| | |
|---|---|
| Model | `qwen2.5-1.5b-instruct-q4_k_m.gguf` from `Qwen/Qwen2.5-1.5B-Instruct-GGUF`, Apache-2.0, 1,117,320,736 bytes |
| SHA-256 | `6a1a2eb6d15622bf3c96857206351ba97e1af16c30d7a74ee38970e434e9407e` (matches Hugging Face's LFS hash; the file is never committed) |
| Machine | cloud container, Intel Xeon @ 2.10 GHz, 4 vCPUs, AVX2 + AVX-512 (VNNI, BF16, FP16) |
| arm64 | the same engine cross-built with `aarch64-linux-gnu-gcc -static`, run under `qemu-aarch64` (emulated, so its speed means nothing) |
| ZXV build | `bench/Makefile`, `-O2`, kernels `c`, `avx2`, `avx512` (x86) and `c`, `neon` (arm64), 4 threads, Q16 KV cache |
| Reference | llama.cpp commit `781dbc5` (2026-10-10), `GGML_NATIVE=ON`, CPU only, 4 threads, driven by `bench/tensor/compare/llama_ref.cpp` |
| Text set | the first 512 tokens of WikiText-2 (raw, test split, `Salesforce/wikitext`), tokenized by llama.cpp; both engines get exactly the same token ids |
| Prompt set | the 8 fixed questions in `bench/tensor/real_model_test.py`, Qwen chat template, greedy, up to 64 new tokens |

## Summary

| Question | Result |
|---|---|
| Does the real model run on ZXV? | Yes. 8 of 8 prompts answered correctly. |
| Same bits on every kernel, thread count and batch size? | Yes, on all 8 prompts and on the 512-token text. |
| Same bits on x86_64 and arm64? | Yes, all 8 prompts (FNV-1a of every logit vector matches). |
| Same answers as llama.cpp? | Yes, token for token on all 8 prompts. |
| Accuracy against llama.cpp | Perplexity 9.371 against 9.362 (0.10% higher). Next-token choice agrees at 95.5% of positions. |
| Speed against llama.cpp | Slower: about 11 tok/s against 15 to 18 tok/s generating, and about 22 against 80 to 200 tok/s reading the prompt. |
| Golden-ratio parts | Lossless coil + holographic coding: no change. Phi scales: small harm. E8 lattice: clearly better than the plain 2-bit quantiser it replaces, but 2-bit weights of any kind need retraining. Lossy holographic decode: breaks the model. |
| Defect found | The first token of a text (no context yet) differs sharply from llama.cpp (F1 below). |

## 1. The plain engine against llama.cpp

### 1.1 Answers (logs/zxv_prompts_x86_64.log, logs/llama_cpp_greedy_compare.json)

| # | Question | ZXV answer | Same tokens as llama.cpp |
|---|---|---|---|
| 1 | Capital of France | Paris | yes |
| 2 | 17 + 25 | 42 | yes |
| 3 | Days in a leap year | 366 | yes |
| 4 | Red Planet | Mars | yes |
| 5 | "thank you" in Spanish | gracias | yes |
| 6 | Symbol for gold | Au | yes |
| 7 | Python string reverse | ```` ```python\ns[::-1]\n``` ```` | yes |
| 8 | Author of Romeo and Juliet | William Shakespeare | yes |

The two tokenizers produced the same prompt token counts on all 8 prompts (31 to 35 tokens).

### 1.2 Determinism (logs/zxv_prompts_*.log, logs/logits_vs_llama_cpp.json)

* **ZXV:** for every prompt, the C, AVX2 and AVX-512 kernels on x86_64 and the C and NEON
  kernels on arm64 gave the same FNV-1a hash over every logit vector they computed. On the
  512-token text, AVX-512 with 4 threads and batch 32 gave the same logit file, byte for
  byte, as plain C with 1 thread and batch 7.
* **llama.cpp:** on this one machine its logits did not change between 1 and 4 threads.
  They did change with the batch size: physical batch 512 against 7 moved the logits by
  0.138 on average (maximum 1.88), changed the most likely next token at 5.1% of positions,
  and gave perplexity 9.348 against 9.362. Floating-point sums give different results when
  they are added in a different order. The integer engine has no such dependence.

What the determinism makes possible: the same prompt gives the same answer on a phone, a
laptop and a server. An answer can be re-run elsewhere and checked bit for bit, and so it
can be signed, attested or settled on the ledger. Whether that matters more than speed is
a product question. This test only shows that the property holds on a real model.

### 1.3 Accuracy on real text (logs/logits_vs_llama_cpp.json)

| | llama.cpp | ZXV |
|---|---|---|
| Perplexity, 511 predictions | 9.362 | 9.371 |
| Top-1 agreement with llama.cpp | | 95.5% |
| KL(llama.cpp ‖ ZXV), mean over positions 1..510 | | 0.0081 nats |
| KL at position 0 | | 4.50 nats (F1) |
| Mean / max absolute logit difference | | 0.146 / 9.68 (the max is at position 0) |

For scale: llama.cpp's own batch-size change in 1.2 moved its logits by about the same mean
amount (0.138) and its top-1 by about the same share (94.9% agreement). Away from position
0, ZXV differs from llama.cpp by about as much as llama.cpp differs from itself. Neither is
"the truth": both approximate the same Q4_K_M weights. ZXV re-rounds them to 8-bit blocks
and holds activations at 16 fraction bits, while llama.cpp rounds activations to 8 bits for
its 4-bit dot products.

### 1.4 Speed (4 vCPUs; logs/zxv_prompts_x86_64.log, logs/llama_cpp_greedy_compare.json)

| | ZXV C | ZXV AVX2 | ZXV AVX-512 | llama.cpp |
|---|---|---|---|---|
| Generate (tok/s, 8 prompts) | 4.1 to 5.2 | 9.8 to 12.2 | 10.2 to 12.9 | 14.5 to 18.8 |
| Prompt, 31 to 35 tokens (tok/s) | 15 to 18 | 20 to 24 | 22 to 24 | 83 to 205 |
| 512-token prefill (tok/s) | 4.6 (1 thread) | | 16.2 | 101 |
| Model load | about 36 s (weights converted into the arena) | | | under 1 s (mmap) |

The answers in the prompt set are 1 to 8 tokens long, so the generation figures are noisy.
The gap at prefill is the biggest: llama.cpp runs prompts as matrix-matrix products and
ZXV runs them one row at a time per token. That is the main speed work ahead. ZXV meets
the roadmap's 10 to 18 tok/s generation target on this container. The M3 Max has not been
measured.

### F1. First-token defect

With no context, at the first position of a text, ZXV's prediction differs sharply from
llama.cpp's: KL 4.5 nats, and ZXV's top logit is 18.52 against llama.cpp's 10.39 (the same
token wins). From position 1 on, the KL drops to about 0.01. An instrumented build
(logs/position0_instrumented.log) shows values up to 9,400 inside the forward pass at that
position, without saturating the int32 range. Qwen models are known for very large
activations on their first token. **Inferred, not proven:** the likely cause is ZXV's
block quantisation of a matrix product's input (16 bits per block of 32, one exponent per
block), which loses the small values that share a block with one huge value. It matters
when a prompt is a single token. Chat prompts start with the template's `<|im_start|>`
token, and the 8 prompts were unaffected. Next step: a unit test that feeds a block with
one outlier through the kernels and compares against a 64-bit reference, then a fix (for
example, splitting outlier blocks) that must keep the bit-identity guarantees.

## 2. The golden-ratio parts inside the model (logs/golden_*.log, logs/golden_variants_compare.json)

`bench/tensor/zt_golden.c` loads the model normally, rewrites every converted weight matrix
with one engine part (196 layer matrices plus the output matrix, 1,543,569,408 weights; the
token-embedding rows read from the file are untouched), and then runs the same 512 tokens.
KL and top-1 skip position 0 so that F1 does not hide the effect.

| Variant | What it does | Perplexity | KL vs plain ZXV | Top-1 vs plain ZXV |
|---|---|---|---|---|
| none | the plain engine | 9.371 | 0 | 100% |
| requant | control: re-round every block to 8 bits with an exact scale | 9.371 | 0.00000004 | 100% |
| phi | T3: block scale snapped up to the next power of phi (full precision) | 9.404 | 0.00093 | 99.2% |
| phi16 | T3 as the engine ships it (`zt_quantize(golden = true)`, Q16 ladder) | 9.396 | 0.00078 | 98.0% |
| holo10 | T7 + T15: each row's codes on a golden coil, holographic encode, decode all 10 shells | 9.371 | 0 (bit-identical) | 100% |
| holo9 | the same, decoding 9 shells | 332,055 | 10.27 | 0.2% |
| int2down | control: 4-level scalar quantiser, 2 bits a weight, ffn_down only | 20.52 | 0.821 | 62.7% |
| e8down | T17: E8 lattice, 1.875 bits a weight, ffn_down only | 13.97 | 0.413 | 72.2% |
| int2 | control: 4-level scalar quantiser on every matrix | 52,602 | 8.41 | 1.8% |
| e8 | T17: E8 lattice on every matrix | 7,698 | 6.59 | 7.1% |

How the model behaves with each part, against how it behaves normally:

* **Phi scales (T3): a small harm.** Snapping a block's scale up to a power of phi
  coarsens its step by up to 1.618x, about 1.27x on average. The model loses a little
  precision (perplexity +0.3%) and changes 0.8% to 2% of its top choices. The requant
  control changed nothing, so the loss comes from the phi ladder itself, not from
  re-rounding. The shipped Q16 ladder holds phi^k only to whole Q16 units, which is coarse
  at weight-scale sizes (around 1e-4). It happened to land slightly better than the exact
  ladder here. Recommendation: keep T3 off for model weights.
* **Coil + holographic coding, lossless (T7, T15): no effect on the output, and the data
  takes more space.** Decoding all 10 shells gave the model back bit for bit (0 weights
  changed). The residuals have an entropy of 8.12 bits a weight against 7.31 for the codes,
  so holographic coding makes weights harder to compress. Each residual is the difference
  of two nearly independent neighbours, which spreads it out (theory says about half a bit
  more; measured 0.81). Placement rule, agreed with the owner: residuals stay on the device
  and the network carries the codes. `kernel/Makefile` now fails if a network module
  calls the holographic encoder or decoder. zt.h T15 records the measurement.
* **Holographic coding, lossy (9 shells): breaks the model.** Leaving out the innermost
  shell gives 34% of the weights their ancestor's value (518,521,180 weights changed), and
  the model outputs nonsense. Weights must always be decoded with all 10 shells.
* **E8 lattice (T17): a real advantage over the plain quantiser it replaces.** At 1.875
  bits a weight, fewer than the 2-bit control, E8 on the MLP output matrices gave
  perplexity 13.97 against 20.52, with half the KL. That fits the lattice's denser
  packing. On every matrix, both break a model this small (7,698 against 52,602), because
  2-bit weights without retraining are too coarse for a 1.5B model. E8 is the one golden
  part that improves on the conventional method it replaces, but it is a 2-bit method.
  Using it for real needs either a model trained or fine-tuned for low-bit weights, or a
  bigger E8 codebook (more bits a weight) to compare against 4-bit methods. The E8 pass
  over every matrix took 31 minutes on 4 cores (the nearest-point search), so it belongs at
  conversion time, not at load.
* **Not tested here:** T4 Fibonacci tiling, T6 Fibonacci hashing, T11 fractal placement and
  T12/T14 field/AC/interference are not in the forward pass and do not change weights. The
  engine documents T4 as giving the same result as the plain product. Their effect would be
  on speed and memory layout, which is the next experiment for them.

## 3. Logs and what they mean

| File | What it shows |
|---|---|
| logs/zxv_prompts_x86_64.log / .json | `real_model_test.py` on x86_64: each prompt's answer, correctness, and per-kernel prefill/generation speed, token count and `fnv` (FNV-1a of every logit vector). Equal `fnv` values mean bit-identical computation. |
| logs/zxv_prompts_arm64_qemu.log / .json | The same on the arm64 build under qemu. Compare its `fnv` values with the x86 log: all 8 match. Its tok/s are emulation speed, not arm64 speed. |
| logs/llama_cpp_greedy_compare.json | Per prompt: ZXV and llama.cpp generated ids (4 and 1 threads), prompt token counts, and both engines' speeds. `same_answer` means identical token ids. |
| logs/logits_vs_llama_cpp.json | Every-position comparison on the 512 WikiText-2 tokens: perplexity of each engine, top-1 agreement, KL (mean, 99th percentile, max), logit differences. The second entry is llama.cpp against itself at two batch sizes, the yardstick for "normal" variation. |
| logs/position0_instrumented.log | F1: the largest values inside the forward pass, and the positions with the largest KL. |
| logs/golden_<variant>.log | `zt_golden` output: how many matrices and weights it rewrote, the shift range of the converted blocks (0..24), the transform time, and for holo the code and residual entropies and how many weights decoding changed. |
| logs/golden_variants_compare.json | The table in section 2. |

## 4. Reproducing

```sh
make -C bench zt_bench zt_dump zt_golden            # into /tmp/zxv_bench
make -C bench qwen ARM64=1                          # download (cache outside the repo), prompts, x86 + arm64 bits
# llama.cpp reference (built separately):
g++ -O2 -std=c++17 -I$LLAMA/include -I$LLAMA/ggml/include bench/tensor/compare/llama_ref.cpp \
    -L$LLAMA/build/bin -lllama -lggml -lggml-base -Wl,-rpath,$LLAMA/build/bin -o $ZXV_CMP_DIR/llama_ref
$ZXV_CMP_DIR/llama_ref dump $MODEL wikitext2_test.txt 512 ids.bin ref_t4.f32 4
/tmp/zxv_bench/zt_dump $MODEL ids.bin zxv.f32 --threads 4
python3 bench/tensor/compare/cmp_logits.py          # needs ref_ub7.f32 too (llama_ref ... 4 7)
sh bench/tensor/compare/run_golden.sh && python3 bench/tensor/compare/cmp_golden.py requant phi phi16 holo10 holo9 e8down int2down e8 int2
```

## 5. Follow-ups this report opens

1. F1: outlier-aware activation blocks, with a kernel test, keeping bit-identity.
2. Speed: matrix-matrix prefill, and loading converted weights without the 36-second
   conversion (cache the arena, or use Q8_0 files in place).
3. T4/T6/T11 on speed and memory, measured the same way.
4. E8 at more bits a weight against Q4/Q5, and E8 or quaternary weights on a model trained
   for low bits.
5. Run the same suite on the owner's M3 Max (native NEON, real arm64 speed).
