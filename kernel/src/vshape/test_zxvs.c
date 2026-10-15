/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxvs.c — ZXVS descriptors (zxvs.c) and their drawing (zxvs_draw.c).
 *
 *   cc -std=c11 -Wall -Wextra -Werror -DTEST_HOST -Iinclude -Isrc/modbind \
 *      -Isrc/zui -Isrc/font -Isrc/vshape src/vshape/test_zxvs.c src/vshape/zxvs.c \
 *      src/vshape/zxvs_draw.c src/zui/zui_vec.c src/zui/zui_canvas.c \
 *      src/font/truetype.c src/robin_debanks/sha256.c -o /tmp/t && /tmp/t
 *
 * Fixtures: tools/vshape/gen_zxvs_fixtures.py (real repository files and
 * small made-up ones, with their CIDs and, for three series, the normalised
 * samples computed in Python). Checks:
 *   - every fixture: kind, format, CID, zxvs_check, histogram sum;
 *   - curves measured against the independent samples: |curve - sample| <=
 *     the bound at every sample, and the recorded max / RMS error are exactly
 *     what the test recomputes; segment counts within measured thresholds;
 *     a looser bound gives fewer segments;
 *   - structure: node counts, folding of deep nesting, labels;
 *   - media: REF with the caller's CID; similarity sanity; Grid Chain words
 *     and the registration callback;
 *   - determinism; error paths; fuzz: describe random and mutated inputs of
 *     every small length, every truncation and random flips of every
 *     descriptor through zxvs_check (exact-size buffers);
 *   - drawing: pixels drawn, identical twice, at two sizes, nothing for a
 *     malformed descriptor.
 * One [PASS]/[FAIL] line per section; exit 0 when all pass. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxvs.h"
#include "zxvs_draw.h"

#include "test/zxvs_fixtures.inc"

static int g_pass, g_fail, s_bad;
static const char *s_name;
#define SECTION(n) (s_name = (n), s_bad = 0)
#define OK(c, ...)                                                                                 \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            if (s_bad++ < 6) {                                                                     \
                printf("    fail %s:%d: ", s_name, __LINE__);                                      \
                printf(__VA_ARGS__);                                                               \
                printf("\n");                                                                      \
            }                                                                                      \
        }                                                                                          \
    } while (0)
static void end_section(const char *det)
{
    printf("[%s] %s%s%s\n", s_bad ? "FAIL" : "PASS", s_name, det ? " " : "", det ? det : "");
    if (s_bad)
        g_fail++;
    else
        g_pass++;
}

static uint32_t rs = 0x2468ace1u;
static uint32_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}

typedef struct {
    int64_t n;
    uint8_t *d; /* exact size */
    zxvs_report_t r;
} desc_t;

static desc_t describe(const uint8_t *d, uint32_t n, const zxvs_cfg_t *cfg)
{
    desc_t o;
    memset(&o, 0, sizeof o);
    uint64_t ws = zxvs_work_size(n, cfg), ob = zxvs_out_bound(n, cfg);
    void *w = malloc((size_t) ws);
    uint8_t *out = malloc((size_t) ob);
    /* exact-size copy of the input so ASan sees any overread */
    uint8_t *in = malloc(n ? n : 1);
    memcpy(in, d, n);
    o.n = zxvs_describe(n ? in : 0, n, w, ws, out, ob, cfg, &o.r);
    if (o.n > 0) {
        o.d = malloc((size_t) o.n);
        memcpy(o.d, out, (size_t) o.n);
    }
    free(w);
    free(out);
    free(in);
    return o;
}

static const zfx_t *fx(const char *name)
{
    for (size_t i = 0; i < sizeof ZFX / sizeof ZFX[0]; i++)
        if (!strcmp(ZFX[i].name, name)) return &ZFX[i];
    return 0;
}

