/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zt_gram.h — grammar-constrained decoding: a model may only write text a
 * grammar admits.
 *
 *   G1 THE GRAMMAR.  A GBNF-style text (llama.cpp's format, the subset
 *       below), compiled into the caller's arena:
 *         root   ::= "TREATY " name " " op "\n"   # a rule; '#' to end of line
 *         op     ::= "NET" | "SETL"                # alternatives
 *         name   ::= [A-Z] [A-Z0-9-]{0,29}         # byte classes, repetition
 *         list   ::= item ("," item)*              # groups
 *       Literals "..." and classes [...] take the escapes \n \r \t \\ \"
 *       \' \] \[ \- \^ and \xHH; [^...] negates; . is any byte. Postfix
 *       * + ? {m} {m,} {m,n} (m, n <= ZT_GRAM_MAX_REP) bind to the
 *       primary before them. A rule ends where the next "name ::=" starts.
 *       The root is the rule called "root" unless the caller names another.
 *       Groups and repetitions become generated rules, as in llama.cpp.
 *       Refused at compile time: syntax errors, a reference to an undefined
 *       rule, a rule defined twice, and left recursion (direct, indirect,
 *       or through a part that can be empty, such as ("" )* ): every
 *       expansion therefore ends, and matching needs no depth guard to
 *       terminate (it keeps one for capacity).
 *   G2 MATCHING.  A pushdown matcher over BYTES: the state is a set of
 *       stacks of grammar positions, advanced one byte at a time, with
 *       duplicates merged. Text is accepted when some stack is empty.
 *       Fixed capacity (stacks, depth), chosen by the caller; running out
 *       is an error (ZT_GRAM_ECAP) and leaves the state as it was: the
 *       language is never silently narrowed.
 *   G3 THE TOKEN MASK.  For a vocabulary (zt_gram_vocab_init decodes each
 *       token once), zt_gram_mask sets one bit per token: 1 when the
 *       token's bytes keep the text inside the grammar, and for the end
 *       tokens (eos and any added) only when the text so far is complete.
 *       Control, unused and unknown tokens and empty tokens are never
 *       allowed. The sampler then chooses only among allowed tokens
 *       (zt_sample_masked in zt_model.h), so the probabilities of allowed
 *       tokens keep their ratios and nothing is renormalised by hand.
 *   G4 ONE ANSWER EVERYWHERE.  Integer only, no floating point, no
 *       allocation, no recursion, no libc: the same grammar, text and
 *       vocabulary give the same states and masks on every machine.
 *
 * HONEST LIMITS. Bytes, not characters: a class matches one byte, so
 * [é] cannot be written (a literal "é" works, as two bytes). No
 * lookahead, no semantic rules (lengths up to a bound are fine; "the
 * weights sum to 1" is not a grammar). The mask costs, per step, one
 * simulation for each token whose first byte can come next; there is no
 * token trie yet, so a grammar that admits almost anything (free text)
 * costs about the whole vocabulary's length in matcher steps. A grammar
 * the model finds unnatural still forces the text: it constrains the
 * form, never the truth of what is written.
 */
#ifndef ZT_GRAM_H
#define ZT_GRAM_H

#include <stdbool.h>
#include <stdint.h>
#include "zt_tok.h"

#define ZT_GRAM_MAX_RULES 4096u  /* named and generated */
#define ZT_GRAM_MAX_ELEMS 65000u /* compiled grammar elements */
#define ZT_GRAM_MAX_NEST  32u    /* nested groups */
#define ZT_GRAM_MAX_REP   255u   /* m and n in {m,n} */
#define ZT_GRAM_MAX_DEPTH 255u   /* stack depth */
#define ZT_GRAM_MAX_END   8u     /* end tokens in a vocabulary */

enum {
    ZT_GRAM_ESYNTAX = -40,  /* the text is not a grammar */
    ZT_GRAM_EUNDEF = -41,   /* a reference to a rule that is not defined */
    ZT_GRAM_EDUP = -42,     /* a rule defined twice */
    ZT_GRAM_ELEFTREC = -43, /* left recursion, or a repeated part that can be empty */
    ZT_GRAM_ESPACE = -44,   /* a caller buffer is too small */
    ZT_GRAM_ELIMIT = -45,   /* too many rules or elements, nesting or repetition too deep */
    ZT_GRAM_ENOROOT = -46,  /* no rule with the root's name */
    ZT_GRAM_EARG = -47,     /* bad argument */
    ZT_GRAM_ECAP = -48,     /* the matcher's stack capacity ran out (state unchanged) */
    ZT_GRAM_EREJECT = -49   /* the grammar does not admit this text (state unchanged) */
};

/* element types */
enum { ZT_GE_END = 0, ZT_GE_ALT = 1, ZT_GE_BYTE = 2, ZT_GE_CLASS = 3, ZT_GE_REF = 4 };

typedef struct {
    uint8_t type; /* ZT_GE_* */
    uint8_t pad;
    uint16_t v; /* byte, class index or rule id */
} zt_gram_el_t;

typedef struct {
    const zt_gram_el_t *el; /* every rule's alternatives, each rule closed by END */
    uint32_t n_el;
    const uint32_t *rule; /* first element of each rule */
    uint32_t n_rules, n_named;
    const uint8_t (*cls)[32]; /* byte bitmaps */
    uint32_t n_cls;
    uint32_t root;
} zt_gram_t;

typedef struct {
    int32_t code;
    uint32_t off;     /* byte offset in the grammar text */
    const char *what; /* static text */
} zt_gram_err_t;

/* Arena bytes the grammar needs (0 when it does not compile; err says
 * why). root may be NULL for "root". */
uint64_t zt_gram_arena_bytes(const uint8_t *src, uint32_t len, const char *root,
                             zt_gram_err_t *err);
int32_t zt_gram_compile(zt_gram_t *g, const uint8_t *src, uint32_t len, const char *root,
                        void *arena, uint64_t arena_bytes, zt_gram_err_t *err);

/* ---- matching ---- */
typedef struct {
    uint16_t *pos; /* [max_stacks][max_depth] grammar positions, top last */
    uint8_t *dep;  /* [max_stacks] */
    uint32_t n;
} zt_gram_set_t;

typedef struct {
    const zt_gram_t *g;
    uint32_t ms, md;            /* max stacks, max depth */
    zt_gram_set_t cur, x, y, w; /* live set; two scratch sets; the expansion work list */
} zt_gram_state_t;

uint64_t zt_gram_state_bytes(uint32_t max_stacks, uint32_t max_depth);
/* Start at the beginning of the root rule. */
int32_t zt_gram_state_init(zt_gram_state_t *s, const zt_gram_t *g, uint32_t max_stacks,
                           uint32_t max_depth, void *mem, uint64_t bytes);
int32_t zt_gram_reset(zt_gram_state_t *s);
/* Consume text: OK, or EREJECT / ECAP with the state unchanged. */
int32_t zt_gram_accept(zt_gram_state_t *s, const uint8_t *text, uint32_t len);
/* The text so far is a whole sentence of the grammar. */
bool zt_gram_complete(const zt_gram_state_t *s);
/* Complete, and no byte may follow. */
bool zt_gram_finished(const zt_gram_state_t *s);
/* Bitmap of the bytes that may come next. */
void zt_gram_next_bytes(const zt_gram_state_t *s, uint8_t out[32]);
/* Would this text be accepted from here (state not changed)? 1 yes, 0 no,
 * or ECAP. */
int32_t zt_gram_test(zt_gram_state_t *s, const uint8_t *text, uint32_t len);

/* ---- vocabulary and token mask ---- */
typedef struct {
    uint32_t n;
    const uint32_t *off; /* [n + 1] offsets into bytes */
    const uint8_t *bytes;
    const uint8_t *skip; /* [n] never allowed (control, unused, unknown, empty) */
    int32_t end[ZT_GRAM_MAX_END];
    uint32_t n_end;
} zt_gram_vocab_t;

uint64_t zt_gram_vocab_bytes(const zt_tok_t *t);
/* Decode every token once; tok->eos becomes the first end token. */
int32_t zt_gram_vocab_init(zt_gram_vocab_t *v, const zt_tok_t *t, void *mem, uint64_t bytes);
/* Another end token (a chat's end-of-turn id). */
int32_t zt_gram_vocab_add_end(zt_gram_vocab_t *v, int32_t id);
/* allow: (n_vocab + 7) / 8 bytes, bit i = token i. Returns how many
 * tokens are allowed (0: nothing fits), or ECAP / EARG. */
int32_t zt_gram_mask(zt_gram_state_t *s, const zt_gram_vocab_t *v, uint8_t *allow,
                     uint32_t n_vocab);
/* After a token is chosen: an end token needs a complete text; any other
 * token's bytes are consumed. EREJECT / ECAP leave the state unchanged. */
int32_t zt_gram_accept_token(zt_gram_state_t *s, const zt_gram_vocab_t *v, int32_t id);

/* zt_model_generate's filter hook (zt_gen_filter_t), ctx a zt_gram_hook_t. */
typedef struct {
    zt_gram_state_t *st;
    const zt_gram_vocab_t *v;
} zt_gram_hook_t;
int32_t zt_gram_hook_mask(void *ctx, uint8_t *allow, uint32_t n_vocab);
int32_t zt_gram_hook_accept(void *ctx, int32_t id);

/* ---- grammars for the shorthands (zt_gram_lib.c) ---- */
/* "zxt-sutra" (treaty compact form, src/treaty/zxt.h), "lpx-tag" (the
 * 64-state tag's text form, src/lpres/lpres_hex.h), "json" (RFC 8259,
 * nesting up to the matcher's depth). NULL for an unknown name. */
const char *zt_gram_lib(const char *name, uint32_t *len);

#endif /* ZT_GRAM_H */
