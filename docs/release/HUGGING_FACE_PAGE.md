---
# DRAFT, not posted. This is the README.md (model card) for the ZXV page on
# Hugging Face. Posting it, and choosing the account it goes under, waits for
# the owner's go-ahead. Remove this comment block when posting.
license: apache-2.0
library_name: zxv
pipeline_tag: text-generation
language:
  - en
tags:
  - gguf
  - integer-inference
  - deterministic
  - multi-agent
  - post-quantum
  - operating-system
  - desktop-app
base_model:
  - Qwen/Qwen2.5-1.5B-Instruct
---

<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->

# ZXV: a local multi-agent AI swarm on a post-quantum OS

ZXV (ZEDEC XERO VULPINE) runs a swarm of small language models on your own
computer. It is a desktop app for macOS, Linux and Windows, and the same
kernel also boots on its own from a UEFI disc on arm64 and x86_64 machines.

Source code, issues and the full documentation live on GitHub:
https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0

## What is on this page

| File | What it is |
|---|---|
| `ZXV-<version>-macos.dmg` | the Mac app, Apple Silicon and Intel, macOS 10.15 or later |
| `ZXV-<version>-linux-<arch>.deb` / `.AppImage` / `.tar.gz` | Linux, x86_64 and aarch64 |
| `ZXV-<version>-windows-x86_64.zip` | Windows 10 1803 or later, 64-bit |
| `ZXV-<version>-uefi-<arch>.img.gz` | bare-metal UEFI disc images (opt-in) |
| `SHA256SUMS` | checksums for every file above |
| `models/` | mirrored model weights, each under its own licence (below) |

The app files are the same ones attached to the GitHub release of the same
version, built by the repository's release workflow from one commit. Check
them against `SHA256SUMS` before you run them.

## The engine

- Integer-only tensor engine: no floating point, and the same output bits on
  every CPU it was tested on (x86_64 and arm64).
- Loads GGUF models, with native Q4_K and Q6_K kernels (AVX2, AVX-512, NEON).
- Qwen2.5-1.5B-Instruct (Q4_K_M) gives the same greedy answers as llama.cpp
  on the test prompts, with perplexity 9.371 against llama.cpp's 9.362.
- It is slower than llama.cpp today. On an M3 Max (8 threads) ZXV ran prompt
  processing at 70 tokens/s and generation at 37 tokens/s, against
  llama.cpp's CPU backend at 202 and 91. Batched prompt processing is the
  next speed-up.

## The swarm

Several small models run together under a tokens-per-cycle budget, split
across swarm levels with Fibonacci scaling, and a cooperative market decides
which model gets how many tokens. See `docs/SWARM_ECONOMY.md` on GitHub.

## Model weights

ZXV's code contains no model weights, and neither do the app downloads. On
first run the app offers to download a model and checks it against a
SHA-256 listed in `macos/models.manifest`.

The `models/` folder here mirrors only weights whose licence allows
redistribution and commercial use. Each one keeps its original licence and
model card:

| Model | Licence | Source |
|---|---|---|
| Qwen2.5-1.5B-Instruct, GGUF Q4_K_M | Apache-2.0 | https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct |

The ZXV licence (Apache-2.0) covers ZXV's own code and builds, not these
weights.

## Limits

- Preview software, provided with no warranty.
- The Mac app is not notarised and the Windows app is not code-signed, so
  both show a warning the first time; `docs/MAC_APP.md` says how to open it.
- The UEFI discs are tested under QEMU with real UEFI firmware. Writing one
  to a physical disk erases that disk.

## Licence

Apache-2.0, registered to 36N9 Genetics, LLC (Michael Laurence Curzi).
Hardware designs, when published, use the separate ZXV Hardware License
(`LICENSE-HARDWARE.md`).

## Citation

See `CITATION.cff` in the GitHub repository. A DOI will be listed here once
it is minted.