static void t_fixtures(void)
{
    SECTION("fixtures: kind, format, CID, check, histogram");
    char det[512];
    size_t dl = 0;
    det[0] = 0;
    for (size_t i = 0; i < sizeof ZFX / sizeof ZFX[0]; i++) {
        const zfx_t *f = &ZFX[i];
        desc_t o = describe(f->d, f->n, 0);
        OK(o.n > 0, "%s rc %lld", f->name, (long long) o.n);
        if (o.n <= 0) continue;
        zxvs_info_t in;
        OK(zxvs_check(o.d, (uint32_t) o.n, &in) == 0, "%s check", f->name);
        OK(in.kind == f->kind && in.fmt == f->fmt, "%s kind %u fmt %u (want %u %u)", f->name,
           in.kind, in.fmt, f->kind, f->fmt);
        OK(memcmp(in.cid, f->cid, ZXVS_CID_LEN) == 0, "%s cid", f->name);
        OK(in.src_bytes == f->n, "%s size", f->name);
        const uint8_t *b;
        uint32_t len;
        OK(zxvs_section(o.d, (uint32_t) o.n, ZXVS_HIST, 0, &b, &len) == 0, "%s hist", f->name);
        uint32_t sv = 0;
        for (int k = 0; k < ZXVS_DIMS; k++) sv |= in.shape[k] > 65536;
        OK(!sv, "%s shape range", f->name);
        int w = snprintf(det + dl, sizeof det - dl, "%s%s %u->%lld", i ? ", " : "\n    ", f->name,
                         f->n, (long long) o.n);
        if (w > 0 && (size_t) w < sizeof det - dl) dl += (size_t) w;
        free(o.d);
    }
    end_section(det);
}

/* measure a curve of descriptor d against independent samples */
static int measure_curve(const uint8_t *d, uint32_t n, uint32_t which, const int32_t *yn,
                         uint32_t ny, uint32_t *maxe, uint32_t *rmse, zxvs_curve_t *cv)
{
    if (zxvs_curve(d, n, which, cv)) return -1;
    if (cv->npts != ny) return -2;
    uint64_t sq_hi = 0, sq_lo = 0, mx = 0;
    for (uint32_t x = 0; x < ny; x++) {
        int64_t v;
        if (zxvs_curve_eval(cv, x, &v)) return -3;
        uint64_t e = (uint64_t) (v > yn[x] ? v - yn[x] : yn[x] - v);
        if (e > mx) mx = e;
        uint64_t sq = e * e, lo = sq_lo + sq;
        sq_hi += lo < sq_lo;
        sq_lo = lo;
    }
    /* mean (the sums here fit in 64 bits for the fixtures) and its floor sqrt */
    uint64_t mean = sq_hi ? UINT64_MAX : sq_lo / ny, r = 0;
    while ((r + 1) * (r + 1) <= mean) r++;
    *maxe = (uint32_t) mx;
    *rmse = (uint32_t) (r > mx ? mx : r);
    return 0;
}

