/* wifi.c — ZEDEC XERO pqOS IEEE 802.11 STATION/AP subsystem
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 * 36N9 Genetics, LLC
 *
 * The split this file is built around is stated at the top of wifi.h: every
 * line below is either portable protocol logic that runs and is tested with
 * no radio present, or a call through wifi_ops_t. There is no third category.
 * When ops is NULL the radio-facing entry points return WIFI_ENODEV and
 * change nothing — no scan results appear, no counters move, no interface
 * claims to be connected.
 */

#ifdef TEST_HOST
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#else
#include "freestanding.h"
#endif

#include "wifi.h"

/* ===================================================================== */
/* Small helpers — no libc in the kernel build                            */
/* ===================================================================== */

static void w_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t)v;
}

static void w_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static int w_memcmp(const void *a, const void *b, uint32_t n) {
    const uint8_t *pa = (const uint8_t *)a, *pb = (const uint8_t *)b;
    for (uint32_t i = 0; i < n; i++)
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    return 0;
}

/* Constant-time equality for anything derived from a key. Returns true on
 * match. Length is public; contents are not. */
static bool w_ct_equal(const uint8_t *a, const uint8_t *b, uint32_t n) {
    uint8_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc |= (uint8_t)(a[i] ^ b[i]);
    return acc == 0;
}

