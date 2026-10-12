/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_juris.c — the jurisdiction plug-in ABI (juris.h J1-J7): zxvd's
 * jurisdiction.get JSON (the shapes PR #69's tests send), signed manifests,
 * the settings gate, fail-closed combining, tax-rate agreement, the
 * accounting framework, and vhub's limit rule as the first plug-in. */
#include <stdio.h>
#include <string.h>
#include "juris.h"
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
    CHECK(parse("{\"standard\":\"SAP_INSURER\",\"country\":\"ZA\",\"in_effect\":true}", &s) == JURIS_OK &&
              s.standard == ACCT_STD_SAP_INSURER && s.in_effect && s.market == JURIS_MARKET_CLOSED,
          "missing keys keep the defaults");
    CHECK(parse("{}", &s) == JURIS_OK && s.country[0] == 0, "empty object");
    CHECK(parse("{\"country\":\"\\u004E\\u0047\",\"x\":{\"a\":[1,-2.5e3,null,true,{}]},\"y\":\"z\"}",
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
    CHECK(juris_host_tax_rate(&h, JURIS_TAX_INCOME, &n, &d) == JURIS_E_NO_RULE, "a rate above 100%%");
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

int main(void)
{
    test_parse();
    test_register();
    test_checks();
    test_market_tax_standard();
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}