static void t_curves(void)
{
    SECTION("curves vs independent samples (bound, max, RMS, segments)");
    char det[512];
    size_t dl = 0;
    det[0] = 0;
    /* measured segment counts (default bound 64/65536 of the range), + ~20 % */
    static const struct {
        const char *name;
        uint32_t max_seg;
    } LIM[] = {{"energy_tsv", 14}, {"sine_csv", 34}, {"sine_csv_noise", 1999}};
    for (size_t i = 0; i < sizeof ZSN / sizeof ZSN[0]; i++) {
        const zsn_t *s = &ZSN[i];
        const zfx_t *f = fx(s->fixture);
        desc_t o = describe(f->d, f->n, 0);
        OK(o.n > 0, "%s", s->name);
        if (o.n <= 0) continue;
        /* find the curve of this column */
        uint32_t which = 99;
        zxvs_info_t in;
        zxvs_check(o.d, (uint32_t) o.n, &in);
        for (uint32_t c = 0; c < in.ncurves; c++) {
            zxvs_curve_t cv;
            if (!zxvs_curve(o.d, (uint32_t) o.n, c, &cv) && cv.column == s->column) which = c;
        }
        OK(which != 99, "%s: no curve for column %u", s->name, s->column);
        uint32_t me = 0, re = 0;
        zxvs_curve_t cv;
        int rc = which != 99 ? measure_curve(o.d, (uint32_t) o.n, which, s->yn, s->n, &me, &re, &cv)
                             : -9;
        OK(rc == 0, "%s measure rc %d", s->name, rc);
        if (!rc) {
            OK(me <= cv.tol, "%s max %u > bound %u", s->name, me, cv.tol);
            OK(me == cv.maxerr && re == cv.rmserr, "%s recorded %u/%u, measured %u/%u", s->name,
               cv.maxerr, cv.rmserr, me, re);
            uint32_t lim = 0;
            for (size_t k = 0; k < sizeof LIM / sizeof LIM[0]; k++)
                if (!strcmp(LIM[k].name, s->name)) lim = LIM[k].max_seg;
            OK(cv.nseg <= lim, "%s %u segments > %u", s->name, cv.nseg, lim);
            int w = snprintf(det + dl, sizeof det - dl,
                             "%s%s %u pts %u seg max %.4f%% rms %.4f%% of range",
                             i ? "; " : "\n    ", s->name, cv.npts, cv.nseg,
                             me * 100.0 / 16777216.0, re * 100.0 / 16777216.0);
            if (w > 0 && (size_t) w < sizeof det - dl) dl += (size_t) w;
        }
        free(o.d);
    }
    /* a looser bound gives no more segments, and still holds */
    const zfx_t *f = fx("sine_csv");
    uint32_t prev = 0xFFFFFFFFu;
    static const uint32_t tols[] = {16, 64, 655, 6554};
    for (int t = 0; t < 4; t++) {
        zxvs_cfg_t cfg;
        memset(&cfg, 0, sizeof cfg);
        cfg.tol_q16 = tols[t];
        desc_t o = describe(f->d, f->n, &cfg);
        uint32_t me, re;
        zxvs_curve_t cv;
        /* curve 1 is column 1 ("value"): curve 0 is the "t" column */
        OK(o.n > 0 &&
               measure_curve(o.d, (uint32_t) o.n, 1, ZSN[1].yn, ZSN[1].n, &me, &re, &cv) == 0 &&
               cv.column == 1,
           "tol %u", tols[t]);
        if (o.n > 0) {
            OK(me <= tols[t] * 256u && cv.tol == tols[t] * 256u, "tol %u: max %u", tols[t], me);
            OK(cv.nseg <= prev, "tol %u: %u segments, tighter had %u", tols[t], cv.nseg, prev);
            prev = cv.nseg;
            int w = snprintf(det + dl, sizeof det - dl,
                             "%ssine at bound %.3f%%: %u segments, max %.4f%%", t ? ", " : "\n    ",
                             tols[t] * 100.0 / 65536, cv.nseg, me * 100.0 / 16777216.0);
            if (w > 0 && (size_t) w < sizeof det - dl) dl += (size_t) w;
        }
        free(o.d);
    }
    /* the entropy curve obeys the same rule */
    desc_t o = describe(fx("font_head_bin")->d, fx("font_head_bin")->n, 0);
    zxvs_curve_t ec;
    OK(o.n > 0 && zxvs_curve(o.d, (uint32_t) o.n, ZXVS_CURVE_ENTROPY, &ec) == 0 &&
           ec.maxerr <= ec.tol && ec.vrange == 2048,
       "entropy curve");
    free(o.d);
    end_section(det);
}

