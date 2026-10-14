/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_cardnet.c — the card networks' wire decoders (kernel/src/cardnet):
 * ISO 8583 (cn_iso8583.c), the EMV-style BER-TLV container (cn_emv.c) and
 * the mobile-money amount parser (cn_mobile.c).
 *
 * Byte 0 picks the mode (mod 6); the rest is the input:
 *   0  cn8583_unpack: every accepted field re-validates; decode accepts only
 *      canonical encodings, so cn8583_pack(unpack(x)) == x byte for byte;
 *      field 4 (amount) reads back as exactly the value of its 12 digits;
 *      cn8583_get_vss output re-encodes (cn8583_put_vss) to the same 60/61/63;
 *      cn8583_to_auth_req -> cn8583_from_auth_req -> pack -> unpack ->
 *      to_auth_req gives back the same request (amount exact, no rounding)
 *   1  cn_emv_decode_auth: the decoded request is in range (amount <=
 *      CN_AMOUNT_MAX_MINOR, stan <= 999999, form, PAN network matches the
 *      AID, cryptogram == SHA3(sig)[0..8]); decode accepts only canonical
 *      containers: cn_emv_encode_auth of what was decoded (with the carried
 *      expiry) reproduces the input byte for byte
 *   2  cn_mm_parse_amount against an independent reference grammar
 *      [0-9]{1,10}(\.[0-9]{1,2})?: same accept/refuse and the exact value
 *      (no silent rounding, truncation or overflow; '-' and '+' refused);
 *      cn_mm_format_amount of an fz_edge64 value parses back exactly when it
 *      fits 10 integer digits and is refused otherwise
 *   3  BER-TLV walk (cn_tlv_next / cn_tlv_find): no memory error, every
 *      element re-encodes with cn_tlv_put to the same bytes (minimal lengths)
 *   4  BCD: cn_bcd_to_u64 / cn_bcd_from_u64 round trip, nibbles > 9 refused
 *   Build with -DFZ_LAX_CANON to skip only the byte-exact canonical checks
 *   of modes 0 and 1 (the semantic checks stay).
 *   5  EMV round trip: a generated valid request + signature encodes, and
 *      decodes back to exactly the same request and signature */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cardnet.h"
#include "cn_emv.h"
#include "cn_iso8583.h"
#include "cn_mobile.h"
#include "fuzz_in.h"

static uint8_t g_wire[CN8583_MAX_WIRE + 64];
static uint8_t g_wire2[CN8583_MAX_WIRE + 64];
static cn8583_msg_t g_m, g_m2;
static uint8_t g_sig[CN_SIG_BYTES], g_sig2[CN_SIG_BYTES];
static uint8_t g_emv[CN_EMV_AUTH_MAX + 64];

static void req_same(const cn_auth_req_t *a, const cn_auth_req_t *b, bool un_atc_form)
{
    if (memcmp(a->pan, b->pan, sizeof a->pan) != 0) abort();
    if (a->amount_minor != b->amount_minor || a->currency != b->currency) abort();
    if (a->time != b->time || a->stan != b->stan) abort();
    if (memcmp(a->terminal_id, b->terminal_id, sizeof a->terminal_id) != 0) abort();
    if (memcmp(a->merchant_id, b->merchant_id, sizeof a->merchant_id) != 0) abort();
    if (un_atc_form && (a->atc != b->atc || a->form != b->form || memcmp(a->un, b->un, 4) != 0))
        abort();
}

/* ---- mode 0: ISO 8583 ---- */

