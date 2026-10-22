/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_ai_engine.c — the model in a browser tab: the same integer engine as
 * the app and the golden disc (kernel/src/tensor), built to WebAssembly with
 * the WebGPU backend (zt_wgpu.h) beside the CPU kernels. zxv_ai_worker.js
 * drives it from a Worker; it builds natively too, for the tests that prove
 * the browser's answer is the app's.
 *
 *   zw_load(buf, len)        the GGUF's bytes (the caller keeps them alive
 *                            until zw_free): weights, tokenizer, a KV cache
 *                            of up to 2048 tokens, and the engine the
 *                            determiner measures as fastest and exact
 *                            (zt_choose.h: WebGPU where the browser has it,
 *                            the CPU otherwise); 0, or <0 with zw_error
 *   zw_device(), zw_record() what runs the model; the determiner's line
 *   zw_start(prompt, max)    a request: the prompt wrapped in the chat
 *                            template exactly as the app's glue does
 *                            (zxv_zt_glue.c G2), read in one pass; the
 *                            number of prompt tokens, or <0
 *   zw_next()                one greedy step (G3): the token's id, or -1
 *                            when the reply is over (zw_stop says why), or
 *                            -2 when the forward pass failed
 *   zw_text()                the reply's text since the last call, cut at
 *                            whole UTF-8 characters (the rest follows);
 *                            NUL bytes a token may decode to are left out,
 *                            so the text is one C string
 *   zw_digest()              FNV-1a over every logit this request computed,
 *                            16 hex digits: the same on every engine and
 *                            every machine for the same model and prompt
 *   zw_free()
 *
 * One request at a time; integers only (rule R1). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zt_accel.h"
#include "zt_choose.h"
#include "zt_gguf.h"
#include "zt_model.h"
#include "zt_simd.h"
#include "zt_tok.h"

#if defined(__EMSCRIPTEN__)
#    include <emscripten.h>
#    define ZW_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#    define ZW_EXPORT
#endif

#define ZW_CTX   2048u
#define ZW_BATCH 64u
#define ZW_TEXT  65536u /* the reply's text, decoded */

static struct {
    zt_gguf_t g;
    zt_model_t m;
    void *arena;
    zt_tok_t tok;
    void *tok_arena;
    zt_model_state_t s;
    void *state_raw, *state_mem;
    uint32_t n_ctx;
    zt_engine_t *eng;
    bool loaded;
    int32_t im_start, im_end, eot;
    /* the request */
    zt_fx *logits;
    int32_t *gen;
    uint32_t max_new, n_gen;
    bool active;
    uint64_t digest;
    const char *stop;
    char text[ZW_TEXT], piece[ZW_TEXT];
    uint64_t emitted; /* bytes of text already handed out */
    char err[256];
} W;

static void fail(const char *what, int32_t code)
{
    snprintf(W.err, sizeof W.err, "%s (error %d)", what, (int) code);
}

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ull;
    return h;
}

ZW_EXPORT void zw_free(void)
{
    zt_engine_free(W.eng); /* before the memory its GPU shares */
    free(W.arena);
    free(W.tok_arena);
    free(W.state_raw);
    free(W.logits);
    free(W.gen);
    memset(&W, 0, sizeof W);
}

ZW_EXPORT const char *zw_error(void)
{
    return W.err;
}

ZW_EXPORT const char *zw_device(void)
{
    return W.eng ? zt_engine_device(W.eng) : "";
}

ZW_EXPORT const char *zw_record(void)
{
    return W.eng ? zt_engine_record(W.eng) : "";
}

static int32_t find_tok(const char *s)
{
    return zt_tok_find(&W.tok, (const uint8_t *) s, strlen(s));
}

