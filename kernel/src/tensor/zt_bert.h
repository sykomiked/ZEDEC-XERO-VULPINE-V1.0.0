/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_bert.h — BERT and ELECTRA encoders, in integers: the checker cells
 * (classifiers and sentence embedders) beside the language model.
 *
 *   T26 ENCODER CELLS.  zt_bert_load reads a GGUF written by
 *       bench/tensor/convert_bert.py from a Hugging Face BertModel,
 *       BertForSequenceClassification or ElectraForSequenceClassification
 *       checkpoint (llama.cpp's BERT tensor names, so a llama.cpp BERT file
 *       has the same layout; the zxv.wordpiece.* keys say how the text is
 *       normalised and are required).
 *
 *       TOKENIZER (zt_bert_tokenize): the Hugging Face BertNormalizer,
 *       BertPreTokenizer and WordPiece model, and the [CLS] A [SEP] (B [SEP])
 *       template, giving the same ids as the `tokenizers` library:
 *         clean_text removes NUL, U+FFFD and control characters and turns
 *         white space into ' '; chinese_chars puts spaces around CJK
 *         ideographs; strip_accents is NFD (Hangul arithmetically, canonical
 *         reordering of combining marks) with nonspacing marks dropped;
 *         lowercase maps each code point on its own (no final-sigma rule,
 *         as the library); words split at white space, and each punctuation
 *         character is a word; WordPiece takes the longest vocabulary entry
 *         from the left ("##" pieces after the first), and a word longer
 *         than max_chars or with a piece not in the vocabulary is [UNK].
 *         The character classes and maps are zt_wp_tables.h, read from the
 *         library one code point at a time (gen_wp_tables.py). The
 *         vocabulary is stored as llama.cpp stores it: a word-start piece
 *         carries U+2581 in front, a "##" piece is stored bare.
 *         parse_special: the text of a special token ([CLS], [SEP], [UNK], ...)
 *         inside the input becomes that token, as the library does by
 *         default; off, such text is tokenized as words, so text from
 *         outside cannot place a separator.
 *         A byte sequence that is not UTF-8 is read as U+FFFD (removed by
 *         clean_text; [UNK] otherwise).
 *         Too long for max_tokens: the end is cut; with B, the shorter of
 *         A and B (A on a tie) keeps up to half the room and the longer
 *         takes the rest (the library's longest_first).
 *
 *       ENCODER (post-norm): token, position and token-type rows summed,
 *       LayerNorm; per layer Q/K/V (+ bias), non-causal attention, output
 *       (+ bias), residual, LayerNorm; up (+ bias), GELU (erf form), down
 *       (+ bias), residual, LayerNorm. Every step is zt_nn.h's, so the bits
 *       are the same on every machine and kernel set.
 *
 *       HEADS. zt_bert_classify: the first token's vector through cls
 *       (+ bias), tanh (BERT's pooler; tanh x = 2 sigmoid(2x) - 1) or GELU
 *       (ELECTRA's classification head), then cls.output (+ bias): one logit
 *       per label, Q16. zt_bert_embed: the mean of all token vectors (exact
 *       64-bit sums, rounded), then, when the file says so, divided by its
 *       length (a 64-bit integer square root): a sentence embedding, Q16.
 *       What a label means to the swarm (a verdict in the four states) is
 *       the caller's: the labels are in the file (zt_bert_label).
 *
 *       HONEST LIMITS. One sequence (or pair) at a time, no padding; at
 *       most n_ctx tokens. embedding_size must equal hidden_size (ELECTRA-
 *       small's projection is not handled; convert_bert.py refuses it).
 * Freestanding: no libc, no malloc, no floating point. */
#ifndef ZT_BERT_H
#define ZT_BERT_H

#include <stdint.h>
#include <stdbool.h>
#include "zt_nn.h"

enum { ZT_BERT_BERT = 1, ZT_BERT_ELECTRA = 2 };
enum { ZT_BERT_POOL_MEAN = 1, ZT_BERT_POOL_CLS = 2 }; /* llama.cpp's pooling_type values */

#define ZT_BERT_MAX_CHARS 1024u /* longest max_chars a file may set */

typedef struct {
    uint32_t arch;
    uint32_t n_vocab, n_ctx, n_embd, n_ff, n_head, n_layer, n_types, n_labels;
    uint32_t eps_f32;
    uint32_t pooling;
    bool normalize; /* zt_bert_embed divides by the length */
    bool clean_text, chinese_chars, strip_accents, lowercase;
    uint32_t max_chars;
    uint32_t max_tokens; /* the length the model was trained to (zt_bert_tokenize's usual limit) */
    int32_t cls_id, sep_id, unk_id;
} zt_bert_cfg_t;

typedef struct {
    zt_mat_t wq, wk, wv, wo, wu, wd;
    const zt_fx *bq, *bk, *bv, *bo, *bu, *bd;
    zt_nn_norm_t ln_att, ln_out;
} zt_bert_layer_t;

typedef struct {
    zt_bert_cfg_t cfg;
    zt_gguf_tensor_t tok_embd; /* read a row at a time */
    const zt_fx *pos, *types;
    zt_nn_norm_t ln_embd;
    zt_bert_layer_t *layers;
    zt_mat_t cls_w, out_w; /* the head, when n_labels > 0 */
    const zt_fx *cls_b, *out_b;
    zt_gguf_str_t *vocab;         /* n_vocab strings, pointing into the gguf buffer */
    uint32_t *special, n_special; /* control and unknown tokens: their text can be parsed */
    uint32_t *hash, hash_mask;    /* id + 1 per slot, 0 empty */
    const zt_gguf_t *g;
    const struct zt_kern *kern; /* null: C reference; a hosted caller may set one */
} zt_bert_t;

int32_t zt_bert_config(const zt_gguf_t *g, zt_bert_cfg_t *c, zt_model_err_t *err);
uint64_t zt_bert_arena_bytes(const zt_gguf_t *g);
/* The gguf buffer must outlive the encoder (weights and vocabulary are used
 * in place). */
int32_t zt_bert_load(zt_bert_t *b, const zt_gguf_t *g, void *arena, uint64_t arena_bytes,
                     zt_model_err_t *err);

/* Label i's name (classifiers). */
int32_t zt_bert_label(const zt_bert_t *b, uint32_t i, zt_gguf_str_t *out);
/* The id of a vocabulary entry as stored (U+2581 in front of word starts),
 * or -1. */
int32_t zt_bert_find(const zt_bert_t *b, const uint8_t *s, uint64_t len);

/* Work for tokenizing texts of len_a + len_b bytes. */
uint64_t zt_bert_tok_work_bytes(uint64_t len_a, uint64_t len_b);
/* ids/types receive [CLS] A [SEP] (B [SEP]) cut to max_tokens (at least 3
 * with B, 2 without; at most n_ctx); *n the count. b may be null (one text). */
int32_t zt_bert_tokenize(const zt_bert_t *b, const uint8_t *a, uint64_t len_a, const uint8_t *bt,
                         uint64_t len_b, bool parse_special, uint32_t max_tokens, int32_t *ids,
                         uint8_t *types, uint32_t *n, void *work, uint64_t work_bytes);

/* Work for n tokens (n <= n_ctx). */
uint64_t zt_bert_work_bytes(const zt_bert_t *b, uint32_t n);
/* logits: n_labels Q16 values. n_sat (may be null) receives the count of
 * values that hit the Q16 range limit. */
int32_t zt_bert_classify(const zt_bert_t *b, const int32_t *ids, const uint8_t *types, uint32_t n,
                         zt_fx *logits, void *work, uint64_t work_bytes, uint32_t *n_sat);
/* out: n_embd Q16 values. */
int32_t zt_bert_embed(const zt_bert_t *b, const int32_t *ids, const uint8_t *types, uint32_t n,
                      zt_fx *out, void *work, uint64_t work_bytes, uint32_t *n_sat);

#endif /* ZT_BERT_H */
