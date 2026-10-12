/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_common.c — the shared industry layer: status names, exact
 * quantities and rounding, subjects, seen-sets, signers, signed chains
 * (tamper, truncation, reordering, untrusted keys), the quarantine sink and
 * the byte writer/reader. */
#include "zind_test.h"

/* ---------- quantities ---------- */

static zind_qty Q(int64_t n, int64_t d) {
    zind_qty q = ZIND_QTY_ZERO;
    (void) zind_qty_make(n, d, &q);
    return q;
}

static int qeq(zind_qty a, zind_qty b) { return a.num == b.num && a.den == b.den; }

/* Reference rounding: the integer m chosen by rule `mode` for x/y (y > 0),
 * found by search rather than by division. */
static int64_t ref_round(int64_t x, int64_t y, int mode, int *exact) {
    int64_t fl = x / y - 2;
    while ((fl + 1) * y <= x) fl++;
    /* fl*y <= x < (fl+1)*y */
    int64_t lo = x - fl * y, hi = (fl + 1) * y - x;
    *exact = lo == 0;
    if (lo == 0) return fl;
    switch (mode) {
    case ZIND_ROUND_FLOOR: return fl;
    case ZIND_ROUND_CEIL: return fl + 1;
    case ZIND_ROUND_TRUNC: return x < 0 ? fl + 1 : fl;
    case ZIND_ROUND_HALF_UP:
        if (lo < hi) return fl;
        if (lo > hi) return fl + 1;
        return x > 0 ? fl + 1 : fl;
    default:
        if (lo < hi) return fl;
        if (lo > hi) return fl + 1;
        return (fl % 2 == 0) ? fl : fl + 1;
    }
}

