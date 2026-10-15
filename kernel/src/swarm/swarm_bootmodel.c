/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* swarm_bootmodel.c — run the boot model and print [AI_OK] only when it
 * answered. See swarm_bootmodel.h. */
#include "swarm_bootmodel.h"
#include "../lpres/lpres.h"
#include "../robin_debanks/sha256.h"
#include "../tensor/zt_boot.h" /* zt_boot_fnv */
#include "../tensor/zt_model.h"
#include "../tensor/zt_tok.h"

/* Everything the engine keeps between calls lives here, not on the stack. */
static zt_gguf_t g_gguf;
static zt_model_t g_model;
static zt_tok_t g_tok;
static zt_model_state_t g_state;
static char g_line[512];

/* ---- line building ---- */

typedef struct {
    char *p, *end;
} line_t;

static void put_s(line_t *l, const char *s)
{
    while (*s && l->p < l->end) *l->p++ = *s++;
}

static void put_n(line_t *l, const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n && l->p < l->end; i++) *l->p++ = s[i];
}

static void put_u(line_t *l, uint64_t v)
{
    char d[20];
    uint32_t n = 0;
    do {
        d[n++] = (char) ('0' + (uint32_t) (v % 10u));
        v /= 10u;
    } while (v && n < 20u);
    while (n && l->p < l->end) *l->p++ = d[--n];
}

static void put_hexbytes(line_t *l, const uint8_t *b, uint32_t n)
{
    static const char hx[] = "0123456789abcdef";
    for (uint32_t i = 0; i < n && l->p + 1 < l->end; i++) {
        *l->p++ = hx[b[i] >> 4];
        *l->p++ = hx[b[i] & 15u];
    }
}

static void put_hex32(line_t *l, uint32_t v)
{
    static const char hx[] = "0123456789abcdef";
    put_s(l, "0x");
    for (int k = 7; k >= 0 && l->p < l->end; k--) *l->p++ = hx[(v >> (4 * k)) & 15u];
}

static line_t line_start(void)
{
    line_t l = {g_line, g_line + sizeof g_line - 2};
    return l;
}

static void line_emit(line_t *l, void (*out)(const char *))
{
    *l->p++ = '\n';
    *l->p = 0;
    if (out) out(g_line);
}

static int32_t fault(swarm_bootmodel_out_t *res, const swarm_bootmodel_in_t *in, int32_t step,
                     int32_t engine, const char *why, void (*out)(const char *))
{
    res->step = step;
    res->engine = engine;
    if (res->verdict != LPRES_STATE_BOTH) res->verdict = LPRES_STATE_FALSE;
    line_t l = line_start();
    put_s(&l, "[AI_FAULT] ");
    put_n(&l, in->name, in->name_len);
    put_s(&l, " ");
    put_s(&l, why);
    if (engine) {
        put_s(&l, " (engine ");
        if (engine < 0) {
            put_s(&l, "-");
            put_u(&l, (uint64_t) - (int64_t) engine);
        } else {
            put_u(&l, (uint64_t) engine);
        }
        put_s(&l, ")");
    }
    line_emit(&l, out);
    return step;
}

/* ---- regions ---- */

static int inside(uint64_t a, uint64_t n, uint64_t lo, uint64_t hi)
{
    return n && a >= lo && a <= hi && n <= hi - a;
}

static int overlaps(uint64_t a, uint64_t an, uint64_t b, uint64_t bn)
{
    return a < b + bn && b < a + an;
}

/* ---- scratch: a bump allocator over the caller's region, 64-byte aligned ---- */

typedef struct {
    uint8_t *base;
    uint64_t size, used;
} bump_t;

static uint64_t align64(uint64_t v)
{
    return (v + 63u) & ~(uint64_t) 63u;
}

static void *bump_take(bump_t *b, uint64_t n)
{
    uint64_t at = align64(b->used);
    if (at > b->size || n > b->size - at) return 0;
    b->used = at + n;
    return b->base + at;
}

/* ---- generation callback ---- */

typedef struct {
    swarm_bootmodel_out_t *res;
} gen_ctx_t;

