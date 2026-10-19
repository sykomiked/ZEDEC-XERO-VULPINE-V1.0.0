/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_devmesh.c — the personal device mesh's wire decoders
 * (kernel/src/devmesh): base32 text form, capability records, money actions
 * and money confirmations, and the home node's wallet-request check.
 *
 * Byte 0 picks the mode (mod 5); the rest is the input:
 *   0  dm_b32_decode: never writes past cap; decode accepts only canonical
 *      text (RFC 4648 without padding, either case): a length that no byte
 *      string encodes to (len % 8 in {1, 3, 6}) or nonzero trailing bits is
 *      refused, so dm_b32_encode(decode(s)) == upper(s); and for any bytes,
 *      decode(encode(x)) == x
 *      (-DFZ_LAX_CANON skips only this canonical-text check)
 *   1  dm_caps_decode: field ranges (nmodels <= DM_MAX_MODELS, net, battery,
 *      model class, NUL-terminated names); byte-exact re-encode
 *   2  dm_money_decode: kind, rail 555/777/888, currency "VFV", memo
 *      terminated, 0 < amount <= DM_MONEY_AMOUNT_MAX (0 and anything above
 *      the cap are refused); byte-exact
 *      re-encode; the amount reads back exactly (big-endian u64, no rounding)
 *   3  dm_confirm_decode: approve in {0,1}, sig_len <= PQM_SIG_MAX_BYTES;
 *      byte-exact re-encode
 *   4  authorisation: a genuine DM_REQ_WALLET payload (money action + a
 *      confirmation signed by this ACTIVE+HELD device) with fuzz-chosen byte
 *      flips. dm_wallet_check accepts it iff not one byte changed, the
 *      action has not expired and has not been seen; the same payload a
 *      second time is DM_ERR_REPLAY; a declined confirmation is never OK. */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "devmesh.h"
#include "dm_remote.h"
#include "fuzz_in.h"
#include "../mlkem/keccak.h"

/* ---- mode 0: base32 ---- */

static void mode_b32(const uint8_t *p, size_t n)
{
    if (n < 1) return;
    uint32_t cap = p[0];
    p++;
    n--;
    if (n > 600) n = 600;
    char *s = fz_dupz(p, n);
    size_t sl = strlen(s);
    uint8_t *out = (uint8_t *) malloc(cap ? cap : 1u);
    if (!out) abort();
    int32_t r = dm_b32_decode(s, out, cap);
    if (r >= 0) {
        if ((uint32_t) r > cap || (uint32_t) r != sl * 5u / 8u) abort();
        static char enc[1200];
        int32_t e = dm_b32_encode(out, (uint32_t) r, enc, sizeof enc);
        if (e < 0) abort();
        /* canonical text only */
        bool same = (size_t) e == sl;
        for (size_t i = 0; same && i < sl; i++) {
            char c = s[i];
            if (c >= 'a' && c <= 'z') c = (char) (c - 32);
            if (c != enc[i]) same = false;
        }
#ifndef FZ_LAX_CANON
        if (!same) abort();
#else
        (void) same;
#endif
    }
    free(out);
    /* bytes -> text -> bytes */
    static char t[1300];
    static uint8_t back[700];
    int32_t e = dm_b32_encode(p, (uint32_t) n, t, sizeof t);
    if (e != (int32_t) ((n * 8u + 4u) / 5u)) abort();
    if (dm_b32_decode(t, back, sizeof back) != (int32_t) n || memcmp(back, p, n) != 0) abort();
    free(s);
}

/* ---- mode 1: caps ---- */

static void mode_caps(const uint8_t *p, size_t n)
{
    uint8_t *b = fz_dup(p, n);
    dm_caps_t c;
    if (dm_caps_decode(&c, b, (uint32_t) n) == DM_OK) {
        if (c.nmodels > DM_MAX_MODELS || c.net > DM_NET_METERED) abort();
        if (c.battery_pct > 100u && c.battery_pct != 255u) abort();
        for (uint32_t i = 0; i < c.nmodels; i++) {
            if (c.models[i].name[DM_MODEL_NAME - 1] != 0) abort();
            if (c.models[i].cls != DM_MODEL_SMALL && c.models[i].cls != DM_MODEL_LARGE) abort();
        }
        static uint8_t out[1024];
        int32_t w = dm_caps_encode(&c, out, sizeof out);
        if (w != (int32_t) n || memcmp(out, b, n) != 0) abort();
        (void) dm_caps_can_run(&c, DM_MODEL_LARGE);
    }
    free(b);
}