ZW_EXPORT int32_t zw_load(const uint8_t *buf, uint32_t len)
{
    zw_free();
    int32_t r = zt_gguf_open(&W.g, buf, len);
    if (r) {
        fail("not a GGUF file", r);
        return -1;
    }
    zt_model_err_t me;
    memset(&me, 0, sizeof me);
    uint64_t ab = zt_model_arena_bytes(&W.g);
    if (!ab) {
        zt_model_cfg_t cfg;
        r = zt_model_config(&W.g, &cfg, &me);
        snprintf(W.err, sizeof W.err, "the engine cannot run this file (%s, error %d)",
                 me.what ? me.what : (me.name[0] ? me.name : "unsupported"),
                 (int) (r ? r : me.code));
        zw_free();
        return -1;
    }
    if (ab > SIZE_MAX / 2 || !(W.arena = malloc((size_t) ab))) {
        fail("not enough memory for the weights", -1);
        zw_free();
        return -1;
    }
    r = zt_model_load(&W.m, &W.g, W.arena, ab, &me);
    if (r) {
        snprintf(W.err, sizeof W.err, "the weights did not load (%s, error %d)",
                 me.what ? me.what : "load", (int) r);
        zw_free();
        return -1;
    }
    W.m.kern = zt_simd_default();
    uint64_t tb = zt_tok_arena_bytes(&W.g);
    if (!tb || !(W.tok_arena = malloc((size_t) tb)) ||
        (r = zt_tok_load(&W.tok, &W.g, W.tok_arena, tb)) != 0) {
        fail("the file has no tokenizer this engine reads", tb ? r : -1);
        zw_free();
        return -1;
    }
    if (W.tok.n_vocab != W.m.cfg.n_vocab) {
        fail("the tokenizer and the model disagree on the vocabulary", (int32_t) W.tok.n_vocab);
        zw_free();
        return -1;
    }
    /* the KV cache, aligned so a GPU can share it in place */
    W.n_ctx = W.m.cfg.n_ctx_train;
    if (W.n_ctx == 0 || W.n_ctx > ZW_CTX) W.n_ctx = ZW_CTX;
    uint64_t sb = zt_model_state_bytes(&W.m, W.n_ctx, ZW_BATCH, ZT_KV_Q16),
             al = zt_accel_state_align();
    uint64_t sl = (sb + al - 1) / al * al;
    if (!sb || sl > SIZE_MAX / 2 || !(W.state_raw = malloc((size_t) (sl + al)))) {
        fail("not enough memory for the KV cache", -1);
        zw_free();
        return -1;
    }
    W.state_mem = (void *) (((uintptr_t) W.state_raw + al - 1) / al * al);
    if ((r = zt_model_state_init(&W.s, &W.m, W.n_ctx, ZW_BATCH, ZT_KV_Q16, W.state_mem, sb)) != 0) {
        fail("could not set up the KV cache", r);
        zw_free();
        return -1;
    }
    W.logits = malloc((size_t) W.m.cfg.n_vocab * sizeof *W.logits);
    W.gen = malloc(W.n_ctx * sizeof *W.gen);
    if (!W.logits || !W.gen) {
        fail("out of memory", -1);
        zw_free();
        return -1;
    }
    zt_choose_in_t in = {buf, len, &W.s, W.state_mem, sl};
    W.eng = zt_choose(&W.m, &in);
    W.im_start = find_tok("<|im_start|>");
    W.im_end = find_tok("<|im_end|>");
    W.eot = find_tok("<|endoftext|>");
    W.loaded = true;
    return 0;
}

/* plain text (no special tokens) onto ids[*n] */
static int32_t push_text(const char *s, uint64_t len, int32_t *ids, uint64_t cap, uint64_t *n)
{
    uint64_t wb = zt_tok_work_bytes(len), got = 0;
    void *work = malloc((size_t) (wb ? wb : 1));
    if (!work) return -1;
    int32_t r =
        zt_tok_encode(&W.tok, (const uint8_t *) s, len, false, ids + *n, cap - *n, &got, work, wb);
    free(work);
    *n += got;
    return r;
}

