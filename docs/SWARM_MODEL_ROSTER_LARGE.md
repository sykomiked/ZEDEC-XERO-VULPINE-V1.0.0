<!-- SPDX-FileCopyrightText: 2026 Michael Laurence Curzi & 36N9 GENETICS LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Large Tiamat models: peers for the Sovereign Anchor and Swarm Agora

These are the large Apache-2.0 models that a node with a big GPU (up to about
48 GB of VRAM, an A6000-class card) can offer to other cells through zxvd.
They are building blocks next to the small cells, not instead of them. None
of them is on the disc or in the first root's weights. Where they sit in the
network is `docs/spec/GOSSIP_TOPOLOGY.md` (G5, the three tiers).

The small roster and its checks are `docs/release/SWARM_MODEL_VERIFICATION.md`
and `docs/release/swarm_model_manifest.json`. This page's machine-readable
twin is `docs/release/swarm_model_large_manifest.json`, written by
`tools/tiamat/large_roster.py --fetch`.

## How each row was checked (2026-10-15)

- The model's Hub card says `license: apache-2.0`; the script refuses any row
  whose card says otherwise.
- Each base model named on the card was read too, and must be Apache-2.0 on
  its own card: the owner chose Apache only (2026-10-15), so the script
  refuses a row whose lineage leaves Apache-2.0.
- Weights are SafeTensors or ONNX, pinned to the Hub commit with the SHA-256
  and size of every file. Pickle-only weights are refused, as in the small
  roster.
- Every model credits its developer and links its source.
- `zero_float_verified` is false for every row. A row marked "integer engine"
  has an architecture the engine runs today (qwen2 or qwen3 with a byte-level
  BPE tokenizer), but it has not passed parity tests at these sizes yet.
  Every other row is runtime-gated: it runs under its own runtime on a server
  until the engine gains its path.

## The verified models (31)