static void t_struct(void)
{
    SECTION("structure graphs and layout");
    static const struct {
        const char *name;
        uint32_t nodes, total;
    } W[] = {{"zenodo_json", 0, 0}, {"deep_json", 102, 102},    {"catalog_xml", 0, 0},
             {"page_html", 0, 0},   {"settings_zxcfg", 12, 12}, {"stft_math_c", 0, 0},
             {"energy_tsv", 12, 12}};
    char det[512];
    size_t dl = 0;
    det[0] = 0;
    for (size_t i = 0; i < sizeof W / sizeof W[0]; i++) {
        const zfx_t *f = fx(W[i].name);
        desc_t o = describe(f->d, f->n, 0);
        zxvs_info_t in;
        OK(o.n > 0 && zxvs_check(o.d, (uint32_t) o.n, &in) == 0, "%s", W[i].name);
        if (o.n <= 0) continue;
        OK(in.nnodes > 1 && in.nnodes <= in.total_nodes, "%s nodes %u/%u", W[i].name, in.nnodes,
           in.total_nodes);
        if (W[i].nodes)
            OK(in.nnodes == W[i].nodes && in.total_nodes == W[i].total, "%s %u/%u want %u/%u",
               W[i].name, in.nnodes, in.total_nodes, W[i].nodes, W[i].total);
        int w = snprintf(det + dl, sizeof det - dl, "%s%s %u/%u", i ? ", " : "\n    ", W[i].name,
                         in.nnodes, in.total_nodes);
        if (w > 0 && (size_t) w < sizeof det - dl) dl += (size_t) w;
        free(o.d);
    }
    /* deep nesting folds into level 64: no node deeper than 65 */
    {
        const zfx_t *f = fx("deep_json");
        desc_t o = describe(f->d, f->n, 0);
        const uint8_t *b = 0;
        uint32_t len = 0, maxy = 0;
        OK(o.n > 0 && !zxvs_section(o.d, (uint32_t) o.n, ZXVS_STRUCT, 0, &b, &len), "deep struct");
        if (!b) b = (const uint8_t *) "", len = 0;
        /* walk the nodes: uv up, u8 type, uv nchild, uv x, uv y, u8 n, label */
        if (len > 2) {
            const uint8_t *p = b, *e = b + len;
            uint64_t v[2] = {0, 0};
            p++;
            for (int k = 0; k < 2; k++) {
                uint64_t x = 0;
                int sh = 0;
                while (p < e && (*p & 0x80)) x |= (uint64_t) (*p++ & 0x7F) << sh, sh += 7;
                x |= (uint64_t) (*p++ & 0x7F) << sh;
                v[k] = x;
            }
            for (uint64_t i = 0; i < v[1] && p < e; i++) {
                uint64_t f5[5];
                for (int k = 0; k < 5; k++) {
                    if (k == 1) {
                        f5[k] = *p++;
                        continue;
                    }
                    uint64_t x = 0;
                    int sh = 0;
                    while (p < e && (*p & 0x80)) x |= (uint64_t) (*p++ & 0x7F) << sh, sh += 7;
                    x |= (uint64_t) (*p++ & 0x7F) << sh;
                    f5[k] = x;
                }
                if (f5[4] > maxy) maxy = (uint32_t) f5[4];
                p += 1 + *p;
            }
        }
        OK(maxy == ZXVS_MAX_DEPTH + 1, "deep max depth %u", maxy);
        free(o.d);
    }
    /* a node cap: kept <= cap, total unchanged */
    zxvs_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.max_nodes = 5;
    const zfx_t *f = fx("zenodo_json");
    desc_t a = describe(f->d, f->n, 0), b = describe(f->d, f->n, &cfg);
    zxvs_info_t ia, ib;
    OK(a.n > 0 && b.n > 0 && !zxvs_check(a.d, (uint32_t) a.n, &ia) &&
           !zxvs_check(b.d, (uint32_t) b.n, &ib) && ib.nnodes == 5 &&
           ib.total_nodes == ia.total_nodes,
       "cap");
    free(a.d);
    free(b.d);
    /* the zxcfg tree has the dotted sections and a label "accent" */
    f = fx("settings_zxcfg");
    a = describe(f->d, f->n, 0);
    const uint8_t *s;
    uint32_t len;
    OK(a.n > 0 && !zxvs_section(a.d, (uint32_t) a.n, ZXVS_STRUCT, 0, &s, &len), "zxcfg struct");
    bool found = false;
    for (uint32_t k = 0; a.n > 0 && k + 6 <= len; k++)
        if (!memcmp(s + k, "accent", 6)) found = true;
    OK(found, "label accent");
    free(a.d);
    end_section(det);
}