static void mode_8583(const uint8_t *p, size_t n)
{
    if (n > CN8583_MAX_WIRE) return;
    uint8_t *b = fz_dup(p, n);
    cn8583_rc_t rc = cn8583_unpack(&g_m, b, (uint32_t) n);
    if (rc == CN8583_OK) {
        if (!cn8583_mti_supported(g_m.mti)) abort();
        for (uint32_t f = 2; f <= 128; f++) {
            if (!cn8583_has(&g_m, f)) continue;
            const char *d;
            uint32_t l;
            if (cn8583_get(&g_m, f, &d, &l) != CN8583_OK) abort();
            /* re-setting the same value must be accepted (it validated once) */
            cn8583_msg_t *t = &g_m2;
            if (cn8583_init(t, g_m.mti) != CN8583_OK) abort();
            if (cn8583_set(t, f, d, l) != CN8583_OK) abort();
        }
        uint64_t amt;
        if (cn8583_get_num(&g_m, 4, &amt) == CN8583_OK) {
            const char *d;
            uint32_t l;
            (void) cn8583_get(&g_m, 4, &d, &l);
            uint64_t ref = 0;
            if (l != 12) abort();
            for (uint32_t i = 0; i < 12; i++) ref = ref * 10u + (uint64_t) (d[i] - '0');
            if (ref != amt || amt > CN_AMOUNT_MAX_MINOR) abort();
        }
        uint32_t wl = 0;
        if (cn8583_pack(&g_m, g_wire, sizeof g_wire, &wl) != CN8583_OK) abort();
        /* canonical: the packer gives back the exact bytes */
#ifndef FZ_LAX_CANON
        if (wl != n || memcmp(g_wire, b, n) != 0) abort();
#endif
        cn_vss_meta_t v, v2;
        if (cn8583_get_vss(&g_m, &v) == CN8583_OK) {
            if (cn8583_init(&g_m2, g_m.mti) != CN8583_OK) abort();
            if (cn8583_put_vss(&g_m2, &v) != CN8583_OK) abort();
            static const uint32_t fl[3] = {60, 61, 63};
            for (int i = 0; i < 3; i++) {
                const char *d1, *d2;
                uint32_t l1, l2;
                if (cn8583_get(&g_m, fl[i], &d1, &l1) != CN8583_OK ||
                    cn8583_get(&g_m2, fl[i], &d2, &l2) != CN8583_OK || l1 != l2 ||
                    memcmp(d1, d2, l1) != 0)
                    abort();
            }
            if (cn8583_get_vss(&g_m2, &v2) != CN8583_OK || memcmp(&v, &v2, sizeof v) != 0) abort();
        }
        cn_auth_req_t req, back;
        if ((g_m.mti == 100 || g_m.mti == 200) && cn8583_to_auth_req(&g_m, 2026, &req) == CN8583_OK) {
            if (req.amount_minor != amt) abort();
            if (req.stan > 999999u) abort();
            /* re-encode only what the encoder takes (a network PAN, a form) */
            if (cn_pan_network(req.pan, CN_PAN_LEN) != CN_NET_NONE &&
                req.form < CN_FORM_COUNT && req.atc <= 0xFFFFu) {
                cn8583_rc_t r2 = cn8583_from_auth_req(&g_m2, g_m.mti, &req, NULL);
                if (r2 == CN8583_OK) {
                    uint32_t l2 = 0;
                    if (cn8583_pack(&g_m2, g_wire2, sizeof g_wire2, &l2) != CN8583_OK) abort();
                    if (cn8583_unpack(&g_m2, g_wire2, l2) != CN8583_OK) abort();
                    if (cn8583_to_auth_req(&g_m2, 2026, &back) != CN8583_OK) abort();
                    req_same(&req, &back, false);
                }
            }
        }
    }
    free(b);
}

/* ---- mode 1: EMV decode ---- */

/* The carried 5F24 expiry (YYMMDD BCD), for re-encoding. */
static bool carried_expiry(const uint8_t *b, size_t n, uint32_t *y, uint32_t *m)
{
    uint32_t pos = 0, t, blen, l;
    const uint8_t *body, *v;
    if (cn_tlv_next(b, (uint32_t) n, &pos, &t, &body, &blen) != CN_TLV_OK) return false;
    if (cn_tlv_find(body, blen, CN_EMV_EXPIRY, &v, &l) != CN_TLV_OK || l != 3) return false;
    uint64_t x;
    if (cn_bcd_to_u64(v, 3, &x) != CN_TLV_OK) return false;
    *y = 2000u + (uint32_t) (x / 10000u);
    *m = (uint32_t) (x / 100u % 100u);
    return true;
}