static uint32_t w_strlen(const char *s) {
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static void w_strcpy(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    for (; src && src[i] && i + 1 < cap; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static uint16_t rd16le(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static void     wr16le(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint16_t rd16be(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static void     wr16be(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static uint64_t rd64be(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}
static void wr64be(uint8_t *p, uint64_t v) {
    for (int i = 7; i >= 0; i--) { p[i] = (uint8_t)v; v >>= 8; }
}

static const uint8_t WIFI_BCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

/* ===================================================================== */
/* SHA-1 (FIPS 180-1) — streaming, so no message ever gets truncated      */
/* ===================================================================== */

static uint32_t rol32(uint32_t x, unsigned n) {
    return (uint32_t)((x << n) | (x >> (32 - n)));
}

static void sha1_compress(uint32_t h[5], const uint8_t blk[64]) {
    uint32_t w[80];
    for (uint32_t i = 0; i < 16; i++)
        w[i] = ((uint32_t)blk[i*4] << 24) | ((uint32_t)blk[i*4+1] << 16) |
               ((uint32_t)blk[i*4+2] << 8) | (uint32_t)blk[i*4+3];
    for (uint32_t i = 16; i < 80; i++)
        w[i] = rol32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (uint32_t i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | ((~b) & d);            k = 0x5A827999u; }
        else if (i < 40) { f = b ^ c ^ d;                       k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d);     k = 0x8F1BBCDCu; }
        else             { f = b ^ c ^ d;                       k = 0xCA62C1D6u; }
        uint32_t t = rol32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol32(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void wifi_sha1_init(wifi_sha1_ctx_t *c) {
    if (!c) return;
    c->h[0] = 0x67452301u; c->h[1] = 0xEFCDAB89u; c->h[2] = 0x98BADCFEu;
    c->h[3] = 0x10325476u; c->h[4] = 0xC3D2E1F0u;
    c->buf_len = 0;
    c->total = 0;
}

void wifi_sha1_update(wifi_sha1_ctx_t *c, const uint8_t *data, uint32_t len) {
    if (!c || (!data && len)) return;
    c->total += len;
    while (len) {
        uint32_t take = 64 - c->buf_len;
        if (take > len) take = len;
        w_memcpy(c->buf + c->buf_len, data, take);
        c->buf_len += take;
        data += take;
        len  -= take;
        if (c->buf_len == 64) { sha1_compress(c->h, c->buf); c->buf_len = 0; }
    }
}

void wifi_sha1_final(wifi_sha1_ctx_t *c, uint8_t out[WIFI_SHA1_LEN]) {
    if (!c || !out) return;
    uint64_t bits = c->total * 8u;
    uint8_t pad = 0x80;
    wifi_sha1_update(c, &pad, 1);
    uint8_t zero = 0;
    while (c->buf_len != 56) wifi_sha1_update(c, &zero, 1);
    uint8_t lenbuf[8];
    for (int i = 7; i >= 0; i--) { lenbuf[i] = (uint8_t)bits; bits >>= 8; }
    /* update() would re-count these 8 bytes into total, but total is no
     * longer read after this point; the block still hashes correctly. */
    w_memcpy(c->buf + 56, lenbuf, 8);
    sha1_compress(c->h, c->buf);
    c->buf_len = 0;
    for (uint32_t i = 0; i < 5; i++) {
        out[i*4]   = (uint8_t)(c->h[i] >> 24);
        out[i*4+1] = (uint8_t)(c->h[i] >> 16);
        out[i*4+2] = (uint8_t)(c->h[i] >> 8);
        out[i*4+3] = (uint8_t)(c->h[i]);
    }
}

void wifi_sha1(const uint8_t *data, uint32_t len, uint8_t out[WIFI_SHA1_LEN]) {
    wifi_sha1_ctx_t c;
    wifi_sha1_init(&c);
    wifi_sha1_update(&c, data, len);
    wifi_sha1_final(&c, out);
}

/* ===================================================================== */
/* HMAC-SHA1 (RFC 2104) — streaming for the same reason as SHA-1          */
/* ===================================================================== */

void wifi_hmac_sha1_init(wifi_hmac_sha1_ctx_t *c, const uint8_t *key, uint32_t klen) {
    if (!c) return;
    uint8_t k[64];
    w_memset(k, 0, sizeof k);
    if (key && klen > 64) {
        uint8_t kd[WIFI_SHA1_LEN];
        wifi_sha1(key, klen, kd);
        w_memcpy(k, kd, WIFI_SHA1_LEN);
    } else if (key && klen) {
        w_memcpy(k, key, klen);
    }
    uint8_t ipad[64];
    for (uint32_t i = 0; i < 64; i++) {
        ipad[i]    = (uint8_t)(k[i] ^ 0x36);
        c->opad[i] = (uint8_t)(k[i] ^ 0x5C);
    }
    wifi_sha1_init(&c->inner);
    wifi_sha1_update(&c->inner, ipad, 64);
    w_memset(k, 0, sizeof k);
    w_memset(ipad, 0, sizeof ipad);
}

void wifi_hmac_sha1_update(wifi_hmac_sha1_ctx_t *c, const uint8_t *data, uint32_t len) {
    if (!c) return;
    wifi_sha1_update(&c->inner, data, len);
}

void wifi_hmac_sha1_final(wifi_hmac_sha1_ctx_t *c, uint8_t out[WIFI_SHA1_LEN]) {
    if (!c || !out) return;
    uint8_t ih[WIFI_SHA1_LEN];
    wifi_sha1_final(&c->inner, ih);
    wifi_sha1_ctx_t outer;
    wifi_sha1_init(&outer);
    wifi_sha1_update(&outer, c->opad, 64);
    wifi_sha1_update(&outer, ih, WIFI_SHA1_LEN);
    wifi_sha1_final(&outer, out);
    w_memset(c->opad, 0, sizeof c->opad);
    w_memset(ih, 0, sizeof ih);
}

void wifi_hmac_sha1(const uint8_t *key, uint32_t klen,
                    const uint8_t *msg, uint32_t mlen, uint8_t out[WIFI_SHA1_LEN]) {
    wifi_hmac_sha1_ctx_t c;
    wifi_hmac_sha1_init(&c, key, klen);
    wifi_hmac_sha1_update(&c, msg, mlen);
    wifi_hmac_sha1_final(&c, out);
}

/* ===================================================================== */
/* PBKDF2-HMAC-SHA1 (RFC 2898). Anchored to RFC 6070 in test_wifi.c.      */
/* ===================================================================== */

#define WIFI_PBKDF2_MAX_OUT 64

int wifi_pbkdf2_sha1(const uint8_t *pass, uint32_t plen,
                     const uint8_t *salt, uint32_t slen,
                     uint32_t iters, uint8_t *out, uint32_t outlen) {
    if (!pass || !salt || !out) return WIFI_EINVAL;
    if (iters == 0 || outlen == 0 || outlen > WIFI_PBKDF2_MAX_OUT) return WIFI_EINVAL;

    uint32_t blocks = (outlen + WIFI_SHA1_LEN - 1) / WIFI_SHA1_LEN;
    for (uint32_t b = 1; b <= blocks; b++) {
        uint8_t ctr[4];
        ctr[0] = (uint8_t)(b >> 24); ctr[1] = (uint8_t)(b >> 16);
        ctr[2] = (uint8_t)(b >> 8);  ctr[3] = (uint8_t)b;

        uint8_t u[WIFI_SHA1_LEN], t[WIFI_SHA1_LEN];
        wifi_hmac_sha1_ctx_t h;
        wifi_hmac_sha1_init(&h, pass, plen);
        wifi_hmac_sha1_update(&h, salt, slen);
        wifi_hmac_sha1_update(&h, ctr, 4);
        wifi_hmac_sha1_final(&h, u);
        w_memcpy(t, u, WIFI_SHA1_LEN);

        for (uint32_t i = 1; i < iters; i++) {
            wifi_hmac_sha1(pass, plen, u, WIFI_SHA1_LEN, u);
            for (uint32_t j = 0; j < WIFI_SHA1_LEN; j++) t[j] ^= u[j];
        }

        uint32_t off = (b - 1) * WIFI_SHA1_LEN;
        uint32_t n = outlen - off;
        if (n > WIFI_SHA1_LEN) n = WIFI_SHA1_LEN;
        w_memcpy(out + off, t, n);
        w_memset(u, 0, sizeof u);
        w_memset(t, 0, sizeof t);
    }
    return WIFI_OK;
}

/* ===================================================================== */
/* WPA2 key derivation                                                     */
/* ===================================================================== */

int wifi_wpa_pmk(const char *passphrase, const char *ssid, uint8_t pmk[WIFI_PMK_LEN]) {
    if (!passphrase || !ssid || !pmk) return WIFI_EINVAL;
    uint32_t plen = w_strlen(passphrase);
    uint32_t slen = w_strlen(ssid);
    /* IEEE 802.11i: 8..63 characters, SSID 1..32 octets. Anything outside
     * that is refused rather than silently stretched. */
    if (plen < 8 || plen > 63) return WIFI_EINVAL;
    if (slen < 1 || slen > 32) return WIFI_EINVAL;
    return wifi_pbkdf2_sha1((const uint8_t *)passphrase, plen,
                            (const uint8_t *)ssid, slen,
                            4096, pmk, WIFI_PMK_LEN);
}

int wifi_wpa_prf(const uint8_t *key, uint32_t klen, const char *label,
                 const uint8_t *data, uint32_t dlen, uint8_t *out, uint32_t outlen) {
    if (!key || !label || !out) return WIFI_EINVAL;
    if (!data && dlen) return WIFI_EINVAL;
    if (outlen == 0 || outlen > 64) return WIFI_EINVAL;

    uint32_t llen = w_strlen(label);
    uint32_t pos = 0;
    for (uint32_t i = 0; pos < outlen; i++) {
        uint8_t counter = (uint8_t)i;      /* PRF is defined for i = 0..255 */
        uint8_t sep = 0;
        uint8_t digest[WIFI_SHA1_LEN];
        wifi_hmac_sha1_ctx_t h;
        wifi_hmac_sha1_init(&h, key, klen);
        wifi_hmac_sha1_update(&h, (const uint8_t *)label, llen);
        wifi_hmac_sha1_update(&h, &sep, 1);
        if (dlen) wifi_hmac_sha1_update(&h, data, dlen);
        wifi_hmac_sha1_update(&h, &counter, 1);
        wifi_hmac_sha1_final(&h, digest);

        uint32_t n = outlen - pos;
        if (n > WIFI_SHA1_LEN) n = WIFI_SHA1_LEN;
        w_memcpy(out + pos, digest, n);
        pos += n;
        w_memset(digest, 0, sizeof digest);
    }
    return WIFI_OK;
}

int wifi_wpa_derive_ptk(const uint8_t pmk[WIFI_PMK_LEN],
                        const uint8_t aa[6], const uint8_t spa[6],
                        const uint8_t anonce[32], const uint8_t snonce[32],
                        wifi_ptk_t *ptk) {
    if (!pmk || !aa || !spa || !anonce || !snonce || !ptk) return WIFI_EINVAL;

    /* B = Min(AA,SPA) || Max(AA,SPA) || Min(ANonce,SNonce) || Max(...).
     * The canonical ordering is what lets both ends derive the same key
     * without agreeing on who is "first". */
    uint8_t b[76];
    int mac_cmp = w_memcmp(aa, spa, 6);
    const uint8_t *mac_lo = (mac_cmp <= 0) ? aa : spa;
    const uint8_t *mac_hi = (mac_cmp <= 0) ? spa : aa;
    w_memcpy(b + 0, mac_lo, 6);
    w_memcpy(b + 6, mac_hi, 6);

    int nonce_cmp = w_memcmp(anonce, snonce, 32);
    const uint8_t *n_lo = (nonce_cmp <= 0) ? anonce : snonce;
    const uint8_t *n_hi = (nonce_cmp <= 0) ? snonce : anonce;
    w_memcpy(b + 12, n_lo, 32);
    w_memcpy(b + 44, n_hi, 32);

    uint8_t material[WIFI_PTK_LEN];
    int rc = wifi_wpa_prf(pmk, WIFI_PMK_LEN, "Pairwise key expansion",
                          b, sizeof b, material, WIFI_PTK_LEN);
    if (rc != WIFI_OK) return rc;

    w_memcpy(ptk->kck, material + 0,  16);
    w_memcpy(ptk->kek, material + 16, 16);
    w_memcpy(ptk->tk,  material + 32, 16);
    w_memset(material, 0, sizeof material);
    w_memset(b, 0, sizeof b);
    return WIFI_OK;
}

/* ===================================================================== */
/* EAPOL-Key frames (IEEE 802.1X-2004 §7.6 / 802.11i §8.5.2)              */
/* ===================================================================== */

int wifi_eapol_key_parse(const uint8_t *frame, uint32_t len, wifi_eapol_key_t *out) {
    if (!frame || !out) return WIFI_EINVAL;
    if (len < WIFI_EAPOL_KEY_FIXED) return WIFI_EINVAL;
    if (frame[1] != WIFI_EAPOL_TYPE_KEY) return WIFI_EINVAL;

    uint16_t body = rd16be(frame + 2);
    if (body < WIFI_EAPOL_KEY_FIXED - WIFI_EAPOL_HDR_LEN) return WIFI_EINVAL;
    if ((uint32_t)body + WIFI_EAPOL_HDR_LEN > len) return WIFI_EINVAL;

    w_memset(out, 0, sizeof(*out));
    out->descriptor_type = frame[4];
    out->key_info        = rd16be(frame + 5);
    out->key_length      = rd16be(frame + 7);
    out->replay_counter  = rd64be(frame + 9);
    w_memcpy(out->nonce, frame + 17, 32);
    w_memcpy(out->rsc,   frame + 65, 8);
    w_memcpy(out->mic,   frame + WIFI_EAPOL_MIC_OFF, 16);
    out->key_data_len    = rd16be(frame + 97);
    out->key_data_off    = WIFI_EAPOL_KEY_FIXED;

    /* The declared body length and the declared key-data length must agree,
     * and both must fit inside the buffer we were handed. A frame that lies
     * about either is rejected, not clamped. */
    if ((uint32_t)WIFI_EAPOL_KEY_FIXED + out->key_data_len > len) return WIFI_EINVAL;
    if ((uint32_t)WIFI_EAPOL_HDR_LEN + body !=
        (uint32_t)WIFI_EAPOL_KEY_FIXED + out->key_data_len) return WIFI_EINVAL;
    out->total_len = (uint32_t)WIFI_EAPOL_KEY_FIXED + out->key_data_len;

    /* Classify. Only the pairwise handshake is recognised; the group-key
     * handshake leaves msg = 0 and is refused higher up. */
    bool pairwise = (out->key_info & WIFI_KI_PAIRWISE) != 0;
    bool ack      = (out->key_info & WIFI_KI_ACK) != 0;
    bool mic      = (out->key_info & WIFI_KI_MIC) != 0;
    bool secure   = (out->key_info & WIFI_KI_SECURE) != 0;
    if (!pairwise)            out->msg = 0;
    else if (ack && !mic)     out->msg = 1;
    else if (ack && mic)      out->msg = 3;
    else if (mic && !secure)  out->msg = 2;
    else if (mic && secure)   out->msg = 4;
    else                      out->msg = 0;
    return WIFI_OK;
}

int wifi_eapol_key_build(const wifi_eapol_key_t *k, const uint8_t *key_data,
                         uint8_t *out, uint32_t cap) {
    if (!k || !out) return WIFI_EINVAL;
    if (k->key_data_len && !key_data) return WIFI_EINVAL;
    uint32_t total = (uint32_t)WIFI_EAPOL_KEY_FIXED + k->key_data_len;
    if (cap < total) return WIFI_EMSGSIZE;

    w_memset(out, 0, total);
    out[0] = 2;                          /* 802.1X-2004 */
    out[1] = WIFI_EAPOL_TYPE_KEY;
    wr16be(out + 2, (uint16_t)(total - WIFI_EAPOL_HDR_LEN));
    out[4] = k->descriptor_type;
    wr16be(out + 5, k->key_info);
    wr16be(out + 7, k->key_length);
    wr64be(out + 9, k->replay_counter);
    w_memcpy(out + 17, k->nonce, 32);
    w_memcpy(out + 65, k->rsc, 8);
    w_memcpy(out + WIFI_EAPOL_MIC_OFF, k->mic, 16);
    wr16be(out + 97, k->key_data_len);
    if (k->key_data_len) w_memcpy(out + WIFI_EAPOL_KEY_FIXED, key_data, k->key_data_len);
    return (int)total;
}

int wifi_eapol_mic(const uint8_t kck[16], const uint8_t *frame, uint32_t len,
                   uint16_t key_desc_version, uint8_t mic[16]) {
    if (!kck || !frame || !mic) return WIFI_EINVAL;
    if (len < WIFI_EAPOL_KEY_FIXED) return WIFI_EINVAL;
    /* Version 1 is HMAC-MD5 and version 3 is AES-128-CMAC. Neither primitive
     * exists in this tree, and a MIC we cannot compute is not a MIC we may
     * pretend to have computed. */
    if (key_desc_version != 2) return WIFI_ENOTSUP;

    /* HMAC-SHA1 over the whole frame with the MIC field read as 16 zeros.
     * Streaming means we never have to copy the frame to do that. */
    uint8_t zeros[16];
    w_memset(zeros, 0, sizeof zeros);
    uint8_t digest[WIFI_SHA1_LEN];
    wifi_hmac_sha1_ctx_t h;
    wifi_hmac_sha1_init(&h, kck, 16);
    wifi_hmac_sha1_update(&h, frame, WIFI_EAPOL_MIC_OFF);
    wifi_hmac_sha1_update(&h, zeros, 16);
    wifi_hmac_sha1_update(&h, frame + WIFI_EAPOL_MIC_OFF + 16,
                          len - WIFI_EAPOL_MIC_OFF - 16);
    wifi_hmac_sha1_final(&h, digest);
    w_memcpy(mic, digest, 16);           /* HMAC-SHA1-128: truncate to 16 */
    w_memset(digest, 0, sizeof digest);
    return WIFI_OK;
}

bool wifi_eapol_mic_verify(const uint8_t kck[16], const uint8_t *frame, uint32_t len,
                           uint16_t key_desc_version) {
    uint8_t want[16];
    if (wifi_eapol_mic(kck, frame, len, key_desc_version, want) != WIFI_OK) return false;
    return w_ct_equal(want, frame + WIFI_EAPOL_MIC_OFF, 16);
}

/* The RSN information element a WPA2-PSK/CCMP supplicant advertises. Also
 * used verbatim in our own beacons. */
static const uint8_t WIFI_RSNE_PSK_CCMP[22] = {
    WIFI_EID_RSN, 20,
    0x01, 0x00,                         /* version 1 */
    0x00, 0x0F, 0xAC, 0x04,             /* group cipher: CCMP */
    0x01, 0x00, 0x00, 0x0F, 0xAC, 0x04, /* 1 pairwise cipher: CCMP */
    0x01, 0x00, 0x00, 0x0F, 0xAC, 0x02, /* 1 AKM: PSK */
    0x00, 0x00                          /* RSN capabilities */
};

/* ===================================================================== */
/* Channel <-> frequency                                                   */
/* ===================================================================== */

/* 20 MHz operating channels actually defined for 5 GHz. Arithmetic alone
 * would happily "convert" channel 7 to 5035 MHz, which is not a channel any
 * radio operates on, so the set is spelled out. */
static const uint16_t WIFI_5G_CHANNELS[] = {
    32, 36, 40, 44, 48, 52, 56, 60, 64, 68, 96,
    100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
    149, 153, 157, 161, 165, 169, 173, 177
};
#define WIFI_5G_COUNT (sizeof(WIFI_5G_CHANNELS)/sizeof(WIFI_5G_CHANNELS[0]))

static bool ch_valid_5g(uint32_t ch) {
    for (uint32_t i = 0; i < WIFI_5G_COUNT; i++)
        if (WIFI_5G_CHANNELS[i] == ch) return true;
    return false;
}

/* 6 GHz: channel 2 sits alone at 5935 MHz; the rest are 1, 5, 9, ... 233. */
static bool ch_valid_6g(uint32_t ch) {
    if (ch == 2) return true;
    if (ch < 1 || ch > 233) return false;
    return ((ch - 1) % 4) == 0;
}

static bool ch_valid_24g(uint32_t ch) { return ch >= 1 && ch <= 14; }

bool wifi_channel_valid(uint32_t channel, wifi_band_t band) {
    switch (band) {
        case WIFI_BAND_2_4GHZ: return ch_valid_24g(channel);
        case WIFI_BAND_5GHZ:   return ch_valid_5g(channel);
        case WIFI_BAND_6GHZ:   return ch_valid_6g(channel);
        case WIFI_BAND_DUAL:
        case WIFI_BAND_TRI:
            /* Documented resolution: 1..14 means 2.4 GHz, >= 32 means 5 GHz.
             * 6 GHz cannot be named this way — use wifi_set_freq(). */
            return ch_valid_24g(channel) || ch_valid_5g(channel);
        default: return false;
    }
}

uint32_t wifi_channel_to_freq(uint32_t channel, wifi_band_t band) {
    if (band == WIFI_BAND_DUAL || band == WIFI_BAND_TRI)
        band = ch_valid_24g(channel) ? WIFI_BAND_2_4GHZ : WIFI_BAND_5GHZ;
    switch (band) {
        case WIFI_BAND_2_4GHZ:
            if (channel == 14) return 2484;
            if (channel >= 1 && channel <= 13) return 2407 + channel * 5;
            return 0;
        case WIFI_BAND_5GHZ:
            if (!ch_valid_5g(channel)) return 0;
            return 5000 + channel * 5;
        case WIFI_BAND_6GHZ:
            if (channel == 2) return 5935;
            if (!ch_valid_6g(channel)) return 0;
            return 5950 + channel * 5;
        default: return 0;
    }
}

uint32_t wifi_freq_to_channel(uint32_t freq_mhz) {
    if (freq_mhz == 2484) return 14;
    if (freq_mhz >= 2412 && freq_mhz <= 2472 && ((freq_mhz - 2412) % 5) == 0)
        return 1 + (freq_mhz - 2412) / 5;
    if (freq_mhz == 5935) return 2;                       /* 6 GHz, the odd one */
    if (freq_mhz >= 5160 && freq_mhz <= 5885 && ((freq_mhz - 5000) % 5) == 0) {
        uint32_t ch = (freq_mhz - 5000) / 5;
        return ch_valid_5g(ch) ? ch : 0;
    }
    if (freq_mhz >= 5955 && freq_mhz <= 7115 && ((freq_mhz - 5950) % 5) == 0) {
        uint32_t ch = (freq_mhz - 5950) / 5;
        /* Channel 2 is NOT at 5950 + 2*5 = 5960; it sits alone at 5935, which
         * the special case above already answered. Letting the arithmetic
         * branch claim 5960 would return a channel whose own centre frequency
         * is a different number — the round trip would not close, and a driver
         * would be handed the contradictory pair (channel 2, 5960 MHz). */
        if (ch == 2) return 0;
        return ch_valid_6g(ch) ? ch : 0;
    }
    return 0;
}

int wifi_freq_to_band(uint32_t freq_mhz) {
    if (freq_mhz >= 2412 && freq_mhz <= 2484) return (int)WIFI_BAND_2_4GHZ;
    if (freq_mhz >= 5160 && freq_mhz <= 5885) return (int)WIFI_BAND_5GHZ;
    if (freq_mhz == 5935 || (freq_mhz >= 5955 && freq_mhz <= 7115))
        return (int)WIFI_BAND_6GHZ;
    return WIFI_EINVAL;
}

/* ===================================================================== */
/* SSID / BSSID helpers                                                   */
/* ===================================================================== */

int wifi_ssid_set(char *dst33, const char *src) {
    if (!dst33 || !src) return WIFI_EINVAL;
    uint32_t n = w_strlen(src);
    if (n > 32) return WIFI_EINVAL;      /* 802.11 caps the SSID at 32 octets */
    w_memcpy(dst33, src, n);
    dst33[n] = '\0';
    return (int)n;
}

bool wifi_ssid_equal(const char *a, const char *b) {
    if (!a || !b) return false;
    uint32_t i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return false; i++; }
    return a[i] == b[i];
}

bool wifi_bssid_is_zero(const uint8_t bssid[6]) {
    if (!bssid) return false;
    for (int i = 0; i < 6; i++) if (bssid[i]) return false;
    return true;
}

bool wifi_bssid_is_broadcast(const uint8_t bssid[6]) {
    if (!bssid) return false;
    return w_memcmp(bssid, WIFI_BCAST, 6) == 0;
}

int wifi_bssid_format(const uint8_t bssid[6], char *out, uint32_t cap) {
    static const char hex[] = "0123456789abcdef";
    if (!bssid || !out) return WIFI_EINVAL;
    if (cap < 18) return WIFI_EMSGSIZE;
    uint32_t p = 0;
    for (int i = 0; i < 6; i++) {
        out[p++] = hex[(bssid[i] >> 4) & 0xF];
        out[p++] = hex[bssid[i] & 0xF];
        if (i != 5) out[p++] = ':';
    }
    out[p] = '\0';
    return (int)p;                       /* 17 */
}

/* ===================================================================== */
/* 802.11 MAC header                                                       */
/* ===================================================================== */

static bool hdr_is_qos(const wifi_mac_hdr_t *h) {
    return h->type == WIFI_FTYPE_DATA && (h->subtype & 0x08) != 0;
}

uint32_t wifi_mac_hdr_len(const wifi_mac_hdr_t *h) {
    if (!h) return 0;
    uint32_t n = WIFI_MAC_HDR_MIN;
    if (h->to_ds && h->from_ds) n += 6;   /* addr4 */
    if (hdr_is_qos(h)) n += 2;            /* QoS control */
    return n;
}

int wifi_mac_hdr_build(const wifi_mac_hdr_t *h, uint8_t *out, uint32_t cap) {
    if (!h || !out) return WIFI_EINVAL;
    if (h->type > 3 || h->subtype > 15) return WIFI_EINVAL;
    if (h->seq_num >= WIFI_SEQ_MODULO || h->frag_num > 15) return WIFI_EINVAL;
    /* The +HTC variant adds a four-byte HT Control field we neither build nor
     * parse; refusing is the only way not to mis-size the header (L8). */
    if (h->order) return WIFI_ENOTSUP;

    uint32_t n = wifi_mac_hdr_len(h);
    if (cap < n) return WIFI_EMSGSIZE;

    out[0] = (uint8_t)(((h->type & 0x3) << 2) | ((h->subtype & 0xF) << 4));
    uint8_t fc1 = 0;
    if (h->to_ds)           fc1 |= 0x01;
    if (h->from_ds)         fc1 |= 0x02;
    if (h->more_frag)       fc1 |= 0x04;
    if (h->retry)           fc1 |= 0x08;
    if (h->pwr_mgmt)        fc1 |= 0x10;
    if (h->more_data)       fc1 |= 0x20;
    if (h->protected_frame) fc1 |= 0x40;
    out[1] = fc1;

    wr16le(out + 2, h->duration_id);
    w_memcpy(out + 4,  h->addr1, 6);
    w_memcpy(out + 10, h->addr2, 6);
    w_memcpy(out + 16, h->addr3, 6);
    /* Sequence control: fragment number in bits 0..3, sequence in 4..15. */
    wr16le(out + 22, (uint16_t)((h->frag_num & 0x0F) | (uint16_t)(h->seq_num << 4)));

    uint32_t off = 24;
    if (h->to_ds && h->from_ds) { w_memcpy(out + off, h->addr4, 6); off += 6; }
    if (hdr_is_qos(h))          { wr16le(out + off, h->qos_ctl);    off += 2; }
    return (int)off;
}

int wifi_mac_hdr_parse(const uint8_t *in, uint32_t len, wifi_mac_hdr_t *out) {
    if (!in || !out) return WIFI_EINVAL;
    if (len < WIFI_MAC_HDR_MIN) return WIFI_EINVAL;

    w_memset(out, 0, sizeof(*out));
    uint8_t fc0 = in[0], fc1 = in[1];
    if ((fc0 & 0x03) != 0) return WIFI_ENOTSUP;      /* protocol version != 0 */
    out->type    = (uint8_t)((fc0 >> 2) & 0x03);
    out->subtype = (uint8_t)((fc0 >> 4) & 0x0F);
    out->to_ds           = (fc1 & 0x01) != 0;
    out->from_ds         = (fc1 & 0x02) != 0;
    out->more_frag       = (fc1 & 0x04) != 0;
    out->retry           = (fc1 & 0x08) != 0;
    out->pwr_mgmt        = (fc1 & 0x10) != 0;
    out->more_data       = (fc1 & 0x20) != 0;
    out->protected_frame = (fc1 & 0x40) != 0;
    out->order           = (fc1 & 0x80) != 0;
    if (out->order) return WIFI_ENOTSUP;             /* see L8 */

    out->duration_id = rd16le(in + 2);
    w_memcpy(out->addr1, in + 4,  6);
    w_memcpy(out->addr2, in + 10, 6);
    w_memcpy(out->addr3, in + 16, 6);
    uint16_t sc = rd16le(in + 22);
    out->frag_num = (uint8_t)(sc & 0x0F);
    out->seq_num  = (uint16_t)(sc >> 4);

    uint32_t off = WIFI_MAC_HDR_MIN;
    if (out->to_ds && out->from_ds) {
        if (len < off + 6) return WIFI_EINVAL;
        w_memcpy(out->addr4, in + off, 6);
        out->has_addr4 = true;
        off += 6;
    }
    if (hdr_is_qos(out)) {
        if (len < off + 2) return WIFI_EINVAL;
        out->qos = true;
        out->qos_ctl = rd16le(in + off);
        off += 2;
    }
    return (int)off;
}

int wifi_frame_addrs(const wifi_mac_hdr_t *h, uint8_t da[6], uint8_t sa[6],
                     uint8_t bssid[6]) {
    if (!h || !da || !sa || !bssid) return WIFI_EINVAL;
    if (!h->to_ds && !h->from_ds) {          /* IBSS / management */
        w_memcpy(da, h->addr1, 6);
        w_memcpy(sa, h->addr2, 6);
        w_memcpy(bssid, h->addr3, 6);
    } else if (!h->to_ds && h->from_ds) {    /* from the AP */
        w_memcpy(da, h->addr1, 6);
        w_memcpy(bssid, h->addr2, 6);
        w_memcpy(sa, h->addr3, 6);
    } else if (h->to_ds && !h->from_ds) {    /* to the AP */
        w_memcpy(bssid, h->addr1, 6);
        w_memcpy(sa, h->addr2, 6);
        w_memcpy(da, h->addr3, 6);
    } else {
        /* WDS: addr1=RA, addr2=TA, addr3=DA, addr4=SA. There are two BSSIDs
         * involved and neither is "the" BSSID, so this is refused rather
         * than answered with a plausible-looking wrong address. */
        return WIFI_ENOTSUP;
    }
    return WIFI_OK;
}

uint16_t wifi_next_seq(wifi_interface_t *iface) {
    if (!iface) return 0;
    uint16_t s = iface->seq_num;
    iface->seq_num = (uint16_t)((iface->seq_num + 1) % WIFI_SEQ_MODULO);
    return s;
}

/* ===================================================================== */
/* Management frame builders                                              */
/* ===================================================================== */

static int build_mgmt_hdr(uint8_t *out, uint32_t cap, uint8_t subtype,
                          const uint8_t da[6], const uint8_t sa[6],
                          const uint8_t bssid[6], uint16_t seq) {
    wifi_mac_hdr_t h;
    w_memset(&h, 0, sizeof h);
    h.type = WIFI_FTYPE_MGMT;
    h.subtype = subtype;
    h.seq_num = (uint16_t)(seq % WIFI_SEQ_MODULO);
    w_memcpy(h.addr1, da, 6);
    w_memcpy(h.addr2, sa, 6);
    w_memcpy(h.addr3, bssid, 6);
    return wifi_mac_hdr_build(&h, out, cap);
}

int wifi_build_auth(uint8_t *out, uint32_t cap, const uint8_t bssid[6],
                    const uint8_t sa[6], uint16_t seq, uint16_t auth_alg,
                    uint16_t auth_seq, uint16_t status) {
    if (!out || !bssid || !sa) return WIFI_EINVAL;
    int n = build_mgmt_hdr(out, cap, WIFI_STYPE_AUTH, bssid, sa, bssid, seq);
    if (n < 0) return n;
    if (cap < (uint32_t)n + 6) return WIFI_EMSGSIZE;
    wr16le(out + n,     auth_alg);       /* 0 = Open System */
    wr16le(out + n + 2, auth_seq);
    wr16le(out + n + 4, status);
    return n + 6;
}

int wifi_build_assoc_req(uint8_t *out, uint32_t cap, const uint8_t bssid[6],
                         const uint8_t sa[6], uint16_t seq, const char *ssid,
                         uint16_t cap_info, uint16_t listen_interval) {
    if (!out || !bssid || !sa || !ssid) return WIFI_EINVAL;
    uint32_t slen = w_strlen(ssid);
    if (slen == 0 || slen > 32) return WIFI_EINVAL;

    int n = build_mgmt_hdr(out, cap, WIFI_STYPE_ASSOC_REQ, bssid, sa, bssid, seq);
    if (n < 0) return n;
    uint32_t off = (uint32_t)n;
    uint32_t need = off + 4 + (2 + slen) + (2 + 8);
    if (cap < need) return WIFI_EMSGSIZE;

    wr16le(out + off, cap_info);         off += 2;
    wr16le(out + off, listen_interval);  off += 2;
    out[off++] = WIFI_EID_SSID;
    out[off++] = (uint8_t)slen;
    w_memcpy(out + off, ssid, slen);     off += slen;
    out[off++] = WIFI_EID_RATES;
    out[off++] = 8;
    out[off++] = 0x82; out[off++] = 0x84; out[off++] = 0x8B; out[off++] = 0x96;
    out[off++] = 0x0C; out[off++] = 0x12; out[off++] = 0x18; out[off++] = 0x24;
    return (int)off;
}

int wifi_build_deauth(uint8_t *out, uint32_t cap, const uint8_t da[6],
                      const uint8_t sa[6], const uint8_t bssid[6], uint16_t seq,
                      uint16_t reason) {
    if (!out || !da || !sa || !bssid) return WIFI_EINVAL;
    int n = build_mgmt_hdr(out, cap, WIFI_STYPE_DEAUTH, da, sa, bssid, seq);
    if (n < 0) return n;
    if (cap < (uint32_t)n + 2) return WIFI_EMSGSIZE;
    wr16le(out + n, reason);
    return n + 2;
}

/* ===================================================================== */
/* Information elements and beacon parsing                                */
/* ===================================================================== */

int wifi_find_ie(const uint8_t *ies, uint32_t len, uint8_t id, const uint8_t **val) {
    if (!ies || !val) return WIFI_EINVAL;
    uint32_t p = 0;
    while (p + 2 <= len) {
        uint8_t eid = ies[p];
        uint8_t elen = ies[p + 1];
        /* An element that claims to run past the end of the buffer means the
         * whole list is untrustworthy; stop, do not clamp. */
        if (p + 2u + elen > len) return WIFI_EINVAL;
        if (eid == id) { *val = ies + p + 2; return (int)elen; }
        p += 2u + elen;
    }
    if (p != len) return WIFI_EINVAL;    /* trailing byte that is not an element */
    return WIFI_EAGAIN;                  /* well formed, simply not present */
}

/* Decide the security suite from an RSN element body. */
static wifi_security_t rsn_security(const uint8_t *rsn, uint32_t len) {
    static const uint8_t OUI[3] = {0x00, 0x0F, 0xAC};
    if (len < 2) return WIFI_SEC_WPA2;   /* malformed but present: assume RSN */
    uint32_t p = 2;                      /* skip version */
    if (p + 4 > len) return WIFI_SEC_WPA2;
    p += 4;                              /* group cipher */
    if (p + 2 > len) return WIFI_SEC_WPA2;
    uint16_t pc = rd16le(rsn + p); p += 2;
    if (p + (uint32_t)pc * 4 > len) return WIFI_SEC_WPA2;
    p += (uint32_t)pc * 4;               /* pairwise ciphers */
    if (p + 2 > len) return WIFI_SEC_WPA2;
    uint16_t ac = rd16le(rsn + p); p += 2;
    if (p + (uint32_t)ac * 4 > len) return WIFI_SEC_WPA2;

    bool sae = false, psk_or_dot1x = false;
    for (uint16_t i = 0; i < ac; i++) {
        const uint8_t *s = rsn + p + (uint32_t)i * 4;
        if (w_memcmp(s, OUI, 3) != 0) continue;
        switch (s[3]) {
            case 8: case 9:  sae = true; break;             /* SAE / FT-SAE */
            case 1: case 2: case 3: case 4: case 5: case 6:
                psk_or_dot1x = true; break;
            default: break;
        }
    }
    if (sae && psk_or_dot1x) return WIFI_SEC_WPA2_WPA3;
    if (sae) return WIFI_SEC_WPA3;
    return WIFI_SEC_WPA2;
}

static uint32_t rates_max_mbps(const uint8_t *r, uint32_t len, uint32_t cur) {
    for (uint32_t i = 0; i < len; i++) {
        uint32_t half = (uint32_t)(r[i] & 0x7F);   /* units of 500 kbps */
        uint32_t mbps = half / 2;
        if (mbps > cur) cur = mbps;
    }
    return cur;
}

int wifi_parse_beacon(const uint8_t *frame, uint32_t len, int16_t rssi,
                      uint32_t freq_mhz, wifi_scan_result_t *out) {
    if (!frame || !out) return WIFI_EINVAL;
    wifi_mac_hdr_t h;
    int hl = wifi_mac_hdr_parse(frame, len, &h);
    if (hl < 0) return hl;
    if (h.type != WIFI_FTYPE_MGMT) return WIFI_ENOTSUP;
    if (h.subtype != WIFI_STYPE_BEACON && h.subtype != WIFI_STYPE_PROBE_RESP)
        return WIFI_ENOTSUP;
    /* timestamp(8) + beacon interval(2) + capability(2) */
    if (len < (uint32_t)hl + 12) return WIFI_EINVAL;

    w_memset(out, 0, sizeof(*out));
    w_memcpy(out->bssid, h.addr3, 6);
    out->rssi = rssi;

    uint16_t cap_info = rd16le(frame + hl + 10);
    bool privacy = (cap_info & 0x0010) != 0;

    const uint8_t *ies = frame + hl + 12;
    uint32_t ie_len = len - (uint32_t)hl - 12;

    /* Validate the whole element list once before trusting any of it. */
    {
        const uint8_t *dummy = 0;
        int probe = wifi_find_ie(ies, ie_len, 0xFE, &dummy);
        if (probe == WIFI_EINVAL) return WIFI_EINVAL;
    }

    const uint8_t *v = 0;
    int n;

    /* SSID (a zero-length SSID element is a hidden network, not an error) */
    n = wifi_find_ie(ies, ie_len, WIFI_EID_SSID, &v);
    if (n < 0) {
        out->hidden = true;
    } else if (n == 0) {
        out->hidden = true;
    } else {
        if (n > 32) return WIFI_EINVAL;
        w_memcpy(out->ssid, v, (uint32_t)n);
        out->ssid[n] = '\0';
    }

    /* Channel: prefer the frequency we actually heard it on. */
    uint32_t chan = 0;
    n = wifi_find_ie(ies, ie_len, WIFI_EID_DSPARAM, &v);
    if (n == 1) chan = v[0];
    if (freq_mhz) {
        uint32_t fch = wifi_freq_to_channel(freq_mhz);
        if (fch) chan = fch;
        int band = wifi_freq_to_band(freq_mhz);
        out->band = (band < 0) ? WIFI_BAND_2_4GHZ : (wifi_band_t)band;
    } else {
        out->band = (chan >= 1 && chan <= 14) ? WIFI_BAND_2_4GHZ : WIFI_BAND_5GHZ;
    }
    out->channel = chan;

    /* Rates — legacy only; see L7. */
    uint32_t maxrate = 0;
    n = wifi_find_ie(ies, ie_len, WIFI_EID_RATES, &v);
    if (n > 0) maxrate = rates_max_mbps(v, (uint32_t)n, maxrate);
    n = wifi_find_ie(ies, ie_len, WIFI_EID_EXT_RATES, &v);
    if (n > 0) maxrate = rates_max_mbps(v, (uint32_t)n, maxrate);
    out->max_rate_mbps = maxrate;

    /* PHY generation from the presence of the capability elements. */
    bool he = false, vht = false, ht = false;
    {
        uint32_t p = 0;
        while (p + 2 <= ie_len) {
            uint8_t eid = ies[p], elen = ies[p + 1];
            if (p + 2u + elen > ie_len) break;
            if (eid == WIFI_EID_HT_CAP)  ht = true;
            if (eid == WIFI_EID_VHT_CAP) vht = true;
            if (eid == WIFI_EID_EXTENSION && elen >= 1 && ies[p + 2] == WIFI_EID_EXT_HE_CAP)
                he = true;
            p += 2u + elen;
        }
    }
    if (he)        out->standard = WIFI_802_11AX;
    else if (vht)  out->standard = WIFI_802_11AC;
    else if (ht)   out->standard = WIFI_802_11N;
    else if (out->band != WIFI_BAND_2_4GHZ) out->standard = WIFI_802_11A;
    else if (maxrate > 11) out->standard = WIFI_802_11G;
    else out->standard = WIFI_802_11B;

    /* Security */
    n = wifi_find_ie(ies, ie_len, WIFI_EID_RSN, &v);
    if (n > 0) {
        out->security = rsn_security(v, (uint32_t)n);
    } else {
        bool wpa1 = false;
        uint32_t p = 0;
        while (p + 2 <= ie_len) {
            uint8_t eid = ies[p], elen = ies[p + 1];
            if (p + 2u + elen > ie_len) break;
            if (eid == WIFI_EID_VENDOR && elen >= 4 &&
                ies[p+2] == 0x00 && ies[p+3] == 0x50 && ies[p+4] == 0xF2 && ies[p+5] == 0x01)
                wpa1 = true;
            p += 2u + elen;
        }
        if (wpa1)         out->security = WIFI_SEC_WPA;
        else if (privacy) out->security = WIFI_SEC_WEP;
        else              out->security = WIFI_SEC_OPEN;
    }
    return WIFI_OK;
}

/* ===================================================================== */
/* Length-prefixed staging rings over the DMA buffers                      */
/* ===================================================================== */

static int ring_push(uint8_t *buf, uint32_t cap, uint32_t *head, uint32_t *used,
                     const uint8_t *data, uint32_t len) {
    if (!buf || !head || !used || !data) return WIFI_EINVAL;
    if (len == 0 || len > 0xFFFFu) return WIFI_EINVAL;
    if (len + 2u > cap - *used) return WIFI_ENOSPC;
    uint8_t lp[2];
    wr16le(lp, (uint16_t)len);
    for (uint32_t i = 0; i < 2; i++) { buf[*head] = lp[i]; *head = (*head + 1) % cap; }
    for (uint32_t i = 0; i < len; i++) { buf[*head] = data[i]; *head = (*head + 1) % cap; }
    *used += len + 2u;
    return WIFI_OK;
}

static int ring_peek_len(const uint8_t *buf, uint32_t cap, uint32_t tail, uint32_t used) {
    if (used < 2) return WIFI_EAGAIN;
    uint32_t l = (uint32_t)buf[tail] | ((uint32_t)buf[(tail + 1) % cap] << 8);
    if (l == 0 || l + 2u > used) return WIFI_EIO;   /* the ring is corrupt */
    return (int)l;
}

static int ring_pop(uint8_t *buf, uint32_t cap, uint32_t *tail, uint32_t *used,
                    uint8_t *out, uint32_t out_cap) {
    if (!buf || !tail || !used || !out) return WIFI_EINVAL;
    int l = ring_peek_len(buf, cap, *tail, *used);
    if (l < 0) return l;
    if ((uint32_t)l > out_cap) return WIFI_EMSGSIZE;   /* nothing is consumed */
    *tail = (*tail + 2) % cap;
    for (int i = 0; i < l; i++) { out[i] = buf[*tail]; *tail = (*tail + 1) % cap; }
    *used -= (uint32_t)l + 2u;
    return l;
}

/* ===================================================================== */
/* Device / interface lifecycle                                            */
/* ===================================================================== */

/* With no radio to ask, an interface still needs an address. This one is
 * deterministic, locally administered (bit 0x02), unicast, and flagged as a
 * placeholder — see L10. */
static void derive_placeholder_mac(const char *devname, uint32_t idx, uint8_t mac[6]) {
    uint32_t h = 2166136261u;
    for (const char *p = devname; p && *p; p++) {
        h ^= (uint8_t)*p;
        h *= 16777619u;
    }
    h ^= idx * 2654435761u;
    mac[0] = 0x02;
    mac[1] = (uint8_t)(h >> 24);
    mac[2] = (uint8_t)(h >> 16);
    mac[3] = (uint8_t)(h >> 8);
    mac[4] = (uint8_t)h;
    mac[5] = (uint8_t)(idx & 0xFF);
}

void wifi_init(wifi_device_t *dev, const char *name) {
    /* The ARM32 boot path calls wifi_init(0, "..."). Tolerate it. */
    if (!dev) return;
    w_memset(dev, 0, sizeof(*dev));
    w_strcpy(dev->name, name ? name : "wifi0", sizeof(dev->name));
    dev->device_id = 1;

    /* These capability fields describe what THIS SOFTWARE can drive, not a
     * chipset: we build and parse legacy-rate frames on any of the three
     * bands, we can beacon, and we cannot speak SAE. A real driver should
     * overwrite them with the radio's actual capabilities after binding. */
    dev->max_standard    = WIFI_802_11G;
    dev->max_band        = WIFI_BAND_TRI;
    dev->max_rate_mbps   = 54;
    dev->supports_ap     = true;
    dev->supports_mesh   = false;
    dev->supports_monitor= false;
    dev->supports_wpa3   = false;          /* no SAE — see L6 */
    dev->max_tx_power    = 0;              /* unknown without a radio */

    dev->ops = 0;
    dev->m5.omega = 1;
    dev->m5.r   = SR_FROM_INT(1);
    dev->m5.ell = SR_ZERO;
    dev->m5.phi = SR_ZERO;
    dev->m5.chi = 0;
    dev->coverage_r = 0.0;
    dev->coverage_l = 0.0;
}

wifi_interface_t *wifi_get_interface(wifi_device_t *dev, uint32_t iface_id) {
    if (!dev) return 0;
    if (iface_id == 0 || iface_id > dev->num_ifaces) return 0;
    if (iface_id > WIFI_MAX_INTERFACES) return 0;
    return &dev->ifaces[iface_id - 1];
}

uint32_t wifi_create_interface(wifi_device_t *dev, const char *name, wifi_mode_t mode) {
    if (!dev) return 0;
    if (dev->num_ifaces >= WIFI_MAX_INTERFACES) return 0;
    if ((int)mode < 0 || mode > WIFI_MODE_P2P) return 0;
    if (mode == WIFI_MODE_AP && !dev->supports_ap) return 0;
    if (mode == WIFI_MODE_MESH && !dev->supports_mesh) return 0;
    if (mode == WIFI_MODE_MONITOR && !dev->supports_monitor) return 0;

    uint32_t idx = dev->num_ifaces++;
    wifi_interface_t *f = &dev->ifaces[idx];
    w_memset(f, 0, sizeof(*f));
    f->iface_id = idx + 1;
    w_strcpy(f->name, name ? name : "wlan", sizeof(f->name));
    f->mode = mode;
    f->standard = dev->max_standard;
    f->band = WIFI_BAND_2_4GHZ;
    f->state = WIFI_STATE_IDLE;
    f->rssi = -127;                       /* no measurement yet */
    f->power_save_level = 0;
    f->wmm_enabled = 1;

    if (dev->ops && dev->ops->get_mac &&
        dev->ops->get_mac(dev->ops->ctx, idx, f->mac) >= 0) {
        f->mac_is_placeholder = false;
    } else {
        derive_placeholder_mac(dev->name, idx, f->mac);
        f->mac_is_placeholder = true;
    }
    return f->iface_id;
}

int wifi_set_mac(wifi_device_t *dev, uint32_t iface_id, const uint8_t mac[6]) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f || !mac) return WIFI_EINVAL;
    if (mac[0] & 0x01) return WIFI_EINVAL;    /* a station MAC is never multicast */
    w_memcpy(f->mac, mac, 6);
    f->mac_is_placeholder = false;
    return WIFI_OK;
}

int wifi_bind_ops(wifi_device_t *dev, const wifi_ops_t *ops) {
    if (!dev || !ops) return WIFI_EINVAL;
    /* A backend that can neither transmit nor receive is not a radio. */
    if (!ops->tx_frame || !ops->rx_poll) return WIFI_EINVAL;
    dev->ops = ops;
    /* Adopt real hardware addresses now that someone can supply them. */
    if (ops->get_mac) {
        for (uint32_t i = 0; i < dev->num_ifaces; i++) {
            uint8_t m[6];
            if (ops->get_mac(ops->ctx, i, m) >= 0 && !(m[0] & 0x01)) {
                w_memcpy(dev->ifaces[i].mac, m, 6);
                dev->ifaces[i].mac_is_placeholder = false;
            }
        }
    }
    return WIFI_OK;
}

void wifi_unbind_ops(wifi_device_t *dev) {
    if (!dev) return;
    dev->ops = 0;
    dev->scanning = false;
}

bool wifi_has_radio(const wifi_device_t *dev) {
    return dev && dev->ops && dev->ops->tx_frame && dev->ops->rx_poll;
}

/* ===================================================================== */
/* Association state machine                                               */
/* ===================================================================== */

bool wifi_state_can_transition(wifi_assoc_state_t from, wifi_assoc_state_t to) {
    if ((int)from < 0 || from >= WIFI_STATE__COUNT) return false;
    if ((int)to < 0 || to >= WIFI_STATE__COUNT) return false;
    if (from == to) return false;        /* a no-op "transition" hides bugs */
    switch (from) {
        case WIFI_STATE_IDLE:
            return to == WIFI_STATE_SCANNING || to == WIFI_STATE_AUTHENTICATING ||
                   to == WIFI_STATE_BEACONING;
        case WIFI_STATE_SCANNING:
            return to == WIFI_STATE_IDLE || to == WIFI_STATE_FAILED;
        case WIFI_STATE_AUTHENTICATING:
            return to == WIFI_STATE_ASSOCIATING || to == WIFI_STATE_IDLE ||
                   to == WIFI_STATE_FAILED;
        case WIFI_STATE_ASSOCIATING:
            return to == WIFI_STATE_HANDSHAKING || to == WIFI_STATE_CONNECTED ||
                   to == WIFI_STATE_IDLE || to == WIFI_STATE_FAILED;
        case WIFI_STATE_HANDSHAKING:
            return to == WIFI_STATE_CONNECTED || to == WIFI_STATE_IDLE ||
                   to == WIFI_STATE_FAILED;
        case WIFI_STATE_CONNECTED:
            return to == WIFI_STATE_IDLE || to == WIFI_STATE_FAILED;
        case WIFI_STATE_BEACONING:
            return to == WIFI_STATE_IDLE || to == WIFI_STATE_FAILED;
        case WIFI_STATE_FAILED:
            return to == WIFI_STATE_IDLE;
        default:
            return false;
    }
}

const char *wifi_state_name(wifi_assoc_state_t s) {
    switch (s) {
        case WIFI_STATE_IDLE:           return "IDLE";
        case WIFI_STATE_SCANNING:       return "SCANNING";
        case WIFI_STATE_AUTHENTICATING: return "AUTHENTICATING";
        case WIFI_STATE_ASSOCIATING:    return "ASSOCIATING";
        case WIFI_STATE_HANDSHAKING:    return "HANDSHAKING";
        case WIFI_STATE_CONNECTED:      return "CONNECTED";
        case WIFI_STATE_BEACONING:      return "BEACONING";
        case WIFI_STATE_FAILED:         return "FAILED";
        default:                        return "?";
    }
}

int wifi_state_set(wifi_device_t *dev, uint32_t iface_id, wifi_assoc_state_t to) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f) return WIFI_EINVAL;
    if (!wifi_state_can_transition(f->state, to)) return WIFI_ESTATE;
    /* CONNECTED belongs to a station, BEACONING to an AP-like interface. */
    if (to == WIFI_STATE_CONNECTED && f->mode != WIFI_MODE_STATION) return WIFI_ESTATE;
    if (to == WIFI_STATE_BEACONING &&
        !(f->mode == WIFI_MODE_AP || f->mode == WIFI_MODE_ADHOC || f->mode == WIFI_MODE_MESH))
        return WIFI_ESTATE;

    f->state = to;
    f->connected = (to == WIFI_STATE_CONNECTED || to == WIFI_STATE_BEACONING);
    if (to == WIFI_STATE_IDLE || to == WIFI_STATE_FAILED) {
        f->connected = false;
    }
    return WIFI_OK;
}

/* ===================================================================== */
/* Scan result table                                                       */
/* ===================================================================== */

uint32_t wifi_get_scan_count(wifi_device_t *dev) {
    return dev ? dev->num_scan_results : 0;
}

wifi_scan_result_t *wifi_get_scan_result(wifi_device_t *dev, uint32_t index) {
    if (!dev) return 0;
    if (index >= dev->num_scan_results) return 0;
    if (index >= WIFI_MAX_SCAN_RESULTS) return 0;
    return &dev->scan_results[index];
}

void wifi_scan_clear(wifi_device_t *dev) {
    if (!dev) return;
    w_memset(dev->scan_results, 0, sizeof(dev->scan_results));
    dev->num_scan_results = 0;
}

int wifi_scan_find_bssid(const wifi_device_t *dev, const uint8_t bssid[6]) {
    if (!dev || !bssid) return WIFI_EINVAL;
    for (uint32_t i = 0; i < dev->num_scan_results; i++)
        if (w_memcmp(dev->scan_results[i].bssid, bssid, 6) == 0) return (int)i;
    return WIFI_EAGAIN;
}

int wifi_scan_find_ssid(const wifi_device_t *dev, const char *ssid) {
    if (!dev || !ssid) return WIFI_EINVAL;
    int best = WIFI_EAGAIN;
    int16_t best_rssi = -32768;
    for (uint32_t i = 0; i < dev->num_scan_results; i++) {
        if (!wifi_ssid_equal(dev->scan_results[i].ssid, ssid)) continue;
        if (best < 0 || dev->scan_results[i].rssi > best_rssi) {
            best = (int)i;
            best_rssi = dev->scan_results[i].rssi;
        }
    }
    return best;
}

int wifi_scan_add_result(wifi_device_t *dev, const wifi_scan_result_t *r) {
    if (!dev || !r) return WIFI_EINVAL;
    if (wifi_bssid_is_zero(r->bssid)) return WIFI_EINVAL;

    int existing = wifi_scan_find_bssid(dev, r->bssid);
    if (existing >= 0) {
        /* Same BSSID: refresh in place. A later sighting with a real SSID
         * replaces an earlier hidden one; it never blanks a known name. */
        wifi_scan_result_t *e = &dev->scan_results[existing];
        char keep[WIFI_MAX_SSID_LEN];
        w_memcpy(keep, e->ssid, sizeof keep);
        *e = *r;
        if (r->ssid[0] == '\0' && keep[0] != '\0') {
            w_memcpy(e->ssid, keep, sizeof keep);
            e->hidden = false;
        }
        return existing;
    }

    if (dev->num_scan_results < WIFI_MAX_SCAN_RESULTS) {
        uint32_t idx = dev->num_scan_results++;
        dev->scan_results[idx] = *r;
        return (int)idx;
    }

    /* Full: evict the weakest signal, but only for something stronger. */
    uint32_t weakest = 0;
    for (uint32_t i = 1; i < WIFI_MAX_SCAN_RESULTS; i++)
        if (dev->scan_results[i].rssi < dev->scan_results[weakest].rssi) weakest = i;
    if (r->rssi <= dev->scan_results[weakest].rssi) return WIFI_ENOSPC;
    dev->scan_results[weakest] = *r;
    return (int)weakest;
}

/* ===================================================================== */
/* Radio-facing operations                                                 */
/* ===================================================================== */

int wifi_scan(wifi_device_t *dev, uint32_t iface_id) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f) return WIFI_EINVAL;
    if (f->state == WIFI_STATE_SCANNING || dev->scanning) return WIFI_EBUSY;
    if (f->state != WIFI_STATE_IDLE) return WIFI_ESTATE;
    /* No radio means no scan. The table is NOT touched: with nothing to hear
     * from, reporting even one access point would be a fabrication. */
    if (!wifi_has_radio(dev) || !dev->ops->scan_start) return WIFI_ENODEV;

    int rc = dev->ops->scan_start(dev->ops->ctx, f->channel);
    if (rc < 0) return WIFI_EIO;

    wifi_scan_clear(dev);
    dev->scanning = true;
    dev->irq_scan_done = false;
    (void)wifi_state_set(dev, iface_id, WIFI_STATE_SCANNING);
    return WIFI_OK;
}

int wifi_scan_complete(wifi_device_t *dev, uint32_t iface_id) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f) return WIFI_EINVAL;
    if (f->state != WIFI_STATE_SCANNING) return WIFI_ESTATE;
    dev->scanning = false;
    dev->irq_scan_done = true;
    return wifi_state_set(dev, iface_id, WIFI_STATE_IDLE);
}