static void t_media_sim(void)
{
    SECTION("media references, similarity, Grid Chain");
    uint8_t vcid[ZXVS_CID_LEN] = {1, 0x55, 0x12, 0x20, 9, 9, 9};
    zxvs_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.ref_cid = vcid;
    cfg.ref_kind = 2;
    cfg.ref_fid_known = true;
    cfg.ref_fid_cdb = 3593;
    const zfx_t *f = fx("tiny_wav");
    desc_t o = describe(f->d, f->n, &cfg);
    const uint8_t *b;
    uint32_t len;
    OK(o.n > 0 && !zxvs_section(o.d, (uint32_t) o.n, ZXVS_REF, 0, &b, &len) &&
           b[0] == ZXVS_FMT_WAV && b[1] == 1 && !memcmp(b + 2, vcid, ZXVS_CID_LEN) &&
           b[2 + ZXVS_CID_LEN] == 2,
       "wav ref");
    /* rate 16000, 1 channel, 800 frames follow the CID */
    if (o.n > 0) OK(b[3 + ZXVS_CID_LEN] == (16000 & 0x7F) + 0x80, "wav rate");
    free(o.d);
    f = fx("tiny_png");
    o = describe(f->d, f->n, 0);
    OK(o.n > 0 && !zxvs_section(o.d, (uint32_t) o.n, ZXVS_REF, 0, &b, &len) && b[1] == 0 &&
           b[2] == 7 && b[3] == 5,
       "png 7x5");
    free(o.d);
    /* similarity */
    desc_t e = describe(fx("energy_tsv")->d, fx("energy_tsv")->n, 0);
    uint8_t *mod = malloc(fx("energy_tsv")->n);
    memcpy(mod, fx("energy_tsv")->d, fx("energy_tsv")->n);
    for (uint32_t i = 200; i < 260; i++)
        if (mod[i] >= '1' && mod[i] <= '8') mod[i]++;
    desc_t e2 = describe(mod, fx("energy_tsv")->n, 0);
    desc_t j1 = describe(fx("zenodo_json")->d, fx("zenodo_json")->n, 0);
    desc_t bn = describe(fx("font_head_bin")->d, fx("font_head_bin")->n, 0);
    desc_t cs = describe(fx("sine_csv")->d, fx("sine_csv")->n, 0);
    int32_t s_self = zxvs_similarity(e.d, (uint32_t) e.n, e.d, (uint32_t) e.n);
    int32_t s_mod = zxvs_similarity(e.d, (uint32_t) e.n, e2.d, (uint32_t) e2.n);
    int32_t s_csv = zxvs_similarity(e.d, (uint32_t) e.n, cs.d, (uint32_t) cs.n);
    int32_t s_json = zxvs_similarity(e.d, (uint32_t) e.n, j1.d, (uint32_t) j1.n);
    int32_t s_bin = zxvs_similarity(e.d, (uint32_t) e.n, bn.d, (uint32_t) bn.n);
    OK(s_self == 65536, "self %d", s_self);
    OK(s_mod > s_csv && s_csv > s_bin && s_json > s_bin,
       "order: edited %d, other series %d, json %d, binary %d", s_mod, s_csv, s_json, s_bin);
    OK(zxvs_similarity(e.d, (uint32_t) e.n - 1, bn.d, (uint32_t) bn.n) < 0, "malformed");
    char det[200];
    snprintf(det, sizeof det,
             "(tsv vs: itself %.4f, edited %.4f, other csv %.4f, json %.4f, font %.4f)",
             s_self / 65536.0, s_mod / 65536.0, s_csv / 65536.0, s_json / 65536.0, s_bin / 65536.0);
    /* Grid Chain */
    uint64_t g = zxvs_gc_word(0x50, 0xBEEF, 0x12, 0x3456, 0x78, 0x9A);
    OK(g == 0x50BEEF123456789Aull, "gc %016llx", (unsigned long long) g);
    OK(zxvs_gc_word(0x1FF, 0x1FFFF, 0x1FF, 0x1FFFF, 0x1FF, 0x1FF) == 0xFFFFFFFFFFFFFFFFull,
       "gc masks");
    free(mod);
    free(e.d), free(e2.d), free(j1.d), free(bn.d), free(cs.d);
    end_section(det);
}