static void test_qty(void) {
    zind_qty q, r;
    char buf[64];
    size_t n;
    int32_t c;

    CHECK_RC(zind_qty_parse("12.75", &q), ZXV_OK);
    CHECK(qeq(q, Q(51, 4)), "12.75 = 51/4");
    CHECK_RC(zind_qty_parse("3/4", &q), ZXV_OK);
    CHECK(q.num == 3 && q.den == 4, "3/4");
    CHECK_RC(zind_qty_parse("6/8", &q), ZXV_OK);
    CHECK(q.num == 3 && q.den == 4, "6/8 reduces to 3/4");
    CHECK_RC(zind_qty_parse("-0.1", &q), ZXV_OK);
    CHECK(q.num == -1 && q.den == 10, "-0.1");
    CHECK_RC(zind_qty_parse("42", &q), ZXV_OK);
    CHECK(zind_qty_is_int(q) && q.num == 42, "42 is an integer");
    CHECK_RC(zind_qty_parse("1e3", &q), ZXV_E_FORMAT);
    CHECK_RC(zind_qty_parse(" 1", &q), ZXV_E_FORMAT);
    CHECK_RC(zind_qty_parse("NaN", &q), ZXV_E_FORMAT);
    CHECK_RC(zind_qty_parse("", &q), ZXV_E_FORMAT);
    CHECK_RC(zind_qty_parse("1/0", &q), ZXV_E_ARG);
    CHECK_RC(zind_qty_parse("99999999999999999999", &q), ZXV_E_RANGE);
    CHECK_RC(zind_qty_parse(NULL, &q), ZXV_E_ARG);
    CHECK_RC(zind_qty_parse("1", NULL), ZXV_E_ARG);
    CHECK(strstr(zt_last_err(), "") != NULL, "last error readable");

    CHECK_RC(zind_qty_make(4, -6, &q), ZXV_OK);
    CHECK(q.num == -2 && q.den == 3, "4/-6 = -2/3");
    CHECK_RC(zind_qty_make(1, 0, &q), ZXV_E_ARG);
    CHECK(zind_qty_make(INT64_MIN, 1, &q) != ZXV_OK, "INT64_MIN refused");
    CHECK(!zind_qty_valid((zind_qty){2, 4}), "2/4 is not reduced");
    CHECK(!zind_qty_valid((zind_qty){1, 0}), "den 0 invalid");
    CHECK(!zind_qty_valid((zind_qty){1, -2}), "den < 0 invalid");
    CHECK(!zind_qty_valid((zind_qty){0, 5}), "0/5 not canonical");
    CHECK(zind_qty_valid(ZIND_QTY_ZERO), "0/1 valid");
    CHECK(zind_qty_sign(Q(-3, 4)) == -1 && zind_qty_sign(Q(0, 1)) == 0 &&
              zind_qty_sign(Q(5, 2)) == 1,
          "signs");

    /* format round trip over a grid */
    for (int64_t num = -40; num <= 40; num += 3)
        for (int64_t den = 1; den <= 16; den++) {
            zind_qty a = Q(num, den), b;
            CHECK_RC(zind_qty_format(a, buf, sizeof buf, &n), ZXV_OK);
            CHECK(zind_qty_parse(buf, &b) == ZXV_OK && qeq(a, b), "round trip %lld/%lld via %s",
                  (long long) num, (long long) den, buf);
        }
    CHECK_RC(zind_qty_format((zind_qty){2, 4}, buf, sizeof buf, &n), ZXV_E_ARG);
    CHECK_RC(zind_qty_format(Q(1, 3), buf, 2, &n), ZXV_E_BUFFER);

    /* arithmetic */
    CHECK_RC(zind_qty_add(Q(3, 4), Q(1, 4), &q), ZXV_OK);
    CHECK(qeq(q, Q(1, 1)), "3/4 + 1/4 = 1");
    CHECK_RC(zind_qty_sub(Q(1, 3), Q(1, 2), &q), ZXV_OK);
    CHECK(qeq(q, Q(-1, 6)), "1/3 - 1/2 = -1/6");
    CHECK_RC(zind_qty_mul(Q(2, 3), Q(9, 4), &q), ZXV_OK);
    CHECK(qeq(q, Q(3, 2)), "2/3 * 9/4 = 3/2");
    q = Q(7, 1);
    CHECK_RC(zind_qty_add(Q(INT64_MAX, 1), Q(1, 1), &q), ZXV_E_RANGE);
    CHECK(qeq(q, Q(7, 1)), "out unchanged on overflow");
    CHECK_RC(zind_qty_mul(Q(INT64_MAX / 2, 1), Q(3, 1), &q), ZXV_E_RANGE);
    CHECK_RC(zind_qty_sub(Q(-INT64_MAX, 1), Q(2, 1), &q), ZXV_E_RANGE);
    CHECK_RC(zind_qty_add(Q(1, 3), (zind_qty){2, 4}, &q), ZXV_E_ARG);
    CHECK_RC(zind_qty_add(Q(1, 3), Q(1, 3), NULL), ZXV_E_ARG);
    CHECK_RC(zind_qty_cmp(Q(1, 3), Q(1, 2), &c), ZXV_OK);
    CHECK(c == -1, "1/3 < 1/2");
    CHECK_RC(zind_qty_cmp(Q(2, 4), Q(1, 2), &c), ZXV_OK);
    CHECK(c == 0, "2/4 == 1/2");
    CHECK_RC(zind_qty_cmp(Q(INT64_MAX, INT64_MAX - 1), Q(INT64_MAX - 1, INT64_MAX - 2), &c),
             ZXV_OK);
    CHECK(c == -1, "near-1 comparison exact");
    CHECK_RC(zind_qty_cmp(Q(1, 3), Q(1, 2), NULL), ZXV_E_ARG);

    /* to_minor against the reference, every mode */
    for (int64_t num = -37; num <= 37; num++)
        for (int64_t den = 1; den <= 8; den++)
            for (uint8_t dg = 0; dg <= 1; dg++)
                for (int mode = ZIND_ROUND_EXACT; mode <= ZIND_ROUND_TRUNC; mode++) {
                    int exact = 0;
                    int64_t scale = dg ? 10 : 1, m = 0, want;
                    zind_qty a = Q(num, den), res = Q(99, 1), back;
                    int32_t st;
                    want = ref_round(a.num * scale, a.den, mode == 0 ? 1 : mode, &exact);
                    st = zind_qty_to_minor(a, dg, mode, &m, &res);
                    if (mode == ZIND_ROUND_EXACT && !exact) {
                        CHECK(st == ZIND_E_INEXACT, "%lld/%lld d%u exact refused",
                              (long long) num, (long long) den, dg);
                        continue;
                    }
                    CHECK(st == ZXV_OK && m == want, "%lld/%lld d%u mode %d -> %lld want %lld",
                          (long long) num, (long long) den, dg, mode, (long long) m,
                          (long long) want);
                    /* residual makes it exact again */
                    back = ZIND_QTY_ZERO;
                    if (num % 3 == 0) {
                        zind_qty mq;
                        CHECK(zind_qty_from_minor(m, dg, &mq) == ZXV_OK &&
                                  zind_qty_add(mq, res, &back) == ZXV_OK && qeq(back, a),
                              "minor + residual == q for %lld/%lld", (long long) num,
                              (long long) den);
                    }
                }
    CHECK_RC(zind_qty_to_minor(Q(1, 3), 2, ZIND_ROUND_EXACT, &(int64_t){0}, NULL), ZIND_E_INEXACT);
    {
        int64_t m = 0;
        CHECK_RC(zind_qty_to_minor(Q(5, 2), 0, ZIND_ROUND_HALF_EVEN, &m, NULL), ZXV_OK);
        CHECK(m == 2, "2.5 half-even -> 2");
        CHECK_RC(zind_qty_to_minor(Q(7, 2), 0, ZIND_ROUND_HALF_EVEN, &m, NULL), ZXV_OK);
        CHECK(m == 4, "3.5 half-even -> 4");
        CHECK_RC(zind_qty_to_minor(Q(-5, 2), 0, ZIND_ROUND_HALF_UP, &m, NULL), ZXV_OK);
        CHECK(m == -3, "-2.5 half-up -> -3");
        CHECK_RC(zind_qty_to_minor(Q(1234567, 1000), 2, ZIND_ROUND_HALF_UP, &m, &r), ZXV_OK);
        CHECK(m == 123457 && qeq(r, Q(-3, 1000)), "1234.567 -> 123457 residual -0.003");
        CHECK_RC(zind_qty_to_minor(Q(1, 1), 19, ZIND_ROUND_EXACT, &m, NULL), ZXV_E_ARG);
        CHECK_RC(zind_qty_to_minor(Q(1, 1), 2, 9, &m, NULL), ZXV_E_ARG);
        CHECK_RC(zind_qty_to_minor(Q(1, 1), 2, -1, &m, NULL), ZXV_E_ARG);
        CHECK_RC(zind_qty_to_minor(Q(INT64_MAX, 1), 2, ZIND_ROUND_EXACT, &m, NULL), ZXV_E_RANGE);
        CHECK_RC(zind_qty_to_minor(Q(ZXV_AMOUNT_MAX + 1, 1), 0, ZIND_ROUND_EXACT, &m, NULL),
                 ZXV_E_RANGE);
        CHECK_RC(zind_qty_to_minor(Q(ZXV_AMOUNT_MAX, 1), 0, ZIND_ROUND_EXACT, &m, NULL), ZXV_OK);
        CHECK(m == ZXV_AMOUNT_MAX, "AMOUNT_MAX exact");
        CHECK_RC(zind_qty_to_minor((zind_qty){2, 4}, 0, ZIND_ROUND_EXACT, &m, NULL), ZXV_E_ARG);
        CHECK_RC(zind_qty_to_minor(Q(1, 1), 0, ZIND_ROUND_EXACT, NULL, NULL), ZXV_E_ARG);
        CHECK_RC(zind_qty_from_minor(12345, 2, &q), ZXV_OK);
        CHECK(qeq(q, Q(2469, 20)), "123.45 = 2469/20");
        CHECK_RC(zind_qty_from_minor(1, 19, &q), ZXV_E_ARG);
    }
}

