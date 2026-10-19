/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gen_vectors.c — writes sdk/conformance/vectors.json from the C library
 * itself. Every SDK's test suite loads that file and checks that its own
 * wrappers (and its pure host-language code, such as UBH-168 framing and
 * the four-valued logic tables) give exactly these answers. One source of
 * truth: if the C library changes behaviour, `make vectors` shows the diff.
 * Usage: gen_vectors <fixtures dir> > vectors.json */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_api.h"

static void phex(const uint8_t *b, size_t n)
{
    putchar('"');
    for (size_t i = 0; i < n; i++) printf("%02x", b[i]);
    putchar('"');
}

static const char *sep(int *first)
{
    if (*first) {
        *first = 0;
        return "";
    }
    return ",";
}

static void statuses(void)
{
    static const int32_t codes[] = {0,   -1,  -2,  -3,  -4,  -5,  -6,  -7,  -8,  -9,  -10, -11, -12,
                                    -13, -14, -15, -16, -17, -18, -19, -20, -21, -22, -23, -24, -99};
    printf("\"status\":[");
    int f = 1;
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++)
        printf("%s{\"code\":%d,\"name\":\"%s\",\"class\":%d}", sep(&f), codes[i],
               zxv_status_name(codes[i]), zxv_status_class(codes[i]));
    printf("]");
}

static void rationals(void)
{
    static const int64_t in[][4] = {{1, 10, 2, 10}, {1, 3, 1, 6},   {-7, 4, 5, 6},
                                    {3, 1, -3, 1},  {22, 7, 355, 113}, {1, 1000000007, 1, 998244353}};
    printf(",\"rational_ops\":[");
    int f = 1;
    for (size_t i = 0; i < sizeof in / sizeof in[0]; i++) {
        int64_t s[2], d[2], m[2], q[2];
        int32_t c;
        zxv_rat_add(in[i][0], in[i][1], in[i][2], in[i][3], &s[0], &s[1]);
        zxv_rat_sub(in[i][0], in[i][1], in[i][2], in[i][3], &d[0], &d[1]);
        zxv_rat_mul(in[i][0], in[i][1], in[i][2], in[i][3], &m[0], &m[1]);
        int32_t qs = zxv_rat_div(in[i][0], in[i][1], in[i][2], in[i][3], &q[0], &q[1]);
        zxv_rat_cmp(in[i][0], in[i][1], in[i][2], in[i][3], &c);
        printf("%s{\"a\":[%" PRId64 ",%" PRId64 "],\"b\":[%" PRId64 ",%" PRId64 "],"
               "\"add\":[%" PRId64 ",%" PRId64 "],\"sub\":[%" PRId64 ",%" PRId64 "],"
               "\"mul\":[%" PRId64 ",%" PRId64 "],",
               sep(&f), in[i][0], in[i][1], in[i][2], in[i][3], s[0], s[1], d[0], d[1], m[0], m[1]);
        if (qs == ZXV_OK)
            printf("\"div\":[%" PRId64 ",%" PRId64 "],", q[0], q[1]);
        else
            printf("\"div\":null,");
        printf("\"cmp\":%d}", c);
    }
    printf("]");

    static const char *texts[] = {"0.1", "-12.75", "3/4", "42", "-0.000001", "6/-4",
                                  "1e5", " 1", "NaN", "1/0", "99999999999999999999", ".5"};
    printf(",\"rational_parse\":[");
    f = 1;
    for (size_t i = 0; i < sizeof texts / sizeof texts[0]; i++) {
        int64_t n = 0, d = 0;
        int32_t st = zxv_rat_parse(texts[i], &n, &d);
        char out[64] = "";
        size_t ol;
        if (st == ZXV_OK) zxv_rat_format(n, d, out, sizeof out, &ol);
        printf("%s{\"text\":\"%s\",\"status\":%d", sep(&f), texts[i], st);
        if (st == ZXV_OK) printf(",\"value\":[%" PRId64 ",%" PRId64 "],\"format\":\"%s\"", n, d, out);
        printf("}");
    }
    printf("]");

    static const double dbl[] = {0.1, 0.5, -0.5, 3.0, 1e-300, 1e300, 0.0, -0.0, 6.02214076e23,
                                 0.3, 1.0 / 3.0};
    printf(",\"ieee754\":[");
    f = 1;
    for (size_t i = 0; i < sizeof dbl / sizeof dbl[0]; i++) {
        uint64_t bits;
        memcpy(&bits, &dbl[i], 8);
        int64_t n = 0, d = 0;
        int32_t st = zxv_rat_from_ieee754(bits, &n, &d);
        printf("%s{\"bits\":\"%016" PRIx64 "\",\"status\":%d", sep(&f), bits, st);
        if (st == ZXV_OK) printf(",\"value\":[%" PRId64 ",%" PRId64 "]", n, d);
        printf("}");
    }
    static const uint64_t specials[] = {0x7FF8000000000000ull, 0x7FF0000000000000ull,
                                        0xFFF0000000000000ull};
    for (size_t i = 0; i < 3; i++) {
        int64_t n, d;
        printf(",{\"bits\":\"%016" PRIx64 "\",\"status\":%d}", specials[i],
               zxv_rat_from_ieee754(specials[i], &n, &d));
    }
    printf("]");

    static const int64_t q16in[][2] = {{7, 10}, {-1, 2}, {1, 3}, {-1, 3}, {32767, 1}, {32768, 1}, {1, 131072}};
    printf(",\"q16\":[");
    f = 1;
    for (size_t i = 0; i < sizeof q16in / sizeof q16in[0]; i++) {
        int32_t q = 0;
        int32_t st = zxv_rat_to_q16(q16in[i][0], q16in[i][1], &q);
        printf("%s{\"value\":[%" PRId64 ",%" PRId64 "],\"status\":%d,\"q16\":%d}", sep(&f),
               q16in[i][0], q16in[i][1], st, st == ZXV_OK ? q : 0);
    }
    printf("]");
}

static void logic(void)
{
    printf(",\"lpres\":{\"names\":[\"NEITHER\",\"TRUE\",\"FALSE\",\"BOTH\"],\"not\":[");
    for (int a = 0; a < 4; a++) {
        int32_t r;
        zxv_lp_not(a, &r);
        printf("%s%d", a ? "," : "", r);
    }
    printf("],\"and\":[");
    for (int a = 0; a < 4; a++) {
        printf("%s[", a ? "," : "");
        for (int b = 0; b < 4; b++) {
            int32_t r;
            zxv_lp_and(a, b, &r);
            printf("%s%d", b ? "," : "", r);
        }
        printf("]");
    }
    printf("],\"or\":[");
    for (int a = 0; a < 4; a++) {
        printf("%s[", a ? "," : "");
        for (int b = 0; b < 4; b++) {
            int32_t r;
            zxv_lp_or(a, b, &r);
            printf("%s%d", b ? "," : "", r);
        }
        printf("]");
    }
    printf("]}");
}

static void wire(void)
{
    static const struct {
        uint32_t trunk, shell, sync, sp[3], sm[2];
    } fr[] = {
        {3, 2, 1, {0x01020304, 0x05060708, 0x090A0B0C}, {0x11121314, 0x15161718}},
        {0, 0, 0, {0x00010000, 0x00010000, 0}, {0x00010000, 0x00010000}},
        {1, 0, 0, {0x00010000, 0x00010000, 0}, {0xFFFF0000, 0xFFFF0000}},
        {9, 9, 1, {0x00010000, 0, 0}, {0, 0x00010000}},
        {0, 0, 0, {0, 0, 0}, {0, 0}},
        {5, 1, 0, {0xDEADBEEF, 0xCAFEBABE, 0x8BADF00D}, {0xFEEDFACE, 0x0BADC0DE}},
    };
    printf(",\"ubh168\":[");
    int f = 1;
    for (size_t i = 0; i < sizeof fr / sizeof fr[0]; i++) {
        uint8_t tag, out[21];
        int32_t truth;
        zxv_wire_tag(fr[i].trunk, fr[i].shell, fr[i].sync, &tag);
        zxv_wire_pack(tag, fr[i].sp, fr[i].sm, out);
        zxv_wire_resolve(out, &truth);
        printf("%s{\"trunk\":%u,\"shell\":%u,\"sync\":%u,\"tag\":%u,\"s_plus\":[%u,%u,%u],"
               "\"s_minus\":[%u,%u],\"frame\":",
               sep(&f), fr[i].trunk, fr[i].shell, fr[i].sync, tag, fr[i].sp[0], fr[i].sp[1],
               fr[i].sp[2], fr[i].sm[0], fr[i].sm[1]);
        phex(out, 21);
        printf(",\"truth\":%d}", truth);
    }
    printf("],\"ubh168_idle\":[");
    for (uint32_t t = 0; t < 10; t++) {
        uint8_t out[21];
        zxv_wire_idle(t, out);
        printf("%s", t ? "," : "");
        phex(out, 21);
    }
    printf("]");
}

static void hashes(void)
{
    static const char *msgs[] = {"", "abc", "The ZXV kernel boots the swarm."};
    printf(",\"hash\":[");
    int f = 1;
    for (size_t i = 0; i < 3; i++) {
        uint8_t k[32], s[32], sh[64];
        size_t n = strlen(msgs[i]);
        zxv_keccak256((const uint8_t *) msgs[i], n, k);
        zxv_sha3_256((const uint8_t *) msgs[i], n, s);
        zxv_shake256((const uint8_t *) msgs[i], n, sh, 64);
        printf("%s{\"utf8\":\"%s\",\"keccak256\":", sep(&f), msgs[i]);
        phex(k, 32);
        printf(",\"sha3_256\":");
        phex(s, 32);
        printf(",\"shake256_64\":");
        phex(sh, 64);
        printf("}");
    }
    printf("]");
}

static void crypto(zxv_cap *cap)
{
    uint32_t feat;
    zxv_features(&feat);
    if (!(feat & ZXV_FEAT_MLDSA87)) return;
    uint8_t sk[32], pk[32], sig[64];
    for (int i = 0; i < 32; i++) sk[i] = (uint8_t) i;
    const char *msg = "zxv conformance";
    zxv_ed25519_public(cap, sk, pk);
    zxv_ed25519_sign(cap, sk, (const uint8_t *) msg, strlen(msg), sig);
    printf(",\"ed25519\":{\"sk\":");
    phex(sk, 32);
    printf(",\"pk\":");
    phex(pk, 32);
    printf(",\"msg_utf8\":\"%s\",\"sig\":", msg);
    phex(sig, 64);
    printf("}");

    static uint8_t dpk[ZXV_MLDSA87_PK_BYTES], dsk[ZXV_MLDSA87_SK_BYTES], dsig[ZXV_MLDSA87_SIG_BYTES];
    uint8_t seed[32], h[32];
    for (int i = 0; i < 32; i++) seed[i] = (uint8_t) (0xA0 + i);
    zxv_mldsa87_keygen_seed(cap, seed, dpk, dsk);
    zxv_mldsa87_sign_deterministic(cap, dsk, (const uint8_t *) msg, strlen(msg),
                                   (const uint8_t *) "zxv", 3, dsig);
    printf(",\"mldsa87\":{\"seed\":");
    phex(seed, 32);
    printf(",\"ctx_utf8\":\"zxv\",\"msg_utf8\":\"%s\",\"pk_sha3_256\":", msg);
    zxv_sha3_256(dpk, sizeof dpk, h);
    phex(h, 32);
    printf(",\"sk_sha3_256\":");
    zxv_sha3_256(dsk, sizeof dsk, h);
    phex(h, 32);
    printf(",\"sig_deterministic_sha3_256\":");
    zxv_sha3_256(dsig, sizeof dsig, h);
    phex(h, 32);
    printf("}");

    static uint8_t ek[ZXV_MLKEM1024_EK_BYTES], dk[ZXV_MLKEM1024_DK_BYTES], ct[ZXV_MLKEM1024_CT_BYTES];
    uint8_t d[32], z[32], m[32], ss[32];
    for (int i = 0; i < 32; i++) {
        d[i] = (uint8_t) i;
        z[i] = (uint8_t) (32 + i);
        m[i] = (uint8_t) (64 + i);
    }
    zxv_mlkem1024_keygen_derand(cap, d, z, ek, dk);
    zxv_mlkem1024_encaps_derand(cap, ek, m, ct, ss);
    printf(",\"mlkem1024\":{\"d\":");
    phex(d, 32);
    printf(",\"z\":");
    phex(z, 32);
    printf(",\"m\":");
    phex(m, 32);
    printf(",\"ek_sha3_256\":");
    zxv_sha3_256(ek, sizeof ek, h);
    phex(h, 32);
    printf(",\"ct_sha3_256\":");
    zxv_sha3_256(ct, sizeof ct, h);
    phex(h, 32);
    printf(",\"ss\":");
    phex(ss, 32);
    printf("}");
}

static void swarm(zxv_ctx *ctx, zxv_cap *cap)
{
    zxv_swarm *sw;
    zxv_swarm_create(ctx, 4, 100000, &sw);
    static const uint32_t lv[] = {0, 1, 1, 2, 2, 2};
    for (uint32_t m = 0; m < 6; m++) zxv_swarm_register(cap, sw, 100 + m, lv[m]);
    zxv_swarm_set_mood(cap, sw, 0x1F914, 4);
    zxv_swarm_set_feeling(cap, sw, 101, 0x1F604, 5);
    zxv_swarm_set_feeling(cap, sw, 104, 0x1F61F, 2);
    uint64_t cyc;
    zxv_swarm_begin_cycle(cap, sw, &cyc);
    printf(",\"swarm\":{\"levels\":4,\"tokens_per_cycle\":100000,\"models\":["
           "[100,0],[101,1],[102,1],[103,2],[104,2],[105,2]],"
           "\"mood\":[129300,4],\"feelings\":[[101,128516,5],[104,128543,2]],\"allotments\":[");
    for (uint32_t m = 0; m < 6; m++) {
        uint64_t al, im, used;
        zxv_swarm_allotment(sw, 100 + m, &al, &im, &used);
        printf("%s[%u,%" PRIu64 ",%" PRIu64 "]", m ? "," : "", 100 + m, al, im);
    }
    printf("],\"level_capacity\":[");
    for (uint32_t l = 0; l < 8; l++) {
        uint32_t c;
        zxv_swarm_level_capacity(l, &c);
        printf("%s%u", l ? "," : "", c);
    }
    printf("]}");
    zxv_swarm_destroy(sw);
}

static int32_t collect(void *user, int32_t token, const uint8_t *piece, size_t len)
{
    (void) piece;
    (void) len;
    int32_t *ids = user;
    if (ids[0] < 63) ids[1 + ids[0]++] = token;
    return 1;
}

static void generation(zxv_cap *cap, const char *dir)
{
    uint32_t feat;
    zxv_features(&feat);
    if (!(feat & ZXV_FEAT_TENSOR)) return;
    char mp[1024], tp[1024], pp[1024];
    snprintf(mp, sizeof mp, "%s/tiny-qwen2.gguf", dir);
    snprintf(tp, sizeof tp, "%s/tiny-tokenizer.gguf", dir);
    snprintf(pp, sizeof pp, "%s/prompt.txt", dir);
    FILE *f = fopen(pp, "rb");
    if (!f) return;
    uint8_t prompt[256];
    size_t plen = fread(prompt, 1, sizeof prompt, f);
    fclose(f);
    zxv_model *m;
    if (zxv_model_open(cap, mp, tp, &m) != ZXV_OK) return;
    zxv_session *s;
    zxv_session_create(m, 128, ZXV_KV_Q16, &s);
    int32_t greedy[64] = {0}, sampled[64] = {0}, ptok[64];
    size_t np;
    zxv_tokenize(m, prompt, plen, 1, ptok, 64, &np);
    zxv_generate(cap, s, prompt, plen, 16, 0, 0, 0, 0, 0, 0, collect, greedy, NULL, NULL, NULL);
    zxv_session_reset(s);
    zxv_generate(cap, s, prompt, plen, 12, 45875, 40, 62259, 72090, 64, 42, collect, sampled, NULL,
                 NULL, NULL);
    printf(",\"generate\":{\"model\":\"tiny-qwen2.gguf\",\"tokenizer\":\"tiny-tokenizer.gguf\","
           "\"prompt\":\"prompt.txt\",\"n_ctx\":128,\"prompt_tokens\":[");
    for (size_t i = 0; i < np; i++) printf("%s%d", i ? "," : "", ptok[i]);
    printf("],\"greedy\":{\"max_tokens\":16,\"tokens\":[");
    for (int i = 0; i < greedy[0]; i++) printf("%s%d", i ? "," : "", greedy[1 + i]);
    printf("]},\"sampled\":{\"max_tokens\":12,\"temperature_q16\":45875,\"top_k\":40,"
           "\"top_p_q16\":62259,\"repeat_penalty_q16\":72090,\"repeat_last_n\":64,\"seed\":42,"
           "\"tokens\":[");
    for (int i = 0; i < sampled[0]; i++) printf("%s%d", i ? "," : "", sampled[1 + i]);
    printf("]}}");
    zxv_session_destroy(s);
    zxv_model_destroy(m);
}

static void evm(zxv_cap *cap)
{
    uint8_t id[32], addr[60], buf[1024], dg[32];
    for (int i = 0; i < 32; i++) id[i] = (uint8_t) (0xC0 + i % 16);
    for (int i = 0; i < 60; i++) addr[i] = (uint8_t) (i * 7 + 1);
    const int64_t bal[3] = {1000000, -250, 0};
    size_t len;
    zxv_evm_encode_state(cap, id, 7, addr, bal, 3, buf, sizeof buf, &len, dg);
    printf(",\"evm_state\":{\"channel_id\":");
    phex(id, 32);
    printf(",\"nonce\":7,\"addresses\":[");
    for (int i = 0; i < 3; i++) {
        printf("%s", i ? "," : "");
        phex(addr + 20 * i, 20);
    }
    printf("],\"balances\":[1000000,-250,0],\"encoding\":");
    phex(buf, len);
    printf(",\"digest\":");
    phex(dg, 32);
    printf("}");
}

/* ABI 1.3: the 64-state hexagram tag. Every table below is computed by
 * libzxv (never written by hand): the and/or results for some base pairs
 * (e.g. BOTH and NEITHER) follow lpres's current tables and may change by
 * the owner's decision, and `make vectors` will then show the diff. */
static void hex_row(const char *key, int (*f)(uint8_t, uint8_t, uint8_t *))
{
    printf(",\"%s\":[", key);
    for (int a = 0; a < 64; a++) {
        printf("%s[", a ? "," : "");
        for (int b = 0; b < 64; b++) {
            uint8_t r = 0;
            int st = f((uint8_t) a, (uint8_t) b, &r);
            printf("%s%d", b ? "," : "", st == ZXV_OK ? (int) r : -1);
        }
        printf("]");
    }
    printf("]");
}