| Model | Developer | Params | Source size | Runs on | Used for |
|---|---|---|---|---|---|
| `Qwen/Qwen3-32B` | Qwen team, Alibaba Cloud | 32B | 65.5 GB | integer engine (qwen3) | ui dialogue, tool routing, structured extraction, decision calibration, legal extraction, anomaly reasoning, scientific, creative writing, translation, tabular, code review |
| `Qwen/Qwen2.5-32B-Instruct` | Qwen team, Alibaba Cloud | 32B | 65.5 GB | integer engine (qwen2) | ui dialogue, structured extraction |
| `Qwen/Qwen3-14B` | Qwen team, Alibaba Cloud | 14B | 29.5 GB | integer engine (qwen3) | tool routing, structured extraction, reranking |
| `Qwen/Qwen2.5-14B-Instruct` | Qwen team, Alibaba Cloud | 14B | 29.5 GB | integer engine (qwen2) | tool routing, structured extraction, translation, tabular, anomaly reasoning, moderation, scientific, legal extraction, nli triples, reranking |
| `Qwen/Qwen2.5-7B-Instruct` | Qwen team, Alibaba Cloud | 7B | 15.2 GB | integer engine (qwen2) | audio captioning, tts planning |
| `mistralai/Mistral-Small-3.1-24B-Instruct-2503` | Mistral AI | 24B | 48.0 GB | server runtime (Mistral3, vision) | ui dialogue, vision |
| `mistralai/Mixtral-8x7B-Instruct-v0.1` | Mistral AI | 47B (MoE, 13B active) | 93.4 GB | server runtime (Mixtral MoE) | ui dialogue, tool routing |
| `NousResearch/Nous-Hermes-2-Mixtral-8x7B-DPO` | Nous Research | 47B (MoE, 13B active) | 93.4 GB | server runtime (Mixtral MoE) | ui dialogue, tool routing, creative writing |
| `allenai/OLMo-2-1124-13B-Instruct` | Allen Institute for AI (AI2) | 13B | 27.4 GB | server runtime (OLMo 2) | code generation, math, structured extraction, tabular, anomaly reasoning, scientific, legal extraction, decision calibration |
| `mistralai/Mistral-7B-Instruct-v0.3` | Mistral AI | 7B | 14.5 GB | needs SentencePiece tokenizer (engine task E5) | code generation, code review, tool routing, math, nli scoring, structured extraction, tabular, legal extraction, decision calibration |
| `Qwen/Qwen2.5-Coder-32B-Instruct` | Qwen team, Alibaba Cloud | 32B | 65.5 GB | integer engine (qwen2) | code generation, code review, sql |
| `Qwen/Qwen2.5-Coder-14B-Instruct` | Qwen team, Alibaba Cloud | 14B | 29.5 GB | integer engine (qwen2) | code generation, sql |
| `ibm-granite/granite-20b-code-instruct-8k` | IBM Granite team | 20B | 40.1 GB | server runtime (GPTBigCode) | code review |
| `Qwen/QwQ-32B` | Qwen team, Alibaba Cloud | 32B | 65.5 GB | integer engine (qwen2) | math, decision calibration |
| `Qwen/Qwen2.5-Math-7B-Instruct` | Qwen team, Alibaba Cloud | 7B | 15.2 GB | integer engine (qwen2) | math |
| `tasksource/ModernBERT-base-nli` | tasksource (Damien Sileo) | 150M | 0.6 GB | needs ModernBERT encoder path | nli |
| `tasksource/ModernBERT-large-nli` | tasksource (Damien Sileo) | 396M | 1.6 GB | needs ModernBERT encoder path | nli |
| `Alibaba-NLP/gte-modernbert-base` | Alibaba (Tongyi Lab, NLP) | 149M | 0.9 GB | needs ModernBERT encoder path | embeddings, recommendation |
| `nomic-ai/nomic-embed-text-v1.5` | Nomic AI | 137M | 1.1 GB | needs NomicBERT path (custom code) | embeddings, recommendation |
| `Alibaba-NLP/gte-large-en-v1.5` | Alibaba (Tongyi Lab, NLP) | 434M | 3.5 GB | needs custom 'NewModel' path | embeddings, recommendation |
| `Alibaba-NLP/gte-reranker-modernbert-base` | Alibaba (Tongyi Lab, NLP) | 149M | 1.2 GB | needs ModernBERT encoder path | reranking |
| `cross-encoder/ms-marco-electra-base` | Sentence Transformers (cross-encoder) | 110M | 0.9 GB | needs ELECTRA encoder path | reranking |
| `Qwen/Qwen2.5-VL-32B-Instruct` | Qwen team, Alibaba Cloud | 33B | 68.3 GB | server runtime (Qwen2.5-VL) | vision, document ocr |
| `Qwen/Qwen2.5-VL-7B-Instruct` | Qwen team, Alibaba Cloud | 8B | 16.6 GB | server runtime (Qwen2.5-VL) | vision, document ocr, vla planning |
| `parler-tts/parler-tts-large-v1` | Hugging Face (Parler-TTS team) | 2.3B | 9.3 GB | server runtime (Parler-TTS) | tts |
| `google/madlad400-3b-mt` | Google | 3B | 11.8 GB | needs T5 path | translation |
| `ibm-granite/granite-timeseries-ttm-v1` | IBM Granite team | 1M | 0.0 GB | needs TinyTimeMixer path | time series |
| `amazon/chronos-t5-large` | Amazon | 710M | 2.8 GB | needs T5 path | time series |
| `ibm-granite/granite-guardian-3.1-8b` (advisory only) | IBM Granite team | 8B | 16.3 GB | server runtime (Granite) | moderation |
| `kandinsky-community/kandinsky-2-1` | Kandinsky team (Sber AI) | 3.3B | 7.4 GB | server runtime (Kandinsky 2.1) | image generation |
| `enfuse/edgevla-tiny-fmb` (planning only) | enfuse | 164M | 0.3 GB | server runtime (SmolVLA/EdgeVLA) | vla planning |

Source sizes are the upstream weights (BF16 or F32). A Q4_K_M build of a 32B
model is about 20 GB; those builds are made on the server from the pinned
sources and get their own hashes there.

## What changed from the list