/* ---------- subjects and seen-sets ---------- */

static void test_seen(void) {
    uint8_t a[32], b[32], c[32];
    zind_seen *s = NULL;
    int32_t present = -1;
    uint32_t cnt = 0;
    CHECK_RC(zind_subject("t", (const uint8_t *) "x", 1, a), ZXV_OK);
    CHECK_RC(zind_subject("t", (const uint8_t *) "x", 1, b), ZXV_OK);
    CHECK(memcmp(a, b, 32) == 0, "subject deterministic");
    CHECK_RC(zind_subject("u", (const uint8_t *) "x", 1, c), ZXV_OK);
    CHECK(memcmp(a, c, 32) != 0, "domain separates");
    CHECK_RC(zind_subject("t", (const uint8_t *) "y", 1, c), ZXV_OK);
    CHECK(memcmp(a, c, 32) != 0, "data separates");
    CHECK_RC(zind_subject("t", NULL, 0, c), ZXV_OK);
    CHECK_RC(zind_subject("", (const uint8_t *) "x", 1, c), ZXV_E_ARG);
    CHECK_RC(zind_subject("has space", (const uint8_t *) "x", 1, c), ZXV_E_ARG);
    CHECK_RC(zind_subject("0123456789012345678901234567890123", (const uint8_t *) "x", 1, c),
             ZXV_E_ARG);
    CHECK_RC(zind_subject("t", NULL, 3, c), ZXV_E_ARG);
    {
        static uint8_t big[2000];
        memset(big, 7, sizeof big);
        CHECK_RC(zind_subject("t", big, sizeof big, c), ZXV_OK);
    }

    CHECK_RC(zind_seen_create(0, &s), ZXV_E_ARG);
    CHECK_RC(zind_seen_create((1u << 24) + 1, &s), ZXV_E_ARG);
    CHECK_RC(zind_seen_create(100, &s), ZXV_OK);
    for (uint32_t i = 0; i < 100; i++) {
        uint8_t d[32];
        CHECK_RC(zind_subject("seen", (const uint8_t *) &i, sizeof i, d), ZXV_OK);
        CHECK_RC(zind_seen_add(s, d), ZXV_OK);
        CHECK_RC(zind_seen_add(s, d), ZIND_E_REPLAY);
    }
    CHECK_RC(zind_seen_count(s, &cnt), ZXV_OK);
    CHECK(cnt == 100, "100 entries");
    CHECK_RC(zind_seen_add(s, a), ZXV_E_CAPACITY);
    CHECK_RC(zind_seen_has(s, a, &present), ZXV_OK);
    CHECK(present == 0, "refused entry absent");
    {
        uint8_t d[32];
        uint32_t i = 57;
        zind_subject("seen", (const uint8_t *) &i, sizeof i, d);
        CHECK_RC(zind_seen_has(s, d, &present), ZXV_OK);
        CHECK(present == 1, "57 present");
        CHECK_RC(zind_seen_add(s, d), ZIND_E_REPLAY);
    }
    CHECK_RC(zind_seen_add(NULL, a), ZXV_E_ARG);
    CHECK_RC(zind_seen_has(s, NULL, &present), ZXV_E_ARG);
    CHECK_RC(zind_seen_count(s, NULL), ZXV_E_ARG);
    zind_seen_destroy(s);
    zind_seen_destroy(NULL);
}