/* ---- mode 2: money ---- */

static uint64_t be64(const uint8_t *q)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | q[i];
    return v;
}

static void mode_money(const uint8_t *p, size_t n)
{
    uint8_t *b = fz_dup(p, n);
    dm_money_t a;
    if (dm_money_decode(&a, b, (uint32_t) n) == DM_OK) {
        if (n != DM_MONEY_WIRE) abort();
        if (a.kind < DM_MONEY_PAY || a.kind > DM_MONEY_SIGN) abort();
        if (a.rail != DM_RAIL_DEBIT && a.rail != DM_RAIL_CREDIT && a.rail != DM_RAIL_EQUITY)
            abort();
        if (memcmp(a.currency, "VFV", 4) != 0 || a.memo[DM_MONEY_MEMO - 1] != 0) abort();
        if (a.amount == 0 || a.amount > DM_MONEY_AMOUNT_MAX) abort();
        if (a.amount != be64(b + DM_ID_BYTES + 1u + 2u + 4u)) abort();
        uint8_t out[DM_MONEY_WIRE];
        int32_t w = dm_money_encode(&a, out, sizeof out);
        if (w != (int32_t) n || memcmp(out, b, n) != 0) abort();
    } else if (n == DM_MONEY_WIRE) {
        /* a refusal of a full-length record names a broken rule */
        const uint8_t *q = b + DM_ID_BYTES;
        uint16_t rail = (uint16_t) ((q[1] << 8) | q[2]);
        bool ok = q[0] >= DM_MONEY_PAY && q[0] <= DM_MONEY_SIGN &&
                  (rail == DM_RAIL_DEBIT || rail == DM_RAIL_CREDIT || rail == DM_RAIL_EQUITY) &&
                  !memcmp(q + 3, "VFV", 4) && be64(q + 7) != 0 &&
                  be64(q + 7) <= DM_MONEY_AMOUNT_MAX &&
                  b[DM_ID_BYTES + 15u + DM_HASH_BYTES + DM_MONEY_MEMO - 1u] == 0;
        if (ok) abort();
    }
    free(b);
}

/* ---- mode 3: confirm ---- */

static uint8_t g_cbuf[DM_CONFIRM_WIRE + 16];

static void mode_confirm(const uint8_t *p, size_t n)
{
    if (n > DM_CONFIRM_WIRE + 8) return;
    uint8_t *b = fz_dup(p, n);
    static dm_confirm_t c;
    if (dm_confirm_decode(&c, b, (uint32_t) n) == DM_OK) {
        if (c.approve > 1u || c.sig_len > PQM_SIG_MAX_BYTES) abort();
        int32_t w = dm_confirm_encode(&c, g_cbuf, sizeof g_cbuf);
        if (w != (int32_t) n || memcmp(g_cbuf, b, n) != 0) abort();
    }
    free(b);
}

/* ---- mode 4: wallet authorisation ---- */

static dm_mesh_t g_mesh;
static bool g_ready;
static uint8_t g_pay[4 + DM_MONEY_WIRE + DM_CONFIRM_WIRE];
static uint32_t g_paylen;
static uint8_t g_decl[4 + DM_MONEY_WIRE + DM_CONFIRM_WIRE];
static uint32_t g_decllen;
static uint64_t g_ctr;
#define T0 1000000u

static void h_random(void *ctx, uint8_t *out, uint32_t n)
{
    (void) ctx;
    uint8_t in[16] = "fuzz-devmesh";
    for (int i = 0; i < 4; i++) in[12 + i] = (uint8_t) (g_ctr >> (8 * i));
    g_ctr++;
    shake256(in, sizeof in, out, n);
}

static int h_send(void *ctx, const uint8_t to[DM_ID_BYTES], const uint8_t *f, uint32_t len)
{
    (void) ctx;
    (void) to;
    (void) f;
    (void) len;
    return 0;
}

