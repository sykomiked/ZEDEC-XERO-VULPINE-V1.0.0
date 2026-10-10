/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cn_iso8583.c — ISO 8583:1987 subset codec. See cn_iso8583.h. */
#include "cn_iso8583.h"
#include "cn_util.h"
#include "../mlkem/keccak.h"

typedef enum { T_N = 1, T_AN = 2, T_ANS = 3 } ftype_t;
typedef enum { L_FIXED = 0, L_LL = 2, L_LLL = 3 } flen_t;

typedef struct {
    uint8_t field;
    uint8_t type;
    uint8_t lenkind;
    uint16_t max; /* exact length for L_FIXED */
} fspec_t;

static const fspec_t specs[] = {
    {2, T_N, L_LL, 19},      {3, T_N, L_FIXED, 6},    {4, T_N, L_FIXED, 12},
    {7, T_N, L_FIXED, 10},   {11, T_N, L_FIXED, 6},   {12, T_N, L_FIXED, 6},
    {13, T_N, L_FIXED, 4},   {37, T_AN, L_FIXED, 12}, {38, T_AN, L_FIXED, 6},
    {39, T_AN, L_FIXED, 2},  {41, T_ANS, L_FIXED, 8}, {42, T_ANS, L_FIXED, 15},
    {49, T_N, L_FIXED, 3},   {60, T_ANS, L_LLL, 999}, {61, T_ANS, L_LLL, 999},
    {63, T_ANS, L_LLL, 999}, {90, T_N, L_FIXED, 42},
};
#define NSPECS ((uint32_t) (sizeof(specs) / sizeof(specs[0])))

static const fspec_t *spec_for(uint32_t field)
{
    for (uint32_t i = 0; i < NSPECS; i++)
        if (specs[i].field == field) return &specs[i];
    return 0;
}

bool cn8583_mti_supported(uint32_t mti)
{
    switch (mti) {
    case 100:
    case 110:
    case 200:
    case 210:
    case 400:
    case 410:
        return true;
    default:
        return false;
    }
}

static void bit_set(uint8_t *bm, uint32_t f)
{
    bm[(f - 1) / 8] |= (uint8_t) (0x80u >> ((f - 1) % 8));
}

static void bit_clear(uint8_t *bm, uint32_t f)
{
    bm[(f - 1) / 8] &= (uint8_t) ~(0x80u >> ((f - 1) % 8));
}

static bool bit_get(const uint8_t *bm, uint32_t f)
{
    return (bm[(f - 1) / 8] & (0x80u >> ((f - 1) % 8))) != 0;
}

cn8583_rc_t cn8583_init(cn8583_msg_t *m, uint32_t mti)
{
    if (!m) return CN8583_ERR_ARG;
    if (!cn8583_mti_supported(mti)) return CN8583_ERR_MTI;
    cn_zero(m->bitmap, 16);
    cn_zero(m->off, (uint32_t) sizeof(m->off));
    cn_zero(m->len, (uint32_t) sizeof(m->len));
    m->used = 0;
    m->mti = mti;
    return CN8583_OK;
}

bool cn8583_has(const cn8583_msg_t *m, uint32_t field)
{
    if (!m || field < 2 || field > 128) return false;
    return bit_get(m->bitmap, field);
}

static bool char_ok(uint8_t type, char c)
{
    if (type == T_N) return cn_is_digit(c);
    if (type == T_AN)
        return cn_is_digit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == ' ';
    return c >= 0x20 && c <= 0x7E;
}

static cn8583_rc_t check_value(const fspec_t *s, const char *data, uint32_t len)
{
    if (s->lenkind == L_FIXED ? len != s->max : (len > s->max)) return CN8583_ERR_FORMAT;
    if (s->lenkind != L_FIXED && len == 0) return CN8583_ERR_FORMAT;
    for (uint32_t i = 0; i < len; i++)
        if (!char_ok(s->type, data[i])) return CN8583_ERR_FORMAT;
    return CN8583_OK;
}

