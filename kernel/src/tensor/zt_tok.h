/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* zt_tok.h — byte-level BPE tokenizer (GPT-2 / Qwen2 / Llama 3 family),
 * read straight from a GGUF model file.
 *
 *   T19 TOKENIZER.  The vocabulary and merges stay in the mapped GGUF file;
 *       zt_tok_load only builds index tables in a caller-supplied arena (no
 *       allocation, no copies of the strings). Encoding follows the
 *       reference (Hugging Face tokenizers, and llama.cpp) step for step:
 *         1. special tokens (GGUF token types CONTROL and USER_DEFINED) are
 *            cut out first, leftmost-longest;
 *         2. the rest is pre-split by the model's regular expression, here
 *            implemented by hand with the same alternation order and
 *            backtracking result, using generated Unicode tables for \p{L},
 *            \p{N} and \s (zt_tok_unicode.h);
 *         3. each piece's bytes map to the GPT-2 byte alphabet, then the
 *            lowest-ranked adjacent pair is merged until none applies (ties
 *            go to the leftmost pair). A heap keeps this O(n log n) per
 *            piece, so a long run of spaces cannot stall the encoder.
 *       Supported pre-tokenizers: "qwen2" (and "deepseek-r1-qwen"), and
 *       "llama-bpe" (Llama 3: digits in groups of up to three, and a piece
 *       that is already a vocabulary entry is taken whole). Other models
 *       (SentencePiece "llama", WordPiece) report ZT_GGUF_EUNSUPPORTED
 *       rather than tokenising wrongly. Invalid UTF-8 bytes in the input are
 *       kept, one per character, so every input round-trips. The encoder
 *       never adds BOS; the caller prepends t->bos when t->add_bos says so.
 */
#ifndef ZT_TOK_H
#define ZT_TOK_H

#include <stdint.h>
#include <stdbool.h>
#include "zt_gguf.h"

enum { ZT_TOK_PRE_QWEN2 = 1, ZT_TOK_PRE_LLAMA3 = 2 };

/* GGUF token types */
enum {
    ZT_TOK_NORMAL = 1,
    ZT_TOK_UNKNOWN = 2,
    ZT_TOK_CONTROL = 3,
    ZT_TOK_USER = 4,
    ZT_TOK_UNUSED = 5,
    ZT_TOK_BYTE = 6
};

/* Errors beyond the ZT_GGUF_* codes. */
enum { ZT_TOK_ESPACE = -20, ZT_TOK_EBADID = -21 };

typedef struct {
    uint32_t n_vocab, n_merges;
    const zt_gguf_str_t *tok; /* [n_vocab] token strings (point into the file) */
    const uint8_t *ttype;     /* [n_vocab] GGUF token types */
    const uint32_t *vhash;    /* token lookup: id + 1, 0 = empty */
    uint32_t vmask;
    const uint32_t *merge; /* [n_merges][3]: left id, right id, result id; rank = index */
    const uint32_t *mhash; /* pair lookup: merge index + 1 */
    uint32_t mmask;
    const uint32_t *special; /* special token ids, longest first */
    uint32_t n_special;
    uint8_t special_first[32]; /* bitmap of the specials' first bytes */
    int32_t byte_tok[256];     /* token of each raw byte */
    uint16_t byte_cp[256];     /* byte -> its GPT-2 alphabet code point */
    uint8_t unbyte[68];        /* GPT-2 alphabet code point 256 + i -> byte */
    uint32_t pre;
    int32_t bos, eos; /* -1 when absent */
    bool add_bos;
} zt_tok_t;

/* Arena bytes zt_tok_load needs for this file (0 when it has no BPE vocab). */
uint64_t zt_tok_arena_bytes(const zt_gguf_t *g);
int32_t zt_tok_load(zt_tok_t *t, const zt_gguf_t *g, void *arena, uint64_t arena_bytes);
/* Override the pre-tokenizer named in the file. */
void zt_tok_set_pre(zt_tok_t *t, uint32_t pre);

/* Token id of an exact string, or -1. */
int32_t zt_tok_find(const zt_tok_t *t, const uint8_t *s, uint64_t len);

/* Scratch bytes zt_tok_encode needs for len bytes of text. */
uint64_t zt_tok_work_bytes(uint64_t len);
/* Encode text to ids. With parse_special false, special-token text is
 * tokenised like any other text. Returns ZT_TOK_ESPACE when cap or the
 * work area is too small (*n_ids then holds the ids written so far). */
int32_t zt_tok_encode(const zt_tok_t *t, const uint8_t *text, uint64_t len, bool parse_special,
                      int32_t *ids, uint64_t cap, uint64_t *n_ids, void *work, uint64_t work_bytes);
/* Decode ids to bytes (special tokens are written as their text). */
int32_t zt_tok_decode(const zt_tok_t *t, const int32_t *ids, uint64_t n, uint8_t *out, uint64_t cap,
                      uint64_t *n_out);

#endif /* ZT_TOK_H */