static bool security_compatible(wifi_security_t want, wifi_security_t advertised) {
    if (want == WIFI_SEC_OPEN) return advertised == WIFI_SEC_OPEN;
    if (want == WIFI_SEC_WPA2)
        return advertised == WIFI_SEC_WPA2 || advertised == WIFI_SEC_WPA2_WPA3;
    return false;
}

int wifi_connect(wifi_device_t *dev, uint32_t iface_id, const char *ssid,
                 const char *password, wifi_security_t security) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f || !ssid) return WIFI_EINVAL;
    if (f->mode != WIFI_MODE_STATION) return WIFI_ESTATE;

    uint32_t slen = w_strlen(ssid);
    if (slen == 0 || slen > 32) return WIFI_EINVAL;

    /* Only what we can genuinely complete: Open System, and WPA2-PSK with
     * CCMP. WEP needs RC4, WPA needs HMAC-MD5, WPA3 needs SAE — none of
     * which exist here, so they are refused instead of half-attempted. */
    if (security != WIFI_SEC_OPEN && security != WIFI_SEC_WPA2) return WIFI_ENOTSUP;

    uint32_t plen = password ? w_strlen(password) : 0;
    if (security == WIFI_SEC_OPEN && plen != 0) return WIFI_EINVAL;
    if (security == WIFI_SEC_WPA2 && (plen < 8 || plen > 63)) return WIFI_EINVAL;

    if (f->state == WIFI_STATE_SCANNING) return WIFI_EBUSY;
    if (f->state != WIFI_STATE_IDLE) return WIFI_ESTATE;
    if (!wifi_has_radio(dev)) return WIFI_ENODEV;

    /* We associate to a BSS we have actually heard, so the channel, BSSID
     * and advertised security are measured rather than guessed. */
    int idx = wifi_scan_find_ssid(dev, ssid);
    if (idx < 0) return WIFI_EAGAIN;
    const wifi_scan_result_t *ap = &dev->scan_results[(uint32_t)idx];
    if (!security_compatible(security, ap->security)) return WIFI_EAUTH;

    uint32_t freq = wifi_channel_to_freq(ap->channel, ap->band);
    if (freq == 0) return WIFI_EINVAL;
    if (dev->ops->set_channel &&
        dev->ops->set_channel(dev->ops->ctx, ap->channel, freq) < 0)
        return WIFI_EIO;

    w_strcpy(f->ssid, ssid, sizeof(f->ssid));
    w_memcpy(f->bssid, ap->bssid, 6);
    f->channel  = ap->channel;
    f->band     = ap->band;
    f->security = security;
    f->rssi     = ap->rssi;
    dev->reg_channel = ap->channel;

    /* Pre-compute the PMK now, while we still have the passphrase, and keep
     * only the PMK: the passphrase is never stored. */
    w_memset(&f->sup, 0, sizeof(f->sup));
    if (security == WIFI_SEC_WPA2) {
        int rc = wifi_wpa_pmk(password, ssid, f->sup.pmk);
        if (rc != WIFI_OK) return rc;
        f->sup.pmk_valid = true;
        w_memcpy(f->sup.aa, ap->bssid, 6);
        w_memcpy(f->sup.spa, f->mac, 6);
    }

    int rc = wifi_state_set(dev, iface_id, WIFI_STATE_AUTHENTICATING);
    if (rc != WIFI_OK) return rc;

    uint8_t frame[64];
    int n = wifi_build_auth(frame, sizeof frame, f->bssid, f->mac,
                            wifi_next_seq(f), 0 /* Open System */, 1, 0);
    if (n < 0) { (void)wifi_state_set(dev, iface_id, WIFI_STATE_IDLE); return n; }
    if (dev->ops->tx_frame(dev->ops->ctx, frame, (uint32_t)n) < 0) {
        (void)wifi_state_set(dev, iface_id, WIFI_STATE_IDLE);
        return WIFI_EIO;
    }
    f->tx_packets++;
    f->tx_bytes += (uint64_t)n;
    f->up = true;
    /* WIFI_OK here means "authentication has been sent", not "connected".
     * The interface reaches CONNECTED only through wifi_rx_mgmt(). */
    return WIFI_OK;
}