/* ---------- signers and chains ---------- */

static void test_chain(zt_env *e) {
    zind_signer *s1 = zt_signer(e->cap, 1), *s1b = zt_signer(e->cap, 1), *s2 = zt_signer(e->cap, 2);
    zind_chain *c = NULL, *imp = NULL;
    zxv_cap *nosign = NULL;
    uint8_t sig[ZIND_SIG_BYTES], head[32], h[32];
    const uint8_t *trust1[1], *trust12[2];
    uint32_t cnt = 0, bad = 0;
    uint64_t seq = 0;
    size_t len = 0;
    uint8_t *buf;
    zind_record rec;

    CHECK(s1 && s1b && s2, "signers");
    if (!s1 || !s1b || !s2) return;
    CHECK(memcmp(zind_signer_pk(s1), zind_signer_pk(s1b), ZIND_PK_BYTES) == 0,
          "seeded keys deterministic");
    CHECK(memcmp(zind_signer_pk(s1), zind_signer_pk(s2), ZIND_PK_BYTES) != 0, "seeds differ");
    CHECK_RC(zind_key_id(zind_signer_pk(s1), h), ZXV_OK);
    CHECK(memcmp(h, zind_signer_key_id(s1), 32) == 0, "key id");
    CHECK(zind_signer_pk(NULL) == NULL && zind_signer_key_id(NULL) == NULL, "NULL signer");
    trust1[0] = zind_signer_pk(s1);
    trust12[0] = zind_signer_pk(s1);
    trust12[1] = zind_signer_pk(s2);

    CHECK_RC(zind_sign(e->cap, s1, (const uint8_t *) "msg", 3, "ctx", sig), ZXV_OK);
    CHECK_RC(zind_verify(e->cap, zind_signer_pk(s1), (const uint8_t *) "msg", 3, "ctx", sig),
             ZXV_OK);
    CHECK_RC(zind_verify(e->cap, zind_signer_pk(s1), (const uint8_t *) "msh", 3, "ctx", sig),
             ZXV_E_VERIFY);
    CHECK_RC(zind_verify(e->cap, zind_signer_pk(s1), (const uint8_t *) "msg", 3, "cty", sig),
             ZXV_E_VERIFY);
    CHECK_RC(zind_verify(e->cap, zind_signer_pk(s2), (const uint8_t *) "msg", 3, "ctx", sig),
             ZXV_E_VERIFY);
    sig[100] ^= 1;
    CHECK_RC(zind_verify(e->cap, zind_signer_pk(s1), (const uint8_t *) "msg", 3, "ctx", sig),
             ZXV_E_VERIFY);
    {
        char longctx[300];
        memset(longctx, 'a', 299);
        longctx[299] = 0;
        CHECK_RC(zind_sign(e->cap, s1, (const uint8_t *) "m", 1, longctx, sig), ZXV_E_ARG);
    }
    CHECK_RC(zxv_cap_attenuate(e->cap, ZXV_CAP_LEDGER_READ, &nosign), ZXV_OK);
    CHECK(zind_sign(nosign, s1, (const uint8_t *) "m", 1, "c", sig) == ZXV_E_CAP,
          "signing needs CRYPTO_SIGN");
    {
        zind_signer *x = NULL;
        uint8_t seed[32] = {0};
        CHECK(zind_signer_from_seed(nosign, seed, &x) == ZXV_E_CAP && x == NULL,
              "keygen needs CRYPTO_SIGN");
        CHECK_RC(zind_signer_generate(e->cap, &x), ZXV_OK);
        CHECK(x && memcmp(zind_signer_pk(x), zind_signer_pk(s1), 32) != 0, "fresh key");
        zind_signer_destroy(x);
        CHECK_RC(zind_signer_from_seed(e->cap, NULL, &x), ZXV_E_ARG);
    }

    CHECK_RC(zind_chain_create("bad domain", 4, &c), ZXV_E_ARG);
    CHECK_RC(zind_chain_create("t", 0, &c), ZXV_E_ARG);
    CHECK_RC(zind_chain_create("t", ZIND_CHAIN_MAX + 1, &c), ZXV_E_ARG);
    CHECK_RC(zind_chain_create("test/chain", 6, &c), ZXV_OK);
    CHECK(strcmp(zind_chain_domain(c), "test/chain") == 0, "domain");
    CHECK_RC(zind_chain_head(c, head), ZXV_OK);
    {
        static const uint8_t z[32];
        CHECK(memcmp(head, z, 32) == 0, "empty head zero");
    }
    CHECK_RC(zind_chain_verify(e->cap, c, NULL, 0, &bad), ZXV_OK);
    CHECK(bad == UINT32_MAX, "empty chain verifies");
    for (int i = 0; i < 5; i++) {
        char p[32];
        int k = snprintf(p, sizeof p, "record %d", i);
        CHECK_RC(zind_chain_append(e->cap, c, (i % 2) ? s2 : s1, (uint16_t) (10 + i),
                                   (const uint8_t *) p, (size_t) k, &seq, h),
                 ZXV_OK);
        CHECK(seq == (uint64_t) i, "seq %d", i);
        CHECK_RC(zind_chain_head(c, head), ZXV_OK);
        CHECK(memcmp(head, h, 32) == 0, "head = last hash");
    }
    CHECK_RC(zind_chain_append(e->cap, c, s1, 1, NULL, 0, NULL, NULL), ZXV_OK); /* empty payload */
    CHECK_RC(zind_chain_append(e->cap, c, s1, 1, (const uint8_t *) "x", 1, NULL, NULL),
             ZXV_E_CAPACITY);
    CHECK_RC(zind_chain_count(c, &cnt), ZXV_OK);
    CHECK(cnt == 6, "6 records");
    CHECK_RC(zind_chain_at(c, 2, &rec), ZXV_OK);
    CHECK(rec.seq == 2 && rec.kind == 12 && rec.payload_len == 8 &&
              memcmp(rec.payload, "record 2", 8) == 0,
          "record 2 view");
    CHECK(memcmp(rec.key_id, zind_signer_key_id(s1), 32) == 0, "record 2 signed by s1");
    CHECK_RC(zind_chain_at(c, 6, &rec), ZXV_E_NOT_FOUND);
    CHECK_RC(zind_chain_verify(e->cap, c, trust12, 2, &bad), ZXV_OK);
    CHECK_RC(zind_chain_verify(e->cap, c, trust1, 1, &bad), ZIND_E_UNTRUSTED);
    CHECK(bad == 1, "record 1 is s2's (bad=%u)", bad);
    CHECK_RC(zind_chain_verify(nosign, c, trust12, 2, &bad), ZXV_E_CAP);
    CHECK_RC(zind_chain_verify(e->cap, c, NULL, 2, &bad), ZXV_E_ARG);
    {
        static uint8_t big[ZIND_PAYLOAD_MAX + 1];
        zind_chain *c2 = NULL;
        CHECK_RC(zind_chain_create("t", 2, &c2), ZXV_OK);
        CHECK_RC(zind_chain_append(e->cap, c2, s1, 0, big, sizeof big, NULL, NULL),
                 ZXV_E_TOO_LONG);
        CHECK_RC(zind_chain_append(e->cap, c2, s1, 0, big, ZIND_PAYLOAD_MAX, NULL, NULL), ZXV_OK);
        CHECK_RC(zind_chain_append(nosign, c2, s1, 0, big, 1, NULL, NULL), ZXV_E_CAP);
        CHECK_RC(zind_chain_count(c2, &cnt), ZXV_OK);
        CHECK(cnt == 1, "failed appends add nothing");
        CHECK_RC(zind_chain_verify(e->cap, c2, trust1, 1, NULL), ZXV_OK);
        zind_chain_destroy(c2);
    }

    /* export / import */
    CHECK_RC(zind_chain_export(c, NULL, 0, &len), ZXV_E_BUFFER);
    buf = (uint8_t *) malloc(len);
    CHECK_RC(zind_chain_export(c, buf, len - 1, &len), ZXV_E_BUFFER);
    CHECK_RC(zind_chain_export(c, buf, len, &len), ZXV_OK);
    CHECK_RC(zind_chain_import("test/chain", buf, len, &imp), ZXV_OK);
    CHECK_RC(zind_chain_verify(e->cap, imp, trust12, 2, &bad), ZXV_OK);
    {
        uint8_t h2[32];
        zind_chain_head(c, head);
        zind_chain_head(imp, h2);
        CHECK(memcmp(head, h2, 32) == 0, "imported head equal");
        /* the imported chain can be continued */
        CHECK_RC(zind_chain_append(e->cap, imp, s1, 99, (const uint8_t *) "more", 4, &seq, NULL),
                 ZXV_OK);
        CHECK(seq == 6, "continued at 6");
        CHECK_RC(zind_chain_verify(e->cap, imp, trust12, 2, NULL), ZXV_OK);
    }
    zind_chain_destroy(imp);
    imp = NULL;
    CHECK_RC(zind_chain_import("other", buf, len, &imp), ZXV_E_FORMAT);
    CHECK(imp == NULL, "nothing on failure");
    /* every truncation is refused */
    for (size_t cut = 0; cut < len; cut += (cut < 64 ? 1 : 977)) {
        CHECK_RC(zind_chain_import("test/chain", buf, cut, &imp), ZXV_E_FORMAT);
    }
    {
        uint8_t *ext = (uint8_t *) malloc(len + 1);
        memcpy(ext, buf, len);
        ext[len] = 0;
        CHECK_RC(zind_chain_import("test/chain", ext, len + 1, &imp), ZXV_E_FORMAT);
        free(ext);
    }
    /* tamper with single bytes across the export: refused by import or verify */
    {
        size_t step = len / 97 + 1, caught = 0, tried = 0;
        for (size_t off = 0; off < len; off += step) {
            uint8_t *t = (uint8_t *) malloc(len);
            int32_t st;
            memcpy(t, buf, len);
            t[off] ^= 0x40;
            imp = NULL;
            st = zind_chain_import("test/chain", t, len, &imp);
            tried++;
            if (st == ZXV_OK) {
                st = zind_chain_verify(e->cap, imp, trust12, 2, &bad);
                CHECK(st == ZIND_E_CHAIN || st == ZIND_E_UNTRUSTED,
                      "tamper at %zu detected by verify (%s)", off, zind_status_name(st));
                zind_chain_destroy(imp);
            } else {
                CHECK(st == ZXV_E_FORMAT, "tamper at %zu refused by import", off);
            }
            caught++;
            free(t);
        }
        CHECK(caught == tried && tried > 90, "every tamper caught (%zu)", tried);
    }
    /* drop the middle record: the next record's prev no longer links */
    {
        zind_chain *a = NULL;
        CHECK_RC(zind_chain_create("drop", 4, &a), ZXV_OK);
        for (int i = 0; i < 3; i++)
            zind_chain_append(e->cap, a, s1, 0, (const uint8_t *) &i, sizeof i, NULL, NULL);
        size_t l = 0;
        zind_chain_export(a, NULL, 0, &l);
        uint8_t *x = (uint8_t *) malloc(l);
        zind_chain_export(a, x, l, &l);
        size_t hdr = 4 + 1 + 1 + 4 + 4, rl = 8 + 2 + 4 + sizeof(int) + 96 + ZIND_SIG_BYTES;
        /* splice out record 1 and renumber record 2 as 1, count 2 */
        uint8_t *y = (uint8_t *) malloc(l);
        memcpy(y, x, hdr + rl);
        memcpy(y + hdr + rl, x + hdr + 2 * rl, rl);
        y[hdr - 4] = 2;
        y[hdr + rl] = 1;
        zind_chain *b = NULL;
        CHECK_RC(zind_chain_import("drop", y, hdr + 2 * rl, &b), ZXV_OK);
        CHECK_RC(zind_chain_verify(e->cap, b, trust1, 1, &bad), ZIND_E_CHAIN);
        CHECK(bad == 1, "dropped record found at 1");
        zind_chain_destroy(b);
        /* swap records 1 and 2 (keeping seq fields): links break */
        memcpy(y, x, l);
        memcpy(y + hdr + rl, x + hdr + 2 * rl, rl);
        memcpy(y + hdr + 2 * rl, x + hdr + rl, rl);
        y[hdr + rl] = 1;
        y[hdr + 2 * rl] = 2;
        CHECK_RC(zind_chain_import("drop", y, l, &b), ZXV_OK);
        CHECK_RC(zind_chain_verify(e->cap, b, trust1, 1, &bad), ZIND_E_CHAIN);
        zind_chain_destroy(b);
        /* a record whose seq is out of order is malformed */
        memcpy(y, x, l);
        y[hdr + rl] = 5;
        CHECK_RC(zind_chain_import("drop", y, l, &b), ZXV_E_FORMAT);
        /* an impossible count is refused before allocation */
        memcpy(y, x, l);
        y[hdr - 1] = 0x7f;
        CHECK_RC(zind_chain_import("drop", y, l, &b), ZXV_E_FORMAT);
        free(x);
        free(y);
        zind_chain_destroy(a);
    }
    free(buf);
    zind_chain_destroy(c);
    zxv_cap_destroy(nosign);
    zind_signer_destroy(s1);
    zind_signer_destroy(s1b);
    zind_signer_destroy(s2);
    zind_signer_destroy(NULL);
    zind_chain_destroy(NULL);
}

