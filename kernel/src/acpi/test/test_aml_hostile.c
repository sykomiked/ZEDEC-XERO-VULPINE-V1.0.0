/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_aml_hostile.c — byte code that is broken or malicious: truncated and
 * oversized lengths, endless loops, unbounded recursion, giant objects,
 * arena exhaustion, plus a deterministic mutation fuzz over every fixture.
 * Every case must end in an error code (never a crash, hang or sanitizer
 * report). Hand-assembled AML is used where iasl refuses to emit it. */
#include "amltest.h"

static amlfake_t f;
static aml_ctx_t c;
static uint8_t tbl[1 << 16];

/* PkgLength encoding of n (the length counts its own bytes). */
static uint32_t pkglen(uint8_t *o, uint32_t body)
{
    uint32_t n = body + 1;
    if (n < 0x40) {
        o[0] = (uint8_t) n;
        return 1;
    }
    n = body + 2;
    if (n < 0x1000) {
        o[0] = (uint8_t) (0x40 | (n & 0xF));
        o[1] = (uint8_t) (n >> 4);
        return 2;
    }
    n = body + 3;
    o[0] = (uint8_t) (0x80 | (n & 0xF));
    o[1] = (uint8_t) (n >> 4);
    o[2] = (uint8_t) (n >> 12);
    return 3;
}

/* Method (name, flags) { body } */
static uint32_t method(uint8_t *o, const char *name, uint8_t flags, const uint8_t *body,
                       uint32_t blen)
{
    uint32_t k = 0;
    o[k++] = 0x14;
    k += pkglen(o + k, 5 + blen);
    memcpy(o + k, name, 4);
    k += 4;
    o[k++] = flags;
    memcpy(o + k, body, blen);
    return k + blen;
}

static int load_body(const uint8_t *body, uint32_t blen)
{
    uint32_t n = fk_table(tbl, sizeof tbl, "DSDT", 2, body, blen);
    if (setup(&f, &c, 0, 0)) return -100;
    return aml_load_table(&c, tbl, n);
}

