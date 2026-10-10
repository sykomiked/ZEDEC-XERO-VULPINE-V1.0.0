/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cn_emv.c — BER-TLV and the EMV-style authorization container. See cn_emv.h. */
#include "cn_emv.h"
#include "cn_util.h"

/* ===== BER-TLV ===== */

static uint32_t tag_bytes(uint32_t tag)
{
    return tag > 0xFFFFu ? 3u : tag > 0xFFu ? 2u : 1u;
}

bool cn_tlv_tag_valid(uint32_t tag)
{
    if (tag == 0 || tag > 0xFFFFFFu) return false;
    uint32_t n = tag_bytes(tag);
    uint8_t b0 = (uint8_t) (tag >> (8 * (n - 1)));
    bool multi = (b0 & 0x1Fu) == 0x1Fu;
    if (n == 1) return !multi;
    if (!multi) return false;
    if (n == 2) return ((tag & 0xFFu) & 0x80u) == 0 && (tag & 0xFFu) != 0;
    uint8_t b1 = (uint8_t) (tag >> 8), b2 = (uint8_t) tag;
    return (b1 & 0x80u) != 0 && (b2 & 0x80u) == 0;
}

bool cn_tlv_constructed(uint32_t tag)
{
    uint32_t n = tag_bytes(tag);
    return ((tag >> (8 * (n - 1))) & 0x20u) != 0;
}

void cn_tlv_writer_init(cn_tlv_writer_t *w, uint8_t *buf, uint32_t cap)
{
    if (!w) return;
    w->buf = buf;
    w->cap = buf ? cap : 0;
    w->len = 0;
    w->err = CN_TLV_OK;
}

cn_tlv_rc_t cn_tlv_put(cn_tlv_writer_t *w, uint32_t tag, const uint8_t *val, uint32_t len)
{
    if (!w) return CN_TLV_ERR_ARG;
    if (w->err) return (cn_tlv_rc_t) w->err;
    if (!cn_tlv_tag_valid(tag)) return (cn_tlv_rc_t) (w->err = CN_TLV_ERR_TAG);
    if ((!val && len) || len > 0xFFFFu) return (cn_tlv_rc_t) (w->err = CN_TLV_ERR_LEN);
    uint32_t tn = tag_bytes(tag);
    uint32_t ln = len < 0x80u ? 1u : len <= 0xFFu ? 2u : 3u;
    if (w->cap - w->len < tn + ln || w->cap - w->len - tn - ln < len)
        return (cn_tlv_rc_t) (w->err = CN_TLV_ERR_SPACE);
    for (uint32_t i = tn; i-- > 0;) w->buf[w->len++] = (uint8_t) (tag >> (8 * i));
    if (ln == 1) {
        w->buf[w->len++] = (uint8_t) len;
    } else if (ln == 2) {
        w->buf[w->len++] = 0x81u;
        w->buf[w->len++] = (uint8_t) len;
    } else {
        w->buf[w->len++] = 0x82u;
        w->buf[w->len++] = (uint8_t) (len >> 8);
        w->buf[w->len++] = (uint8_t) len;
    }
    cn_copy(w->buf + w->len, val, len);
    w->len += len;
    return CN_TLV_OK;
}

