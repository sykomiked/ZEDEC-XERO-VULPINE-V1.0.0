/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxm.c — the shared part of the app models (see zxm.h M1-M5). */
#include <string.h>

#include "zxm.h"

void zxm_link_init(zxm_link_t *l, zxm_send_fn send, void *ctx)
{
    memset(l, 0, sizeof *l);
    l->send = send;
    l->ctx = ctx;
    l->result = -1;
}

/* ===================== writing a request ===================== */

static void put(zxm_link_t *l, const char *s, uint32_t n)
{
    if (l->req_over || l->req_len + n > ZXM_REQ_MAX) {
        l->req_over = true;
        return;
    }
    memcpy(l->req + l->req_len, s, n);
    l->req_len += n;
}

static void puts_(zxm_link_t *l, const char *s)
{
    put(l, s, (uint32_t) strlen(s));
}

static void put_u64(zxm_link_t *l, uint64_t v)
{
    char d[21];
    int i = 20;
    d[i] = 0;
    do {
        d[--i] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v);
    puts_(l, d + i);
}

int32_t zxm_escape(const char *s, uint32_t n, char *out, uint32_t cap)
{
    static const char HEX[] = "0123456789abcdef";
    uint32_t o = 0;
    for (uint32_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char) s[i];
        char e[6];
        uint32_t k = 0;
        if (c == '"' || c == '\\') {
            e[k++] = '\\';
            e[k++] = (char) c;
        } else if (c == '\n') {
            e[k++] = '\\';
            e[k++] = 'n';
        } else if (c == '\t') {
            e[k++] = '\\';
            e[k++] = 't';
        } else if (c < 0x20) {
            e[k++] = '\\';
            e[k++] = 'u';
            e[k++] = '0';
            e[k++] = '0';
            e[k++] = HEX[c >> 4];
            e[k++] = HEX[c & 15];
        } else {
            e[k++] = (char) c;
        }
        if (o + k > cap) return -1;
        memcpy(out + o, e, k);
        o += k;
    }
    if (o < cap) out[o] = 0;
    return (int32_t) o;
}

uint32_t zxm_utf8_cut(const char *s, uint32_t n)
{
    uint32_t back = 0;
    while (back < 3 && back < n && ((unsigned char) s[n - 1 - back] & 0xc0) == 0x80) back++;
    if (back == n) return 0;
    unsigned char lead = (unsigned char) s[n - 1 - back];
    uint32_t need = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
    return back + 1 < need ? n - back - 1 : n;
}

static void key(zxm_link_t *l, const char *k)
{
    put(l, l->req_first ? "\"" : ",\"", l->req_first ? 1 : 2);
    l->req_first = false;
    puts_(l, k);
    put(l, "\":", 2);
}

void zxm_begin(zxm_link_t *l, const char *method)
{
    l->req_len = 0;
    l->req_over = false;
    l->req_first = true;
    puts_(l, "{\"jsonrpc\":\"2.0\",\"id\":");
    put_u64(l, ++l->seq);
    puts_(l, ",\"method\":\"");
    puts_(l, method);
    puts_(l, "\",\"params\":{");
}

void zxm_p_strn(zxm_link_t *l, const char *k, const char *val, uint32_t n)
{
    key(l, k);
    put(l, "\"", 1);
    if (!l->req_over) {
        int32_t w = zxm_escape(val, n, l->req + l->req_len, ZXM_REQ_MAX - l->req_len);
        if (w < 0)
            l->req_over = true;
        else
            l->req_len += (uint32_t) w;
    }
    put(l, "\"", 1);
}

void zxm_p_str(zxm_link_t *l, const char *k, const char *val)
{
    zxm_p_strn(l, k, val, (uint32_t) strlen(val));
}

void zxm_p_u64(zxm_link_t *l, const char *k, uint64_t v)
{
    key(l, k);
    put_u64(l, v);
}

void zxm_p_bool(zxm_link_t *l, const char *k, bool v)
{
    key(l, k);
    puts_(l, v ? "true" : "false");
}