cn8583_rc_t cn8583_set(cn8583_msg_t *m, uint32_t field, const char *data, uint32_t len)
{
    if (!m || !data) return CN8583_ERR_ARG;
    const fspec_t *s = spec_for(field);
    if (!s) return CN8583_ERR_FIELD;
    cn8583_rc_t rc = check_value(s, data, len);
    if (rc != CN8583_OK) return rc;
    if (len > CN8583_POOL - m->used) return CN8583_ERR_SPACE;
    cn_copy(m->pool + m->used, data, len);
    m->off[field] = (uint16_t) m->used;
    m->len[field] = (uint16_t) len;
    m->used += len;
    bit_set(m->bitmap, field);
    if (field > 64) bit_set(m->bitmap, 1);
    return CN8583_OK;
}

cn8583_rc_t cn8583_set_num(cn8583_msg_t *m, uint32_t field, uint64_t v)
{
    const fspec_t *s = spec_for(field);
    char buf[20];
    if (!s) return CN8583_ERR_FIELD;
    if (s->type != T_N || s->lenkind != L_FIXED || s->max > 19) return CN8583_ERR_FORMAT;
    if (!cn_u64_to_dec_fixed(v, s->max, buf)) return CN8583_ERR_FORMAT;
    return cn8583_set(m, field, buf, s->max);
}

cn8583_rc_t cn8583_get(const cn8583_msg_t *m, uint32_t field, const char **data, uint32_t *len)
{
    if (!m || !data || !len) return CN8583_ERR_ARG;
    if (!cn8583_has(m, field)) return CN8583_ERR_MISSING;
    *data = (const char *) m->pool + m->off[field];
    *len = m->len[field];
    return CN8583_OK;
}

cn8583_rc_t cn8583_get_num(const cn8583_msg_t *m, uint32_t field, uint64_t *v)
{
    const char *d;
    uint32_t n;
    cn8583_rc_t rc = cn8583_get(m, field, &d, &n);
    if (rc != CN8583_OK) return rc;
    if (!v || !cn_dec_to_u64(d, n, v)) return CN8583_ERR_FORMAT;
    return CN8583_OK;
}

cn8583_rc_t cn8583_check_mandatory(const cn8583_msg_t *m)
{
    static const uint8_t req_req[] = {2, 3, 4, 7, 11, 41, 42, 49, 0};
    static const uint8_t req_rsp[] = {3, 4, 11, 39, 41, 0};
    static const uint8_t req_rev[] = {2, 3, 4, 11, 90, 0};
    static const uint8_t req_rvr[] = {3, 4, 11, 39, 90, 0};
    const uint8_t *list;
    if (!m) return CN8583_ERR_ARG;
    switch (m->mti) {
    case 100:
    case 200:
        list = req_req;
        break;
    case 110:
    case 210:
        list = req_rsp;
        break;
    case 400:
        list = req_rev;
        break;
    case 410:
        list = req_rvr;
        break;
    default:
        return CN8583_ERR_MTI;
    }
    for (uint32_t i = 0; list[i]; i++)
        if (!cn8583_has(m, list[i])) return CN8583_ERR_MISSING;
    return CN8583_OK;
}

cn8583_rc_t cn8583_pack(const cn8583_msg_t *m, uint8_t *out, uint32_t cap, uint32_t *out_len)
{
    if (!m || !out || !out_len) return CN8583_ERR_ARG;
    if (!cn8583_mti_supported(m->mti)) return CN8583_ERR_MTI;
    bool secondary = false;
    for (uint32_t f = 65; f <= 128; f++)
        if (bit_get(m->bitmap, f)) secondary = true;
    uint8_t bm[16];
    cn_copy(bm, m->bitmap, 16);
    if (secondary)
        bit_set(bm, 1);
    else
        bit_clear(bm, 1);
    uint32_t bmlen = secondary ? 16u : 8u;
    uint32_t n = 0;
    if (cap < 4u + bmlen) return CN8583_ERR_SPACE;
    if (!cn_u64_to_dec_fixed(m->mti, 4, (char *) out)) return CN8583_ERR_MTI;
    n = 4;
    cn_copy(out + n, bm, bmlen);
    n += bmlen;
    for (uint32_t f = 2; f <= 128; f++) {
        if (!bit_get(bm, f)) continue;
        const fspec_t *s = spec_for(f);
        if (!s) return CN8583_ERR_FIELD;
        uint32_t len = m->len[f];
        if (check_value(s, (const char *) m->pool + m->off[f], len) != CN8583_OK)
            return CN8583_ERR_FORMAT;
        if (s->lenkind != L_FIXED) {
            if (cap - n < s->lenkind) return CN8583_ERR_SPACE;
            if (!cn_u64_to_dec_fixed(len, s->lenkind, (char *) out + n)) return CN8583_ERR_FORMAT;
            n += s->lenkind;
        }
        if (cap - n < len) return CN8583_ERR_SPACE;
        cn_copy(out + n, m->pool + m->off[f], len);
        n += len;
    }
    *out_len = n;
    return CN8583_OK;
}

