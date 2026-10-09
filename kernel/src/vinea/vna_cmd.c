/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_cmd.c — Hackronomicon request lines. See vna_cmd.h. */
#include "vna_cmd.h"
#include "vna_schema.h"
#include "../swarm/swarm_hk.h"

static const char *const VERBS[VNA_V_COUNT] = {"ask",    "offer",  "bid",   "counter",
                                               "accept", "reject", "lease", "fetch"};
static const char *const RES[VNA_RES_COUNT] = {"route",   "record",  "file",
                                               "compute", "storage", "memory"};
static const char *const FORMS[9] = {"financial", "manufactured", "intellectual", "human", "social",
                                     "natural",   "cultural",     "spiritual",    "system"};

static bool span_is(const char *src, uint16_t start, uint16_t len, const char *w)
{
    uint16_t i = 0;
    for (; i < len; i++)
        if (w[i] == 0 || src[start + i] != w[i]) return false;
    return w[i] == 0;
}

static int lookup_word(const char *src, uint16_t start, uint16_t len, const char *const *tab, int n)
{
    for (int i = 0; i < n; i++)
        if (span_is(src, start, len, tab[i])) return i;
    return -1;
}

static bool parse_u64(const char *s, uint16_t len, uint64_t *out)
{
    uint64_t v = 0;
    if (len == 0 || len > 20) return false;
    for (uint16_t i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        uint64_t d = (uint64_t) (s[i] - '0');
        if (v > 1844674407370955161u || (v == 1844674407370955161u && d > 5u))
            return false; /* overflow (UINT64_MAX = 18446744073709551615) */
        v = v * 10u + d;
    }
    if (len > 1 && s[0] == '0') return false; /* one spelling per number */
    *out = v;
    return true;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

vna_status_t vna_cmd_decode(const uint8_t *text, uint32_t len, vna_cmd_t *out)
{
    static swarm_hk_ast_t ast; /* single-threaded scratch */
    char src[SWARM_HK_MAX_TEXT];
    if (!out || !vna_hk_is_canonical(text, len)) return VNA_ERR_HK;
    for (uint32_t i = 0; i < len; i++) src[i] = (char) text[i];
    src[len] = 0;
    if (swarm_hk_parse(src, &ast) != SWARM_HK_OK || ast.root < 0) return VNA_ERR_HK;
    const swarm_hk_node_t *root = &ast.node[ast.root];
    if (root->kind != SWARM_HK_CALL) return VNA_ERR_HK;
    vna_zero(out, sizeof *out);
    int v = lookup_word(ast.src, root->start, root->len, VERBS, VNA_V_COUNT);
    if (v < 0) return VNA_ERR_HK; /* no such verb: e.g. set(...) */
    out->verb = (uint8_t) v;
    int16_t a = root->first_arg;
    if (a < 0 || ast.node[a].kind != SWARM_HK_ATOM) return VNA_ERR_HK;
    int r = lookup_word(ast.src, ast.node[a].start, ast.node[a].len, RES, VNA_RES_COUNT);
    if (r < 0) return VNA_ERR_HK;
    out->resource = (uint8_t) r;
    for (a = ast.node[a].next; a >= 0; a = ast.node[a].next) {
        const swarm_hk_node_t *kv = &ast.node[a];
        if (kv->kind != SWARM_HK_KV || kv->a < 0) return VNA_ERR_HK;
        const swarm_hk_node_t *val = &ast.node[kv->a];
        if (val->kind != SWARM_HK_ATOM) return VNA_ERR_HK;
        const char *vs = ast.src + val->start;
        uint8_t bit;
        uint64_t *dst = 0;
        if (span_is(ast.src, kv->start, kv->len, "units")) {
            bit = VNA_CMD_UNITS;
            dst = &out->units;
        } else if (span_is(ast.src, kv->start, kv->len, "price")) {
            bit = VNA_CMD_PRICE;
            dst = &out->price;
        } else if (span_is(ast.src, kv->start, kv->len, "cycles")) {
            bit = VNA_CMD_CYCLES;
            dst = &out->cycles;
        } else if (span_is(ast.src, kv->start, kv->len, "index")) {
            bit = VNA_CMD_INDEX;
            dst = &out->index;
        } else if (span_is(ast.src, kv->start, kv->len, "form")) {
            bit = VNA_CMD_FORM;
            int f = lookup_word(ast.src, val->start, val->len, FORMS, 9);
            if (f < 0) return VNA_ERR_HK;
            out->form = (uint8_t) f;
        } else if (span_is(ast.src, kv->start, kv->len, "root")) {
            bit = VNA_CMD_ROOT;
            if (val->len != 64) return VNA_ERR_HK;
            for (uint32_t i = 0; i < 32; i++) {
                int hi = hexval(vs[2 * i]), lo = hexval(vs[2 * i + 1]);
                if (hi < 0 || lo < 0) return VNA_ERR_HK;
                out->root.b[i] = (uint8_t) ((hi << 4) | lo);
            }
        } else {
            return VNA_ERR_HK; /* unknown key */
        }
        if (out->has & bit) return VNA_ERR_HK; /* a key may appear once */
        if (dst && !parse_u64(vs, val->len, dst)) return VNA_ERR_HK;
        out->has |= bit;
    }
    return VNA_OK;
}

static uint32_t put_str(char *b, uint32_t pos, uint32_t cap, const char *s)
{
    while (*s && pos + 1 < cap) b[pos++] = *s++;
    return pos;
}

static uint32_t put_u64(char *b, uint32_t pos, uint32_t cap, uint64_t v)
{
    uint64_t p10[20];
    p10[0] = 1;
    for (uint32_t i = 1; i < 20; i++) p10[i] = p10[i - 1] * 10u;
    int32_t top = 0;
    while (top < 19 && p10[top + 1] <= v) top++;
    for (int32_t i = top; i >= 0 && pos + 1 < cap; i--) { /* no 64-bit division */
        uint32_t d = 0;
        while (v >= p10[i]) {
            v -= p10[i];
            d++;
        }
        b[pos++] = (char) ('0' + d);
    }
    return pos;
}

int32_t vna_cmd_encode(const vna_cmd_t *c, uint8_t *out, uint32_t cap)
{
    char b[SWARM_HK_MAX_TEXT];
    static const char HEX[] = "0123456789abcdef";
    uint32_t p = 0, cb = sizeof b;
    if (!c || c->verb >= VNA_V_COUNT || c->resource >= VNA_RES_COUNT) return -1;
    p = put_str(b, p, cb, VERBS[c->verb]);
    p = put_str(b, p, cb, "(");
    p = put_str(b, p, cb, RES[c->resource]);
    if (c->has & VNA_CMD_ROOT) {
        p = put_str(b, p, cb, ", root: ");
        for (uint32_t i = 0; i < 32 && p + 3 < cb; i++) {
            b[p++] = HEX[c->root.b[i] >> 4];
            b[p++] = HEX[c->root.b[i] & 15u];
        }
    }
    if (c->has & VNA_CMD_INDEX) {
        p = put_str(b, p, cb, ", index: ");
        p = put_u64(b, p, cb, c->index);
    }
    if (c->has & VNA_CMD_UNITS) {
        p = put_str(b, p, cb, ", units: ");
        p = put_u64(b, p, cb, c->units);
    }
    if (c->has & VNA_CMD_CYCLES) {
        p = put_str(b, p, cb, ", cycles: ");
        p = put_u64(b, p, cb, c->cycles);
    }
    if (c->has & VNA_CMD_FORM) {
        if (c->form >= 9) return -1;
        p = put_str(b, p, cb, ", form: ");
        p = put_str(b, p, cb, FORMS[c->form]);
    }
    if (c->has & VNA_CMD_PRICE) {
        p = put_str(b, p, cb, ", price: ");
        p = put_u64(b, p, cb, c->price);
    }
    p = put_str(b, p, cb, ")");
    if (p + 1 >= cb) return -1;
    b[p] = 0;
    return vna_hk_canonicalize(b, out, cap);
}

bool vna_cmd_requests_resource(const vna_cmd_t *c)
{
    return c->verb == VNA_V_ASK || c->verb == VNA_V_BID || c->verb == VNA_V_LEASE ||
           c->verb == VNA_V_FETCH || c->verb == VNA_V_ACCEPT;
}
