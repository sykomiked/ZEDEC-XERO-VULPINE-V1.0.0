/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* prov_adapter.c — envelope codec and request/response shape adapters (see
 * prov_adapter.h). JSON goes through kernel/src/web4's tokenizer and writer. */
#include "prov_adapter.h"
#include "../web4/web4_web2.h"

prov_str_t prov_s(const char *z)
{
    prov_str_t s;
    s.p = z;
    s.n = z ? (uint32_t) prov_strnlen(z, PROV_ENV_STR_MAX) : 0u;
    return s;
}

/* ===== Envelope codec ===== */

typedef struct {
    uint8_t *b;
    uint32_t cap, len;
    bool err;
} wbuf_t;

static void wput(wbuf_t *w, const void *p, uint32_t n)
{
    if (w->err || n > w->cap - w->len) {
        w->err = true;
        return;
    }
    prov_memcpy(w->b + w->len, p, n);
    w->len += n;
}

static void wu8(wbuf_t *w, uint8_t v)
{
    wput(w, &v, 1);
}

static void wu32(wbuf_t *w, uint32_t v)
{
    uint8_t b[4];
    prov_le32_put(b, v);
    wput(w, b, 4);
}

static void wstr(wbuf_t *w, prov_str_t s)
{
    if (s.n > PROV_ENV_STR_MAX || (s.n && !s.p)) {
        w->err = true;
        return;
    }
    wu32(w, s.n);
    if (s.n) wput(w, s.p, s.n);
}

int32_t prov_env_encode(const prov_env_t *e, uint8_t *out, uint32_t cap)
{
    if (!e || !out) return PROV_ERR_ARG;
    if (e->shape == PROV_SHAPE_NONE || e->shape >= PROV_SHAPE_COUNT || (e->flags & ~0x03u) ||
        e->n_msgs > PROV_ENV_MAX_MSGS || e->n_inputs > PROV_ENV_MAX_INPUTS)
        return PROV_ERR_ARG;
    for (uint32_t i = 0; i < e->n_msgs; i++)
        if (e->msgs[i].role > PROV_ROLE_SYSTEM) return PROV_ERR_ARG;
    wbuf_t w = {out, cap, 0, false};
    wput(&w, "ZXPE", 4);
    wu8(&w, 1);
    wu8(&w, e->shape);
    wu8(&w, e->flags);
    wu8(&w, e->n_msgs);
    wu8(&w, e->n_inputs);
    wu32(&w, e->max_tokens);
    wput(&w, e->job_id, PROV_HASH_LEN);
    wstr(&w, e->model);
    wstr(&w, e->system);
    wstr(&w, e->prompt);
    for (uint32_t i = 0; i < e->n_msgs; i++) {
        wu8(&w, e->msgs[i].role);
        wstr(&w, e->msgs[i].text);
    }
    for (uint32_t i = 0; i < e->n_inputs; i++) wstr(&w, e->inputs[i]);
    if (w.err) return PROV_ERR_SPACE;
    return (int32_t) w.len;
}

typedef struct {
    const uint8_t *b;
    uint32_t len, at;
    bool err;
} rbuf_t;

static uint8_t ru8(rbuf_t *r)
{
    if (r->err || r->at >= r->len) {
        r->err = true;
        return 0;
    }
    return r->b[r->at++];
}

static uint32_t ru32(rbuf_t *r)
{
    if (r->err || r->len - r->at < 4) {
        r->err = true;
        return 0;
    }
    uint32_t v = prov_le32_get(r->b + r->at);
    r->at += 4;
    return v;
}

static prov_str_t rstr(rbuf_t *r)
{
    prov_str_t s = {0, 0};
    uint32_t n = ru32(r);
    if (r->err || n > PROV_ENV_STR_MAX || n > r->len - r->at) {
        r->err = true;
        return s;
    }
    s.p = (const char *) (r->b + r->at);
    s.n = n;
    r->at += n;
    return s;
}

int prov_env_decode(const uint8_t *in, uint32_t len, prov_env_t *e)
{
    if (!in || !e) return PROV_ERR_ARG;
    prov_memset(e, 0, sizeof *e);
    rbuf_t r = {in, len, 0, false};
    if (len < 4 || !prov_memeq(in, "ZXPE", 4)) return PROV_ERR_PARSE;
    r.at = 4;
    if (ru8(&r) != 1) return PROV_ERR_PARSE;
    e->shape = ru8(&r);
    e->flags = ru8(&r);
    e->n_msgs = ru8(&r);
    e->n_inputs = ru8(&r);
    e->max_tokens = ru32(&r);
    if (r.err || e->shape == PROV_SHAPE_NONE || e->shape >= PROV_SHAPE_COUNT ||
        (e->flags & ~0x03u) || e->n_msgs > PROV_ENV_MAX_MSGS || e->n_inputs > PROV_ENV_MAX_INPUTS ||
        len - r.at < PROV_HASH_LEN)
        return PROV_ERR_PARSE;
    prov_memcpy(e->job_id, in + r.at, PROV_HASH_LEN);
    r.at += PROV_HASH_LEN;
    e->model = rstr(&r);
    e->system = rstr(&r);
    e->prompt = rstr(&r);
    for (uint32_t i = 0; i < e->n_msgs; i++) {
        e->msgs[i].role = ru8(&r);
        if (e->msgs[i].role > PROV_ROLE_SYSTEM) return PROV_ERR_PARSE;
        e->msgs[i].text = rstr(&r);
    }
    for (uint32_t i = 0; i < e->n_inputs; i++) e->inputs[i] = rstr(&r);
    if (r.err || r.at != len) return PROV_ERR_PARSE;
    return PROV_OK;
}