static uint32_t build_payload(const dm_money_t *a, bool approve, uint8_t *out)
{
    static dm_confirm_t c;
    if (dm_money_confirm_local(&g_mesh, a, approve, T0, &c) != DM_OK) abort();
    int32_t ml = dm_money_encode(a, out + 4, DM_MONEY_WIRE);
    if (ml != (int32_t) DM_MONEY_WIRE) abort();
    for (int i = 0; i < 4; i++) out[i] = (uint8_t) ((uint32_t) ml >> (24 - 8 * i));
    int32_t cl = dm_confirm_encode(&c, out + 4 + ml, DM_CONFIRM_WIRE);
    if (cl < 0) abort();
    return 4u + (uint32_t) (ml + cl);
}

static void setup_mesh(void)
{
    dm_host_t h;
    memset(&h, 0, sizeof h);
    h.random = h_random;
    h.send = h_send;
    uint8_t seed[64];
    h_random(NULL, seed, 64);
    if (dm_init(&g_mesh, &h, PQM_LEVEL_STANDARD, seed, "fuzz", DM_ROLE_THIN, DM_FLAG_HELD) !=
        DM_OK)
        abort();
    if (dm_create(&g_mesh, T0) != DM_OK) abort();
    dm_money_t a;
    memset(&a, 0, sizeof a);
    memset(a.action_id, 0x5A, DM_ID_BYTES);
    a.kind = DM_MONEY_PAY;
    a.rail = DM_RAIL_DEBIT;
    memcpy(a.currency, "VFV", 4);
    a.amount = 12500;
    memset(a.payee, 0x77, DM_HASH_BYTES);
    memcpy(a.memo, "coffee", 7);
    a.expires_ms = T0 + 60000u;
    g_paylen = build_payload(&a, true, g_pay);
    a.action_id[0] ^= 1;
    g_decllen = build_payload(&a, false, g_decl);
    g_ready = true;
}

static void mode_wallet(const uint8_t *p, size_t n)
{
    if (!g_ready) setup_mesh();
    fz_in in;
    fz_init(&in, p, n);
    uint8_t flags = fz_u8(&in);
    static uint8_t buf[sizeof g_pay];
    const uint8_t *src = (flags & 1u) ? g_decl : g_pay;
    uint32_t len = (flags & 1u) ? g_decllen : g_paylen;
    memcpy(buf, src, len);
    uint32_t nm = fz_u8(&in) % 5u;
    for (uint32_t k = 0; k < nm; k++) {
        uint16_t pos = fz_u16(&in);
        uint8_t x = fz_u8(&in);
        uint32_t at;
        /* half of the flips land in the money action and header */
        at = (pos & 0x8000u) ? (uint32_t) (pos & 0x7fffu) % len
                             : (uint32_t) (pos % (4u + DM_MONEY_WIRE + 40u));
        if (at < len) buf[at] ^= x;
    }
    if (flags & 2u) len = len - 1u - fz_u8(&in) % 8u; /* truncated */
    uint64_t now = T0 + ((flags & 4u) ? 60001u : (uint64_t) fz_u16(&in));
    bool unchanged = len == ((flags & 1u) ? g_decllen : g_paylen) && memcmp(buf, src, len) == 0;
    g_mesh.money_seen_n = 0; /* a fresh replay ring for each input */
    uint8_t *b = fz_dup(buf, len);
    dm_money_t out;
    dm_status_t st = dm_wallet_check(&g_mesh, b, len, now, &out);
    bool expect = unchanged && !(flags & 1u) && now <= T0 + 60000u;
    if ((st == DM_OK) != expect) abort();
    if (st == DM_OK) {
        if (out.amount != 12500u || out.rail != DM_RAIL_DEBIT) abort();
        if (dm_wallet_check(&g_mesh, b, len, now, &out) != DM_ERR_REPLAY) abort();
    }
    if (unchanged && (flags & 1u) && now <= T0 + 60000u && st != DM_ERR_PERM) abort();
    free(b);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1 || size > 8192) return 0;
    const uint8_t *p = data + 1;
    size_t n = size - 1;
    switch (data[0] % 5u) {
    case 0: mode_b32(p, n); break;
    case 1: mode_caps(p, n); break;
    case 2: mode_money(p, n); break;
    case 3: mode_confirm(p, n); break;
    default: mode_wallet(p, n); break;
    }
    return 0;
}