static void mode_emv(const uint8_t *p, size_t n)
{
    if (n > CN_EMV_AUTH_MAX + 16) return;
    uint8_t *b = fz_dup(p, n);
    cn_auth_req_t req;
    if (cn_emv_decode_auth(b, (uint32_t) n, &req, g_sig) == CN_TLV_OK) {
        if (req.amount_minor > CN_AMOUNT_MAX_MINOR || req.stan > 999999u) abort();
        if (req.form >= CN_FORM_COUNT || req.atc > 0xFFFFu) abort();
        if (cn_pan_network(req.pan, CN_PAN_LEN) == CN_NET_NONE) abort();
        if (req.pan[CN_PAN_LEN] || req.terminal_id[CN_TID_LEN] || req.merchant_id[CN_MID_LEN])
            abort();
        uint8_t c8[8];
        cn_cryptogram(g_sig, c8);
        const uint8_t *v;
        uint32_t l;
        if (cn_tlv_find(b, (uint32_t) n, CN_EMV_CRYPTOGRAM, &v, &l) != CN_TLV_OK || l != 8 ||
            memcmp(v, c8, 8) != 0)
            abort();
        /* canonical: re-encoding gives the same bytes */
        uint32_t ey = 0, em = 0, ol = 0;
        if (!carried_expiry(b, n, &ey, &em)) abort();
        cn_tlv_rc_t rc = cn_emv_encode_auth(&req, ey, em, g_sig, g_emv, sizeof g_emv, &ol);
        if (rc != CN_TLV_OK) {
            if (getenv("FZ_DEBUG")) fprintf(stderr, "emv: decoded but encoder refuses (%d)\n", rc);
#ifndef FZ_LAX_CANON
            abort();
#endif
        }
#ifndef FZ_LAX_CANON
        if (ol != n || memcmp(g_emv, b, n) != 0) {
            if (getenv("FZ_DEBUG")) fprintf(stderr, "emv: decoded a non-canonical container\n");
            abort();
        }
#endif
    }
    free(b);
}

/* ---- mode 2: amounts ---- */

static bool ref_amount(const char *s, size_t n, uint64_t *out)
{
    size_t i = 0;
    uint64_t u = 0, c = 0;
    if (n == 0 || n >= 16) return false;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        u = u * 10u + (uint64_t) (s[i] - '0');
        i++;
    }
    if (i == 0 || i > 10) return false;
    if (i < n) {
        if (s[i] != '.') return false;
        size_t f0 = ++i;
        while (i < n && s[i] >= '0' && s[i] <= '9') i++;
        size_t fd = i - f0;
        if (i != n || fd < 1 || fd > 2) return false;
        c = (uint64_t) (s[f0] - '0') * 10u + (fd == 2 ? (uint64_t) (s[f0 + 1] - '0') : 0u);
    }
    *out = u * 100u + c;
    return true;
}