int wifi_disconnect(wifi_device_t *dev, uint32_t iface_id) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f) return WIFI_EINVAL;
    if (f->state == WIFI_STATE_IDLE) return WIFI_ESTATE;
    if (f->state == WIFI_STATE_BEACONING) return WIFI_ESTATE;  /* use wifi_stop_ap */
    if (!wifi_has_radio(dev)) return WIFI_ENODEV;

    if (f->state != WIFI_STATE_FAILED && f->state != WIFI_STATE_SCANNING) {
        uint8_t frame[64];
        int n = wifi_build_deauth(frame, sizeof frame, f->bssid, f->mac, f->bssid,
                                  wifi_next_seq(f), 3 /* STA is leaving */);
        if (n > 0 && dev->ops->tx_frame(dev->ops->ctx, frame, (uint32_t)n) >= 0) {
            f->tx_packets++;
            f->tx_bytes += (uint64_t)n;
        }
    }

    int rc = wifi_state_set(dev, iface_id, WIFI_STATE_IDLE);
    if (rc != WIFI_OK) return rc;
    dev->scanning = false;
    f->up = false;
    w_memset(f->ssid, 0, sizeof(f->ssid));
    w_memset(f->bssid, 0, 6);
    f->rssi = -127;
    /* Key material does not outlive the association. */
    w_memset(&f->sup, 0, sizeof(f->sup));
    dev->irq_disconnected = true;
    return WIFI_OK;
}

