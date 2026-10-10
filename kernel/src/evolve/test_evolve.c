/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_evolve.c — host tests for src/evolve.
 *
 *  T1  CIDs and descriptor canonical encoding
 *  T2  capability negotiation across random fork pairs, and messages that
 *      interoperate on the shared core with byte-identical passthrough
 *  T3  unknown-field passthrough through a relay that knows only the base
 *  T4  must-understand, required, malformed input, decoder fuzz
 *  T5  lineage records signed with pq_matrix; merges, conflicts, resolution
 *  T6  random lineage DAGs: insertion-order independence of every state
 *  T7  trust: the update module's publisher list, pins, ratings
 *  T8  adoption sketches: opt-in, idempotent merge, estimate accuracy
 *  T9  conformance: exact fee checker vs pay_assure_fee, pinned suite,
 *      challenge/response, gating money paths only
 *  T10 device profiles mixing modules from different forks
 */
#include <stdio.h>
#include <string.h>

#include "evo.h"
#include "keccak.h"
#include "pq_matrix.h"
#include "update.h"
#include "zx_upcheck.h"
#include "pay_assure.h"

static int g_pass, g_fail;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            g_pass++;                                                                              \
        else {                                                                                     \
            g_fail++;                                                                              \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                  \
        }                                                                                          \
    } while (0)

static uint64_t g_rng = 0x9e3779b97f4a7c15ull;
static uint64_t rnd(void)
{
    g_rng ^= g_rng >> 12;
    g_rng ^= g_rng << 25;
    g_rng ^= g_rng >> 27;
    return g_rng * 0x2545f4914f6cdd1dull;
}
static uint32_t rndn(uint32_t n)
{
    return (uint32_t) (rnd() % n);
}

static void fake_cid(evo_cid_t *c, const char *s)
{
    evo_cid_of((const uint8_t *) s, (uint32_t) strlen(s), c);
}