ZW_EXPORT int32_t zw_start(const char *prompt, uint32_t max_new)
{
    W.active = false;
    W.n_gen = 0;
    W.emitted = 0;
    W.text[0] = W.piece[0] = 0;
    W.stop = "";
    W.digest = 0xcbf29ce484222325ull;
    if (!W.loaded) {
        fail("no model is loaded", -1);
        return -1;
    }
    /* the app's prompt (zxv_model_host.c): BOS when the vocabulary adds it,
     * then the text; the glue (G2) then wraps it in the chat template */
    uint64_t len = strlen(prompt), n = 0, cap = W.n_ctx;
    int32_t *p = W.gen; /* free until the reply starts */
    int32_t r = 0;
    if (len + 64 > cap) {
        fail("the message is too long for the model's context", (int32_t) W.n_ctx);
        return -1;
    }
    if (W.tok.add_bos && W.tok.bos >= 0) p[n++] = W.tok.bos;
    if (W.im_start >= 0 && W.im_end >= 0) {
        p[n++] = W.im_start;
        r |= push_text("user\n", 5, p, cap, &n);
        r |= push_text(prompt, len, p, cap, &n);
        p[n++] = W.im_end;
        r |= push_text("\n", 1, p, cap, &n);
        p[n++] = W.im_start;
        r |= push_text("assistant\n", 10, p, cap, &n);
    } else {
        r |= push_text(prompt, len, p, cap, &n);
    }
    if (r || n == 0 || n >= W.n_ctx) {
        fail("could not build the prompt", r ? r : -1);
        return -1;
    }
    zt_model_state_seek(&W.s, 0);
    r = zt_model_eval(&W.m, &W.s, p, (uint32_t) n, W.logits, 0);
    if (r) {
        fail("the forward pass failed", r);
        return -1;
    }
    W.digest = fnv(W.digest, W.logits, (size_t) W.m.cfg.n_vocab * sizeof *W.logits);
    W.max_new = max_new < W.n_ctx ? max_new : W.n_ctx;
    W.active = true;
    return (int32_t) n;
}

/* How much of s[0..n) to give out now: all of it, less a trailing UTF-8
 * character whose bytes have not all come (the next token brings the rest). */
uint64_t zw_whole_utf8(const char *s, uint64_t n)
{
    uint64_t i = n, k = 0;
    while (i > 0 && k < 4 && ((uint8_t) s[i - 1] & 0xC0u) == 0x80u) i--, k++;
    if (i > 0) {
        uint8_t lead = (uint8_t) s[i - 1];
        uint64_t need = lead >= 0xF0u ? 4 : lead >= 0xE0u ? 3 : lead >= 0xC0u ? 2 : 1;
        if (need > k + 1) return i - 1;
    }
    return n;
}

/* the reply so far into text; how many bytes are whole UTF-8 characters */
static uint64_t decode_reply(bool final)
{
    uint64_t dn = 0;
    int32_t r = zt_tok_decode(&W.tok, W.gen, W.n_gen, (uint8_t *) W.text, sizeof W.text - 1, &dn);
    if (r && r != ZT_TOK_ESPACE) dn = 0;
    W.text[dn] = 0;
    return final ? dn : zw_whole_utf8(W.text, dn);
}

ZW_EXPORT int32_t zw_next(void)
{
    if (!W.active) return -1;
    int32_t t = zt_argmax(W.logits, W.m.cfg.n_vocab);
    if (t < 0 || t == W.tok.eos || t == W.im_end || t == W.eot) {
        W.stop = "eos";
        W.active = false;
        return -1;
    }
    W.gen[W.n_gen++] = t;
    if (W.n_gen >= W.max_new) {
        W.stop = "max";
        W.active = false;
        return t;
    }
    if (W.s.pos >= W.n_ctx) {
        W.stop = "ctx";
        W.active = false;
        return t;
    }
    int32_t r = zt_model_decode(&W.m, &W.s, t, W.logits);
    if (r) {
        fail("the forward pass failed", r);
        W.stop = "error";
        W.active = false;
        return -2;
    }
    W.digest = fnv(W.digest, W.logits, (size_t) W.m.cfg.n_vocab * sizeof *W.logits);
    return t;
}

ZW_EXPORT const char *zw_text(void)
{
    uint64_t upto = decode_reply(!W.active);
    uint64_t n = 0;
    for (uint64_t i = W.emitted; i < upto; i++)
        if (W.text[i]) W.piece[n++] = W.text[i];
    W.piece[n] = 0;
    if (upto > W.emitted) W.emitted = upto;
    return W.piece;
}

ZW_EXPORT const char *zw_stop(void)
{
    return W.stop ? W.stop : "";
}

ZW_EXPORT uint32_t zw_n_gen(void)
{
    return W.n_gen;
}

ZW_EXPORT const char *zw_digest(void)
{
    static char hex[17];
    snprintf(hex, sizeof hex, "%016llx", (unsigned long long) W.digest);
    return hex;
}