static void mode_amount(const uint8_t *p, size_t n)
{
    if (n < 1) return;
    uint8_t sel = p[0];
    p++;
    n--;
    if (sel & 1u) {
        /* the bytes as a string (up to the first NUL) */
        if (n > 40) n = 40;
        char *s = fz_dupz(p, n);
        size_t sl = strlen(s);
        uint64_t a = 0xDEADu, r = 0;
        bool ok = cn_mm_parse_amount(s, &a);
        bool rok = ref_amount(s, sl, &r);
        if (ok != rok) abort();
        if (ok && a != r) abort();
        if (!ok && a != 0xDEADu) abort(); /* a refusal writes nothing */
        if (ok && memchr(s, '-', sl)) abort();
        if (ok) {
            char out[32];
            uint32_t k = cn_mm_format_amount(a, out, sizeof out);
            uint64_t a2;
            if (k == 0 || !cn_mm_parse_amount(out, &a2) || a2 != a) abort();
        }
        free(s);
    } else {
        fz_in in;
        fz_init(&in, p, n);
        while (in.n) {
            uint64_t x = fz_edge64(&in);
            char out[32];
            uint32_t k = cn_mm_format_amount(x, out, sizeof out);
            if (k == 0 || k != strlen(out)) abort();
            /* the formatted text is exact: digits, '.', two decimals */
            uint64_t ip = x / 100u, ref_len = 1;
            for (uint64_t t = ip; t >= 10u; t /= 10u) ref_len++;
            if (k != ref_len + 3u || out[k - 3] != '.') abort();
            uint64_t a = 0;
            bool ok = cn_mm_parse_amount(out, &a);
            bool fits = ip <= 9999999999ull;
            if (ok != fits) abort();
            if (ok && a != x) abort();
            /* a small capacity is refused, never truncated */
            char tiny[4];
            if (k + 1 > sizeof tiny && cn_mm_format_amount(x, tiny, sizeof tiny) != 0) abort();
        }
    }
}

/* ---- mode 3: TLV ---- */

static void walk(const uint8_t *b, uint32_t n, uint32_t depth)
{
    uint32_t pos = 0;
    static uint8_t out[8192];
    while (pos < n) {
        uint32_t t, l, p0 = pos;
        const uint8_t *v;
        if (cn_tlv_next(b, n, &pos, &t, &v, &l) != CN_TLV_OK) return;
        if (pos <= p0 || pos > n || v < b || v + l > b + n) abort();
        if (!cn_tlv_tag_valid(t)) abort();
        cn_tlv_writer_t w;
        cn_tlv_writer_init(&w, out, sizeof out);
        if (cn_tlv_put(&w, t, v, l) != CN_TLV_OK) abort();
        if (w.len != pos - p0 || memcmp(out, b + p0, w.len) != 0) abort();
        if (cn_tlv_constructed(t) && depth < 6) walk(v, l, depth + 1);
    }
}

static void mode_tlv(const uint8_t *p, size_t n)
{
    if (n < 3) return;
    uint32_t tag = ((uint32_t) p[0] << 16) | ((uint32_t) p[1] << 8) | p[2];
    p += 3;
    n -= 3;
    uint8_t *b = fz_dup(p, n);
    walk(b, (uint32_t) n, 0);
    const uint8_t *v;
    uint32_t l;
    if (cn_tlv_find(b, (uint32_t) n, tag >> (8 * (p[-3] % 3u)), &v, &l) == CN_TLV_OK) {
        if (v < b || v + l > b + n) abort();
    }
    free(b);
}

/* ---- mode 4: BCD ---- */

static void mode_bcd(const uint8_t *p, size_t n)
{
    if (n < 1) return;
    uint32_t nb = 1u + p[0] % 9u;
    p++;
    n--;
    if (n < nb) return;
    uint8_t *b = fz_dup(p, nb);
    uint64_t v;
    bool bad = false;
    for (uint32_t i = 0; i < nb; i++)
        if ((b[i] >> 4) > 9 || (b[i] & 15) > 9) bad = true;
    cn_tlv_rc_t rc = cn_bcd_to_u64(b, nb, &v);
    if (bad != (rc != CN_TLV_OK)) abort();
    if (rc == CN_TLV_OK) {
        uint8_t back[10];
        if (cn_bcd_from_u64(v, nb, back) != CN_TLV_OK || memcmp(back, b, nb) != 0) abort();
        /* one digit narrower must refuse a value that does not fit */
        uint64_t lim = 1;
        for (uint32_t i = 0; i < 2 * (nb - 1); i++) lim *= 10u;
        if (nb > 1 && (cn_bcd_from_u64(v, nb - 1, back) == CN_TLV_OK) != (v < lim)) abort();
    }
    free(b);
}