int wifi_start_ap(wifi_device_t *dev, uint32_t iface_id, const char *ssid,
                  const char *password, uint32_t channel) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f || !ssid) return WIFI_EINVAL;
    if (f->mode != WIFI_MODE_AP) return WIFI_ESTATE;

    uint32_t slen = w_strlen(ssid);
    if (slen == 0 || slen > 32) return WIFI_EINVAL;
    uint32_t plen = password ? w_strlen(password) : 0;
    if (plen != 0 && (plen < 8 || plen > 63)) return WIFI_EINVAL;
    if (!wifi_channel_valid(channel, f->band)) return WIFI_EINVAL;
    if (f->state != WIFI_STATE_IDLE) return WIFI_ESTATE;
    if (!wifi_has_radio(dev) || !dev->ops->ap_start) return WIFI_ENODEV;

    uint32_t freq = wifi_channel_to_freq(channel, f->band);
    if (freq == 0) return WIFI_EINVAL;
    if (dev->ops->ap_start(dev->ops->ctx, ssid, channel) < 0) return WIFI_EIO;

    w_strcpy(f->ssid, ssid, sizeof(f->ssid));
    w_memcpy(f->bssid, f->mac, 6);        /* an AP's BSSID is its own address */
    f->channel = channel;
    f->security = plen ? WIFI_SEC_WPA2 : WIFI_SEC_OPEN;
    f->up = true;
    dev->reg_channel = channel;
    /* Note: the AP side of the 4-way handshake is not implemented (L3), so a
     * secured AP started here will beacon RSN but cannot complete a client's
     * key exchange. The flag records the advertised suite, nothing more. */
    return wifi_state_set(dev, iface_id, WIFI_STATE_BEACONING);
}