static int hex_and(uint8_t a, uint8_t b, uint8_t *o) { return zxv_hex_and(a, b, o); }
static int hex_or(uint8_t a, uint8_t b, uint8_t *o) { return zxv_hex_or(a, b, o); }

static void hex(zxv_cap *cap)
{
    printf(",\"hex\":{\"note\":\"tag = base<<4 | polarity<<2 | phase. Base codes are NOT ZXV_LP_* values: "
           "use lp_to_base / base_to_lp. -1 marks a refused call (ZXV_E_STATE).\"");
    printf(",\"base_names\":[\"false\",\"neither\",\"both\",\"true\"]");
    printf(",\"polarity_names\":[\"entropic\",\"neutral\",\"forbidden\",\"negentropic\"]");
    printf(",\"phase_names\":[\"potential\",\"emerging\",\"dissolving\",\"manifest\"]");
    printf(",\"action_names\":[");
    for (int a = 0; a < 5; a++) printf("%s\"%s\"", a ? "," : "", zxv_hex_action_name(a));
    printf("],\"lp_to_base\":[");
    for (int l = 0; l < 4; l++) {
        uint8_t b;
        zxv_hex_from_lp(l, &b);
        printf("%s%u", l ? "," : "", b);
    }
    printf("],\"base_to_lp\":[");
    for (int b = 0; b < 4; b++) {
        int32_t l;
        zxv_hex_to_lp((uint8_t) b, &l);
        printf("%s%d", b ? "," : "", l);
    }
    printf("],\"phase_next\":[");
    for (int p = 0; p < 4; p++) {
        uint8_t n;
        zxv_hex_phase_next((uint8_t) p, &n);
        printf("%s%u", p ? "," : "", n);
    }
    printf("],\"tags\":[");
    for (int t = 0; t < 64; t++) {
        uint8_t b, p, ph;
        int32_t act;
        zxv_hex_unpack((uint8_t) t, &b, &p, &ph);
        zxv_hex_action((uint8_t) t, &act);
        printf("%s{\"tag\":%d,\"name\":\"%s\",\"base\":%u,\"polarity\":%u,\"phase\":%u,\"action\":%d}",
               t ? "," : "", t, zxv_hex_name((uint8_t) t), b, p, ph, act);
    }
    printf("],\"not\":[");
    for (int t = 0; t < 64; t++) {
        uint8_t r;
        zxv_hex_not((uint8_t) t, &r);
        printf("%s%u", t ? "," : "", r);
    }
    printf("]");
    hex_row("and", hex_and);
    hex_row("or", hex_or);
    printf(",\"step\":[");
    for (int t = 0; t < 64; t++) {
        printf("%s[", t ? "," : "");
        for (int p = 0; p < 4; p++) {
            uint8_t r;
            int st = zxv_hex_step((uint8_t) t, (uint8_t) p, &r);
            printf("%s%d", p ? "," : "", st == ZXV_OK ? (int) r : -1);
        }
        printf("]");
    }
    printf("],\"set\":[");
    for (int t = 0; t < 64; t++) {
        printf("%s[", t ? "," : "");
        for (int bp = 0; bp < 16; bp++) {
            uint8_t r;
            int st = zxv_hex_set((uint8_t) t, (uint8_t) (bp >> 2), (uint8_t) (bp & 3), &r);
            printf("%s%d", bp ? "," : "", st == ZXV_OK ? (int) r : -1);
        }
        printf("]");
    }
    static const uint32_t pc[][5] = {{0, 0, 1, 0, 0}, {3, 1, 5, 1, 0}, {3, 1, 3, 2, 0}, {3, 1, 4, 2, 0},
                                     {0, 0, 0, 0, 0}, {3, 1, 9, 1, 1}, {1, 4, 1, 6, 0},
                                     {4294967295u, 0, 0, 4294967295u, 0}, {2, 2, 7, 7, 0}};
    printf("],\"polarity\":[");
    for (size_t i = 0; i < sizeof pc / sizeof pc[0]; i++) {
        uint8_t r;
        zxv_hex_polarity(pc[i][0], pc[i][1], pc[i][2], pc[i][3], (int32_t) pc[i][4], &r);
        printf("%s{\"pos0\":%u,\"neg0\":%u,\"pos1\":%u,\"neg1\":%u,\"stale\":%s,\"polarity\":%u}",
               i ? "," : "", pc[i][0], pc[i][1], pc[i][2], pc[i][3], pc[i][4] ? "true" : "false", r);
    }
    /* A three-record quarantine chain, so an SDK can recompute the SHA-256
     * links in host code and compare zxv_hexlog_at / zxv_hexlog_head. */
    static const uint8_t qtag[3] = {0x3B, 0x28, 0x0A}; /* true/forbidden/manifest, both/forbidden/potential, false/forbidden/dissolving */
    static const uint8_t qreason[3] = {1, 7, 255};
    static const uint32_t qwho[3] = {42, 0xDEADBEEFu, 0};
    zxv_hexlog *log;
    zxv_hexlog_create(cap, 3, &log);
    printf("],\"qlog\":{\"domain\":\"zxv/lpx-qlog/v1\",\"records\":[");
    for (uint32_t k = 0; k < 3; k++) {
        uint8_t subj[32], prev[32], hash[32];
        for (int i = 0; i < 32; i++) subj[i] = (uint8_t) (i + 16 * k);
        zxv_hexlog_append(cap, log, qtag[k], qreason[k], qwho[k], subj, NULL);
        zxv_hexlog_at(log, k, NULL, NULL, NULL, NULL, NULL, prev, hash);
        printf("%s{\"seq\":%u,\"tag\":%u,\"reason\":%u,\"submitter\":%u,\"subject\":", k ? "," : "", k,
               qtag[k], qreason[k], qwho[k]);
        phex(subj, 32);
        printf(",\"prev\":");
        phex(prev, 32);
        printf(",\"hash\":");
        phex(hash, 32);
        printf("}");
    }
    uint8_t head[32];
    zxv_hexlog_head(log, head);
    printf("],\"head\":");
    phex(head, 32);
    printf("}}");
    zxv_hexlog_destroy(log);
}

/* ===================================================================== */
/* ABI 1.5 sections. Conventions (all new sections):                      */
/*   - every 64-bit integer (int64_t / uint64_t / size_t amounts) is a     */
/*     decimal STRING, so readers without exact 64-bit JSON numbers (JS,   */
/*     a Long-only parser) read it exactly; 32-bit values are numbers;     */
/*   - bytes are lowercase hex; statuses are their ZXV_E_* names;          */
/*   - text is a JSON string whose only escapes are \n, \" and \\.        */
/*   - nothing here depends on randomness: keys come from fixed seeds and  */
/*     randomized signatures are left out (only the signed bodies are     */
/*     given, see "web4").                                                 */
/* ===================================================================== */

static void pjs(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        unsigned char c = (unsigned char) *s;
        if (c == '"' || c == '\\')
            printf("\\%c", c);
        else if (c == '\n')
            printf("\\n");
        else if (c < 0x20 || c >= 0x7f)
            printf("\\u%04x", c);
        else
            putchar(c);
    }
    putchar('"');
}
static void pu(uint64_t v) { printf("\"%" PRIu64 "\"", v); }
static void pi(int64_t v) { printf("\"%" PRId64 "\"", v); }
static void pst(int32_t st) { printf("\"%s\"", zxv_status_name(st)); }

static void status15(void)
{
    static const int32_t codes[] = {-32, -33, -34, -35, -36, -37, -38, -39, -40};
    printf(",\"status_abi15\":[");
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++)
        printf("%s{\"code\":%d,\"name\":\"%s\",\"class\":%d}", i ? "," : "", codes[i],
               zxv_status_name(codes[i]), zxv_status_class(codes[i]));
    printf("]");
}

/* ---------------------------------------------------------------- treaty */

static const char T_BASE[] = "TREATY CORRIDOR-7 USD SETL\nPARTY ACME 5000 4000 1/2\n"
                             "PARTY BETA 3000 3000 1/2\nOWE ACME BETA 1200\nEND\n";
static const char T_TRI[] = "TREATY CORRIDOR-7 USD SETL\nPARTY ACME 5000000 4000 1/3\n"
                            "PARTY BETA 3000000 3000 1/7\nPARTY GAMMA 900000 0 11/21\n"
                            "OWE ACME BETA 1200000\nOWE BETA GAMMA 700000\nOWE GAMMA ACME 100000\nEND\n";
static const char T_NET[] = "TREATY CORRIDOR-7 USD NET\nPARTY ACME 5000 4000 1/2\n"
                            "PARTY BETA 3000 3000 1/2\nOWE ACME BETA 1200\nEND\n";
static const char T_XAU[] = "TREATY A-1 XAU SETL\nPARTY A 10 10 1/3\nPARTY B 10 9 1/3\nPARTY C 0 0 1/3\n"
                            "OWE A B 5\nOWE B C 5\nOWE C A 5\nEND\n";
static const char T_EDGE[] = "TREATY ABCDEFGHIJKLMNOPQRSTUVWXYZ0123 EUR NET\n"
                             "PARTY ABCDEFGH 999999999999999999 999999999999999999 1/999999937\n"
                             "PARTY Z9-Z 1 0 999999936/999999937\nEND\n";
static const char T_POOR[] = "TREATY POOR USD SETL\nPARTY A 0 0 1/2\nPARTY B 0 0 1/2\nOWE A B 100\nEND\n";

static void treaty_text(const char *what, int32_t (*f)(const zxv_treaty *, char *, size_t, size_t *),
                        const zxv_treaty *t)
{
    char buf[ZXV_TREATY_SUTRA_MAX + 1];
    size_t n;
    f(t, buf, sizeof buf, &n);
    printf(",\"%s\":", what);
    pjs(buf);
}

static void treaty(void)
{
    static const char *ok[] = {T_BASE, T_TRI, T_NET, T_XAU, T_EDGE};
    static const char *bad[] = {
        "TREATY",
        "treaty N USD NET\nPARTY A 1 1 1/1\nEND\n",
        "TREATY n USD NET\nPARTY A 1 1 1/1\nEND\n",
        "TREATY N US NET\nPARTY A 1 1 1/1\nEND\n",
        "TREATY N USD NETT\nPARTY A 1 1 1/1\nEND\n",
        "TREATY N USD NET\nEND\n",
        "TREATY N USD NET\nPARTY A 1 1 1/1\nEND\n\n",
        "TREATY N USD NET\nPARTY A 5 6 1/1\nEND\n",
        "TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/3\nEND\n",
        "TREATY N USD NET\nPARTY A 1 1 2/4\nPARTY B 1 1 1/2\nEND\n",
        "TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY A 1 1 1/2\nEND\n",
        "TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A A 5\nEND\n",
        "TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A C 5\nEND\n",
        "TREATY N USD NET\nPARTY A 1000000000000000000 1 1/1\nEND\n",
        "TREATY N USD NET\nPARTY A 01 1 1/1\nEND\n",
    };
    static uint8_t rec0[ZXV_TREATY_REC_LEN];
    printf(",\"treaty\":{\"rec_len\":%u,\"result_len\":%u,\"op_names\":{\"1\":\"NET\",\"2\":\"SETL\"},"
           "\"sutra\":[",
           ZXV_TREATY_REC_LEN, ZXV_TREATY_RESULT_LEN);
    for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        zxv_treaty *t, *u;
        zxv_treaty_create(&t);
        zxv_treaty_create(&u);
        uint32_t off = 99;
        int32_t fault = 99;
        int32_t st = zxv_treaty_parse_sutra(t, ok[i], &off, &fault);
        printf("%s{\"sutra\":", i ? "," : "");
        pjs(ok[i]);
        printf(",\"status\":");
        pst(st);
        if (st == ZXV_OK) {
            static uint8_t rec[ZXV_TREATY_REC_LEN];
            size_t n;
            zxv_treaty_emit_record(t, rec, sizeof rec, &n);
            if (i == 0) memcpy(rec0, rec, sizeof rec0);
            printf(",\"record\":");
            phex(rec, n);
            int32_t eq = 0;
            zxv_treaty_parse_record(u, rec, n, NULL, NULL);
            zxv_treaty_equal(t, u, &eq);
            printf(",\"record_parses_equal\":%s", eq ? "true" : "false");
            treaty_text("reemit", zxv_treaty_emit_sutra, u);
            treaty_text("name", zxv_treaty_name, t);
            treaty_text("asset", zxv_treaty_asset, t);
            int32_t op;
            uint32_t np, no;
            zxv_treaty_header(t, &op, &np, &no);
            printf(",\"op\":%d,\"party_count\":%u,\"owe_count\":%u,\"parties\":[", op, np, no);
            for (uint32_t p = 0; p < np; p++) {
                char id[16];
                size_t il;
                int64_t dep, clm;
                uint32_t wn, wd;
                zxv_treaty_party_id(t, p, id, sizeof id, &il);
                zxv_treaty_party(t, p, &dep, &clm, &wn, &wd);
                printf("%s{\"id\":", p ? "," : "");
                pjs(id);
                printf(",\"deposit\":");
                pi(dep);
                printf(",\"claim\":");
                pi(clm);
                printf(",\"weight\":[%u,%u]}", wn, wd);
            }
            printf("],\"owes\":[");
            for (uint32_t o = 0; o < no; o++) {
                uint32_t fr, to;
                int64_t amt;
                zxv_treaty_owe(t, o, &fr, &to, &amt);
                printf("%s{\"from\":%u,\"to\":%u,\"amount\":", o ? "," : "", fr, to);
                pi(amt);
                printf("}");
            }
            printf("]");
        }
        printf("}");
        zxv_treaty_destroy(u);
        zxv_treaty_destroy(t);
    }
    printf("],\"reject\":[");
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        zxv_treaty *t;
        zxv_treaty_create(&t);
        uint32_t off = 0;
        int32_t fault = 0;
        int32_t st = zxv_treaty_parse_sutra(t, bad[i], &off, &fault);
        printf("%s{\"sutra\":", i ? "," : "");
        pjs(bad[i]);
        printf(",\"status\":");
        pst(st);
        printf(",\"fault\":%d,\"offset\":%u}", fault, off);
        zxv_treaty_destroy(t);
    }
    /* Record refusals: sutra[0]'s record with one byte replaced. */
    static const struct {
        uint32_t off;
        uint8_t byte;
    } patch[] = {{0, 'X'}, {ZXV_TREATY_REC_LEN - 1, 'X'}};
    printf("],\"reject_record\":[");
    for (size_t i = 0; i < sizeof patch / sizeof patch[0]; i++) {
        static uint8_t rec[ZXV_TREATY_REC_LEN];
        memcpy(rec, rec0, sizeof rec);
        rec[patch[i].off] = patch[i].byte;
        zxv_treaty *t;
        zxv_treaty_create(&t);
        uint32_t off = 0;
        int32_t fault = 0;
        int32_t st = zxv_treaty_parse_record(t, rec, sizeof rec, &off, &fault);
        printf("%s{\"base\":\"sutra[0].record\",\"patch_offset\":%u,\"patch_byte\":\"%02x\",\"status\":",
               i ? "," : "", patch[i].off, patch[i].byte);
        pst(st);
        printf(",\"fault\":%d,\"offset\":%u}", fault, off);
        zxv_treaty_destroy(t);
    }

    static const int64_t M = ZXV_TREATY_PIC18_MAX;
    static const struct {
        char op;
        int64_t a, b;
    } pic[] = {
        {'+', 1, 2}, {'+', 999999999999999998, 1}, {'+', M, 1}, {'+', -M, -1}, {'+', M + 1, 0},
        {'+', -5, 3}, {'-', 5, 7}, {'-', -M, 1}, {'-', M, -M}, {'-', 0, M},
        {'*', 123456789, 1000000000}, {'*', 1000000000, 1000000000}, {'*', -3, 7},
        {'*', 999999999, 1000000001}, {'*', 0, M}, {'/', -7, 2}, {'/', 7, -2}, {'/', 7, 2},
        {'/', -7, -2}, {'/', 7, 0}, {'/', M, -1}, {'/', 0, 5}, {'/', M + 1, 1},
    };
    printf("],\"pic\":[");
    for (size_t i = 0; i < sizeof pic / sizeof pic[0]; i++) {
        int64_t o = 0, r = 0;
        int32_t st;
        const char *nm;
        switch (pic[i].op) {
        case '+': st = zxv_treaty_pic_add(pic[i].a, pic[i].b, &o), nm = "add"; break;
        case '-': st = zxv_treaty_pic_sub(pic[i].a, pic[i].b, &o), nm = "sub"; break;
        case '*': st = zxv_treaty_pic_mul(pic[i].a, pic[i].b, &o), nm = "mul"; break;
        default: st = zxv_treaty_pic_div(pic[i].a, pic[i].b, &o, &r), nm = "div"; break;
        }
        printf("%s{\"op\":\"%s\",\"a\":", i ? "," : "", nm);
        pi(pic[i].a);
        printf(",\"b\":");
        pi(pic[i].b);
        printf(",\"status\":");
        pst(st);
        if (st == ZXV_OK) {
            printf(",\"out\":");
            pi(o);
            if (pic[i].op == '/') {
                printf(",\"rem\":");
                pi(r);
            }
        }
        printf("}");
    }

    static const char *runs[] = {T_BASE, T_TRI, T_NET, T_XAU, T_POOR};
    printf("],\"run\":[");
    for (size_t i = 0; i < sizeof runs / sizeof runs[0]; i++) {
        zxv_treaty *t;
        zxv_treaty_result *r = NULL;
        zxv_treaty_create(&t);
        zxv_treaty_parse_sutra(t, runs[i], NULL, NULL);
        int32_t st = zxv_treaty_run(t, &r);
        printf("%s{\"sutra\":", i ? "," : "");
        pjs(runs[i]);
        printf(",\"status\":");
        pst(st);
        if (st == ZXV_OK) {
            uint32_t np, nl;
            int64_t fees;
            zxv_treaty_result_summary(r, &np, &nl, &fees);
            printf(",\"parties\":%u,\"legs\":%u,\"fees\":", np, nl);
            pi(fees);
            printf(",\"party\":[");
            for (uint32_t p = 0; p < np; p++) {
                int64_t net, bal, div;
                zxv_treaty_result_party(r, p, &net, &bal, &div);
                printf("%s{\"net\":", p ? "," : "");
                pi(net);
                printf(",\"balance\":");
                pi(bal);
                printf(",\"dividend\":");
                pi(div);
                printf("}");
            }
            printf("],\"leg\":[");
            for (uint32_t l = 0; l < nl; l++) {
                uint32_t fr, to;
                int64_t amt;
                zxv_treaty_result_leg(r, l, &fr, &to, &amt);
                printf("%s{\"from\":%u,\"to\":%u,\"amount\":", l ? "," : "", fr, to);
                pi(amt);
                printf("}");
            }
            static uint8_t rr[ZXV_TREATY_RESULT_LEN];
            size_t n;
            zxv_treaty_result_record(r, rr, sizeof rr, &n);
            printf("],\"result_record\":");
            phex(rr, n);
            zxv_treaty_result_destroy(r);
        }
        printf("}");
        zxv_treaty_destroy(t);
    }
    printf("]}");
}