void zxm_p_raw(zxm_link_t *l, const char *obj, uint32_t n)
{
    /* replace the "{" that zxm_begin opened with the caller's object */
    if (!l->req_first || n < 2 || obj[0] != '{' || obj[n - 1] != '}') {
        l->req_over = true;
        return;
    }
    l->req_len--;
    put(l, obj, n - 1);
    l->req_first = n == 2;
}

void zxm_p_space(zxm_link_t *l, const char *space)
{
    if (space && space[0] && strcmp(space, "home")) zxm_p_str(l, "space", space);
}

/* ===================== one call ===================== */

static int local(zxm_link_t *l, int code, const char *why)
{
    l->err_code = 0;
    size_t n = strlen(why);
    if (n >= sizeof l->err) n = sizeof l->err - 1;
    memcpy(l->err, why, n);
    l->err[n] = 0;
    return code;
}

int zxm_call(zxm_link_t *l)
{
    l->result = -1;
    l->ntok = 0;
    put(l, "}}", 2);
    if (l->req_over) return local(l, ZXM_E_TOOBIG, "the request is too large");
    l->req[l->req_len] = 0;
    int32_t got = l->send(l->ctx, l->req, l->req_len, l->resp, ZXM_RESP_MAX);
    if (got < 0) return local(l, ZXM_E_TRANSPORT, "no connection to zxvd");
    if ((uint32_t) got > ZXM_RESP_MAX) return local(l, ZXM_E_TOOBIG, "the reply is too large");
    l->resp_len = (uint32_t) got;
    l->resp[got] = 0;
    int32_t n = w4_json_parse(l->resp, l->resp_len, l->tok, ZXM_TOK_MAX, W4_JSON_MAX_DEPTH);
    if (n == W4_ERR_SPACE) return local(l, ZXM_E_TOOBIG, "the reply is too large");
    if (n <= 0 || l->tok[0].type != W4_J_OBJ)
        return local(l, ZXM_E_PROTOCOL, "zxvd's reply is not JSON-RPC");
    l->ntok = n;
    uint64_t id = 0;
    int32_t idt = w4_json_get(l->resp, l->tok, n, 0, "id");
    if (idt < 0 || w4_json_u64(l->resp, &l->tok[idt], &id) != W4_OK || id != l->seq)
        return local(l, ZXM_E_PROTOCOL, "zxvd's reply is for another request");
    int32_t e = w4_json_get(l->resp, l->tok, n, 0, "error");
    if (e >= 0) {
        int64_t code = 0;
        int32_t c = w4_json_get(l->resp, l->tok, n, e, "code");
        if (c < 0 || w4_json_i64(l->resp, &l->tok[c], &code) != W4_OK)
            return local(l, ZXM_E_PROTOCOL, "zxvd's error has no code");
        if (w4_json_get_str(l->resp, l->tok, n, e, "message", l->err, sizeof l->err) < 0)
            l->err[0] = 0;
        l->err_code = (int32_t) code;
        return ZXM_E_REMOTE;
    }
    l->result = w4_json_get(l->resp, l->tok, n, 0, "result");
    if (l->result < 0) return local(l, ZXM_E_PROTOCOL, "zxvd's reply has no result");
    l->err[0] = 0;
    l->err_code = 0;
    return ZXM_OK;
}

/* ===================== reading the result ===================== */

int32_t zxm_get(const zxm_link_t *l, int32_t obj, const char *k)
{
    if (obj < 0 || obj >= l->ntok || l->tok[obj].type != W4_J_OBJ) return W4_ERR_NOTFOUND;
    return w4_json_get(l->resp, l->tok, l->ntok, obj, k);
}

int zxm_get_str(const zxm_link_t *l, int32_t obj, const char *k, char *out, uint32_t cap)
{
    int32_t i = zxm_get(l, obj, k);
    out[0] = 0;
    if (i < 0 || l->tok[i].type != W4_J_STR) return ZXM_E_PROTOCOL;
    return w4_json_str(l->resp, &l->tok[i], out, cap) < 0 ? ZXM_E_TOOBIG : ZXM_OK;
}