static bool on_token(void *ctx, int32_t id, const uint8_t *piece, uint32_t len)
{
    swarm_bootmodel_out_t *res = ((gen_ctx_t *) ctx)->res;
    res->digest = zt_boot_fnv(res->digest, (uint32_t) id);
    for (uint32_t i = 0; i < len && res->text_len < SWARM_BOOTMODEL_TEXT_MAX; i++) {
        uint8_t c = piece[i];
        res->text[res->text_len++] = (char) ((c < 0x20u || c == 0x7Fu || c == '"') ? ' ' : c);
    }
    return true;
}

int32_t swarm_bootmodel_run(const swarm_bootmodel_in_t *in, swarm_bootmodel_out_t *res,
                            void (*out)(const char *))
{
    uint8_t *r = (uint8_t *) res;
    for (uint32_t i = 0; i < (uint32_t) sizeof *res; i++) r[i] = 0;
    res->digest = ZT_BOOT_FNV_INIT;
    res->verdict = LPRES_STATE_NEITHER;

    /* 1. regions */
    uint64_t ma = (uint64_t) (uintptr_t) in->model, sa = (uint64_t) (uintptr_t) in->scratch;
    if (!in->model || !in->scratch || !in->loader_sha256 ||
        !inside(ma, in->model_bytes, in->map_lo, in->map_hi) ||
        !inside(sa, in->scratch_bytes, in->map_lo, in->map_hi) ||
        overlaps(ma, in->model_bytes, sa, in->scratch_bytes) ||
        overlaps(ma, in->model_bytes, in->kernel_lo, in->kernel_hi - in->kernel_lo) ||
        overlaps(sa, in->scratch_bytes, in->kernel_lo, in->kernel_hi - in->kernel_lo))
        return fault(res, in, SWARM_BM_EREGION, 0,
                     "refused: its memory is outside the kernel's map or overlaps the kernel", out);

    /* 2. the kernel's own witness of the model's bytes */
    sha256_ctx_t c;
    sha256_init(&c);
    for (uint64_t off = 0; off < in->model_bytes;) {
        uint64_t n = in->model_bytes - off;
        if (n > (1u << 20)) n = 1u << 20;
        sha256_update(&c, in->model + off, (size_t) n);
        off += n;
    }
    sha256_final(&c, res->kernel_sha256);
    int same = 1;
    for (uint32_t i = 0; i < 32u; i++)
        if (res->kernel_sha256[i] != in->loader_sha256[i]) same = 0;
    /* The loader handed the model over only after its hash matched the
     * manifest (TRUE). The kernel's hash agreeing is TRUE, disagreeing FALSE;
     * their knowledge join is TRUE or BOTH. */
    res->verdict =
        (uint8_t) lpres_kjoin(LPRES_STATE_TRUE, same ? LPRES_STATE_TRUE : LPRES_STATE_FALSE);
    line_t l = line_start();
    if (res->verdict != LPRES_STATE_TRUE) {
        put_s(&l, "[AI_FAULT] ");
        put_n(&l, in->name, in->name_len);
        put_s(&l, " refused: kernel sha256 ");
        put_hexbytes(&l, res->kernel_sha256, 8);
        put_s(&l, " disagrees with the loader's ");
        put_hexbytes(&l, in->loader_sha256, 8);
        line_emit(&l, out);
        res->step = SWARM_BM_EDIGEST;
        return res->step;
    }
    put_s(&l, "[AI_MODEL] ");
    put_n(&l, in->name, in->name_len);
    put_s(&l, " ");
    put_u(&l, in->model_bytes);
    put_s(&l, " bytes, kernel sha256 ");
    put_hexbytes(&l, res->kernel_sha256, 8);
    put_s(&l, " agrees with the loader");
    line_emit(&l, out);

    /* 3. open, size everything, then load */
    int32_t e = zt_gguf_open(&g_gguf, in->model, in->model_bytes);
    if (e) return fault(res, in, SWARM_BM_EGGUF, e, "is not a GGUF file this engine reads", out);
    uint64_t ab = zt_model_arena_bytes(&g_gguf);
    if (!ab) {
        zt_model_cfg_t cfg;
        zt_model_err_t err;
        e = zt_model_config(&g_gguf, &cfg, &err);
        return fault(res, in, SWARM_BM_EMODEL, e ? e : ZT_MODEL_ECONFIG,
                     "is not a model this engine runs", out);
    }
    uint64_t tb = zt_tok_arena_bytes(&g_gguf);
    if (!tb) return fault(res, in, SWARM_BM_ETOK, 0, "has no tokenizer this engine reads", out);

    bump_t b = {in->scratch, in->scratch_bytes, 0};
    void *arena = bump_take(&b, ab);
    void *tarena = bump_take(&b, tb);
    if (!arena || !tarena) {
        res->need_bytes = align64(ab) + align64(tb);
        return fault(res, in, SWARM_BM_ESCRATCH, 0, "refused: the scratch is too small", out);
    }
    zt_model_err_t err;
    e = zt_model_load(&g_model, &g_gguf, arena, ab, &err);
    if (e) return fault(res, in, SWARM_BM_EMODEL, e, "is not a model this engine runs", out);
    e = zt_tok_load(&g_tok, &g_gguf, tarena, tb);
    if (e) return fault(res, in, SWARM_BM_ETOK, e, "tokenizer failed to load", out);
    if (g_tok.n_vocab != g_model.cfg.n_vocab)
        return fault(res, in, SWARM_BM_ETOK, 0, "tokenizer and model disagree on the vocabulary",
                     out);

    const uint8_t *prompt = (const uint8_t *) SWARM_BOOTMODEL_PROMPT;
    uint64_t plen = sizeof(SWARM_BOOTMODEL_PROMPT) - 1u;
    uint64_t sb =
        zt_model_state_bytes(&g_model, SWARM_BOOTMODEL_N_CTX, SWARM_BOOTMODEL_N_BATCH, ZT_KV_Q16);
    uint64_t wb = zt_model_gen_work_bytes(&g_model, &g_tok, plen, SWARM_BOOTMODEL_MAX_TOKENS);
    res->need_bytes = align64(ab) + align64(tb) + align64(sb) + align64(wb);
    void *smem = sb ? bump_take(&b, sb) : 0;
    void *work = wb ? bump_take(&b, wb) : 0;
    if (!smem || !work)
        return fault(res, in, SWARM_BM_ESCRATCH, 0, "refused: the scratch is too small", out);
    e = zt_model_state_init(&g_state, &g_model, SWARM_BOOTMODEL_N_CTX, SWARM_BOOTMODEL_N_BATCH,
                            ZT_KV_Q16, smem, sb);
    if (e) return fault(res, in, SWARM_BM_EMODEL, e, "state did not initialise", out);

    /* 4. answer */
    gen_ctx_t gc = {res};
    zt_gen_params_t gp;
    uint8_t *q = (uint8_t *) &gp;
    for (uint32_t i = 0; i < (uint32_t) sizeof gp; i++) q[i] = 0;
    gp.sampler = 0; /* greedy: the same answer on every machine */
    gp.max_tokens = SWARM_BOOTMODEL_MAX_TOKENS;
    gp.parse_special = true;
    gp.on_token = on_token;
    gp.ctx = &gc;
    zt_gen_result_t gr;
    e = zt_model_generate(&g_model, &g_state, &g_tok, prompt, plen, &gp, work, wb, &gr);
    if (e) return fault(res, in, SWARM_BM_EGEN, e, "generation failed", out);
    res->n_prompt = gr.n_prompt;
    res->n_gen = gr.n_gen;
    if (gr.n_gen == 0)
        return fault(res, in, SWARM_BM_EGEN, 0, "gave no answer (an end token came first)", out);

    l = line_start();
    put_s(&l, "[AI_OK] ");
    put_n(&l, in->name, in->name_len);
    put_s(&l, " answered \"");
    put_n(&l, res->text, res->text_len);
    put_s(&l, "\" (");
    put_u(&l, res->n_prompt);
    put_s(&l, " prompt + ");
    put_u(&l, res->n_gen);
    put_s(&l, " tokens, digest ");
    put_hex32(&l, res->digest);
    put_s(&l, ")");
    line_emit(&l, out);
    res->step = SWARM_BM_OK;
    return SWARM_BM_OK;
}

void swarm_bootmodel_none(const char *why, void (*out)(const char *))
{
    line_t l = line_start();
    put_s(&l, "[AI_NO_MODEL] ");
    put_s(&l, why);
    put_s(&l, "; the AI is not running");
    line_emit(&l, out);
}