/* ---------- tags and the quarantine sink ---------- */

static void test_qsink(zt_env *e) {
    zxv_hexlog *log = NULL;
    zind_qsink *q = NULL;
    zxv_cap *noattest = NULL;
    uint8_t tag = 0, subj[32], rt = 0, base = 0, pol = 0, ph = 0;
    uint64_t seq = 9, w = 0;
    int32_t lp = -1, poisoned = 0;
    uint32_t cnt = 0;

    for (int32_t l = 0; l <= 3; l++)
        for (uint8_t p = 0; p <= 3; p++)
            for (uint8_t f = 0; f <= 3; f++) {
                CHECK_RC(zind_tag(l, p, f, &tag), ZXV_OK);
                CHECK(zind_tag_lp(tag, &lp) == ZXV_OK && lp == l, "lp round trip %d", l);
                CHECK(zxv_hex_unpack(tag, NULL, &pol, &ph) == ZXV_OK && pol == p && ph == f,
                      "fields kept");
            }
    CHECK_RC(zind_tag(4, 0, 0, &tag), ZXV_E_ARG);
    CHECK_RC(zind_tag(0, 4, 0, &tag), ZXV_E_ARG);
    CHECK_RC(zind_tag_lp(64, &lp), ZXV_E_ARG);

    CHECK_RC(zxv_hexlog_create(e->cap, 3, &log), ZXV_OK);
    CHECK_RC(zxv_cap_attenuate(e->cap, ZXV_CAP_LEDGER_READ, &noattest), ZXV_OK);
    CHECK_RC(zind_qsink_create(noattest, log, NULL, &q), ZXV_E_CAP);
    CHECK_RC(zind_qsink_create(e->cap, NULL, NULL, &q), ZXV_E_ARG);
    CHECK_RC(zind_qsink_create(e->cap, log, e->ctx, &q), ZXV_OK);
    zind_subject("q", (const uint8_t *) "a", 1, subj);

    zind_tag(ZXV_LP_TRUE, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_MANIFEST, &tag);
    CHECK_RC(zind_qsink_event(q, tag, 1, 1, subj, &seq), ZXV_E_ARG);
    zind_tag(ZXV_LP_NEITHER, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_POTENTIAL, &tag);
    CHECK_RC(zind_qsink_event(q, tag, 1, 1, subj, &seq), ZXV_E_ARG);
    CHECK_RC(zind_qsink_event(q, 64, 1, 1, subj, &seq), ZXV_E_ARG);
    CHECK_RC(zind_qsink_event(q, 0, 1, 1, NULL, &seq), ZXV_E_ARG);

    zind_tag(ZXV_LP_BOTH, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_EMERGING, &tag);
    CHECK_RC(zind_qsink_event(q, tag, ZIND_REASON_HELD_BOTH, 7, subj, &seq), ZXV_OK);
    CHECK(seq == 0, "first record");
    CHECK_RC(zxv_hexlog_at(log, 0, NULL, &rt, NULL, NULL, NULL, NULL, NULL), ZXV_OK);
    CHECK(zxv_hex_unpack(rt, &base, &pol, &ph) == ZXV_OK && base == ZXV_HEX_BASE_BOTH &&
              pol == ZXV_HEX_POL_FORBIDDEN && ph == ZXV_HEX_PHASE_EMERGING,
          "Both is recorded as both/forbidden/emerging (%s)", zxv_hex_name(rt));
    zind_tag(ZXV_LP_FALSE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_MANIFEST, &tag);
    CHECK_RC(zind_qsink_event(q, tag, ZIND_REASON_TAMPER, 7, subj, &seq), ZXV_OK);
    CHECK_RC(zind_qsink_event(q, tag, ZIND_REASON_REPLAY, 8, subj, &seq), ZXV_OK);
    CHECK_RC(zind_qsink_written(q, &w), ZXV_OK);
    CHECK(w == 3, "3 written");
    CHECK_RC(zxv_hexlog_verify(log), ZXV_OK);
    CHECK_RC(zxv_ctx_is_poisoned(e->ctx, &poisoned), ZXV_OK);
    CHECK(!poisoned, "not halted yet");
    /* full: the context is halted */
    CHECK_RC(zind_qsink_event(q, tag, ZIND_REASON_REPLAY, 8, subj, &seq), ZXV_E_CAPACITY);
    CHECK_RC(zxv_ctx_is_poisoned(e->ctx, &poisoned), ZXV_OK);
    CHECK(poisoned, "full log halts the context");
    CHECK_RC(zxv_hexlog_count(log, &cnt), ZXV_E_POISONED); /* fails closed once halted */
    (void) cnt;
    CHECK_RC(zind_qsink_written(q, &w), ZXV_OK);
    CHECK(w == 3, "still 3");
    zind_qsink_destroy(q);
    zxv_hexlog_destroy(log);
    zxv_cap_destroy(noattest);
}