int zxm_get_u64(const zxm_link_t *l, int32_t obj, const char *k, uint64_t *v)
{
    int32_t i = zxm_get(l, obj, k);
    return i >= 0 && w4_json_u64(l->resp, &l->tok[i], v) == W4_OK ? ZXM_OK : ZXM_E_PROTOCOL;
}

int zxm_get_i64(const zxm_link_t *l, int32_t obj, const char *k, int64_t *v)
{
    int32_t i = zxm_get(l, obj, k);
    return i >= 0 && w4_json_i64(l->resp, &l->tok[i], v) == W4_OK ? ZXM_OK : ZXM_E_PROTOCOL;
}

int zxm_get_bool(const zxm_link_t *l, int32_t obj, const char *k, bool *v)
{
    int32_t i = zxm_get(l, obj, k);
    return i >= 0 && w4_json_bool(&l->tok[i], v) == W4_OK ? ZXM_OK : ZXM_E_PROTOCOL;
}

int32_t zxm_at(const zxm_link_t *l, int32_t arr, uint32_t i)
{
    if (arr < 0 || arr >= l->ntok || l->tok[arr].type != W4_J_ARR) return W4_ERR_NOTFOUND;
    return w4_json_at(l->tok, l->ntok, arr, i);
}

uint32_t zxm_len(const zxm_link_t *l, int32_t arr)
{
    if (arr < 0 || arr >= l->ntok || l->tok[arr].type != W4_J_ARR) return 0;
    return l->tok[arr].size;
}

/* ===================== AI (M3, M4) ===================== */

int zxm_ai_ask(zxm_link_t *l, const zxm_ai_action_t *act, int32_t idx, const char *instruction,
               const char *input, uint32_t input_len, zxm_held_t *held)
{
    static char prompt[ZXM_ASK_MAX + 1];
    uint32_t il = (uint32_t) strlen(instruction);
    if (il + 2 >= ZXM_ASK_MAX) return ZXM_E_ARG;
    memcpy(prompt, instruction, il);
    prompt[il++] = '\n';
    prompt[il++] = '\n';
    uint32_t room = ZXM_ASK_MAX - il, take = input_len;
    bool cut = false;
    if (take > room) {
        take = zxm_utf8_cut(input, room);
        cut = true;
    }
    memcpy(prompt + il, input, take);
    il += take;
    zxm_begin(l, "swarm.ask");
    zxm_p_strn(l, "prompt", prompt, il);
    int rc = zxm_call(l);
    if (rc) return rc;
    memset(held, 0, sizeof *held);
    int32_t a = zxm_get(l, l->result, "answer");
    if (a < 0 || l->tok[a].type != W4_J_STR)
        return local(l, ZXM_E_PROTOCOL, "swarm.ask gave no answer");
    if (w4_json_str(l->resp, &l->tok[a], held->text, sizeof held->text) < 0) {
        /* an answer too long for the held text is cut, not lost */
        static char whole[ZXM_RESP_MAX + 1];
        int32_t n = w4_json_str(l->resp, &l->tok[a], whole, sizeof whole);
        if (n < 0) return local(l, ZXM_E_TOOBIG, "the answer is too large");
        uint32_t k = zxm_utf8_cut(whole, ZXM_ANSWER_MAX);
        memcpy(held->text, whole, k);
        held->text[k] = 0;
        cut = true;
    }
    held->len = (uint32_t) strlen(held->text);
    held->state = ZXM_HELD_PENDING;
    held->action = idx;
    held->cut = cut;
    size_t al = strlen(act->agent);
    if (al >= sizeof held->cost.agent) al = sizeof held->cost.agent - 1;
    memcpy(held->cost.agent, act->agent, al);
    held->cost.agent[al] = 0;
    uint64_t tokens;
    if (zxm_get_u64(l, l->result, "tokens", &tokens) == ZXM_OK) {
        held->cost.tokens = tokens;
        held->cost.exact = true;
    } else {
        held->cost.tokens = ((uint64_t) il + held->len + 3u) / 4u;
        held->cost.exact = false;
    }
    return ZXM_OK;
}
