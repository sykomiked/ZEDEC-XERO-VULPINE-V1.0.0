/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_juris.c — the jurisdiction plug-in ABI (juris.h J1-J7): zxvd's
 * jurisdiction.get JSON (the shapes PR #69's tests send), signed manifests,
 * the settings gate, fail-closed combining, tax-rate agreement, the
 * accounting framework, and vhub's limit rule as the first plug-in. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "juris_vhub.h"
#include "../robin_debanks/sha256.h"

static int pass, fail;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static juris_status_t parse(const char *s, juris_settings_t *o)
{
    return juris_settings_parse(s, strlen(s), o);
}

static void test_parse(void)
{
    juris_settings_t s;
    /* zxvd's default reply */
    CHECK(parse("{\"market\":\"closed\",\"standard\":\"\",\"country\":\"\",\"standards\":[\"IFRS\","
                "\"US_GAAP\",\"IPSAS\",\"AAOIFI\",\"OHADA\",\"CAS\",\"SAP_INSURER\"],"
                "\"in_effect\":false}",
                &s) == JURIS_OK &&
              s.market == JURIS_MARKET_CLOSED && s.standard == ACCT_STD_NONE && s.country[0] == 0 &&
              !s.in_effect,
          "default settings");
    CHECK(parse(" {\n \"market\" : \"open\", \"standard\":\"IFRS\",\"country\":\"KE\"} \n", &s) ==
                  JURIS_OK &&
              s.market == JURIS_MARKET_OPEN && s.standard == ACCT_STD_IFRS &&
              strcmp(s.country, "KE") == 0,
          "jurisdiction.set's KE / IFRS / open");
    CHECK(parse("{\"standard\":\"SAP_INSURER\",\"country\":\"ZA\",\"in_effect\":true}", &s) ==
                  JURIS_OK &&
              s.standard == ACCT_STD_SAP_INSURER && s.in_effect && s.market == JURIS_MARKET_CLOSED,
          "missing keys keep the defaults");
    CHECK(parse("{}", &s) == JURIS_OK && s.country[0] == 0, "empty object");
    CHECK(
        parse("{\"country\":\"\\u004E\\u0047\",\"x\":{\"a\":[1,-2.5e3,null,true,{}]},\"y\":\"z\"}",
              &s) == JURIS_OK &&
            strcmp(s.country, "NG") == 0,
        "escapes and unknown keys skipped");

    static const char *const bad[] = {
        "",
        "[]",
        "{\"market\":\"Open\"}",
        "{\"standard\":\"GAAP\"}",
        "{\"standard\":\"ifrs\"}",
        "{\"country\":\"ke\"}",
        "{\"country\":\"KEN\"}",
        "{\"country\":7}",
        "{\"market\":\"open\",\"market\":\"closed\"}",
        "{\"in_effect\":1}",
        "{\"standards\":[1]}",
        "{\"market\":\"open\"} x",
        "{\"market\":\"open\"",
        "{\"market\":\"op\\qen\"}",
        "{\"market\":\"op\nen\"}",
        "{\"market\" \"open\"}",
        "{\"a\":[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]}",
        "{\"a\":-}",
        "{\"market\":\"open\",}",
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        s.market = 9;
        CHECK(parse(bad[i], &s) == JURIS_E_PARSE && s.market == JURIS_MARKET_CLOSED,
              "refused, and defaults left: %s", bad[i]);
    }
    CHECK(juris_settings_parse("{\"country\":\"KE\"}", 5, &s) == JURIS_E_PARSE, "length honoured");
    CHECK(juris_settings_parse(NULL, 0, &s) == JURIS_E_ARG, "NULL");
}

/* ---- a stand-in gateway signature: sha256("gw" || msg) ---- */
static bool gw_ok = true;
static void gw_sign(const uint8_t *msg, uint32_t len, uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "gw", 2);
    sha256_update(&c, msg, len);
    sha256_final(&c, out);
}
static bool gw_verify(void *ctx, const uint8_t *msg, uint32_t len, const uint8_t *sig, uint32_t sl)
{
    (void) ctx;
    uint8_t want[32];
    gw_sign(msg, len, want);
    return gw_ok && sl == 32 && memcmp(want, sig, 32) == 0;
}

static uint8_t sigbuf[16][32];
static unsigned nsig;
static juris_manifest_t manifest_for(const juris_plugin_t *p, uint64_t version, const uint8_t *img,
                                     size_t n)
{
    juris_manifest_t m;
    memset(&m, 0, sizeof m);
    memcpy(m.id, p->id, JURIS_ID_LEN);
    memcpy(m.country, p->country, 3);
    m.hooks = p->hooks;
    m.version = version;
    sha256(img, n, m.image_hash);
    uint8_t msg[JURIS_MANIFEST_MSG];
    juris_manifest_msg(&m, msg);
    uint8_t *sig = sigbuf[nsig++ % 16];
    gw_sign(msg, JURIS_MANIFEST_MSG, sig);
    m.sig = sig;
    m.sig_len = 32;
    return m;
}