/* ---------- writer / reader / identifiers ---------- */

static void test_bytes(void) {
    uint8_t b[64];
    zind_wr w;
    zind_rd r;
    char s[256];
    zind_qty q;
    zind_wr_init(&w, b, sizeof b);
    zind_wr_u8(&w, 0xAB);
    zind_wr_u16(&w, 0x1234);
    zind_wr_u32(&w, 0xDEADBEEF);
    zind_wr_u64(&w, 0x0102030405060708ULL);
    zind_wr_str(&w, "hello");
    zind_wr_qty(&w, Q(-3, 4));
    CHECK(!w.overflow && w.len == 1 + 2 + 4 + 8 + 6 + 16, "writer length %zu", w.len);
    CHECK(b[1] == 0x34 && b[2] == 0x12, "little endian");
    zind_rd_init(&r, b, w.len);
    CHECK(zind_rd_u8(&r) == 0xAB, "u8");
    CHECK(zind_rd_u16(&r) == 0x1234, "u16");
    CHECK(zind_rd_u32(&r) == 0xDEADBEEF, "u32");
    CHECK(zind_rd_u64(&r) == 0x0102030405060708ULL, "u64");
    zind_rd_str(&r, s, sizeof s);
    CHECK(strcmp(s, "hello") == 0, "str");
    zind_rd_qty(&r, &q);
    CHECK(!r.bad && qeq(q, Q(-3, 4)) && r.pos == r.len, "qty");
    CHECK(zind_rd_u8(&r) == 0 && r.bad, "read past end is bad");
    /* overflow */
    zind_wr_init(&w, b, 3);
    zind_wr_u32(&w, 1);
    CHECK(w.overflow && w.len == 0, "overflow sets flag");
    zind_wr_u8(&w, 1);
    CHECK(w.len == 0, "later writes ignored");
    {
        char longs[300];
        memset(longs, 'x', 299);
        longs[299] = 0;
        zind_wr_init(&w, b, sizeof b);
        zind_wr_str(&w, longs);
        CHECK(w.overflow, "string over 255 overflows");
    }
    /* reader: NUL inside a string, short cap, invalid qty */
    {
        uint8_t x[] = {3, 'a', 0, 'b'};
        zind_rd_init(&r, x, sizeof x);
        zind_rd_str(&r, s, sizeof s);
        CHECK(r.bad, "NUL inside string is bad");
        uint8_t y[] = {3, 'a', 'b', 'c'};
        zind_rd_init(&r, y, sizeof y);
        zind_rd_str(&r, s, 3);
        CHECK(r.bad, "string longer than cap is bad");
        uint8_t z[16] = {2, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0};
        zind_rd_init(&r, z, sizeof z);
        zind_rd_qty(&r, &q);
        CHECK(r.bad && qeq(q, ZIND_QTY_ZERO), "2/4 not canonical: bad");
    }
    CHECK(zind_ident_ok("MSCU1234565", 16), "ident");
    CHECK(zind_ident_ok("a.b_c:d/e-f", 16), "ident punctuation");
    CHECK(!zind_ident_ok("", 16), "empty");
    CHECK(!zind_ident_ok("a b", 16), "space");
    CHECK(!zind_ident_ok("abcdefghijklmnopq", 16), "too long");
    CHECK(zind_ident_ok("abcdefghijklmnop", 16), "exactly max");
    CHECK(!zind_ident_ok(NULL, 16), "NULL");
    CHECK(!zind_ident_ok("caf\xc3\xa9", 16), "non-ASCII");
}

