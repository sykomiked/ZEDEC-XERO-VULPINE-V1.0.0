<!-- SPDX-FileCopyrightText: 2026 Michael Laurence Curzi & 36N9 GENETICS LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Encoder cells on the integer engine (engine route step 1), 2026-10-15

The three disc cells on the BERT and ELECTRA path, converted with
`bench/tensor/convert_bert.py` and run by `bench/tensor/zt_enc` (zt_bert),
against the Hugging Face stack they came from
(`bench/tensor/compare/ref_bert.py`, tokenizers 0.23.3, transformers 5.19.0,
float64). Bounds written before the run: logits within 0.15 with the top
label agreeing outside near ties; embeddings cosine at least 0.999.

| Cell | Source (pinned) | File | Tokens: cases differing | Forward |
|---|---|---|---|---|
| Fallacydrakeling | heavyhelium/electra-base-logic-fallacy 8327c1fa | F16 | 0 of 3,088, both special modes | max logit diff 0.0089, top label 35 of 35 |
| Verdictwyrmling | Pankaj8922/bert-small-nli cd578a15 | F16 | 0 of 3,088, both | max logit diff 0.0022, top label 35 of 35 |
| Indexserpent | sentence-transformers/all-MiniLM-L6-v2 1110a243 | F16 | 0 of 3,088, both | cosine min 1.000000 |
| Fallacydrakeling | the same | Q8_0 | 0 | max logit diff 0.2149: FAILS the 0.15 bound (top label still 35 of 35) |
| Verdictwyrmling | the same | Q8_0 | 0 | max logit diff 0.0475, passes |
| Indexserpent | the same | Q8_0 | 0 | cosine min 0.999617, passes |

F16 is the format the disc ships (convert_bert.py's default): the engine's
own arithmetic is within 0.009 of float64 there, and the Q8_0 differences
are the 8-bit weight rounding, largest on out-of-domain text (Japanese and
Korean into English models, every word [UNK]).

Every kernel set on the test machine (c, avx2, avx512, avx512vnni), with one
and four threads, gave the same bits on all three cells.

F16 GGUF SHA-256 (deterministic: a second conversion gave the same bytes):

    aee6364f566d851cdcebe7817d8b041cd17ce372304b9444c7120f099ce82946  Fallacydrakeling
    3cb1d71d95c3d2d74350bd95169965dc7a9b43b8c0e0e148d0cd3eda0072f8dc  Verdictwyrmling
    e74c0cc956d145a93b0cf98ccc6c7b42e260df213db4967d3b684ee4295a5a41  Indexserpent

Notes. Verdictwyrmling's tokenizer does not lowercase although its
vocabulary is uncased (as published), so capitalised words become [UNK];
the engine does what the checkpoint's tokenizer does. Fallacydrakeling's
13 labels are all fallacy types (no "no fallacy" label): it names the
likeliest fallacy, it does not say whether there is one.