/* ---- mode 5: EMV round trip ---- */

static bool make_pan(fz_in *in, char pan[CN_PAN_LEN + 1])
{
    static const char *pre[3] = {"880", "882", "884"};
    uint32_t net = fz_u8(in) % 3u;
    memcpy(pan, pre[net], 3);
    for (uint32_t i = 3; i < CN_PAN_LEN - 1; i++) pan[i] = (char) ('0' + fz_u8(in) % 10u);
    pan[CN_PAN_LEN] = 0;
    for (int d = 0; d < 10; d++) {
        pan[CN_PAN_LEN - 1] = (char) ('0' + d);
        if (cn_pan_network(pan, CN_PAN_LEN) == (cn_network_t) net) return true;
    }
    return false;
}

static void mode_emv_rt(const uint8_t *p, size_t n)
{
    fz_in in;
    fz_init(&in, p, n);
    cn_auth_req_t req;
    memset(&req, 0, sizeof req);
    if (!make_pan(&in, req.pan)) return;
    req.amount_minor = fz_edge64(&in);
    req.currency = fz_u16(&in) % 10000u;
    req.form = fz_u8(&in) % CN_FORM_COUNT;
    req.atc = 1u + fz_u16(&in) % 0xFFFFu;
    fz_bytes(&in, req.un, 4);
    req.time = fz_u32(&in);
    req.stan = fz_u32(&in) % 1000000u;
    for (uint32_t i = 0; i < CN_TID_LEN; i++) req.terminal_id[i] = (char) (0x20 + fz_u8(&in) % 95u);
    for (uint32_t i = 0; i < CN_MID_LEN; i++) req.merchant_id[i] = (char) (0x20 + fz_u8(&in) % 95u);
    uint32_t ey = 2000u + fz_u8(&in) % 100u, em = 1u + fz_u8(&in) % 12u;
    for (uint32_t i = 0; i < CN_SIG_BYTES; i++) g_sig[i] = (uint8_t) (i * 131u + fz_u8(&in));
    uint32_t ol = 0;
    cn_tlv_rc_t rc = cn_emv_encode_auth(&req, ey, em, g_sig, g_emv, sizeof g_emv, &ol);
    cn_civil_t c;
    cn_civil_from_time(req.time, &c);
    bool should = req.amount_minor <= 999999999999ull && c.year <= 2099;
    if (should != (rc == CN_TLV_OK)) abort();
    if (rc != CN_TLV_OK) return;
    if (ol > CN_EMV_AUTH_MAX) abort();
    uint8_t *b = fz_dup(g_emv, ol);
    cn_auth_req_t back;
    if (cn_emv_decode_auth(b, ol, &back, g_sig2) != CN_TLV_OK) abort();
    req_same(&req, &back, true);
    if (memcmp(g_sig, g_sig2, CN_SIG_BYTES) != 0) abort();
    /* any single flipped bit in the signature is caught by 9F26 */
    uint32_t at = fz_u16(&in) % CN_SIG_BYTES;
    const uint8_t *v;
    uint32_t l;
    if (cn_tlv_find(b, ol, CN_EMV_ZXV_SIG, &v, &l) != CN_TLV_OK || l != CN_SIG_BYTES) abort();
    b[(uint32_t) (v - b) + at] ^= 0x04;
    if (cn_emv_decode_auth(b, ol, &back, g_sig2) == CN_TLV_OK) abort();
    free(b);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1 || size > 8192) return 0;
    const uint8_t *p = data + 1;
    size_t n = size - 1;
    switch (data[0] % 6u) {
    case 0: mode_8583(p, n); break;
    case 1: mode_emv(p, n); break;
    case 2: mode_amount(p, n); break;
    case 3: mode_tlv(p, n); break;
    case 4: mode_bcd(p, n); break;
    default: mode_emv_rt(p, n); break;
    }
    return 0;
}
