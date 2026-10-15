<!-- SPDX-FileCopyrightText: 2026 Michael Laurence Curzi & 36N9 GENETICS LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Tiamat model verification (2026-10-15)

Every candidate on the ZXV Swarm Model Candidate Roster (97 ids, plus replacements) was checked on Hugging Face on 2026-10-15: the licence tag and LICENSE file of the exact checkpoint, the licence of every base model it names, the datasets on its card, and the weight formats it publishes. The admitted models are pinned in [swarm_model_manifest.json](swarm_model_manifest.json) to the commit we checked and the SHA-256 of each file; the creators are credited in [NOTICE](../../NOTICE). The source of truth is `tools/tiamat/roster.py`; `tools/tiamat/check.py` keeps roster, manifest, swarm config and NOTICE consistent.

## Answer: were they all Apache-2.0?

No. 31 could not ship: 15 because of their own or their base model's licence, 13 because the weights are only published as pickles (which can run code when loaded) or another unsafe or unsupported format, and 3 as duplicates or for training data. Each has a replacement below. 79 models are admitted, all Apache-2.0 at the pinned checkpoint, each with a Tiamat dragon name.

Rules applied:

- The shipped checkpoint is Apache-2.0. A Qwen model ships only where that exact checkpoint is Apache-2.0 (Qwen2.5 0.5B, 1.5B, 7B, Coder 0.5B/1.5B, Math 1.5B are; Qwen2.5-3B and Coder-3B are not).
- Base models and data impose no incompatible rider. A fine-tune cannot relicense its base: Draco-CoderMini-3B is tagged Apache-2.0 but is built on Qwen2.5-3B.
- Weights in SafeTensors, GGUF or ONNX only.
- Replacements come from independent developers where one does the job well.

**Your call:** ten admitted Apache-2.0 models are fine-tuned from MIT-licensed bases: Verdictwyrmling (bert-small-nli), Weighwyrmling (nli-deberta-v3-xsmall), Sophismdrake (roberta-large fallacy), Indexserpent and Shelfserpent (all-MiniLM-L6/L12), Bridgeserpent and Twinserpent (multilingual-e5), Choicewyrmling and Choicewyrm (Quyet), and Puzzlewyrm (Yuuki-RxG-nano). MIT only asks for its notice, which we carry, so I admitted them. The roster's hard rule lists MIT as not admitted; if you want that strict reading, these ten move to the excluded list. Indexserpent and Verdictwyrmling are on the disc set, so the strict reading would also need new embedding and NLI cells.

## Disc set (golden disc and Mac app)

The disc can now hold up to 10 GB. It carries only what runs on the integer engine today, so everything on it works; a cell that needs an engine path we have not built yet is added when that path's test passes.

| Cell | Model | Creator | Job | Size |
|---|---|---|---|---|
| Spinewyrm | `Qwen/Qwen2.5-1.5B-Instruct` | Qwen team, Alibaba Cloud | trunk | 1.12 GB |
| Kindlewyrm | `Qwen/Qwen2.5-0.5B-Instruct` | Qwen team, Alibaba Cloud | trunk | 0.49 GB |
| Awllindworm | `Qwen/Qwen2.5-Coder-0.5B-Instruct` | Qwen team, Alibaba Cloud | trunk | 0.49 GB |
| Mendlindworm | `sandeeprdy1729/TIMPS-Coder-0.5B` | Sandeep Reddy (TIMPS) | code_repair | ~35 MB (built) |
| Codelindworm | `Qwen/Qwen2.5-Coder-1.5B-Instruct` | Qwen team, Alibaba Cloud | code_generation | 1.12 GB |
| Wardendrake | `JackKozmo29/codeguard-jev-style-1.5b` | JackKozmo29 | code_review | ~74 MB (built) |
| Reviewdrake | `DharaneswarReddy/codereview-agent` | Dharaneswar Reddy | code_review | ~74 MB (built) |
| Numberwyrm | `Qwen/Qwen2.5-Math-1.5B` | Qwen team, Alibaba Cloud | math_reasoning | 0.99 GB |
| Theoremwyrm | `zbeeb/Qwen2.5-Math-1.5B-GRPO-Staleness-4` | zbeeb | math_reasoning | ~74 MB (built) |
| Puzzlewyrm | `OpceanAI/Yuuki-RxG-nano` | OpceanAI | math_reasoning | ~74 MB (built) |
| Debatelindworm | `iamjayeshc/ArgParser-v4-Qwen1.5B` | Jayesh C (iamjayeshc) | argument_mining | ~74 MB (built) |
| Watchwyvern | `HuggingFaceTB/SmolVLM2-500M-Video-Instruct` | Hugging Face (SmolLM / SmolVLM team) | vision_language | 0.64 GB |
| Listenlindworm | `allenai/OLMoASR` | Allen Institute for AI (Ai2) | speech_recognition | ~76 MB (built) |
| Recordlindworm | `MidlightDDK/pocketsql-0.5b` | MidlightDDK | sql | ~35 MB (built) |
| Statutewyrmling | `sallani/EUAIAct-Qwen2.5-0.5B-Edge` | sallani | regulatory_qa | ~35 MB (built) |
| Vouchwyvern | `HuggingFaceTB/SmolLM2-1.7B-Instruct` | Hugging Face (SmolLM / SmolVLM team) | witness | 1.06 GB |

