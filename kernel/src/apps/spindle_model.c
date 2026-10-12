/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* spindle_model.c — Spindle, the console (see spindle_model.h). */
#include <string.h>

#include "spindle_model.h"

const zxm_ai_action_t SPINDLE_AI[SPINDLE_AI_COUNT] = {
    {"Explain this reply", "Companion", 500},
};

void spindle_init(spindle_t *s, zxm_link_t *link)
{
    s->link = link;
    s->n_methods = 0;
    s->protocol[0] = 0;
    s->n_hist = 0;
    memset(&s->held, 0, sizeof s->held);
}

int spindle_discover(spindle_t *s)
{
    zxm_link_t *l = s->link;
    zxm_begin(l, "rpc.discover");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "methods");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    static char protocol[sizeof s->protocol];
    if (zxm_get_str(l, l->result, "protocol", protocol, sizeof protocol)) protocol[0] = 0;
    uint32_t n = zxm_len(l, arr), k = 0;
    for (uint32_t i = 0; i < n && k < SPINDLE_METHODS; i++) {
        int32_t t = zxm_at(l, arr, i);
        if (t < 0 || l->tok[t].type != W4_J_STR) return ZXM_E_PROTOCOL;
        if (w4_json_str(l->resp, &l->tok[t], s->methods[k], sizeof s->methods[k]) < 0) continue;
        k++;
    }
    s->n_methods = k;
    memcpy(s->protocol, protocol, sizeof protocol);
    return ZXM_OK;
}

static bool method_ok(const char *m)
{
    size_t n = strlen(m);
    if (!n || n > SPINDLE_METHOD_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        char c = m[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_'))
            return false;
    }
    return true;
}

/* one JSON object and nothing after it */
static bool object_ok(const char *p, uint32_t n)
{
    static w4_jtok_t tok[1024];
    int32_t k = w4_json_parse(p, n, tok, 1024u, W4_JSON_MAX_DEPTH);
    return k > 0 && tok[0].type == W4_J_OBJ;
}

static void keep(spindle_entry_t *e, const char *s, uint32_t n)
{
    e->reply_cut = n > SPINDLE_REPLY_MAX;
    if (e->reply_cut) n = zxm_utf8_cut(s, SPINDLE_REPLY_MAX);
    memcpy(e->reply, s, n);
    e->reply[n] = 0;
    e->reply_len = n;
}

int spindle_call(spindle_t *s, const char *method, const char *params)
{
    size_t pn = strlen(params);
    if (!method_ok(method) || pn > SPINDLE_PARAMS_MAX) return ZXM_E_ARG;
    if (pn && !object_ok(params, (uint32_t) pn)) return ZXM_E_ARG;
    zxm_link_t *l = s->link;
    zxm_begin(l, method);
    if (pn) zxm_p_raw(l, params, (uint32_t) pn);
    int rc = zxm_call(l);
    if (rc == ZXM_E_TOOBIG && l->req_over) return rc; /* never sent */
    spindle_entry_t *e = &s->hist[s->n_hist % SPINDLE_HISTORY];
    memcpy(e->method, method, strlen(method) + 1);
    memcpy(e->params, params, pn + 1);
    e->rc = rc;
    e->err_code = rc == ZXM_E_REMOTE ? l->err_code : 0;
    if (rc == ZXM_OK) {
        const w4_jtok_t *t = &l->tok[l->result];
        uint32_t a = t->start, b = t->end;
        if (t->type == W4_J_STR) { /* keep the quotes: the reply is JSON */
            a--;
            b++;
        }
        keep(e, l->resp + a, b - a);
    } else {
        keep(e, l->err, (uint32_t) strlen(l->err));
    }
    s->n_hist++;
    return rc;
}

uint32_t spindle_history_len(const spindle_t *s)
{
    return s->n_hist < SPINDLE_HISTORY ? s->n_hist : SPINDLE_HISTORY;
}

const spindle_entry_t *spindle_entry(const spindle_t *s, uint32_t i)
{
    if (i >= spindle_history_len(s)) return NULL;
    return &s->hist[(s->n_hist - 1u - i) % SPINDLE_HISTORY];
}

int spindle_ai(spindle_t *s, int32_t action, uint32_t i)
{
    if (action < 0 || action >= SPINDLE_AI_COUNT) return ZXM_E_ARG;
    const spindle_entry_t *e = spindle_entry(s, i);
    if (!e) return ZXM_E_STATE;
    static char in[SPINDLE_METHOD_MAX + SPINDLE_PARAMS_MAX + SPINDLE_REPLY_MAX + 64];
    uint32_t n = 0;
#define ADD(str, len)                                                                              \
    do {                                                                                           \
        memcpy(in + n, (str), (len));                                                              \
        n += (uint32_t) (len);                                                                     \
    } while (0)
    ADD("Method: ", 8);
    ADD(e->method, strlen(e->method));
    ADD("\nParameters: ", 13);
    ADD(e->params[0] ? e->params : "{}", e->params[0] ? strlen(e->params) : 2);
    ADD(e->rc == ZXM_OK ? "\nReply: " : "\nError: ", 8);
    ADD(e->reply, e->reply_len);
#undef ADD
    return zxm_ai_ask(s->link, &SPINDLE_AI[action], action,
                      "Explain in plain words what this zxvd call did and what its reply means. "
                      "Be brief.",
                      in, n, &s->held);
}

int spindle_ai_accept(spindle_t *s)
{
    if (s->held.state != ZXM_HELD_PENDING) return ZXM_E_STATE;
    s->held.state = ZXM_HELD_ACCEPTED;
    return ZXM_OK;
}

int spindle_ai_reject(spindle_t *s)
{
    if (s->held.state != ZXM_HELD_PENDING) return ZXM_E_STATE;
    s->held.state = ZXM_HELD_REJECTED;
    s->held.len = 0;
    s->held.text[0] = 0;
    return ZXM_OK;
}