/* ------------------------------------------------------------------- cid */

static void cid_text(const char *key, const uint8_t *cid, size_t n, uint32_t base)
{
    char t[ZXV_CID_TEXT_MAX + 1];
    size_t tl;
    int32_t st = zxv_cid_to_text(cid, n, base, t, sizeof t, &tl);
    printf(",\"%s\":", key);
    if (st == ZXV_OK)
        pjs(t);
    else
        pst(st);
}

static void cid_info_out(const uint8_t *cid, size_t n)
{
    uint32_t v, c, h;
    uint8_t d[ZXV_CID_DIGEST_MAX];
    size_t dl;
    zxv_cid_info(cid, n, &v, &c, &h, d, &dl);
    printf(",\"version\":%u,\"codec\":%u,\"hash\":%u,\"digest\":", v, c, h);
    phex(d, dl);
}

/* Large test inputs are a formula, not bytes: byte i = i & 255. */
static uint8_t *ramp(size_t n)
{
    uint8_t *p = malloc(n ? n : 1);
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t) i;
    return p;
}

static void cid(void)
{
    static const char *data[] = {"", "abc", "hello world", "zxv conformance"};
    static const uint32_t codecs[] = {ZXV_CID_CODEC_RAW, ZXV_CID_CODEC_DAG_PB, ZXV_AIPI_MC_FN,
                                      ZXV_AIPI_MC_CALL};
    printf(",\"cid\":{\"note\":\"compute = CIDv1 sha2-256 of the bytes as one block; text_default is "
           "Kubo's form, text_58btc the z form. unixfs inputs with ramp=true are byte i = i & 255.\","
           "\"compute\":[");
    int f = 1;
    for (size_t i = 0; i < sizeof data / sizeof data[0]; i++)
        for (size_t c = 0; c < sizeof codecs / sizeof codecs[0]; c++) {
            uint8_t id[ZXV_CID_MAX];
            size_t n;
            int32_t st = zxv_cid_compute(codecs[c], (const uint8_t *) data[i], strlen(data[i]), id, &n);
            printf("%s{\"utf8\":", sep(&f));
            pjs(data[i]);
            printf(",\"codec\":%u,\"status\":", codecs[c]);
            pst(st);
            printf(",\"cid\":");
            phex(id, n);
            cid_text("text_default", id, n, ZXV_CID_BASE_DEFAULT);
            cid_text("text_58btc", id, n, ZXV_CID_BASE_58BTC);
            cid_info_out(id, n);
            printf("}");
        }

    static const size_t ulen[] = {0, 1, 262144, 262145, 1000000};
    printf("],\"unixfs\":[");
    for (size_t i = 0; i < sizeof ulen / sizeof ulen[0]; i++) {
        uint8_t *d = ramp(ulen[i]);
        uint8_t id[ZXV_CID_MAX];
        size_t n;
        int32_t st = zxv_cid_unixfs(d, ulen[i], id, &n);
        printf("%s{\"ramp\":true,\"len\":%zu,\"status\":", i ? "," : "", ulen[i]);
        pst(st);
        printf(",\"cid\":");
        phex(id, n);
        cid_text("text_default", id, n, ZXV_CID_BASE_DEFAULT);
        printf(",\"codec\":%u}", ulen[i] > 262144 ? ZXV_CID_CODEC_DAG_PB : ZXV_CID_CODEC_RAW);
        free(d);
    }
    printf(",{\"utf8\":\"hello world\",\"status\":");
    {
        uint8_t id[ZXV_CID_MAX];
        size_t n;
        pst(zxv_cid_unixfs((const uint8_t *) "hello world", 11, id, &n));
        printf(",\"cid\":");
        phex(id, n);
        cid_text("text_default", id, n, ZXV_CID_BASE_DEFAULT);
        printf("}");
    }

    static const char *texts[] = {
        "bafkreihdwdcefgh4dqkjv67uzcmw7ojee6xedzdetojuzjevtenxquvyku",
        "bafkreifjjcie6lypi6ny7amxnfftagclbuxndqonfipmb64f2km2devei4",
        "QmT78zSuBmuS4z925WZfrqQ1qHaJ56DQaTfyMUF7F8ff5o",
        "zb2rhe5P4gXftAwvA4eXQ5HJwsER2owDyS9sKaQRRVQPn93bA",
    };
    printf("],\"text\":[");
    for (size_t i = 0; i < sizeof texts / sizeof texts[0]; i++) {
        uint8_t id[ZXV_CID_MAX], v1[ZXV_CID_MAX];
        size_t n, n1;
        int32_t st = zxv_cid_from_text(texts[i], id, &n);
        printf("%s{\"text\":", i ? "," : "");
        pjs(texts[i]);
        printf(",\"status\":");
        pst(st);
        printf(",\"cid\":");
        phex(id, n);
        cid_text("text_default", id, n, ZXV_CID_BASE_DEFAULT);
        cid_text("text_58btc", id, n, ZXV_CID_BASE_58BTC);
        cid_info_out(id, n);
        zxv_cid_to_v1(id, n, v1, &n1);
        printf(",\"v1\":");
        phex(v1, n1);
        cid_text("v1_text", v1, n1, ZXV_CID_BASE_DEFAULT);
        uint8_t pk[32];
        zxv_aipi_provider_key(id, n, pk);
        printf(",\"provider_key\":");
        phex(pk, 32);
        printf("}");
    }
    static const char *badt[] = {
        "bafkreifjjcie6lypi6ny7amxnfftagclbuxndqonfipmb64f2km2devei",
        "bafkreifjjcie6lypi6ny7amxnfftagclbuxndqonfipmb64f2km2devei4a",
        "BAFKREIFJJCIE6LYPI6NY7AMXNFFTAGCLBUXNDQONFIPMB64F2KM2DEVEI4",
        "QmT78zSuBmuS4z925WZfrqQ1qHaJ56DQaTfyMUF7F8ff50",
        "z1b2rhe5P4gXftAwvA4eXQ5HJwsER2owDyS9sKaQRRVQPn93bA",
        "z",
        "",
    };
    printf("],\"text_reject\":[");
    for (size_t i = 0; i < sizeof badt / sizeof badt[0]; i++) {
        uint8_t id[ZXV_CID_MAX];
        size_t n;
        printf("%s{\"text\":", i ? "," : "");
        pjs(badt[i]);
        printf(",\"status\":");
        pst(zxv_cid_from_text(badt[i], id, &n));
        printf("}");
    }
    {
        uint8_t id[ZXV_CID_MAX];
        size_t n;
        zxv_cid_compute(ZXV_CID_CODEC_RAW, (const uint8_t *) "abc", 3, id, &n);
        printf("],\"verify\":[{\"cid\":");
        phex(id, n);
        printf(",\"utf8\":\"abc\",\"status\":");
        pst(zxv_cid_verify(id, n, (const uint8_t *) "abc", 3));
        printf("},{\"cid\":");
        phex(id, n);
        printf(",\"utf8\":\"abd\",\"status\":");
        pst(zxv_cid_verify(id, n, (const uint8_t *) "abd", 3));
        printf("},{\"cid\":");
        phex(id, n - 1);
        printf(",\"utf8\":\"abc\",\"status\":");
        pst(zxv_cid_verify(id, n - 1, (const uint8_t *) "abc", 3));
        printf("}]}");
    }
}

/* ------------------------------------------------------------------ aipi */

static void aipi(void)
{
    uint8_t mod[ZXV_CID_MAX], con[ZXV_CID_MAX];
    size_t mn, cn;
    zxv_cid_compute(ZXV_CID_CODEC_RAW, (const uint8_t *) "zxv module bytes", 16, mod, &mn);
    zxv_cid_compute(ZXV_CID_CODEC_RAW, (const uint8_t *) "zxv contract text", 17, con, &cn);
    static const struct {
        uint32_t it, ve, pr, de, qn, qk;
        uint64_t fuel;
        int contract;
        const char *entry, *checker, *certifier;
    } fn[] = {
        {0, 1, 0, 1, 0, 0, 1000000, 0, "main", "check", NULL},
        {1, 2, 1, 0, 0, 0, 99, 1, "factor", "verify", "cert"},
        {0, 3, 0, 1, 3, 2, 5, 0, "f", NULL, NULL},
        {2, 4, 0, 0, 0, 0, 7, 0, "run", NULL, NULL},
        {3, 4, 1, 1, 0, 0, UINT64_MAX, 1, "route_v2", NULL, NULL},
        /* refusals */
        {0, 3, 0, 0, 3, 2, 5, 0, "f", NULL, NULL},     /* QUORUM needs DETERMINISTIC */
        {0, 1, 0, 1, 0, 0, 1, 0, "e", "e", NULL},      /* checker == entry */
        {0, 4, 0, 1, 0, 0, 0, 0, "e", NULL, NULL},     /* fuel 0 */
        {4, 4, 0, 1, 0, 0, 1, 0, "e", NULL, NULL},     /* unknown intent */
        {0, 4, 0, 1, 0, 0, 1, 0, "bad-name", NULL, NULL},
    };
    printf(",\"aipi\":{\"module_utf8\":\"zxv module bytes\",\"module_cid\":");
    phex(mod, mn);
    printf(",\"contract_utf8\":\"zxv contract text\",\"contract_cid\":");
    phex(con, cn);
    printf(",\"fn\":[");
    uint8_t fcid0[ZXV_CID_MAX];
    size_t fcid0n = 0;
    for (size_t i = 0; i < sizeof fn / sizeof fn[0]; i++) {
        uint8_t rec[ZXV_AIPI_FN_MAX];
        size_t n = 0;
        int32_t st = zxv_aipi_fn_encode(fn[i].it, fn[i].ve, fn[i].pr, fn[i].de, fn[i].qn, fn[i].qk,
                                        fn[i].fuel, mod, mn, fn[i].contract ? con : NULL,
                                        fn[i].contract ? cn : 0, fn[i].entry, fn[i].checker,
                                        fn[i].certifier, rec, sizeof rec, &n);
        printf("%s{\"intent\":%u,\"verify\":%u,\"privacy\":%u,\"determinism\":%u,\"quorum_n\":%u,"
               "\"quorum_k\":%u,\"fuel_max\":",
               i ? "," : "", fn[i].it, fn[i].ve, fn[i].pr, fn[i].de, fn[i].qn, fn[i].qk);
        pu(fn[i].fuel);
        printf(",\"contract\":%s,\"entry\":", fn[i].contract ? "true" : "false");
        pjs(fn[i].entry);
        printf(",\"checker\":");
        if (fn[i].checker) pjs(fn[i].checker); else printf("null");
        printf(",\"certifier\":");
        if (fn[i].certifier) pjs(fn[i].certifier); else printf("null");
        printf(",\"status\":");
        pst(st);
        if (st == ZXV_OK) {
            uint8_t c[ZXV_CID_MAX];
            size_t cl;
            zxv_aipi_fn_cid(rec, n, c, &cl);
            if (i == 0) memcpy(fcid0, c, cl), fcid0n = cl;
            printf(",\"record\":");
            phex(rec, n);
            printf(",\"fn_cid\":");
            phex(c, cl);
            cid_text("fn_cid_text", c, cl, ZXV_CID_BASE_58BTC);
        }
        printf("}");
    }
    static const char *inputs[] = {"", "hello", "{\"n\":42}"};
    uint8_t ccid0[ZXV_CID_MAX];
    size_t ccid0n = 0;
    printf("],\"call\":[");
    for (size_t i = 0; i < sizeof inputs / sizeof inputs[0]; i++) {
        uint8_t rec[ZXV_AIPI_CALL_MAX], c[ZXV_CID_MAX];
        size_t n, cl;
        size_t il = strlen(inputs[i]);
        zxv_aipi_call_encode(fcid0, fcid0n, (const uint8_t *) inputs[i], il, rec, sizeof rec, &n);
        zxv_aipi_call_cid(fcid0, fcid0n, (const uint8_t *) inputs[i], il, c, &cl);
        if (i == 1) memcpy(ccid0, c, cl), ccid0n = cl;
        printf("%s{\"fn\":\"fn[0].fn_cid\",\"input_utf8\":", i ? "," : "");
        pjs(inputs[i]);
        printf(",\"record\":");
        phex(rec, n);
        printf(",\"call_cid\":");
        phex(c, cl);
        cid_text("call_cid_text", c, cl, ZXV_CID_BASE_58BTC);
        printf("}");
    }
    {
        uint8_t rec[ZXV_AIPI_CALL_MAX];
        size_t n;
        printf(",{\"fn\":\"module_cid\",\"input_utf8\":\"\",\"status\":");
        pst(zxv_aipi_call_encode(mod, mn, NULL, 0, rec, sizeof rec, &n));
        printf("}");
    }
    uint8_t ex[32];
    for (int i = 0; i < 32; i++) ex[i] = (uint8_t) i;
    printf("],\"claim\":[");
    {
        uint8_t rec[ZXV_AIPI_CLAIM_MAX];
        size_t n;
        zxv_aipi_claim_encode(ccid0, ccid0n, ex, (const uint8_t *) "42", 2, NULL, 0, rec, sizeof rec, &n);
        printf("{\"call\":\"call[1].call_cid\",\"executor\":");
        phex(ex, 32);
        printf(",\"output_utf8\":\"42\",\"cert_utf8\":\"\",\"record\":");
        phex(rec, n);
        zxv_aipi_claim_encode(ccid0, ccid0n, ex, (const uint8_t *) "42", 2, (const uint8_t *) "proof", 5,
                              rec, sizeof rec, &n);
        printf("},{\"call\":\"call[1].call_cid\",\"executor\":");
        phex(ex, 32);
        printf(",\"output_utf8\":\"42\",\"cert_utf8\":\"proof\",\"record\":");
        phex(rec, n);
        printf("}");
    }
    printf("]}");
}

/* ------------------------------------------------------------------ coin */

/* A holding as sparse [[tier,"count"],...] (tiers with count 0 left out). */
static void pcounts(const uint64_t c[64])
{
    printf("[");
    int f = 1;
    for (int k = 0; k < 64; k++)
        if (c[k]) {
            printf("%s[%d,", sep(&f), k);
            pu(c[k]);
            printf("]");
        }
    printf("]");
}

typedef struct {
    int n;
    uint32_t tier[4];
    uint64_t count[4];
} holding;

static void hold(const holding *h, uint64_t c[64])
{
    memset(c, 0, 64 * sizeof c[0]);
    for (int i = 0; i < h->n; i++) c[h->tier[i]] = h->count[i];
}