cn_tlv_rc_t cn_tlv_next(const uint8_t *buf, uint32_t len, uint32_t *pos, uint32_t *tag,
                        const uint8_t **val, uint32_t *vlen)
{
    if (!buf || !pos || !tag || !val || !vlen || *pos >= len) return CN_TLV_ERR_ARG;
    uint32_t p = *pos;
    uint32_t t = buf[p++];
    if (t == 0) return CN_TLV_ERR_TAG;
    if ((t & 0x1Fu) == 0x1Fu) {
        for (uint32_t k = 0;; k++) {
            if (k == 2) return CN_TLV_ERR_TAG; /* would be a 4th tag byte */
            if (p >= len) return CN_TLV_ERR_TAG;
            uint8_t b = buf[p++];
            t = (t << 8) | b;
            if (!(b & 0x80u)) break;
        }
    }
    if (!cn_tlv_tag_valid(t)) return CN_TLV_ERR_TAG;
    if (p >= len) return CN_TLV_ERR_LEN;
    uint32_t l = buf[p++];
    if (l == 0x81u) {
        if (p >= len) return CN_TLV_ERR_LEN;
        l = buf[p++];
        if (l < 0x80u) return CN_TLV_ERR_LEN; /* non-minimal */
    } else if (l == 0x82u) {
        if (len - p < 2) return CN_TLV_ERR_LEN;
        l = ((uint32_t) buf[p] << 8) | buf[p + 1];
        p += 2;
        if (l <= 0xFFu) return CN_TLV_ERR_LEN; /* non-minimal */
    } else if (l >= 0x80u) {
        return CN_TLV_ERR_LEN; /* indefinite (80) or > 2 length bytes */
    }
    if (len - p < l) return CN_TLV_ERR_LEN;
    *tag = t;
    *val = buf + p;
    *vlen = l;
    *pos = p + l;
    return CN_TLV_OK;
}

static cn_tlv_rc_t find_rec(const uint8_t *buf, uint32_t len, uint32_t tag, const uint8_t **val,
                            uint32_t *vlen, uint32_t depth, bool *found)
{
    if (depth > CN_TLV_MAX_DEPTH) return CN_TLV_ERR_DEPTH;
    uint32_t pos = 0;
    while (pos < len) {
        uint32_t t, l;
        const uint8_t *v;
        cn_tlv_rc_t rc = cn_tlv_next(buf, len, &pos, &t, &v, &l);
        if (rc != CN_TLV_OK) return rc;
        if (t == tag && !*found) {
            *found = true;
            *val = v;
            *vlen = l;
        }
        if (cn_tlv_constructed(t)) {
            rc = find_rec(v, l, tag, val, vlen, depth + 1, found);
            if (rc != CN_TLV_OK) return rc;
        }
    }
    return CN_TLV_OK;
}

cn_tlv_rc_t cn_tlv_find(const uint8_t *buf, uint32_t len, uint32_t tag, const uint8_t **val,
                        uint32_t *vlen)
{
    if (!buf || !val || !vlen) return CN_TLV_ERR_ARG;
    bool found = false;
    cn_tlv_rc_t rc = find_rec(buf, len, tag, val, vlen, 1, &found);
    if (rc != CN_TLV_OK) return rc;
    return found ? CN_TLV_OK : CN_TLV_ERR_MISSING;
}

/* ===== BCD ===== */

cn_tlv_rc_t cn_bcd_from_digits(const char *digits, uint32_t ndigits, uint8_t *out, uint32_t cap,
                               uint32_t *out_len)
{
    if (!digits || !out || !out_len || ndigits == 0) return CN_TLV_ERR_ARG;
    if (!cn_all_digits(digits, ndigits)) return CN_TLV_ERR_VALUE;
    uint32_t nb = (ndigits + 1u) / 2u;
    if (nb > cap) return CN_TLV_ERR_SPACE;
    for (uint32_t i = 0; i < nb; i++) {
        uint32_t hi = (uint32_t) (digits[2 * i] - '0');
        uint32_t lo = (2 * i + 1 < ndigits) ? (uint32_t) (digits[2 * i + 1] - '0') : 0xFu;
        out[i] = (uint8_t) ((hi << 4) | lo);
    }
    *out_len = nb;
    return CN_TLV_OK;
}

cn_tlv_rc_t cn_bcd_from_u64(uint64_t v, uint32_t nbytes, uint8_t *out)
{
    char d[20];
    if (!out || nbytes == 0 || nbytes > 10) return CN_TLV_ERR_ARG;
    if (!cn_u64_to_dec_fixed(v, 2 * nbytes, d)) return CN_TLV_ERR_VALUE;
    for (uint32_t i = 0; i < nbytes; i++)
        out[i] = (uint8_t) (((uint32_t) (d[2 * i] - '0') << 4) | (uint32_t) (d[2 * i + 1] - '0'));
    return CN_TLV_OK;
}