static int reg_cb(void *ctx, uint64_t w, const uint8_t cid[ZXVS_CID_LEN], const uint8_t *d,
                  uint32_t n)
{
    uint64_t *out = ctx;
    out[out[0] + 1] = w;
    out[0]++;
    return cid[0] == 1 && d && n ? 0 : 7;
}

static void t_register(void)
{
    SECTION("Alexandria registration callback");
    const zfx_t *f = fx("zenodo_json");
    desc_t o = describe(f->d, f->n, 0);
    uint64_t got[3] = {0, 0, 0};
    const uint64_t src = 0x1002000100000001ull; /* @512.01-001, a code word */
    OK(zxvs_register(o.d, (uint32_t) o.n, src, reg_cb, got) == 0 && got[0] == 2, "two calls");
    OK(got[1] == 0x2002000100000001ull && got[2] == 0x6002000100000001ull,
       "the source's word re-domained to data and embeddings %016llx %016llx",
       (unsigned long long) got[1], (unsigned long long) got[2]);
    OK(zxvs_register(o.d, (uint32_t) o.n - 1, src, reg_cb, got) == ZXVS_EFORMAT, "malformed");
    OK(zxvs_register(o.d, (uint32_t) o.n, src, 0, got) == ZXVS_EARG, "no callback");
    zxvs_info_t in;
    int8_t row[ZXVS_DIMS];
    uint32_t sc = 0, worst = 0;
    OK(zxvs_check(o.d, (uint32_t) o.n, &in) == 0 &&
           zxvs_alxv_row(o.d, (uint32_t) o.n, row, &sc) == 0 && sc >= 1,
       "ALXV row");
    for (uint32_t i = 0; i < ZXVS_DIMS; i++) {
        int64_t back = (int64_t) row[i] * sc, d = back - (int64_t) in.shape[i];
        if (row[i] < 0) worst = 0xFFFFFFFFu;
        if (d < 0) d = -d;
        if ((uint32_t) d > worst) worst = (uint32_t) d;
    }
    OK(worst <= sc / 2u + 1u, "ALXV row within half a step of the shape (worst %u, step %u)", worst,
       sc);
    OK(zxvs_alxv_row(o.d, (uint32_t) o.n - 1, row, &sc) == ZXVS_EFORMAT, "ALXV row: malformed");
    free(o.d);
    end_section(NULL);
}

