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

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../conformance/fixtures";
    zxv_ctx *ctx;
    zxv_cap *cap;
    zxv_ctx_create(&ctx);
    zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap);
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
    printf(",\"phase_cycle\":%u}\n", ZXV_PHASE_CYCLE);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
    return 0;
}