int prov_env_hash(const prov_env_t *e, uint8_t *scratch, uint32_t cap, uint8_t out[PROV_HASH_LEN])
{
    int32_t n = prov_env_encode(e, scratch, cap);
    if (n < 0) return n;
    prov_sha3(scratch, (size_t) n, out);
    return PROV_OK;
}

/* ===== Request bodies ===== */

static const char *role_name(uint8_t r)
{
    return r == PROV_ROLE_ASSISTANT ? "assistant" : r == PROV_ROLE_SYSTEM ? "system" : "user";
}

static void jw_model(w4_jw *j, const prov_api_t *api, const prov_env_t *e)
{
    w4_jw_key(j, "model");
    if (e->model.n)
        w4_jw_strn(j, e->model.p, e->model.n);
    else
        w4_jw_str(j, api->model);
}

static void jw_msgs(w4_jw *j, const prov_env_t *e, bool system_inline)
{
    w4_jw_key(j, "messages");
    w4_jw_arr(j);
    if (system_inline && e->system.n) {
        w4_jw_obj(j);
        w4_jw_key(j, "role");
        w4_jw_str(j, "system");
        w4_jw_key(j, "content");
        w4_jw_strn(j, e->system.p, e->system.n);
        w4_jw_obj_end(j);
    }
    for (uint32_t i = 0; i < e->n_msgs; i++) {
        w4_jw_obj(j);
        w4_jw_key(j, "role");
        w4_jw_str(j, role_name(e->msgs[i].role));
        w4_jw_key(j, "content");
        w4_jw_strn(j, e->msgs[i].text.p, e->msgs[i].text.n);
        w4_jw_obj_end(j);
    }
    w4_jw_arr_end(j);
}

int32_t prov_adapter_body(const prov_api_t *api, const prov_env_t *e, char *out, uint32_t cap)
{
    if (!api || !e || !out || cap == 0) return PROV_ERR_ARG;
    if (api->shape != e->shape) return PROV_ERR_UNSUPPORTED;
    if (e->shape == PROV_SHAPE_RAW) {
        if (e->prompt.n > cap) return PROV_ERR_SPACE;
        prov_memcpy(out, e->prompt.p, e->prompt.n);
        return (int32_t) e->prompt.n;
    }
    w4_jw j;
    w4_jw_init(&j, out, cap);
    w4_jw_obj(&j);
    switch (e->shape) {
    case PROV_SHAPE_MESSAGES:
        jw_model(&j, api, e);
        w4_jw_key(&j, "max_tokens");
        w4_jw_u64(&j, e->max_tokens);
        if (e->system.n) {
            w4_jw_key(&j, "system");
            w4_jw_strn(&j, e->system.p, e->system.n);
        }
        jw_msgs(&j, e, false);
        break;
    case PROV_SHAPE_CHAT:
        jw_model(&j, api, e);
        jw_msgs(&j, e, true);
        w4_jw_key(&j, "max_tokens");
        w4_jw_u64(&j, e->max_tokens);
        break;
    case PROV_SHAPE_COMPLETION:
        jw_model(&j, api, e);
        w4_jw_key(&j, "prompt");
        w4_jw_strn(&j, e->prompt.p, e->prompt.n);
        w4_jw_key(&j, "max_tokens");
        w4_jw_u64(&j, e->max_tokens);
        break;
    case PROV_SHAPE_EMBEDDINGS:
        jw_model(&j, api, e);
        w4_jw_key(&j, "input");
        w4_jw_arr(&j);
        for (uint32_t i = 0; i < e->n_inputs; i++) w4_jw_strn(&j, e->inputs[i].p, e->inputs[i].n);
        w4_jw_arr_end(&j);
        break;
    default:
        return PROV_ERR_UNSUPPORTED;
    }
    w4_jw_obj_end(&j);
    int32_t n = w4_jw_finish(&j);
    return n < 0 ? PROV_ERR_SPACE : n;
}