/* ===== T1 ===== */
static void t1_cid_desc(void)
{
    printf("T1 CIDs and descriptors\n");
    evo_cid_t c;
    evo_cid_of((const uint8_t *) "abc", 3, &c);
    static const uint8_t abc[32] = {0x3a, 0x98, 0x5d, 0xa7, 0x4f, 0xe2, 0x25, 0xb2,
                                    0x04, 0x5c, 0x17, 0x2d, 0x6b, 0xd3, 0x90, 0xbd,
                                    0x85, 0x5f, 0x08, 0x6e, 0x3e, 0x9d, 0x52, 0x5b,
                                    0x46, 0xbf, 0xe2, 0x45, 0x11, 0x43, 0x15, 0x32};
    CHECK(c.b[0] == 0x01 && c.b[1] == 0x55 && c.b[2] == 0x16 && c.b[3] == 0x20);
    CHECK(memcmp(c.b + 4, abc, 32) == 0);

    evo_desc_t d, d2, e;
    CHECK(evo_desc_init(&d, "chat.message", EVO_KIND_MESSAGE, EVO_CLASS_GENERAL) == EVO_OK);
    CHECK(evo_desc_add(&d, 3, EVO_T_TEXT, EVO_FF_REQUIRED, 280, 0) == EVO_OK);
    CHECK(evo_desc_add(&d, 1, EVO_T_UINT, EVO_FF_REQUIRED, 0, 0) == EVO_OK);
    CHECK(evo_desc_add(&d, 2, EVO_T_CID, 0, 0, 0) == EVO_OK);
    CHECK(evo_desc_add(&d, 2, EVO_T_UINT, 0, 0, 0) == EVO_ERR_DUP);
    CHECK(d.n_fields == 3 && d.f[0].tag == 1 && d.f[1].tag == 2 && d.f[2].tag == 3);
    CHECK(evo_desc_add(&d, 0, EVO_T_UINT, 0, 0, 0) == EVO_ERR_ARG);
    CHECK(evo_desc_add(&d, 9, EVO_T_TEXT, 0, 0, 7) == EVO_ERR_ARG); /* default on text */
    CHECK(evo_desc_add(&d, 9, EVO_T_UINT, EVO_FF_REQUIRED, 0, 7) == EVO_ERR_ARG);
    CHECK(evo_desc_add(&d, 9, EVO_T_UINT, 0x80, 0, 0) == EVO_ERR_ARG);
    CHECK(evo_desc_add(&d, 4, EVO_T_UINT, 0, 0, 42) == EVO_OK);

    /* insertion order does not matter: same CID */
    evo_desc_init(&e, "chat.message", EVO_KIND_MESSAGE, EVO_CLASS_GENERAL);
    evo_desc_add(&e, 4, EVO_T_UINT, 0, 0, 42);
    evo_desc_add(&e, 2, EVO_T_CID, 0, 0, 0);
    evo_desc_add(&e, 1, EVO_T_UINT, EVO_FF_REQUIRED, 0, 0);
    evo_desc_add(&e, 3, EVO_T_TEXT, EVO_FF_REQUIRED, 280, 0);
    evo_cid_t c1, c2;
    evo_desc_cid(&d, &c1);
    evo_desc_cid(&e, &c2);
    CHECK(evo_cid_eq(&c1, &c2));

    uint8_t buf[EVO_DESC_ENC_MAX];
    uint32_t n = evo_desc_encode(&d, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(evo_desc_decode(buf, n, &d2) == EVO_OK);
    uint8_t buf2[EVO_DESC_ENC_MAX];
    CHECK(evo_desc_encode(&d2, buf2, sizeof(buf2)) == n && memcmp(buf, buf2, n) == 0);
    CHECK(evo_desc_decode(buf, n - 1, &d2) == EVO_ERR_PARSE);
    buf[n] = 0;
    CHECK(evo_desc_decode(buf, n + 1, &d2) == EVO_ERR_PARSE);
    CHECK(evo_desc_encode(&d, buf, 10) == 0);

    /* registry is self-certifying */
    static evo_registry_t reg1;
    evo_registry_t *reg = &reg1;
    evo_reg_init(reg);
    n = evo_desc_encode(&d, buf, sizeof(buf));
    evo_cid_t wrong = c1;
    wrong.b[10] ^= 1;
    CHECK(evo_reg_add_bytes(reg, buf, n, &wrong) == EVO_ERR_CID_MISMATCH);
    CHECK(evo_reg_add_bytes(reg, buf, n, &c1) == EVO_OK);
    CHECK(evo_reg_add(reg, &e, 0) == EVO_ERR_DUP);
    CHECK(evo_reg_find(reg, &c1) != 0);

    /* capset canonical */
    evo_capset_t s, s2;
    evo_capset_init(&s);
    evo_desc_t z;
    evo_desc_init(&z, "aaa.first", EVO_KIND_FILE, EVO_CLASS_GENERAL);
    CHECK(evo_capset_put(&s, &d) == EVO_OK && evo_capset_put(&s, &z) == EVO_OK);
    CHECK(s.n == 2 && s.c[0].name[0] == 'a');
    CHECK(evo_capset_put(&s, &d) == EVO_OK && s.n == 2);
    uint8_t cb[EVO_CAPSET_ENC_MAX];
    n = evo_capset_encode(&s, cb, sizeof(cb));
    CHECK(n > 0 && evo_capset_decode(cb, n, &s2) == EVO_OK && s2.n == 2);
    CHECK(evo_ext_tag("alice.mood") >= EVO_EXT_TAG_FIRST &&
          evo_ext_tag("alice.mood") <= EVO_TAG_MAX);
    CHECK(evo_ext_tag("alice.mood") == evo_ext_tag("alice.mood"));
}

/* ===== T2 / T3: random fork pairs ===== */
static void random_base(evo_desc_t *b)
{
    evo_desc_init(b, "chat.message", EVO_KIND_MESSAGE, EVO_CLASS_GENERAL);
    uint32_t nf = 2 + rndn(7);
    for (uint32_t i = 0; i < nf; i++) {
        uint16_t tag = (uint16_t) (1 + rndn(200));
        evo_type_t t = (evo_type_t) (1 + rndn(4));
        uint8_t fl = rndn(3) == 0 ? EVO_FF_REQUIRED : 0;
        if (rndn(4) == 0) fl |= EVO_FF_MUST_UNDERSTAND;
        uint64_t def = (t == EVO_T_UINT && !(fl & EVO_FF_REQUIRED)) ? rnd() % 1000 : 0;
        uint16_t ml = (t == EVO_T_BYTES || t == EVO_T_TEXT) ? (uint16_t) (8 + rndn(60)) : 0;
        evo_desc_add(b, tag, t, fl, ml, def); /* duplicate tags simply skipped */
    }
}

/* A fork: the base plus 0..6 optional extension fields, some colliding. */
static void random_fork(const evo_desc_t *base, evo_desc_t *f, bool allow_mu)
{
    memcpy(f, base, sizeof(*f));
    uint32_t ne = rndn(7);
    for (uint32_t i = 0; i < ne; i++) {
        char nm[24];
        snprintf(nm, sizeof(nm), "ext.%llx", (unsigned long long) rnd());
        uint16_t tag = rndn(3) == 0 ? (uint16_t) (EVO_EXT_TAG_FIRST + rndn(8)) /* collide often */
                                    : evo_ext_tag(nm);
        evo_type_t t = (evo_type_t) (1 + rndn(4));
        uint8_t fl = (allow_mu && rndn(4) == 0) ? EVO_FF_MUST_UNDERSTAND : 0;
        uint64_t def = t == EVO_T_UINT ? rnd() % 3 : 0;
        evo_desc_add(f, tag, t, fl, 0, def);
    }
}

static void fill_value(evo_msg_t *m, const evo_field_t *f)
{
    uint8_t b[EVO_VAL_MAX];
    if (f->type == EVO_T_UINT) {
        uint64_t v = rnd();
        if (rndn(3) == 0) v &= 0xff;
        if (rndn(8) == 0) v = 0;
        evo_msg_set_uint(m, f->tag, v);
        return;
    }
    uint32_t len = f->type == EVO_T_CID ? EVO_CID_LEN : rndn((uint32_t) f->max_len + 1u);
    if (len > 24 && f->type != EVO_T_CID) len = 24;
    for (uint32_t i = 0; i < len; i++)
        b[i] = (uint8_t) (f->type == EVO_T_TEXT ? 'a' + rndn(26) : rnd());
    evo_msg_set_bytes(m, f->tag, b, len);
}

static bool same_value(const evo_msg_t *a, const evo_msg_t *b, uint16_t tag, uint8_t type)
{
    if (type == EVO_T_UINT) {
        uint64_t x, y;
        return evo_msg_get_uint(a, tag, &x) && evo_msg_get_uint(b, tag, &y) && x == y;
    }
    const uint8_t *p, *q;
    uint32_t pl, ql;
    return evo_msg_get_bytes(a, tag, &p, &pl) && evo_msg_get_bytes(b, tag, &q, &ql) && pl == ql &&
           memcmp(p, q, pl) == 0;
}

static evo_registry_t g_regA, g_regB;
static evo_msg_t g_ma, g_mb, g_mc;
static evo_session_t g_sa, g_sb;

/* Peers exchange descriptors the way they would over the network: by CID,
 * each side fetching what need[] lists and verifying it. */
static bool exchange(evo_registry_t *ra, const evo_desc_t *da, evo_registry_t *rb,
                     const evo_desc_t *db, evo_capset_t *ca, evo_capset_t *cb)
{
    uint8_t buf[EVO_DESC_ENC_MAX];
    evo_reg_init(ra);
    evo_reg_init(rb);
    evo_reg_add(ra, da, 0);
    evo_reg_add(rb, db, 0);
    evo_capset_init(ca);
    evo_capset_init(cb);
    evo_capset_put(ca, da);
    evo_capset_put(cb, db);
    if (evo_negotiate(ra, ca, cb, &g_sa) != EVO_OK) return false;
    if (evo_negotiate(rb, cb, ca, &g_sb) != EVO_OK) return false;
    bool same = evo_cid_eq(&ca->c[0].cid, &cb->c[0].cid);
    if (!same && (g_sa.n_need != 1 || g_sb.n_need != 1)) return false;
    if (g_sa.n_need) {
        uint32_t n = evo_desc_encode(db, buf, sizeof(buf));
        if (evo_reg_add_bytes(ra, buf, n, &g_sa.need[0]) != EVO_OK) return false;
    }
    if (g_sb.n_need) {
        uint32_t n = evo_desc_encode(da, buf, sizeof(buf));
        if (evo_reg_add_bytes(rb, buf, n, &g_sb.need[0]) != EVO_OK) return false;
    }
    return evo_negotiate(ra, ca, cb, &g_sa) == EVO_OK && evo_negotiate(rb, cb, ca, &g_sb) == EVO_OK;
}

static void t2_random_pairs(void)
{
    printf("T2 negotiation across random fork pairs\n");
    uint32_t pairs = 0, interop = 0, identical = 0, mu_refused = 0;
    for (uint32_t it = 0; it < 400; it++) {
        evo_desc_t base, fa, fb;
        random_base(&base);
        random_fork(&base, &fa, it % 2 == 0);
        random_fork(&base, &fb, it % 3 == 0);
        evo_capset_t ca, cb;
        bool ok = exchange(&g_regA, &fa, &g_regB, &fb, &ca, &cb);
        CHECK(ok);
        if (!ok) continue;
        pairs++;
        const evo_sess_cap_t *ea = evo_session_find(&g_sa, "chat.message");
        const evo_sess_cap_t *eb = evo_session_find(&g_sb, "chat.message");
        CHECK(ea && eb && ea->status == EVO_OK && eb->status == EVO_OK);
        if (!ea || !eb || ea->status != EVO_OK) continue;
        CHECK(evo_cid_eq(&ea->core_cid, &eb->core_cid)); /* both compute the same core */
        CHECK(ea->allowed && !ea->money);
        bool core_ok = true; /* the shared base is always in the core */
        for (uint32_t i = 0; i < base.n_fields; i++)
            core_ok = core_ok && evo_desc_field(&ea->core, base.f[i].tag);
        CHECK(core_ok);

        /* A writes every field it knows except must-understand extensions */
        evo_msg_init(&g_ma, &fa);
        for (uint32_t i = 0; i < fa.n_fields; i++) {
            const evo_field_t *f = &fa.f[i];
            bool ext = !evo_desc_field(&base, f->tag);
            if (ext && (f->flags & EVO_FF_MUST_UNDERSTAND) && !evo_desc_field(&ea->core, f->tag))
                continue;
            if (!(f->flags & EVO_FF_REQUIRED) && rndn(4) == 0) continue;
            fill_value(&g_ma, f);
        }
        uint8_t wa[EVO_MSG_MAX], wb[EVO_MSG_MAX], wc[EVO_MSG_MAX];
        uint32_t la, lb, lc;
        evo_status_t st = evo_msg_encode(&g_ma, &ea->core, wa, sizeof(wa), &la);
        CHECK(st == EVO_OK);
        if (st != EVO_OK) continue;
        st = evo_msg_decode(&fb, wa, la, &g_mb);
        CHECK(st == EVO_OK);
        if (st != EVO_OK) continue;
        bool vals = true; /* the core reads the same on both sides */
        for (uint32_t i = 0; i < ea->core.n_fields; i++) {
            const evo_field_t *f = &ea->core.f[i];
            if (evo_msg_state(&g_ma, f->tag) == EVO_V_PRESENT)
                vals = vals && same_value(&g_ma, &g_mb, f->tag, f->type);
            else
                vals = vals && evo_msg_state(&g_mb, f->tag) == EVO_V_DEFAULTED;
        }
        CHECK(vals);
        /* B re-encodes untouched: byte-identical, so A's extensions survive */
        st = evo_msg_encode(&g_mb, 0, wb, sizeof(wb), &lb);
        CHECK(st == EVO_OK && lb == la && memcmp(wa, wb, la) == 0);
        if (st == EVO_OK && lb == la && memcmp(wa, wb, la) == 0) identical++;
        st = evo_msg_decode(&fa, wb, lb, &g_mc);
        CHECK(st == EVO_OK);
        bool back = st == EVO_OK;
        for (uint32_t i = 0; back && i < fa.n_fields; i++)
            if (evo_msg_state(&g_ma, fa.f[i].tag) == EVO_V_PRESENT)
                back = same_value(&g_ma, &g_mc, fa.f[i].tag, fa.f[i].type);
        CHECK(back);
        if (vals && back) interop++;

        /* a must-understand extension the peer lacks is refused at send time */
        for (uint32_t i = 0; i < fa.n_fields; i++) {
            const evo_field_t *f = &fa.f[i];
            if (!(f->flags & EVO_FF_MUST_UNDERSTAND) || evo_desc_field(&ea->core, f->tag)) continue;
            fill_value(&g_ma, f);
            CHECK(evo_msg_encode(&g_ma, &ea->core, wc, sizeof(wc), &lc) == EVO_ERR_MUST_UNDERSTAND);
            /* and if sent anyway, a receiver that does not know it rejects */
            if (!evo_desc_field(&fb, f->tag)) {
                CHECK(evo_msg_encode(&g_ma, 0, wc, sizeof(wc), &lc) == EVO_OK);
                CHECK(evo_msg_decode(&fb, wc, lc, &g_mb) == EVO_ERR_MUST_UNDERSTAND);
            }
            mu_refused++;
            break;
        }
    }
    printf("  %u pairs, %u interoperated on the core, %u byte-identical passthroughs, %u MU "
           "refusals\n",
           pairs, interop, identical, mu_refused);
    CHECK(pairs == 400 && interop == 400 && identical == 400 && mu_refused > 0);

    /* a REQUIRED field the other side does not know is a real conflict */
    evo_desc_t base, fa, fb;
    evo_desc_init(&base, "chat.message", EVO_KIND_MESSAGE, EVO_CLASS_GENERAL);
    evo_desc_add(&base, 1, EVO_T_TEXT, EVO_FF_REQUIRED, 0, 0);
    memcpy(&fa, &base, sizeof(fa));
    memcpy(&fb, &base, sizeof(fb));
    evo_desc_add(&fb, 2, EVO_T_UINT, EVO_FF_REQUIRED, 0, 0);
    evo_capset_t ca, cb;
    CHECK(exchange(&g_regA, &fa, &g_regB, &fb, &ca, &cb));
    CHECK(g_sa.n == 1 && g_sa.e[0].status == EVO_ERR_CONFLICT && !g_sa.e[0].allowed);
    /* families only one side has are simply absent from the session */
    evo_desc_t other;
    evo_desc_init(&other, "zz.only.mine", EVO_KIND_FILE, EVO_CLASS_GENERAL);
    evo_reg_add(&g_regA, &other, 0);
    evo_capset_put(&ca, &other);
    CHECK(evo_negotiate(&g_regA, &ca, &cb, &g_sa) == EVO_OK &&
          !evo_session_find(&g_sa, "zz.only.mine"));
}

static void t3_relay(void)
{
    printf("T3 unknown-field passthrough via a base-only relay\n");
    evo_desc_t base, ext;
    evo_desc_init(&base, "post", EVO_KIND_MESSAGE, EVO_CLASS_GENERAL);
    evo_desc_add(&base, 1, EVO_T_TEXT, EVO_FF_REQUIRED, 0, 0);
    evo_desc_add(&base, 2, EVO_T_UINT, 0, 0, 7);
    memcpy(&ext, &base, sizeof(ext));
    uint16_t mood = evo_ext_tag("alice.mood"), pic = evo_ext_tag("alice.picture");
    evo_desc_add(&ext, mood, EVO_T_UINT, 0, 0, 0);
    evo_desc_add(&ext, pic, EVO_T_CID, 0, 0, 0);
    evo_cid_t pc;
    fake_cid(&pc, "picture bytes");

    evo_msg_init(&g_ma, &ext);
    evo_msg_set_bytes(&g_ma, 1, (const uint8_t *) "hello", 5);
    evo_msg_set_uint(&g_ma, mood, 3);
    evo_msg_set_bytes(&g_ma, pic, pc.b, EVO_CID_LEN);
    uint8_t w1[EVO_MSG_MAX], w2[EVO_MSG_MAX];
    uint32_t l1, l2;
    CHECK(evo_msg_encode(&g_ma, 0, w1, sizeof(w1), &l1) == EVO_OK);
    /* relay knows only the base */
    CHECK(evo_msg_decode(&base, w1, l1, &g_mb) == EVO_OK);
    CHECK(g_mb.n_unknown == 2);
    uint64_t v = 0;
    CHECK(evo_msg_get_uint(&g_mb, 2, &v) && v == 7 && evo_msg_state(&g_mb, 2) == EVO_V_DEFAULTED);
    /* the relay edits a field it does know, and forwards */
    evo_msg_set_uint(&g_mb, 2, 9);
    CHECK(evo_msg_encode(&g_mb, 0, w2, sizeof(w2), &l2) == EVO_OK);
    CHECK(evo_msg_decode(&ext, w2, l2, &g_mc) == EVO_OK);
    const uint8_t *p;
    uint32_t pl;
    CHECK(evo_msg_get_uint(&g_mc, mood, &v) && v == 3);
    CHECK(evo_msg_get_bytes(&g_mc, pic, &p, &pl) && pl == EVO_CID_LEN && memcmp(p, pc.b, pl) == 0);
    CHECK(evo_msg_get_uint(&g_mc, 2, &v) && v == 9);
    CHECK(evo_msg_get_bytes(&g_mc, 1, &p, &pl) && pl == 5 && memcmp(p, "hello", 5) == 0);
    /* an older reader sees defaults; a newer one round-trips exactly */
    CHECK(evo_msg_encode(&g_mc, 0, w1, sizeof(w1), &l1) == EVO_OK && l1 == l2 &&
          memcmp(w1, w2, l1) == 0);
    /* setting a field the relay holds as unknown with a new type collides */
    evo_desc_t clash;
    memcpy(&clash, &base, sizeof(clash));
    evo_desc_add(&clash, mood, EVO_T_TEXT, 0, 0, 0);
    CHECK(evo_msg_decode(&clash, w2, l2, &g_mb) == EVO_OK); /* typed differently -> preserved */
    CHECK(g_mb.n_unknown == 2 && evo_msg_state(&g_mb, mood) == EVO_V_DEFAULTED);
    evo_msg_set_bytes(&g_mb, mood, (const uint8_t *) "x", 1);
    CHECK(evo_msg_encode(&g_mb, 0, w1, sizeof(w1), &l1) == EVO_ERR_CONFLICT);
}

/* ===== T4 ===== */
static void t4_strict(void)
{
    printf("T4 must-understand, required, malformed, fuzz\n");
    evo_desc_t d;
    evo_desc_init(&d, "pay.request", EVO_KIND_MESSAGE, EVO_CLASS_MONEY);
    evo_desc_add(&d, 1, EVO_T_UINT, EVO_FF_REQUIRED | EVO_FF_MUST_UNDERSTAND, 0, 0);
    evo_desc_add(&d, 2, EVO_T_TEXT, 0, 16, 0);
    evo_msg_init(&g_ma, &d);
    uint8_t w[EVO_MSG_MAX];
    uint32_t l;
    CHECK(evo_msg_encode(&g_ma, &d, w, sizeof(w), &l) == EVO_ERR_MISSING_REQUIRED);
    CHECK(evo_msg_set_bytes(&g_ma, 2, (const uint8_t *) "0123456789abcdefX", 17) == EVO_ERR_ARG);
    evo_msg_set_uint(&g_ma, 1, 1000);
    CHECK(evo_msg_encode(&g_ma, &d, w, sizeof(w), &l) == EVO_OK);
    CHECK(w[1] & 0x80); /* MU bit on the wire */
    CHECK(evo_msg_decode(&d, w, l, &g_mb) == EVO_OK);
    /* missing required on decode */
    static const uint8_t only2[] = {2, 0, EVO_T_TEXT, 1, 0, 'x'};
    CHECK(evo_msg_decode(&d, only2, sizeof(only2), &g_mb) == EVO_ERR_MISSING_REQUIRED);
    /* non-minimal uint, descending tags, truncation, bad type */
    static const uint8_t nonmin[] = {1, 0x80, EVO_T_UINT, 2, 0, 5, 0};
    CHECK(evo_msg_decode(&d, nonmin, sizeof(nonmin), &g_mb) == EVO_ERR_PARSE);
    static const uint8_t desc[] = {2, 0, EVO_T_TEXT, 0, 0, 1, 0x80, EVO_T_UINT, 1, 0, 5};
    CHECK(evo_msg_decode(&d, desc, sizeof(desc), &g_mb) == EVO_ERR_PARSE);
    CHECK(evo_msg_decode(&d, w, l - 1, &g_mb) == EVO_ERR_PARSE);
    static const uint8_t badt[] = {1, 0x80, 9, 1, 0, 5};
    CHECK(evo_msg_decode(&d, badt, sizeof(badt), &g_mb) == EVO_ERR_PARSE);
    /* a required field typed differently is an error, not a passthrough */
    static const uint8_t reqty[] = {1, 0x80, EVO_T_TEXT, 1, 0, 'x'};
    CHECK(evo_msg_decode(&d, reqty, sizeof(reqty), &g_mb) == EVO_ERR_PARSE);

    /* fuzz: anything accepted re-encodes canonically and stably */
    uint32_t accepted = 0;
    for (uint32_t it = 0; it < 20000; it++) {
        uint8_t in[64], o1[EVO_MSG_MAX], o2[EVO_MSG_MAX];
        uint32_t n = rndn(sizeof(in));
        for (uint32_t i = 0; i < n; i++) in[i] = (uint8_t) rnd();
        if (n >= 6 && rndn(2)) { /* bias toward plausible TLVs */
            in[0] = 1;
            in[1] = (uint8_t) (in[1] & 0x80);
            in[2] = EVO_T_UINT;
            in[3] = 1;
            in[4] = 0;
            in[5] |= 1;
        }
        if (evo_msg_decode(&d, in, n, &g_mb) != EVO_OK) continue;
        accepted++;
        uint32_t l1, l2;
        bool ok = evo_msg_encode(&g_mb, 0, o1, sizeof(o1), &l1) == EVO_OK && l1 == n &&
                  memcmp(o1, in, n) == 0;
        ok = ok && evo_msg_decode(&d, o1, l1, &g_mc) == EVO_OK &&
             evo_msg_encode(&g_mc, 0, o2, sizeof(o2), &l2) == EVO_OK && l2 == l1;
        CHECK(ok);
    }
    printf("  fuzz: %u of 20000 inputs accepted, all canonical\n", accepted);
    CHECK(accepted > 0);
}

/* ===== T5-T6: lineage ===== */
typedef struct {
    pqm_sig_pk_t pk;
    pqm_sig_sk_t sk;
    uint8_t pk_enc[PQM_SIG_PK_MAX_BYTES];
    uint32_t pk_len;
    uint8_t id[32];
} author_t;

static author_t g_up, g_alice, g_bob, g_matrix;
static pqm_sig_t g_sig;
static const uint8_t LIN_CTX[] = "ZXV-EVO-lineage";

static void make_author(author_t *a, uint8_t seedbyte, pqm_level_t lvl)
{
    uint8_t seed[PQM_SEED_BYTES];
    memset(seed, seedbyte, sizeof(seed));
    CHECK(pqm_sig_keygen(lvl, seed, &a->pk, &a->sk));
    a->pk_len = (uint32_t) pqm_sig_pk_encode(&a->pk, a->pk_enc, sizeof(a->pk_enc));
    CHECK(a->pk_len > 0);
    evo_key_id(a->pk_enc, a->pk_len, a->id);
}

static bool pqm_hook(const uint8_t *pk, uint32_t pk_len, const uint8_t *msg, uint32_t msg_len,
                     const uint8_t *sig, uint32_t sig_len, void *ctx)
{
    static pqm_sig_pk_t P;
    static pqm_sig_t S;
    (void) ctx;
    if (!pqm_sig_pk_decode(&P, pk, pk_len) || !pqm_sig_decode(&S, sig, sig_len)) return false;
    if (P.level != S.level) return false;
    return pqm_verify(&P, msg, msg_len, LIN_CTX, sizeof(LIN_CTX) - 1, &S);
}

#define WIRE_CAP (EVO_LIN_BODY_MAX + 12u + PQM_SIG_PK_MAX_BYTES + PQM_SIG_MAX_BYTES)
static uint8_t g_wire[WIRE_CAP];

static uint32_t sign_fork(const author_t *a, const evo_fork_t *f, uint8_t *out, uint32_t cap,
                          evo_cid_t *cid)
{
    uint8_t body[EVO_LIN_BODY_MAX], sig[PQM_SIG_MAX_BYTES];
    uint32_t bl = evo_fork_encode(f, body, sizeof(body));
    if (!bl) return 0;
    if (cid) evo_cid_of(body, bl, cid);
    if (!pqm_sign(&a->sk, body, bl, LIN_CTX, sizeof(LIN_CTX) - 1, 0, &g_sig)) return 0;
    uint32_t sl = (uint32_t) pqm_sig_encode(&g_sig, sig, sizeof(sig));
    return evo_fork_wire(body, bl, a->pk_enc, a->pk_len, sig, sl, out, cap);
}

static evo_dag_t g_dag, g_dag2;
static evo_state_t g_st;

static evo_status_t add(const author_t *a, const evo_fork_t *f, evo_cid_t *cid, uint32_t *idx)
{
    uint32_t n = sign_fork(a, f, g_wire, WIRE_CAP, cid);
    if (!n) return EVO_ERR_ARG;
    return evo_dag_add_wire(&g_dag, g_wire, n, pqm_hook, 0, idx);
}

static evo_cid_t g_r0, g_a1, g_b1, g_m, g_a2;
static evo_cid_t v_chat, v_ledger, v_ui, v_uA, v_uB, v_games, v_maps, v_games2;

static void t5_lineage(void)
{
    printf("T5 lineage records, merges, conflicts\n");
    make_author(&g_up, 1, PQM_LEVEL_STANDARD);
    make_author(&g_alice, 2, PQM_LEVEL_STANDARD);
    make_author(&g_bob, 3, PQM_LEVEL_STANDARD);
    fake_cid(&v_chat, "chat-1");
    fake_cid(&v_ledger, "ledger-1");
    fake_cid(&v_ui, "ui-1");
    fake_cid(&v_uA, "ui-alice");
    fake_cid(&v_uB, "ui-bob");
    fake_cid(&v_games, "games-1");
    fake_cid(&v_maps, "maps-1");
    fake_cid(&v_games2, "games-2");
    evo_dag_init(&g_dag);
    evo_fork_t f;
    uint32_t idx, i_r0, i_a1, i_b1, i_m;

    evo_fork_init(&f, g_up.id, "", 0); /* the upstream root: no label at all */
    evo_fork_change(&f, EVO_CH_ADD, "ui", &v_ui, 0);
    evo_fork_change(&f, EVO_CH_ADD, "chat", &v_chat, 0);
    evo_fork_change(&f, EVO_CH_ADD, "ledger", &v_ledger, 0);
    CHECK(f.ch[0].slot[0] == 'c' && f.ch[2].slot[0] == 'u'); /* kept sorted */
    CHECK(evo_fork_change(&f, EVO_CH_ADD, "ui", &v_ui, 0) == EVO_ERR_DUP);
    CHECK(add(&g_up, &f, &g_r0, &i_r0) == EVO_OK);
    CHECK(add(&g_up, &f, 0, &idx) == EVO_ERR_DUP && idx == i_r0);

    uint8_t body[EVO_LIN_BODY_MAX];
    evo_fork_t f2;
    uint32_t bl = evo_fork_encode(&f, body, sizeof(body));
    CHECK(bl && evo_fork_decode(body, bl, &f2) == EVO_OK && memcmp(&f, &f2, sizeof(f)) == 0);

    evo_fork_init(&f, g_alice.id, "alice-garden 3.14-ish", 20261009);
    evo_fork_parent(&f, &g_r0);
    evo_fork_change(&f, EVO_CH_REPLACE, "ui", &v_uA, &v_ui);
    CHECK(add(&g_alice, &f, &g_a1, &i_a1) == EVO_OK);

    evo_fork_init(&f, g_bob.id, "bob/v900", 1);
    evo_fork_parent(&f, &g_r0);
    evo_fork_change(&f, EVO_CH_REPLACE, "ui", &v_uB, &v_ui);
    evo_fork_change(&f, EVO_CH_ADD, "games", &v_games, 0);
    CHECK(add(&g_bob, &f, &g_b1, &i_b1) == EVO_OK);
    CHECK(evo_dag_is_ancestor(&g_dag, i_r0, i_b1) && !evo_dag_is_ancestor(&g_dag, i_a1, i_b1));

    /* failures */
    evo_fork_init(&f, g_bob.id, "x", 2);
    evo_fork_parent(&f, &g_b1);
    evo_fork_change(&f, EVO_CH_REPLACE, "ui", &v_games, &v_ui); /* wrong old */
    CHECK(add(&g_bob, &f, 0, 0) == EVO_ERR_CONFLICT);
    evo_fork_init(&f, g_bob.id, "x", 3);
    evo_fork_parent(&f, &g_b1);
    evo_fork_change(&f, EVO_CH_ADD, "games", &v_games2, 0); /* already present */
    CHECK(add(&g_bob, &f, 0, 0) == EVO_ERR_CONFLICT);
    evo_fork_init(&f, g_bob.id, "x", 4);
    evo_fork_parent(&f, &v_maps); /* not a record we hold */
    CHECK(add(&g_bob, &f, 0, 0) == EVO_ERR_UNKNOWN_PARENT);
    evo_fork_init(&f, g_bob.id, "x", 5);
    evo_fork_parent(&f, &g_b1);
    evo_fork_change(&f, EVO_CH_REMOVE, "nothing", 0, &v_ui);
    CHECK(add(&g_bob, &f, 0, 0) == EVO_ERR_CONFLICT);
    /* signed by Alice but claiming Bob */
    evo_fork_init(&f, g_bob.id, "x", 6);
    evo_fork_parent(&f, &g_b1);
    CHECK(add(&g_alice, &f, 0, 0) == EVO_ERR_BAD_SIG);
    /* tampered body */
    evo_fork_init(&f, g_bob.id, "tamper", 7);
    evo_fork_parent(&f, &g_b1);
    uint32_t n = sign_fork(&g_bob, &f, g_wire, WIRE_CAP, 0);
    g_wire[4 + 4 + 1 + EVO_CID_LEN + 32 + 1] ^= 1; /* first label byte */
    CHECK(evo_dag_add_wire(&g_dag, g_wire, n, pqm_hook, 0, 0) == EVO_ERR_BAD_SIG);
    CHECK(evo_dag_add_wire(&g_dag, g_wire, n - 1, pqm_hook, 0, 0) == EVO_ERR_PARSE);

    /* merging Alice and Bob: "ui" conflicts until the merge says how */
    evo_cid_t ps[2] = {g_a1, g_b1};
    char names[4][EVO_NAME_MAX];
    CHECK(evo_dag_conflicts(&g_dag, ps, 2, names, 4) == 1 && strcmp(names[0], "ui") == 0);
    evo_fork_init(&f, g_alice.id, "garden+bob", 8);
    evo_fork_parent(&f, &g_b1);
    evo_fork_parent(&f, &g_a1);
    CHECK(add(&g_alice, &f, 0, 0) == EVO_ERR_CONFLICT);
    evo_fork_change(&f, EVO_CH_REPLACE, "ui", &v_uA, &v_uB); /* keep Alice's UI */
    CHECK(add(&g_alice, &f, &g_m, &i_m) == EVO_OK);
    CHECK(evo_dag_state(&g_dag, i_m, &g_st) == EVO_OK && g_st.n == 4);
    CHECK(evo_cid_eq(evo_state_get(&g_st, "ui"), &v_uA));
    CHECK(evo_cid_eq(evo_state_get(&g_st, "games"), &v_games));
    CHECK(evo_cid_eq(evo_state_get(&g_st, "chat"), &v_chat));
    CHECK(evo_dag_is_ancestor(&g_dag, i_r0, i_m) && evo_dag_is_ancestor(&g_dag, i_b1, i_m));

    /* a merge with nothing to resolve needs no changes */
    evo_fork_init(&f, g_alice.id, "maps", 9);
    evo_fork_parent(&f, &g_a1);
    evo_fork_change(&f, EVO_CH_ADD, "maps", &v_maps, 0);
    CHECK(add(&g_alice, &f, &g_a2, &idx) == EVO_OK);
    evo_fork_init(&f, g_bob.id, "everything", 10);
    evo_fork_parent(&f, &g_a2);
    evo_fork_parent(&f, &g_m);
    CHECK(add(&g_bob, &f, 0, &idx) == EVO_OK);
    CHECK(evo_dag_state(&g_dag, idx, &g_st) == EVO_OK && g_st.n == 5);
    CHECK(evo_cid_eq(evo_state_get(&g_st, "ui"), &v_uA) && evo_state_get(&g_st, "maps"));

    /* delete-vs-modify across branches is a conflict too */
    evo_cid_t x1, x2;
    evo_fork_init(&f, g_bob.id, "no games", 11);
    evo_fork_parent(&f, &g_b1);
    evo_fork_change(&f, EVO_CH_REMOVE, "games", 0, &v_games);
    CHECK(add(&g_bob, &f, &x1, &idx) == EVO_OK);
    CHECK(evo_dag_state(&g_dag, idx, &g_st) == EVO_OK && !evo_state_get(&g_st, "games"));
    evo_fork_init(&f, g_bob.id, "games 2", 12);
    evo_fork_parent(&f, &g_b1);
    evo_fork_change(&f, EVO_CH_REPLACE, "games", &v_games2, &v_games);
    CHECK(add(&g_bob, &f, &x2, &idx) == EVO_OK);
    evo_cid_t px[2] = {x1, x2};
    CHECK(evo_dag_conflicts(&g_dag, px, 2, names, 4) == 1);
    evo_fork_init(&f, g_bob.id, "keep games 2", 13);
    evo_fork_parent(&f, &x1);
    evo_fork_parent(&f, &x2);
    CHECK(add(&g_bob, &f, 0, 0) == EVO_ERR_CONFLICT);
    evo_fork_change(&f, EVO_CH_REPLACE, "games", &v_games2, &v_games2);
    CHECK(add(&g_bob, &f, 0, &idx) == EVO_OK);
    CHECK(evo_dag_state(&g_dag, idx, &g_st) == EVO_OK &&
          evo_cid_eq(evo_state_get(&g_st, "games"), &v_games2));

    /* the release-grade signature level (MATRIX: ML-DSA-87 + SLH-DSA-256s) */
    make_author(&g_matrix, 4, PQM_LEVEL_MATRIX);
    evo_fork_init(&f, g_matrix.id, "anchored", 14);
    evo_fork_parent(&f, &g_r0);
    evo_fork_change(&f, EVO_CH_ADD, "maps", &v_maps, 0);
    CHECK(add(&g_matrix, &f, 0, &idx) == EVO_OK);
}

/* Random DAGs: every accepted node has a conflict-free state, and the state
 * of each record does not depend on the order records arrived in. */
#define RAND_RECS 40u
static uint8_t g_rw[RAND_RECS][8192];
static uint32_t g_rl[RAND_RECS];
static evo_cid_t g_rc[RAND_RECS];
static uint32_t g_rpar[RAND_RECS][EVO_LIN_MAX_PARENTS], g_rnp[RAND_RECS];
static evo_state_t g_st2;

static const char *slot_names[] = {"chat", "ledger", "ui",    "games",
                                   "maps", "model",  "theme", "voice"};

static void t6_random_dag(void)
{
    printf("T6 random lineage DAGs\n");
    evo_dag_init(&g_dag);
    uint32_t made = 0, merges = 0, resolved = 0;
    const author_t *who[3] = {&g_up, &g_alice, &g_bob};
    for (uint32_t r = 0; made < RAND_RECS && r < 400; r++) {
        evo_fork_t f;
        const author_t *a = who[rndn(3)];
        evo_fork_init(&f, a->id, "rand", r);
        uint32_t np = made == 0 ? 0 : 1 + rndn(made < 3 ? 1 : 3);
        uint32_t par[EVO_LIN_MAX_PARENTS];
        uint32_t got = 0;
        for (uint32_t p = 0; p < np; p++) {
            uint32_t k = rndn(made);
            if (evo_fork_parent(&f, &g_rc[k]) == EVO_OK) par[got++] = k;
        }
        /* the parents' merged state: union of their states; conflicts named */
        char conf[8][EVO_NAME_MAX];
        int32_t nc = got ? evo_dag_conflicts(&g_dag, f.parent, f.n_parents, conf, 8) : 0;
        CHECK(nc >= 0);
        for (int32_t c = 0; c < nc; c++) { /* resolve with a value some parent holds */
            const evo_cid_t *old = 0;
            for (uint32_t p = 0; p < got && !old; p++) {
                int32_t pi = evo_dag_find(&g_dag, &g_rc[par[p]]);
                evo_dag_state(&g_dag, (uint32_t) pi, &g_st);
                old = evo_state_get(&g_st, conf[c]);
            }
            evo_cid_t nv;
            fake_cid(&nv, slot_names[rndn(8)]);
            nv.b[5] ^= (uint8_t) r;
            if (old) {
                evo_cid_t o = *old;
                evo_fork_change(&f, EVO_CH_REPLACE, conf[c], &nv, &o);
            }
            resolved++;
        }
        /* plus a few ordinary changes against parent 0's state */
        if (got) {
            int32_t pi = evo_dag_find(&g_dag, &g_rc[par[0]]);
            evo_dag_state(&g_dag, (uint32_t) pi, &g_st);
        } else {
            memset(&g_st, 0, sizeof(g_st));
        }
        uint32_t nch = rndn(3);
        for (uint32_t c = 0; c < nch; c++) {
            const char *s = slot_names[rndn(8)];
            const evo_cid_t *cur = evo_state_get(&g_st, s);
            evo_cid_t nv;
            fake_cid(&nv, s);
            nv.b[6] ^= (uint8_t) (r * 7 + c);
            if (!cur)
                evo_fork_change(&f, EVO_CH_ADD, s, &nv, 0);
            else if (rndn(4) == 0) {
                evo_cid_t o = *cur;
                evo_fork_change(&f, EVO_CH_REMOVE, s, 0, &o);
            } else {
                evo_cid_t o = *cur;
                evo_fork_change(&f, EVO_CH_REPLACE, s, &nv, &o);
            }
        }
        uint32_t n = sign_fork(a, &f, g_rw[made], sizeof(g_rw[0]), &g_rc[made]);
        CHECK(n > 0 && n <= sizeof(g_rw[0]));
        uint32_t idx;
        evo_status_t st = evo_dag_add_wire(&g_dag, g_rw[made], n, pqm_hook, 0, &idx);
        /* a change against parent 0 may clash with another parent's state */
        if (st == EVO_ERR_CONFLICT || st == EVO_ERR_DUP) continue;
        CHECK(st == EVO_OK);
        if (st != EVO_OK) continue;
        g_rl[made] = n;
        g_rnp[made] = got;
        for (uint32_t p = 0; p < got; p++) g_rpar[made][p] = par[p];
        if (got > 1) merges++;
        CHECK(evo_dag_state(&g_dag, idx, &g_st) == EVO_OK);
        made++;
    }
    printf("  %u records, %u merges, %u conflicts resolved\n", made, merges, resolved);
    CHECK(made == RAND_RECS && merges > 5);

    /* replay in a different topological order: parents first, else random */
    bool done[RAND_RECS] = {false};
    evo_dag_init(&g_dag2);
    for (uint32_t placed = 0; placed < made;) {
        uint32_t k = rndn(made);
        if (done[k]) continue;
        bool ready = true;
        for (uint32_t p = 0; p < g_rnp[k]; p++) ready = ready && done[g_rpar[k][p]];
        if (!ready) continue;
        CHECK(evo_dag_add_wire(&g_dag2, g_rw[k], g_rl[k], pqm_hook, 0, 0) == EVO_OK);
        done[k] = true;
        placed++;
    }
    bool same = true;
    for (uint32_t k = 0; k < made; k++) {
        int32_t i1 = evo_dag_find(&g_dag, &g_rc[k]), i2 = evo_dag_find(&g_dag2, &g_rc[k]);
        same = same && i1 >= 0 && i2 >= 0 &&
               evo_dag_state(&g_dag, (uint32_t) i1, &g_st) == EVO_OK &&
               evo_dag_state(&g_dag2, (uint32_t) i2, &g_st2) == EVO_OK &&
               memcmp(&g_st, &g_st2, sizeof(g_st)) == 0;
        for (uint32_t j = 0; j < made && same; j++) {
            int32_t j1 = evo_dag_find(&g_dag, &g_rc[j]), j2 = evo_dag_find(&g_dag2, &g_rc[j]);
            same = evo_dag_is_ancestor(&g_dag, (uint32_t) j1, (uint32_t) i1) ==
                   evo_dag_is_ancestor(&g_dag2, (uint32_t) j2, (uint32_t) i2);
        }
    }
    CHECK(same);
}

/* ===== T7 ===== */
static upd_catalog_t g_upd;

static void t7_trust(void)
{
    printf("T7 trust via the update module's publisher list\n");
    evo_dag_init(&g_dag);
    evo_fork_t f;
    uint32_t i_r0, i_a1, i_b1, i_m;
    evo_fork_init(&f, g_up.id, "", 0);
    evo_fork_change(&f, EVO_CH_ADD, "ui", &v_ui, 0);
    add(&g_up, &f, &g_r0, &i_r0);
    evo_fork_init(&f, g_alice.id, "a", 1);
    evo_fork_parent(&f, &g_r0);
    evo_fork_change(&f, EVO_CH_REPLACE, "ui", &v_uA, &v_ui);
    add(&g_alice, &f, &g_a1, &i_a1);
    evo_fork_init(&f, g_bob.id, "b", 2);
    evo_fork_parent(&f, &g_r0);
    evo_fork_change(&f, EVO_CH_ADD, "games", &v_games, 0);
    add(&g_bob, &f, &g_b1, &i_b1);
    evo_fork_init(&f, g_alice.id, "m", 3);
    evo_fork_parent(&f, &g_a1);
    evo_fork_parent(&f, &g_b1);
    CHECK(add(&g_alice, &f, &g_m, &i_m) == EVO_OK);

    upd_init(&g_upd);
    upd_trust_author(&g_upd, g_up.id);
    upd_trust_author(&g_upd, g_alice.id);
    evo_trust_t t;
    evo_trust_init(&t, evo_trust_upd, &g_upd);
    uint32_t why = 99;
    CHECK(evo_trust_build_ok(&t, &g_dag, i_a1, &why) == EVO_OK);
    CHECK(evo_trust_build_ok(&t, &g_dag, i_m, &why) == EVO_ERR_UNTRUSTED && why == i_b1);
    /* pinning Bob's build endorses it (and its history) without trusting Bob */
    CHECK(evo_trust_pin(&t, &g_b1) == EVO_OK);
    CHECK(evo_trust_build_ok(&t, &g_dag, i_m, &why) == EVO_OK);
    /* ratings: below the user's bar blocks, even a trusted publisher */
    t.min_stars = 3;
    CHECK(evo_trust_rate_key(&t, g_alice.id, 2) == EVO_OK);
    CHECK(evo_trust_build_ok(&t, &g_dag, i_a1, &why) == EVO_ERR_UNTRUSTED && why == i_a1);
    CHECK(evo_trust_rate_key(&t, g_alice.id, 5) == EVO_OK &&
          evo_trust_stars_key(&t, g_alice.id) == 5);
    CHECK(evo_trust_build_ok(&t, &g_dag, i_m, &why) == EVO_OK);
    CHECK(evo_trust_rate_record(&t, &g_r0, 1) == EVO_OK); /* blocks everything after it */
    CHECK(evo_trust_build_ok(&t, &g_dag, i_m, &why) == EVO_ERR_UNTRUSTED && why == i_r0);
    CHECK(evo_trust_rate_record(&t, &g_r0, 9) == EVO_ERR_ARG);
    /* the same decision through zx_upcheck's per-bucket key lists */
    static zxu_config_t zc;
    memset(&zc, 0, sizeof(zc));
    zc.b[1].used = true;
    zc.b[1].nkeys = 1;
    memcpy(zc.b[1].keys[0], g_bob.pk.mldsa_pk, ZXU_PK_BYTES);
    uint8_t bob_raw[32];
    evo_key_id(zc.b[1].keys[0], ZXU_PK_BYTES, bob_raw);
    CHECK(!evo_trust_zxu(bob_raw, &zc)); /* bucket disabled */
    zc.b[1].enabled = true;
    CHECK(evo_trust_zxu(bob_raw, &zc) && !evo_trust_zxu(g_alice.id, &zc));
    evo_trust_t none;
    evo_trust_init(&none, 0, 0);
    CHECK(evo_trust_build_ok(&none, &g_dag, i_r0, &why) == EVO_ERR_UNTRUSTED);
}

/* ===== T8 ===== */
static void t8_adoption(void)
{
    printf("T8 adoption sketches\n");
    evo_cid_t var;
    fake_cid(&var, "variant");
    evo_adopt_t a, b, c;
    evo_adopt_init(&a, &var, 7);
    evo_adopt_init(&b, &var, 7);
    uint8_t secret[32];
    CHECK(evo_adopt_contribute(&a, false, secret) == EVO_ERR_ARG && a.n == 0);
    for (uint32_t i = 0; i < 50; i++) {
        for (uint32_t j = 0; j < 32; j++) secret[j] = (uint8_t) rnd();
        evo_adopt_contribute(&a, true, secret);
        evo_adopt_contribute(&a, true, secret); /* same node twice: once */
    }
    CHECK(evo_adopt_estimate(&a) == 50);
    /* 3000 nodes heard by two gossip halves that overlap by 1000 */
    evo_adopt_init(&a, &var, 8);
    evo_adopt_init(&b, &var, 8);
    for (uint32_t i = 0; i < 3000; i++) {
        for (uint32_t j = 0; j < 32; j++) secret[j] = (uint8_t) rnd();
        if (i < 2000) evo_adopt_contribute(&a, true, secret);
        if (i >= 1000) evo_adopt_contribute(&b, true, secret);
    }
    memcpy(&c, &a, sizeof(c));
    CHECK(evo_adopt_merge(&c, &b) == EVO_OK);
    uint64_t e1 = evo_adopt_estimate(&c);
    CHECK(evo_adopt_merge(&c, &b) == EVO_OK && evo_adopt_merge(&c, &a) == EVO_OK);
    CHECK(evo_adopt_estimate(&c) == e1); /* gossip repeats change nothing */
    printf("  estimate %llu for 3000 distinct nodes\n", (unsigned long long) e1);
    CHECK(e1 > 2000 && e1 < 4000);
    uint8_t buf[EVO_ADOPT_ENC_MAX];
    uint32_t n = evo_adopt_encode(&c, buf, sizeof(buf));
    evo_adopt_t d;
    CHECK(n > 0 && evo_adopt_decode(buf, n, &d) == EVO_OK && memcmp(&d, &c, sizeof(d)) == 0);
    buf[n - 1] ^= 0xff;
    buf[n - 9] = buf[n - 1]; /* break ascending order */
    CHECK(evo_adopt_decode(buf, n, &d) != EVO_OK || d.n == c.n);
    evo_adopt_t other;
    evo_adopt_init(&other, &var, 9);
    CHECK(evo_adopt_merge(&c, &other) == EVO_ERR_ARG); /* epochs never mix */
    /* the same secret in another epoch gives an unrelated token */
    evo_adopt_t e7, e9;
    evo_adopt_init(&e7, &var, 7);
    evo_adopt_init(&e9, &var, 9);
    evo_adopt_contribute(&e7, true, secret);
    evo_adopt_contribute(&e9, true, secret);
    CHECK(e7.h[0] != e9.h[0]);
}

/* ===== T9: conformance ===== */
static void up_hash(const uint8_t *m, uint32_t l, uint8_t o[32])
{
    sha3_256(m, l, o);
}
static bool up_post(const evo_line_t *l, uint32_t n)
{
    int64_t d = 0, c = 0;
    if (n == 0 || n > 8) return false;
    for (uint32_t i = 0; i < n; i++) {
        if (l[i].d_equity != l[i].d_debit - l[i].d_credit) return false;
        d += l[i].d_debit;
        c += l[i].d_credit;
    }
    return d == c;
}
static uint64_t up_repay(uint64_t p, uint32_t days)
{
    (void) days;
    return p;
}
static bool up_spend(const evo_consent_t *c, uint64_t a, uint64_t now)
{
    return c->granted && a <= c->max_amount && now < c->expires_at;
}
/* broken forks */
static uint64_t bad_fee_tithe(uint64_t a)
{
    return a / 1000 * 16; /* the retired ~1.6% tithe rate */
}
static uint64_t bad_fee_ceil(uint64_t a)
{
    return pay_assure_fee(a) + (a % 7 == 3 ? 1 : 0);
}
static uint64_t bad_repay(uint64_t p, uint32_t days)
{
    return p + p / 10000 * days; /* "just a little interest" */
}
static bool bad_post(const evo_line_t *l, uint32_t n)
{
    (void) l;
    return n > 0; /* accepts anything */
}
static bool bad_spend(const evo_consent_t *c, uint64_t a, uint64_t now)
{
    (void) now; /* ignores expiry */
    return c->granted && a <= c->max_amount;
}
static void bad_hash(const uint8_t *m, uint32_t l, uint8_t o[32])
{
    sha3_256(m, l, o);
    if (l > 10) o[0] ^= 1; /* passes the short KATs, fails on challenge */
}

static void t9_conformance(void)
{
    printf("T9 conformance\n");
    /* the exact checker agrees with pay_assure_fee everywhere we look */
    static const uint64_t edges[] = {0,          1,     2,        1124,       1125,
                                     2249,       2250,  10000000, 1ull << 32, (1ull << 63) - 1,
                                     1ull << 63, ~0ull, ~0ull - 1};
    bool agree = true;
    for (uint32_t i = 0; i < sizeof(edges) / sizeof(edges[0]); i++) {
        uint64_t t = pay_assure_fee(edges[i]);
        agree = agree && evo_fee_is_exact(edges[i], t) && !evo_fee_is_exact(edges[i], t + 1);
        if (t) agree = agree && !evo_fee_is_exact(edges[i], t - 1);
    }
    for (uint32_t i = 0; i < 100000 && agree; i++) {
        uint64_t a = rnd() >> rndn(64);
        uint64_t t = pay_assure_fee(a);
        agree = evo_fee_is_exact(a, t) && !evo_fee_is_exact(a, t + 1) &&
                (t == 0 || !evo_fee_is_exact(a, t - 1));
    }
    CHECK(agree);
    CHECK(pay_assure_fee(10000) == 8 && evo_fee_is_exact(10000, 8) &&
          !evo_fee_is_exact(10000, 161));

    evo_core_impl_t up = {up_hash, pay_assure_fee, up_post, up_repay, up_spend};
    CHECK(evo_conform_local(&up) == EVO_CHK_ALL);
    evo_challenge_t ch;
    evo_response_t rs, rs2;
    for (uint32_t j = 0; j < 32; j++) ch.seed[j] = (uint8_t) rnd();
    evo_conform_respond(&up, &ch, &rs);
    CHECK(evo_conform_check(&ch, &rs) == EVO_CHK_ALL);
    uint8_t enc[EVO_RESP_ENC_LEN];
    CHECK(evo_response_encode(&rs, enc, sizeof(enc)) == EVO_RESP_ENC_LEN);
    CHECK(evo_response_decode(enc, sizeof(enc), &rs2) == EVO_OK &&
          evo_conform_check(&ch, &rs2) == EVO_CHK_ALL);
    evo_challenge_t ch2 = ch;
    ch2.seed[0] ^= 1;
    CHECK(evo_conform_check(&ch2, &rs) == 0); /* replayed answers do not count */

    /* many seeds: upstream always passes; each broken fork fails exactly its rule */
    struct {
        evo_core_impl_t im;
        uint32_t bit;
    } bad[] = {
        {{up_hash, bad_fee_tithe, up_post, up_repay, up_spend}, EVO_CHK_FEE},
        {{up_hash, bad_fee_ceil, up_post, up_repay, up_spend}, EVO_CHK_FEE},
        {{up_hash, pay_assure_fee, up_post, bad_repay, up_spend}, EVO_CHK_USURY},
        {{up_hash, pay_assure_fee, bad_post, up_repay, up_spend}, EVO_CHK_LEDGER},
        {{up_hash, pay_assure_fee, up_post, up_repay, bad_spend}, EVO_CHK_CONSENT},
        {{bad_hash, pay_assure_fee, up_post, up_repay, up_spend}, EVO_CHK_HASH},
    };
    bool all = true;
    for (uint32_t s = 0; s < 64; s++) {
        for (uint32_t j = 0; j < 32; j++) ch.seed[j] = (uint8_t) rnd();
        evo_conform_respond(&up, &ch, &rs);
        all = all && evo_conform_check(&ch, &rs) == EVO_CHK_ALL;
    }
    CHECK(all);
    for (uint32_t b = 0; b < sizeof(bad) / sizeof(bad[0]); b++) {
        uint32_t loc = evo_conform_local(&bad[b].im), worst = EVO_CHK_ALL;
        for (uint32_t s = 0; s < 16; s++) {
            for (uint32_t j = 0; j < 32; j++) ch.seed[j] = (uint8_t) rnd();
            evo_conform_respond(&bad[b].im, &ch, &rs);
            worst &= evo_conform_check(&ch, &rs);
        }
        CHECK(worst == (EVO_CHK_ALL & ~bad[b].bit));
        CHECK((loc & ~bad[b].bit) == (EVO_CHK_ALL & ~bad[b].bit));
        if (b != 5) CHECK(!(loc & bad[b].bit)); /* bad_hash slips past the short KATs */
    }

    /* gating: money paths need both sides conformant; everything else stays */
    evo_desc_t chat, pay;
    evo_desc_init(&chat, "chat.message", EVO_KIND_MESSAGE, EVO_CLASS_GENERAL);
    evo_desc_add(&chat, 1, EVO_T_TEXT, EVO_FF_REQUIRED, 0, 0);
    evo_desc_init(&pay, "pay.request", EVO_KIND_MESSAGE, EVO_CLASS_MONEY);
    evo_desc_add(&pay, 1, EVO_T_UINT, EVO_FF_REQUIRED | EVO_FF_MUST_UNDERSTAND, 0, 0);
    evo_reg_init(&g_regA);
    evo_reg_add(&g_regA, &chat, 0);
    evo_reg_add(&g_regA, &pay, 0);
    evo_capset_t ca;
    evo_capset_init(&ca);
    evo_capset_put(&ca, &chat);
    evo_capset_put(&ca, &pay);
    CHECK(evo_negotiate(&g_regA, &ca, &ca, &g_sa) == EVO_OK && g_sa.n == 2);
    CHECK(evo_session_find(&g_sa, "chat.message")->allowed);
    CHECK(!evo_session_find(&g_sa, "pay.request")->allowed); /* until gated */
    for (uint32_t j = 0; j < 32; j++) ch.seed[j] = (uint8_t) rnd();
    evo_conform_respond(&bad[2].im, &ch, &rs); /* the usurious fork */
    uint32_t peer = evo_conform_check(&ch, &rs);
    CHECK(evo_session_gate(&g_sa, evo_conform_local(&up), peer) == 1);
    CHECK(evo_session_find(&g_sa, "chat.message")->allowed);
    CHECK(!evo_session_find(&g_sa, "pay.request")->allowed);
    evo_conform_respond(&up, &ch, &rs);
    CHECK(evo_session_gate(&g_sa, evo_conform_local(&up), evo_conform_check(&ch, &rs)) == 2);
    CHECK(evo_session_find(&g_sa, "pay.request")->allowed);
    evo_cid_t s1, s2;
    evo_conform_suite_cid(&s1);
    evo_conform_suite_cid(&s2);
    CHECK(evo_cid_eq(&s1, &s2) && s1.b[0] == 1);
}

/* ===== T10: device profiles ===== */
static void t10_profiles(void)
{
    printf("T10 device profiles mixing forks\n");
    evo_desc_t chat, chat_plus, ledger;
    evo_desc_init(&chat, "chat.message", EVO_KIND_MESSAGE, EVO_CLASS_GENERAL);
    evo_desc_add(&chat, 1, EVO_T_TEXT, EVO_FF_REQUIRED, 0, 0);
    evo_desc_add(&chat, 2, EVO_T_UINT, 0, 0, 0);
    memcpy(&chat_plus, &chat, sizeof(chat));
    evo_desc_add(&chat_plus, evo_ext_tag("alice.stickers"), EVO_T_CID, 0, 0, 0);
    evo_desc_init(&ledger, "pay.posting", EVO_KIND_MODULE_IFACE, EVO_CLASS_MONEY);
    evo_desc_add(&ledger, 1, EVO_T_BYTES, EVO_FF_REQUIRED, 0, 0);
    evo_registry_t *reg = &g_regA;
    evo_reg_init(reg);
    evo_cid_t c_chat, c_plus, c_ledger;
    evo_reg_add(reg, &chat, &c_chat);
    evo_reg_add(reg, &chat_plus, &c_plus);
    evo_reg_add(reg, &ledger, &c_ledger);
    CHECK(evo_desc_satisfies(&chat_plus, &chat) && !evo_desc_satisfies(&chat, &chat_plus));

    evo_module_t mods[3];
    memset(mods, 0, sizeof(mods));
    fake_cid(&mods[0].code, "alice chat_plus code");
    mods[0].name_len = 9;
    memcpy(mods[0].name, "chat_plus", 9);
    mods[0].n_prov = 1;
    mods[0].prov[0] = c_plus;
    fake_cid(&mods[1].code, "bob bot code");
    mods[1].name_len = 3;
    memcpy(mods[1].name, "bot", 3);
    mods[1].n_req = 1;
    mods[1].req[0] = c_chat; /* written against the upstream chat */
    fake_cid(&mods[2].code, "bob shop code");
    mods[2].name_len = 4;
    memcpy(mods[2].name, "shop", 4);
    mods[2].n_req = 1;
    mods[2].req[0] = c_ledger;
    evo_cid_t m0, m1, m2;
    evo_module_cid(&mods[0], &m0);
    evo_module_cid(&mods[1], &m1);
    evo_module_cid(&mods[2], &m2);
    uint8_t mb[EVO_MOD_ENC_MAX];
    evo_module_t mm;
    uint32_t n = evo_module_encode(&mods[1], mb, sizeof(mb));
    CHECK(n && evo_module_decode(mb, n, &mm) == EVO_OK && memcmp(&mm, &mods[1], sizeof(mm)) == 0);

    /* lineages that carry the modules */
    evo_dag_init(&g_dag);
    evo_fork_t f;
    evo_cid_t la, lb;
    uint32_t idx;
    evo_fork_init(&f, g_alice.id, "alice", 1);
    evo_fork_change(&f, EVO_CH_ADD, "chat", &m0, 0);
    CHECK(add(&g_alice, &f, &la, &idx) == EVO_OK);
    evo_fork_init(&f, g_bob.id, "bob", 1);
    evo_fork_change(&f, EVO_CH_ADD, "bot", &m1, 0);
    evo_fork_change(&f, EVO_CH_ADD, "shop", &m2, 0);
    CHECK(add(&g_bob, &f, &lb, &idx) == EVO_OK);

    evo_profile_t p, q, r;
    uint8_t phone[32];
    memset(phone, 0x77, sizeof(phone));
    evo_profile_init(&p, "my laptop");
    CHECK(evo_profile_put(&p, &m1, &lb, EVO_PLACE_LOCAL, 0) == EVO_OK);
    CHECK(evo_profile_put(&p, &m0, &la, EVO_PLACE_PEER, phone) == EVO_OK);
    uint32_t bad = 99;
    CHECK(evo_profile_check(&p, mods, 3, reg, &g_dag, &bad) == EVO_OK);
    /* the same profile in another order has the same CID, and round-trips */
    evo_profile_init(&q, "my laptop");
    evo_profile_put(&q, &m0, &la, EVO_PLACE_PEER, phone);
    evo_profile_put(&q, &m1, &lb, EVO_PLACE_LOCAL, 0);
    evo_cid_t cp, cq;
    CHECK(evo_profile_cid(&p, &cp) == EVO_OK && evo_profile_cid(&q, &cq) == EVO_OK &&
          evo_cid_eq(&cp, &cq));
    uint8_t pb[EVO_PROFILE_ENC_MAX];
    n = evo_profile_encode(&p, pb, sizeof(pb));
    CHECK(n && evo_profile_decode(pb, n, &r) == EVO_OK && memcmp(&r, &p, sizeof(r)) == 0);
    /* turn the provider off: the bot is unsatisfied */
    evo_profile_put(&q, &m0, &la, EVO_PLACE_OFF, 0);
    CHECK(evo_profile_check(&q, mods, 3, reg, &g_dag, &bad) == EVO_ERR_UNSATISFIED);
    /* a shop that needs a ledger nobody provides */
    evo_profile_put(&p, &m2, &lb, EVO_PLACE_LOCAL, 0);
    CHECK(evo_profile_check(&p, mods, 3, reg, &g_dag, &bad) == EVO_ERR_UNSATISFIED);
    /* claiming a module came from a build that does not contain it */
    evo_profile_init(&q, "liar");
    evo_profile_put(&q, &m0, &lb, EVO_PLACE_LOCAL, 0);
    CHECK(evo_profile_check(&q, mods, 3, reg, &g_dag, &bad) == EVO_ERR_UNTRUSTED);
    CHECK(evo_profile_check(&q, mods, 3, reg, 0, &bad) == EVO_OK); /* without provenance */
}

int main(void)
{
    t1_cid_desc();
    t2_random_pairs();
    t3_relay();
    t4_strict();
    t5_lineage();
    t6_random_dag();
    t7_trust();
    t8_adoption();
    t9_conformance();
    t10_profiles();
    printf("test_evolve: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
