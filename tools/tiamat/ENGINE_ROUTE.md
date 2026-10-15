<!-- SPDX-FileCopyrightText: 2026 Michael Laurence Curzi & 36N9 GENETICS LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Engine route: the 15 disc cells that are not text cells yet

Fifteen disc cells cannot run on the integer engine (`kernel/src/tensor`,
zt) yet. This page says which engine path each one needs, what that path is
in integers, and the order they will be built in (2026-10-15). Every path
follows the same rules as the text cells:

- integer arithmetic only: Q16 activations, 8-bit weight blocks, exact
  64-bit sums, tables generated offline (zt_nn.h); the same bits on every
  machine;
- each path is tested against its reference (Hugging Face or llama.cpp)
  on fixed inputs, with the error bound written before the run;
- a cell answers with a verdict or a value that the swarm can witness:
  classifiers map to the four states (TRUE, FALSE, BOTH, NEITHER) and join
  with `lpres_conjoin` / `lpres_disjoin`; generators are witnessed by a cell
  of another lineage, as now;
- weights are converted offline into GGUF by a script in the repository
  and pinned by SHA-256; nothing is downloaded at run time.

## Order

The order follows the paraconsistent design: the checkers first. A
classifier that reads two statements and says "entails", "contradicts" or
"neither" cannot invent an answer, and it is a different architecture and
a different developer from every text cell, so it is the strongest witness
the swarm can add.

| Step | Path | Cells | What the engine needs | Size of the work |
|---|---|---|---|---|
| 1 | Encoder (BERT, ELECTRA) | Fallacydrakeling, Verdictwyrmling, Indexserpent | WordPiece tokenizer; the encoder stack (already in zt_nn: LayerNorm with bias, GELU, non-causal attention); absolute position and token-type embeddings; a classification head (logits to the four states) and mean pooling for embeddings; a converter from SafeTensors to GGUF | medium: the blocks exist (zt_vision uses them) |
| 2 | Wire what exists | Listenlindworm (zt_whisper), Watchwyvern (zt_vision) | convert_whisper.py reading OLMoASR's layout; confirm the SmolVLM2 projector; the host passes audio and images to them (zxvd), the swarm witnesses their text | small |
| 3 | SentencePiece tokenizer (engine task E5) | Parleywyvern (EuroLLM), Alembicwyrm (TinyLlama base), Tagwyrmling (slim-ner) | zt_tok reading the "llama" SentencePiece vocabulary (scores, byte fallback, U+2581 spaces); the Llama graph already runs | medium |
| 4 | Small vision and audio nets | Rustlewyrmling (CED, a ViT on log-mel), Spotwyvern (DBNet), Letterwyvern (SVTR) | ViT on spectrogram patches (zt_vision's blocks, whisper's log-mel); 2-D convolution, folded batch norm and upsampling for DBNet; convolution, transformer and CTC decoding for SVTR; an ONNX-to-GGUF converter for the two OCR models | medium to large |
| 5 | Encoder-decoders | Tidedrakeling (Chronos-Bolt, T5), Gistwyrm (LED) | T5 relative position buckets and RMS norm; LED's local and global attention; cross-attention (zt_whisper has it) | large |
| 6 | Speech out | Chimedrake (Kokoro, StyleTTS2 + iSTFTNet) | style encoder, duration predictor, 1-D convolutions, inverse STFT in integers (zt_whisper has the forward STFT) | large |
| 7 | Custom architecture | Toolhatchling (Nandi) | its own layer definition, read from its code and checked against it | unknown until read |

What lands first: step 1, tested on Fallacydrakeling (ELECTRA on
google/electra-base-discriminator, Apache-2.0 all the way down), then the
two BERT cells.

## Status

- Step 1, the engine: done (2026-10-15). `kernel/src/tensor/zt_bert.{h,c}`
  (WordPiece tokenizer, encoder, classifier and embedding heads),
  `bench/tensor/convert_bert.py`, `bench/tensor/zt_enc.c`; unit test
  `test_zt_bert.c` in `make -C kernel verify-all`. All three cells match the
  Hugging Face stack in F16: the same token ids on 3,088 texts, logits
  within 0.009, embedding cosine 1.000000
  (`bench/results/2026-10-15-tiamat/encoders/`).
- Step 1, the swarm: the cells are not yet called by the host (zxv_tiamat);
  that, with the four-state mapping of each classifier's labels, is next.

## Open: two encoder cells sit on MIT bases

The owner chose Apache only for the large roster. Two step-1 cells are
Apache-2.0 checkpoints on MIT bases: Verdictwyrmling (on
prajjwal1/bert-small, MIT) and Indexserpent (all-MiniLM-L6-v2, on
nreimers/MiniLM-L6-H384-uncased, MIT). The encoder path is the same either
way; whether those two cells stay or are replaced by Apache-lineage models
is the owner's call.