int32_t prov_adapter_privacy_header(const prov_api_t *api, const prov_env_t *e, char *out,
                                    uint32_t cap)
{
    if (!api || !e || !out || cap == 0) return PROV_ERR_ARG;
    out[0] = 0;
    uint32_t hl = (uint32_t) prov_strnlen(api->privacy_header, PROV_KEYPATH_MAX);
    if (!(e->flags & PROV_EF_NO_TRAIN) || hl == 0) return 0;
    for (uint32_t i = 0; i < hl; i++) {
        char c = api->privacy_header[i]; /* RFC 9110 token characters only */
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                  c == '-' || c == '_';
        if (!ok) return PROV_ERR_ARG;
    }
    if (hl + 6 > cap) return PROV_ERR_SPACE;
    prov_memcpy(out, api->privacy_header, hl);
    prov_memcpy(out + hl, ": 1\r\n", 5);
    out[hl + 5] = 0;
    return (int32_t) (hl + 5);
}

/* ===== Responses ===== */

#define PROV_JTOK_MAX 512u

/* Walk "a.b.0.c" from the root; returns the token index or negative. */
static int32_t walk(const char *js, const w4_jtok_t *t, int32_t n, const char *path)
{
    int32_t cur = 0;
    char seg[PROV_KEYPATH_MAX];
    uint32_t i = 0, plen = (uint32_t) prov_strnlen(path, PROV_KEYPATH_MAX);
    if (plen == 0 || plen >= PROV_KEYPATH_MAX) return W4_ERR_NOTFOUND;
    while (i <= plen) {
        uint32_t k = 0;
        while (i < plen && path[i] != '.') seg[k++] = path[i++];
        seg[k] = 0;
        i++; /* skip '.' (or end) */
        if (k == 0) return W4_ERR_PARSE;
        if (t[cur].type == W4_J_ARR) {
            uint32_t idx = 0;
            for (uint32_t q = 0; q < k; q++) {
                if (seg[q] < '0' || seg[q] > '9' || idx > 100000u) return W4_ERR_NOTFOUND;
                idx = idx * 10u + (uint32_t) (seg[q] - '0');
            }
            cur = w4_json_at(t, n, cur, idx);
        } else if (t[cur].type == W4_J_OBJ) {
            cur = w4_json_get(js, t, n, cur, seg);
        } else {
            return W4_ERR_NOTFOUND;
        }
        if (cur < 0) return cur;
    }
    return cur;
}

int prov_adapter_parse(const prov_api_t *api, const char *json, uint32_t len, prov_resp_t *r)
{
    static w4_jtok_t tok[PROV_JTOK_MAX]; /* not reentrant: one parse at a time */
    if (!api || !json || !r) return PROV_ERR_ARG;
    prov_memset(r, 0, sizeof *r);
    int32_t n = w4_json_parse(json, len, tok, PROV_JTOK_MAX, W4_JSON_MAX_DEPTH);
    if (n <= 0) return PROV_ERR_PARSE;
    int32_t i;
    const char *paths[3] = {api->usage_in, api->usage_out, api->text_path};
    for (uint32_t k = 0; k < 3; k++)
        if (paths[k][0] && walk(json, tok, n, paths[k]) == W4_ERR_PARSE) return PROV_ERR_PARSE;
    if (api->usage_in[0] && (i = walk(json, tok, n, api->usage_in)) >= 0) {
        if (w4_json_u64(json, &tok[i], &r->units_in) != W4_OK) return PROV_ERR_PARSE;
        r->has_in = true;
    }
    if (api->usage_out[0] && (i = walk(json, tok, n, api->usage_out)) >= 0) {
        if (w4_json_u64(json, &tok[i], &r->units_out) != W4_OK) return PROV_ERR_PARSE;
        r->has_out = true;
    }
    if (api->text_path[0] && (i = walk(json, tok, n, api->text_path)) >= 0 &&
        tok[i].type == W4_J_STR) {
        int32_t sl = w4_json_str(json, &tok[i], r->text, sizeof r->text);
        if (sl == W4_ERR_SPACE) {
            /* Keep a raw (unescaped) prefix; mark it truncated. */
            uint32_t m = tok[i].end - tok[i].start;
            if (m > sizeof r->text - 1) m = sizeof r->text - 1;
            prov_memcpy(r->text, json + tok[i].start, m);
            r->text[m] = 0;
            r->text_truncated = true;
        } else if (sl < 0) {
            return PROV_ERR_PARSE;
        }
        r->has_text = true;
    }
    return PROV_OK;
}

int prov_adapter_units(const prov_resp_t *r, uint64_t *units)
{
    if (!r || !units) return PROV_ERR_ARG;
    if (!r->has_in && !r->has_out) return PROV_ERR_NOT_FOUND;
    uint64_t s = r->units_in + r->units_out;
    if (s < r->units_in) return PROV_ERR_OVERFLOW;
    *units = s;
    return PROV_OK;
}