cn8583_rc_t cn8583_unpack(cn8583_msg_t *m, const uint8_t *in, uint32_t len)
{
    if (!m || !in) return CN8583_ERR_ARG;
    if (len < 12) return CN8583_ERR_TRUNC;
    uint64_t mti;
    if (!cn_dec_to_u64((const char *) in, 4, &mti)) return CN8583_ERR_MTI;
    cn8583_rc_t rc = cn8583_init(m, (uint32_t) mti);
    if (rc != CN8583_OK) return rc;
    uint32_t p = 4;
    cn_copy(m->bitmap, in + p, 8);
    p += 8;
    if (bit_get(m->bitmap, 1)) {
        if (len - p < 8) return CN8583_ERR_TRUNC;
        cn_copy(m->bitmap + 8, in + p, 8);
        p += 8;
    }
    uint8_t bm[16];
    cn_copy(bm, m->bitmap, 16);
    cn_zero(m->bitmap, 16);
    for (uint32_t f = 2; f <= 128; f++) {
        if (!bit_get(bm, f)) continue;
        const fspec_t *s = spec_for(f);
        if (!s) return CN8583_ERR_FIELD;
        uint32_t flen = s->max;
        if (s->lenkind != L_FIXED) {
            uint64_t v;
            if (len - p < s->lenkind) return CN8583_ERR_TRUNC;
            if (!cn_dec_to_u64((const char *) in + p, s->lenkind, &v)) return CN8583_ERR_FORMAT;
            p += s->lenkind;
            flen = (uint32_t) v;
        }
        if (len - p < flen) return CN8583_ERR_TRUNC;
        rc = cn8583_set(m, f, (const char *) in + p, flen);
        if (rc != CN8583_OK) return rc;
        p += flen;
    }
    if (p != len) return CN8583_ERR_FORMAT; /* trailing bytes */
    if (bit_get(bm, 1)) bit_set(m->bitmap, 1);
    return CN8583_OK;
}

/* ---- VSS metadata ---- */

typedef struct {
    char *buf;
    uint32_t cap, len;
    bool err;
} tw_t;

static void tw_str(tw_t *w, const char *s)
{
    for (uint32_t i = 0; s[i]; i++) {
        if (w->len >= w->cap) {
            w->err = true;
            return;
        }
        w->buf[w->len++] = s[i];
    }
}

/* Minimal-width decimal (no division). */
static void tw_dec(tw_t *w, uint64_t v)
{
    char tmp[20];
    uint32_t width = 1;
    while (width < 20 && v >= cn_pow10_tab[width]) width++;
    if (!cn_u64_to_dec_fixed(v, width, tmp)) {
        w->err = true;
        return;
    }
    for (uint32_t i = 0; i < width; i++) {
        if (w->len >= w->cap) {
            w->err = true;
            return;
        }
        w->buf[w->len++] = tmp[i];
    }
}

static const char net_letter[CN_NET_COUNT] = {'D', 'P', 'T'};
static const char hexd[17] = "0123456789abcdef";

