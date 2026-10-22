# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Tiamat model roster: the licence decision and the cell for every candidate.

Each entry was checked on 2026-10-15 against its Hugging Face repository
(licence tag and LICENSE file), the licence of every base model it names,
the datasets on its card and the weight formats it publishes.
build_manifest.py pins the admitted ones to a commit and their SHA-256.

Rules (the user's roster, 2026-10-15, and their answers the same day):
  - the shipped checkpoint is Apache-2.0, checked on that exact checkpoint
    (Qwen models only where that checkpoint is Apache-2.0: Qwen2.5-3B is not);
  - base models and data impose no incompatible rider (an MIT base is
    compatible: it only asks for its notice, which we carry; listed in
    the verification report so the user can tighten it);
  - weights published as SafeTensors, GGUF or ONNX (no pickle);
  - a failed model is replaced by an Apache-2.0 model, from an independent
    developer where one does the job well.

Every cell has a dragon name: one word, a plain purpose plus a dragon kind,
never a fictional or product dragon and never one of the language twins'
names (kernel/src/lingua/zl_langs.def). Each name needs a trademark check
before release. The upstream id and creator stay beside the name in the
manifest and NOTICE.

placement:
  disc             golden disc and Mac app; runs on the integer engine today
  disc_gated       goes on the disc only when its engine path passes its test
                   (build_disc_models.sh refuses it until then)
  pack_laptop      first-run pack for 16 GB laptops and up
  pack_workstation first-run pack for 32 GB and up
  server           server tiers only

trunk: cells fine-tuned from the same base ship as an adapter on that base
(tools/tiamat/server/extract_adapters.py) so they share its weights, its
tokenizer and, for K/V-frozen adapters, its KV cache. A cell whose adapter
fails the quality gate ships as full weights in a pack instead.
"""

T15 = "Qwen/Qwen2.5-1.5B-Instruct"
T05 = "Qwen/Qwen2.5-0.5B-Instruct"
TC05 = "Qwen/Qwen2.5-Coder-0.5B-Instruct"
TM15 = "Qwen/Qwen2.5-Math-1.5B"


def cell(id, name, developer, cap, organ, params, place, ship, engine, files,
         para=False, langs=("en",), trunk=None, note=None):
    return dict(id=id, name=name, developer=developer, cap=cap, organ=organ,
                params=params, place=place, ship=ship, engine=engine,
                files=list(files), para=para, langs=list(langs), trunk=trunk,
                note=note)


QWEN = "Qwen team, Alibaba Cloud"
IBM = "IBM Granite team"
HFTB = "Hugging Face (SmolLM / SmolVLM team)"

ADMITTED = [
    # --- trunks: shared bases the adapters ride on ------------------------
    cell(T15, "Spinewyrm", QWEN, "trunk", "trunk", "1.5B", "disc", "GGUF-Q4_K_M",
         "runs now (qwen2)", ["*.safetensors"], langs=["29+ languages"],
         note="Already on the golden disc. Trunk for the 1.5B adapters."),
    cell(T05, "Kindlewyrm", QWEN, "trunk", "trunk", "0.5B", "disc", "GGUF-Q4_K_M",
         "runs now (qwen2)", ["*.safetensors"], langs=["29+ languages"],
         note="Already on the golden disc. Trunk for the 0.5B adapters."),
    cell(TC05, "Awllindworm", QWEN, "trunk", "trunk", "0.5B", "disc", "GGUF-Q4_K_M",
         "runs now (qwen2)", ["*.safetensors"], langs=["en", "code"],
         note="Trunk for TIMPS-Coder and pocketsql."),
    # --- dialogue ---------------------------------------------------------
    cell("utter-project/EuroLLM-1.7B-Instruct", "Parleywyvern",
         "UTTER consortium (Unbabel, Instituto Superior Tecnico, University of Edinburgh, Aveni, Sorbonne Universite, Naver Labs Europe, University of Amsterdam), funded by EU Horizon Europe",
         "ui_dialogue", "voice_of_tiamat", "1.7B", "disc_gated", "GGUF-Q4_K_M", "needs SentencePiece tokenizer (engine task E5)",
         ["model.safetensors"], langs=["35 languages"],
         note="Llama shape, but its tokenizer is SentencePiece-style (no byte-level pre-split), which zt_tok cannot read yet; joins the disc when E5 lands."),
    cell("HuggingFaceTB/SmolLM3-3B", "Greetwyvern", HFTB, "ui_dialogue", "voice_of_tiamat", "3B",
         "pack_laptop", "GGUF-Q4_K_M", "needs smollm3 arch", ["*.safetensors"],
         langs=["en", "fr", "es", "it", "pt", "zh", "ar", "ru"]),
    cell("arcee-ai/AFM-4.5B", "Hostwyvern", "Arcee AI", "ui_dialogue", "voice_of_tiamat", "4.5B",
         "pack_workstation", "GGUF-Q4_K_M", "needs arcee arch", ["*.safetensors"]),
    cell("Qwen/Qwen2.5-7B-Instruct", "Heraldwyvern", QWEN, "ui_dialogue", "voice_of_tiamat", "7B",
         "pack_workstation", "GGUF-Q4_K_M", "runs now (qwen2)", ["*.safetensors"], langs=["29+ languages"]),
    # --- code -------------------------------------------------------------
    cell("sandeeprdy1729/TIMPS-Coder-0.5B", "Mendlindworm", "Sandeep Reddy (TIMPS)", "code_repair", "smith",
         "0.5B", "disc", "adapter on Qwen2.5-Coder-0.5B-Instruct", "runs now (qwen2 + zt_lora)",
         ["model.safetensors"], langs=["en", "code"], trunk=TC05),
    cell("Qwen/Qwen2.5-Coder-1.5B-Instruct", "Codelindworm", QWEN, "code_generation", "smith", "1.5B",
         "disc", "GGUF-Q4_K_M", "runs now (qwen2)", ["*.safetensors"], langs=["en", "code"]),
    # --- code review / security ---------------------------------------------
    cell("JackKozmo29/codeguard-jev-style-1.5b", "Wardendrake", "JackKozmo29", "code_review", "warden",
         "1.5B", "disc", "adapter on Qwen2.5-1.5B-Instruct", "runs now (qwen2 + zt_lora)",
         ["*.safetensors"], para=True, langs=["en", "code"], trunk=T15),
    cell("DharaneswarReddy/codereview-agent", "Reviewdrake", "Dharaneswar Reddy", "code_review", "warden",
         "1.5B", "disc", "adapter on Qwen2.5-1.5B-Instruct", "runs now (qwen2 + zt_lora)",
         ["*.safetensors"], para=True, langs=["en", "code"], trunk=T15,
         note="Base is unsloth/qwen2.5-1.5b-instruct-unsloth-bnb-4bit, a 4-bit copy of Qwen2.5-1.5B-Instruct (Apache-2.0); credit Unsloth too."),
    cell("vitorallo/securereview-coder-32B-FP8", "Bastiondrake", "vitorallo (Foil)", "security_review", "warden",
         "32B", "server", "FP8 safetensors", "server runtime", ["*.safetensors"], para=True, langs=["en", "code"],
         note="Gated: the owner accepts the access terms on Hugging Face before download. Base Qwen2.5-Coder-32B-Instruct is Apache-2.0. Ships its own NOTICE."),
    # --- tool routing ---------------------------------------------------------
    cell("FrontiersMind/Nandi-Mini-150M-Tool-Calling", "Toolhatchling", "FrontiersMind", "tool_routing", "herald",
         "150M", "disc_gated", "F16 safetensors", "needs Nandi arch (custom code)", ["model.safetensors"],
         langs=["en", "10 Indic languages"],
         note="Replaces Cactus-Compute/needle2 (pickle only). Pretrained from scratch on 525B tokens; base FrontiersMind/Nandi-Mini-150M is Apache-2.0."),
    cell("ajvikram/toolcall-2b", "Toolwyvern", "ajvikram", "tool_routing", "herald", "2B", "pack_laptop",
         "GGUF-Q4_K_M", "needs qwen3.5 arch", ["*.safetensors"], note="Base Qwen/Qwen3.5-2B is Apache-2.0."),
    # --- extraction / summaries -------------------------------------------------
    cell("llmware/slim-ner-tool", "Tagwyrmling", "llmware (AI Bloks)", "structured_extraction", "scribe", "1.1B",
         "disc_gated", "GGUF (upstream)", "needs SentencePiece tokenizer (engine task E5); quant type checked on load", ["slim-ner.gguf"],
         note="SentencePiece tokenizer (TinyLlama), not readable by zt_tok until E5. Card names no base model; the config is a 1.1B llama (TinyLlama shape, Apache-2.0). Confirm with llmware before release."),
    cell("pszemraj/led-base-book-summary", "Gistwyrm", "Peter Szemraj (pszemraj)", "summarization", "scribe",
         "162M", "disc_gated", "F16 safetensors", "needs LED encoder-decoder path", ["model.safetensors"],
         note="Replaces llmware/slim-summary-tool (CC-BY-SA-4.0). Base allenai/led-base-16384 is Apache-2.0; data kmfoda/booksum is BSD-3-Clause."),
    cell("mithulaartigala/reasoning-summarizer-qwen3.5-0.8b-preview", "Briefwyrm", "Mithula Artigala",
         "summarization", "scribe", "0.8B", "pack_laptop", "GGUF-Q4_K_M", "needs qwen3.5 arch",
         ["*.safetensors"], note="Base Qwen/Qwen3.5-0.8B-Base and data reasoning-summaries-61k are Apache-2.0."),
    # --- math -------------------------------------------------------------------
    cell(TM15, "Numberwyrm", QWEN, "math_reasoning", "reckoner", "1.5B", "disc", "GGUF-Q4_K_M",
         "runs now (qwen2)", ["*.safetensors"], para=True, langs=["en", "zh"],
         note="This exact checkpoint is Apache-2.0. Trunk for the math adapters."),
    cell("zbeeb/Qwen2.5-Math-1.5B-GRPO-Staleness-4", "Theoremwyrm", "zbeeb", "math_reasoning", "reckoner",
         "1.5B", "disc", "adapter on Qwen2.5-Math-1.5B", "runs now (qwen2 + zt_lora)", ["*.safetensors"],
         para=True, trunk=TM15),
    cell("OpceanAI/Yuuki-RxG-nano", "Puzzlewyrm", "OpceanAI", "math_reasoning", "reckoner", "1.5B",
         "disc", "adapter on Qwen2.5-Math-1.5B", "runs now (qwen2 + zt_lora)", ["*.safetensors"], para=True,
         trunk=TM15, note="Base WeiboAI/VibeThinker-1.5B (MIT, itself on Qwen2.5-Math-1.5B); credit Weibo AI too."),
    # --- paraconsistent / contradiction ------------------------------------------
    cell("Pankaj8922/bert-small-nli", "Verdictwyrmling", "Pankaj8922", "nli_contradiction", "arbiter", "29M",
         "disc_gated", "F16 safetensors", "needs BERT encoder path", ["model.safetensors"], para=True,
         note="Base prajjwal1/bert-small is MIT. NLI data descends from SNLI/MultiNLI; see data notes."),
    cell("cross-encoder/nli-deberta-v3-xsmall", "Weighwyrmling",
         "sentence-transformers / cross-encoder (UKP Lab, Nils Reimers, Tom Aarsen)", "nli_contradiction",
         "arbiter", "70M", "pack_laptop", "F16 safetensors", "needs DeBERTa encoder path", ["model.safetensors"],
         para=True, note="Base microsoft/deberta-v3-xsmall is MIT. Trained on SNLI and MultiNLI."),
    cell("iamjayeshc/ArgParser-v4-Qwen1.5B", "Debatelindworm", "Jayesh C (iamjayeshc)", "argument_mining",
         "arbiter", "1.5B", "disc", "adapter on Qwen2.5-1.5B-Instruct", "runs now (qwen2 + zt_lora)",
         ["*.safetensors"], para=True, trunk=T15),
    cell("heavyhelium/electra-base-logic-fallacy", "Fallacydrakeling", "heavyhelium", "fallacy_detection",
         "arbiter", "110M", "disc_gated", "F16 safetensors", "needs ELECTRA encoder path", ["model.safetensors"],
         para=True, note="Replaces EliaMengozzi/fallama (Llama 3.1). Base google/electra-base-discriminator is Apache-2.0; data is the LOGIC set (Jin et al. 2022)."),
    cell("MidhunKanadan/roberta-large-fallacy-classification", "Sophismdrake", "Midhun Kanadan",
         "fallacy_detection", "arbiter", "355M", "server", "F16 safetensors", "server runtime",
         ["model.safetensors"], para=True, note="Base FacebookAI/roberta-large is MIT."),
    # --- embeddings / retrieval / reranking --------------------------------------
    cell("sentence-transformers/all-MiniLM-L6-v2", "Indexserpent",
         "sentence-transformers (UKP Lab, Nils Reimers); MiniLM by Microsoft", "embeddings", "librarian",
         "22M", "disc_gated", "F16 safetensors", "needs BERT encoder path", ["model.safetensors"],
         note="Base nreimers/MiniLM-L6-H384-uncased is MIT."),
    cell("sentence-transformers/all-MiniLM-L12-v2", "Shelfserpent",
         "sentence-transformers (UKP Lab, Nils Reimers); MiniLM by Microsoft", "embeddings", "librarian",
         "33M", "pack_laptop", "F16 safetensors", "needs BERT encoder path", ["model.safetensors"],
         note="Base microsoft/MiniLM-L12-H384-uncased is MIT."),
    cell("dragonkue/multilingual-e5-small-ko", "Bridgeserpent", "dragonkue", "embeddings", "librarian", "118M",
         "pack_laptop", "F16 safetensors", "needs BERT encoder path", ["*.safetensors"],
         langs=["ko", "en", "multilingual"], note="Base intfloat/multilingual-e5-small is MIT."),
    cell("exp-models/dragonkue-KoEn-E5-Tiny", "Twinserpent", "dragonkue / exp-models", "embeddings",
         "librarian", "37M", "pack_laptop", "F16 safetensors", "needs BERT encoder path", ["*.safetensors"],
         langs=["ko", "en"], note="Built on intfloat/multilingual-e5-small (MIT)."),
    cell("jinaai/jina-reranker-v1-tiny-en", "Sortserpent", "Jina AI", "reranking", "librarian", "33M",
         "pack_laptop", "F16 safetensors", "needs JinaBERT path", ["model.safetensors"]),
    cell("jinaai/jina-reranker-v1-turbo-en", "Swiftserpent", "Jina AI", "reranking", "librarian", "38M",
         "pack_laptop", "F16 safetensors", "needs JinaBERT path", ["model.safetensors"]),
    cell("BAAI/bge-reranker-v2-m3", "Rankserpent", "BAAI (Beijing Academy of Artificial Intelligence)",
         "reranking", "librarian", "568M", "pack_workstation", "F16 safetensors", "needs XLM-R encoder path",
         ["model.safetensors"], langs=["multilingual"]),
    # --- documents / vision -------------------------------------------------------
    cell("PaddlePaddle/PP-OCRv6_small_det_onnx", "Spotwyvern", "PaddlePaddle team (Baidu)", "document_ocr",
         "reader", "small", "disc_gated", "ONNX", "needs DBNet path", ["inference.onnx"], langs=["multilingual"],
         note="ONNX release of the roster's PP-OCRv6 (the .pdiparams one was excluded for format)."),
    cell("PaddlePaddle/PP-OCRv6_small_rec_onnx", "Letterwyvern", "PaddlePaddle team (Baidu)", "document_ocr",
         "reader", "small", "disc_gated", "ONNX", "needs SVTR path", ["inference.onnx"], langs=["multilingual"]),
    cell("tiiuae/Falcon-OCR", "Scrollwyvern", "Technology Innovation Institute (TII)", "document_ocr", "reader",
         "270M", "pack_laptop", "safetensors", "needs new arch", ["*.safetensors"], langs=["en", "multilingual"],
         note="Unlike TII's Falcon LLMs, this card declares plain Apache-2.0."),
    cell("HuggingFaceTB/SmolVLM2-500M-Video-Instruct", "Watchwyvern", HFTB, "vision_language", "reader",
         "500M", "disc", "GGUF-Q8_0 + mmproj F16", "zt_vision (SigLIP + Idefics3); confirm the smolvlm2 projector on load",
         ["*.safetensors"]),
    cell("HuggingFaceTB/SmolVLM-Instruct", "Sightwyvern", HFTB, "vision_language", "reader", "2.2B",
         "pack_laptop", "GGUF-Q4_K_M + mmproj", "zt_vision (SmolVLM family)", ["*.safetensors"],
         note="Bases SmolLM2-1.7B-Instruct (Apache-2.0) and google/siglip-so400m (Apache-2.0)."),
    cell("Quazim0t0/Byrne-VLM-131M", "Glimpsewyvern", "Quazim0t0", "vision_language", "reader", "131M",
         "pack_laptop", "safetensors", "needs new arch", ["*.safetensors"]),
    # --- speech in ------------------------------------------------------------------
    cell("allenai/OLMoASR", "Listenlindworm", "Allen Institute for AI (Ai2)", "speech_recognition", "ear",
         "39M (tiny.en)", "disc", "F16 (zt_whisper)", "zt_whisper (Whisper arch)",
         ["models/OLMoASR-tiny.en/model.safetensors"],
         note="The roster's id allenai/OLMoASR-tiny does not exist; tiny.en lives inside allenai/OLMoASR."),
    cell("ibm-granite/granite-4.0-1b-speech", "Dictatelindworm", IBM, "speech_recognition", "ear", "1B",
         "pack_workstation", "safetensors", "needs granite speech arch", ["*.safetensors"],
         langs=["en", "fr", "de", "es", "pt", "ja"]),
    cell("ibm-granite/granite-speech-3.3-2b", "Hearinglindworm", IBM, "speech_recognition", "ear", "2B",
         "server", "safetensors", "server runtime", ["*.safetensors"], langs=["en", "fr", "de", "es", "pt"]),
    cell("ibm-granite/granite-speech-4.1-2b-plus", "Minutelindworm", IBM, "speech_recognition", "ear", "2B",
         "server", "safetensors", "server runtime", ["*.safetensors"], langs=["multilingual"]),
    # --- speech out ------------------------------------------------------------------
    cell("onnx-community/Kokoro-82M-v1.0-ONNX", "Chimedrake",
         "hexgrad (Kokoro); ONNX export by onnx-community; architecture StyleTTS 2 by Yinghao Aaron Li et al.",
         "text_to_speech", "voice", "82M", "disc_gated", "ONNX q8f16", "needs StyleTTS2/iSTFTNet path",
         ["onnx/model_q8f16.onnx"], langs=["en", "ja", "zh", "es", "fr", "hi", "it", "pt"],
         note="Replaces hexgrad/Kokoro-82M, whose weights are only a pickle. Kokoro's card credits CC-BY audio (Koniwa, SIWIS): credited too."),
    cell("KittenML/kitten-tts-nano-0.1", "Purrdrake", "KittenML", "text_to_speech", "voice", "15M",
         "pack_laptop", "ONNX", "needs new arch", ["*.onnx"]),
    cell("parler-tts/parler-tts-mini-multilingual", "Choirdrake", "Parler-TTS (Hugging Face)", "text_to_speech",
         "voice", "0.9B", "server", "safetensors", "server runtime", ["*.safetensors"],
         langs=["en", "fr", "es", "pt", "pl", "de", "nl", "it"]),
    # --- translation ------------------------------------------------------------------
    cell("google/madlad400-3b-mt", "Polyglotwyvern", "Google Research (MADLAD-400 team)", "translation",
         "interpreter", "3B", "server", "safetensors", "needs T5 path", ["*.safetensors"], langs=["400+ languages"]),
    # --- forecasting / tabular ------------------------------------------------------------
    cell("amazon/chronos-bolt-tiny", "Tidedrakeling", "Amazon Science (Chronos team)", "time_series", "augur",
         "9M", "disc_gated", "F16 safetensors", "needs T5 encoder path", ["model.safetensors"], langs=["n/a"],
         note="Replaces eulogik/nanoforecast-500k, whose weights are only a pickle."),
    cell("amazon/chronos-bolt-mini", "Tidedrake", "Amazon Science (Chronos team)", "time_series", "augur", "21M",
         "pack_laptop", "F16 safetensors", "needs T5 encoder path", ["model.safetensors"], langs=["n/a"]),
    cell("autogluon/mitra-classifier", "Ledgerdrake", "AutoGluon team (Amazon)", "tabular", "augur", "72M",
         "pack_laptop", "safetensors", "needs new arch", ["*.safetensors"], langs=["n/a"]),
    cell("autogluon/mitra-regressor", "Gaugedrake", "AutoGluon team (Amazon)", "tabular", "augur", "72M",
         "pack_laptop", "safetensors", "needs new arch", ["*.safetensors"], langs=["n/a"]),
    # --- anomaly detection (the roster's four were all pickles or empty) --------------------
    cell("AungMoonLord/bert-log-anomaly-detection", "Lanterndrake", "AungMoonLord", "anomaly_logs", "sentinel",
         "110M", "pack_laptop", "F16 safetensors", "needs BERT encoder path", ["model.safetensors"], para=True,
         note="Replaces stetteh/otics-anomaly. Database transaction logs, normal or anomaly. Base bert-base-uncased is Apache-2.0; training data not named: confirm."),
    cell("Fazmaj/modernbert-flow-anomaly-classifier", "Currentdrake", "Fazmaj", "anomaly_network", "sentinel",
         "150M", "pack_laptop", "F16 safetensors", "needs ModernBERT path", ["model.safetensors"], para=True,
         note="Replaces nvidia/Kumo-Anomaly. Base answerdotai/ModernBERT-base is Apache-2.0; training data not named: confirm."),
    cell("ManasKumar28/mimii-distilhubert-anomaly", "Humdrake", "Manas Kumar", "anomaly_machine_sound",
         "sentinel", "24M", "pack_laptop", "F16 safetensors", "needs HuBERT path", ["model.safetensors"], para=True,
         langs=["n/a"], note="Replaces kkonathala/powerplant-anomaly-transformer. Base ntu-spml/distilhubert is Apache-2.0; MIMII audio is CC-BY-SA-4.0 (data notes)."),
    # --- SQL --------------------------------------------------------------------------------
    cell("MidlightDDK/pocketsql-0.5b", "Recordlindworm", "MidlightDDK", "sql", "scribe", "0.5B", "disc",
         "adapter on Qwen2.5-Coder-0.5B-Instruct", "runs now (qwen2 + zt_lora)", ["*.safetensors"],
         langs=["en", "sql"], trunk=TC05, note="Training data MidlightDDK/pocketsql-data is CC-BY-SA-4.0; see data notes."),
    # --- moderation ---------------------------------------------------------------------------
    cell("ibm-granite/granite-guardian-hap-38m", "Shieldwyvern", IBM, "moderation", "sentinel", "38M",
         "pack_laptop", "F16 safetensors", "needs RoBERTa encoder path", ["model.safetensors"], para=True),
    cell("lakresi/toxic-comment-moderator", "Thornwyvern", "Lloyd Akresi", "moderation", "sentinel", "66M",
         "pack_laptop", "F16 safetensors", "needs DistilBERT path", ["*.safetensors"], para=True,
         note="Trained on the Jigsaw toxic-comment set (competition terms); see data notes."),
    cell("navodPeiris/minilm-toxic-spam-classifier", "Sievewyvern", "Navod Peiris", "moderation", "sentinel",
         "22M", "pack_laptop", "ONNX", "needs BERT encoder path", ["*.onnx"], para=True,
         note="Base nreimers/MiniLMv2 declares no licence on its card (Microsoft MiniLM is MIT upstream). Confirm before shipping."),
    # --- audio events ------------------------------------------------------------------------
    cell("mispeech/ced-tiny", "Rustlewyrmling", "Xiaomi speech team (mispeech; Heinrich Dinkel et al.)",
         "audio_events", "ear", "5.5M", "disc_gated", "F16 safetensors", "needs ViT audio path",
         ["model.safetensors"], langs=["n/a"], note="Trained on AudioSet; see data notes."),
    cell("mispeech/ced-mini", "Murmurwyrmling", "Xiaomi speech team (mispeech)", "audio_events", "ear", "10M",
         "pack_laptop", "F16 safetensors", "needs ViT audio path", ["model.safetensors"], langs=["n/a"]),
    cell("mispeech/ced-small", "Thunderwyrmling", "Xiaomi speech team (mispeech)", "audio_events", "ear", "22M",
         "pack_laptop", "F16 safetensors", "needs ViT audio path", ["model.safetensors"], langs=["n/a"]),
    # --- image generation ----------------------------------------------------------------------
    cell("AuroraAI-Research/AuroraIMG-6M", "Sketchwyvern", "AuroraAI-Research", "image_generation", "painter",
         "6.8M", "pack_laptop", "safetensors", "needs new arch", ["*.safetensors"],
         note="Trained on CC12M (web images with their own terms); see data notes."),
    cell("Arrexel/pattern-diffusion", "Tilewyvern", "Alex Reid (Arrexel)", "image_generation", "painter",
         "0.9B", "server", "safetensors", "server runtime", ["*.safetensors"]),
    cell("alfredplpl/CommonArt-PoC", "Muralwyvern", "alfredplpl (Yasunori Ozaki)", "image_generation", "painter",
         "DiT", "server", "safetensors", "server runtime", ["*.safetensors"],
         note="Trained on CommonCatalog CC-BY images, whose authors are listed with the dataset; we credit the dataset."),
    # --- robotics (logistics and mapping only, per project safety rules) ------------------------
    cell("lerobot/smolvla_base", "Gripdrake", "Hugging Face LeRobot team", "robotics_vla", "hand", "450M",
         "server", "safetensors", "server runtime", ["*.safetensors"]),
    cell("lerobot/smolvla_libero", "Reachdrake", "Hugging Face LeRobot team", "robotics_vla", "hand", "450M",
         "server", "safetensors", "server runtime", ["*.safetensors"]),
    cell("lerobot/smolvla_robotwin", "Pairdrake", "Hugging Face LeRobot team", "robotics_vla", "hand", "450M",
         "server", "safetensors", "server runtime", ["*.safetensors"]),
    cell("ThiennNguyen/act_so101_small", "Armdrakeling", "Thien Nguyen", "robotics_policy", "hand", "small",
         "server", "safetensors", "server runtime", ["*.safetensors"], langs=["n/a"]),
    # --- science --------------------------------------------------------------------------------
    cell("Haamipromax/HamAI-Science-1b", "Alembicwyrm", "Haamipromax", "science_qa", "natural_philosopher",
         "1.1B", "disc_gated", "GGUF-Q4_K_M", "needs SentencePiece tokenizer (engine task E5)", ["*.safetensors"],
         note="Base TinyLlama-1.1B-Chat (Apache-2.0); credit the TinyLlama team. Its SentencePiece tokenizer waits on E5."),
    cell("eaddario/MiniCPM5-1B-GGUF", "Starwyrm", "Ed Addario (GGUF); model by OpenBMB", "science_qa",
         "natural_philosopher", "1B", "pack_laptop", "GGUF (upstream)", "needs minicpm arch", ["*.gguf"],
         langs=["en", "zh"], note="Base openbmb/MiniCPM5-1B is tagged Apache-2.0."),
    cell("HuggingFaceTB/SmolLM2-1.7B-Instruct", "Vouchwyvern", HFTB, "witness", "arbiter", "1.7B",
         "disc", "GGUF-Q4_K_M", "runs now (llama, byte-level BPE)", ["model.safetensors"],
         note="The cross-lineage witness in harness H1: a different family from the Qwen trunks, so its agreement counts as a second witness. Byte-level BPE (SmolLM pre-split), which zt_tok reads today. Trained from scratch by Hugging Face."),
    # --- decision / calibration ----------------------------------------------------------------
    cell("autotrust/JEV-27B-VL", "Senatewyrm", "autotrust", "decision_vl", "council", "27B", "server",
         "safetensors", "server runtime", ["*.safetensors"], para=True, langs=["multilingual"]),
    cell("chinhnc/Quyet-1.0-Tiny", "Choicewyrmling", "Chinh Nguyen", "calibrated_decision", "arbiter", "183M",
         "pack_laptop", "F16 safetensors", "needs ModernBERT path", ["*.safetensors"], para=True,
         langs=["multilingual"], note="Base jhu-clsp/mmBERT-small is MIT. Ships its own NOTICE."),
    cell("chinhnc/Quyet-1.0-Small", "Choicewyrm", "Chinh Nguyen", "calibrated_decision", "arbiter", "328M",
         "pack_workstation", "F16 safetensors", "needs ModernBERT path", ["*.safetensors"], para=True,
         langs=["multilingual"], note="Base aisingapore/SEA-LION-ModernBERT-300M is MIT. Ships its own NOTICE."),
    cell("samratduttaofficial/WaterSheep", "Balancewyrm", "Samrat Dutta", "calibrated_decision", "arbiter",
         "149M", "pack_laptop", "ONNX", "needs ModernBERT path", ["*.onnx"], para=True,
         note="Trained on ~80 datasets with mixed terms (some non-commercial benchmark sets); see data notes. Ships its own NOTICE."),
    # --- creative --------------------------------------------------------------------------------
    cell("BananaMind/StoryBananaMind-7.5M", "Fablehatchling", "BananaMind", "creative_writing", "bard", "7.5M",
         "pack_laptop", "safetensors", "check arch", ["*.safetensors"],
         note="Trained on TinyStories (CDLA-Sharing-1.0); see data notes."),
    cell("VertexAGI/prism-creative-1-mini", "Fablewyvern", "VertexAGI", "creative_writing", "bard", "4B",
         "pack_laptop", "GGUF (upstream)", "runs now (qwen3)", ["*.gguf"],
         note="Base Qwen3-4B-Instruct-2507 (via an MLX 4-bit copy) is Apache-2.0."),
    # --- legal / regulatory -----------------------------------------------------------------------
    cell("sallani/EUAIAct-Qwen2.5-0.5B-Edge", "Statutewyrmling", "sallani", "regulatory_qa", "clerk", "0.5B",
         "disc", "adapter on Qwen2.5-0.5B-Instruct", "runs now (qwen2 + zt_lora)", ["*.gguf"], para=True,
         langs=["en", "fr"], trunk=T05,
         note="Advisory only, never legal advice (project policy). Adapter extracted from the upstream GGUF dequantised; if that loses quality it ships as the upstream GGUF."),
    cell("AlexandruGirlea/precommiteu-models", "Compliancewyrm", "Alexandru Girlea", "regulatory_code_scan",
         "clerk", "7B + adapters", "server", "GGUF (upstream)", "runs now (qwen2)", ["*.gguf"], para=True,
         langs=["en", "code"], note="Base Qwen2.5-Coder-7B-Instruct is Apache-2.0. Ships its own NOTICE."),
    cell("PrinceRansom7/gemma4-e2b-it-regulatory-obligation-v1", "Dutywyrm",
         "PrinceRansom7; base Gemma 4 by Google DeepMind", "regulatory_extraction", "clerk", "2B", "server",
         "safetensors", "needs gemma4 arch", ["*.safetensors"], para=True,
         note="Gemma 4 is released under Apache-2.0 (google/gemma-4-E2B-it card), unlike Gemma 1-3."),
    cell("zenlm/zen-legal", "Covenantwyrm", "zenlm", "legal_research", "clerk", "8B", "server", "safetensors",
         "runs now (qwen3)", ["*.safetensors"], para=True, note="Base Qwen3-8B is Apache-2.0. Ships its own NOTICE."),
]

# Failed one of the rules. Reason first; then the replacement.
EXCLUDED = [
    ("prithivMLmods/Draco-CoderMini-3B", "licence", "Card says Apache-2.0, but its base Qwen2.5-3B-Instruct is under the Qwen Research licence (no commercial use). A fine-tune cannot relicense its base.", "Qwen2.5-Coder-1.5B-Instruct and TIMPS-Coder-0.5B"),
    ("Qwen/Qwen2.5-Coder-3B-Instruct", "licence", "Qwen Research licence (license: other / qwen-research).", "Qwen2.5-Coder-1.5B-Instruct"),
    ("llmware/slim-extract-tool", "licence", "CC-BY-SA-4.0.", "slim-ner-tool"),
    ("llmware/slim-summary-tool", "licence", "CC-BY-SA-4.0.", "pszemraj/led-base-book-summary"),
    ("Dongwei/Qwen2.5-1.5B-Open-R1-GRPO_Math", "licence", "No licence declared; weights partly pickled (.bin).", "zbeeb/Qwen2.5-Math-1.5B-GRPO-Staleness-4"),
    ("EliaMengozzi/fallama", "licence", "Llama 3.1 Community licence.", "heavyhelium/electra-base-logic-fallacy"),
    ("eulogik/TinyDoc-VLM-256M", "licence", "No licence declared on the card.", "SmolVLM2-500M-Video-Instruct"),
    ("docling-project/SmolDocling-256M-preview", "licence", "CDLA-Permissive-2.0, not Apache-2.0 (permissive; admit only if you widen the rule).", "PP-OCRv6 small (ONNX)"),
    ("nums-ai/causilo", "licence", "Custom causilo-1.0 licence.", "autogluon/mitra-classifier"),
    ("melihemin/qwen2.5-0.5b-text2sql-full", "licence", "No licence declared.", "MidlightDDK/pocketsql-0.5b"),
    ("saadxsalman/SS-350M-SQL-Strict-GGUF", "licence", "Base LiquidAI/LFM2.5-350M is under the LFM Open License 1.0.", "MidlightDDK/pocketsql-0.5b"),
    ("openjev/openjev", "licence", "CC-BY-NC-4.0 (non-commercial).", "the three moderation cells"),
    ("yasserrmd/MegaSciMoE-1.2B", "licence", "Base LiquidAI/LFM2-1.2B is under the LFM Open License 1.0.", "Haamipromax/HamAI-Science-1b"),
    ("Arigadam/TinyStories-with-EOS", "licence", "Base Gemma 2 2B carries the Gemma terms.", "BananaMind/StoryBananaMind-7.5M"),
    ("DavidAU/L3.2-Rogue-Creative-Instruct-Uncensored-7B-GGUF", "licence", "Tagged Apache-2.0, but it is a Llama 3.2 merge (its own tags say llama-3.2), so the Llama licence applies.", "VertexAGI/prism-creative-1-mini"),
    ("Cactus-Compute/needle2", "format", "Weights published only as a pickle (checkpoints/needle2.pkl).", "FrontiersMind/Nandi-Mini-150M-Tool-Calling"),
    ("llmware/slim-sql-1b-v0", "format", "Weights only as pickled pytorch_model.bin.", "MidlightDDK/pocketsql-0.5b"),
    ("eulogik/nanoforecast-500k", "format", "Weights only as a pickle (model.pt).", "amazon/chronos-bolt-tiny"),
    ("zerooneresearch/predictlm-mini-13m", "format", "Weights only as a pickle (.pt).", "amazon/chronos-bolt-mini"),
    ("hexgrad/Kokoro-82M", "format", "Weights only as a pickle (kokoro-v1_0.pth). Licence is fine.", "onnx-community/Kokoro-82M-v1.0-ONNX (same weights)"),
    ("stetteh/otics-anomaly", "format", "Weights only as pickles (.pkl/.pth).", "AungMoonLord/bert-log-anomaly-detection"),
    ("kkonathala/powerplant-anomaly-transformer", "format", "No weight files published (code only).", "ManasKumar28/mimii-distilhubert-anomaly"),
    ("nvidia/Kumo-Anomaly", "format", "Weights only as a pickle (.pth).", "Fazmaj/modernbert-flow-anomaly-classifier"),
    ("ash12321/deep-svdd-anomaly-detection", "format", "Weights only as pickles (.pkl/.pth).", "the three anomaly cells"),
    ("jibala1022/easyrec-base", "format", "Weights only as pickles (.bin/.pt/.pth).", "all-MiniLM-L6-v2 with the Jina rerankers"),
    ("DamonDemon/SpanUQ", "format", "Weights only as a pickle (.pt).", "Tiamat's evidence head (zt_evh)"),
    ("PaddlePaddle/PP-OCRv6_tiny_rec", "format", "Paddle inference format (.pdiparams).", "PaddlePaddle/PP-OCRv6_small_*_onnx"),
    ("litert-community/SmolVLM2-500M", "format", "LiteRT .litertlm container only.", "HuggingFaceTB/SmolVLM2-500M-Video-Instruct"),
    ("onnx-community/kitten-tts-nano-0.1-ONNX", "duplicate", "Same model as KittenML/kitten-tts-nano-0.1, which publishes ONNX itself.", "KittenML/kitten-tts-nano-0.1"),
    ("mudler/ced-gguf", "duplicate", "A GGUF packaging of the CED models, which we convert from the originals; Ettore Di Giacinto is credited for ced.cpp.", "mispeech/ced-*"),
    ("segmind/Segmind-Vega", "data", "Licence is Apache-2.0, but its card lists a Midjourney-output dataset among its training data, whose terms restrict use.", "AuroraIMG-6M / pattern-diffusion"),
]

# Also checked as replacements and turned down (so nobody re-checks them).
CONSIDERED = [
    ("tjarvis91/Q-ToolCall-50M-Sovereign", "Weights only as a pickle (.pt)."),
    ("eoinedge/AnomalyNet", "Weights only as .pt/.pte/.tflite."),
    ("Jvachier/dcase2020-task2-anomaly-detection", "Keras/NumPy archive only."),
    ("kha-white/manga-ocr-base", "Weights only as pickled .bin."),
    ("Falconsai/text_summarization", "Fine, but led-base-book-summary handles long documents, which swarm memory needs."),
]

# Data notes: weights are Apache-2.0, but training data carries its own terms.
DATA_NOTES = {
    "nli": "SNLI is CC-BY-SA-4.0 and MultiNLI mixes CC-BY-3.0, CC-BY-SA-3.0 and other terms. The arbiter's T/F/B/N training uses Tiamat's own derived statements (tools/tiamat/server/make_tfbn_data.py), so its evidence head does not depend on them.",
    "jigsaw": "Jigsaw toxic-comment data came from a Kaggle competition with its own terms.",
    "audioset": "AudioSet labels are CC-BY-4.0; the audio is YouTube clips.",
    "mimii": "The MIMII machine-sound set is CC-BY-SA-4.0 (share-alike for the data, not for model outputs).",
    "logic": "The LOGIC fallacy set (Jin et al. 2022) is published with its paper's code.",
    "booksum": "BookSum (Salesforce) is BSD-3-Clause.",
    "tinystories": "TinyStories is CDLA-Sharing-1.0 (share-alike for the data, not for model outputs).",
    "cc12m": "CC12M is a list of web image URLs, each under its own terms.",
    "pocketsql": "pocketsql-data is CC-BY-SA-4.0.",
    "watersheep": "WaterSheep's card lists about 80 training sets, some non-commercial.",
}