static void t_determinism_errors(void)
{
    SECTION("determinism and error paths");
    const zfx_t *f = fx("cards_head_tsv");
    uint64_t ws = zxvs_work_size(f->n, 0), ob = zxvs_out_bound(f->n, 0);
    uint8_t *w = malloc((size_t) ws), *o1 = malloc((size_t) ob), *o2 = malloc((size_t) ob);
    memset(w, 0, (size_t) ws);
    int64_t n1 = zxvs_describe(f->d, f->n, w, ws, o1, ob, 0, 0);
    for (uint64_t i = 0; i < ws; i++) w[i] = (uint8_t) rnd();
    int64_t n2 = zxvs_describe(f->d, f->n, w, ws, o2, ob, 0, 0);
    OK(n1 > 0 && n1 == n2 && !memcmp(o1, o2, (size_t) n1), "same bytes");
    OK(zxvs_describe(f->d, f->n, w, ws - 17, o1, ob, 0, 0) == ZXVS_EWORK, "work - 17");
    OK(zxvs_describe(f->d, f->n, w, ws, o1, (uint64_t) n1 - 1, 0, 0) == ZXVS_EOUT, "out n - 1");
    OK(zxvs_describe(f->d, f->n, w, ws, o1, (uint64_t) n1, 0, 0) == n1, "out exactly n");
    OK(zxvs_describe(0, 5, w, ws, o1, ob, 0, 0) == ZXVS_EARG, "null data");
    OK(zxvs_describe(f->d, f->n, 0, ws, o1, ob, 0, 0) == ZXVS_EARG, "null work");
    OK(zxvs_describe(f->d, (uint64_t) 1 << 32, w, ws, o1, ob, 0, 0) == ZXVS_ECAP, "4 GiB");
    free(w);
    free(o1);
    free(o2);
    end_section(NULL);
}

static void t_fuzz(void)
{
    SECTION("fuzz: describe random/mutated inputs; check every truncation and flips");
    long inputs = 0, truncs = 0, flips = 0, flip_rej = 0;
    /* describe: random bytes and mutated text fixtures, lengths 0..400 */
    for (int it = 0; it < 600; it++) {
        uint32_t n = (uint32_t) (rnd() % 401);
        uint8_t *b = malloc(n ? n : 1);
        const zfx_t *src = &ZFX[rnd() % (sizeof ZFX / sizeof ZFX[0])];
        for (uint32_t i = 0; i < n; i++) {
            if (it & 1 && src->n)
                b[i] = src->d[i % src->n];
            else
                b[i] = (uint8_t) rnd();
            if ((it & 1) && rnd() % 23 == 0) b[i] = (uint8_t) "{}[]\",:<>/=.\n\t#0-e"[rnd() % 19];
        }
        desc_t o = describe(b, n, 0);
        OK(o.n > 0, "it %d n %u rc %lld", it, n, (long long) o.n);
        if (o.n > 0) OK(zxvs_check(o.d, (uint32_t) o.n, 0) == 0, "it %d check", it);
        free(o.d);
        free(b);
        inputs++;
    }
    /* check: every truncation and random flips of every fixture's descriptor */
    for (size_t i = 0; i < sizeof ZFX / sizeof ZFX[0]; i++) {
        desc_t o = describe(ZFX[i].d, ZFX[i].n, 0);
        if (o.n <= 0) continue;
        uint32_t step = o.n > 4000 ? (uint32_t) (o.n / 2000) : 1;
        for (uint32_t L = 0; L < (uint32_t) o.n; L += step) {
            uint8_t *c = malloc(L ? L : 1);
            memcpy(c, o.d, L);
            OK(zxvs_check(c, L, 0) != 0, "%s truncated to %u accepted", ZFX[i].name, L);
            free(c);
            truncs++;
        }
        for (int k = 0; k < 300; k++) {
            uint8_t *c = malloc((size_t) o.n);
            memcpy(c, o.d, (size_t) o.n);
            uint32_t at = rnd() % (uint32_t) o.n;
            c[at] ^= (uint8_t) (1u << (rnd() % 8));
            int rc = zxvs_check(c, (uint32_t) o.n, 0);
            flip_rej += rc != 0;
            if (rc == 0) { /* still well formed: the readers must cope too */
                zxvs_curve_t cv;
                uint32_t ncur = 0;
                zxvs_info_t in;
                zxvs_check(c, (uint32_t) o.n, &in);
                ncur = in.ncurves;
                for (uint32_t q = 0; q < ncur; q++)
                    if (!zxvs_curve(c, (uint32_t) o.n, q, &cv)) {
                        int64_t v;
                        zxvs_curve_eval(&cv, cv.npts - 1, &v);
                    }
            }
            free(c);
            flips++;
        }
        free(o.d);
    }
    char det[160];
    snprintf(det, sizeof det,
             "(%ld inputs, %ld truncations rejected, %ld flips: %ld rejected, rest still valid)",
             inputs, truncs, flips, flip_rej);
    end_section(det);
}