int wifi_stop_ap(wifi_device_t *dev, uint32_t iface_id) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f) return WIFI_EINVAL;
    if (f->mode != WIFI_MODE_AP) return WIFI_ESTATE;
    if (f->state != WIFI_STATE_BEACONING) return WIFI_ESTATE;
    if (!wifi_has_radio(dev) || !dev->ops->ap_stop) return WIFI_ENODEV;
    if (dev->ops->ap_stop(dev->ops->ctx) < 0) return WIFI_EIO;

    int rc = wifi_state_set(dev, iface_id, WIFI_STATE_IDLE);
    if (rc != WIFI_OK) return rc;
    f->up = false;
    w_memset(f->ssid, 0, sizeof(f->ssid));
    w_memset(f->bssid, 0, 6);
    return WIFI_OK;
}

int wifi_set_channel(wifi_device_t *dev, uint32_t iface_id, uint32_t channel) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f) return WIFI_EINVAL;
    if (!wifi_channel_valid(channel, f->band)) return WIFI_EINVAL;
    uint32_t freq = wifi_channel_to_freq(channel, f->band);
    if (freq == 0) return WIFI_EINVAL;
    if (!wifi_has_radio(dev) || !dev->ops->set_channel) return WIFI_ENODEV;
    if (dev->ops->set_channel(dev->ops->ctx, channel, freq) < 0) return WIFI_EIO;
    f->channel = channel;
    dev->reg_channel = channel;
    return WIFI_OK;
}

int wifi_set_freq(wifi_device_t *dev, uint32_t iface_id, uint32_t freq_mhz) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f) return WIFI_EINVAL;
    uint32_t ch = wifi_freq_to_channel(freq_mhz);
    if (ch == 0) return WIFI_EINVAL;
    int band = wifi_freq_to_band(freq_mhz);
    if (band < 0) return WIFI_EINVAL;
    if (!wifi_has_radio(dev) || !dev->ops->set_channel) return WIFI_ENODEV;
    if (dev->ops->set_channel(dev->ops->ctx, ch, freq_mhz) < 0) return WIFI_EIO;
    f->channel = ch;
    f->band = (wifi_band_t)band;
    dev->reg_channel = ch;
    return WIFI_OK;
}

int wifi_set_power_save(wifi_device_t *dev, uint32_t iface_id, uint8_t level) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f) return WIFI_EINVAL;
    if (level > 2) return WIFI_EINVAL;
    if (!wifi_has_radio(dev) || !dev->ops->set_power_save) return WIFI_ENODEV;
    if (dev->ops->set_power_save(dev->ops->ctx, level) < 0) return WIFI_EIO;
    f->power_save_level = level;
    return WIFI_OK;
}

int wifi_set_tx_power(wifi_device_t *dev, uint32_t iface_id, uint8_t dbm) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f) return WIFI_EINVAL;
    /* 30 dBm (1 W conducted) is the ceiling essentially every regulatory
     * domain imposes on these bands; a uint8_t can ask for 255, which is not
     * a transmit power, it is a typo. */
    if (dbm > WIFI_MAX_TX_POWER_DBM) return WIFI_EINVAL;
    /* max_tx_power is 0 ("unknown") until a driver fills it in after binding.
     * Once it says something, asking for more than the radio can do is a bad
     * argument rather than something to hand down and hope about. */
    if (dev->max_tx_power != 0 && dbm > dev->max_tx_power) return WIFI_EINVAL;
    if (!wifi_has_radio(dev) || !dev->ops->set_tx_power) return WIFI_ENODEV;
    if (dev->ops->set_tx_power(dev->ops->ctx, dbm) < 0) return WIFI_EIO;
    /* Recorded only now, because only now is it the radio's actual setting. */
    dev->reg_tx_power = dbm;
    return WIFI_OK;
}

