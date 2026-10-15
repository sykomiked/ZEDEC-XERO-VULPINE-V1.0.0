/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_opal.c — the Opal host against the fake TPer (fake_tper.c): tokens,
 * golden command bytes, Level 0 Discovery, the unlock with right and wrong
 * keys, User1 and Admin1, MBR shadowing, ranges, hostile responses (lengths
 * past the buffer, truncation, wrong ComID or session, bad tokens), random
 * corruption, and the arbiter key release end to end. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opal.h"
#include "opal_arb.h"
#include "fake_tper.h"
#include "../arbiter/arb_dev.h"

static int checks, failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
        }                                                                                          \
    } while (0)

static const uint8_t SMUID[8] = {0, 0, 0, 0, 0, 0, 0, 0xFF};
static const uint8_t LOCKINGSP[8] = {0, 0, 2, 5, 0, 0, 0, 2};
static const uint8_t ADMIN1[8] = {0, 0, 0, 9, 0, 1, 0, 1};
static const uint8_t USER1[8] = {0, 0, 0, 9, 0, 3, 0, 1};
static const uint8_t STARTSESSION[8] = {0, 0, 0, 0, 0, 0, 0xFF, 2};
static const uint8_t SET[8] = {0, 0, 0, 6, 0, 0, 0, 0x17};
static const uint8_t GLOBAL[8] = {0, 0, 8, 2, 0, 0, 0, 1};
static const uint8_t MBRCTL[8] = {0, 0, 8, 3, 0, 0, 0, 1};

static uint8_t g_key[32], g_ukey[32];
static ftper_t g_f;
static opal_dev_t g_d;
static const opal_auth_t A1 = {OPAL_AUTH_ADMIN1, 0};
static const opal_auth_t U1 = {OPAL_AUTH_USER, 1};

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}
static void wbe32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}
static void wbe16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 8);
    p[1] = (uint8_t) v;
}

static int all_zero(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (p[i]) return 0;
    return 1;
}

/* A drive with the global range and range 2 locked, Admin1 = g_key, User1 =
 * g_ukey. */
static void fresh(void)
{
    for (uint32_t i = 0; i < 32; i++) {
        g_key[i] = (uint8_t) (0xC0 + i * 3);
        g_ukey[i] = (uint8_t) (0x11 * (i + 1));
    }
    ftper_init(&g_f, g_key, 32, g_ukey, 32);
    g_f.rl[0] = g_f.wl[0] = 1;
    g_f.rl[2] = g_f.wl[2] = 1;
    opal_init(&g_d, ftper_send, ftper_recv, &g_f);
}

/* ---- tokens --------------------------------------------------------------- */