/* ---- drawing ---------------------------------------------------------------- */
static uint32_t PX[512 * 512], PX2[512 * 512];
static zui_vec_work_t WK;

static void t_draw(void)
{
    SECTION("zxvs_draw: draws, deterministic, two sizes, malformed draws nothing");
    char det[256];
    size_t dl = 0;
    det[0] = 0;
    static const char *names[] = {"energy_tsv",    "sine_csv", "zenodo_json",    "stft_math_c",
                                  "font_head_bin", "tiny_png", "cards_head_tsv", "page_html"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        const zfx_t *f = fx(names[i]);
        desc_t o = describe(f->d, f->n, 0);
        if (o.n <= 0) {
            OK(0, "%s", names[i]);
            continue;
        }
        for (int sz = 0; sz < 2; sz++) {
            int32_t S = sz ? 512 : 96;
            zui_surface_t s;
            for (int k = 0; k < 512 * 512; k++) PX[k] = 0x101010;
            zui_surface_init(&s, PX, S, S, 512);
            uint32_t err = 1;
            zxvs_draw_opts_t op = {0xFF000000u, 0, 0};
            int rc = zxvs_draw(&s, &WK, o.d, (uint32_t) o.n, (zui_rect_t){0, 0, S, S}, &op, &err);
            uint32_t lit = 0;
            for (int y = 0; y < S; y++)
                for (int x = 0; x < S; x++) lit += (PX[y * 512 + x] & 0xFFFFFF) != 0;
            OK(rc == 0 && err == 0 && lit > (uint32_t) S, "%s size %d rc %d err %u lit %u",
               names[i], S, rc, err, lit);
            memcpy(PX2, PX, sizeof PX);
            for (int k = 0; k < 512 * 512; k++) PX[k] = 0x101010;
            zxvs_draw(&s, &WK, o.d, (uint32_t) o.n, (zui_rect_t){0, 0, S, S}, &op, &err);
            OK(!memcmp(PX, PX2, sizeof PX), "%s size %d not deterministic", names[i], S);
            if (sz) {
                int w = snprintf(det + dl, sizeof det - dl, "%s%s %u px", i ? ", " : "\n    ",
                                 names[i], lit);
                if (w > 0 && (size_t) w < sizeof det - dl) dl += (size_t) w;
            }
        }
        /* malformed: nothing drawn */
        for (int k = 0; k < 512 * 512; k++) PX[k] = 0x101010;
        zui_surface_t s;
        zui_surface_init(&s, PX, 128, 128, 512);
        o.d[o.n - 1] = 1;
        OK(zxvs_draw(&s, &WK, o.d, (uint32_t) o.n, (zui_rect_t){0, 0, 128, 128}, 0, 0) == -1,
           "%s malformed", names[i]);
        uint32_t ch = 0;
        for (int k = 0; k < 512 * 512; k++) ch += PX[k] != 0x101010;
        OK(ch == 0, "%s malformed drew %u px", names[i], ch);
        free(o.d);
    }
    end_section(det);
}

int main(void)
{
    t_fixtures();
    t_curves();
    t_struct();
    t_media_sim();
    t_register();
    t_determinism_errors();
    t_fuzz();
    t_draw();
    printf("test_zxvs: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