int wifi_tx_packet(wifi_device_t *dev, uint32_t iface_id, const void *data, uint32_t len) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f || !data) return WIFI_EINVAL;
    if (len == 0 || len > 2352) return WIFI_EINVAL;   /* max 802.11 MPDU */
    if (!wifi_has_radio(dev)) return WIFI_ENODEV;
    int rc = dev->ops->tx_frame(dev->ops->ctx, (const uint8_t *)data, len);
    if (rc < 0) { f->tx_dropped++; return WIFI_EIO; }
    /* Counted only now, because only now did bytes actually leave. */
    f->tx_packets++;
    f->tx_bytes += len;
    dev->irq_tx_done = true;
    return (int)len;
}

int wifi_tx_enqueue(wifi_device_t *dev, const void *data, uint32_t len) {
    if (!dev || !data) return WIFI_EINVAL;
    if (len == 0 || len > 2352) return WIFI_EINVAL;
    return ring_push(dev->tx_dma, (uint32_t)sizeof(dev->tx_dma),
                     &dev->tx_head, &dev->tx_used, (const uint8_t *)data, len);
}

uint32_t wifi_tx_pending(const wifi_device_t *dev) { return dev ? dev->tx_used : 0; }
uint32_t wifi_rx_pending(const wifi_device_t *dev) { return dev ? dev->rx_used : 0; }

int wifi_tx_flush(wifi_device_t *dev, uint32_t iface_id) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f) return WIFI_EINVAL;
    if (dev->tx_used == 0) return 0;
    if (!wifi_has_radio(dev)) return WIFI_ENODEV;

    int sent = 0;
    uint8_t frame[2352];
    for (;;) {
        int n = ring_pop(dev->tx_dma, (uint32_t)sizeof(dev->tx_dma),
                         &dev->tx_tail, &dev->tx_used, frame, (uint32_t)sizeof frame);
        if (n == WIFI_EAGAIN) break;
        if (n < 0) return n;
        if (dev->ops->tx_frame(dev->ops->ctx, frame, (uint32_t)n) < 0) {
            f->tx_dropped++;
            return sent ? sent : WIFI_EIO;
        }
        f->tx_packets++;
        f->tx_bytes += (uint64_t)n;
        sent++;
    }
    dev->irq_tx_done = true;
    return sent;
}

int wifi_rx_inject(wifi_device_t *dev, uint32_t iface_id, const uint8_t *frame, uint32_t len) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f || !frame) return WIFI_EINVAL;
    if (len == 0 || len > 2352) return WIFI_EINVAL;
    int rc = ring_push(dev->rx_dma, (uint32_t)sizeof(dev->rx_dma),
                       &dev->rx_head, &dev->rx_used, frame, len);
    if (rc != WIFI_OK) { f->rx_dropped++; return rc; }
    /* Counted here, at the point the frame really arrived from the radio;
     * delivering it to a caller later does not count it a second time. */
    f->rx_packets++;
    f->rx_bytes += len;
    dev->irq_rx_ready = true;
    return WIFI_OK;
}

int wifi_rx_packet(wifi_device_t *dev, uint32_t iface_id, void *data, uint32_t max_len) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f || !data || max_len == 0) return WIFI_EINVAL;

    if (dev->rx_used) {
        int n = ring_pop(dev->rx_dma, (uint32_t)sizeof(dev->rx_dma),
                         &dev->rx_tail, &dev->rx_used, (uint8_t *)data, max_len);
        if (n >= 0 && dev->rx_used == 0) dev->irq_rx_ready = false;
        return n;
    }
    if (!wifi_has_radio(dev)) return WIFI_ENODEV;
    int n = dev->ops->rx_poll(dev->ops->ctx, (uint8_t *)data, max_len);
    if (n < 0) return WIFI_EIO;
    if (n == 0) return WIFI_EAGAIN;
    f->rx_packets++;
    f->rx_bytes += (uint64_t)n;
    return n;
}

/* ===================================================================== */
/* Beacon builder (AP mode)                                                */
/* ===================================================================== */

int wifi_build_beacon(wifi_device_t *dev, uint32_t iface_id, uint8_t *out, uint32_t cap) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f || !out) return WIFI_EINVAL;
    if (f->mode != WIFI_MODE_AP) return WIFI_ESTATE;
    uint32_t slen = w_strlen(f->ssid);
    if (slen == 0 || slen > 32) return WIFI_EINVAL;
    if (!wifi_channel_valid(f->channel, f->band)) return WIFI_EINVAL;

    int n = build_mgmt_hdr(out, cap, WIFI_STYPE_BEACON, WIFI_BCAST, f->mac,
                           f->bssid, wifi_next_seq(f));
    if (n < 0) return n;
    uint32_t off = (uint32_t)n;

    bool secured = (f->security != WIFI_SEC_OPEN);
    bool two_four = (f->band == WIFI_BAND_2_4GHZ) || (f->channel <= 14);
    /* Both rate sets below are 8 octets long; only their CONTENTS differ
     * (DSSS+OFDM on 2.4 GHz, OFDM only above it), so the size is a constant
     * and the length check does not depend on the band. */
    const uint32_t rates_n = 8u;
    uint32_t need = off + 12 + (2 + slen) + (2 + rates_n) + 3 + (2 + 4) +
                    (secured ? sizeof(WIFI_RSNE_PSK_CCMP) : 0);
    if (cap < need) return WIFI_EMSGSIZE;

    /* TSF timestamp: left at zero. There is no radio clock to read, and a
     * made-up TSF would be worse than an obviously absent one; a real driver
     * overwrites these eight bytes. */
    w_memset(out + off, 0, 8); off += 8;
    wr16le(out + off, 100);    off += 2;      /* beacon interval, TU */
    wr16le(out + off, (uint16_t)(0x0001 | (secured ? 0x0010 : 0))); off += 2; /* ESS[|Privacy] */

    out[off++] = WIFI_EID_SSID;
    out[off++] = (uint8_t)slen;
    w_memcpy(out + off, f->ssid, slen); off += slen;

    out[off++] = WIFI_EID_RATES;
    out[off++] = (uint8_t)rates_n;
    if (two_four) {
        out[off++] = 0x82; out[off++] = 0x84; out[off++] = 0x8B; out[off++] = 0x96;
        out[off++] = 0x0C; out[off++] = 0x12; out[off++] = 0x18; out[off++] = 0x24;
    } else {
        out[off++] = 0x8C; out[off++] = 0x12; out[off++] = 0x98; out[off++] = 0x24;
        out[off++] = 0xB0; out[off++] = 0x48; out[off++] = 0x60; out[off++] = 0x6C;
    }

    out[off++] = WIFI_EID_DSPARAM;
    out[off++] = 1;
    out[off++] = (uint8_t)f->channel;

    out[off++] = WIFI_EID_EXT_RATES;
    out[off++] = 4;
    out[off++] = 0x30; out[off++] = 0x48; out[off++] = 0x60; out[off++] = 0x6C;

    if (secured) {
        w_memcpy(out + off, WIFI_RSNE_PSK_CCMP, sizeof(WIFI_RSNE_PSK_CCMP));
        off += (uint32_t)sizeof(WIFI_RSNE_PSK_CCMP);
    }
    return (int)off;
}

/* ===================================================================== */
/* Management frame ingress — this is what drives the state machine        */
/* ===================================================================== */

int wifi_rx_mgmt(wifi_device_t *dev, uint32_t iface_id, const uint8_t *frame,
                 uint32_t len, int16_t rssi) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f || !frame) return WIFI_EINVAL;

    wifi_mac_hdr_t h;
    int hl = wifi_mac_hdr_parse(frame, len, &h);
    if (hl < 0) return hl;
    if (h.type != WIFI_FTYPE_MGMT) return WIFI_ENOTSUP;

    /* Beacons and probe responses are for everybody; anything else must be
     * addressed to us or we have no business acting on it. */
    bool for_us = (w_memcmp(h.addr1, f->mac, 6) == 0) || wifi_bssid_is_broadcast(h.addr1);
    uint32_t body = (uint32_t)hl;

    switch (h.subtype) {
    case WIFI_STYPE_BEACON:
    case WIFI_STYPE_PROBE_RESP: {
        wifi_scan_result_t r;
        /* The RSSI stored in a scan result is the one the CALLER measured for
         * THIS frame. The interface's own f->rssi belongs to the BSS we are
         * associated with (and is the -127 "never measured" sentinel before
         * that), so using it here would stamp every AP we overhear with a
         * signal strength taken from a different radio path — a fabricated
         * measurement. The driver has the real per-frame value; it passes it. */
        uint32_t freq = wifi_channel_to_freq(f->channel, f->band);
        int rc = wifi_parse_beacon(frame, len, rssi, freq, &r);
        if (rc != WIFI_OK) return rc;
        rc = wifi_scan_add_result(dev, &r);
        if (rc < 0) return rc;
        return (int)h.subtype;
    }
    case WIFI_STYPE_AUTH: {
        if (!for_us) return WIFI_EAGAIN;
        /* A frame that is not from the BSS we are talking to is not evidence
         * about that BSS. Silently ignoring it is the point. */
        if (w_memcmp(h.addr3, f->bssid, 6) != 0) return WIFI_EAGAIN;
        if (f->state != WIFI_STATE_AUTHENTICATING) return WIFI_ESTATE;
        if (len < body + 6) return WIFI_EINVAL;
        uint16_t alg    = rd16le(frame + body);
        uint16_t aseq   = rd16le(frame + body + 2);
        uint16_t status = rd16le(frame + body + 4);
        if (alg != 0) return WIFI_ENOTSUP;          /* Open System only */
        if (aseq != 2) return WIFI_EAGAIN;
        if (status != 0) {
            dev->irq_auth_failed = true;
            (void)wifi_state_set(dev, iface_id, WIFI_STATE_FAILED);
            return WIFI_EAUTH;
        }
        int rc = wifi_state_set(dev, iface_id, WIFI_STATE_ASSOCIATING);
        if (rc != WIFI_OK) return rc;
        if (!wifi_has_radio(dev)) return WIFI_ENODEV;
        uint8_t tx[192];
        uint16_t cap_info = (uint16_t)(0x0001 | (f->security != WIFI_SEC_OPEN ? 0x0010 : 0));
        int n = wifi_build_assoc_req(tx, sizeof tx, f->bssid, f->mac,
                                     wifi_next_seq(f), f->ssid, cap_info, 10);
        if (n < 0) return n;
        if (dev->ops->tx_frame(dev->ops->ctx, tx, (uint32_t)n) < 0) return WIFI_EIO;
        f->tx_packets++;
        f->tx_bytes += (uint64_t)n;
        return (int)h.subtype;
    }
    case WIFI_STYPE_ASSOC_RESP:
    case WIFI_STYPE_REASSOC_RSP: {
        if (!for_us) return WIFI_EAGAIN;
        if (w_memcmp(h.addr3, f->bssid, 6) != 0) return WIFI_EAGAIN;
        if (f->state != WIFI_STATE_ASSOCIATING) return WIFI_ESTATE;
        if (len < body + 6) return WIFI_EINVAL;
        uint16_t status = rd16le(frame + body + 2);
        if (status != 0) {
            dev->irq_auth_failed = true;
            (void)wifi_state_set(dev, iface_id, WIFI_STATE_FAILED);
            return WIFI_EAUTH;
        }
        if (f->security == WIFI_SEC_OPEN) {
            int rc = wifi_state_set(dev, iface_id, WIFI_STATE_CONNECTED);
            if (rc != WIFI_OK) return rc;
            dev->irq_connected = true;
        } else {
            /* Associated but not yet keyed: no data may flow until the
             * 4-way handshake completes. */
            int rc = wifi_state_set(dev, iface_id, WIFI_STATE_HANDSHAKING);
            if (rc != WIFI_OK) return rc;
        }
        return (int)h.subtype;
    }
    case WIFI_STYPE_DEAUTH:
    case WIFI_STYPE_DISASSOC: {
        if (!for_us) return WIFI_EAGAIN;
        if (w_memcmp(h.addr3, f->bssid, 6) != 0) return WIFI_EAGAIN;
        if (f->state == WIFI_STATE_IDLE) return WIFI_ESTATE;
        int rc = wifi_state_set(dev, iface_id, WIFI_STATE_IDLE);
        if (rc != WIFI_OK) return rc;
        f->up = false;
        w_memset(&f->sup, 0, sizeof(f->sup));
        dev->irq_disconnected = true;
        return (int)h.subtype;
    }
    default:
        return WIFI_ENOTSUP;
    }
}