cn8583_rc_t cn8583_put_vss(cn8583_msg_t *m, const cn_vss_meta_t *v)
{
    char buf[96];
    char nl[2] = {0, 0};
    if (!m || !v) return CN8583_ERR_ARG;
    if (v->network >= CN_NET_COUNT || v->form >= CN_FORM_COUNT || v->rail_dr > 999 ||
        v->rail_cr > 999 || v->rail_eq > 999 || v->receipt > 999999999u || v->atc > 0xFFFFu)
        return CN8583_ERR_ARG;
    nl[0] = net_letter[v->network];
    tw_t w = {buf, sizeof(buf), 0, false};
    tw_str(&w, "VSS1;NET=");
    tw_str(&w, nl);
    tw_str(&w, ";FORM=");
    tw_dec(&w, v->form);
    tw_str(&w, ";DR=");
    tw_dec(&w, v->rail_dr);
    tw_str(&w, ";CR=");
    tw_dec(&w, v->rail_cr);
    tw_str(&w, ";EQ=");
    tw_dec(&w, v->rail_eq);
    if (w.err) return CN8583_ERR_SPACE;
    cn8583_rc_t rc = cn8583_set(m, 60, buf, w.len);
    if (rc != CN8583_OK) return rc;

    w.len = 0;
    tw_str(&w, "RCPT=");
    tw_dec(&w, v->receipt);
    tw_str(&w, ";ATC=");
    tw_dec(&w, v->atc);
    if (w.err) return CN8583_ERR_SPACE;
    rc = cn8583_set(m, 61, buf, w.len);
    if (rc != CN8583_OK) return rc;

    w.len = 0;
    tw_str(&w, "CRY=");
    for (uint32_t i = 0; i < 32 && w.len + 2 <= w.cap; i++) {
        w.buf[w.len++] = hexd[v->sig_digest[i] >> 4];
        w.buf[w.len++] = hexd[v->sig_digest[i] & 15u];
    }
    return cn8583_set(m, 63, buf, w.len);
}

/* Reader over "KEY=value;KEY=value". */
typedef struct {
    const char *s;
    uint32_t n, p;
} tr_t;

static bool tr_lit(tr_t *r, const char *lit)
{
    for (uint32_t i = 0; lit[i]; i++) {
        if (r->p >= r->n || r->s[r->p] != lit[i]) return false;
        r->p++;
    }
    return true;
}