static void test_status(void) {
    char b[8];
    size_t n = 0;
    CHECK(strcmp(zind_status_name(ZIND_E_HELD), "ZIND_E_HELD") == 0, "held name");
    CHECK(strcmp(zind_status_name(ZIND_E_REPLAY), "ZIND_E_REPLAY") == 0, "replay name");
    CHECK(strcmp(zind_status_name(ZIND_E_UNTRUSTED), "ZIND_E_UNTRUSTED") == 0, "untrusted");
    CHECK(strcmp(zind_status_name(ZXV_E_ARG), zxv_status_name(ZXV_E_ARG)) == 0, "falls back");
    zind_set_error("a long error message that does not fit");
    CHECK_RC(zind_last_error(b, sizeof b, &n), ZXV_E_BUFFER);
    CHECK(n == strlen("a long error message that does not fit"), "needed length");
    CHECK_RC(zind_last_error(NULL, 0, &n), ZXV_E_BUFFER);
    zind_set_error("ok");
    CHECK_RC(zind_last_error(b, sizeof b, &n), ZXV_OK);
    CHECK(strcmp(b, "ok") == 0 && n == 2, "short error");
}

int main(void) {
    zt_env e;
    if (zt_env_open(&e, NULL) != 0) {
        fprintf(stderr, "cannot open a libzxv context\n");
        return 2;
    }
    test_status();
    test_qty();
    test_seen();
    test_bytes();
    test_chain(&e);
    test_qsink(&e);
    zt_env_close(&e);
    return zt_report("test_common");
}