/* ===================================================================== */
/* WPA2 4-way handshake — supplicant side                                  */
/* ===================================================================== */

int wifi_wpa_start(wifi_device_t *dev, uint32_t iface_id, const char *passphrase) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f || !passphrase) return WIFI_EINVAL;
    if (f->ssid[0] == '\0') return WIFI_ESTATE;
    if (f->state != WIFI_STATE_ASSOCIATING && f->state != WIFI_STATE_HANDSHAKING)
        return WIFI_ESTATE;

    uint8_t pmk[WIFI_PMK_LEN];
    int rc = wifi_wpa_pmk(passphrase, f->ssid, pmk);
    if (rc != WIFI_OK) return rc;

    w_memset(&f->sup, 0, sizeof(f->sup));
    w_memcpy(f->sup.pmk, pmk, WIFI_PMK_LEN);
    f->sup.pmk_valid = true;
    w_memcpy(f->sup.aa, f->bssid, 6);
    w_memcpy(f->sup.spa, f->mac, 6);
    w_memset(pmk, 0, sizeof pmk);
    return WIFI_OK;
}

int wifi_wpa_set_snonce(wifi_device_t *dev, uint32_t iface_id, const uint8_t snonce[32]) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f || !snonce) return WIFI_EINVAL;
    w_memcpy(f->sup.snonce, snonce, 32);
    f->sup.snonce_valid = true;
    return WIFI_OK;
}

int wifi_wpa_rx_eapol(wifi_device_t *dev, uint32_t iface_id,
                      const uint8_t *frame, uint32_t len,
                      uint8_t *reply, uint32_t reply_cap, uint32_t *reply_len) {
    wifi_interface_t *f = wifi_get_interface(dev, iface_id);
    if (!f || !frame || !reply || !reply_len) return WIFI_EINVAL;
    *reply_len = 0;

    wifi_supplicant_t *s = &f->sup;
    if (!s->pmk_valid) return WIFI_ESTATE;
    if (f->state != WIFI_STATE_HANDSHAKING) return WIFI_ESTATE;

    wifi_eapol_key_t k;
    int rc = wifi_eapol_key_parse(frame, len, &k);
    if (rc != WIFI_OK) return rc;

    uint16_t ver = (uint16_t)(k.key_info & WIFI_KI_VERSION_MASK);
    if (ver != 2) return WIFI_ENOTSUP;      /* see L3 */
    s->key_desc_version = ver;

    if (k.msg == 1) {
        w_memcpy(s->anonce, k.nonce, 32);
        s->anonce_valid = true;

        if (!s->snonce_valid) {
            /* A nonce is a security input. With no entropy source bound we
             * say so instead of shipping a predictable one. */
            if (!dev->ops || !dev->ops->get_random) return WIFI_ENODEV;
            if (dev->ops->get_random(dev->ops->ctx, s->snonce, 32) < 0) return WIFI_EIO;
            s->snonce_valid = true;
        }

        rc = wifi_wpa_derive_ptk(s->pmk, s->aa, s->spa, s->anonce, s->snonce, &s->ptk);
        if (rc != WIFI_OK) return rc;
        s->ptk_derived = true;

        wifi_eapol_key_t m2;
        w_memset(&m2, 0, sizeof m2);
        m2.descriptor_type = 2;                            /* RSN */
        m2.key_info        = (uint16_t)(ver | WIFI_KI_PAIRWISE | WIFI_KI_MIC);
        m2.key_length      = 0;
        m2.replay_counter  = k.replay_counter;
        w_memcpy(m2.nonce, s->snonce, 32);
        m2.key_data_len    = (uint16_t)sizeof(WIFI_RSNE_PSK_CCMP);

        int n = wifi_eapol_key_build(&m2, WIFI_RSNE_PSK_CCMP, reply, reply_cap);
        if (n < 0) return n;
        uint8_t mic[16];
        rc = wifi_eapol_mic(s->ptk.kck, reply, (uint32_t)n, ver, mic);
        if (rc != WIFI_OK) return rc;
        w_memcpy(reply + WIFI_EAPOL_MIC_OFF, mic, 16);

        s->replay_counter = k.replay_counter;
        s->replay_seen = true;
        s->msg_mask |= 0x1;
        *reply_len = (uint32_t)n;
        return 1;
    }

    if (k.msg == 3) {
        if (!(s->msg_mask & 0x1) || !s->ptk_derived) return WIFI_ESTATE;
        /* Order matters: authenticate before believing anything in the frame. */
        if (!wifi_eapol_mic_verify(s->ptk.kck, frame, k.total_len, ver)) return WIFI_EAUTH;
        if (w_memcmp(k.nonce, s->anonce, 32) != 0) return WIFI_EAUTH;
        if (s->replay_seen && k.replay_counter <= s->replay_counter) return WIFI_EAUTH;

        wifi_eapol_key_t m4;
        w_memset(&m4, 0, sizeof m4);
        m4.descriptor_type = 2;
        m4.key_info = (uint16_t)(ver | WIFI_KI_PAIRWISE | WIFI_KI_MIC | WIFI_KI_SECURE);
        m4.replay_counter = k.replay_counter;
        m4.key_data_len = 0;                               /* M4 carries nothing */

        int n = wifi_eapol_key_build(&m4, 0, reply, reply_cap);
        if (n < 0) return n;
        uint8_t mic[16];
        rc = wifi_eapol_mic(s->ptk.kck, reply, (uint32_t)n, ver, mic);
        if (rc != WIFI_OK) return rc;
        w_memcpy(reply + WIFI_EAPOL_MIC_OFF, mic, 16);

        s->replay_counter = k.replay_counter;
        s->msg_mask |= 0x4;
        s->ptk_valid = true;      /* the pairwise TK is now installed */
        s->gtk_valid = false;     /* the group key is NOT — see L4 */

        rc = wifi_state_set(dev, iface_id, WIFI_STATE_CONNECTED);
        if (rc != WIFI_OK) return rc;
        dev->irq_connected = true;
        *reply_len = (uint32_t)n;
        return 3;
    }

    /* M2 and M4 are ours to send, and the group-key handshake is not
     * implemented; neither is quietly swallowed. */
    return WIFI_ENOTSUP;
}

/* ===================================================================== */
/* IRQ                                                                     */
/* ===================================================================== */

void wifi_handle_irq(wifi_device_t *dev) {
    if (!dev) return;

    if (dev->irq_scan_done) {
        dev->scanning = false;
        for (uint32_t i = 0; i < dev->num_ifaces; i++)
            if (dev->ifaces[i].state == WIFI_STATE_SCANNING)
                (void)wifi_state_set(dev, i + 1, WIFI_STATE_IDLE);
        dev->irq_scan_done = false;
    }

    if (dev->irq_rx_ready && wifi_has_radio(dev)) {
        /* Drain what the radio has, bounded so an ISR cannot spin forever.
         *
         * Honesty note: the drain buffer is 1600 bytes and that cap is handed
         * to rx_poll, so a backend that has a larger frame reports its own
         * failure (n < 0) rather than handing us a truncated one — this loop
         * cannot distinguish "too big" from any other backend error, and does
         * not pretend to. What it MUST NOT do is pull a frame off the radio
         * and then drop it without saying so, which is why the no-interface
         * case is checked BEFORE rx_poll and the failed-inject case counts. */
        uint8_t frame[1600];
        if (dev->num_ifaces != 0) {
            for (int guard = 0; guard < 32; guard++) {
                int n = dev->ops->rx_poll(dev->ops->ctx, frame, (uint32_t)sizeof frame);
                if (n == 0) break;                  /* nothing waiting */
                if (n < 0) { dev->ifaces[0].rx_dropped++; break; }
                /* wifi_rx_inject() counts a refusal as rx_dropped itself. */
                if (wifi_rx_inject(dev, 1, frame, (uint32_t)n) != WIFI_OK) break;
            }
        }
        if (dev->rx_used == 0) dev->irq_rx_ready = false;
    }

    if (dev->irq_tx_done) dev->irq_tx_done = false;

    if (dev->irq_auth_failed) {
        for (uint32_t i = 0; i < dev->num_ifaces; i++) {
            wifi_assoc_state_t st = dev->ifaces[i].state;
            if (st == WIFI_STATE_AUTHENTICATING || st == WIFI_STATE_ASSOCIATING ||
                st == WIFI_STATE_HANDSHAKING)
                (void)wifi_state_set(dev, i + 1, WIFI_STATE_FAILED);
        }
        dev->irq_auth_failed = false;
    }

    if (dev->irq_disconnected) {
        for (uint32_t i = 0; i < dev->num_ifaces; i++) {
            if (dev->ifaces[i].state == WIFI_STATE_CONNECTED) {
                (void)wifi_state_set(dev, i + 1, WIFI_STATE_IDLE);
                dev->ifaces[i].up = false;
                w_memset(&dev->ifaces[i].sup, 0, sizeof(dev->ifaces[i].sup));
            }
        }
        dev->irq_disconnected = false;
    }

    if (dev->irq_connected) dev->irq_connected = false;
}

/* ===================================================================== */
/* Coverage                                                                */
/* ===================================================================== */

static bool iface_is_coherent(const wifi_interface_t *f) {
    if (f->name[0] == '\0') return false;
    if (f->mode > WIFI_MODE_P2P) return false;
    if ((int)f->state < 0 || f->state >= WIFI_STATE__COUNT) return false;
    if (f->power_save_level > 2) return false;

    bool claims_link = (f->state == WIFI_STATE_CONNECTED ||
                        f->state == WIFI_STATE_BEACONING);
    if (claims_link) {
        if (!f->connected) return false;
        if (!f->up) return false;
        if (f->ssid[0] == '\0') return false;
        if (wifi_bssid_is_zero(f->bssid)) return false;
        if (!wifi_channel_valid(f->channel, f->band)) return false;
        if (f->state == WIFI_STATE_CONNECTED && f->mode != WIFI_MODE_STATION) return false;
    } else {
        /* Anything not in a link state must not advertise one. */
        if (f->connected) return false;
    }
    return true;
}

bool wifi_verify_coverage(wifi_device_t *dev) {
    if (!dev) return false;

    /* l: is there a radio at all? A Wi-Fi device with no radio covers
     * nothing, so this is 0.0 on a freshly initialised device and the check
     * below correctly fails. */
    dev->coverage_l = wifi_has_radio(dev) ? 1.0 : 0.0;

    /* r: the fraction of interfaces whose advertised state matches their
     * data. This is the part that can catch a real bug — an interface that
     * says CONNECTED while holding no BSSID drags r below 1. */
    uint32_t coherent = 0;
    uint32_t n = dev->num_ifaces;
    if (n > WIFI_MAX_INTERFACES) n = WIFI_MAX_INTERFACES;
    for (uint32_t i = 0; i < n; i++)
        if (iface_is_coherent(&dev->ifaces[i])) coherent++;
    dev->coverage_r = (n == 0) ? 0.0 : (double)coherent / (double)n;

    /* Both factors are exact small-integer fractions in [0,1], so the
     * product reaches 1.0 only when every interface is coherent AND a radio
     * is bound. No epsilon is needed and none is used. */
    return (dev->coverage_r * dev->coverage_l) >= WIFI_COVERAGE_FLOOR;
}
