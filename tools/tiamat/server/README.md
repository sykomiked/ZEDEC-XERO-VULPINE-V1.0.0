<!-- SPDX-FileCopyrightText: 2026 Michael Laurence Curzi & 36N9 GENETICS LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Tiamat on the A6000 server

Tasks for the bare-metal test server: integrate, tune and package the
Tiamat cells listed in `docs/release/swarm_model_manifest.json`. The cell
layout, profiles and cross-wiring rules are in `tools/tiamat/tiamat_swarm.json`.

Ground rules:

- Weights, adapters and hidden-state dumps live under `/srv/tiamat` (or any
  path outside the checkout). Nothing here writes weights into git; every
  script refuses an output path inside the repository.
- Every download is checked against the manifest's SHA-256 at its pinned
  revision. A mismatch stops the task.
- Each result is a JSON report. Copy reports, not weights, back to
  `/mnt/project-files/zxv-server/tiamat/` for review.
- Quality gates are fixed before running (below). A cell that misses its
  gate is reported, not tuned until it passes on the test split.

Python: `torch`, `transformers`, `safetensors`, `peft` (S6 only), and a
llama.cpp checkout at `$LLAMA_CPP` for GGUF conversion.

## Tasks, in order

| # | Task | Command | Done when |
|---|------|---------|-----------|
| S1 | Fetch the disc set and its sources | `fetch_pinned.py --dest /srv/tiamat/models --placement disc disc_gated --source` | every file says ok |
| S2 | Run each "runs now" disc model on the engine | `make -C kernel` real-model test against each GGUF (bench/tensor/real_model_test.py pattern) | each loads and answers; quant types it refuses are listed |
| S3 | Turn same-base fine-tunes into adapters | `extract_adapters.py --models /srv/tiamat/models --out /srv/tiamat/adapters --tissue <base>` for each of the 4 tissues | report per cell: ADAPTER (split, rank, agreement, KL) or FULL |
| S4 | Make the four-valued data | `make_tfbn_data.py --n 40000 --out /srv/tiamat/tfbn.jsonl` | file written |
| S5 | Evidence-head probe per cell | `train_tfbn.py probe --model .../src --data tfbn.jsonl --out /srv/tiamat/evh/<cell>` | report.json per cell |
| S6 | Native four-valued adapters | `train_tfbn.py adapter --model <trunk>/src --split-layer <tissue split> --data tfbn.jsonl --out /srv/tiamat/tfbn/<cell>` | accuracy and false-certainty rate beat S5's |
| S7 | Engine state fork (cross-wiring) | engine task E2 below | test passes |
| S8 | Measure cross-wiring | `bench_crosswire.py --tissue <base> --data tfbn.jsonl --n 400 --out bench_<base>.json` | report written, losses included |
| S9 | Assemble the disc set | `ZXV_ENGINE_PATHS="..." build_disc_models.sh /srv/tiamat` | golden.models under 10 GB, every line hashed |

S3's tissues: `Qwen/Qwen2.5-1.5B-Instruct`, `Qwen/Qwen2.5-Math-1.5B`,
`Qwen/Qwen2.5-0.5B-Instruct`, `Qwen/Qwen2.5-Coder-0.5B-Instruct`.

## Gates, fixed in advance

- **Adapter (S3):** top-1 next-token agreement with the full fine-tune at
  least 0.97 and mean KL at most 0.05 nats per token on held-out prompts.
  Bias and norm deltas are not carried by a LoRA; the gate measures their loss.
- **Evidence head (S5, S6):** reported on the test split as the 4 x 4
  confusion. The number that matters most is the false-certainty rate:
  how often the head says TRUE or FALSE when the sources conflict (BOTH)
  or are silent (NEITHER). S6 must lower it against S5 without lowering
  overall accuracy.
- **Cross-wiring (S8):** a tissue counts as cross-wired only if every
  cell keeps top-1 agreement of at least 0.97 with running separately and
  its four-valued accuracy within 1 point. Savings are reported in weight
  bytes, multiply-adds, seconds with and without model swapping, and peak
  GPU memory. If cross-wiring loses, the report says so and the tissue runs
  separately.

## Engine tasks (kernel, through the usual PR batches)

- **E1** A host tool that runs a GGUF on prompts through `zt_model`, dumps
  the Q16 hidden state per prompt, runs `zt_evh_fit`, and writes the head
  (int16 weights, shifts, Q16 biases). The shipping head must be fitted on
  the engine's own hidden states, not the float model's.
- **E2** State fork: run layers below a split once, then continue several
  `zt_model_state_t` from that point, each with its own adapter. The KV
  cache below the split is shared read-only. Today a LoRA change needs an
  empty state (zt_model.h, T23 LIMITS), so this is new.
- **E3** Encoder paths for the disc_gated cells, one at a time, each with its
  own test: BERT-family encoder (Verdictwyrmling, Indexserpent,
  Fallacydrakeling), T5 encoder (Tidedrakeling), LED (Gistwyrm),
  StyleTTS2 (Chimedrake), ViT audio (Rustlewyrmling), PP-OCR DBNet and SVTR
  (Spotwyvern, Letterwyvern), Nandi (Toolhatchling). A cell goes on the
  disc only when its path's test passes (S9 enforces it through
  `ZXV_ENGINE_PATHS`).
- **E4** `mk_golden_disc.sh` accepts only `.gguf` names; ONNX cells need
  either a GGUF conversion or that rule widened.

## Notes

- OLMoASR tiny.en lives in a subfolder of `allenai/OLMoASR`; check that
  `bench/tensor/convert_whisper.py` reads its layout (config and tokenizer
  files) before S9.
- `vitorallo/securereview-coder-32B-FP8` is gated: the owner accepts its
  terms on Hugging Face and gives the server an `HF_TOKEN`.
- Robotics cells (hand organ) stay limited to logistics and mapping, with a
  human approving every motion (project safety rules).