static void coin(void)
{
    printf(",\"coin\":{\"note\":\"holdings are sparse [[tier,count],...]; split rungs are given "
           "only when the split has at most 64 coins\",\"rungs\":%u,\"top_face\":",
           ZXV_COIN_RUNGS);
    pu(ZXV_COIN_TOP_FACE);
    static const uint32_t rungs[] = {0, 1, 2, 3, 4, 5, 6, 10, 20, 44, 45, 46, 1000};
    printf(",\"face\":[");
    for (size_t i = 0; i < sizeof rungs / sizeof rungs[0]; i++) {
        uint64_t face = 0;
        int32_t st = zxv_coin_face(rungs[i], &face);
        printf("%s{\"rung\":%u,\"status\":", i ? "," : "", rungs[i]);
        pst(st);
        if (st == ZXV_OK) printf(",\"face\":"), pu(face);
        printf("}");
    }
    static const uint64_t faces[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 13, 100, 144, 1134903170,
                                     1836311903, 1836311904, 2971215073u, UINT64_MAX};
    printf("],\"rung_of\":[");
    for (size_t i = 0; i < sizeof faces / sizeof faces[0]; i++) {
        uint32_t r = 0;
        int32_t st = zxv_coin_rung(faces[i], &r);
        printf("%s{\"face\":", i ? "," : "");
        pu(faces[i]);
        printf(",\"status\":");
        pst(st);
        if (st == ZXV_OK) printf(",\"rung\":%u", r);
        printf("}");
    }
    static const uint64_t amounts[] = {0, 1, 2, 4, 7, 12, 100, 1000, 1000000, 1836311902,
                                       1836311903, 1836311904, UINT64_C(5508935809) + 100,
                                       UINT64_C(1000000000000), UINT64_C(7000000000000000000),
                                       UINT64_MAX};
    printf("],\"split\":[");
    for (size_t i = 0; i < sizeof amounts / sizeof amounts[0]; i++) {
        size_t cnt = 0;
        int32_t st = zxv_coin_split(amounts[i], NULL, 0, &cnt);
        printf("%s{\"amount\":", i ? "," : "");
        pu(amounts[i]);
        if (st == ZXV_E_BUFFER || st == ZXV_OK) {
            printf(",\"count\":");
            pu(cnt);
            if (cnt <= 64) {
                uint32_t r[64];
                st = zxv_coin_split(amounts[i], r, 64, &cnt);
                printf(",\"status\":");
                pst(st);
                printf(",\"rungs\":[");
                for (size_t k = 0; k < cnt; k++) printf("%s%u", k ? "," : "", r[k]);
                printf("]");
            } else {
                printf(",\"status\":\"ZXV_OK\"");
            }
        } else {
            printf(",\"status\":");
            pst(st);
        }
        printf("}");
    }

    static const uint32_t tiers[] = {0, 1, 2, 9, 10, 11, 23, 62, 63, 64};
    printf("],\"token_face\":[");
    for (size_t i = 0; i < sizeof tiers / sizeof tiers[0]; i++) {
        uint64_t face = 0;
        int32_t sup = 0;
        int32_t st = zxv_token_face(tiers[i], &face, &sup);
        printf("%s{\"tier\":%u,\"status\":", i ? "," : "", tiers[i]);
        pst(st);
        if (st == ZXV_OK) printf(",\"face\":"), pu(face), printf(",\"super\":%s", sup ? "true" : "false");
        printf("}");
    }
    static const uint64_t tf[] = {1, 2, 4, 89, 144, 75025, UINT64_C(17167680177565), UINT64_C(27777890035288), 0};
    printf("],\"token_tier\":[");
    for (size_t i = 0; i < sizeof tf / sizeof tf[0]; i++) {
        uint32_t t = 0;
        int32_t st = zxv_token_tier(tf[i], &t);
        printf("%s{\"face\":", i ? "," : "");
        pu(tf[i]);
        printf(",\"status\":");
        pst(st);
        if (st == ZXV_OK) printf(",\"tier\":%u", t);
        printf("}");
    }
    static const holding hv[] = {
        {0, {0}, {0}},
        {1, {0}, {1000000}},
        {3, {0, 4, 10}, {3, 2, 1}},
        {2, {23, 0}, {13, 5}},
        {1, {63}, {1}},
        {1, {63}, {UINT64_C(1074559)}},
        {1, {63}, {UINT64_C(1074560)}},
        {2, {0, 1}, {UINT64_MAX, 1}},
    };
    printf("],\"token_value\":[");
    for (size_t i = 0; i < sizeof hv / sizeof hv[0]; i++) {
        uint64_t c[64], v = 0, p = 0;
        hold(&hv[i], c);
        int32_t st = zxv_token_value(c, &v, &p);
        printf("%s{\"counts\":", i ? "," : "");
        pcounts(c);
        printf(",\"status\":");
        pst(st);
        if (st == ZXV_OK) printf(",\"value\":"), pu(v), printf(",\"pieces\":"), pu(p);
        printf("}");
    }
    static const struct {
        uint64_t v;
        uint32_t o;
    } cv[] = {{0, 9},       {1, 0},        {100, 0},      {100, 4},          {100, 9},
              {1000000, 0}, {1000000, 4},  {1000000, 14}, {1000000, 23},     {1000000, 63},
              {UINT64_MAX, 63}, {UINT64_MAX, 23}, {12345, 64}};
    printf("],\"token_canonical\":[");
    for (size_t i = 0; i < sizeof cv / sizeof cv[0]; i++) {
        uint64_t c[64];
        int32_t st = zxv_token_canonical(cv[i].v, cv[i].o, c);
        printf("%s{\"value\":", i ? "," : "");
        pu(cv[i].v);
        printf(",\"octane\":%u,\"status\":", cv[i].o);
        pst(st);
        if (st == ZXV_OK) {
            uint64_t p = 0;
            zxv_token_value(c, NULL, &p);
            printf(",\"counts\":");
            pcounts(c);
            printf(",\"pieces\":");
            pu(p);
        }
        printf("}");
    }
    static const struct {
        holding h;
        uint32_t o;
    } ic[] = {
        {{1, {0}, {100}}, 0},
        {{1, {0}, {100}}, 4},
        {{3, {4, 2, 0}, {12, 1, 1}}, 4},
        {{2, {1, 2}, {1, 1}}, 4},
        {{1, {5}, {1}}, 4},
    };
    printf("],\"token_is_canonical\":[");
    for (size_t i = 0; i < sizeof ic / sizeof ic[0]; i++) {
        uint64_t c[64];
        int32_t can = -1;
        hold(&ic[i].h, c);
        int32_t st = zxv_token_is_canonical(c, ic[i].o, &can);
        printf("%s{\"counts\":", i ? "," : "");
        pcounts(c);
        printf(",\"octane\":%u,\"status\":", ic[i].o);
        pst(st);
        printf(",\"canonical\":%s}", can == 1 ? "true" : "false");
    }
    printf("],\"token_compress\":[");
    for (size_t i = 0; i < sizeof ic / sizeof ic[0]; i++) {
        uint64_t c[64];
        hold(&ic[i].h, c);
        printf("%s{\"counts\":", i ? "," : "");
        pcounts(c);
        uint32_t o = ic[i].o == 0 ? 9 : 0;
        int32_t st = zxv_token_compress(c, o);
        printf(",\"octane\":%u,\"status\":", o);
        pst(st);
        printf(",\"out\":");
        pcounts(c);
        printf("}");
    }
    static const struct {
        char op;
        holding h;
        uint32_t o, t;
    } fs[] = {
        {'f', {2, {0, 1}, {1, 1}}, 4, 0},
        {'f', {2, {3, 4}, {2, 5}}, 9, 3},
        {'f', {2, {3, 4}, {2, 5}}, 4, 3},  /* tier 5 above octane 4: STATE */
        {'f', {1, {0}, {5}}, 9, 0},        /* no tier-1 token: FUNDS */
        {'f', {2, {62, 63}, {1, 1}}, 63, 62},
        {'f', {2, {0, 1}, {1, 1}}, 64, 0},
        {'s', {1, {1}, {1}}, 0, 1},
        {'s', {1, {10}, {3}}, 0, 10},
        {'s', {1, {0}, {1}}, 0, 0},
        {'s', {1, {5}, {0}}, 0, 5},
        {'s', {2, {7, 6}, {1, UINT64_MAX}}, 0, 7},
    };
    printf("],\"token_fuse_split\":[");
    for (size_t i = 0; i < sizeof fs / sizeof fs[0]; i++) {
        uint64_t c[64];
        hold(&fs[i].h, c);
        printf("%s{\"op\":\"%s\",\"counts\":", i ? "," : "", fs[i].op == 'f' ? "fuse" : "split");
        pcounts(c);
        int32_t st = fs[i].op == 'f' ? zxv_token_fuse(c, fs[i].o, fs[i].t) : zxv_token_split(c, fs[i].t);
        if (fs[i].op == 'f') printf(",\"octane\":%u", fs[i].o);
        printf(",\"tier\":%u,\"status\":", fs[i].t);
        pst(st);
        printf(",\"out\":");
        pcounts(c);
        printf("}");
    }
    static const uint64_t codev[] = {0, 1, 2, 3, 4, 5, 6, 7, 100, 4294967296u, UINT64_MAX - 1, UINT64_MAX};
    printf("],\"token_code\":[");
    for (size_t i = 0; i < sizeof codev / sizeof codev[0]; i++) {
        uint8_t buf[16] = {0};
        uint32_t bit = 0, nb = 0;
        int32_t sb = zxv_token_code_bits(codev[i], &nb);
        int32_t st = zxv_token_code_put(buf, sizeof buf, &bit, codev[i]);
        printf("%s{\"value\":", i ? "," : "");
        pu(codev[i]);
        printf(",\"status\":");
        pst(st);
        if (st == ZXV_OK && sb == ZXV_OK) {
            printf(",\"bits\":%u,\"code\":\"", nb);
            for (uint32_t b = 0; b < bit; b++) putchar('0' + ((buf[b / 8] >> (b % 8)) & 1));
            printf("\",\"bytes\":");
            phex(buf, (bit + 7) / 8);
        }
        printf("}");
    }
    {
        static const uint64_t seq[] = {0, 1, 100, 7, UINT64_MAX - 1, 12};
        uint8_t buf[64] = {0};
        uint32_t bit = 0;
        for (size_t i = 0; i < sizeof seq / sizeof seq[0]; i++) zxv_token_code_put(buf, sizeof buf, &bit, seq[i]);
        printf("],\"token_code_stream\":{\"values\":[");
        for (size_t i = 0; i < sizeof seq / sizeof seq[0]; i++) printf("%s", i ? "," : ""), pu(seq[i]);
        printf("],\"bits\":%u,\"bytes\":", bit);
        phex(buf, (bit + 7) / 8);
        printf("},\"token_code_get_reject\":[");
        static const uint8_t z[2] = {0, 0}, o1[1] = {0x01};
        uint32_t b0 = 0;
        uint64_t v;
        printf("{\"bytes\":\"0000\",\"bit\":0,\"status\":");
        pst(zxv_token_code_get(z, 2, &b0, &v));
        b0 = 0;
        printf("},{\"bytes\":\"01\",\"bit\":0,\"status\":");
        pst(zxv_token_code_get(o1, 1, &b0, &v));
        b0 = 9;
        printf("},{\"bytes\":\"01\",\"bit\":9,\"status\":");
        pst(zxv_token_code_get(o1, 1, &b0, &v));
        printf("}]}");
    }
}

/* ------------------------------------------------------------------- fee */

static void fee(void)
{
    static const uint64_t g[] = {0, 1, 2, 1124, 1125, 2249, 2250, 3374, 3375, 10000, 1000000,
                                 1000000000, UINT64_C(1000000000000), UINT64_C(9007199254740993),
                                 UINT64_C(1000000000000000000), UINT64_MAX / 8889, UINT64_MAX};
    printf(",\"fee\":{\"num\":%u,\"den\":%u,\"assure\":[", ZXV_FEE_NUM, ZXV_FEE_DEN);
    for (size_t i = 0; i < sizeof g / sizeof g[0]; i++) {
        uint64_t f = 0;
        zxv_fee_assure(g[i], &f);
        printf("%s{\"gross\":", i ? "," : "");
        pu(g[i]);
        printf(",\"fee\":");
        pu(f);
        printf("}");
    }
    static const struct {
        uint64_t rem0;
        int n;
        uint64_t g[8];
    } ch[] = {
        {0, 8, {100, 100, 100, 100, 100, 100, 100, 100}},
        {0, 7, {1, 1124, 1, 1, 5000000, 999, 123456789}},
        {9999999, 4, {1, 0, 1125, UINT64_MAX}},
        {5000000, 3, {562, 563, 1}},
    };
    printf("],\"carry\":[");
    for (size_t i = 0; i < sizeof ch / sizeof ch[0]; i++) {
        uint64_t r = ch[i].rem0;
        printf("%s{\"rem_in\":", i ? "," : "");
        pu(r);
        printf(",\"steps\":[");
        for (int k = 0; k < ch[i].n; k++) {
            uint64_t f = 0, r2 = 0;
            int32_t st = zxv_fee_assure_carry(r, ch[i].g[k], &f, &r2);
            printf("%s{\"gross\":", k ? "," : "");
            pu(ch[i].g[k]);
            printf(",\"status\":");
            pst(st);
            printf(",\"fee\":");
            pu(f);
            printf(",\"rem\":");
            pu(r2);
            printf("}");
            r = r2;
        }
        printf("]}");
    }
    {
        uint64_t f = 7, r = 7;
        printf("],\"carry_reject\":[{\"rem_in\":\"10000000\",\"gross\":\"1\",\"status\":");
        pst(zxv_fee_assure_carry(ZXV_FEE_DEN, 1, &f, &r));
        printf("},{\"rem_in\":\"18446744073709551615\",\"gross\":\"0\",\"status\":");
        pst(zxv_fee_assure_carry(UINT64_MAX, 0, &f, &r));
        printf("}]");
    }
    static const uint64_t fs[] = {0, 1, 2, 3, 4, 7, 9, 10, 11, 19, 20, 99, 100, 101, 12345,
                                  UINT64_C(1000000000000000001), UINT64_MAX};
    printf(",\"buckets\":[\"reserve_floor\",\"vbill_dividend\",\"infra_bounty\",\"regen_capital\"],"
           "\"split\":[");
    for (size_t i = 0; i < sizeof fs / sizeof fs[0]; i++) {
        uint64_t b[4] = {0};
        zxv_fee_split(fs[i], b);
        printf("%s{\"fee\":", i ? "," : "");
        pu(fs[i]);
        printf(",\"parts\":[");
        for (int k = 0; k < 4; k++) printf("%s", k ? "," : ""), pu(b[k]);
        printf("]}");
    }
    uint32_t pct[4];
    int32_t below;
    uint64_t rn[4], rd[4], mn, md, cn, cd;
    zxv_fee_policy_default(pct, &below, rn, rd, &mn, &md, &cn, &cd);
    printf("],\"policy_default\":{\"pct\":[%u,%u,%u,%u],\"allow_below\":%d,\"rate\":[", pct[0], pct[1],
           pct[2], pct[3], below);
    for (int k = 0; k < 4; k++) printf("%s[", k ? "," : ""), pu(rn[k]), printf(","), pu(rd[k]), printf("]");
    printf("],\"multiplier\":[");
    pu(mn), printf(","), pu(md);
    printf("],\"commons_cap\":[");
    pu(cn), printf(","), pu(cd);
    printf("]}}");
}

/* ------------------------------------------------------------------ vhub */

static uint8_t vpk[4][ZXV_MLDSA87_PK_BYTES], vsk[4][ZXV_MLDSA87_SK_BYTES];

static int32_t vpeg(zxv_cap *cap, zxv_vhub *h, uint8_t slot, uint64_t num, uint64_t den,
                    uint64_t epoch, int key, int show)
{
    static uint8_t sig[ZXV_MLDSA87_SIG_BYTES];
    uint8_t m[40], hs[32];
    zxv_vhub_peg_msg(slot, num, den, epoch, m);
    zxv_mldsa87_sign_deterministic(cap, vsk[key], m, sizeof m, NULL, 0, sig);
    int32_t st = zxv_vhub_set_peg(cap, h, slot, num, den, epoch, sig);
    printf("%s{\"slot\":%u,\"num\":", show ? "" : ",", slot);
    pu(num);
    printf(",\"den\":");
    pu(den);
    printf(",\"epoch\":");
    pu(epoch);
    printf(",\"signer\":%d,\"sig_sha3_256\":", key);
    zxv_sha3_256(sig, sizeof sig, hs);
    phex(hs, 32);
    printf(",\"status\":");
    pst(st);
    printf("}");
    return st;
}

static void vhub(zxv_cap *cap)
{
    static const struct {
        uint8_t slot;
        uint64_t num, den, epoch;
    } pm[] = {{0, 350, 1, 1}, {2, 9, 4, 7}, {7, UINT64_MAX, 1, UINT64_MAX}, {255, 1, UINT64_MAX, 0}};
    printf(",\"vhub\":{\"peg_msg\":[");
    for (size_t i = 0; i < sizeof pm / sizeof pm[0]; i++) {
        uint8_t m[40];
        zxv_vhub_peg_msg(pm[i].slot, pm[i].num, pm[i].den, pm[i].epoch, m);
        printf("%s{\"slot\":%u,\"num\":", i ? "," : "", pm[i].slot);
        pu(pm[i].num);
        printf(",\"den\":");
        pu(pm[i].den);
        printf(",\"epoch\":");
        pu(pm[i].epoch);
        printf(",\"msg\":");
        phex(m, 40);
        printf("}");
    }
    printf("]");
    uint32_t f2 = 0;
    zxv_features2(&f2);
    if (!(f2 & ZXV_FEAT2_VHUB)) {
        printf("}");
        return;
    }
    uint8_t hs[32];
    printf(",\"scenario\":{\"note\":\"issuer keys: zxv_mldsa87_keygen_seed with seed = 32 bytes of "
           "0x40+k; peg signatures: zxv_mldsa87_sign_deterministic, empty context. Steps run in "
           "order on one hub; accounts are numbered in order of creation.\",\"keys\":[");
    for (int k = 0; k < 4; k++) {
        uint8_t seed[32];
        memset(seed, 0x40 + k, sizeof seed);
        zxv_mldsa87_keygen_seed(cap, seed, vpk[k], vsk[k]);
        zxv_sha3_256(vpk[k], sizeof vpk[k], hs);
        printf("%s{\"seed\":", k ? "," : "");
        phex(seed, 32);
        printf(",\"pk_sha3_256\":");
        phex(hs, 32);
        printf("}");
    }
    static const uint8_t hseed[32] = {1, 2, 3};
    zxv_vhub *h;
    zxv_vhub_create(cap, hseed, &h);
    printf("],\"hub_seed\":");
    phex(hseed, 32);
    static const char *code[4] = {"ZTA", "ZTB", "ZTC", "ZTD"};
    printf(",\"currencies\":[");
    for (uint8_t c = 0; c < 4; c++) {
        int32_t st = zxv_vhub_add_ccy(cap, h, c, code[c], 2, vpk[c], 500000, 10000);
        uint32_t ia, pa, va;
        zxv_vhub_ccy_info(cap, h, c, &ia, &pa, &va, NULL, NULL, NULL);
        printf("%s{\"slot\":%u,\"code\":\"%s\",\"minor_units\":2,\"issuer_key\":%u,\"pool_funding\":"
               "\"500000\",\"vbill_backing\":\"10000\",\"status\":",
               c ? "," : "", c, code[c], c);
        pst(st);
        printf(",\"issuer_account\":%u,\"pool_account\":%u,\"vfloat_account\":%u}", ia, pa, va);
    }
    printf(",{\"slot\":4,\"code\":\"USD\",\"status\":");
    pst(zxv_vhub_add_ccy(cap, h, 4, "USD", 2, vpk[0], 0, 0));
    printf("},{\"slot\":4,\"code\":\"ZTA\",\"status\":");
    pst(zxv_vhub_add_ccy(cap, h, 4, "ZTA", 2, vpk[0], 0, 0));
    printf("}],\"pegs\":[");
    vpeg(cap, h, 0, 350, 1, 1, 0, 1);
    vpeg(cap, h, 1, 120, 1, 1, 1, 0);
    vpeg(cap, h, 2, 9, 4, 1, 2, 0);
    vpeg(cap, h, 0, 351, 1, 2, 1, 0); /* another issuer's key */
    vpeg(cap, h, 0, 351, 1, 1, 0, 0); /* stale epoch */
    vpeg(cap, h, 0, 0, 1, 2, 0, 0);   /* zero term */
    uint32_t alice, bob, carol, dave;
    zxv_vhub_open_account(cap, h, 0, 50, &alice);
    zxv_vhub_open_account(cap, h, 1, 51, &bob);
    zxv_vhub_open_account(cap, h, 2, 52, &carol);
    zxv_vhub_open_account(cap, h, 3, 53, &dave);
    zxv_vhub_issue(cap, h, alice, 1000000);
    printf("],\"accounts\":[{\"slot\":0,\"owner\":50,\"account\":%u,\"issued\":\"1000000\"},"
           "{\"slot\":1,\"owner\":51,\"account\":%u},{\"slot\":2,\"owner\":52,\"account\":%u},"
           "{\"slot\":3,\"owner\":53,\"account\":%u}]",
           alice, bob, carol, dave);
    static const struct {
        uint8_t from, to;
        uint64_t want;
    } q[] = {{0, 1, 35000}, {0, 1, 35001}, {0, 1, 349}, {0, 2, 3500}, {0, 2, 14000}, {2, 0, 100},
             {1, 2, 1000}, {0, 3, 35000}, {0, 0, 1}};
    printf(",\"quotes\":[");
    for (size_t i = 0; i < sizeof q / sizeof q[0]; i++) {
        uint64_t a = 0, v = 0, r = 0;
        int32_t st = zxv_vhub_quote(cap, h, q[i].from, q[i].to, q[i].want, &a, &v, &r);
        printf("%s{\"from\":%u,\"to\":%u,\"want\":", i ? "," : "", q[i].from, q[i].to);
        pu(q[i].want);
        printf(",\"status\":");
        pst(st);
        if (st == ZXV_OK) {
            printf(",\"amount\":");
            pu(a);
            printf(",\"vbills\":");
            pu(v);
            printf(",\"received\":");
            pu(r);
        }
        printf("}");
    }
    /* Swaps before any rule fail closed; then each currency gets a limit. */
    static const struct {
        int rule; /* -1: none; else add a limit on slot `rule` of `limit` before this step */
        uint64_t limit;
        uint8_t from, to;
        int sender, receiver; /* 0 alice 1 bob 2 carol 3 dave */
        uint64_t amount, tick;
    } sw[] = {
        {-1, 0, 0, 1, 0, 1, 35000, 1},
        {0, 105000, 0, 1, 0, 1, 35000, 1},
        {1, UINT64_MAX, 0, 1, 0, 1, 35000, 1},
        {2, UINT64_MAX, 0, 1, 0, 1, 35001, 2},
        {-1, 0, 0, 2, 0, 2, 3500, 3},
        {-1, 0, 0, 2, 0, 2, 14000, 4},
        {-1, 0, 1, 0, 1, 0, 12000, 5},
        {-1, 0, 0, 1, 0, 1, 70000, 6},
        {-1, 0, 0, 1, 0, 1, 350, 7},
        {-1, 0, 0, 3, 0, 3, 350, 8},
        {-1, 0, 0, 1, 0, 2, 350, 9},
        {-1, 0, 2, 0, 2, 0, 90, 10},
        {-1, 0, 1, 0, 1, 0, 240, 11},
    };
    uint32_t acct[4] = {alice, bob, carol, dave};
    printf("],\"swaps\":[");
    for (size_t i = 0; i < sizeof sw / sizeof sw[0]; i++) {
        if (sw[i].rule >= 0) zxv_vhub_add_limit(cap, h, (uint8_t) sw[i].rule, sw[i].limit);
        uint64_t v = 0, r = 0;
        int32_t st = zxv_vhub_swap(cap, h, sw[i].from, sw[i].to, acct[sw[i].sender],
                                   acct[sw[i].receiver], sw[i].amount, sw[i].tick, &v, &r);
        printf("%s{", i ? "," : "");
        if (sw[i].rule >= 0) {
            printf("\"add_limit\":{\"slot\":%d,\"limit\":", sw[i].rule);
            pu(sw[i].limit);
            printf("},");
        }
        printf("\"from\":%u,\"to\":%u,\"sender\":%u,\"receiver\":%u,\"amount\":", sw[i].from, sw[i].to,
               acct[sw[i].sender], acct[sw[i].receiver]);
        pu(sw[i].amount);
        printf(",\"tick\":");
        pu(sw[i].tick);
        printf(",\"status\":");
        pst(st);
        if (st == ZXV_OK) {
            printf(",\"vbills\":");
            pu(v);
            printf(",\"received\":");
            pu(r);
        }
        printf("}");
    }
    uint64_t posts = 0;
    int32_t cst = zxv_vhub_check(cap, h, &posts);
    printf("],\"check\":");
    pst(cst);
    printf(",\"postings\":");
    pu(posts);
    printf(",\"balances\":[");
    for (uint32_t a = 0;; a++) {
        uint64_t d, c;
        int64_t e;
        if (zxv_vhub_balance(cap, h, a, &d, &c, &e) != ZXV_OK) break;
        printf("%s{\"account\":%u,\"debit\":", a ? "," : "", a);
        pu(d);
        printf(",\"credit\":");
        pu(c);
        printf(",\"equity\":");
        pi(e);
        printf("}");
    }
    printf("]}}");
    zxv_vhub_destroy(h);
}