/* 1..max_digits decimal digits, no leading zero unless the value is 0. */
static bool tr_dec(tr_t *r, uint32_t max_digits, uint64_t *v)
{
    uint32_t start = r->p;
    while (r->p < r->n && cn_is_digit(r->s[r->p]) && r->p - start < max_digits + 1) r->p++;
    uint32_t k = r->p - start;
    if (k == 0 || k > max_digits) return false;
    if (k > 1 && r->s[start] == '0') return false;
    return cn_dec_to_u64(r->s + start, k, v);
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

cn8583_rc_t cn8583_get_vss(const cn8583_msg_t *m, cn_vss_meta_t *v)
{
    const char *d;
    uint32_t n;
    uint64_t x;
    if (!m || !v) return CN8583_ERR_ARG;
    cn_zero(v, (uint32_t) sizeof(*v));

    cn8583_rc_t rc = cn8583_get(m, 60, &d, &n);
    if (rc != CN8583_OK) return rc;
    tr_t r = {d, n, 0};
    if (!tr_lit(&r, "VSS1;NET=") || r.p >= r.n) return CN8583_ERR_FORMAT;
    v->network = CN_NET_COUNT;
    for (uint32_t i = 0; i < CN_NET_COUNT; i++)
        if (r.s[r.p] == net_letter[i]) v->network = i;
    if (v->network == CN_NET_COUNT) return CN8583_ERR_FORMAT;
    r.p++;
    if (!tr_lit(&r, ";FORM=") || !tr_dec(&r, 1, &x) || x >= CN_FORM_COUNT) return CN8583_ERR_FORMAT;
    v->form = (uint32_t) x;
    if (!tr_lit(&r, ";DR=") || !tr_dec(&r, 3, &x)) return CN8583_ERR_FORMAT;
    v->rail_dr = (uint32_t) x;
    if (!tr_lit(&r, ";CR=") || !tr_dec(&r, 3, &x)) return CN8583_ERR_FORMAT;
    v->rail_cr = (uint32_t) x;
    if (!tr_lit(&r, ";EQ=") || !tr_dec(&r, 3, &x)) return CN8583_ERR_FORMAT;
    v->rail_eq = (uint32_t) x;
    if (r.p != r.n) return CN8583_ERR_FORMAT;

    rc = cn8583_get(m, 61, &d, &n);
    if (rc != CN8583_OK) return rc;
    r = (tr_t){d, n, 0};
    if (!tr_lit(&r, "RCPT=") || !tr_dec(&r, 9, &x)) return CN8583_ERR_FORMAT;
    v->receipt = (uint32_t) x;
    if (!tr_lit(&r, ";ATC=") || !tr_dec(&r, 5, &x) || x > 0xFFFFu) return CN8583_ERR_FORMAT;
    v->atc = (uint32_t) x;
    if (r.p != r.n) return CN8583_ERR_FORMAT;

    rc = cn8583_get(m, 63, &d, &n);
    if (rc != CN8583_OK) return rc;
    if (n != 4 + 64 || !cn_eq(d, "CRY=", 4)) return CN8583_ERR_FORMAT;
    for (uint32_t i = 0; i < 32; i++) {
        int hi = hexval(d[4 + 2 * i]), lo = hexval(d[5 + 2 * i]);
        if (hi < 0 || lo < 0) return CN8583_ERR_FORMAT;
        v->sig_digest[i] = (uint8_t) (hi * 16 + lo);
    }
    return CN8583_OK;
}

/* ---- cardnet mapping ---- */

cn8583_rc_t cn8583_from_auth_req(cn8583_msg_t *m, uint32_t mti, const cn_auth_req_t *req,
                                 const uint8_t sig[CN_SIG_BYTES])
{
    if (!m || !req || (mti != 100 && mti != 200)) return CN8583_ERR_ARG;
    if (cn_strnlen(req->pan, CN_PAN_LEN + 1) != CN_PAN_LEN) return CN8583_ERR_ARG;
    cn8583_rc_t rc = cn8583_init(m, mti);
    if (rc != CN8583_OK) return rc;
    cn_civil_t c;
    cn_civil_from_time(req->time, &c);
    uint64_t f7 = (uint64_t) c.month * 100000000ull + (uint64_t) c.day * 1000000ull +
                  (uint64_t) c.hour * 10000ull + (uint64_t) c.minute * 100ull + c.second;
    uint64_t f12 = (uint64_t) c.hour * 10000ull + (uint64_t) c.minute * 100ull + c.second;
    uint64_t f13 = (uint64_t) c.month * 100ull + c.day;
    if ((rc = cn8583_set(m, 2, req->pan, CN_PAN_LEN)) != CN8583_OK) return rc;
    if ((rc = cn8583_set_num(m, 3, 0)) != CN8583_OK) return rc;
    if ((rc = cn8583_set_num(m, 4, req->amount_minor)) != CN8583_OK) return rc;
    if ((rc = cn8583_set_num(m, 7, f7)) != CN8583_OK) return rc;
    if ((rc = cn8583_set_num(m, 11, req->stan)) != CN8583_OK) return rc;
    if ((rc = cn8583_set_num(m, 12, f12)) != CN8583_OK) return rc;
    if ((rc = cn8583_set_num(m, 13, f13)) != CN8583_OK) return rc;
    if ((rc = cn8583_set(m, 41, req->terminal_id, cn_strnlen(req->terminal_id, CN_TID_LEN + 1))) !=
        CN8583_OK)
        return rc;
    if ((rc = cn8583_set(m, 42, req->merchant_id, cn_strnlen(req->merchant_id, CN_MID_LEN + 1))) !=
        CN8583_OK)
        return rc;
    if ((rc = cn8583_set_num(m, 49, req->currency)) != CN8583_OK) return rc;
    cn_vss_meta_t v;
    cn_zero(&v, (uint32_t) sizeof(v));
    cn_network_t net = cn_pan_network(req->pan, CN_PAN_LEN);
    if (net == CN_NET_NONE) return CN8583_ERR_FORMAT;
    v.network = (uint32_t) net;
    v.form = req->form;
    v.rail_dr = CN_RAIL_DEBIT;
    v.rail_cr = CN_RAIL_CREDIT;
    v.rail_eq = CN_RAIL_EQUITY;
    v.receipt = 0;
    v.atc = req->atc;
    if (sig) sha3_256(sig, CN_SIG_BYTES, v.sig_digest);
    return cn8583_put_vss(m, &v);
}

cn8583_rc_t cn8583_response_for(cn8583_msg_t *m, uint32_t mti, const cn_auth_t *au)
{
    char rrn[12];
    if (!m || !au || (mti != 110 && mti != 210)) return CN8583_ERR_ARG;
    cn8583_rc_t rc = cn8583_init(m, mti);
    if (rc != CN8583_OK) return rc;
    if ((rc = cn8583_set_num(m, 3, 0)) != CN8583_OK) return rc;
    if ((rc = cn8583_set_num(m, 4, au->req.amount_minor)) != CN8583_OK) return rc;
    if ((rc = cn8583_set_num(m, 11, au->req.stan)) != CN8583_OK) return rc;
    if (!cn_u64_to_dec_fixed(au->req.stan, 12, rrn)) return CN8583_ERR_FORMAT;
    if ((rc = cn8583_set(m, 37, rrn, 12)) != CN8583_OK) return rc;
    if (au->state == CN_AUTH_APPROVED || au->state == CN_AUTH_SETTLED)
        if ((rc = cn8583_set(m, 38, au->approval, 6)) != CN8583_OK) return rc;
    if ((rc = cn8583_set(m, 39, au->rc, 2)) != CN8583_OK) return rc;
    return cn8583_set(m, 41, au->req.terminal_id, cn_strnlen(au->req.terminal_id, CN_TID_LEN + 1));
}

cn8583_rc_t cn8583_to_auth_req(const cn8583_msg_t *m, uint32_t year, cn_auth_req_t *req)
{
    const char *d;
    uint32_t n;
    uint64_t v;
    if (!m || !req || (m->mti != 100 && m->mti != 200)) return CN8583_ERR_ARG;
    cn8583_rc_t rc = cn8583_check_mandatory(m);
    if (rc != CN8583_OK) return rc;
    cn_zero(req, (uint32_t) sizeof(*req));
    if ((rc = cn8583_get(m, 2, &d, &n)) != CN8583_OK) return rc;
    if (n != CN_PAN_LEN) return CN8583_ERR_FORMAT;
    cn_copy(req->pan, d, n);
    if ((rc = cn8583_get_num(m, 4, &v)) != CN8583_OK) return rc;
    req->amount_minor = v;
    if ((rc = cn8583_get_num(m, 11, &v)) != CN8583_OK) return rc;
    req->stan = (uint32_t) v;
    if ((rc = cn8583_get_num(m, 49, &v)) != CN8583_OK) return rc;
    req->currency = (uint32_t) v;
    if ((rc = cn8583_get(m, 7, &d, &n)) != CN8583_OK) return rc;
    {
        /* MMDDhhmmss, read as five two-digit groups (no 64-bit division). */
        uint64_t g[5];
        for (uint32_t i = 0; i < 5; i++)
            if (!cn_dec_to_u64(d + 2 * i, 2, &g[i])) return CN8583_ERR_FORMAT;
        cn_civil_t c;
        c.year = year;
        c.month = (uint32_t) g[0];
        c.day = (uint32_t) g[1];
        c.hour = (uint32_t) g[2];
        c.minute = (uint32_t) g[3];
        c.second = (uint32_t) g[4];
        if (!cn_time_from_civil(&c, &req->time)) return CN8583_ERR_FORMAT;
    }
    if ((rc = cn8583_get(m, 41, &d, &n)) != CN8583_OK) return rc;
    cn_copy(req->terminal_id, d, n);
    if ((rc = cn8583_get(m, 42, &d, &n)) != CN8583_OK) return rc;
    cn_copy(req->merchant_id, d, n);
    cn_vss_meta_t vm;
    if (cn8583_get_vss(m, &vm) == CN8583_OK) {
        req->form = vm.form;
        req->atc = vm.atc;
    }
    return CN8583_OK;
}