/* ---- small plug-ins ---- */
static lpx_t T(uint8_t b)
{
    return lpx_make(b, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
}
typedef struct {
    lpx_t answer;
    uint64_t num, den;
    int commits;
    acct_std_t refuse_std;
} fake_t;
static lpx_t f_transfer(void *c, const juris_transfer_t *t)
{
    (void) t;
    return ((fake_t *) c)->answer;
}
static lpx_t f_market(void *c, juris_market_t m)
{
    (void) m;
    return ((fake_t *) c)->answer;
}
static lpx_t f_tax(void *c, juris_tax_kind_t k, uint64_t *n, uint64_t *d)
{
    fake_t *f = c;
    if (k != JURIS_TAX_INCOME) return T(LPX_B_NEITHER);
    *n = f->num;
    *d = f->den;
    return f->answer;
}
static lpx_t f_std(void *c, acct_std_t s)
{
    return s == ((fake_t *) c)->refuse_std ? T(LPX_B_FALSE) : T(LPX_B_TRUE);
}
static void f_commit(void *c, const juris_transfer_t *t)
{
    (void) t;
    ((fake_t *) c)->commits++;
}
static void mk(juris_plugin_t *p, const char *id, const char *cc, uint32_t hooks, fake_t *f)
{
    memset(p, 0, sizeof *p);
    p->abi = JURIS_PLUGIN_ABI;
    p->size = sizeof *p;
    strncpy(p->id, id, JURIS_ID_LEN);
    memcpy(p->country, cc, 2);
    p->hooks = hooks;
    p->ctx = f;
    p->transfer = f_transfer;
    p->market = f_market;
    p->tax_rate = f_tax;
    p->standard = f_std;
    p->commit = f_commit;
}

static const uint8_t IMG[] = "plug-in image bytes";

static juris_status_t reg(juris_host_t *h, const juris_plugin_t *p, uint64_t v)
{
    juris_manifest_t m = manifest_for(p, v, IMG, sizeof IMG);
    return juris_host_register(h, p, &m, IMG, sizeof IMG);
}

static lpx_qrec_t QS[4];

static void test_register(void)
{
    juris_settings_t s;
    CHECK(parse("{\"country\":\"KE\",\"standard\":\"IFRS\"}", &s) == JURIS_OK, "settings");
    juris_host_t h;
    juris_host_init(&h, &s, gw_verify, NULL, QS, 4);
    fake_t f = {T(LPX_B_TRUE), 30, 100, 0, ACCT_STD_NONE};
    juris_plugin_t p;
    mk(&p, "ke-cbk-rules", "KE", JURIS_HOOK_TRANSFER, &f);
    CHECK(reg(&h, &p, 1) == JURIS_OK && h.n == 1, "J3: signed plug-in registers");

    juris_manifest_t m = manifest_for(&p, 2, IMG, sizeof IMG);
    CHECK(juris_host_register(&h, &p, &m, IMG, sizeof IMG - 1) == JURIS_E_HASH,
          "J3: a different image is refused");
    m = manifest_for(&p, 2, IMG, sizeof IMG);
    uint8_t bad[32];
    memcpy(bad, m.sig, 32);
    bad[0] ^= 1;
    m.sig = bad;
    CHECK(juris_host_register(&h, &p, &m, IMG, sizeof IMG) == JURIS_E_SIG, "J3: bad signature");
    m = manifest_for(&p, 2, IMG, sizeof IMG);
    m.hooks = 0; /* changes the signed message too */
    CHECK(juris_host_register(&h, &p, &m, IMG, sizeof IMG) == JURIS_E_SIG,
          "J3: an edited manifest fails its signature");
    juris_plugin_t q = p;
    q.hooks = JURIS_HOOK_TRANSFER | JURIS_HOOK_TAX;
    m = manifest_for(&p, 2, IMG, sizeof IMG); /* signed for TRANSFER only */
    CHECK(juris_host_register(&h, &q, &m, IMG, sizeof IMG) == JURIS_E_HOOKS,
          "J3: hooks beyond the signed ones");
    CHECK(reg(&h, &p, 1) == JURIS_E_VERSION, "J3: same version again");
    CHECK(reg(&h, &p, 2) == JURIS_OK && h.n == 1, "J3: a newer version replaces in place");
    CHECK(reg(&h, &p, 1) == JURIS_E_VERSION, "J3: no rollback");

    juris_plugin_t ng;
    mk(&ng, "ng-rules", "NG", JURIS_HOOK_TRANSFER, &f);
    CHECK(reg(&h, &ng, 1) == JURIS_E_COUNTRY, "J4: another country's plug-in");
    juris_plugin_t v2 = p;
    v2.abi = (2u << 16);
    CHECK(reg(&h, &v2, 9) == JURIS_E_ABI, "J2: a newer major ABI");
    v2 = p;
    v2.size = sizeof v2 - 8;
    CHECK(reg(&h, &v2, 9) == JURIS_E_ABI, "J2: smaller than this header's struct");
    v2 = p;
    v2.transfer = NULL;
    CHECK(reg(&h, &v2, 9) == JURIS_E_ABI, "J2: a declared hook that is NULL");
    v2 = p;
    v2.hooks = 0x100;
    CHECK(reg(&h, &v2, 9) == JURIS_E_ABI, "J2: unknown hook bits");
    gw_ok = false;
    CHECK(reg(&h, &p, 9) == JURIS_E_SIG, "J3: the gateway says no");
    gw_ok = true;
    juris_host_t nv;
    juris_host_init(&nv, &s, NULL, NULL, NULL, 0);
    CHECK(reg(&nv, &p, 1) == JURIS_E_SIG, "J3: no verify hook, no plug-ins");

    /* J4: no country chosen */
    juris_settings_t none;
    juris_settings_default(&none);
    juris_host_t h0;
    juris_host_init(&h0, &none, gw_verify, NULL, NULL, 0);
    CHECK(reg(&h0, &p, 1) == JURIS_E_COUNTRY, "J4: nothing loads without a country");
    juris_transfer_t t;
    memset(&t, 0, sizeof t);
    t.amount = 1;
    CHECK(lpx_action(juris_check_transfer(&h0, &t)) == LPX_ACT_WAIT, "J4: and every check waits");
    CHECK(juris_host_standard(&h0) == ACCT_STD_NONE, "J7: no framework without a country");

    /* full */
    juris_host_t hf;
    juris_host_init(&hf, &s, gw_verify, NULL, NULL, 0);
    juris_plugin_t many[JURIS_MAX_PLUGINS + 1];
    for (unsigned i = 0; i <= JURIS_MAX_PLUGINS; i++) {
        char id[16];
        snprintf(id, sizeof id, "p%u", i);
        mk(&many[i], id, "KE", JURIS_HOOK_TRANSFER, &f);
        juris_status_t r = reg(&hf, &many[i], 1);
        CHECK(i < JURIS_MAX_PLUGINS ? r == JURIS_OK : r == JURIS_E_FULL, "slot %u", i);
    }

    /* J4: a new country unloads the old plug-ins */
    juris_settings_t ng_s = s;
    memcpy(ng_s.country, "NG", 3);
    CHECK(juris_host_apply_settings(&h, &ng_s) == JURIS_OK && h.n == 0, "unloaded on NG");
    CHECK(reg(&h, &ng, 1) == JURIS_OK && h.n == 1, "NG's plug-in now loads");
    ng_s.market = JURIS_MARKET_OPEN;
    CHECK(juris_host_apply_settings(&h, &ng_s) == JURIS_OK && h.n == 1,
          "same country: plug-ins stay");
    juris_settings_t badset = ng_s;
    badset.country[0] = 'n';
    CHECK(juris_host_apply_settings(&h, &badset) == JURIS_E_ARG, "bad settings refused");
}

static void test_checks(void)
{
    juris_settings_t s;
    parse("{\"country\":\"KE\",\"standard\":\"AAOIFI\"}", &s);
    juris_host_t h;
    juris_host_init(&h, &s, gw_verify, NULL, QS, 1);
    juris_transfer_t t;
    memset(&t, 0, sizeof t);
    t.amount = 500;
    t.sender = 7;
    t.receiver = 8;
    CHECK(lpx_action(juris_check_transfer(&h, &t)) == LPX_ACT_WAIT, "J5: no plug-in waits");

    /* J6: vhub's limit rule as the first plug-in */
    vhub_limit_t lim;
    memset(&lim, 0, sizeof lim);
    lim.limit = 1000;
    juris_plugin_t lp;
    juris_limit_plugin(&lp, "ke-capital-control", "KE", &lim);
    CHECK(reg(&h, &lp, 1) == JURIS_OK, "limit plug-in registers");
    CHECK(lpx_action(juris_check_transfer(&h, &t)) == LPX_ACT_PROCEED, "500 of 1000");
    juris_commit_transfer(&h, &t);
    t.amount = 600;
    CHECK(lpx_action(juris_check_transfer(&h, &t)) == LPX_ACT_REFUSE, "500 + 600 > 1000");
    t.amount = 500;
    CHECK(lpx_action(juris_check_transfer(&h, &t)) == LPX_ACT_PROCEED, "500 + 500 fits");
    t.sender = 9;
    t.amount = 1000;
    CHECK(lpx_action(juris_check_transfer(&h, &t)) == LPX_ACT_PROCEED, "per sender");

    /* through vhub's rule interface */
    vhub_swap_t sw;
    memset(&sw, 0, sizeof sw);
    sw.sender = 7;
    sw.amount = 600;
    CHECK(lpx_action(juris_vhub_rule(&h, &sw, 0)) == LPX_ACT_REFUSE, "vhub bridge refuses");
    CHECK(lpx_action(juris_vhub_rule(&h, &sw, 1)) == LPX_ACT_PROCEED, "the receiving side passes");
    sw.amount = 400;
    CHECK(lpx_action(juris_vhub_rule(&h, &sw, 0)) == LPX_ACT_PROCEED, "400 fits");
    juris_vhub_commit(&h, &sw, 0);
    sw.amount = 101;
    CHECK(lpx_action(juris_vhub_rule(&h, &sw, 0)) == LPX_ACT_REFUSE, "vhub commit counted");

    /* J5: the worst answer wins */
    fake_t both = {T(LPX_B_BOTH), 0, 1, 0, ACCT_STD_NONE};
    juris_plugin_t bp;
    mk(&bp, "disputed", "KE", JURIS_HOOK_TRANSFER | JURIS_HOOK_COMMIT, &both);
    CHECK(reg(&h, &bp, 1) == JURIS_OK, "second plug-in");
    t.sender = 10;
    t.amount = 1;
    CHECK(lpx_action(juris_check_transfer(&h, &t)) == LPX_ACT_ESCALATE, "BOTH escalates");
    juris_commit_transfer(&h, &t);
    CHECK(both.commits == 1, "commit hooks run");
    both.answer = T(LPX_B_FALSE);
    CHECK(lpx_action(juris_check_transfer(&h, &t)) == LPX_ACT_REFUSE, "FALSE refuses");
    both.answer = lpx_forbid(T(LPX_B_TRUE));
    CHECK(lpx_action(juris_check_transfer(&h, &t)) == LPX_ACT_ROLLBACK && h.qlog.n == 1 &&
              lpx_qlog_verify(&h.qlog),
          "forbidden goes to the quarantine log");
    CHECK(lpx_action(juris_check_transfer(&h, &t)) == LPX_ACT_ROLLBACK && h.halted,
          "a full log halts the host");
    CHECK(lpx_action(juris_check_transfer(&h, &t)) == LPX_ACT_ROLLBACK, "halted stays halted");
    juris_plugin_t late;
    mk(&late, "late", "KE", JURIS_HOOK_TRANSFER, &both);
    CHECK(reg(&h, &late, 1) == JURIS_E_HALTED, "no registration while halted");
}

static void test_market_tax_standard(void)
{
    juris_settings_t s;
    parse("{\"country\":\"KE\",\"standard\":\"AAOIFI\",\"market\":\"closed\"}", &s);
    juris_host_t h;
    juris_host_init(&h, &s, gw_verify, NULL, NULL, 0);
    CHECK(lpx_action(juris_check_market(&h, JURIS_MARKET_CLOSED)) == LPX_ACT_PROCEED,
          "closed clearing needs no plug-in");
    CHECK(lpx_action(juris_check_market(&h, JURIS_MARKET_OPEN)) == LPX_ACT_REFUSE,
          "J5: open while the settings say closed");
    s.market = JURIS_MARKET_OPEN;
    juris_host_apply_settings(&h, &s);
    CHECK(lpx_action(juris_check_market(&h, JURIS_MARKET_OPEN)) == LPX_ACT_WAIT,
          "open with no plug-in to allow it waits");
    fake_t yes = {T(LPX_B_TRUE), 30, 100, 0, ACCT_STD_NONE};
    juris_plugin_t mp;
    mk(&mp, "ke-cma", "KE", JURIS_HOOK_MARKET | JURIS_HOOK_TAX | JURIS_HOOK_STANDARD, &yes);
    CHECK(reg(&h, &mp, 1) == JURIS_OK, "market plug-in");
    CHECK(lpx_action(juris_check_market(&h, JURIS_MARKET_OPEN)) == LPX_ACT_PROCEED, "open allowed");

    uint64_t n, d;
    CHECK(juris_host_tax_rate(&h, JURIS_TAX_INCOME, &n, &d) == JURIS_OK && n == 30 && d == 100,
          "income tax 30%%");
    CHECK(juris_host_tax_rate(&h, JURIS_TAX_VAT, &n, &d) == JURIS_E_NO_RULE && n == 0,
          "nobody knows VAT");
    fake_t same = {T(LPX_B_TRUE), 3, 10, 0, ACCT_STD_NONE};
    juris_plugin_t tp;
    mk(&tp, "ke-kra", "KE", JURIS_HOOK_TAX, &same);
    CHECK(reg(&h, &tp, 1) == JURIS_OK, "tax plug-in");
    CHECK(juris_host_tax_rate(&h, JURIS_TAX_INCOME, &n, &d) == JURIS_OK && n * 10 == d * 3,
          "3/10 agrees with 30/100");
    same.num = 25;
    same.den = 100;
    CHECK(juris_host_tax_rate(&h, JURIS_TAX_INCOME, &n, &d) == JURIS_E_CONFLICT &&
              lpx_base(h.last_tag) == LPX_B_BOTH,
          "J5: rates that disagree escalate");
    same.num = 101;
    CHECK(juris_host_tax_rate(&h, JURIS_TAX_INCOME, &n, &d) == JURIS_E_NO_RULE,
          "a rate above 100%%");
    same.num = 3;
    same.den = 10;
    same.answer = T(LPX_B_BOTH);
    CHECK(juris_host_tax_rate(&h, JURIS_TAX_INCOME, &n, &d) == JURIS_E_CONFLICT, "a disputed rate");

    CHECK(juris_host_standard(&h) == ACCT_STD_AAOIFI, "J7: AAOIFI applies");
    yes.refuse_std = ACCT_STD_AAOIFI;
    CHECK(juris_host_standard(&h) == ACCT_STD_NONE, "J7: a plug-in refuses it");
    yes.refuse_std = ACCT_STD_NONE;
    s.standard = ACCT_STD_OHADA;
    juris_host_apply_settings(&h, &s);
    CHECK(juris_host_standard(&h) == ACCT_STD_NONE, "J7: OHADA is not built");
}

/* every prefix of a valid document is refused, read from an exact-size
 * heap copy so ASan sees any read past the end */
static juris_status_t parse_exact(const char *s, size_t n, juris_settings_t *o)
{
    char *b = malloc(n ? n : 1);
    memcpy(b, s, n);
    juris_status_t r = juris_settings_parse(b, n, o);
    free(b);
    return r;
}

static void test_parse_more(void)
{
    const char *full =
        "{\"x\":{\"a\":[1,-2,3.5e+2,0.5E-3,\"s\\\"\\\\\\/\\b\\f\\n\\r\\t\\u0041\\u00e9 "
        "\",true,false,null,"
        "{},[],{\"k\":[[]]}],\"b\":{}},\"market\":\"op\\u0065n\",\"standards\":[],"
        "\"a_key_much_longer_than_twenty_four_bytes\":\"v\",\"standard\":\"AAOIFI\","
        "\"country\":\"\\u004bE\",\"standards2\":[\"IFRS\",\"\"],\"in_effect\":true}";
    juris_settings_t s;
    size_t n = strlen(full);
    CHECK(parse_exact(full, n, &s) == JURIS_OK && s.market == JURIS_MARKET_OPEN &&
              s.standard == ACCT_STD_AAOIFI && strcmp(s.country, "KE") == 0 && s.in_effect,
          "a document using every JSON form");
    unsigned bad = 0;
    for (size_t k = 0; k < n; k++)
        if (parse_exact(full, k, &s) != JURIS_E_PARSE) bad++;
    CHECK(bad == 0, "J1: every proper prefix is refused (%u were not)", bad);
    const char *std = "{\"standards\":[\"IFRS\",\"US_GAAP\"],\"country\":\"NG\"}";
    n = strlen(std);
    CHECK(parse_exact(std, n, &s) == JURIS_OK && strcmp(s.country, "NG") == 0, "standards list");
    bad = 0;
    for (size_t k = 0; k < n; k++)
        if (parse_exact(std, k, &s) != JURIS_E_PARSE) bad++;
    CHECK(bad == 0, "standards prefixes (%u)", bad);

    /* single-byte changes never crash, and an accepted result is well formed */
    unsigned weird = 0;
    const char subs[] = "{}[]\":,\\ \x01"
                        "0aZ-";
    n = strlen(full);
    char *b = malloc(n);
    for (size_t k = 0; k < n; k++)
        for (unsigned j = 0; j < sizeof subs - 1; j++) {
            memcpy(b, full, n);
            b[k] = subs[j];
            if (juris_settings_parse(b, n, &s) == JURIS_OK &&
                (s.market > 1 || s.standard >= ACCT_STD_COUNT ||
                 (s.country[0] && (s.country[0] < 'A' || s.country[0] > 'Z' || s.country[2]))))
                weird++;
        }
    free(b);
    CHECK(weird == 0, "mutated documents parse to valid settings only");

    static const struct {
        const char *j;
        int ok;
    } v[] = {
        {"{\"country\":\"AZ\"}", 1},
        {"{\"country\":\"ZA\"}", 1},
        {"{\"country\":\"@A\"}", 0},
        {"{\"country\":\"A@\"}", 0},
        {"{\"country\":\"[A\"}", 0},
        {"{\"country\":\"A[\"}", 0},
        {"{\"country\":\"\\u00c9A\"}", 0},
        {"{\"country\":\"\\u004G\"}", 0},
        {"{\"country\":\"\\u004", 0},
        {"{\"country\":\"\\uFFFFA\"}", 0},
        {"{\"x\":\"a\\u00Ff\\u00fF\"}", 1},
        {"{\"x\":\"\x1f\"}", 0},
        {"{\"x\":\" ~\"}", 1},
        {"{\"x\":\"\\x\"}", 0},
        {"{\"x\":-5}", 1},
        {"{\"x\":-}", 0},
        {"{\"x\":5}", 1},
        {"{\"x\":9}", 1},
        {"{\"x\":0}", 1},
        {"{\"x\":tru}", 0},
        {"{\"x\":nul}", 0},
        {"{\"x\":[1 2]}", 0},
        {"{\"x\":{\"a\" 1}}", 0},
        {"{\"x\":{\"a\":1 \"b\":2}}", 0},
        {"{\"x\":{1:2}}", 0},
        {"{\"x\":[,]}", 0},
        {"{\"x\": [ ] , \"y\" : { } }", 1},
        {"{\"standards\":[\"a\" \"b\"]}", 0},
        {"{\"standards\":[\"a\",]}", 0},
        {"{\"standards\":\"IFRS\"}", 0},
        {"{\"standards\":[ ]}", 1},
        {"{\"market\":\"closedxxxxxxxxxxxxxxxxxxxxxxxxx\"}", 0},
        {"{\"market\":\"closed\"}", 1},
        {"{\"market\":closed}", 0},
        {"{\"in_effect\":false}", 1},
        {"{\"in_effect\":\"true\"}", 0},
        {"{\"standard\":7}", 0},
        {"{\"country\":\"\"}", 1},
        {"{\"marketx\":\"weird\"}", 1},
        {"{\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaamarket\":\"weird\"}", 1},
        {"{\"x\":1}}", 0},
        {"{\"x\":1", 0},
        {"{\"x\" 1}", 0},
        {"{,}", 0},
        {"{\"x\":1}\t\r\n ", 1},
        {" \t\r\n{}", 1},
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++)
        CHECK((parse(v[i].j, &s) == JURIS_OK) == v[i].ok, "%s should be %s", v[i].j,
              v[i].ok ? "accepted" : "refused");

    /* nesting: 17 levels of containers are skipped, 18 are refused */
    char deep[64];
    for (int lv = 17; lv <= 18; lv++) {
        size_t o = 0;
        o += (size_t) sprintf(deep + o, "{\"x\":");
        for (int i = 0; i < lv; i++) deep[o++] = '[';
        for (int i = 0; i < lv; i++) deep[o++] = ']';
        deep[o++] = '}';
        deep[o] = 0;
        CHECK((parse(deep, &s) == JURIS_OK) == (lv == 17), "depth %d", lv);
    }
    juris_settings_default(NULL);
    CHECK(juris_settings_parse("{}", 2, NULL) == JURIS_E_ARG, "NULL out");
}

/* J3: the manifest message, byte for byte */
static void test_manifest_bytes(void)
{
    juris_manifest_t m;
    memset(&m, 0, sizeof m);
    memcpy(m.id, "ke-rules", 8);
    memcpy(m.country, "KE", 3);
    m.hooks = 0x0A0B0C0Du;
    m.version = 0x0102030405060708ull;
    for (int i = 0; i < 32; i++) m.image_hash[i] = (uint8_t) (0xA0 + i);
    uint8_t got[JURIS_MANIFEST_MSG], want[JURIS_MANIFEST_MSG];
    size_t o = 0;
    memcpy(want, "zxv/juris-plugin/v1", 19);
    o = 19;
    memcpy(want + o, m.id, 32);
    o += 32;
    want[o++] = 'K';
    want[o++] = 'E';
    const uint8_t hk[4] = {0x0D, 0x0C, 0x0B, 0x0A};
    memcpy(want + o, hk, 4);
    o += 4;
    for (int i = 0; i < 8; i++) want[o++] = (uint8_t) (8 - i);
    memcpy(want + o, m.image_hash, 32);
    o += 32;
    CHECK(o == JURIS_MANIFEST_MSG, "length");
    memset(got, 0xEE, sizeof got);
    juris_manifest_msg(&m, got);
    CHECK(memcmp(got, want, sizeof want) == 0, "J3: manifest layout");
}

static void test_host_more(void)
{
    juris_host_t h;
    juris_host_init(&h, NULL, gw_verify, NULL, QS, 2);
    CHECK(h.set.country[0] == 0 && h.set.market == JURIS_MARKET_CLOSED, "NULL settings: defaults");
    CHECK(lpx_base(juris_check_market(&h, JURIS_MARKET_CLOSED)) == LPX_B_NEITHER,
          "no country: wait");
    juris_host_init(NULL, NULL, NULL, NULL, NULL, 0);
    juris_settings_t s;
    juris_settings_default(&s);
    s.market = 2;
    CHECK(juris_host_apply_settings(&h, &s) == JURIS_E_ARG, "market 2");
    s.market = 0;
    s.standard = ACCT_STD_COUNT;
    CHECK(juris_host_apply_settings(&h, &s) == JURIS_E_ARG, "standard out of range");
    s.standard = 0;
    s.country[1] = 'X';
    CHECK(juris_host_apply_settings(&h, &s) == JURIS_E_ARG, "\"\" with junk after it");
    memcpy(s.country, "K1", 3);
    CHECK(juris_host_apply_settings(&h, &s) == JURIS_E_ARG, "K1");
    memcpy(s.country, "KE", 3);
    CHECK(juris_host_apply_settings(&h, &s) == JURIS_OK, "KE");
    CHECK(juris_host_apply_settings(NULL, &s) == JURIS_E_ARG &&
              juris_host_apply_settings(&h, NULL) == JURIS_E_ARG,
          "NULLs");

    /* hooks are asked only when declared */
    fake_t no = {T(LPX_B_FALSE), 1, 2, 0, ACCT_STD_IFRS};
    juris_plugin_t p;
    mk(&p, "only-tax", "KE", JURIS_HOOK_TAX, &no);
    no.answer = T(LPX_B_TRUE);
    CHECK(reg(&h, &p, 1) == JURIS_OK, "tax-only plug-in");
    no.answer = T(LPX_B_FALSE);
    juris_transfer_t t;
    memset(&t, 0, sizeof t);
    t.amount = 3;
    CHECK(lpx_base(juris_check_transfer(&h, &t)) == LPX_B_NEITHER,
          "a plug-in without the transfer hook is not asked");
    CHECK(lpx_base(juris_check_market(&h, JURIS_MARKET_CLOSED)) == LPX_B_TRUE,
          "nor about the market");
    juris_commit_transfer(&h, &t);
    CHECK(no.commits == 0, "nor told about commits");
    s.standard = ACCT_STD_IFRS;
    juris_host_apply_settings(&h, &s);
    CHECK(juris_host_standard(&h) == ACCT_STD_IFRS, "nor about the framework");
    uint64_t n = 9, d = 9;
    CHECK(juris_host_tax_rate(&h, JURIS_TAX_INCOME, &n, &d) == JURIS_E_NO_RULE && n == 0 && d == 0,
          "a FALSE tax answer: no rate");
    no.answer = T(LPX_B_TRUE);
    no.den = 0;
    CHECK(juris_host_tax_rate(&h, JURIS_TAX_INCOME, &n, &d) == JURIS_E_NO_RULE, "den 0");
    no.den = 2;
    fake_t unk = {T(LPX_B_NEITHER), 0, 0, 0, ACCT_STD_NONE};
    juris_plugin_t u;
    mk(&u, "unknown", "KE", JURIS_HOOK_TAX, &unk);
    CHECK(reg(&h, &u, 1) == JURIS_OK, "a plug-in that does not know");
    CHECK(juris_host_tax_rate(&h, JURIS_TAX_INCOME, &n, &d) == JURIS_OK && n == 1 && d == 2,
          "skipped; the other answers 1/2");
    CHECK(juris_host_tax_rate(&h, JURIS_TAX_COUNT, &n, &d) == JURIS_E_ARG, "bad kind");
    CHECK(juris_host_tax_rate(&h, JURIS_TAX_INCOME, NULL, &d) == JURIS_E_ARG, "NULL num");
    CHECK(lpx_base(juris_check_market(&h, (juris_market_t) 7)) == LPX_B_NEITHER, "bad market");
    CHECK(lpx_base(juris_check_transfer(&h, NULL)) == LPX_B_NEITHER, "NULL transfer");
    juris_plugin_t bad = p;
    memcpy(bad.country, "k1", 2);
    CHECK(reg(&h, &bad, 5) == JURIS_E_ARG, "a plug-in country that is not alpha-2");
    CHECK(juris_host_register(&h, &p, NULL, IMG, 1) == JURIS_E_ARG, "NULL manifest");
    juris_manifest_t m = manifest_for(&p, 7, IMG, sizeof IMG);
    CHECK(juris_host_register(&h, &p, &m, NULL, 4) == JURIS_E_ARG, "NULL image with a length");
    juris_plugin_t other = p;
    other.id[0] = 'X';
    CHECK(juris_host_register(&h, &other, &m, IMG, sizeof IMG) == JURIS_E_SIG,
          "a manifest for another id");
    memcpy(other.id, p.id, JURIS_ID_LEN);
    m.sig = NULL;
    CHECK(juris_host_register(&h, &p, &m, IMG, sizeof IMG) == JURIS_E_SIG, "no signature");

    /* J5: the quarantine record names the sender and the transfer's digest */
    fake_t fb = {lpx_forbid(T(LPX_B_TRUE)), 0, 1, 0, ACCT_STD_NONE};
    juris_plugin_t fp;
    mk(&fp, "forbid", "KE", JURIS_HOOK_TRANSFER, &fb);
    CHECK(reg(&h, &fp, 1) == JURIS_OK, "forbidding plug-in");
    memset(&t, 0, sizeof t);
    memcpy(t.from_country, "KE", 3);
    memcpy(t.to_country, "NG", 3);
    memcpy(t.ccy, "KES", 4);
    t.amount = 0x1122334455667788ull;
    t.sender = 0xA1B2C3D4u;
    t.receiver = 0x01020304u;
    t.epoch = 0x0807060504030201ull;
    CHECK(lpx_action(juris_check_transfer(&h, &t)) == LPX_ACT_ROLLBACK, "rollback");
    uint8_t ser[34], dg[32];
    size_t o = 0;
    memcpy(ser + o, "KE\0NG\0KES\0", 10);
    o = 10;
    for (int i = 0; i < 8; i++) ser[o++] = (uint8_t) (t.amount >> (8 * i));
    for (int i = 0; i < 4; i++) ser[o++] = (uint8_t) (t.sender >> (8 * i));
    for (int i = 0; i < 4; i++) ser[o++] = (uint8_t) (t.receiver >> (8 * i));
    for (int i = 0; i < 8; i++) ser[o++] = (uint8_t) (t.epoch >> (8 * i));
    sha256(ser, o, dg);
    CHECK(h.qlog.n == 1 && h.qlog.rec[0].submitter == t.sender &&
              memcmp(h.qlog.rec[0].subject, dg, 32) == 0 && h.qlog.rec[0].reason == 1,
          "the record binds the transfer");
    CHECK(!h.halted, "one record fits");
    CHECK(lpx_action(juris_check_market(&h, JURIS_MARKET_CLOSED)) == LPX_ACT_PROCEED,
          "a transfer quarantine does not stop the market check");
    t.amount = 1;
    juris_check_transfer(&h, &t);
    CHECK(h.qlog.n == 2 && !h.halted, "two records fit");
    juris_check_transfer(&h, &t);
    CHECK(h.halted, "the third halts");
    CHECK(juris_host_tax_rate(&h, JURIS_TAX_INCOME, &n, &d) == JURIS_E_HALTED, "halted: no tax");
    CHECK(lpx_forbidden(juris_check_market(&h, JURIS_MARKET_CLOSED)), "halted: market forbidden");
    CHECK(juris_host_standard(&h) == ACCT_STD_NONE, "halted: no framework");
    no.commits = 0;
    juris_commit_transfer(&h, &t);

    /* J3: the version table fills up, even across unloads */
    juris_host_t hv;
    juris_settings_t ke;
    juris_settings_default(&ke);
    memcpy(ke.country, "KE", 3);
    juris_host_init(&hv, &ke, gw_verify, NULL, NULL, 0);
    fake_t f = {T(LPX_B_TRUE), 0, 1, 0, ACCT_STD_NONE};
    static juris_plugin_t ps[JURIS_MAX_IDS + 1];
    unsigned okc = 0;
    for (unsigned i = 0; i <= JURIS_MAX_IDS; i++) {
        char id[16];
        snprintf(id, sizeof id, "v%u", i);
        mk(&ps[i], id, "KE", JURIS_HOOK_TRANSFER, &f);
        if (i % JURIS_MAX_PLUGINS == 0 && i) { /* unload by switching away and back */
            juris_settings_t ng = ke;
            memcpy(ng.country, "NG", 3);
            juris_host_apply_settings(&hv, &ng);
            juris_host_apply_settings(&hv, &ke);
        }
        juris_status_t r = reg(&hv, &ps[i], 1);
        okc += r == JURIS_OK;
        if (i == JURIS_MAX_IDS) CHECK(r == JURIS_E_FULL, "17th id: the version table is full");
    }
    CHECK(okc == JURIS_MAX_IDS, "16 ids remembered");
    CHECK(reg(&hv, &ps[0], 1) == JURIS_E_VERSION,
          "an unloaded id cannot come back at its old version");
    CHECK(reg(&hv, &ps[0], 2) == JURIS_OK, "a newer one can");

    /* the limit plug-in's fields */
    juris_plugin_t lp;
    vhub_limit_t lim;
    juris_limit_plugin(&lp, "0123456789012345678901234567890123456789", "KE", &lim);
    CHECK(memcmp(lp.id, "01234567890123456789012345678901", 32) == 0, "id cut at 32 bytes");
    CHECK(lp.abi == JURIS_PLUGIN_ABI && lp.size == sizeof lp &&
              lp.hooks == (JURIS_HOOK_TRANSFER | JURIS_HOOK_COMMIT) && lp.ctx == &lim &&
              strcmp(lp.country, "KE") == 0,
          "limit plug-in fields");
    juris_limit_plugin(&lp, NULL, "K", &lim);
    CHECK(lp.id[0] == 0 && lp.country[0] == 0, "no id, a one-letter country stays empty");
    juris_limit_plugin(&lp, "x", NULL, &lim);
    CHECK(lp.id[0] == 'x' && lp.id[1] == 0 && lp.country[0] == 0, "NULL country");
    juris_limit_plugin(NULL, "x", "KE", &lim);
    CHECK(lpx_base(juris_vhub_rule(NULL, NULL, 0)) == LPX_B_NEITHER, "vhub rule without a host");
    juris_vhub_commit(NULL, NULL, 0);
}

int main(void)
{
    test_parse();
    test_register();
    test_checks();
    test_market_tax_standard();
    test_parse_more();
    test_manifest_bytes();
    test_host_more();
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}
