<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# .zxvc converter on Qwen2.5-0.5B-Instruct (2026-10-12)

Roadmap 6.6c, steps M.1 (import) and M.4 (package). Tool: `kernel/tools/zxvc.c`,
format: `kernel/src/tensor/zt_zxvc.h`. Machine: the cloud container (x86_64
Xeon), `gcc -O2`, one thread.

## Sources

| File | Size | SHA-256 |
|---|---|---|
| `Qwen/Qwen2.5-0.5B-Instruct` `model.safetensors` (BF16) | 988,097,824 | `fdf756fa7fcbe7404d5c60e26bff1a0c8b8aa1f72ced49e7dd0210fe288fb7fe` |
| `Qwen/Qwen2.5-0.5B-Instruct-GGUF` `qwen2.5-0.5b-instruct-q4_k_m.gguf` | 491,400,032 | `74a4da8c9fdbcd15bd1f6d01d621410d31c6fc00986f5eb687824e7b93d7a9db` |

Licence: Apache-2.0 (the repository's LICENSE file). Neither file declares it in
its own metadata, so both runs passed `--license apache-2.0`.

## Results

| | GGUF Q4_K_M | safetensors BF16 |
|---|---|---|
| Tensors | 291 (121 exact F32, 170 native K-quant/Q8) | 290 (all exact BF16) |
| Exact blocks by integer width | 16-bit 1,653 · 24-bit 567 · 32-bit 16 | 16-bit 12,097,100 · 24-bit 3,326,618 · 32-bit 14,742 · 64-bit 64 |
| Tensor bytes, source → package | 485,452,288 → 485,335,060 (1.000x) | 988,065,536 → 1,141,788,660 (1.156x) |
| Convert (signed, ML-DSA-87) | 7.6 s | 35.5 s |
| `zxvc check` (every tensor decoded, compared byte for byte) | bit-exact, 1.1 s | bit-exact, 10.6 s |
| `zxvc verify --pub` | hashes OK, signature OK | hashes OK, signature OK |
| `zxvc export` | rebuilt GGUF has the source's SHA-256 | (not a GGUF source) |

No value was refused: every BF16 block of this model fits a 63-bit spread.

## What this shows, and what it does not

* Shows: the package holds the model's weights exactly. Decoding every tensor
  gives back the source bytes, and a GGUF package rebuilds the original file
  byte for byte, so the engine runs it exactly as it runs the GGUF today.
* Shows: BF16 weights cost 15.6% more space as exact integers (a fifth of the
  32-weight blocks need 24-bit integers because a small value shares the
  block with a large one).
* Does not show: the engine running a safetensors package directly. The
  engine loads GGUF; mapping Hugging Face tensor names, config.json and
  tokenizer.json onto it is the next converter step. Until then a
  safetensors package is an exact, signed archive of the weights.
* Does not cover M.2/M.3 (the calibration probe and the evidence head).