/* --------------------------------------------------------------- freight */

static void freight_one(int first, const char *label, uint64_t id, uint8_t k, const uint8_t *p,
                        size_t len)
{
    uint8_t hdr[64];
    static uint8_t pk[ZXV_FREIGHT_BYTES], sub[ZXV_FREIGHT_BYTES], out[ZXV_FREIGHT_MAX_PAYLOAD];
    zxv_freight_header_init(id, k, 0, 0, p, len, hdr);
    zxv_freight_encode(hdr, p, len, pk);
    printf("%s{\"label\":\"%s\",\"freight_id\":", first ? "" : ",", label);
    pu(id);
    printf(",\"k\":%u,\"payload\":", k);
    phex(p, len);
    printf(",\"header\":");
    phex(hdr, 64);
    printf(",\"packets\":");
    phex(pk, sizeof pk);
    /* decodes from row subsets: {first row, step, count}, then a flipped byte */
    const struct {
        uint32_t start, step, count;
        int flip;
    } ss[] = {{0, 1, k, 0}, {168 - k, 1, k, 0}, {1, 2, k, 0}, {0, 1, (uint32_t) k - 1, 0},
              {167, 0, 3, 0}, {k, 1, k, 1}};
    printf(",\"decode\":[");
    for (size_t i = 0; i < sizeof ss / sizeof ss[0]; i++) {
        size_t n = 0;
        printf("%s{\"rows\":[", i ? "," : "");
        for (uint32_t r = 0; r < ss[i].count; r++) {
            uint32_t row = (ss[i].start + r * ss[i].step) % 168;
            printf("%s%u", r ? "," : "", row);
            memcpy(sub + 21 * r, pk + 21 * row, 21);
        }
        printf("]");
        if (ss[i].flip) {
            sub[5] ^= 0x01;
            printf(",\"flip\":{\"index_in_rows\":0,\"byte\":5,\"xor\":\"01\"}");
        }
        size_t ol = 0;
        int32_t st = zxv_freight_decode(hdr, sub, 21 * (size_t) ss[i].count, out, sizeof out, &ol);
        (void) n;
        printf(",\"status\":");
        pst(st);
        if (st == ZXV_OK) printf(",\"matches_payload\":%s", ol == len && !memcmp(out, p, len) ? "true" : "false");
        printf("}");
    }
    printf("]}");
}

static void freight(void)
{
    uint8_t sha[32];
    memset(sha, 0xAB, sizeof sha);
    static const struct {
        uint8_t k, flags;
        uint16_t plen, seq, cnt;
        uint64_t id, lim;
    } hs[] = {{10, 0, 5, 0, 1, UINT64_C(0x0102030405060708), 0},
              {168, 1, 3360, 2, 3, UINT64_MAX, 1000000},
              {1, 0, 20, 16383, 16384, 0, 0},
              {0, 0, 0, 0, 1, 0, 0},
              {10, 0, 201, 0, 1, 0, 0},
              {10, 0, 5, 1, 1, 0, 0},
              {10, 0, 5, 0, 1, 0, 7}};
    printf(",\"freight\":{\"note\":\"packets are the 168 rows of 21 bytes, in order; decode rows are "
           "row indices into them. ramp payloads are byte i = i & 255.\",\"header_serialize\":[");
    for (size_t i = 0; i < sizeof hs / sizeof hs[0]; i++) {
        uint8_t h[64];
        int32_t st = zxv_freight_header_serialize(hs[i].k, hs[i].flags, hs[i].plen, hs[i].seq, hs[i].cnt,
                                                  hs[i].id, hs[i].lim, sha, h);
        printf("%s{\"k\":%u,\"flags\":%u,\"payload_len\":%u,\"seq\":%u,\"seq_count\":%u,\"freight_id\":",
               i ? "," : "", hs[i].k, hs[i].flags, hs[i].plen, hs[i].seq, hs[i].cnt);
        pu(hs[i].id);
        printf(",\"expand_limit\":");
        pu(hs[i].lim);
        printf(",\"payload_sha256\":");
        phex(sha, 32);
        printf(",\"status\":");
        pst(st);
        if (st == ZXV_OK) printf(",\"header\":"), phex(h, 64);
        printf("}");
    }
    static const char msg[] = "ZXV freight conformance: any k rows rebuild the payload.";
    uint8_t *r = ramp(200);
    printf("],\"codec\":[");
    freight_one(1, "utf8 message, k=10", 42, 10, (const uint8_t *) msg, sizeof msg - 1);
    freight_one(0, "ramp 200, k=10", 43, 10, r, 200);
    freight_one(0, "ramp 20, k=1", 44, 1, r, 20);
    free(r);
    {
        uint8_t h[64];
        printf("],\"header_init_reject\":[{\"k\":10,\"len\":201,\"status\":");
        uint8_t *rr = ramp(201);
        pst(zxv_freight_header_init(1, 10, 0, 0, rr, 201, h));
        printf("},{\"k\":0,\"len\":1,\"status\":");
        pst(zxv_freight_header_init(1, 0, 0, 0, rr, 1, h));
        printf("},{\"k\":10,\"len\":1,\"expand_limit\":\"5\",\"flags\":0,\"status\":");
        pst(zxv_freight_header_init(1, 10, 0, 5, rr, 1, h));
        printf("}]");
        free(rr);
    }
    static const struct {
        uint64_t len;
        uint8_t k;
    } sc[] = {{0, 1}, {1, 1}, {20, 1}, {21, 1}, {3360, 168}, {3361, 168}, {100, 2},
              {UINT64_C(55050240), 168}, {UINT64_C(55050241), 168}, {327680, 1}, {327681, 1}};
    printf(",\"set_count\":[");
    for (size_t i = 0; i < sizeof sc / sizeof sc[0]; i++) {
        uint32_t c = 0;
        int32_t st = zxv_freight_set_count(sc[i].len, sc[i].k, &c);
        printf("%s{\"len\":", i ? "," : "");
        pu(sc[i].len);
        printf(",\"k\":%u,\"status\":", sc[i].k);
        pst(st);
        if (st == ZXV_OK) printf(",\"count\":%u", c);
        printf("}");
    }
    {
        uint8_t *d = ramp(100);
        static uint8_t pk[ZXV_FREIGHT_BYTES];
        uint8_t h[64], hh[32];
        printf("],\"split\":{\"ramp\":true,\"len\":100,\"k\":2,\"freight_id\":\"77\",\"members\":[");
        for (uint32_t s = 0; s < 4; s++) {
            int32_t st = zxv_freight_split(77, 2, 0, 0, d, 100, s, h, pk);
            printf("%s{\"seq\":%u,\"status\":", s ? "," : "", s);
            pst(st);
            if (st == ZXV_OK) {
                printf(",\"header\":");
                phex(h, 64);
                zxv_sha3_256(pk, sizeof pk, hh);
                printf(",\"packets_sha3_256\":");
                phex(hh, 32);
            }
            printf("}");
        }
        printf("]}");
        free(d);
    }
    /* op streams */
    static const char *encin[] = {"", "hello", "abcabcabcabcabcabcabcabcabcabcabcabc",
                                  "the quick brown fox jumps over the lazy dog; the quick brown fox"};
    printf(",\"ops_encode\":[");
    int f = 1;
    for (size_t i = 0; i < sizeof encin / sizeof encin[0]; i++)
        for (int ws = 0; ws < 2; ws++) {
            uint8_t buf[512];
            size_t n = 0;
            size_t il = strlen(encin[i]);
            int32_t st = zxv_freight_ops_encode((const uint8_t *) encin[i], il, ws, buf, sizeof buf, &n);
            uint64_t bound = 0;
            zxv_freight_ops_bound(il, ws, &bound);
            printf("%s{\"utf8\":", sep(&f));
            pjs(encin[i]);
            printf(",\"with_sha\":%s,\"status\":", ws ? "true" : "false");
            pst(st);
            printf(",\"ops\":");
            phex(buf, n);
            printf(",\"bound\":");
            pu(bound);
            uint8_t out[256];
            size_t ol = 0;
            int32_t es = zxv_freight_ops_expand(buf, n, 256, NULL, NULL, out, sizeof out, &ol);
            printf(",\"expand_status\":");
            pst(es);
            printf("}");
        }
    /* writer: one of each op kind except CIDREF, then a CIDREF stream */
    printf("],\"ops_writer\":[");
    {
        static const uint8_t xs[8] = {1, 0, 0, 0, 0, 0, 0, 0};
        static const uint8_t sp[3] = {3, '#', '.'};
        zxv_freight_ops_writer *w;
        uint64_t total = 2 + 10 + 7 + 13 + 64 + 16;
        zxv_freight_ops_writer_create(total, NULL, 4096, &w);
        int32_t s1 = zxv_freight_ops_put_literal(w, (const uint8_t *) "ab", 2);
        int32_t s2 = zxv_freight_ops_put_copy(w, 2, 10);
        int32_t s3 = zxv_freight_ops_put_seed(w, ZXV_FREIGHT_GEN_REPEAT, (const uint8_t *) "xyz", 3, 7);
        int32_t s4 = zxv_freight_ops_put_seed(w, ZXV_FREIGHT_GEN_FIBWORD, (const uint8_t *) "ab", 2, 13);
        int32_t s5 = zxv_freight_ops_put_seed(w, ZXV_FREIGHT_GEN_SIERPINSKI, sp, 3, 64);
        int32_t s6 = zxv_freight_ops_put_seed(w, ZXV_FREIGHT_GEN_XORSHIFT, xs, 8, 16);
        uint8_t buf[512], out[512];
        size_t n = 0, ol = 0;
        int32_t sf = zxv_freight_ops_writer_finish(w, buf, sizeof buf, &n);
        zxv_freight_ops_writer_destroy(w);
        printf("{\"declared_total\":");
        pu(total);
        printf(",\"ops_list\":[{\"op\":\"literal\",\"utf8\":\"ab\",\"status\":");
        pst(s1);
        printf("},{\"op\":\"copy\",\"distance\":\"2\",\"len\":\"10\",\"status\":");
        pst(s2);
        printf("},{\"op\":\"seed\",\"gen\":%d,\"params\":\"78797a\",\"len\":\"7\",\"status\":",
               ZXV_FREIGHT_GEN_REPEAT);
        pst(s3);
        printf("},{\"op\":\"seed\",\"gen\":%d,\"params\":\"6162\",\"len\":\"13\",\"status\":",
               ZXV_FREIGHT_GEN_FIBWORD);
        pst(s4);
        printf("},{\"op\":\"seed\",\"gen\":%d,\"params\":\"03232e\",\"len\":\"64\",\"status\":",
               ZXV_FREIGHT_GEN_SIERPINSKI);
        pst(s5);
        printf("},{\"op\":\"seed\",\"gen\":%d,\"params\":\"0100000000000000\",\"len\":\"16\",\"status\":",
               ZXV_FREIGHT_GEN_XORSHIFT);
        pst(s6);
        printf("}],\"finish_status\":");
        pst(sf);
        printf(",\"ops\":");
        phex(buf, n);
        int32_t es = zxv_freight_ops_expand(buf, n, 1024, NULL, NULL, out, sizeof out, &ol);
        printf(",\"expand_status\":");
        pst(es);
        printf(",\"expanded\":");
        phex(out, ol);
        uint64_t pt = 0;
        int32_t hsh = -1;
        zxv_freight_ops_peek(buf, n, &pt, &hsh);
        printf(",\"peek_total\":");
        pu(pt);
        printf(",\"peek_has_sha\":%s", hsh ? "true" : "false");
        printf(",\"expand_limit_too_small\":");
        pst(zxv_freight_ops_expand(buf, n, total - 1, NULL, NULL, out, sizeof out, &ol));
        printf(",\"expand_truncated\":");
        pst(zxv_freight_ops_expand(buf, n - 1, 1024, NULL, NULL, out, sizeof out, &ol));
        printf("}");
    }
    {
        uint8_t c[ZXV_CID_MAX];
        size_t cl;
        zxv_cid_compute(ZXV_CID_CODEC_RAW, (const uint8_t *) "abc", 3, c, &cl);
        zxv_freight_ops_writer *w;
        zxv_freight_ops_writer_create(5, NULL, 4096, &w);
        zxv_freight_ops_put_literal(w, (const uint8_t *) "<", 1);
        zxv_freight_ops_put_cidref(w, c, cl, 3);
        zxv_freight_ops_put_literal(w, (const uint8_t *) ">", 1);
        uint8_t buf[256], out[16];
        size_t n = 0, ol = 0;
        zxv_freight_ops_writer_finish(w, buf, sizeof buf, &n);
        zxv_freight_ops_writer_destroy(w);
        printf(",{\"declared_total\":\"5\",\"ops_list\":[{\"op\":\"literal\",\"utf8\":\"<\"},"
               "{\"op\":\"cidref\",\"cid\":");
        phex(c, cl);
        printf(",\"len\":\"3\",\"resolves_to_utf8\":\"abc\"},{\"op\":\"literal\",\"utf8\":\">\"}],"
               "\"ops\":");
        phex(buf, n);
        printf(",\"expand_without_resolver\":");
        pst(zxv_freight_ops_expand(buf, n, 1024, NULL, NULL, out, sizeof out, &ol));
        printf(",\"expanded_with_resolver_utf8\":\"<abc>\"}");
    }
    static const struct {
        uint8_t gen;
        uint8_t plen;
        uint8_t p[8];
        size_t len;
    } gr[] = {{ZXV_FREIGHT_GEN_XORSHIFT, 8, {1}, 24},
              {ZXV_FREIGHT_GEN_XORSHIFT, 8, {0xEF, 0xBE, 0xAD, 0xDE}, 16},
              {ZXV_FREIGHT_GEN_REPEAT, 2, {0xAA, 0x55}, 9},
              {ZXV_FREIGHT_GEN_FIBWORD, 2, {'a', 'b'}, 21},
              {ZXV_FREIGHT_GEN_SIERPINSKI, 3, {2, 1, 0}, 32},
              {ZXV_FREIGHT_GEN_XORSHIFT, 8, {0}, 4},
              {ZXV_FREIGHT_GEN_FIBWORD, 2, {'a', 'a'}, 4},
              {9, 1, {0}, 4}};
    printf("],\"gen_run\":[");
    for (size_t i = 0; i < sizeof gr / sizeof gr[0]; i++) {
        uint8_t out[64];
        int32_t st = zxv_freight_gen_run(gr[i].gen, gr[i].p, gr[i].plen, out, gr[i].len);
        printf("%s{\"gen\":%u,\"params\":", i ? "," : "", gr[i].gen);
        phex(gr[i].p, gr[i].plen);
        printf(",\"len\":%zu,\"status\":", gr[i].len);
        pst(st);
        if (st == ZXV_OK) printf(",\"out\":"), phex(out, gr[i].len);
        printf("}");
    }
    printf("]}");
}

/* ------------------------------------------------------------------ web4 */

/* A signed web4 encoding ends with one ML-DSA-65 signature (3309 bytes),
 * which is hedged (randomized) inside libzxv; only the signed body before
 * it is a fixed answer. */
static void w4_body(const char *key, const uint8_t *b, size_t n)
{
    printf(",\"%s_len\":%zu,\"%s_signed_body\":", key, n, key);
    phex(b, n >= ZXV_W4_SIG_BYTES ? n - ZXV_W4_SIG_BYTES : 0);
}