- **Qwen2.5-72B-Instruct** is under the Qwen licence, not Apache-2.0. Its
  slots (dialogue, tool routing, extraction, science, legal, creative,
  calibration, anomaly reasoning, tabular, translation) go to
  **Qwen3-32B**, with **Qwen2.5-32B-Instruct** as a second large dialogue peer.
- **Qwen2.5-Math-72B-Instruct** is under the Qwen licence; **QwQ-32B** (on the
  Apache-2.0 Qwen2.5-32B base) takes the maths slot. **Qwen2.5-Math-14B** does
  not exist; **Qwen2.5-Math-7B-Instruct** takes its place.
- **Qwen2.5-VL-72B** (Qwen licence) and **Qwen2.5-VL-3B** (Qwen Research
  licence, no commercial use) are out; **Qwen2.5-VL-14B** does not exist.
  **Qwen2.5-VL-32B** and **Qwen2.5-VL-7B** (both Apache-2.0) take the vision,
  document and robot-planning slots.
- **Mistral-Small-3.1-24B-Instruct-2501** does not exist; the 3.1 release is
  **-2503**, Apache-2.0 with its vision encoder.
- **Nous-Hermes-2-Mixtral-8x7B-DPO** is Apache-2.0 on an Apache-2.0 Mixtral
  base; its base, **Mixtral-8x7B-Instruct**, is listed too so the trunk is
  pinned.
- **Apache only (owner, 2026-10-15).** Five rows were Apache-2.0 checkpoints
  on MIT bases and were replaced by models whose whole lineage is Apache-2.0
  (all on the Apache-2.0 ModernBERT base from Answer.AI):
  - **nli-deberta-v3-base** and **-large** (DeBERTa-v3, MIT) by
    **tasksource/ModernBERT-base-nli** and **-large-nli** (three labels:
    entailment, neutral, contradiction);
  - **snowflake-arctic-embed-l** (from e5-large-unsupervised, MIT) and
    **all-mpnet-base-v2** (MPNet, MIT code) by **gte-modernbert-base**;
  - **ms-marco-MiniLM-L-12-v2** (MiniLM, MIT) by
    **gte-reranker-modernbert-base**.
- **Granite 20B code**, **Granite Guardian 3.1 8B**, the ELECTRA reranker and
  the other embedding models are Apache-2.0 on their cards, with Apache-2.0
  bases.
- Out for format or missing weights: **unispeech-sat-base-plus** (no licence on
  its card, pickle weights), **VoxCPM2** (Apache-2.0, but its audio VAE is only
  a pickle), **prose-humanizer-7b** (no weights published), and, as in the
  small roster, **Kokoro-82M**, **nanoforecast-500k**,
  **powerplant-anomaly-transformer** and **ced-gguf**.
- No large Apache-2.0 speech-recognition model and no large Apache-2.0
  robot-action model was found; those slots stay empty rather than filled by
  a stand-in.
- Six rows in the list are small models already in the small roster
  (EuroLLM, the toxic-comment moderator, pattern-diffusion, AuroraIMG, HamAI,
  all-MiniLM-L6-v2); they are not repeated here. Three more are small and
  Apache-2.0 but not in the small roster (PP-OCRv6 tiny ONNX, the T5 SQL
  model, smolgpt-fables); they go to the small roster's review, not here.

## Limits that apply to every use

- **Granite Guardian is advisory only.** It flags risk for a person to judge;
  it never blocks or acts by itself.
- **EdgeVLA and the vision models in the robotics slot are planning only**:
  logistics and mapping, human-approved, driving no actuator.
- **Results from a peer outside the user's own devices are witnessed** before
  they are shown (`GOSSIP_TOPOLOGY.md` G5).
- The NLI models feed `kernel/src/lpres`: entailment TRUE, contradiction FALSE,
  neutral NEITHER, strong conflicting signals BOTH.

## For the owner to decide

- **Training data.** The ELECTRA reranker learned from MS MARCO, and the two
  ModernBERT NLI models partly from ANLI (CC BY-NC 4.0); both datasets carry
  non-commercial terms. The Hermes fine-tune learned largely from
  GPT-4 output. The weights' licences allow commercial use; the data's terms
  are recorded for the owner.