static void test_limits(void)
{
    static uint8_t b[0x8000];
    uint32_t k = 0;
    uint64_t v;
    static const uint8_t spin[] = {0xA2, 0x02, 0x01};   /* While (One) {} */
    static const uint8_t recu[] = {'R', 'E', 'C', 'U'}; /* RECU () */
    static const uint8_t hbuf[] = {0xA4, 0x11, 0x06, 0x0C, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t hpkg[] = {0xA4, 0x13, 0x06, 0x0C, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t slpx[] = {0xA2, 0x07, 0x01, 0x5B, 0x22, 0x0B, 0xE8, 0x03};
    static const uint8_t relm[] = {0x5B, 0x27, 'M', 'T', 'X', '0'};
    /* Local0 = "ab"; While (One) { Local0 = Concatenate (Local0, Local0) } */
    static const uint8_t grow[] = {0x70, 0x0D, 'a',  'b',  0x00, 0x60, 0xA2,
                                   0x07, 0x01, 0x73, 0x60, 0x60, 0x60, 0x00};
    /* Return (Index (Package (1) {}, 0x1000)) */
    static const uint8_t oobi[] = {0xA4, 0x88, 0x12, 0x02, 0x01, 0x0B, 0x00, 0x10, 0x00};
    /* Return (DerefOf ("\\NOPE")) */
    static const uint8_t drfx[] = {0xA4, 0x83, 0x0D, '\\', 'N', 'O', 'P', 'E', 0x00};
    /* Return (Arg6) for a method declared with no arguments */
    static const uint8_t arg6[] = {0xA4, 0x6E};
    /* Divide (One, Zero, Local0) */
    static const uint8_t div0[] = {0x78, 0x01, 0x00, 0x00, 0x60};
    /* Fatal (1, 2, 3) */
    static const uint8_t ftl[] = {0x5B, 0x32, 0x01, 0x0C, 0x02, 0, 0, 0, 0x0A, 0x03};
    /* Store (Local0, Local0) with Local0 uninitialised */
    static const uint8_t unin[] = {0xA4, 0x60};

    b[k++] = 0x5B; /* Mutex (MTX0, 0) */
    b[k++] = 0x01;
    memcpy(b + k, "MTX0", 4);
    k += 4;
    b[k++] = 0;
    k += method(b + k, "SPIN", 0, spin, sizeof spin);
    k += method(b + k, "RECU", 0, recu, sizeof recu);
    k += method(b + k, "HBUF", 0, hbuf, sizeof hbuf);
    k += method(b + k, "HPKG", 0, hpkg, sizeof hpkg);
    k += method(b + k, "SLPX", 0, slpx, sizeof slpx);
    k += method(b + k, "RELM", 0, relm, sizeof relm);
    k += method(b + k, "GROW", 0, grow, sizeof grow);
    k += method(b + k, "OOBI", 0, oobi, sizeof oobi);
    k += method(b + k, "DRFX", 0, drfx, sizeof drfx);
    k += method(b + k, "ARG6", 0, arg6, sizeof arg6);
    k += method(b + k, "DIV0", 0, div0, sizeof div0);
    k += method(b + k, "FTL0", 0, ftl, sizeof ftl);
    k += method(b + k, "UNIN", 0, unin, sizeof unin);

    /* Return (Add (Add (... (One, One) ...))) nested 400 deep */
    {
        static uint8_t deep[2000];
        uint32_t d = 0;
        deep[d++] = 0xA4;
        for (int i = 0; i < 400; i++) deep[d++] = 0x72;
        deep[d++] = 0x01;
        deep[d++] = 0x01;
        deep[d++] = 0x00;
        for (int i = 1; i < 400; i++) {
            deep[d++] = 0x01;
            deep[d++] = 0x00;
        }
        k += method(b + k, "DEEP", 0, deep, d);
    }
    /* Package (1) { Package (1) { ... } } nested 300 deep */
    {
        static uint8_t pk[4000];
        uint32_t d = 0, lens[300];
        /* build inside-out: innermost is Package (1) { One } */
        uint8_t inner[8] = {0x12, 0x03, 0x01, 0x01};
        uint32_t il = 4;
        memcpy(pk + 2000, inner, il);
        uint32_t start = 2000, len = il;
        for (int i = 0; i < 300; i++) {
            uint8_t h[6];
            uint32_t hl = 0;
            h[hl++] = 0x12;
            hl += pkglen(h + hl, 1 + len);
            h[hl++] = 0x01;
            start -= hl;
            memcpy(pk + start, h, hl);
            len += hl;
            lens[i] = len;
        }
        (void) lens;
        static uint8_t body[4000];
        body[d++] = 0xA4;
        memcpy(body + d, pk + start, len);
        d += len;
        k += method(b + k, "PKGD", 0, body, d);
    }

    CHECK_EQ(load_body(b, k), 0);
    uint64_t t0 = f.now_us;
    CHECK_EQ(ev_int(&c, "SPIN", &v), AML_ELIMIT);
    CHECK_EQ(ev_int(&c, "RECU", &v), AML_ELIMIT);
    CHECK_EQ(c.depth, 0);
    CHECK_EQ(ev_int(&c, "HBUF", &v), AML_ENOMEM);
    CHECK_EQ(ev_int(&c, "HPKG", &v), AML_ENOMEM);
    CHECK_EQ(ev_int(&c, "SLPX", &v), AML_ELIMIT);
    CHECK(f.now_us - t0 <= AML_MAX_SLEEP_US);
    CHECK_EQ(ev_int(&c, "RELM", &v), AML_EMUTEX);
    CHECK_EQ(ev_int(&c, "GROW", &v), AML_ENOMEM);
    CHECK_EQ(ev_int(&c, "OOBI", &v), AML_EBOUNDS);
    CHECK_EQ(ev_int(&c, "DRFX", &v), AML_ENOENT);
    CHECK(ev_int(&c, "ARG6", &v) != 0);
    CHECK_EQ(ev_int(&c, "DIV0", &v), AML_EDIV0);
    CHECK_EQ(ev_int(&c, "FTL0", &v), AML_EFATAL);
    CHECK(ev_int(&c, "UNIN", &v) != 0);
    CHECK_EQ(ev_int(&c, "DEEP", &v), AML_ELIMIT);
    aml_val_t pv;
    CHECK(aml_eval(&c, 0, "PKGD", 0, 0, &pv) != 0);
    aml_val_free(&c, &pv);

    /* after all that the context still works and the heap went back */
    uint32_t used = aml_stats(&c).heap_used;
    for (int i = 0; i < 3; i++) {
        ev_int(&c, "GROW", &v);
        ev_int(&c, "HBUF", &v);
        ev_int(&c, "RECU", &v);
        ev_int(&c, "DEEP", &v);
    }
    CHECK_EQ(aml_stats(&c).heap_used, used);
    CHECK_EQ(c.depth, 0);
    CHECK_EQ(c.nest, 0);

    /* a smaller op budget set by the caller */
    c.op_limit = 1000;
    CHECK_EQ(ev_int(&c, "SPIN", &v), AML_ELIMIT);
    CHECK(c.ops <= 1001);
    c.op_limit = 0;
}

static void test_tables(void)
{
    static uint8_t b[256];
    uint64_t v;
    uint32_t k;

    /* header problems */
    CHECK_EQ(setup(&f, &c, 0, 0), 0);
    CHECK_EQ(aml_load_table(&c, fx_ops, 20), AML_EARG);
    CHECK_EQ(aml_load_table(&c, 0, 100), AML_EARG);
    memcpy(tbl, fx_ops, 64);
    tbl[4] = 30; /* length below a header */
    tbl[5] = tbl[6] = tbl[7] = 0;
    CHECK_EQ(aml_load_table(&c, tbl, 64), AML_EPARSE);
    tbl[4] = 0xFF; /* longer than the buffer we have */
    CHECK_EQ(aml_load_table(&c, tbl, 64), AML_EPARSE);
    CHECK(aml_init(&c, &f.plat, g_arena, 1024) != 0);
    CHECK(aml_init(0, &f.plat, g_arena, ARENA_SIZE) != 0);

    /* a method used at the top level before its definition (decl pass):
     * Name (FWDV, 0)  Store (FWDM (), FWDV)  Method (FWDM) { Return (0x42) } */
    k = 0;
    static const uint8_t pre[] = {0x08, 'F', 'W', 'D', 'V', 0x00, 0x70, 'F',
                                  'W',  'D', 'M', 'F', 'W', 'D',  'V'};
    memcpy(b, pre, sizeof pre);
    k = sizeof pre;
    static const uint8_t fm[] = {0xA4, 0x0A, 0x42};
    k += method(b + k, "FWDM", 0, fm, sizeof fm);
    CHECK_EQ(load_body(b, k), 0);
    CHECK_EQ(ev_int(&c, "FWDV", &v), 0);
    CHECK_EQ(v, 0x42);

    /* a method whose PkgLength runs past the table: the good name before it
     * stays, the load reports the error */
    static const uint8_t trunc[] = {0x08, 'G',  'O',  'O', 'D', 0x0A, 0x07, 0x14, 0xCF,
                                    0xFF, 0xFF, 0xFF, 'B', 'A', 'D',  '_',  0x00};
    CHECK(load_body(trunc, sizeof trunc) != 0);
    CHECK_EQ(ev_int(&c, "GOOD", &v), 0);
    CHECK_EQ(v, 7);
    CHECK(aml_lookup(&c, 0, "BAD_") == 0);
    CHECK(aml_stats(&c).errors >= 1);

    /* a name with a character that is not allowed, then a good one */
    static const uint8_t badn[] = {0x08, 'B', 'a', 'd', '!', 0x00, 0x08, 'O', 'K', 'A', 'Y', 0x01};
    CHECK(load_body(badn, sizeof badn) != 0);
    CHECK(aml_lookup(&c, 0, "Bad!") == 0);

    /* unknown opcodes at the top level are stepped over, the rest loads */
    static const uint8_t unk[] = {0x5B, 0x99, 0x02, 0x08, 'A', 'F', 'T', 'R', 0x0A, 0x09};
    CHECK(load_body(unk, sizeof unk) != 0);
    CHECK_EQ(ev_int(&c, "AFTR", &v), 0);
    CHECK_EQ(v, 9);

    /* a table of nothing but unknown opcodes gives up after the cap */
    static uint8_t junk[4096];
    memset(junk, 0x02, sizeof junk);
    CHECK(load_body(junk, sizeof junk) != 0);
    CHECK(aml_stats(&c).errors <= AML_MAX_LOAD_SKIPS + 2);

    /* the same name twice with different types */
    static const uint8_t dup[] = {0x08, 'D',  'U', 'P', '0', 0x01, 0x5B,
                                  0x82, 0x06, 'D', 'U', 'P', '0'};
    CHECK(load_body(dup, sizeof dup) != 0);
    CHECK_EQ(aml_node_type(aml_lookup(&c, 0, "DUP0")), AML_T_INT);

    /* While (One) at the top level of a table */
    static const uint8_t tspin[] = {0xA2, 0x02, 0x01, 0x08, 'L', 'A', 'T', 'E', 0x01};
    CHECK(load_body(tspin, sizeof tspin) != 0);

    /* Load () of a buffer that is not a table (all zeros, then a header
     * whose length points past the buffer): refused, Load reports failure */
    static uint8_t ld[64];
    k = 0;
    memcpy(ld, (const uint8_t[]){0x08, 'S', 'L', 'D', 'B', 0x11, 0x03, 0x0A, 0x24}, 9);
    k = 9;
    static const uint8_t ldm[] = {0x5B, 0x20, 'S', 'L', 'D', 'B', 0x60, 0xA4, 0x60};
    k += method(ld + k, "LDIT", 0, ldm, sizeof ldm);
    CHECK_EQ(load_body(ld, k), 0);
    CHECK_EQ(ev_int(&c, "LDIT", &v), 0);
    CHECK_EQ(v, 0);
    CHECK_EQ(aml_stats(&c).tables, 1);
}

/* Arena too small for the whole fixture: errors, no crash, still usable. */
static void test_arena(void)
{
    static uint8_t small[0x18000];
    uint64_t v;
    fk_init(&f);
    CHECK_EQ(aml_init(&c, &f.plat, small, sizeof small), 0);
    int r = aml_load_table(&c, fx_ops, sizeof fx_ops);
    CHECK(r == 0 || r == AML_ENOMEM);
    for (unsigned i = 1; i < 150; i++) {
        char p[16];
        snprintf(p, sizeof p, "T%03u", i);
        ev_int(&c, p, &v);
    }
    CHECK(aml_stats(&c).heap_used <= sizeof small);
    CHECK(aml_stats(&c).heap_peak <= sizeof small);
}

/* ---- deterministic mutation fuzz -------------------------------------------- */
static uint32_t rng = 0x2545F491u;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

typedef struct mlist {
    aml_node_t *m[512];
    uint32_t n;
} mlist_t;

static int collect(aml_ctx_t *cc, aml_node_t *n, uint32_t depth, void *user)
{
    mlist_t *l = (mlist_t *) user;
    (void) cc;
    (void) depth;
    if (aml_node_type(n) == AML_T_METHOD && n->x.m.code && l->n < 512) l->m[l->n++] = n;
    return 0;
}

static uint8_t memw[0x1000], ssdw[0x400];

static void fuzz_one(const uint8_t *src, uint32_t len, uint32_t iters)
{
    static uint8_t buf[8192];
    static mlist_t ml;
    for (uint32_t it = 0; it < iters; it++) {
        if (len > sizeof buf) return;
        memcpy(buf, src, len);
        uint32_t n = len;
        uint32_t muts = 1 + rnd() % 6;
        for (uint32_t m = 0; m < muts; m++) {
            uint32_t at = 36 + rnd() % (n - 36);
            switch (rnd() % 6) {
            case 0:
                buf[at] = (uint8_t) rnd();
                break;
            case 1:
                buf[at] ^= (uint8_t) (1u << (rnd() % 8));
                break;
            case 2:
                buf[at] = (uint8_t) (buf[at] + 1);
                break;
            case 3:
                buf[at] = 0xFF;
                break;
            case 4:
                buf[at] = 0x00;
                break;
            default: /* truncate */
                n = at > 37 ? at : 37;
                buf[4] = (uint8_t) n;
                buf[5] = (uint8_t) (n >> 8);
                buf[6] = buf[7] = 0;
                break;
            }
        }
        fk_init(&f);
        fk_window(&f, 0xFED40000ull, memw, sizeof memw);
        fk_window(&f, 0xFED41000ull, ssdw, sizeof ssdw);
        if (aml_init(&c, &f.plat, g_arena, ARENA_SIZE)) continue;
        c.op_limit = 20000;
        aml_load_table(&c, buf, n);
        aml_init_devices(&c);
        ml.n = 0;
        aml_walk(&c, 0, collect, &ml);
        for (uint32_t i = 0; i < ml.n; i++) {
            aml_val_t a[7], ret;
            for (int j = 0; j < 7; j++) a[j] = aml_int(rnd() % 4);
            uint32_t argc = ml.m[i]->x.m.flags & 7;
            if (!aml_eval_node(&c, ml.m[i], a, argc, &ret)) aml_val_free(&c, &ret);
            CHECK(c.depth == 0 && c.nest == 0);
            if (c.depth || c.nest) return;
        }
        aml_notify_t nt;
        while (aml_notify_pop(&c, &nt)) {
        }
    }
}

static void test_fuzz(void)
{
    uint32_t iters = 300;
    const char *e = getenv("AML_FUZZ_ITERS");
    if (e) iters = (uint32_t) strtoul(e, 0, 0);
    e = getenv("AML_FUZZ_SEED");
    if (e && strtoul(e, 0, 0)) rng = (uint32_t) strtoul(e, 0, 0);
    memcpy(ssdw, fx_ssdt_dyn, sizeof fx_ssdt_dyn);
    fuzz_one(fx_ops, sizeof fx_ops, iters);
    fuzz_one(fx_regions, sizeof fx_regions, iters);
    fuzz_one(fx_pm, sizeof fx_pm, iters);
    fuzz_one(fx_rev1, sizeof fx_rev1, iters / 4);
    fuzz_one(fx_ssdt_dyn, sizeof fx_ssdt_dyn, iters / 4);
    CHECK(1);
}

int main(void)
{
    test_limits();
    test_tables();
    test_arena();
    test_fuzz();
    return finish_tests("test_aml_hostile");
}