static void web4(zxv_cap *cap)
{
    uint32_t f2 = 0;
    zxv_features2(&f2);
    if (!(f2 & ZXV_FEAT2_WEB4)) return;
    const uint64_t T0 = UINT64_C(1700000000000);
    static const struct {
        uint8_t fill;
        const char *name;
        int node; /* -1 self-bound, else index of the node agent */
    } ag[] = {{0x11, "alice", -1}, {0x22, "bob", -1}, {0x33, "tool.bot", 0}, {0x44, "human", -1}};
    zxv_w4_agent *h[4];
    uint8_t id[4][32], peer[4][32];
    static uint8_t pk[4][ZXV_W4_PK_BYTES];
    printf(",\"web4\":{\"note\":\"agent keys from seed = 32 bytes of `seed_byte`; bound at now_ms. "
           "ML-DSA-65 signatures here are hedged (random), so each signed encoding is given as its "
           "length and the bytes before its trailing 3309-byte signature.\",\"sig_bytes\":%u,"
           "\"agents\":[",
           ZXV_W4_SIG_BYTES);
    for (int i = 0; i < 4; i++) {
        uint8_t seed[32], hs[32];
        memset(seed, ag[i].fill, 32);
        int32_t st = zxv_w4_agent_create(cap, seed, ag[i].name, ag[i].node >= 0 ? h[ag[i].node] : NULL,
                                         T0, &h[i]);
        zxv_w4_agent_info(h[i], id[i], peer[i], pk[i]);
        static uint8_t card[ZXV_W4_CARD_MAX];
        size_t cl = 0;
        zxv_w4_agent_card(h[i], card, sizeof card, &cl);
        printf("%s{\"name\":\"%s\",\"seed_byte\":\"%02x\",\"node\":", i ? "," : "", ag[i].name, ag[i].fill);
        if (ag[i].node >= 0) printf("\"%s\"", ag[i].name ? ag[ag[i].node].name : "");
        else printf("null");
        printf(",\"now_ms\":");
        pu(T0);
        printf(",\"status\":");
        pst(st);
        printf(",\"agent_id\":");
        phex(id[i], 32);
        printf(",\"peer_id\":");
        phex(peer[i], 32);
        zxv_sha3_256(pk[i], ZXV_W4_PK_BYTES, hs);
        printf(",\"pk_sha3_256\":");
        phex(hs, 32);
        printf(",\"card_len\":%zu,\"card_signed_body_sha3_256\":", cl);
        zxv_sha3_256(card, cl - ZXV_W4_SIG_BYTES, hs);
        phex(hs, 32);
        char nm[32];
        uint64_t bms = 0;
        uint8_t i2[32], p2[32];
        int32_t vs = zxv_w4_card_verify(card, cl, i2, p2, nm, &bms);
        printf(",\"card_verify\":");
        pst(vs);
        card[cl - 1] ^= 1;
        printf(",\"card_verify_flipped_last_byte\":");
        pst(zxv_w4_card_verify(card, cl, NULL, NULL, NULL, NULL));
        printf("}");
    }
    /* action digests */
    uint8_t fr[32], to[32];
    memset(fr, 0x01, 32);
    memset(to, 0x02, 32);
    uint8_t *big = ramp(ZXV_W4_MSG_MAX_PAYLOAD + 1);
    static const struct {
        const char *tool;
        uint64_t req;
        const char *payload;
        size_t ramp_len; /* used when payload is NULL */
    } ad[] = {{"pay", 7, "", 0},
              {"pay", 7, "amount=100", 0},
              {"pay", 8, "amount=100", 0},
              {"profile.read", UINT64_MAX, "{\"fields\":[\"name\"]}", 0},
              {"stream.feed", 0, NULL, ZXV_W4_MSG_MAX_PAYLOAD},
              {"stream.feed", 0, NULL, ZXV_W4_MSG_MAX_PAYLOAD + 1},
              {"Pay", 7, "", 0},
              {"", 7, "", 0}};
    printf("],\"action_digest\":{\"from\":");
    phex(fr, 32);
    printf(",\"to\":");
    phex(to, 32);
    printf(",\"cases\":[");
    uint8_t dig0[32] = {0};
    for (size_t i = 0; i < sizeof ad / sizeof ad[0]; i++) {
        uint8_t d[32];
        const uint8_t *p = ad[i].payload ? (const uint8_t *) ad[i].payload : big;
        size_t pl = ad[i].payload ? strlen(ad[i].payload) : ad[i].ramp_len;
        int32_t st = zxv_w4_action_digest(ad[i].tool, fr, to, ad[i].req, p, pl, d);
        if (i == 1) memcpy(dig0, d, 32);
        printf("%s{\"tool\":", i ? "," : "");
        pjs(ad[i].tool);
        printf(",\"req_id\":");
        pu(ad[i].req);
        if (ad[i].payload) printf(",\"payload_utf8\":"), pjs(ad[i].payload);
        else printf(",\"payload_ramp_len\":%zu", pl);
        printf(",\"status\":");
        pst(st);
        if (st == ZXV_OK) printf(",\"digest\":"), phex(d, 32);
        printf("}");
    }
    free(big);
    /* a manifest signed by alice */
    zxv_w4_manifest *m;
    zxv_w4_manifest_create(1, T0, T0 + 86400000, &m);
    static const struct {
        const char *name;
        uint64_t price;
        uint32_t rate, burst, flags, max_in;
    } tl[] = {{"pay", 150, 60, 10, ZXV_W4_TOOL_MONEY, 256},
              {"profile.read", 0, 120, 20, ZXV_W4_TOOL_PII, 0},
              {"stream.feed", 5, 30, 5, ZXV_W4_TOOL_STREAM, 1024}};
    printf("]},\"manifest\":{\"signer\":\"alice\",\"version\":1,\"issued_ms\":");
    pu(T0);
    printf(",\"expires_ms\":");
    pu(T0 + 86400000);
    printf(",\"tools\":[");
    for (size_t i = 0; i < 3; i++) {
        int32_t st = zxv_w4_manifest_add_tool(m, tl[i].name, tl[i].price, tl[i].rate, tl[i].burst,
                                              tl[i].flags, tl[i].max_in);
        printf("%s{\"name\":\"%s\",\"price_vfv\":", i ? "," : "", tl[i].name);
        pu(tl[i].price);
        printf(",\"rate_per_min\":%u,\"burst\":%u,\"flags\":%u,\"max_input\":%u,\"status\":", tl[i].rate,
               tl[i].burst, tl[i].flags, tl[i].max_in);
        pst(st);
        printf("}");
    }
    printf("],\"add_duplicate\":");
    pst(zxv_w4_manifest_add_tool(m, "pay", 1, 1, 1, 0, 0));
    {
        uint8_t b[ZXV_W4_MANIFEST_MAX];
        size_t n = 0;
        printf(",\"encode_unsigned\":");
        pst(zxv_w4_manifest_encode(m, b, sizeof b, &n));
    }
    zxv_w4_manifest_sign(cap, h[0], m);
    {
        static uint8_t b[ZXV_W4_MANIFEST_MAX];
        size_t n = 0;
        zxv_w4_manifest_encode(m, b, sizeof b, &n);
        w4_body("encoding", b, n);
        char js[4096];
        size_t jl;
        zxv_w4_manifest_to_json(m, js, sizeof js, &jl);
        printf(",\"json\":");
        pjs(js);
        zxv_w4_manifest *d = NULL;
        printf(",\"decode\":");
        pst(zxv_w4_manifest_decode(b, n, &d));
        static const struct {
            int key;
            uint64_t now;
        } vv[] = {{0, 1700000000000}, {0, 1700000000000 + 86399999}, {0, 1699999999999},
                  {0, 1700000000000 + 86400000}, {1, 1700000000000}};
        printf(",\"verify\":[");
        for (size_t i = 0; i < sizeof vv / sizeof vv[0]; i++) {
            printf("%s{\"key\":\"%s\",\"now_ms\":", i ? "," : "", ag[vv[i].key].name);
            pu(vv[i].now);
            printf(",\"status\":");
            pst(zxv_w4_manifest_verify(d, pk[vv[i].key], vv[i].now));
            printf("}");
        }
        printf("]}");
        zxv_w4_manifest_destroy(d);
    }
    zxv_w4_manifest_destroy(m);
    /* consent: human -> alice, for action_digest.cases[1] */
    uint8_t nonce[16], tok[ZXV_W4_CONSENT_BYTES], tok2[ZXV_W4_CONSENT_BYTES];
    for (int i = 0; i < 16; i++) nonce[i] = (uint8_t) (0xA0 + i);
    int32_t ci = zxv_w4_consent_issue(cap, h[3], id[0], dig0, ZXV_W4_CONSENT_MONEY, 1000, T0,
                                      T0 + 600000, nonce, tok);
    printf(",\"consent\":{\"human\":\"human\",\"agent\":\"alice\",\"action\":\"action_digest.cases[1]"
           ".digest\",\"scope\":%u,\"max_vfv\":\"1000\",\"issued_ms\":",
           ZXV_W4_CONSENT_MONEY);
    pu(T0);
    printf(",\"expires_ms\":");
    pu(T0 + 600000);
    printf(",\"nonce\":");
    phex(nonce, 16);
    printf(",\"status\":");
    pst(ci);
    w4_body("token", tok, sizeof tok);
    zxv_w4_consent_log *log;
    zxv_w4_consent_log_create(16, &log);
    static const struct {
        int tok; /* 0: tok, 1: tok2 */
        int agent;
        uint32_t need;
        uint64_t amount, now;
        const char *what;
    } cc[] = {{0, 0, ZXV_W4_CONSENT_MONEY, 2000, 1700000000001, "amount over max_vfv"},
              {0, 1, ZXV_W4_CONSENT_MONEY, 500, 1700000000001, "another agent"},
              {0, 0, ZXV_W4_CONSENT_PII, 0, 1700000000001, "scope not granted"},
              {0, 0, ZXV_W4_CONSENT_MONEY, 500, 1700000600000, "expired"},
              {0, 0, ZXV_W4_CONSENT_MONEY, 500, 1700000000001, "valid"},
              {0, 0, ZXV_W4_CONSENT_MONEY, 500, 1700000000002, "replay"},
              {1, 0, ZXV_W4_CONSENT_MONEY, 1000, 1700000000003, "second token, revoked first"}};
    nonce[0] ^= 0xFF;
    zxv_w4_consent_issue(cap, h[3], id[0], dig0, ZXV_W4_CONSENT_MONEY, 1000, T0, T0 + 600000, nonce, tok2);
    printf(",\"check\":[");
    for (size_t i = 0; i < sizeof cc / sizeof cc[0]; i++) {
        if (cc[i].tok == 1) zxv_w4_consent_revoke(log, tok2, cc[i].now);
        int32_t st = zxv_w4_consent_check(log, cc[i].tok ? tok2 : tok, pk[3], id[cc[i].agent], dig0,
                                          cc[i].need, cc[i].amount, cc[i].now);
        printf("%s{\"what\":\"%s\",\"agent\":\"%s\",\"need\":%u,\"amount\":", i ? "," : "", cc[i].what,
               ag[cc[i].agent].name, cc[i].need);
        pu(cc[i].amount);
        printf(",\"now_ms\":");
        pu(cc[i].now);
        printf(",\"status\":");
        pst(st);
        printf("}");
    }
    printf("],\"check_note\":\"checks run in order on one log of capacity 16; the last check uses a "
           "second token with nonce[0] ^= 0xff, revoked before it is checked\"}");
    zxv_w4_consent_log_destroy(log);
    /* envelopes: alice -> bob REQUEST, bob's RESPONSE */
    {
        static uint8_t req[ZXV_W4_ENV_MAX], rsp[ZXV_W4_ENV_MAX];
        size_t rn = 0, sn = 0;
        int32_t s1 = zxv_w4_env_seal(cap, h[0], ZXV_W4_MSG_REQUEST, id[1], 7, 0, "pay", 0,
                                     (const uint8_t *) "amount=100", 10, NULL, T0 + 500, req, sizeof req, &rn);
        int32_t s2 = zxv_w4_env_reply(cap, h[1], req, rn, ZXV_W4_MSG_RESPONSE, 0, ZXV_W4_MF_FINAL,
                                      (const uint8_t *) "ok", 2, T0 + 900, rsp, sizeof rsp, &sn);
        printf(",\"envelopes\":[{\"op\":\"seal\",\"sender\":\"alice\",\"to\":\"bob\",\"kind\":%d,"
               "\"req_id\":\"7\",\"chunk\":0,\"tool\":\"pay\",\"flags\":0,\"payload_utf8\":\"amount=100\","
               "\"now_ms\":",
               ZXV_W4_MSG_REQUEST);
        pu(T0 + 500);
        printf(",\"status\":");
        pst(s1);
        w4_body("env", req, rn);
        printf(",\"verify_sender\":");
        pst(zxv_w4_env_verify(req, rn, pk[0]));
        printf(",\"verify_other\":");
        pst(zxv_w4_env_verify(req, rn, pk[1]));
        printf("},{\"op\":\"reply\",\"sender\":\"bob\",\"to\":\"alice\",\"kind\":%d,\"chunk\":0,"
               "\"flags\":%u,\"payload_utf8\":\"ok\",\"now_ms\":",
               ZXV_W4_MSG_RESPONSE, ZXV_W4_MF_FINAL);
        pu(T0 + 900);
        printf(",\"status\":");
        pst(s2);
        w4_body("env", rsp, sn);
        printf(",\"verify_sender\":");
        pst(zxv_w4_env_verify(rsp, sn, pk[1]));
        printf("}]");
    }
    printf("}");
    for (int i = 3; i >= 0; i--) zxv_w4_agent_destroy(h[i]);
}

/* --------------------------------------------------------------- paradox */

#define PTAG(base, pol, ph) ((uint8_t) ((base) << 4 | (pol) << 2 | (ph)))
enum { PB_FALSE = 0, PB_NEITHER = 1, PB_BOTH = 2, PB_TRUE = 3 };
enum { PP_ENT = 0, PP_NEU = 1, PP_FORB = 2, PP_NEG = 3 };

static void pdx_head(zxv_paradox *b, const char *key)
{
    printf(",\"%s\":{", key);
    for (int w = 0; w < 3; w++) {
        uint32_t n = 0;
        uint8_t h[32];
        zxv_paradox_log_count(b, w, &n);
        zxv_paradox_log_head(b, w, h);
        printf("%s\"%s\":{\"count\":%u,\"head\":", w ? "," : "", w == 0 ? "ops" : w == 1 ? "frontier" : "quarantine", n);
        phex(h, 32);
        printf("}");
    }
    printf("}");
}

static void pdx_values(zxv_paradox_policy *p, uint32_t n)
{
    int64_t v[8], pv[8];
    int32_t armed = 0;
    uint32_t ta = 0;
    zxv_paradox_policy_values(p, v, pv, n, &armed, &ta);
    printf(",\"values\":[");
    for (uint32_t i = 0; i < n; i++) printf("%s", i ? "," : ""), pi(v[i]);
    printf("],\"prev\":[");
    for (uint32_t i = 0; i < n; i++) printf("%s", i ? "," : ""), pi(pv[i]);
    printf("],\"armed\":%s,\"tension_at\":%u", armed ? "true" : "false", ta);
}

static void pdx_graph(zxv_cap *cap, zxv_paradox *b, int first, const char *label, uint32_t limit,
                      int nn, const char *names, const int32_t *kind, const uint8_t *tag,
                      const uint32_t *mag, const int32_t *dir, const char *deps /* "CB" = C dep B */,
                      int npass)
{
    zxv_paradox_graph *g;
    zxv_paradox_graph_create(cap, limit, &g);
    printf("%s{\"label\":\"%s\",\"step_limit\":%u,\"note\":\"node id = 32 bytes of the node's "
           "letter (ASCII)\",\"nodes\":[",
           first ? "" : ",", label, limit);
    for (int i = 0; i < nn; i++) {
        uint8_t id[32];
        memset(id, names[i], 32);
        int32_t st = zxv_paradox_graph_add_node(g, id, kind[i], tag[i], mag[i], dir[i]);
        printf("%s{\"id\":\"%c\",\"kind\":%d,\"tag\":%u,\"mag\":%u,\"dir\":%d,\"status\":", i ? "," : "",
               names[i], kind[i], tag[i], mag[i], dir[i]);
        pst(st);
        printf("}");
    }
    printf("],\"deps\":[");
    for (size_t i = 0; deps[i]; i += 2) {
        uint8_t a[32], d[32];
        memset(a, deps[i], 32);
        memset(d, deps[i + 1], 32);
        int32_t st = zxv_paradox_graph_add_dep(g, a, d);
        printf("%s{\"id\":\"%c\",\"dep\":\"%c\",\"status\":", i ? "," : "", deps[i], deps[i + 1]);
        pst(st);
        printf("}");
    }
    printf("],\"passes\":[");
    for (int p = 0; p < npass; p++) {
        int32_t oc = -1;
        int32_t st = zxv_paradox_graph_pass(cap, b, g, &oc);
        uint32_t nodes, pass, tm;
        int32_t td, esc;
        uint8_t chain[32];
        zxv_paradox_graph_info(g, &nodes, &pass, &tm, &td, &esc, chain);
        printf("%s{\"status\":", p ? "," : "");
        pst(st);
        printf(",\"outcome\":%d,\"pass\":%u,\"total_mag\":%u,\"total_dir\":%d,\"escalated\":%s,\"chain\":",
               st == ZXV_OK ? oc : -1, pass, tm, td, esc ? "true" : "false");
        phex(chain, 32);
        printf(",\"states\":{");
        for (int i = 0; i < nn; i++) {
            uint8_t id[32];
            int32_t s = -1;
            memset(id, names[i], 32);
            zxv_paradox_graph_state(g, id, &s);
            printf("%s\"%c\":%d", i ? "," : "", names[i], s);
        }
        printf("}}");
        if (st != ZXV_OK || oc == ZXV_PDX_OUT_RESOLVED) break;
    }
    printf("]}");
    zxv_paradox_graph_destroy(g);
}