static void test_tokens(void)
{
    opal_tok_t t[8];
    {
        const uint8_t b[] = {0x05, 0x45, 0x3F, 0xFF, 0xF0, 0xF1, 0xF9};
        CHECK(opal_tokenize(b, 0, sizeof b, t, 8) == 6);
        CHECK(t[0].kind == OPAL_K_UINT && t[0].u == 5);
        CHECK(t[1].kind == OPAL_K_SINT && t[1].u == 5);
        CHECK(t[2].kind == OPAL_K_UINT && t[2].u == 63);
        CHECK(t[3].kind == OPAL_K_CTRL && t[3].ctl == 0xF0); /* Empty dropped */
        CHECK(t[4].kind == OPAL_K_CTRL && t[4].ctl == 0xF1);
        CHECK(t[5].kind == OPAL_K_CTRL && t[5].ctl == 0xF9);
    }
    {
        const uint8_t b[] = {0x81, 0x40, 0x82, 0x10, 0x01, 0x88, 1,    2,
                             3,    4,    5,    6,    7,    8,    0x91, 0xFF};
        CHECK(opal_tokenize(b, 0, sizeof b, t, 8) == 4);
        CHECK(t[0].kind == OPAL_K_UINT && t[0].u == 0x40 && t[0].len == 1);
        CHECK(t[1].kind == OPAL_K_UINT && t[1].u == 0x1001 && t[1].len == 2);
        CHECK(t[2].kind == OPAL_K_UINT && t[2].u == 0x0102030405060708ULL);
        CHECK(t[3].kind == OPAL_K_SINT && t[3].u == 0xFF);
    }
    {
        /* bytes: short, medium, long; offsets include the base */
        uint8_t b[64];
        memset(b, 0, sizeof b);
        b[3] = 0xA3;
        b[4] = 'a';
        b[5] = 'b';
        b[6] = 'c';
        b[7] = 0xD0;
        b[8] = 0x10;
        for (int i = 0; i < 16; i++) b[9 + i] = (uint8_t) i;
        b[25] = 0xE2;
        b[26] = 0;
        b[27] = 0;
        b[28] = 2;
        b[29] = 'x';
        b[30] = 'y';
        b[31] = 0xA0; /* empty byte string */
        CHECK(opal_tokenize(b, 3, 29, t, 8) == 4);
        CHECK(t[0].kind == OPAL_K_BYTES && t[0].off == 4 && t[0].len == 3);
        CHECK(t[1].kind == OPAL_K_BYTES && t[1].off == 9 && t[1].len == 16);
        CHECK(t[2].kind == OPAL_K_BYTES && t[2].off == 29 && t[2].len == 2);
        CHECK(t[3].kind == OPAL_K_BYTES && t[3].len == 0);
        CHECK(memcmp(b + t[0].off, "abc", 3) == 0 && memcmp(b + t[2].off, "xy", 2) == 0);
    }
    /* every reserved token value is refused */
    for (uint32_t h = 0xE4; h <= 0xFE; h++) {
        uint8_t b[1] = {(uint8_t) h};
        int want_ok = (h >= 0xF0 && h <= 0xF3) || (h >= 0xF8 && h <= 0xFC);
        int r = opal_tokenize(b, 0, 1, t, 8);
        CHECK(want_ok ? r == 1 : r == OPAL_E_TOKEN);
    }
    /* truncation and bad atoms */
    {
        const uint8_t a[] = {0xA4, 1, 2, 3};
        CHECK(opal_tokenize(a, 0, sizeof a, t, 8) == OPAL_E_TOKEN);
        const uint8_t b[] = {0xD0};
        CHECK(opal_tokenize(b, 0, 1, t, 8) == OPAL_E_TOKEN);
        const uint8_t c[] = {0xD0, 0x05, 1, 2, 3, 4};
        CHECK(opal_tokenize(c, 0, sizeof c, t, 8) == OPAL_E_TOKEN);
        const uint8_t d[] = {0xE2, 0, 0};
        CHECK(opal_tokenize(d, 0, sizeof d, t, 8) == OPAL_E_TOKEN);
        const uint8_t e[] = {0xE2, 0xFF, 0xFF, 0xFF, 1};
        CHECK(opal_tokenize(e, 0, sizeof e, t, 8) == OPAL_E_TOKEN);
        const uint8_t g[] = {0x80};
        CHECK(opal_tokenize(g, 0, 1, t, 8) == OPAL_E_TOKEN); /* zero-length integer */
        const uint8_t h[] = {0xC0, 9, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        CHECK(opal_tokenize(h, 0, sizeof h, t, 8) == OPAL_E_TOKEN); /* 9-byte integer */
        const uint8_t k[] = {0xB1, 7};
        CHECK(opal_tokenize(k, 0, sizeof k, t, 8) == OPAL_E_TOKEN); /* continued bytes */
        const uint8_t m[] = {0xC0, 8, 1, 2, 3, 4, 5, 6, 7, 8};
        CHECK(opal_tokenize(m, 0, sizeof m, t, 8) == 1 && t[0].u == 0x0102030405060708ULL);
        const uint8_t l[] = {0xE0, 0, 0, 1, 0x7F};
        CHECK(opal_tokenize(l, 0, sizeof l, t, 8) == 1 && t[0].kind == OPAL_K_UINT &&
              t[0].u == 0x7F);
        const uint8_t sh[] = {0xA5, 1, 2, 3, 4};
        CHECK(opal_tokenize(sh, 0, sizeof sh, t, 8) == OPAL_E_TOKEN);
        CHECK(opal_tokenize(sh, 0, 0, t, 8) == 0);
    }
    /* the token array bound */
    {
        uint8_t b[10];
        memset(b, 1, sizeof b);
        CHECK(opal_tokenize(b, 0, 8, t, 8) == 8);
        CHECK(opal_tokenize(b, 0, 9, t, 8) == OPAL_E_TOOBIG);
        memset(b, 0xFF, sizeof b);
        CHECK(opal_tokenize(b, 0, 10, t, 0) == 0); /* Empty atoms only */
    }
}

/* ---- UIDs ---------------------------------------------------------------- */

static void test_uids(void)
{
    uint8_t u[8];
    opal_uid_auth(A1, u);
    CHECK(memcmp(u, ADMIN1, 8) == 0);
    opal_uid_auth(U1, u);
    CHECK(memcmp(u, USER1, 8) == 0);
    opal_auth_t u9 = {OPAL_AUTH_USER, 0x1234};
    opal_uid_auth(u9, u);
    const uint8_t w9[8] = {0, 0, 0, 9, 0, 3, 0x12, 0x34};
    CHECK(memcmp(u, w9, 8) == 0);
    opal_uid_range(0, u);
    CHECK(memcmp(u, GLOBAL, 8) == 0);
    opal_uid_range(5, u);
    const uint8_t r5[8] = {0, 0, 8, 2, 0, 3, 0, 5};
    CHECK(memcmp(u, r5, 8) == 0);
    opal_uid_range(64, u);
    CHECK(u[5] == 3 && u[6] == 0 && u[7] == 64);
}

/* ---- Level 0 Discovery ------------------------------------------------------ */

static void desc(uint8_t *b, uint32_t *o, uint32_t code, uint32_t len)
{
    wbe16(b + *o, code);
    b[*o + 2] = 0x10;
    b[*o + 3] = (uint8_t) len;
    *o += 4 + len;
}

static void test_discovery(void)
{
    uint8_t b[2048];
    opal_disc_t ds;
    fresh();
    uint32_t n = ftper_level0(&g_f, b, sizeof b);
    CHECK(n == 48 + 16 + 16 + 32 + 20);
    CHECK(opal_parse_discovery(b, sizeof b, &ds) == OPAL_OK);
    CHECK(ds.revision == 1 && ds.have_tper && ds.have_locking && !ds.enterprise);
    CHECK(ds.ssc == OPAL_SSC_OPAL2 && ds.base_comid == 0x07FE && ds.num_comids == 1);
    CHECK(ds.num_admins == 4 && ds.num_users == 8);
    CHECK(ds.features == 4 && ds.unknown == 0);
    CHECK(ds.locking_flags ==
          (OPAL_LK_SUPPORTED | OPAL_LK_ENABLED | OPAL_LK_LOCKED | OPAL_LK_MEDIAENC));
    CHECK(ds.tper_flags == 0x11);
    CHECK(opal_disc_usable(&ds) == OPAL_OK);
    CHECK(opal_needs_unlock(&ds));
    /* exactly the bytes used is enough; one short is not */
    CHECK(opal_parse_discovery(b, n, &ds) == OPAL_OK);
    CHECK(opal_parse_discovery(b, n - 1, &ds) == OPAL_E_DISC);
    CHECK(opal_parse_discovery(b, 47, &ds) == OPAL_E_DISC);
    CHECK(opal_parse_discovery(0, 2048, &ds) == OPAL_E_DISC);
    /* every SSC the host accepts, and the one it does not */
    static const struct {
        uint16_t code;
        opal_ssc_t ssc;
        int users;
    } sscs[] = {{0x0203, OPAL_SSC_OPAL2, 8},
                {0x0200, OPAL_SSC_OPAL1, 0},
                {0x0304, OPAL_SSC_RUBY, 8},
                {0x0302, OPAL_SSC_PYRITE1, 0},
                {0x0303, OPAL_SSC_PYRITE2, 0}};
    for (uint32_t i = 0; i < 5; i++) {
        fresh();
        g_f.ssc_code = sscs[i].code;
        ftper_level0(&g_f, b, sizeof b);
        CHECK(opal_parse_discovery(b, sizeof b, &ds) == OPAL_OK);
        CHECK(ds.ssc == sscs[i].ssc);
        CHECK(ds.num_users == sscs[i].users);
        CHECK(opal_disc_usable(&ds) == OPAL_OK);
        CHECK(opal_ssc_str(ds.ssc)[0] != 'n');
    }
    fresh();
    g_f.ssc_code = 0;
    g_f.enterprise = 1;
    ftper_level0(&g_f, b, sizeof b);
    CHECK(opal_parse_discovery(b, sizeof b, &ds) == OPAL_OK && ds.enterprise);
    CHECK(opal_disc_usable(&ds) == OPAL_E_UNSUPPORTED);
    fresh();
    g_f.ssc_code = 0;
    ftper_level0(&g_f, b, sizeof b);
    CHECK(opal_parse_discovery(b, sizeof b, &ds) == OPAL_OK);
    CHECK(opal_disc_usable(&ds) == OPAL_E_UNSUPPORTED);
    fresh();
    g_f.no_tper = 1;
    ftper_level0(&g_f, b, sizeof b);
    CHECK(opal_parse_discovery(b, sizeof b, &ds) == OPAL_OK);
    CHECK(opal_disc_usable(&ds) == OPAL_E_UNSUPPORTED);
    fresh();
    g_f.no_locking = 1;
    ftper_level0(&g_f, b, sizeof b);
    CHECK(opal_parse_discovery(b, sizeof b, &ds) == OPAL_OK);
    CHECK(opal_disc_usable(&ds) == OPAL_E_NOT_SED);
    CHECK(!opal_needs_unlock(&ds));
    fresh();
    g_f.locking_supported = 0;
    ftper_level0(&g_f, b, sizeof b);
    CHECK(opal_parse_discovery(b, sizeof b, &ds) == OPAL_OK);
    CHECK(opal_disc_usable(&ds) == OPAL_E_NOT_SED);

    /* handmade: Opal 1 then Opal 2 then Pyrite: Opal 2 wins whatever the order */
    memset(b, 0, sizeof b);
    uint32_t o = 48;
    desc(b, &o, 0x0001, 12);
    desc(b, &o, 0x0002, 12);
    b[o - 12] = 0x07;
    wbe16(b + o + 4, 0x0100);
    wbe16(b + o + 6, 2);
    desc(b, &o, 0x0200, 12);
    wbe16(b + o + 4, 0x0200);
    wbe16(b + o + 6, 3);
    desc(b, &o, 0x0203, 16);
    wbe16(b + o + 4, 0x0300);
    wbe16(b + o + 6, 4);
    desc(b, &o, 0x0302, 12);
    desc(b, &o, 0x0BAD, 0);
    desc(b, &o, 0x0C01, 3);
    desc(b, &o, 0x0402, 8);
    desc(b, &o, 0x0201, 12);
    desc(b, &o, 0x0202, 12);
    wbe32(b, o - 4);
    CHECK(opal_parse_discovery(b, o, &ds) == OPAL_OK);
    CHECK(ds.ssc == OPAL_SSC_OPAL2 && ds.base_comid == 0x0200 && ds.num_comids == 3);
    CHECK(ds.unknown == 2 && ds.first_unknown == 0x0BAD && ds.features == 10);
    CHECK(ds.locking_flags == 0x07);
    /* lengths past the end, trailing bytes, short descriptors */
    uint8_t c[2048];
    memcpy(c, b, sizeof c);
    wbe32(c, o - 3); /* one byte past what the descriptors fill: a 1-byte tail */
    CHECK(opal_parse_discovery(c, o + 1, &ds) == OPAL_E_DISC);
    wbe32(c, o + 100); /* header claims more than the buffer holds */
    CHECK(opal_parse_discovery(c, o + 50, &ds) == OPAL_E_DISC);
    wbe32(c, 0xFFFFFFFFu);
    CHECK(opal_parse_discovery(c, sizeof c, &ds) == OPAL_E_DISC);
    wbe32(c, 43); /* shorter than the header */
    CHECK(opal_parse_discovery(c, sizeof c, &ds) == OPAL_E_DISC);
    wbe32(c, 44); /* header only: no features */
    CHECK(opal_parse_discovery(c, sizeof c, &ds) == OPAL_OK && ds.features == 0);
    CHECK(opal_disc_usable(&ds) == OPAL_E_NOT_SED);
    memcpy(c, b, sizeof c);
    c[48 + 3] = 250; /* TPer descriptor runs past the data */
    CHECK(opal_parse_discovery(c, o, &ds) == OPAL_E_DISC);
    memcpy(c, b, sizeof c);
    c[48 + 3] = 0; /* TPer with no flags byte: then the next descriptor misparses */
    CHECK(opal_parse_discovery(c, o, &ds) == OPAL_E_DISC);
    /* one descriptor alone, at each short length */
    for (uint32_t code_i = 0; code_i < 4; code_i++) {
        static const uint16_t codes[4] = {0x0001, 0x0002, 0x0203, 0x0302};
        for (uint32_t fl = 0; fl < 6; fl++) {
            memset(c, 0, sizeof c);
            uint32_t q = 48;
            desc(c, &q, codes[code_i], fl);
            if (fl >= 4) {
                wbe16(c + 52, 0x0800);
                wbe16(c + 54, 1);
            }
            wbe32(c, q - 4);
            int want_min = code_i < 2 ? 1 : 4;
            CHECK(opal_parse_discovery(c, q, &ds) ==
                  ((int) fl >= want_min ? OPAL_OK : OPAL_E_DISC));
        }
    }
    /* ComID 0, ComID 1 (Level 0's own) and no ComIDs are refused */
    static const uint16_t bad[3][2] = {{0, 1}, {1, 1}, {0x0800, 0}};
    for (uint32_t i = 0; i < 3; i++) {
        memset(c, 0, sizeof c);
        uint32_t q = 48;
        desc(c, &q, 0x0203, 16);
        wbe16(c + 52, bad[i][0]);
        wbe16(c + 54, bad[i][1]);
        wbe32(c, q - 4);
        CHECK(opal_parse_discovery(c, q, &ds) == OPAL_E_DISC);
    }
    /* needs_unlock over the flag combinations */
    opal_disc_t z;
    memset(&z, 0, sizeof z);
    for (uint32_t f = 0; f < 64; f++) {
        z.locking_flags = (uint8_t) f;
        int want = (f & 2) && ((f & 4) || ((f & 0x10) && !(f & 0x20)));
        CHECK(opal_needs_unlock(&z) == want);
    }
}

/* ---- golden command bytes ---------------------------------------------------- */

static uint32_t golden_start(uint8_t *p, const uint8_t *key, uint32_t kl, const uint8_t auth[8])
{
    uint32_t n = 0;
    p[n++] = 0xF8;
    p[n++] = 0xA8;
    memcpy(p + n, SMUID, 8);
    n += 8;
    p[n++] = 0xA8;
    memcpy(p + n, STARTSESSION, 8);
    n += 8;
    p[n++] = 0xF0;
    p[n++] = 0x81;
    p[n++] = 0x41;
    p[n++] = 0xA8;
    memcpy(p + n, LOCKINGSP, 8);
    n += 8;
    p[n++] = 0x01;
    p[n++] = 0xF2;
    p[n++] = 0x00;
    if (kl < 16) {
        p[n++] = (uint8_t) (0xA0 | kl);
    } else {
        p[n++] = 0xD0;
        p[n++] = (uint8_t) kl;
    }
    memcpy(p + n, key, kl);
    n += kl;
    p[n++] = 0xF3;
    p[n++] = 0xF2;
    p[n++] = 0x03;
    p[n++] = 0xA8;
    memcpy(p + n, auth, 8);
    n += 8;
    p[n++] = 0xF3;
    p[n++] = 0xF1;
    const uint8_t st[6] = {0xF9, 0xF0, 0, 0, 0, 0xF1};
    memcpy(p + n, st, 6);
    return n + 6;
}

static void check_frame(const uint8_t *c, uint32_t sub, uint32_t tsn, uint32_t hsn)
{
    uint32_t pad = (sub + 3) & ~3u;
    CHECK(be32(c) == 0 && c[4] == 0x07 && c[5] == 0xFE && c[6] == 0 && c[7] == 0);
    CHECK(be32(c + 8) == 0 && be32(c + 12) == 0);
    CHECK(be32(c + 16) == 24 + 12 + pad);
    CHECK(be32(c + 20) == tsn && be32(c + 24) == hsn && be32(c + 28) == 0);
    CHECK(be32(c + 32) == 0 && be32(c + 36) == 0);
    CHECK(be32(c + 40) == 12 + pad);
    CHECK(all_zero(c + 44, 6) && c[50] == 0 && c[51] == 0);
    CHECK(be32(c + 52) == sub);
    CHECK(all_zero(c + 56 + sub, OPAL_MAX_COMPACKET - 56 - sub));
}

static void test_golden(void)
{
    uint8_t want[256];
    /* 32-byte key: medium atom; 88 bytes, already a multiple of 4 */
    fresh();
    CHECK(opal_discover(&g_d) == OPAL_OK);
    CHECK(g_d.comid == 0x07FE);
    CHECK(opal_start_session(&g_d, A1, g_key, 32) == OPAL_OK);
    uint32_t n = golden_start(want, g_key, 32, ADMIN1);
    CHECK(n == 88);
    CHECK(g_f.last_len == OPAL_MAX_COMPACKET);
    CHECK(memcmp(g_f.last_cmd + 56, want, n) == 0);
    check_frame(g_f.last_cmd, n, 0, 0);
    CHECK(g_f.strict == 0);
    CHECK(g_d.in_session && g_d.tsn == 0x1001 && g_d.hsn == 0x41);
    CHECK(all_zero(g_d.cmd, OPAL_MAX_COMPACKET)); /* the key is gone from our buffer */
    /* Set ReadLocked 0 WriteLocked 0 on the global range, in the session */
    CHECK(opal_set_range(&g_d, 0, 0, 0) == OPAL_OK);
    const uint8_t set[] = {0xF8, 0xA8, 0,    0,    8,    2,    0,    0,    0,    1,
                           0xA8, 0,    0,    0,    6,    0,    0,    0,    0x17, 0xF0,
                           0xF2, 0x01, 0xF0, 0xF2, 0x07, 0x00, 0xF3, 0xF2, 0x08, 0x00,
                           0xF3, 0xF1, 0xF3, 0xF1, 0xF9, 0xF0, 0,    0,    0,    0xF1};
    CHECK(memcmp(g_f.last_cmd + 56, set, sizeof set) == 0);
    CHECK(memcmp(g_f.last_cmd + 58, GLOBAL, 8) == 0 && memcmp(g_f.last_cmd + 67, SET, 8) == 0);
    check_frame(g_f.last_cmd, sizeof set, 0x1001, 0x41);
    CHECK(g_f.rl[0] == 0 && g_f.wl[0] == 0 && g_f.rl[2] == 1);
    /* lock it again: values 1 */
    CHECK(opal_set_range(&g_d, 0, 1, 0) == OPAL_OK);
    CHECK(g_f.last_cmd[56 + 25] == 0x01 && g_f.last_cmd[56 + 29] == 0x00);
    CHECK(g_f.rl[0] == 1 && g_f.wl[0] == 0);
    CHECK(opal_set_range(&g_d, 0, 0, 1) == OPAL_OK);
    CHECK(g_f.rl[0] == 0 && g_f.wl[0] == 1);
    /* MBRControl Done = 1 */
    CHECK(opal_set_mbr_done(&g_d, 1) == OPAL_OK);
    const uint8_t mbr[] = {0xF8, 0xA8, 0,    0,    8,    3,    0,    0,    0,    1,    0xA8, 0,
                           0,    0,    6,    0,    0,    0,    0x17, 0xF0, 0xF2, 0x01, 0xF0, 0xF2,
                           0x02, 0x01, 0xF3, 0xF1, 0xF3, 0xF1, 0xF9, 0xF0, 0,    0,    0,    0xF1};
    CHECK(memcmp(g_f.last_cmd + 56, mbr, sizeof mbr) == 0);
    CHECK(memcmp(g_f.last_cmd + 58, MBRCTL, 8) == 0 && memcmp(g_f.last_cmd + 67, SET, 8) == 0);
    check_frame(g_f.last_cmd, sizeof mbr, 0x1001, 0x41);
    CHECK(g_f.mbr_done == 1);
    CHECK(opal_set_mbr_done(&g_d, 0) == OPAL_OK && g_f.mbr_done == 0);
    /* EndSession: the token alone, padded */
    CHECK(opal_end_session(&g_d) == OPAL_OK);
    CHECK(g_f.last_cmd[56] == 0xFA);
    check_frame(g_f.last_cmd, 1, 0x1001, 0x41);
    CHECK(!g_d.in_session && !g_f.in_session && g_f.ends == 1);
    CHECK(g_f.strict == 0);

    /* a 6-byte User1 key: short atom, 61 bytes padded to 64 */
    const uint8_t k6[6] = {1, 2, 3, 4, 5, 6};
    ftper_init(&g_f, g_key, 32, k6, 6);
    opal_init(&g_d, ftper_send, ftper_recv, &g_f);
    CHECK(opal_discover(&g_d) == OPAL_OK);
    CHECK(opal_start_session(&g_d, U1, k6, 6) == OPAL_OK);
    n = golden_start(want, k6, 6, USER1);
    CHECK(n == 61);
    CHECK(memcmp(g_f.last_cmd + 56, want, n) == 0);
    check_frame(g_f.last_cmd, 61, 0, 0);
    CHECK(be32(g_f.last_cmd + 16) == 100);
    CHECK(g_f.strict == 0);
    CHECK(opal_end_session(&g_d) == OPAL_OK);
    /* a 15-byte key is the longest short atom, 16 the shortest medium */
    for (uint32_t kl = 15; kl <= 16; kl++) {
        ftper_init(&g_f, g_key, kl, 0, 0);
        opal_init(&g_d, ftper_send, ftper_recv, &g_f);
        CHECK(opal_discover(&g_d) == OPAL_OK);
        CHECK(opal_start_session(&g_d, A1, g_key, kl) == OPAL_OK);
        n = golden_start(want, g_key, kl, ADMIN1);
        CHECK(memcmp(g_f.last_cmd + 56, want, n) == 0);
        CHECK(g_f.strict == 0);
        CHECK(opal_end_session(&g_d) == OPAL_OK);
    }
    /* a TSN that needs four bytes is carried in full */
    fresh();
    g_f.tsn_start = 0xFEDCBA98u;
    CHECK(opal_discover(&g_d) == OPAL_OK);
    CHECK(opal_start_session(&g_d, A1, g_key, 32) == OPAL_OK);
    CHECK(g_d.tsn == 0xFEDCBA98u);
    CHECK(opal_set_range(&g_d, 0, 0, 0) == OPAL_OK);
    CHECK(be32(g_f.last_cmd + 20) == 0xFEDCBA98u);
    CHECK(opal_end_session(&g_d) == OPAL_OK && g_f.strict == 0);
}

/* ---- state and argument errors ---------------------------------------------- */

static void test_state(void)
{
    fresh();
    CHECK(opal_start_session(&g_d, A1, g_key, 32) == OPAL_E_STATE); /* no discovery */
    CHECK(opal_set_range(&g_d, 0, 0, 0) == OPAL_E_STATE);
    CHECK(opal_set_mbr_done(&g_d, 1) == OPAL_E_STATE);
    CHECK(opal_end_session(&g_d) == OPAL_E_STATE);
    CHECK(g_f.sends == 0);
    CHECK(opal_discover(&g_d) == OPAL_OK);
    CHECK(opal_start_session(&g_d, A1, g_key, 0) == OPAL_E_ARG);
    CHECK(opal_start_session(&g_d, A1, 0, 32) == OPAL_E_ARG);
    CHECK(opal_start_session(&g_d, A1, g_key, 33) == OPAL_E_ARG);
    opal_auth_t u0 = {OPAL_AUTH_USER, 0}, u9 = {OPAL_AUTH_USER, 9}, u8 = {OPAL_AUTH_USER, 8};
    opal_auth_t bad = {(opal_auth_kind_t) 7, 1};
    CHECK(opal_start_session(&g_d, u0, g_key, 32) == OPAL_E_ARG);
    CHECK(opal_start_session(&g_d, u9, g_key, 32) == OPAL_E_ARG); /* drive has 8 users */
    CHECK(opal_start_session(&g_d, bad, g_key, 32) == OPAL_E_ARG);
    CHECK(g_f.sends == 0);
    CHECK(opal_start_session(&g_d, u8, g_key, 32) == OPAL_E_STATUS); /* no User8 password */
    CHECK(g_d.status == OPAL_ST_NOT_AUTHORIZED && !g_d.in_session);
    CHECK(opal_start_session(&g_d, A1, g_key, 32) == OPAL_OK);
    CHECK(opal_start_session(&g_d, A1, g_key, 32) == OPAL_E_STATE); /* already open */
    CHECK(opal_set_range(&g_d, OPAL_RANGES_MAX + 1, 0, 0) == OPAL_E_ARG);
    opal_unlock_result_t res;
    CHECK(opal_unlock(&g_d, A1, g_key, 32, 0, &res) == OPAL_E_STATE);
    CHECK(opal_end_session(&g_d) == OPAL_OK);
    CHECK(opal_end_session(&g_d) == OPAL_E_STATE);
    /* Opal 1 lists no user count: any user number up to 0xFFFF is allowed */
    fresh();
    g_f.ssc_code = 0x0200;
    CHECK(opal_discover(&g_d) == OPAL_OK && g_d.disc.num_users == 0);
    opal_auth_t big = {OPAL_AUTH_USER, 0x10000};
    CHECK(opal_start_session(&g_d, big, g_key, 32) == OPAL_E_ARG);
    CHECK(opal_start_session(&g_d, u9, g_key, 32) == OPAL_E_STATUS);
    /* a discovery that fails leaves no ComID behind */
    fresh();
    CHECK(opal_discover(&g_d) == OPAL_OK);
    g_f.no_locking = 1;
    CHECK(opal_discover(&g_d) == OPAL_E_NOT_SED);
    CHECK(g_d.comid == 0);
    CHECK(opal_start_session(&g_d, A1, g_key, 32) == OPAL_E_STATE);
    /* a builder overflow is refused, not sent */
    fresh();
    CHECK(opal_discover(&g_d) == OPAL_OK);
    g_d.polls = 0; /* treated as one poll */
    CHECK(opal_start_session(&g_d, A1, g_key, 32) == OPAL_OK);
    CHECK(opal_end_session(&g_d) == OPAL_OK);
}

/* ---- the unlock --------------------------------------------------------------- */

static void test_unlock(void)
{
    opal_unlock_result_t res;
    /* Admin1, right key: global range opens, range 2 stays locked */
    fresh();
    CHECK(opal_unlock(&g_d, A1, g_key, 32, 0, &res) == OPAL_OK);
    CHECK(res.needed == 1 && res.stage == OPAL_STAGE_NONE && res.status == 0);
    CHECK(res.flags_before & OPAL_LK_LOCKED);
    CHECK(res.flags_after & OPAL_LK_LOCKED); /* range 2 is still locked */
    CHECK(g_f.rl[0] == 0 && g_f.wl[0] == 0 && g_f.rl[2] == 1 && g_f.wl[2] == 1);
    CHECK(g_f.starts == 1 && g_f.sets == 1 && g_f.mbr_sets == 0 && g_f.ends == 1);
    CHECK(g_f.discs == 2 && g_f.strict == 0 && g_f.auth_fail == 0);
    CHECK(!g_d.in_session && !g_f.in_session);
    CHECK(memcmp(g_f.last_challenge, g_key, 32) == 0 && g_f.last_challenge_len == 32);
    CHECK(memcmp(g_f.last_authority, ADMIN1, 8) == 0);
    CHECK(all_zero(g_d.cmd, OPAL_MAX_COMPACKET));
    /* range 2 next, then nothing is locked */
    CHECK(opal_unlock(&g_d, A1, g_key, 32, 2, &res) == OPAL_OK);
    CHECK(g_f.rl[2] == 0 && g_f.wl[2] == 0);
    CHECK(!(res.flags_after & OPAL_LK_LOCKED) && (res.flags_before & OPAL_LK_LOCKED));
    /* now unlocked: no session at all, no key needed */
    uint32_t starts = g_f.starts;
    CHECK(opal_unlock(&g_d, A1, 0, 0, 0, &res) == OPAL_OK);
    CHECK(res.needed == 0 && g_f.starts == starts);
    CHECK(res.flags_before == res.flags_after);

    /* wrong key: NOT_AUTHORIZED at StartSession, nothing changes */
    fresh();
    uint8_t wrong[32];
    memcpy(wrong, g_key, 32);
    wrong[31] ^= 1;
    CHECK(opal_unlock(&g_d, A1, wrong, 32, 0, &res) == OPAL_E_STATUS);
    CHECK(res.stage == OPAL_STAGE_START && res.status == OPAL_ST_NOT_AUTHORIZED);
    CHECK(res.needed == 1);
    CHECK(g_f.rl[0] == 1 && g_f.wl[0] == 1);
    CHECK(g_f.sets == 0 && g_f.ends == 0 && g_f.auth_fail == 1);
    CHECK(!g_d.in_session && !g_f.in_session);
    CHECK(strcmp(opal_status_str(res.status), "NOT_AUTHORIZED") == 0);
    CHECK(strcmp(opal_stage_str(res.stage), "StartSession") == 0);
    /* a short key, or the User1 key presented as Admin1 */
    CHECK(opal_unlock(&g_d, A1, g_key, 31, 0, &res) == OPAL_E_STATUS);
    CHECK(opal_unlock(&g_d, A1, g_ukey, 32, 0, &res) == OPAL_E_STATUS);
    CHECK(g_f.auth_fail == 3 && g_f.rl[0] == 1);
    /* key argument errors are found before anything is sent */
    uint32_t sends = g_f.sends;
    CHECK(opal_unlock(&g_d, A1, g_key, 0, 0, &res) == OPAL_E_ARG);
    CHECK(res.stage == OPAL_STAGE_START && g_f.sends == sends);
    CHECK(opal_unlock(&g_d, A1, g_key, 32, OPAL_RANGES_MAX + 1, &res) == OPAL_E_ARG);
    CHECK(res.stage == OPAL_STAGE_RANGE && g_f.sends == sends);

    /* User1 */
    fresh();
    CHECK(opal_unlock(&g_d, U1, g_ukey, 32, 0, &res) == OPAL_OK);
    CHECK(g_f.rl[0] == 0 && memcmp(g_f.last_authority, USER1, 8) == 0);
    fresh();
    CHECK(opal_unlock(&g_d, U1, g_key, 32, 0, &res) == OPAL_E_STATUS);
    CHECK(res.status == OPAL_ST_NOT_AUTHORIZED && g_f.rl[0] == 1);
    /* User1 not granted the range: Set fails, the session still ends */
    fresh();
    g_f.user_ranges = ~1u;
    CHECK(opal_unlock(&g_d, U1, g_ukey, 32, 0, &res) == OPAL_E_STATUS);
    CHECK(res.stage == OPAL_STAGE_RANGE && res.status == OPAL_ST_NOT_AUTHORIZED);
    CHECK(g_f.rl[0] == 1 && g_f.ends == 1 && !g_f.in_session && !g_d.in_session);

    /* shadow MBR enabled, not done: Admin1 sets Done */
    fresh();
    g_f.mbr_enabled = 1;
    CHECK(opal_unlock(&g_d, A1, g_key, 32, 0, &res) == OPAL_OK);
    CHECK(g_f.mbr_done == 1 && g_f.mbr_sets == 1 && g_f.rl[0] == 0);
    CHECK((res.flags_before & OPAL_LK_MBR_EN) && !(res.flags_before & OPAL_LK_MBR_DONE));
    CHECK(res.flags_after & OPAL_LK_MBR_DONE);
    CHECK(g_f.strict == 0);
    /* already done: no MBR Set */
    fresh();
    g_f.mbr_enabled = 1;
    g_f.mbr_done = 1;
    CHECK(opal_unlock(&g_d, A1, g_key, 32, 0, &res) == OPAL_OK);
    CHECK(g_f.mbr_sets == 0 && g_f.rl[0] == 0);
    /* not locked, MBR not done: the session is opened for the MBR alone */
    fresh();
    memset(g_f.rl, 0, sizeof g_f.rl);
    memset(g_f.wl, 0, sizeof g_f.wl);
    g_f.mbr_enabled = 1;
    CHECK(opal_unlock(&g_d, A1, g_key, 32, 0, &res) == OPAL_OK);
    CHECK(res.needed == 1 && g_f.mbr_done == 1 && g_f.mbr_sets == 1);
    /* User1 may not touch MBRControl by default */
    fresh();
    g_f.mbr_enabled = 1;
    CHECK(opal_unlock(&g_d, U1, g_ukey, 32, 0, &res) == OPAL_E_STATUS);
    CHECK(res.stage == OPAL_STAGE_MBR && res.status == OPAL_ST_NOT_AUTHORIZED);
    CHECK(g_f.rl[0] == 0 && g_f.mbr_done == 0 && g_f.ends == 1 && !g_d.in_session);
    CHECK(strcmp(opal_stage_str(res.stage), "Set MBRControl") == 0);
    fresh();
    g_f.mbr_enabled = 1;
    g_f.user_may_mbr = 1;
    CHECK(opal_unlock(&g_d, U1, g_ukey, 32, 0, &res) == OPAL_OK && g_f.mbr_done == 1);

    /* locking not enabled: nothing to do, nothing sent */
    fresh();
    g_f.locking_enabled = 0;
    CHECK(opal_unlock(&g_d, A1, g_key, 32, 0, &res) == OPAL_OK);
    CHECK(res.needed == 0 && g_f.sends == 0);
    /* not an SED, Enterprise, transport refusal */
    fresh();
    g_f.no_locking = 1;
    CHECK(opal_unlock(&g_d, A1, g_key, 32, 0, &res) == OPAL_E_NOT_SED);
    CHECK(res.stage == OPAL_STAGE_DISCOVERY && g_f.sends == 0);
    fresh();
    g_f.ssc_code = 0;
    g_f.enterprise = 1;
    CHECK(opal_unlock(&g_d, A1, g_key, 32, 0, &res) == OPAL_E_UNSUPPORTED);
    fresh();
    g_f.fail_recv = 1;
    CHECK(opal_unlock(&g_d, A1, g_key, 32, 0, &res) == OPAL_E_TRANSPORT);
    CHECK(res.stage == OPAL_STAGE_DISCOVERY);
    fresh();
    g_f.fail_send = 1;
    CHECK(opal_unlock(&g_d, A1, g_key, 32, 0, &res) == OPAL_E_TRANSPORT);
    CHECK(res.stage == OPAL_STAGE_START && !g_d.in_session);
    CHECK(all_zero(g_d.cmd, OPAL_MAX_COMPACKET)); /* wiped even when the send failed */

    /* a range the drive does not have: INVALID_PARAMETER, session ended */
    fresh();
    CHECK(opal_unlock(&g_d, A1, g_key, 32, 9, &res) == OPAL_E_STATUS);
    CHECK(res.stage == OPAL_STAGE_RANGE && res.status == OPAL_ST_INVALID_PARAM);
    CHECK(g_f.ends == 1);
    for (uint32_t r = 1; r <= 8; r++) {
        fresh();
        g_f.rl[r] = g_f.wl[r] = 1;
        CHECK(opal_unlock(&g_d, A1, g_key, 32, r, &res) == OPAL_OK);
        CHECK(g_f.rl[r] == 0 && g_f.wl[r] == 0 && g_f.rl[0] == 1);
        for (uint32_t q = 1; q <= 8; q++)
            if (q != r && q != 2) CHECK(g_f.rl[q] == 0);
    }
    /* every Set answered with a status: each surfaces by name */
    static const uint8_t sts[] = {0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
                                  0x0C, 0x0F, 0x10, 0x11, 0x12, 0x3F, 0x02, 0x2A};
    for (uint32_t i = 0; i < sizeof sts; i++) {
        fresh();
        g_f.fail_status = sts[i];
        CHECK(opal_unlock(&g_d, A1, g_key, 32, 0, &res) == OPAL_E_STATUS);
        CHECK(res.status == sts[i] && res.stage == OPAL_STAGE_RANGE);
        CHECK(g_f.rl[0] == 1 && g_f.ends == 1);
        CHECK(opal_status_str(sts[i]) != 0);
    }
    CHECK(strcmp(opal_status_str(0x2A), "UNKNOWN_STATUS") == 0);
    CHECK(strcmp(opal_status_str(0x03), "SP_BUSY") == 0);

    /* a TPer that needs polling */
    fresh();
    g_f.busy = 5;
    CHECK(opal_unlock(&g_d, A1, g_key, 32, 0, &res) == OPAL_OK && g_f.rl[0] == 0);
    CHECK(g_f.recvs == 2 + 3 + 5);
    fresh();
    g_f.busy = 5;
    g_d.polls = 5; /* five IF-RECVs: all pending */
    CHECK(opal_unlock(&g_d, A1, g_key, 32, 0, &res) == OPAL_E_NO_RESPONSE);
    CHECK(res.stage == OPAL_STAGE_START && g_f.recvs == 1 + 5);
    fresh();
    g_f.busy = 5;
    g_d.polls = 6;
    CHECK(opal_unlock(&g_d, A1, g_key, 32, 0, &res) == OPAL_OK);
    fresh();
    g_f.silent = 1;
    CHECK(opal_unlock(&g_d, A1, g_key, 32, 0, &res) == OPAL_E_NO_RESPONSE);
    CHECK(g_f.recvs == 1 + OPAL_POLLS);
    CHECK(strcmp(opal_err_str(OPAL_E_NO_RESPONSE), "no response") == 0);
}

/* ---- hostile responses ------------------------------------------------------- */

static void m_cp_huge(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    wbe32(b + 16, 0xFFFFFFFFu);
}
static void m_cp_over(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    wbe32(b + 16, n - 20 + 1);
}
static void m_cp_max(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    wbe32(b + 16, n - 20); /* the largest the buffer holds: accepted */
}
static void m_cp_small(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    wbe32(b + 16, 35);
}
static void m_pkt_huge(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    wbe32(b + 40, 0xFFFFFFF0u);
}
static void m_pkt_over(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    wbe32(b + 40, be32(b + 16) - 24 + 1);
}
static void m_pkt_small(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    wbe32(b + 40, 11);
}
static void m_sub_huge(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    wbe32(b + 52, 0xFFFFFFF0u);
}
static void m_sub_over(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    wbe32(b + 52, be32(b + 40) - 12 + 1);
}
static void m_sub_kind(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    wbe16(b + 50, 0x8001);
}
static void m_trunc(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    wbe32(b + 52, be32(b + 52) - 3); /* the status list is cut */
}
static void m_trunc_atom(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    wbe32(b + 52, 5); /* inside the SMUID byte string */
}
static void m_comid(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    b[5] ^= 1;
}
static void m_comid_ext(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    b[7] = 1;
}
static void m_outstanding(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    wbe32(b + 8, 4096);
}
static void m_min_xfer(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    memset(b + 8, 0, n - 8);
    wbe32(b + 12, 4096); /* nothing sent; the answer needs 4 KB */
}
static void m_tsn(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    wbe32(b + 20, be32(b + 20) + 1);
}
static void m_hsn(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    wbe32(b + 24, 0x41);
}
static void m_bad_token(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    b[56] = 0xE5;
}
static void m_atom_overrun(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    b[56] = 0xD7; /* medium bytes of 0x7FF: past the subpacket */
    b[57] = 0xFF;
}
static void m_long_overrun(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    b[56] = 0xE2;
    b[57] = 0xFF;
    b[58] = 0xFF;
    b[59] = 0xFF;
}
static void m_unbalanced(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    b[57] = 0xF3; /* Set answer "F0 F1" becomes "F0 F3" */
}
static void m_extra_close(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    b[56] = 0xF1; /* "F1 F1": close before open */
}
static void m_status_bytes(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    uint32_t sl = be32(b + 52);
    b[56 + sl - 4] = 0xA0; /* status becomes an empty byte string */
}
static void m_no_eod(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    uint32_t sl = be32(b + 52);
    b[56 + sl - 6] = 0xFF; /* EndOfData becomes Empty */
}
static void m_eos(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    memset(b + 56, 0, 8);
    b[56] = 0xFA;
    wbe32(b + 52, 1);
}
static void m_sync_hsn(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    b[56 + 1 + 9 + 9 + 1 + 1] = 0x42; /* HostSessionID 0x41 -> 0x42 */
}
static void m_sync_uid(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    b[56 + 1 + 9 + 8] = 4; /* SyncSession -> 0xFF04 */
}
static void m_sync_not_call(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    b[56] = 0xFC; /* Call -> EndTransaction */
}
static void m_many_tokens(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    /* 100 tiny atoms then a status list */
    for (uint32_t i = 0; i < 100; i++) b[56 + i] = 1;
    const uint8_t st[6] = {0xF9, 0xF0, 0, 0, 0, 0xF1};
    memcpy(b + 156, st, 6);
    wbe32(b + 52, 106);
    wbe32(b + 40, 12 + 108);
    wbe32(b + 16, 24 + 12 + 108);
}
static void m_deep(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    /* 17 nested lists */
    uint32_t i = 0;
    for (; i < 17; i++) b[56 + i] = 0xF0;
    for (uint32_t k = 0; k < 17; k++) b[56 + i++] = 0xF1;
    const uint8_t st[6] = {0xF9, 0xF0, 0, 0, 0, 0xF1};
    memcpy(b + 56 + i, st, 6);
    i += 6;
    wbe32(b + 52, i);
    uint32_t pad = (i + 3) & ~3u;
    wbe32(b + 40, 12 + pad);
    wbe32(b + 16, 24 + 12 + pad);
}
static void m_status_big(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    /* Set answer: F0 F1 F9 F0 [82 01 00] 00 00 F1 */
    const uint8_t p[] = {0xF0, 0xF1, 0xF9, 0xF0, 0x82, 0x01, 0x00, 0, 0, 0xF1};
    memcpy(b + 56, p, sizeof p);
    wbe32(b + 52, sizeof p);
    wbe32(b + 40, 12 + 12);
    wbe32(b + 16, 24 + 12 + 12);
}
static void m_end_extra(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    (void) n;
    b[57] = 0xFA; /* EndSession answered with two tokens */
    wbe32(b + 52, 2);
}

static void test_hostile(void)
{
    static const struct {
        const char *what;
        void (*m)(ftper_t *, uint8_t *, uint32_t);
        uint32_t at; /* 1 StartSession, 2 Set, 3 EndSession */
        int want;
        opal_stage_t stage;
    } h[] = {
        {"ComPacket length all ones", m_cp_huge, 1, OPAL_E_FRAME, OPAL_STAGE_START},
        {"ComPacket length one past", m_cp_over, 2, OPAL_E_FRAME, OPAL_STAGE_RANGE},
        {"ComPacket length at the limit", m_cp_max, 2, OPAL_OK, OPAL_STAGE_NONE},
        {"ComPacket shorter than a packet", m_cp_small, 1, OPAL_E_FRAME, OPAL_STAGE_START},
        {"Packet length huge", m_pkt_huge, 1, OPAL_E_FRAME, OPAL_STAGE_START},
        {"Packet length one past", m_pkt_over, 2, OPAL_E_FRAME, OPAL_STAGE_RANGE},
        {"Packet shorter than a subpacket", m_pkt_small, 2, OPAL_E_FRAME, OPAL_STAGE_RANGE},
        {"SubPacket length huge", m_sub_huge, 1, OPAL_E_FRAME, OPAL_STAGE_START},
        {"SubPacket length one past", m_sub_over, 2, OPAL_E_FRAME, OPAL_STAGE_RANGE},
        {"SubPacket kind credit", m_sub_kind, 2, OPAL_E_FRAME, OPAL_STAGE_RANGE},
        {"truncated status list", m_trunc, 2, OPAL_E_TOKEN, OPAL_STAGE_RANGE},
        {"truncated atom", m_trunc_atom, 1, OPAL_E_TOKEN, OPAL_STAGE_START},
        {"wrong ComID", m_comid, 1, OPAL_E_COMID, OPAL_STAGE_START},
        {"ComID extension", m_comid_ext, 2, OPAL_E_COMID, OPAL_STAGE_RANGE},
        {"outstanding data", m_outstanding, 2, OPAL_E_TOOBIG, OPAL_STAGE_RANGE},
        {"MinTransfer past buffer", m_min_xfer, 1, OPAL_E_TOOBIG, OPAL_STAGE_START},
        {"wrong TSN", m_tsn, 2, OPAL_E_SESSION, OPAL_STAGE_RANGE},
        {"HSN outside a session", m_hsn, 1, OPAL_E_SESSION, OPAL_STAGE_START},
        {"reserved token", m_bad_token, 2, OPAL_E_TOKEN, OPAL_STAGE_RANGE},
        {"medium atom overrun", m_atom_overrun, 1, OPAL_E_TOKEN, OPAL_STAGE_START},
        {"long atom overrun", m_long_overrun, 2, OPAL_E_TOKEN, OPAL_STAGE_RANGE},
        {"list closed by EndName", m_unbalanced, 2, OPAL_E_TOKEN, OPAL_STAGE_RANGE},
        {"close before open", m_extra_close, 2, OPAL_E_TOKEN, OPAL_STAGE_RANGE},
        {"status not an integer", m_status_bytes, 2, OPAL_E_TOKEN, OPAL_STAGE_RANGE},
        {"no EndOfData", m_no_eod, 2, OPAL_E_TOKEN, OPAL_STAGE_RANGE},
        {"TPer ends the session", m_eos, 2, OPAL_E_SESSION, OPAL_STAGE_RANGE},
        {"EndOfSession for StartSession", m_eos, 1, OPAL_E_TOKEN, OPAL_STAGE_START},
        {"SyncSession HSN", m_sync_hsn, 1, OPAL_E_SESSION, OPAL_STAGE_START},
        {"SyncSession UID", m_sync_uid, 1, OPAL_E_TOKEN, OPAL_STAGE_START},
        {"SyncSession not a call", m_sync_not_call, 1, OPAL_E_TOKEN, OPAL_STAGE_START},
        {"too many tokens", m_many_tokens, 2, OPAL_E_TOOBIG, OPAL_STAGE_RANGE},
        {"lists nested too deep", m_deep, 2, OPAL_E_TOKEN, OPAL_STAGE_RANGE},
        {"status above 0xFF", m_status_big, 2, OPAL_E_TOKEN, OPAL_STAGE_RANGE},
        {"EndSession answer extra token", m_end_extra, 3, OPAL_E_TOKEN, OPAL_STAGE_END},
        {"EndSession answer bad ComID", m_comid, 3, OPAL_E_COMID, OPAL_STAGE_END},
        {"EndSession answer is a method", m_extra_close, 3, OPAL_E_TOKEN, OPAL_STAGE_END},
    };
    for (uint32_t i = 0; i < sizeof h / sizeof h[0]; i++) {
        fresh();
        g_f.mangle = h[i].m;
        g_f.mangle_at = h[i].at;
        opal_unlock_result_t res;
        int r = opal_unlock(&g_d, A1, g_key, 32, 0, &res);
        if (r != h[i].want || res.stage != h[i].stage)
            printf("  hostile '%s': %d (%s) at %s\n", h[i].what, r, opal_err_str(r),
                   opal_stage_str(res.stage));
        CHECK(r == h[i].want);
        CHECK(res.stage == h[i].stage);
        CHECK(!g_d.in_session);
        CHECK(all_zero(g_d.cmd, OPAL_MAX_COMPACKET));
        /* a session that opened is always ended by the host */
        if (h[i].at >= 2 && h[i].m != m_eos) CHECK(g_f.ends == 1 && !g_f.in_session);
        if (h[i].at == 1) CHECK(g_f.sets == 0 && g_f.rl[0] == 1);
        CHECK(g_f.strict == 0);
    }
    /* SyncSession with a TSN below 4096 */
    fresh();
    g_f.tsn_start = 4095;
    opal_unlock_result_t res;
    CHECK(opal_unlock(&g_d, A1, g_key, 32, 0, &res) == OPAL_E_SESSION);
    CHECK(res.stage == OPAL_STAGE_START && g_f.sets == 0);
    fresh();
    g_f.tsn_start = 4096;
    CHECK(opal_unlock(&g_d, A1, g_key, 32, 0, &res) == OPAL_OK);
    /* a Level 0 answer that lies about its length */
    uint8_t raw[64];
    memset(raw, 0, sizeof raw);
    wbe32(raw, 0x7FFFFFFFu);
    fresh();
    g_f.disc_raw = raw;
    g_f.disc_raw_len = sizeof raw;
    CHECK(opal_unlock(&g_d, A1, g_key, 32, 0, &res) == OPAL_E_DISC);
    CHECK(res.stage == OPAL_STAGE_DISCOVERY && g_f.sends == 0);
    /* discovery that turns hostile after the unlock: the error is reported */
    fresh();
    g_f.mangle = m_cp_huge;
    g_f.mangle_at = 99;
    CHECK(opal_unlock(&g_d, A1, g_key, 32, 0, &res) == OPAL_OK);
}

/* Random corruption of every response: never a crash (ASan), never a
 * session left open, and an OK only when the drive really unlocked. */
static uint32_t g_rng = 12345;
static uint32_t rnd(void)
{
    g_rng = g_rng * 1103515245u + 12345u;
    return g_rng >> 8;
}
static void m_random(ftper_t *f, uint8_t *b, uint32_t n)
{
    (void) f;
    uint32_t k = 1 + rnd() % 6;
    for (uint32_t i = 0; i < k; i++) {
        uint32_t where = rnd() % 3;
        uint32_t off = where == 0 ? rnd() % 56 : where == 1 ? 56 + rnd() % 64 : rnd() % n;
        b[off] = (uint8_t) rnd();
    }
}

static void test_random(void)
{
    uint32_t ok = 0, refused = 0, bad_ok = 0, open = 0;
    for (uint32_t i = 0; i < 3000; i++) {
        fresh();
        g_f.mangle = m_random;
        g_f.mangle_at = 1 + rnd() % 3;
        opal_unlock_result_t res;
        int r = opal_unlock(&g_d, A1, g_key, 32, 0, &res);
        if (r == OPAL_OK) {
            ok++;
            if (g_f.rl[0] || g_f.wl[0]) bad_ok++;
        } else {
            refused++;
        }
        if (g_d.in_session) open++;
    }
    CHECK(ok + refused == 3000);
    CHECK(refused > 1000);
    CHECK(bad_ok == 0);
    CHECK(open == 0);
    /* random Level 0 data */
    uint8_t b[512];
    opal_disc_t ds;
    uint32_t parsed = 0;
    for (uint32_t i = 0; i < 20000; i++) {
        for (uint32_t k = 0; k < sizeof b; k++) b[k] = (uint8_t) rnd();
        uint32_t n = 48 + rnd() % 400;
        wbe32(b, rnd() % 2 ? n - 4 - rnd() % 8 : rnd() % 600);
        for (uint32_t o = 48; o + 4 <= n; o += 4 + (rnd() % 24)) b[o + 3] = (uint8_t) (rnd() % 24);
        int r = opal_parse_discovery(b, n, &ds);
        CHECK(r == OPAL_OK || r == OPAL_E_DISC);
        if (r == OPAL_OK) parsed++;
        if (r == OPAL_OK && ds.ssc != OPAL_SSC_NONE) CHECK(ds.base_comid > 1 && ds.num_comids);
    }
    CHECK(parsed > 0);
    /* random token streams */
    opal_tok_t t[OPAL_MAX_TOKENS];
    for (uint32_t i = 0; i < 20000; i++) {
        for (uint32_t k = 0; k < 64; k++) b[k] = (uint8_t) rnd();
        uint32_t n = rnd() % 64;
        int c = opal_tokenize(b, 0, n, t, OPAL_MAX_TOKENS);
        CHECK(c == OPAL_E_TOKEN || (c >= 0 && (uint32_t) c <= n));
        for (int k = 0; k < c; k++)
            if (t[k].kind == OPAL_K_BYTES) CHECK(t[k].off + t[k].len <= n);
    }
}

/* ---- the arbiter releases the key ------------------------------------------- */

#define WIN_SIZE (ARB_OFF_FB + 4096u)
static uint8_t g_win[WIN_SIZE] __attribute__((aligned(4096)));
static uint8_t g_meas[ARB_MEAS_LEN];
static arb_dev_t g_ad;
static arb_host_t g_ah;
static uint32_t g_waits;

static void ent(void *ctx, uint8_t *out, uint32_t n)
{
    (void) ctx;
    for (uint32_t i = 0; i < n; i++) out[i] = (uint8_t) (i * 13 + 7);
}

/* The arbiter's firmware runs while the host waits. */
static void arb_wait(void *ctx)
{
    g_waits++;
    arb_dev_poll((arb_dev_t *) ctx);
}

static void arb_setup(int attest)
{
    memset(g_win, 0, sizeof g_win);
    for (uint32_t i = 0; i < ARB_MEAS_LEN; i++) g_meas[i] = (uint8_t) (i * 5 + 3);
    CHECK(arb_dev_init(&g_ad, g_win, WIN_SIZE, 8, 0, 0, 0, g_meas, ent, 0) == 0);
    CHECK(arb_host_init(&g_ah, g_win, WIN_SIZE) == 0);
    g_waits = 0;
    if (!attest) return;
    CHECK(arb_host_hello(&g_ah) == 0);
    arb_dev_poll(&g_ad);
    arb_host_poll(&g_ah);
    CHECK(arb_host_attest(&g_ah, g_meas) == 0);
    arb_dev_poll(&g_ad);
    arb_host_poll(&g_ah);
    CHECK(g_ah.state == ARB_H_ATTESTED);
}

static void test_arbiter(void)
{
    opal_unlock_result_t res;
    /* attested, slot 1 holds Admin1's password */
    fresh();
    arb_setup(1);
    CHECK(arb_dev_set_key(&g_ad, 1, g_key) == 0);
    CHECK(opal_arb_unlock(&g_d, &g_ah, 1, A1, 0, 10, arb_wait, &g_ad, &res) == OPAL_OK);
    CHECK(res.needed && res.key_status == ARB_KEY_OK && res.stage == OPAL_STAGE_NONE);
    CHECK(g_f.rl[0] == 0 && g_f.wl[0] == 0 && g_f.ends == 1 && g_f.strict == 0);
    CHECK(memcmp(g_f.last_challenge, g_key, 32) == 0);
    CHECK(g_waits == 1);
    CHECK(all_zero(g_ah.key[1], ARB_KEY_LEN)); /* host copy wiped */
    CHECK(all_zero(g_d.cmd, OPAL_MAX_COMPACKET));
    CHECK(!g_d.in_session);
    /* not locked any more: no key is requested */
    memset(g_f.rl, 0, sizeof g_f.rl);
    memset(g_f.wl, 0, sizeof g_f.wl);
    g_waits = 0;
    g_ah.key_status[1] = 0;
    CHECK(opal_arb_unlock(&g_d, &g_ah, 1, A1, 0, 10, arb_wait, &g_ad, &res) == OPAL_OK);
    CHECK(!res.needed && g_waits == 0 && res.key_status == 0);
    CHECK(arb_ring_fill(g_win, ARB_OFF_RING_H2A) == 0);

    /* the wrong slot's key: the drive says NOT_AUTHORIZED */
    fresh();
    arb_setup(1);
    CHECK(arb_dev_set_key(&g_ad, 0, g_ukey) == 0);
    CHECK(opal_arb_unlock(&g_d, &g_ah, 0, A1, 0, 10, arb_wait, &g_ad, &res) == OPAL_E_STATUS);
    CHECK(res.stage == OPAL_STAGE_START && res.status == OPAL_ST_NOT_AUTHORIZED);
    CHECK(res.key_status == ARB_KEY_OK && g_f.rl[0] == 1);
    CHECK(all_zero(g_ah.key[0], ARB_KEY_LEN));
    /* User1 from slot 3 */
    fresh();
    arb_setup(1);
    CHECK(arb_dev_set_key(&g_ad, 3, g_ukey) == 0);
    CHECK(opal_arb_unlock(&g_d, &g_ah, 3, U1, 0, 10, arb_wait, &g_ad, &res) == OPAL_OK);
    CHECK(g_f.rl[0] == 0);
    /* an empty slot */
    fresh();
    arb_setup(1);
    CHECK(opal_arb_unlock(&g_d, &g_ah, 2, A1, 0, 10, arb_wait, &g_ad, &res) == OPAL_E_KEY);
    CHECK(res.key_status == ARB_KEY_NO_SLOT && res.stage == OPAL_STAGE_KEY);
    CHECK(g_f.sends == 0 && g_f.rl[0] == 1);
    /* a slot past the table */
    CHECK(opal_arb_unlock(&g_d, &g_ah, ARB_HOST_KEYS, A1, 0, 10, arb_wait, &g_ad, &res) ==
          OPAL_E_ARG);
    /* not attested: nothing is asked of the arbiter */
    fresh();
    arb_setup(0);
    CHECK(arb_dev_set_key(&g_ad, 0, g_key) == 0);
    CHECK(opal_arb_unlock(&g_d, &g_ah, 0, A1, 0, 10, arb_wait, &g_ad, &res) == OPAL_E_NOT_ATTESTED);
    CHECK(res.stage == OPAL_STAGE_KEY && res.needed && g_waits == 0);
    CHECK(arb_ring_fill(g_win, ARB_OFF_RING_H2A) == 0 && g_f.sends == 0);
    CHECK(opal_arb_unlock(&g_d, 0, 0, A1, 0, 10, arb_wait, &g_ad, &res) == OPAL_E_NOT_ATTESTED);
    /* tampered before the request */
    fresh();
    arb_setup(1);
    CHECK(arb_dev_set_key(&g_ad, 0, g_key) == 0);
    arb_dev_tamper(&g_ad, 1);
    arb_host_poll(&g_ah);
    CHECK(opal_arb_unlock(&g_d, &g_ah, 0, A1, 0, 10, arb_wait, &g_ad, &res) == OPAL_E_NOT_ATTESTED);
    CHECK(g_f.rl[0] == 1);
    /* the arbiter never answers: bounded, E_KEY with no status */
    fresh();
    arb_setup(1);
    CHECK(arb_dev_set_key(&g_ad, 0, g_key) == 0);
    CHECK(opal_arb_unlock(&g_d, &g_ah, 0, A1, 0, 7, 0, 0, &res) == OPAL_E_KEY);
    CHECK(res.key_status == 0 && g_f.sends == 0);
    /* zero polls */
    fresh();
    arb_setup(1);
    CHECK(opal_arb_unlock(&g_d, &g_ah, 0, A1, 0, 0, arb_wait, &g_ad, &res) == OPAL_E_KEY);
    /* the H2A ring is full: one drain, then the request goes */
    fresh();
    arb_setup(1);
    CHECK(arb_dev_set_key(&g_ad, 0, g_key) == 0);
    uint32_t q = 0;
    while (arb_host_state_request(&g_ah) == 0) q++;
    CHECK(q > 0);
    CHECK(opal_arb_unlock(&g_d, &g_ah, 0, A1, 0, 10, arb_wait, &g_ad, &res) == OPAL_E_KEY);
    arb_dev_poll(&g_ad);
    arb_host_poll(&g_ah);
    CHECK(opal_arb_unlock(&g_d, &g_ah, 0, A1, 0, 10, arb_wait, &g_ad, &res) == OPAL_OK);
    /* not an SED: no key */
    fresh();
    arb_setup(1);
    g_f.no_locking = 1;
    CHECK(opal_arb_unlock(&g_d, &g_ah, 0, A1, 0, 10, arb_wait, &g_ad, &res) == OPAL_E_NOT_SED);
    CHECK(res.stage == OPAL_STAGE_DISCOVERY && g_waits == 0);
    /* a session left open is refused */
    fresh();
    arb_setup(1);
    CHECK(opal_discover(&g_d) == OPAL_OK);
    CHECK(opal_start_session(&g_d, A1, g_key, 32) == OPAL_OK);
    CHECK(opal_arb_unlock(&g_d, &g_ah, 0, A1, 0, 10, arb_wait, &g_ad, &res) == OPAL_E_STATE);
}

static void test_strings(void)
{
    for (int e = 0; e >= -16; e--) CHECK(opal_err_str(e) != 0 && opal_err_str(e)[0] != 0);
    CHECK(strcmp(opal_err_str(-16), "unknown error") == 0);
    CHECK(strcmp(opal_err_str(OPAL_E_NOT_SED), "no Locking feature") == 0);
    for (uint32_t s = 0; s < 6; s++) CHECK(opal_ssc_str((opal_ssc_t) s) != 0);
    CHECK(strcmp(opal_ssc_str(OPAL_SSC_NONE), "none") == 0);
    for (uint32_t s = 0; s < 8; s++) CHECK(opal_stage_str((opal_stage_t) s) != 0);
    uint8_t w[40];
    memset(w, 0xAA, sizeof w);
    opal_wipe(w + 1, 38);
    CHECK(w[0] == 0xAA && w[39] == 0xAA && all_zero(w + 1, 38));
}

int main(void)
{
    test_tokens();
    test_uids();
    test_discovery();
    test_golden();
    test_state();
    test_unlock();
    test_hostile();
    test_random();
    test_arbiter();
    test_strings();
    printf("test_opal: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