**Total today: 6.45 GB** (5.90 GB pinned upstream files with known SHA-256, plus about 551 MB of adapters and conversions built on the server, whose hashes are written when they are built).

**Correction (2026-10-15).** The first version of this list put EuroLLM (Parleywyvern), slim-ner (Tagwyrmling) and HamAI-Science (Alembicwyrm) on the disc as "runs now". That was wrong: the engine runs their llama layers, but its tokenizer (`zt_tok`) reads byte-level BPE only, and these three use SentencePiece-style vocabularies. They move to the list below until engine task E5 (a SentencePiece tokenizer in `zt_tok`) passes its test. Vouchwyvern (SmolLM2-1.7B-Instruct, Apache-2.0, byte-level BPE) joins the disc as the witness of another lineage that harness H1 needs; it was run and checked on the engine (SHA-256 of the shipped file matches the Hub's).

Qwen2.5-1.5B and 0.5B stay on the disc, now as trunks: seven fine-tunes from independent developers ride on them and on Qwen2.5-Coder-0.5B and Math-1.5B as adapters of about 35 to 74 MB each instead of 0.5 to 1.1 GB of full weights. An adapter that fails its quality gate ships as full weights in a pack instead, so the disc never exceeds 10 GB.

### Added to the disc when their engine path passes its test

| Cell | Model | Creator | Job | Size | Engine path needed |
|---|---|---|---|---|---|
| Toolhatchling | `FrontiersMind/Nandi-Mini-150M-Tool-Calling` | FrontiersMind | tool_routing | ~307 MB (built) | Nandi arch (custom code) |
| Gistwyrm | `pszemraj/led-base-book-summary` | Peter Szemraj (pszemraj) | summarization | ~324 MB (built) | LED encoder-decoder path |
| Verdictwyrmling | `Pankaj8922/bert-small-nli` | Pankaj8922 | nli_contradiction | ~58 MB (built) | BERT encoder path |
| Fallacydrakeling | `heavyhelium/electra-base-logic-fallacy` | heavyhelium | fallacy_detection | ~219 MB (built) | ELECTRA encoder path |
| Indexserpent | `sentence-transformers/all-MiniLM-L6-v2` | sentence-transformers | embeddings | ~45 MB (built) | BERT encoder path |
| Spotwyvern | `PaddlePaddle/PP-OCRv6_small_det_onnx` | PaddlePaddle team (Baidu) | document_ocr | 0.01 GB | DBNet path |
| Letterwyvern | `PaddlePaddle/PP-OCRv6_small_rec_onnx` | PaddlePaddle team (Baidu) | document_ocr | 0.02 GB | SVTR path |
| Chimedrake | `onnx-community/Kokoro-82M-v1.0-ONNX` | hexgrad | text_to_speech | 0.09 GB | StyleTTS2/iSTFTNet path |
| Tidedrakeling | `amazon/chronos-bolt-tiny` | Amazon Science (Chronos team) | time_series | ~17 MB (built) | T5 encoder path |
| Rustlewyrmling | `mispeech/ced-tiny` | Xiaomi speech team | audio_events | ~11 MB (built) | ViT audio path |
| Parleywyvern | `utter-project/EuroLLM-1.7B-Instruct` | UTTER consortium | ui_dialogue | 1.05 GB | SentencePiece tokenizer (E5) |
| Tagwyrmling | `llmware/slim-ner-tool` | llmware (AI Bloks) | structured_extraction | 0.67 GB | SentencePiece tokenizer (E5) |
| Alembicwyrm | `Haamipromax/HamAI-Science-1b` | Haamipromax | science_qa | 0.67 GB | SentencePiece tokenizer (E5) |

With all of these: **9.92 GB**, inside the 10 GB limit.

## First-run packs and servers

**Laptop pack (16 GB and up), 30 cells:** Greetwyvern (`HuggingFaceTB/SmolLM3-3B`), Toolwyvern (`ajvikram/toolcall-2b`), Briefwyrm (`mithulaartigala/reasoning-summarizer-qwen3.5-0.8b-preview`), Weighwyrmling (`cross-encoder/nli-deberta-v3-xsmall`), Shelfserpent (`sentence-transformers/all-MiniLM-L12-v2`), Bridgeserpent (`dragonkue/multilingual-e5-small-ko`), Twinserpent (`exp-models/dragonkue-KoEn-E5-Tiny`), Sortserpent (`jinaai/jina-reranker-v1-tiny-en`), Swiftserpent (`jinaai/jina-reranker-v1-turbo-en`), Scrollwyvern (`tiiuae/Falcon-OCR`), Sightwyvern (`HuggingFaceTB/SmolVLM-Instruct`), Glimpsewyvern (`Quazim0t0/Byrne-VLM-131M`), Purrdrake (`KittenML/kitten-tts-nano-0.1`), Tidedrake (`amazon/chronos-bolt-mini`), Ledgerdrake (`autogluon/mitra-classifier`), Gaugedrake (`autogluon/mitra-regressor`), Lanterndrake (`AungMoonLord/bert-log-anomaly-detection`), Currentdrake (`Fazmaj/modernbert-flow-anomaly-classifier`), Humdrake (`ManasKumar28/mimii-distilhubert-anomaly`), Shieldwyvern (`ibm-granite/granite-guardian-hap-38m`), Thornwyvern (`lakresi/toxic-comment-moderator`), Sievewyvern (`navodPeiris/minilm-toxic-spam-classifier`), Murmurwyrmling (`mispeech/ced-mini`), Thunderwyrmling (`mispeech/ced-small`), Sketchwyvern (`AuroraAI-Research/AuroraIMG-6M`), Starwyrm (`eaddario/MiniCPM5-1B-GGUF`), Choicewyrmling (`chinhnc/Quyet-1.0-Tiny`), Balancewyrm (`samratduttaofficial/WaterSheep`), Fablehatchling (`BananaMind/StoryBananaMind-7.5M`), Fablewyvern (`VertexAGI/prism-creative-1-mini`).

**Workstation pack (32 GB and up), 5 cells:** Hostwyvern (`arcee-ai/AFM-4.5B`), Heraldwyvern (`Qwen/Qwen2.5-7B-Instruct`), Rankserpent (`BAAI/bge-reranker-v2-m3`), Dictatelindworm (`ibm-granite/granite-4.0-1b-speech`), Choicewyrm (`chinhnc/Quyet-1.0-Small`).

**Server tiers, 16 cells:** Bastiondrake (`vitorallo/securereview-coder-32B-FP8`), Sophismdrake (`MidhunKanadan/roberta-large-fallacy-classification`), Hearinglindworm (`ibm-granite/granite-speech-3.3-2b`), Minutelindworm (`ibm-granite/granite-speech-4.1-2b-plus`), Choirdrake (`parler-tts/parler-tts-mini-multilingual`), Polyglotwyvern (`google/madlad400-3b-mt`), Tilewyvern (`Arrexel/pattern-diffusion`), Muralwyvern (`alfredplpl/CommonArt-PoC`), Gripdrake (`lerobot/smolvla_base`), Reachdrake (`lerobot/smolvla_libero`), Pairdrake (`lerobot/smolvla_robotwin`), Armdrakeling (`ThiennNguyen/act_so101_small`), Senatewyrm (`autotrust/JEV-27B-VL`), Compliancewyrm (`AlexandruGirlea/precommiteu-models`), Dutywyrm (`PrinceRansom7/gemma4-e2b-it-regulatory-obligation-v1`), Covenantwyrm (`zenlm/zen-legal`).

Profiles (laptop core 8 to 16 GB, laptop plus, workstation, server) and the mapping of the 12 device tiers are in `tools/tiamat/tiamat_swarm.json`. The laptop core profile loads only the disc set and keeps about 4 GB of models resident.

## Replacements

| Failed | Why | Replaced by |
|---|---|---|
| `prithivMLmods/Draco-CoderMini-3B` | Card says Apache-2.0, but its base Qwen2.5-3B-Instruct is under the Qwen Research licence (no commercial use). A fine-tune cannot relicense its base. | Qwen2.5-Coder-1.5B-Instruct and TIMPS-Coder-0.5B |
| `Qwen/Qwen2.5-Coder-3B-Instruct` | Qwen Research licence (license: other / qwen-research). | Qwen2.5-Coder-1.5B-Instruct |
| `llmware/slim-extract-tool` | CC-BY-SA-4.0. | slim-ner-tool |
| `llmware/slim-summary-tool` | CC-BY-SA-4.0. | pszemraj/led-base-book-summary |
| `Dongwei/Qwen2.5-1.5B-Open-R1-GRPO_Math` | No licence declared; weights partly pickled (.bin). | zbeeb/Qwen2.5-Math-1.5B-GRPO-Staleness-4 |
| `EliaMengozzi/fallama` | Llama 3.1 Community licence. | heavyhelium/electra-base-logic-fallacy |
| `eulogik/TinyDoc-VLM-256M` | No licence declared on the card. | SmolVLM2-500M-Video-Instruct |
| `docling-project/SmolDocling-256M-preview` | CDLA-Permissive-2.0, not Apache-2.0 (permissive; admit only if you widen the rule). | PP-OCRv6 small (ONNX) |
| `nums-ai/causilo` | Custom causilo-1.0 licence. | autogluon/mitra-classifier |
| `melihemin/qwen2.5-0.5b-text2sql-full` | No licence declared. | MidlightDDK/pocketsql-0.5b |
| `saadxsalman/SS-350M-SQL-Strict-GGUF` | Base LiquidAI/LFM2.5-350M is under the LFM Open License 1.0. | MidlightDDK/pocketsql-0.5b |
| `openjev/openjev` | CC-BY-NC-4.0 (non-commercial). | the three moderation cells |
| `yasserrmd/MegaSciMoE-1.2B` | Base LiquidAI/LFM2-1.2B is under the LFM Open License 1.0. | Haamipromax/HamAI-Science-1b |
| `Arigadam/TinyStories-with-EOS` | Base Gemma 2 2B carries the Gemma terms. | BananaMind/StoryBananaMind-7.5M |
| `DavidAU/L3.2-Rogue-Creative-Instruct-Uncensored-7B-GGUF` | Tagged Apache-2.0, but it is a Llama 3.2 merge (its own tags say llama-3.2), so the Llama licence applies. | VertexAGI/prism-creative-1-mini |
| `Cactus-Compute/needle2` | Weights published only as a pickle (checkpoints/needle2.pkl). | FrontiersMind/Nandi-Mini-150M-Tool-Calling |
| `llmware/slim-sql-1b-v0` | Weights only as pickled pytorch_model.bin. | MidlightDDK/pocketsql-0.5b |
| `eulogik/nanoforecast-500k` | Weights only as a pickle (model.pt). | amazon/chronos-bolt-tiny |
| `zerooneresearch/predictlm-mini-13m` | Weights only as a pickle (.pt). | amazon/chronos-bolt-mini |
| `hexgrad/Kokoro-82M` | Weights only as a pickle (kokoro-v1_0.pth). Licence is fine. | onnx-community/Kokoro-82M-v1.0-ONNX (same weights) |
| `stetteh/otics-anomaly` | Weights only as pickles (.pkl/.pth). | AungMoonLord/bert-log-anomaly-detection |
| `kkonathala/powerplant-anomaly-transformer` | No weight files published (code only). | ManasKumar28/mimii-distilhubert-anomaly |
| `nvidia/Kumo-Anomaly` | Weights only as a pickle (.pth). | Fazmaj/modernbert-flow-anomaly-classifier |
| `ash12321/deep-svdd-anomaly-detection` | Weights only as pickles (.pkl/.pth). | the three anomaly cells |
| `jibala1022/easyrec-base` | Weights only as pickles (.bin/.pt/.pth). | all-MiniLM-L6-v2 with the Jina rerankers |
| `DamonDemon/SpanUQ` | Weights only as a pickle (.pt). | Tiamat's evidence head (zt_evh) |
| `PaddlePaddle/PP-OCRv6_tiny_rec` | Paddle inference format (.pdiparams). | PaddlePaddle/PP-OCRv6_small_*_onnx |
| `litert-community/SmolVLM2-500M` | LiteRT .litertlm container only. | HuggingFaceTB/SmolVLM2-500M-Video-Instruct |
| `onnx-community/kitten-tts-nano-0.1-ONNX` | Same model as KittenML/kitten-tts-nano-0.1, which publishes ONNX itself. | KittenML/kitten-tts-nano-0.1 |
| `mudler/ced-gguf` | A GGUF packaging of the CED models, which we convert from the originals; Ettore Di Giacinto is credited for ced.cpp. | mispeech/ced-* |
| `segmind/Segmind-Vega` | Licence is Apache-2.0, but its card lists a Midjourney-output dataset among its training data, whose terms restrict use. | AuroraIMG-6M / pattern-diffusion |

Also checked as replacements and declined: `tjarvis91/Q-ToolCall-50M-Sovereign` (weights only as a pickle (.pt)); `eoinedge/AnomalyNet` (weights only as .pt/.pte/.tflite); `Jvachier/dcase2020-task2-anomaly-detection` (keras/NumPy archive only); `kha-white/manga-ocr-base` (weights only as pickled .bin); `Falconsai/text_summarization` (fine, but led-base-book-summary handles long documents, which swarm memory needs).

## Training data notes

Model weights are Apache-2.0, but some training data carries its own terms. None of these stops shipping the weights; they are listed so nobody is surprised.

- **nli:** SNLI is CC-BY-SA-4.0 and MultiNLI mixes CC-BY-3.0, CC-BY-SA-3.0 and other terms. The arbiter's T/F/B/N training uses Tiamat's own derived statements (tools/tiamat/server/make_tfbn_data.py), so its evidence head does not depend on them.
- **jigsaw:** Jigsaw toxic-comment data came from a Kaggle competition with its own terms.
- **audioset:** AudioSet labels are CC-BY-4.0; the audio is YouTube clips.
- **mimii:** The MIMII machine-sound set is CC-BY-SA-4.0 (share-alike for the data, not for model outputs).
- **logic:** The LOGIC fallacy set (Jin et al. 2022) is published with its paper's code.
- **booksum:** BookSum (Salesforce) is BSD-3-Clause.
- **tinystories:** TinyStories is CDLA-Sharing-1.0 (share-alike for the data, not for model outputs).
- **cc12m:** CC12M is a list of web image URLs, each under its own terms.
- **pocketsql:** pocketsql-data is CC-BY-SA-4.0.
- **watersheep:** WaterSheep's card lists about 80 training sets, some non-commercial.

## To confirm before release

- `llmware/slim-ner-tool` names no base model (its config is a 1.1B llama, TinyLlama-shaped). Ask llmware.
- `navodPeiris/minilm-toxic-spam-classifier`: its base `nreimers/MiniLMv2` declares no licence on its card.
- `AungMoonLord/bert-log-anomaly-detection` and `Fazmaj/modernbert-flow-anomaly-classifier` do not name their training data.
- `vitorallo/securereview-coder-32B-FP8` is gated: you accept its terms on Hugging Face before the server can download it.
- Trademark checks on "Tiamat" and on every dragon name before release; none is a fictional or product dragon, and none repeats a language twin's name.

## Measuring cross-wiring

When a configuration is set, cells in one tissue share the trunk's weights, the tokenizer pass, the layers below the split and their KV cache; every organ shares retrieval embeddings and finished jobs (Venn rule). `tools/tiamat/server/bench_crosswire.py` compares that with running the same cells separately, on the same prompts and hardware, and reports weight bytes, multiply-adds, seconds with and without model swapping, peak GPU memory, and each cell's agreement and four-valued accuracy. A tissue counts as cross-wired only if every cell keeps top-1 agreement of at least 0.97 with running separately and its accuracy within 1 point; otherwise the report says it lost and the tissue runs separately. These rules are fixed before the first run.