static void paradox(zxv_cap *cap)
{
    uint32_t f2 = 0;
    zxv_features2(&f2);
    if (!(f2 & ZXV_FEAT2_PARADOX)) return;
    printf(",\"paradox\":{\"note\":\"tags are zxv_hex tags (base<<4|polarity<<2|phase; base false 0, "
           "neither 1, both 2, true 3; polarity entropic 0, neutral 1, forbidden 2, negentropic 3). "
           "Every step below runs in order on ONE book created with log capacity 64 and registry "
           "capacity 16; -1 in a table marks ZXV_E_ARG.\"");
    static const char *sd[] = {"", "abc", "loop:A->B->A"};
    printf(",\"signature\":[");
    for (int i = 0; i < 3; i++) {
        uint8_t s[32];
        zxv_paradox_signature((const uint8_t *) sd[i], strlen(sd[i]), s);
        printf("%s{\"utf8\":", i ? "," : "");
        pjs(sd[i]);
        printf(",\"sig\":");
        phex(s, 32);
        printf("}");
    }
    printf("],\"kind_names\":[");
    for (int k = 0; k < 5; k++) printf("%s\"%s\"", k ? "," : "", zxv_paradox_kind_name(k));
    printf("],\"outcome_names\":[");
    for (int k = 0; k < 14; k++) printf("%s\"%s\"", k ? "," : "", zxv_paradox_outcome_name(k));
    printf("],\"node_state_names\":[");
    for (int k = 0; k < 10; k++) printf("%s\"%s\"", k ? "," : "", zxv_paradox_node_state_name(k));
    printf("],\"kind_of_tag\":[");
    for (int t = 0; t < 64; t++) {
        int32_t k = -1;
        int32_t st = zxv_paradox_kind_of_tag((uint8_t) t, &k);
        printf("%s%d", t ? "," : "", st == ZXV_OK ? k : -1);
    }
    printf("],\"action\":[");
    for (int k = 0; k < 5; k++) {
        printf("%s[", k ? "," : "");
        for (int t = 0; t < 64; t++) {
            int32_t d = -1;
            int32_t st = zxv_paradox_action(k, (uint8_t) t, &d);
            printf("%s%d", t ? "," : "", st == ZXV_OK ? d : -1);
        }
        printf("]");
    }
    printf("],\"action_note\":\"action[kind][tag]\",\"outcome_action\":[");
    for (int o = 0; o < 14; o++) {
        int32_t a = -1;
        zxv_paradox_outcome_action(o, &a);
        printf("%s%d", o ? "," : "", a);
    }
    printf("]");

    zxv_paradox *b;
    zxv_paradox_create(cap, 64, 16, &b);
    pdx_head(b, "heads_fresh");
    uint8_t sigL[32], sigM[32];
    zxv_paradox_signature((const uint8_t *) "loop:A->B->A", 12, sigL);
    zxv_paradox_signature((const uint8_t *) "money:drain", 11, sigM);
    static const struct {
        uint8_t tag;
        int ev; /* 0: no evidence */
        uint32_t e[4];
        int32_t stale;
        int sig; /* 0 none, 1 sigL */
    } cl[] = {
        {PTAG(PB_BOTH, PP_NEU, 3), 1, {3, 1, 5, 1}, 0, 0},
        {PTAG(PB_BOTH, PP_NEU, 3), 1, {3, 1, 3, 2}, 0, 0},
        {PTAG(PB_BOTH, PP_NEU, 3), 1, {3, 1, 5, 1}, 1, 0},
        {PTAG(PB_BOTH, PP_NEU, 3), 0, {0}, 0, 0},
        {PTAG(PB_BOTH, PP_NEU, 0), 0, {0}, 0, 0},
        {PTAG(PB_NEITHER, PP_NEU, 1), 0, {0}, 0, 0},
        {PTAG(PB_TRUE, PP_NEU, 3), 1, {3, 1, 5, 1}, 0, 0},
        {PTAG(PB_BOTH, PP_FORB, 3), 1, {3, 1, 5, 1}, 0, 0},
        {PTAG(PB_BOTH, PP_NEU, 0), 0, {0}, 0, 1},
        {PTAG(PB_BOTH, PP_NEU, 3), 1, {3, 1, 5, 1}, 0, 1},
    };
    printf(",\"classify\":[");
    for (size_t i = 0; i < sizeof cl / sizeof cl[0]; i++) {
        if (i == 8) {
            /* register sigL as DESTRUCTIVE before the signature cases */
            uint32_t idx = 99;
            int32_t st = zxv_paradox_registry_add(cap, b, sigL, ZXV_PDX_KIND_DESTRUCTIVE, 1, 0, 0, &idx);
            printf(",{\"registry_add\":{\"sig\":\"signature[2]\",\"kind\":%d,\"reason\":1,\"flags\":0,"
                   "\"owner_ok\":false,\"status\":",
                   ZXV_PDX_KIND_DESTRUCTIVE);
            pst(st);
            printf(",\"index\":%u}}", idx);
        }
        int32_t k = -1, ri = -2;
        uint8_t ot = 255;
        int32_t st = zxv_paradox_classify(b, cl[i].tag, cl[i].ev ? cl[i].e : NULL, cl[i].stale,
                                          cl[i].sig ? sigL : NULL, &k, &ot, &ri);
        printf("%s{\"tag\":%u,\"evidence\":", i ? "," : "", cl[i].tag);
        if (cl[i].ev) printf("[%u,%u,%u,%u]", cl[i].e[0], cl[i].e[1], cl[i].e[2], cl[i].e[3]);
        else printf("null");
        printf(",\"stale\":%s,\"sig\":%s,\"status\":", cl[i].stale ? "true" : "false",
               cl[i].sig ? "\"signature[2]\"" : "null");
        pst(st);
        printf(",\"kind\":%d,\"out_tag\":%u,\"reg_index\":%d}", k, ot, ri);
    }
    {
        uint32_t idx = 99;
        printf("],\"registry\":[{\"utf8\":\"money:drain\",\"kind\":%d,\"flags\":%u,\"owner_ok\":false,"
               "\"status\":",
               ZXV_PDX_KIND_NEUTRAL, ZXV_PDX_FLAG_MONEY);
        pst(zxv_paradox_registry_add(cap, b, sigM, ZXV_PDX_KIND_NEUTRAL, 2, ZXV_PDX_FLAG_MONEY, 0, &idx));
        printf("},{\"utf8\":\"money:drain\",\"kind\":%d,\"flags\":%u,\"owner_ok\":true,\"status\":",
               ZXV_PDX_KIND_NEUTRAL, ZXV_PDX_FLAG_MONEY);
        pst(zxv_paradox_registry_add(cap, b, sigM, ZXV_PDX_KIND_NEUTRAL, 2, ZXV_PDX_FLAG_MONEY, 1, &idx));
        printf(",\"index\":%u},{\"utf8\":\"money:drain\",\"kind\":%d,\"flags\":%u,\"owner_ok\":true,"
               "\"status\":",
               idx, ZXV_PDX_KIND_NEUTRAL, ZXV_PDX_FLAG_MONEY);
        pst(zxv_paradox_registry_add(cap, b, sigM, ZXV_PDX_KIND_NEUTRAL, 2, ZXV_PDX_FLAG_MONEY, 1, &idx));
        printf("}]");
    }
    pdx_head(b, "heads_after_classify");

    /* constructive */
    static const int64_t lo[3] = {0, 0, 10}, hi[3] = {100, 50, 20}, init[3] = {50, 25, 15};
    const uint8_t ct = PTAG(PB_BOTH, PP_NEG, 3), nt = PTAG(PB_BOTH, PP_NEU, 3);
    zxv_paradox_policy *pol, *mpol;
    zxv_paradox_policy_create(cap, 3, lo, hi, init, 0, &pol);
    zxv_paradox_policy_create(cap, 3, lo, hi, init, ZXV_PDX_FLAG_MONEY, &mpol);
    static const struct {
        int money;
        int32_t kind;
        int neutral_tag;
        int synth; /* 0 NULL */
        int64_t s[3];
        uint32_t tension;
    } cs[] = {
        {0, ZXV_PDX_KIND_CONSTRUCTIVE, 0, 1, {60, 20, 12}, 5},
        {0, ZXV_PDX_KIND_CONSTRUCTIVE, 0, 0, {0}, 3},
        {0, ZXV_PDX_KIND_CONSTRUCTIVE, 0, 1, {200, 20, 12}, 3},
        {0, ZXV_PDX_KIND_CONSTRUCTIVE, 0, 1, {55, 30, 18}, 4},
        {0, ZXV_PDX_KIND_CONSTRUCTIVE, 0, 0, {0}, 9},
        {0, ZXV_PDX_KIND_NEUTRAL, 1, 1, {60, 20, 12}, 1},
        {0, ZXV_PDX_KIND_NEUTRAL, 0, 1, {60, 20, 12}, 1},
        {1, ZXV_PDX_KIND_CONSTRUCTIVE, 0, 1, {60, 20, 12}, 1},
    };
    printf(",\"constructive\":{\"lo\":[\"0\",\"0\",\"10\"],\"hi\":[\"100\",\"50\",\"20\"],"
           "\"initial\":[\"50\",\"25\",\"15\"],\"note\":\"policy 'plain' has flags 0, 'money' flags "
           "MONEY; both start at initial\",\"steps\":[");
    for (size_t i = 0; i < sizeof cs / sizeof cs[0]; i++) {
        zxv_paradox_policy *p = cs[i].money ? mpol : pol;
        uint8_t tg = cs[i].neutral_tag ? nt : ct;
        int32_t oc = -1;
        int32_t st = zxv_paradox_constructive(cap, b, p, cs[i].kind, tg, cs[i].synth ? cs[i].s : NULL, 3,
                                              cs[i].tension, &oc);
        printf("%s{\"policy\":\"%s\",\"kind\":%d,\"tag\":%u,\"synth\":", i ? "," : "",
               cs[i].money ? "money" : "plain", cs[i].kind, tg);
        if (cs[i].synth) {
            printf("[");
            for (int k = 0; k < 3; k++) printf("%s", k ? "," : ""), pi(cs[i].s[k]);
            printf("]");
        } else {
            printf("null");
        }
        printf(",\"tension\":%u,\"status\":", cs[i].tension);
        pst(st);
        if (st == ZXV_OK) printf(",\"outcome\":%d", oc);
        pdx_values(p, 3);
        if (cs[i].synth && i == 2) {
            uint8_t pb[ZXV_PDX_PROPOSAL_BYTES(3)];
            size_t pl;
            zxv_paradox_policy_proposal(p, cs[i].s, 3, pb, sizeof pb, &pl);
            printf(",\"proposal\":");
            phex(pb, pl);
        }
        printf("}");
    }
    printf("]}");
    zxv_paradox_policy_destroy(mpol);
    zxv_paradox_policy_destroy(pol);

    /* destructive */
    {
        const uint8_t dt = PTAG(PB_BOTH, PP_ENT, 3);
        uint8_t state[16], ck[16], proof[160];
        memset(state, 'X', 16);
        memset(ck, 'A', 16);
        static const uint64_t trace[4] = {1, 2, 3, 2};
        int32_t oc = -1;
        int32_t st = zxv_paradox_destructive(cap, b, ZXV_PDX_KIND_DESTRUCTIVE, dt, 10, 8, 3, 100, trace, 4,
                                             1, state, ck, 16, (const uint8_t *) "loop:A->B->A", 12, 7,
                                             &oc, proof);
        printf(",\"destructive\":[{\"kind\":%d,\"tag\":%u,\"steps\":10,\"step_limit\":8,\"spent\":3,"
               "\"budget\":100,\"trace\":[\"1\",\"2\",\"3\",\"2\"],\"invariant_ok\":true,"
               "\"state_utf8\":\"XXXXXXXXXXXXXXXX\",\"checkpoint_utf8\":\"AAAAAAAAAAAAAAAA\","
               "\"pattern_utf8\":\"loop:A->B->A\",\"submitter\":7,\"status\":",
               ZXV_PDX_KIND_DESTRUCTIVE, dt);
        pst(st);
        printf(",\"outcome\":%d,\"state_after\":", oc);
        phex(state, 16);
        printf(",\"repair_proof\":");
        phex(proof, 160);
        printf(",\"repair_verify\":");
        pst(zxv_paradox_repair_verify(proof));
        proof[0] ^= 1;
        printf(",\"repair_verify_flipped_byte0\":");
        pst(zxv_paradox_repair_verify(proof));
        memset(state, 'X', 16);
        oc = -1;
        st = zxv_paradox_destructive(cap, b, ZXV_PDX_KIND_CONSTRUCTIVE, ct, 1, 8, 0, 0, NULL, 0, 1, state, ck,
                                     16, NULL, 0, 7, &oc, proof);
        printf("},{\"kind\":%d,\"tag\":%u,\"steps\":1,\"step_limit\":8,\"spent\":0,\"budget\":0,\"trace\":[],"
               "\"invariant_ok\":true,\"state_utf8\":\"XXXXXXXXXXXXXXXX\",\"checkpoint_utf8\":"
               "\"AAAAAAAAAAAAAAAA\",\"pattern_utf8\":\"\",\"submitter\":7,\"status\":",
               ZXV_PDX_KIND_CONSTRUCTIVE, ct);
        pst(st);
        printf(",\"outcome\":%d,\"state_after\":", oc);
        phex(state, 16);
        printf("}]");
    }

    /* neutral */
    {
        zxv_paradox_hold *h;
        zxv_paradox_hold_create(cap, 4, 2, &h);
        uint8_t i1[32], i2[32];
        memset(i1, 0x01, 32);
        memset(i2, 0x02, 32);
        zxv_paradox_hold_add(h, i1);
        zxv_paradox_hold_add(h, i2);
        zxv_paradox_hold_evidence(h, i1, 1);
        zxv_paradox_hold_evidence(h, i2, 1);
        printf(",\"neutral\":{\"capacity\":4,\"decisive\":2,\"branches\":[");
        phex(i1, 32);
        printf(",");
        phex(i2, 32);
        printf("],\"steps\":[");
        static const struct {
            int ev1p, ev1n; /* evidence added before the call */
            int32_t kind;
            uint8_t tag;
            int32_t pick;
        } ns[] = {{0, 0, ZXV_PDX_KIND_NEUTRAL, PTAG(PB_BOTH, PP_NEU, 3), -1},
                  {0, 0, ZXV_PDX_KIND_NEUTRAL, PTAG(PB_NEITHER, PP_NEU, 1), -1},
                  {0, 0, ZXV_PDX_KIND_NEUTRAL, PTAG(PB_BOTH, PP_FORB, 3), -1},
                  {0, 0, ZXV_PDX_KIND_CONSTRUCTIVE, PTAG(PB_BOTH, PP_NEG, 3), -1},
                  {2, 0, ZXV_PDX_KIND_NEUTRAL, PTAG(PB_BOTH, PP_NEU, 3), -1}};
        printf("{\"before\":\"each branch has 1 for, 0 against\"}");
        for (size_t i = 0; i < sizeof ns / sizeof ns[0]; i++) {
            for (int k = 0; k < ns[i].ev1p; k++) zxv_paradox_hold_evidence(h, i1, 1);
            int32_t oc = -1, se = -9;
            int32_t st = zxv_paradox_neutral(cap, b, h, ns[i].kind, ns[i].tag, ns[i].pick, &oc, &se);
            printf(",{\"add_for_branch0\":%d,\"kind\":%d,\"tag\":%u,\"owner_pick\":%d,\"status\":", ns[i].ev1p,
                   ns[i].kind, ns[i].tag, ns[i].pick);
            pst(st);
            printf(",\"outcome\":%d,\"settled\":%d}", oc, se);
        }
        printf("]}");
        zxv_paradox_hold_destroy(h);
    }

    /* special and drafts */
    {
        const uint8_t sp = PTAG(PB_BOTH, PP_NEU, 0);
        uint8_t sg[32], subj[32], draft[71], draft2[71];
        zxv_paradox_signature((const uint8_t *) "frontier:new", 12, sg);
        for (int i = 0; i < 32; i++) subj[i] = (uint8_t) (0x80 + i);
        int32_t oc = -1;
        uint32_t rec = 0;
        int32_t st = zxv_paradox_special(cap, b, ZXV_PDX_KIND_SPECIAL, sp, sg, subj, &oc, &rec);
        printf(",\"special\":{\"sig_utf8\":\"frontier:new\",\"sig\":");
        phex(sg, 32);
        printf(",\"subject\":");
        phex(subj, 32);
        printf(",\"kind\":%d,\"tag\":%u,\"status\":", ZXV_PDX_KIND_SPECIAL, sp);
        pst(st);
        printf(",\"outcome\":%d,\"rec\":%u", oc, rec);
        int32_t ds = zxv_paradox_draft(cap, b, rec, ZXV_PDX_KIND_DESTRUCTIVE, 3, 0, draft);
        printf(",\"draft\":{\"kind\":%d,\"reason\":3,\"flags\":0,\"status\":", ZXV_PDX_KIND_DESTRUCTIVE);
        pst(ds);
        printf(",\"bytes\":");
        phex(draft, 71);
        printf("},\"draft_money\":{\"kind\":%d,\"reason\":4,\"flags\":%u,\"status\":", ZXV_PDX_KIND_NEUTRAL,
               ZXV_PDX_FLAG_MONEY);
        pst(zxv_paradox_draft(cap, b, rec, ZXV_PDX_KIND_NEUTRAL, 4, ZXV_PDX_FLAG_MONEY, draft2));
        printf(",\"bytes\":");
        phex(draft2, 71);
        static const struct {
            int which, owner, consensus;
        } rv[] = {{1, 0, 1}, {0, 0, 0}, {0, 0, 1}, {0, 1, 0}};
        printf("},\"review\":[");
        for (size_t i = 0; i < sizeof rv / sizeof rv[0]; i++) {
            int32_t o = -1;
            int32_t s = zxv_paradox_review(cap, b, rv[i].which ? draft2 : draft, rv[i].owner, rv[i].consensus, &o);
            printf("%s{\"draft\":\"%s\",\"owner_ok\":%s,\"consensus_ok\":%s,\"status\":", i ? "," : "",
                   rv[i].which ? "draft_money" : "draft", rv[i].owner ? "true" : "false",
                   rv[i].consensus ? "true" : "false");
            pst(s);
            printf(",\"outcome\":%d}", o);
        }
        uint32_t rc = 0;
        zxv_paradox_registry_count(b, &rc);
        printf("],\"registry_count_after\":%u}", rc);
    }

    /* graphs */
    printf(",\"graphs\":[");
    {
        static const int32_t k1[4] = {ZXV_PDX_KIND_CONSTRUCTIVE, ZXV_PDX_KIND_DESTRUCTIVE,
                                      ZXV_PDX_KIND_CONSTRUCTIVE, ZXV_PDX_KIND_NONE};
        static const uint8_t t1[4] = {PTAG(PB_BOTH, PP_NEG, 3), PTAG(PB_BOTH, PP_ENT, 3),
                                      PTAG(PB_BOTH, PP_NEG, 3), PTAG(PB_TRUE, PP_NEU, 3)};
        static const uint32_t m1[4] = {3, 5, 2, 0};
        static const int32_t d1[4] = {1, -1, 1, 0};
        pdx_graph(cap, b, 1, "contain and taint", 4, 4, "ABCD", k1, t1, m1, d1, "CBDA", 4);
        static const int32_t k2[3] = {ZXV_PDX_KIND_SPECIAL, ZXV_PDX_KIND_CONSTRUCTIVE, ZXV_PDX_KIND_NEUTRAL};
        static const uint8_t t2[3] = {PTAG(PB_BOTH, PP_NEU, 0), PTAG(PB_BOTH, PP_NEG, 3),
                                      PTAG(PB_NEITHER, PP_NEU, 1)};
        static const uint32_t m2[3] = {1, 4, 2};
        static const int32_t d2[3] = {0, 1, 0};
        pdx_graph(cap, b, 0, "sandbox escalates", 2, 3, "EFG", k2, t2, m2, d2, "FE", 4);
    }
    printf("]");
    pdx_head(b, "heads_final");
    printf(",\"logs\":{");
    for (int w = 0; w < 3; w++) {
        uint32_t n = 0;
        zxv_paradox_log_count(b, w, &n);
        printf("%s\"%s\":[", w ? "," : "", w == 0 ? "ops" : w == 1 ? "frontier" : "quarantine");
        for (uint32_t i = 0; i < n; i++) {
            uint8_t r[ZXV_PDX_REC_BYTES];
            size_t rl = 0;
            zxv_paradox_log_record(b, w, i, r, sizeof r, &rl);
            printf("%s", i ? "," : "");
            phex(r, rl);
        }
        printf("]");
    }
    printf("},\"log_verify\":[");
    for (int w = 0; w < 3; w++) printf("%s", w ? "," : ""), pst(zxv_paradox_log_verify(b, w));
    printf("]}");
    zxv_paradox_destroy(b);
}