cn_tlv_rc_t cn_bcd_to_u64(const uint8_t *bcd, uint32_t nbytes, uint64_t *v)
{
    if (!bcd || !v || nbytes == 0 || nbytes > 9) return CN_TLV_ERR_ARG;
    uint64_t x = 0;
    for (uint32_t i = 0; i < nbytes; i++) {
        uint32_t hi = bcd[i] >> 4, lo = bcd[i] & 0xFu;
        if (hi > 9 || lo > 9) return CN_TLV_ERR_VALUE;
        x = x * 100u + hi * 10u + lo;
    }
    *v = x;
    return CN_TLV_OK;
}

/* ===== Authorization container ===== */

static const uint8_t aid_stem[6] = {0xF0, 0x5A, 0x58, 0x56, 0x43, 0x4E}; /* F0 "ZXVCN" */

static uint32_t days_in_month(uint32_t y, uint32_t m)
{
    static const uint8_t md[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (y % 4u == 0 && y % 100u != 0) || y % 400u == 0;
    return md[m - 1] + ((m == 2 && leap) ? 1u : 0u);
}

static cn_tlv_rc_t put_bcd(cn_tlv_writer_t *w, uint32_t tag, uint64_t v, uint32_t nbytes)
{
    uint8_t b[10];
    cn_tlv_rc_t rc = cn_bcd_from_u64(v, nbytes, b);
    if (rc != CN_TLV_OK) return (cn_tlv_rc_t) (w->err = rc);
    return cn_tlv_put(w, tag, b, nbytes);
}

cn_tlv_rc_t cn_emv_encode_auth(const cn_auth_req_t *req, uint32_t exp_year, uint32_t exp_month,
                               const uint8_t sig[CN_SIG_BYTES], uint8_t *out, uint32_t cap,
                               uint32_t *out_len)
{
    static uint8_t inner[CN_EMV_AUTH_MAX];
    if (!req || !sig || !out || !out_len) return CN_TLV_ERR_ARG;
    if (exp_year < 2000 || exp_year > 2099 || exp_month < 1 || exp_month > 12)
        return CN_TLV_ERR_VALUE;
    cn_network_t net = cn_pan_network(req->pan, CN_PAN_LEN);
    if (net == CN_NET_NONE || req->form >= CN_FORM_COUNT || req->atc == 0 || req->atc > 0xFFFFu)
        return CN_TLV_ERR_VALUE;
    if (cn_strnlen(req->terminal_id, CN_TID_LEN + 1) != CN_TID_LEN ||
        cn_strnlen(req->merchant_id, CN_MID_LEN + 1) != CN_MID_LEN)
        return CN_TLV_ERR_VALUE;
    cn_civil_t c;
    cn_civil_from_time(req->time, &c);
    if (c.year > 2099) return CN_TLV_ERR_VALUE;

    cn_tlv_writer_t w;
    cn_tlv_writer_init(&w, inner, sizeof(inner));
    uint8_t b[16];
    uint32_t n;
    cn_copy(b, aid_stem, 6);
    b[6] = (uint8_t) net;
    cn_tlv_put(&w, CN_EMV_AID, b, 7);
    if (cn_bcd_from_digits(req->pan, CN_PAN_LEN, b, sizeof(b), &n) != CN_TLV_OK)
        return CN_TLV_ERR_VALUE;
    cn_tlv_put(&w, CN_EMV_PAN, b, n);
    uint64_t yymmdd = (uint64_t) (exp_year - 2000u) * 10000u + exp_month * 100u +
                      days_in_month(exp_year, exp_month);
    put_bcd(&w, CN_EMV_EXPIRY, yymmdd, 3);
    put_bcd(&w, CN_EMV_CURRENCY, req->currency, 2);
    cn_zero(b, 5);
    cn_tlv_put(&w, CN_EMV_AIP, b, 2);
    cn_tlv_put(&w, CN_EMV_TVR, b, 5);
    put_bcd(&w, CN_EMV_TXN_DATE, (uint64_t) (c.year - 2000u) * 10000u + c.month * 100u + c.day, 3);
    put_bcd(&w, CN_EMV_TXN_TYPE, 0, 1);
    put_bcd(&w, CN_EMV_AMOUNT, req->amount_minor, 6);
    cn_tlv_put(&w, CN_EMV_MERCHANT_ID, (const uint8_t *) req->merchant_id, CN_MID_LEN);
    cn_tlv_put(&w, CN_EMV_TERMINAL_ID, (const uint8_t *) req->terminal_id, CN_TID_LEN);
    put_bcd(&w, CN_EMV_TXN_TIME, (uint64_t) c.hour * 10000u + c.minute * 100u + c.second, 3);
    cn_cryptogram(sig, b);
    cn_tlv_put(&w, CN_EMV_CRYPTOGRAM, b, 8);
    b[0] = (uint8_t) (req->atc >> 8);
    b[1] = (uint8_t) req->atc;
    cn_tlv_put(&w, CN_EMV_ATC, b, 2);
    cn_tlv_put(&w, CN_EMV_UN, req->un, 4);
    put_bcd(&w, CN_EMV_TSC, req->stan, 4);
    cn_tlv_put(&w, CN_EMV_ZXV_SIG, sig, CN_SIG_BYTES);
    b[0] = (uint8_t) req->form;
    cn_tlv_put(&w, CN_EMV_ZXV_FORM, b, 1);
    b[0] = (uint8_t) net;
    cn_tlv_put(&w, CN_EMV_ZXV_NET, b, 1);
    if (w.err) return (cn_tlv_rc_t) w.err;

    cn_tlv_writer_t o;
    cn_tlv_writer_init(&o, out, cap);
    cn_tlv_rc_t rc = cn_tlv_put(&o, CN_EMV_TEMPLATE, inner, w.len);
    if (rc != CN_TLV_OK) return rc;
    *out_len = o.len;
    return CN_TLV_OK;
}

static cn_tlv_rc_t get_fixed(const uint8_t *buf, uint32_t len, uint32_t tag, uint32_t want,
                             const uint8_t **v)
{
    uint32_t l;
    cn_tlv_rc_t rc = cn_tlv_find(buf, len, tag, v, &l);
    if (rc != CN_TLV_OK) return rc;
    return l == want ? CN_TLV_OK : CN_TLV_ERR_VALUE;
}

static cn_tlv_rc_t get_bcd(const uint8_t *buf, uint32_t len, uint32_t tag, uint32_t nbytes,
                           uint64_t *x)
{
    const uint8_t *v;
    cn_tlv_rc_t rc = get_fixed(buf, len, tag, nbytes, &v);
    if (rc != CN_TLV_OK) return rc;
    return cn_bcd_to_u64(v, nbytes, x);
}

#define TRY(e)                                                                                     \
    do {                                                                                           \
        cn_tlv_rc_t rc_ = (e);                                                                     \
        if (rc_ != CN_TLV_OK) return rc_;                                                          \
    } while (0)

cn_tlv_rc_t cn_emv_decode_auth(const uint8_t *buf, uint32_t len, cn_auth_req_t *req,
                               uint8_t sig[CN_SIG_BYTES])
{
    const uint8_t *v, *body;
    uint32_t blen, pos = 0, t;
    uint64_t x;
    if (!buf || !req || !sig) return CN_TLV_ERR_ARG;
    TRY(cn_tlv_next(buf, len, &pos, &t, &body, &blen));
    if (t != CN_EMV_TEMPLATE || pos != len) return CN_TLV_ERR_VALUE;
    cn_zero(req, (uint32_t) sizeof(*req));

    TRY(get_fixed(body, blen, CN_EMV_PAN, 8, &v));
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t hi = v[i] >> 4, lo = v[i] & 0xFu;
        if (hi > 9 || lo > 9) return CN_TLV_ERR_VALUE;
        req->pan[2 * i] = (char) ('0' + hi);
        req->pan[2 * i + 1] = (char) ('0' + lo);
    }
    cn_network_t net = cn_pan_network(req->pan, CN_PAN_LEN);
    if (net == CN_NET_NONE) return CN_TLV_ERR_VALUE;
    TRY(get_fixed(body, blen, CN_EMV_AID, 7, &v));
    if (!cn_eq(v, aid_stem, 6) || v[6] != (uint8_t) net) return CN_TLV_ERR_VALUE;
    TRY(get_fixed(body, blen, CN_EMV_ZXV_NET, 1, &v));
    if (v[0] != (uint8_t) net) return CN_TLV_ERR_VALUE;

    TRY(get_bcd(body, blen, CN_EMV_EXPIRY, 3, &x)); /* present and valid BCD */
    TRY(get_bcd(body, blen, CN_EMV_AMOUNT, 6, &x));
    req->amount_minor = x;
    TRY(get_bcd(body, blen, CN_EMV_CURRENCY, 2, &x));
    req->currency = (uint32_t) x;
    TRY(get_bcd(body, blen, CN_EMV_TSC, 4, &x));
    if (x > 999999u) return CN_TLV_ERR_VALUE;
    req->stan = (uint32_t) x;
    TRY(get_fixed(body, blen, CN_EMV_ATC, 2, &v));
    req->atc = ((uint32_t) v[0] << 8) | v[1];
    TRY(get_fixed(body, blen, CN_EMV_UN, 4, &v));
    cn_copy(req->un, v, 4);
    TRY(get_fixed(body, blen, CN_EMV_ZXV_FORM, 1, &v));
    if (v[0] >= CN_FORM_COUNT) return CN_TLV_ERR_VALUE;
    req->form = v[0];
    TRY(get_fixed(body, blen, CN_EMV_MERCHANT_ID, CN_MID_LEN, &v));
    cn_copy(req->merchant_id, v, CN_MID_LEN);
    TRY(get_fixed(body, blen, CN_EMV_TERMINAL_ID, CN_TID_LEN, &v));
    cn_copy(req->terminal_id, v, CN_TID_LEN);
    for (uint32_t i = 0; i < CN_MID_LEN; i++)
        if (req->merchant_id[i] < 0x20 || req->merchant_id[i] > 0x7E) return CN_TLV_ERR_VALUE;
    for (uint32_t i = 0; i < CN_TID_LEN; i++)
        if (req->terminal_id[i] < 0x20 || req->terminal_id[i] > 0x7E) return CN_TLV_ERR_VALUE;

    uint64_t date, tod;
    TRY(get_bcd(body, blen, CN_EMV_TXN_DATE, 3, &date));
    TRY(get_bcd(body, blen, CN_EMV_TXN_TIME, 3, &tod));
    {
        /* Split YYMMDD / hhmmss with 32-bit arithmetic (both < 10^6). */
        uint32_t d32 = (uint32_t) date, t32 = (uint32_t) tod;
        cn_civil_t c;
        c.year = 2000u + d32 / 10000u;
        c.month = (d32 / 100u) % 100u;
        c.day = d32 % 100u;
        c.hour = t32 / 10000u;
        c.minute = (t32 / 100u) % 100u;
        c.second = t32 % 100u;
        if (!cn_time_from_civil(&c, &req->time)) return CN_TLV_ERR_VALUE;
    }

    TRY(get_fixed(body, blen, CN_EMV_ZXV_SIG, CN_SIG_BYTES, &v));
    cn_copy(sig, v, CN_SIG_BYTES);
    uint8_t crypt[8];
    cn_cryptogram(sig, crypt);
    TRY(get_fixed(body, blen, CN_EMV_CRYPTOGRAM, 8, &v));
    if (!cn_eq(v, crypt, 8)) return CN_TLV_ERR_VALUE;
    return CN_TLV_OK;
}