static void abi15(zxv_cap *cap)
{
    uint32_t f2 = 0;
    zxv_features2(&f2);
    status15();
    if (f2 & ZXV_FEAT2_TREATY) treaty();
    if (f2 & ZXV_FEAT2_AIPI) {
        cid();
        aipi();
    }
    if (f2 & ZXV_FEAT2_COIN) coin();
    if (f2 & ZXV_FEAT2_FEE) fee();
    vhub(cap);
    if (f2 & ZXV_FEAT2_FREIGHT) freight();
    web4(cap);
    paradox(cap);
}

/* ===================================================================== */
/* ABI 1.6: vino. The same conventions as the 1.5 sections.              */
/* ===================================================================== */

static void status16(void)
{
    printf(",\"status_abi16\":[");
    for (int32_t c = -46; c >= -50; c--)
        printf("%s{\"code\":%d,\"name\":\"%s\",\"class\":%d}", c == -46 ? "" : ",", c,
               zxv_status_name(c), zxv_status_class(c));
    printf("]");
}

/* The test's own record of a request's fields (the ABI passes them one by
 * one), and thin wrappers over encode and decode. */
typedef struct {
    int32_t lane;
    uint32_t flags;
    uint8_t merchant[32];
    uint8_t nonce[16];
    uint64_t amount, created_at, expires_at;
    const char *memo;
} req_t;

static int32_t enc(const req_t *r, uint8_t out[160], uint8_t id[32])
{
    return zxv_vino_request_encode(r->lane, r->flags, r->merchant, r->nonce, r->amount,
                                   r->created_at, r->expires_at, r->memo, out, id);
}
static int32_t dec(const uint8_t in[160], req_t *r, char memo[65], uint8_t id[32])
{
    int32_t rc = zxv_vino_request_decode(in, &r->lane, &r->flags, r->merchant, r->nonce, &r->amount,
                                         &r->created_at, &r->expires_at, memo, id);
    r->memo = memo;
    return rc;
}

static void vreq_fields(const req_t *r)
{
    printf("\"lane\":%d,\"flags\":%u,\"merchant\":", r->lane, r->flags);
    phex(r->merchant, 32);
    printf(",\"nonce\":");
    phex(r->nonce, 16);
    printf(",\"amount\":");
    pu(r->amount);
    printf(",\"created_at\":");
    pu(r->created_at);
    printf(",\"expires_at\":");
    pu(r->expires_at);
    printf(",\"memo\":");
    pjs(r->memo ? r->memo : "");
}

static req_t vreq_base(void)
{
    req_t r;
    memset(&r, 0, sizeof r);
    r.lane = ZXV_VINO_LANE_LIVE;
    for (int i = 0; i < 32; i++) r.merchant[i] = (uint8_t) (i + 1);
    for (int i = 0; i < 16; i++) r.nonce[i] = (uint8_t) (0xa0 + i);
    r.amount = 1234567;
    r.created_at = 1700000000;
    r.expires_at = 1700000600;
    r.memo = "Sword of Dawn";
    return r;
}

/* One play-lane step: print it with its status and outputs. */
static int vfirst;
static void vstep(const char *op, int32_t rc, const char *args)
{
    printf("%s{\"op\":\"%s\",%s,\"status\":", vfirst ? "" : ",", op, args);
    vfirst = 0;
    pst(rc);
}

static void vino(zxv_cap *cap)
{
    char a[256];
    printf(",\"vino\":{\"requests\":[");
    /* encode cases: each changes one field of the base request */
    for (int i = 0; i < 16; i++) {
        req_t r = vreq_base();
        switch (i) {
        case 1:
            r.lane = ZXV_VINO_LANE_PLAY, r.flags = ZXV_VINO_REQ_CHANCE, r.memo = NULL;
            break;
        case 2:
            r.amount = ZXV_VINO_AMOUNT_MAX, r.expires_at = r.created_at + 1;
            break;
        case 3:
            r.memo = "0123456789012345678901234567890123456789012345678901234567890123";
            break;
        case 4:
            r.memo = " ~\"\\";
            break;
        case 5:
            r.lane = 0;
            break;
        case 6:
            r.lane = 3;
            break;
        case 7:
            r.flags = 2;
            break;
        case 8:
            r.flags = ZXV_VINO_REQ_CHANCE;
            break;
        case 9:
            r.amount = 0;
            break;
        case 10:
            r.amount = ZXV_VINO_AMOUNT_MAX + 1;
            break;
        case 11:
            r.expires_at = r.created_at;
            break;
        case 12:
            r.memo = "tab\there";
            break;
        case 13:
            r.memo = "0123456789012345678901234567890123456789012345678901234567890123x";
            break;
        case 14:
            r.lane = ZXV_VINO_LANE_PLAY, r.amount = 1, r.created_at = 0, r.expires_at = UINT64_MAX;
            break;
        case 15:
            r.flags = 0x100;
            break;
        default:
            break;
        }
        uint8_t b[160], id[32];
        int32_t rc = enc(&r, b, id);
        printf("%s{", i ? "," : "");
        vreq_fields(&r);
        printf(",\"status\":");
        pst(rc);
        if (rc == ZXV_OK) {
            printf(",\"bytes\":");
            phex(b, 160);
            printf(",\"id\":");
            phex(id, 32);
        }
        printf("}");
    }
    /* decode refusals: the base bytes with one byte changed */
    req_t base = vreq_base();
    uint8_t good[160];
    enc(&base, good, NULL);
    static const struct {
        uint8_t off, val;
    } db[] = {{0, 'Y'}, {4, 2},   {5, 0},   {5, 3},     {6, 1},    {6, 2},   {7, 1},  {63, 8},
              {75, 0},  {80, 65}, {80, 14}, {81, 0x7f}, {94, 'x'}, {145, 1}, {159, 1}};
    printf("],\"decode\":[");
    for (size_t i = 0; i < sizeof db / sizeof db[0]; i++) {
        uint8_t t[160];
        memcpy(t, good, 160);
        t[db[i].off] = db[i].val;
        req_t d;
        char memo[65];
        int32_t rc = dec(t, &d, memo, NULL);
        printf("%s{\"offset\":%u,\"value\":%u,\"status\":", i ? "," : "", db[i].off, db[i].val);
        pst(rc);
        printf("}");
    }
    printf("]");

    /* receipts: a fixed wallet key and a deterministic signature over the
     * documented message (zxv_vino_receipt_sign itself is hedged) */
    uint32_t f2 = 0;
    zxv_features2(&f2);
    if (f2 & ZXV_FEAT2_VHUB) {
        static uint8_t pk[2592], sk[4896], sig[4627];
        uint8_t seed[32], id[32], msg[88], hs[32];
        memset(seed, 0x31, sizeof seed);
        zxv_mldsa87_keygen_seed(cap, seed, pk, sk);
        zxv_sha3_256(pk, sizeof pk, hs);
        req_t d;
        char memo[65];
        dec(good, &d, memo, id);
        const uint64_t at = 1700000100;
        zxv_vino_receipt_msg(id, pk, at, msg);
        zxv_mldsa87_sign_deterministic(cap, sk, msg, sizeof msg, NULL, 0, sig);
        printf(",\"receipt\":{\"note\":\"wallet key: zxv_mldsa87_keygen_seed with seed = 32 bytes "
               "of 0x31; sig: zxv_mldsa87_sign_deterministic over msg, empty context. request is "
               "requests[0].bytes. Each check flips sig byte flip_sig (when not -1).\",\"seed\":");
        phex(seed, 32);
        printf(",\"pk_sha3_256\":");
        phex(hs, 32);
        printf(",\"approved_at\":");
        pu(at);
        printf(",\"msg\":");
        phex(msg, 88);
        printf(",\"sig\":");
        phex(sig, sizeof sig);
        static const struct {
            uint64_t at, now;
            int flip;
        } vc[] = {{1700000100, 1700000200, -1}, {1700000100, 1700000600, -1},
                  {1700000100, 1700000601, -1}, {1700000101, 1700000200, -1},
                  {1699999999, 1700000200, -1}, {1700000601, 1700000200, -1},
                  {1700000100, 1700000200, 0},  {1700000100, 1700000200, 4626}};
        printf(",\"checks\":[");
        for (size_t i = 0; i < sizeof vc / sizeof vc[0]; i++) {
            if (vc[i].flip >= 0) sig[vc[i].flip] ^= 1;
            int32_t rc = zxv_vino_receipt_verify(good, pk, vc[i].at, sig, vc[i].now);
            if (vc[i].flip >= 0) sig[vc[i].flip] ^= 1;
            printf("%s{\"approved_at\":", i ? "," : "");
            pu(vc[i].at);
            printf(",\"now\":");
            pu(vc[i].now);
            printf(",\"flip_sig\":%d,\"status\":", vc[i].flip);
            pst(rc);
            printf("}");
        }
        printf("]}");
    }

    /* the play lane: steps run in order on one sandbox */
    zxv_vino_play *p = NULL;
    zxv_vino_play_create(cap, &p);
    printf(",\"play\":{\"note\":\"Steps run in order on one sandbox. open: owner, is_issuer -> "
           "account. issue: issuer, holder, face, rail -> id. Every step lists its status; "
           "outputs only on ZXV_OK.\",\"steps\":[");
    vfirst = 1;
    uint32_t acct;
    static const struct {
        uint32_t owner;
        int32_t is_issuer;
    } op[] = {{700, 1}, {801, 0}, {802, 0}, {703, 1}, {9, 2}};
    for (size_t i = 0; i < sizeof op / sizeof op[0]; i++) {
        int32_t rc = zxv_vino_play_open(cap, p, op[i].owner, op[i].is_issuer, &acct);
        snprintf(a, sizeof a, "\"owner\":%u,\"is_issuer\":%d", op[i].owner, op[i].is_issuer);
        vstep("open", rc, a);
        if (rc == ZXV_OK) printf(",\"account\":%u", acct);
        printf("}");
    }
    static const struct {
        char kind; /* b back, u unback, i issue, t transfer, r set_rail, d redeem */
        uint32_t x, y;
        uint64_t n;
        int32_t rail;
    } st[] = {
        {'b', 0, 0, 9, 0},   {'i', 0, 1, 8, 0},  {'i', 0, 1, 5, 0},   {'i', 0, 2, 1, 1},
        {'u', 0, 0, 1, 0},   {'b', 0, 0, 2, 0},  {'i', 0, 2, 1, 1},   {'u', 0, 0, 1, 0},
        {'i', 0, 1, 4, 0},   {'i', 1, 2, 1, 0},  {'i', 0, 0, 1, 0},   {'b', 1, 0, 5, 0},
        {'t', 1, 1, 2, 1},   {'t', 1, 1, 2, 0},  {'t', 1, 2, 1, 0},   {'r', 1, 1, 1, 0},
        {'r', 1, 2, 1, 0},   {'t', 1, 2, 1, 1},  {'d', 1, 1, 0, 0},   {'d', 1, 2, 0, 0},
        {'d', 1, 2, 0, 0},   {'t', 1, 2, 1, 1},  {'u', 0, 0, 9, 0},   {'u', 0, 0, 1, 0},
        {'b', 3, 0, 100, 0}, {'i', 3, 1, 89, 0}, {'u', 3, 0, 100, 0}, {'u', 0, 0, 100, 0},
        {'d', 9, 1, 0, 0},   {'t', 2, 1, 4, 1},
    };
    for (size_t i = 0; i < sizeof st / sizeof st[0]; i++) {
        int32_t rc;
        uint64_t id = 0;
        switch (st[i].kind) {
        case 'b':
            rc = zxv_vino_play_back(cap, p, st[i].x, st[i].n);
            snprintf(a, sizeof a, "\"issuer\":%u,\"amount\":\"%" PRIu64 "\"", st[i].x, st[i].n);
            vstep("back", rc, a);
            break;
        case 'u':
            rc = zxv_vino_play_unback(cap, p, st[i].x, st[i].n);
            snprintf(a, sizeof a, "\"issuer\":%u,\"amount\":\"%" PRIu64 "\"", st[i].x, st[i].n);
            vstep("unback", rc, a);
            break;
        case 'i':
            rc = zxv_vino_issue(cap, p, st[i].x, st[i].y, st[i].n, st[i].rail, &id);
            snprintf(a, sizeof a,
                     "\"issuer\":%u,\"holder\":%u,\"face\":\"%" PRIu64 "\",\"rail\":%d", st[i].x,
                     st[i].y, st[i].n, st[i].rail);
            vstep("issue", rc, a);
            if (rc == ZXV_OK) printf(",\"id\":\"%" PRIu64 "\"", id);
            break;
        case 't':
            /* x = voucher, y = from, n = to */
            rc = zxv_vino_transfer(cap, p, st[i].x, st[i].y, (uint32_t) st[i].n, st[i].rail);
            snprintf(a, sizeof a, "\"voucher\":\"%u\",\"from\":%u,\"to\":%u,\"rail\":%d", st[i].x,
                     st[i].y, (unsigned) st[i].n, st[i].rail);
            vstep("transfer", rc, a);
            break;
        case 'r':
            /* x = voucher, y = holder, n = rail */
            rc = zxv_vino_set_rail(cap, p, st[i].x, st[i].y, (int32_t) st[i].n);
            snprintf(a, sizeof a, "\"voucher\":\"%u\",\"holder\":%u,\"rail\":%d", st[i].x, st[i].y,
                     (int) st[i].n);
            vstep("set_rail", rc, a);
            break;
        default:
            rc = zxv_vino_redeem(cap, p, st[i].x, st[i].y);
            snprintf(a, sizeof a, "\"voucher\":\"%u\",\"holder\":%u", st[i].x, st[i].y);
            vstep("redeem", rc, a);
            break;
        }
        printf("}");
    }
    printf("],\"accounts\":[");
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t owner = 0, count = 0;
        int32_t kind = 0;
        uint64_t back = 0, faces = 0, ids[16];
        size_t n = 0;
        zxv_vino_account_info(cap, p, i, &owner, &kind, &back, &faces, &count);
        zxv_vino_vouchers(cap, p, i, ids, 16, &n);
        printf("%s{\"account\":%u,\"owner\":%u,\"is_issuer\":%d,\"backing\":", i ? "," : "", i,
               owner, kind);
        pu(back);
        printf(",\"faces\":");
        pu(faces);
        printf(",\"count\":%u,\"vouchers\":[", count);
        for (size_t k = 0; k < n; k++) {
            printf("%s", k ? "," : "");
            pu(ids[k]);
        }
        printf("]}");
    }
    printf("],\"vouchers\":[");
    for (uint64_t v = 1;; v++) {
        uint64_t face = 0;
        uint32_t is = 0, ho = 0;
        int32_t rail = 0, state = 0;
        if (zxv_vino_voucher_info(cap, p, v, &face, &is, &ho, &rail, &state) != ZXV_OK) break;
        printf("%s{\"id\":", v > 1 ? "," : "");
        pu(v);
        printf(",\"face\":");
        pu(face);
        printf(",\"issuer\":%u,\"holder\":%u,\"rail\":%d,\"state\":%d}", is, ho, rail, state);
    }
    printf("]}");
    zxv_vino_play_destroy(p);

    /* the daily meter */
    zxv_vino_meter *m = NULL;
    zxv_vino_meter_create(cap, 100, &m);
    static const struct {
        uint64_t day, amount;
    } ms[] = {{5, 60},  {5, 41}, {5, 40},  {5, 1}, {4, 1}, {5, 0},
              {6, 101}, {5, 1},  {7, 100}, {6, 1}, {8, 1}};
    printf(",\"meter\":{\"daily_cap\":\"100\",\"steps\":[");
    for (size_t i = 0; i < sizeof ms / sizeof ms[0]; i++) {
        uint64_t left = UINT64_MAX;
        int32_t rc = zxv_vino_meter_admit(cap, m, ms[i].day, ms[i].amount, &left);
        printf("%s{\"day\":", i ? "," : "");
        pu(ms[i].day);
        printf(",\"amount\":");
        pu(ms[i].amount);
        printf(",\"status\":");
        pst(rc);
        if (rc == ZXV_OK || rc == ZXV_E_LIMIT) {
            printf(",\"left\":");
            pu(left);
        }
        printf("}");
    }
    printf("]}}");
    zxv_vino_meter_destroy(m);
}

static void abi16(zxv_cap *cap)
{
    uint32_t f2 = 0;
    zxv_features2(&f2);
    status16();
    if (f2 & ZXV_FEAT2_VINO) vino(cap);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../conformance/fixtures";
    zxv_ctx *ctx;
    zxv_cap *cap;
    zxv_ctx_create(&ctx);
    zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL_1_6, &cap);
    uint32_t maj, min, pat;
    zxv_version(&maj, &min, &pat);
    printf("{\"abi\":[%u,%u,%u],\"generated_by\":\"sdk/libzxv/tools/gen_vectors.c\",", maj, min, pat);
    statuses();
    rationals();
    logic();
    wire();
    hashes();
    crypto(cap);
    swarm(ctx, cap);
    generation(cap, dir);
    evm(cap);
    hex(cap);
    abi15(cap);
    abi16(cap);
    printf(",\"phase_cycle\":%u}\n", ZXV_PHASE_CYCLE);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
    return 0;
}
