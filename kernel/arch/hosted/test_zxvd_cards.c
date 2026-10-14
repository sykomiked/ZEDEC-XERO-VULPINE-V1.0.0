/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxvd_cards.c — zxvd_cards.c (cards.*) behind the real RPC layer on
 * the real cardnet engine. Every reply is compared exactly with a shadow
 * issuer driven through the same engine calls, so PANs, approval codes,
 * cryptograms, receipt digests and ISO 8583 bytes are all checked. Holder
 * signatures are real ML-DSA-65 signatures made here with the holder key. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxvd_groups.h"
#include "cardnet.h"
#include "cn_emv.h"
#include "cn_iso8583.h"
#include "web4_util.h"

void pq_mldsa65_keygen(const uint8_t seed[32], uint8_t pk[1952], uint8_t sk[4032]);

static int fails, checks;

static char *sdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL %s:%d ", __FILE__, __LINE__);                                           \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static zxvd_buf_t OUT;
static char REQB[16000], WANT[20000];

static const char *rpc(const char *line)
{
    CHECK(zxvd_rpc_handle_line(line, strlen(line), &OUT), "a reply for %.200s", line);
    return OUT.p ? OUT.p : "";
}

static void expect(const char *req, const char *want, int line)
{
    const char *got = rpc(req);
    checks++;
    if (strcmp(got, want)) {
        fails++;
        printf("  FAIL line %d\n    req  %.300s\n    want %.400s\n    got  %.400s\n", line, req,
               want, got);
    }
}
#define EXPECT(req, want) expect(req, want, __LINE__)

static const char *req(const char *method, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
static const char *req(const char *method, const char *fmt, ...)
{
    va_list ap;
    int k = snprintf(REQB, sizeof REQB,
                     "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":", method);
    va_start(ap, fmt);
    k += vsnprintf(REQB + k, sizeof REQB - (size_t) k, fmt, ap);
    va_end(ap);
    snprintf(REQB + k, sizeof REQB - (size_t) k, "}");
    return REQB;
}

static const char *ok(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static const char *ok(const char *fmt, ...)
{
    va_list ap;
    int k = snprintf(WANT, sizeof WANT, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":");
    va_start(ap, fmt);
    k += vsnprintf(WANT + k, sizeof WANT - (size_t) k, fmt, ap);
    va_end(ap);
    snprintf(WANT + k, sizeof WANT - (size_t) k, "}");
    return WANT;
}

static const char *errc(int code, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static const char *errc(int code, const char *fmt, ...)
{
    char msg[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    snprintf(WANT, sizeof WANT,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":%d,\"message\":\"%s\"}}", code,
             msg);
    return WANT;
}
#define ERR(...)  errc(-32040, __VA_ARGS__)
#define PERR(...) errc(-32602, __VA_ARGS__)

/* hex into one of four rotating buffers */
static const char *hx(const uint8_t *p, uint32_t n)
{
    static char b[4][8000];
    static int i;
    i = (i + 1) & 3;
    w4_hex_encode(p, n, b[i], sizeof b[i]);
    return b[i];
}

/* ----- fixtures ----- */

#define EPOCH 946684800ll
static uint8_t PKA[CN_PK_BYTES], SKA[CN_SK_BYTES], PKB[CN_PK_BYTES], SKB[CN_SK_BYTES];
static uint8_t SIG[CN_SIG_BYTES];
static cn_issuer_t S; /* the shadow issuer */
static cn_time_t T0;  /* 2026-10-09 12:00:00Z */
static uint8_t RCOUNT;
static bool RFAIL;

static int fake_random(uint8_t *p, size_t n)
{
    if (RFAIL) return -1;
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t) (RCOUNT + i * 3u);
    RCOUNT++;
    return 0;
}

static void seed_like_fake(uint8_t out[32], uint8_t k)
{
    for (size_t i = 0; i < 32; i++) out[i] = (uint8_t) (k + i * 3u);
}

static void ent(uint8_t e[32], uint32_t k)
{
    for (int i = 0; i < 32; i++) e[i] = (uint8_t) (k * 31u + (uint32_t) i * 17u);
}

static long long U(cn_time_t t)
{
    return (long long) t + EPOCH;
}

static void mkreq(cn_auth_req_t *r, const char *pan, uint64_t amt, uint32_t atc, cn_time_t t)
{
    memset(r, 0, sizeof *r);
    memcpy(r->pan, pan, 16);
    r->amount_minor = amt;
    r->currency = CN_RAIL_DEBIT;
    r->form = CN_FORM_FINANCIAL;
    r->atc = atc;
    r->un[0] = 0xDE;
    r->un[1] = 0xAD;
    r->un[2] = (uint8_t) atc;
    r->un[3] = 0x42;
    r->time = t;
    r->stan = 100000u + atc;
    memcpy(r->terminal_id, "TERM0001", 8);
    memcpy(r->merchant_id, "MERCHANT0000042", 15);
}

/* the request's fields as JSON params (no braces) */
static const char *fields(const cn_auth_req_t *r)
{
    static char f[600];
    snprintf(f, sizeof f,
             "\"pan\":\"%s\",\"amount_minor\":%llu,\"form\":%u,\"atc\":%u,\"un\":\"%s\","
             "\"stan\":%u,\"terminal_id\":\"%s\",\"merchant_id\":\"%s\",\"time\":%lld",
             r->pan, (unsigned long long) r->amount_minor, (unsigned) r->form, (unsigned) r->atc,
             hx(r->un, 4), (unsigned) r->stan, r->terminal_id, r->merchant_id, U(r->time));
    return f;
}

/* the fields with one value replaced (raw JSON) */
static const char *fieldx(const cn_auth_req_t *r, const char *key, const char *raw)
{
    static char f[700];
    char k[40];
    const char *all = fields(r);
    snprintf(k, sizeof k, "\"%s\":", key);
    const char *at = strstr(all, k);
    const char *end = at ? strstr(at + 1, ",\"") : NULL;
    if (!at) return all;
    snprintf(f, sizeof f, "%.*s%s%s%s", (int) (at - all), all, k, raw, end ? end : "");
    return f;
}

static const char *card_json(uint32_t i)
{
    static char s[400];
    const cn_card_t *k = &S.cards[i];
    static const char *const ST[] = {"?", "active", "frozen", "revoked"};
    char rep[16];
    if (k->replaces == UINT32_MAX)
        snprintf(rep, sizeof rep, "null");
    else
        snprintf(rep, sizeof rep, "%u", (unsigned) k->replaces);
    snprintf(s, sizeof s,
             "{\"card\":%u,\"pan\":\"%s\",\"network\":\"%s\",\"account\":%u,\"form\":%u,"
             "\"state\":\"%s\",\"exp_year\":%u,\"exp_month\":%u,\"atc\":%u,\"replaces\":%s}",
             (unsigned) i, k->pan, cn_network_name(cn_pan_network(k->pan, 16)),
             (unsigned) k->account, (unsigned) k->form, ST[k->state], (unsigned) k->exp_year,
             (unsigned) k->exp_month, (unsigned) k->atc, rep);
    return s;
}

static const char *acct_json(uint32_t i)
{
    static char s[400];
    const cn_account_t *a = &S.accounts[i];
    static const char *const ST[] = {"?", "active", "delinquent", "closed"};
    char due[24];
    if (a->billed)
        snprintf(due, sizeof due, "%lld", (long long) a->due_day * 86400 + EPOCH);
    else
        snprintf(due, sizeof due, "null");
    snprintf(s, sizeof s,
             "{\"account\":%u,\"state\":\"%s\",\"held_minor\":\"%llu\",\"unbilled_minor\":\"%llu\","
             "\"billed_minor\":\"%llu\",\"owed_minor\":\"%llu\",\"due\":%s}",
             (unsigned) i, ST[a->state], (unsigned long long) a->held,
             (unsigned long long) a->unbilled, (unsigned long long) a->billed,
             (unsigned long long) cn_account_owed(&S, i), due);
    return s;
}

static const char *auth_json(uint32_t ai)
{
    static char s[1200];
    static const char *const D[] = {"none",    "bad_pan",    "unknown_card", "revoked",
                                    "frozen",  "expired",    "bad_sig",      "replay",
                                    "form",    "currency",   "txn_limit",    "cycle_limit",
                                    "ceiling", "delinquent", "format",       "system"};
    static cn8583_msg_t m;
    static uint8_t w[CN8583_MAX_WIRE];
    uint32_t n = 0;
    const cn_auth_t *au = &S.auths[ai];
    CHECK(cn8583_response_for(&m, 110, au) == CN8583_OK &&
              cn8583_pack(&m, w, sizeof w, &n) == CN8583_OK,
          "shadow 0110");
    char appr[16];
    if (au->state == CN_AUTH_APPROVED)
        snprintf(appr, sizeof appr, "\"%s\"", au->approval);
    else
        snprintf(appr, sizeof appr, "null");
    snprintf(s, sizeof s,
             "{\"auth\":%u,\"approved\":%s,\"rc\":\"%s\",\"decline\":\"%s\",\"approval\":%s,"
             "\"cryptogram\":\"%s\",\"amount_minor\":\"%llu\",\"iso8583\":\"%s\"}",
             (unsigned) ai, au->state == CN_AUTH_APPROVED ? "true" : "false", au->rc,
             D[au->decline], appr, hx(au->cryptogram, 8), (unsigned long long) au->req.amount_minor,
             hx(w, n));
    return s;
}

static const char *posted_json(uint32_t ai)
{
    static char s[600];
    static const char *const ST[] = {"?", "approved", "declined", "settled", "reversed"};
    const cn_auth_t *au = &S.auths[ai];
    const cn_receipt_t *r = &S.receipts[S.n_receipts - 1];
    snprintf(s, sizeof s,
             "{\"auth\":%u,\"state\":\"%s\",\"txn\":%u,\"receipt\":%u,\"amount_minor\":\"%llu\","
             "\"fee_minor\":\"%llu\",\"digest\":\"%s\",\"account\":%u}",
             (unsigned) ai, ST[au->state], (unsigned) au->txn, (unsigned) r->id,
             (unsigned long long) r->amount, (unsigned long long) r->fee, hx(r->digest, 32),
             (unsigned) au->account);
    return s;
}

/* sign r with sk, authorize on both sides, compare the reply */
static uint32_t authorize(const cn_auth_req_t *r, const uint8_t *sk, int line)
{
    uint32_t ai = 0;
    cn_holder_sign(sk, r, NULL, SIG);
    CHECK(cn_authorize(&S, r, SIG, &ai) == CN_OK, "shadow authorize");
    char *want = sdup(ok("%s", auth_json(ai)));
    expect(req("cards.authorize", "{%s,\"sig\":\"%s\"}", fields(r), hx(SIG, CN_SIG_BYTES)), want,
           line);
    free(want);
    return ai;
}
#define AUTHORIZE(r, sk) authorize(r, sk, __LINE__)

static const char *status_json(void)
{
    static char s[800];
    uint32_t accts = 0, cards = 0, ap = 0, de = 0;
    for (uint32_t i = 0; i < CN_MAX_ACCOUNTS; i++) accts += S.accounts[i].used;
    for (uint32_t i = 0; i < CN_MAX_CARDS; i++) cards += S.cards[i].used;
    for (uint32_t i = 0; i < S.n_auths; i++) {
        ap += S.auths[i].state != CN_AUTH_DECLINED;
        de += S.auths[i].state == CN_AUTH_DECLINED;
    }
    snprintf(s, sizeof s,
             "{\"issuer\":true,\"network\":\"%s\",\"prefix\":\"%s\",\"issuer_code\":\"%s\","
             "\"fee_minor\":\"%llu\",\"accounts\":%u,\"cards\":%u,\"approved\":%u,\"declined\":%u,"
             "\"txns\":%u,\"receipts\":%u,\"statements\":%u,\"ledger_balanced\":true,"
             "\"receipts_ok\":true,\"in_memory\":true}",
             cn_network_name((cn_network_t) S.cfg.network),
             cn_network_prefix((cn_network_t) S.cfg.network), S.cfg.issuer_code,
             (unsigned long long) S.cfg.merchant_fee.amount_minor, (unsigned) accts,
             (unsigned) cards, (unsigned) ap, (unsigned) de, (unsigned) S.n_txns,
             (unsigned) S.n_receipts, (unsigned) S.n_statements);
    return s;
}

static void fresh(void)
{
    zxvd_rpc_groups_reset();
    CHECK(zxvd_rpc_add_group(zxvd_cards_group()) == 0, "add the cards group");
    zxvd_cards_set_random(fake_random);
    RCOUNT = 0;
    RFAIL = false;
}

/* ----- tests ----- */

static void test_switch(void)
{
    fresh();
    const zxvd_group_t *g = zxvd_cards_group();
    CHECK(g->regulated && !strcmp(g->feature, "cards") && g->n_methods == 17, "group shape");
    const char *d = rpc(req("rpc.discover", "{}"));
    CHECK(strstr(d, "\"play.draft\",\"cards.status\",\"cards.check_pan\","
                    "\"cards.iso8583_parse\",\"cards.issuer\",\"cards.open_account\","
                    "\"cards.account\",\"cards.issue\",\"cards.reissue\",\"cards.freeze\","
                    "\"cards.card\",\"cards.challenge\",\"cards.authorize\",\"cards.capture\","
                    "\"cards.reverse\",\"cards.refund\",\"cards.pay\",\"cards.advance\"]") != NULL,
          "discover lists the cards methods: %.200s", d);
    /* X4: off, regulated */
    EXPECT(req("cards.issuer", "{\"network\":\"dragon\"}"),
           errc(-32012, "cards is off: start zxvd with --enable cards, or call features.set "
                        "(regulated: test or licensed use only)"));
    EXPECT(req("cards.authorize", "{}"),
           errc(-32012, "cards is off: start zxvd with --enable cards, or call features.set "
                        "(regulated: test or licensed use only)"));
    EXPECT(req("cards.status", "{}"), ok("{\"issuer\":false,\"in_memory\":true}"));
    EXPECT(req("cards.check_pan", "{\"pan\":\"4111111111111111\"}"),
           ok("{\"digits\":16,\"iin\":\"411111\",\"luhn\":true,\"damm\":false,\"verhoeff\":false,"
              "\"network\":null}"));
    EXPECT(req("features.get", "{}"),
           ok("{\"features\":[{\"name\":\"cards\",\"enabled\":false,\"regulated\":true,"
              "\"groups\":[{\"note\":\"Dragon/Phoenix/Thunderbird charge cards "
              "(kernel/src/cardnet): in memory, no scheme membership, no PCI DSS claim\","
              "\"methods\":17}]}],\"saved\":false}"));
    CHECK(zxvd_rpc_enable("cards") == 0 && zxvd_rpc_enabled("cards"), "enable cards");
}

static void test_check_pan(void)
{
    EXPECT(req("cards.check_pan", "{\"pan\":\"79927398713\"}"),
           ok("{\"digits\":11,\"iin\":\"799273\",\"luhn\":true,\"damm\":false,\"verhoeff\":false,"
              "\"network\":null}"));
    EXPECT(req("cards.check_pan", "{\"pan\":\"5724\"}"),
           ok("{\"digits\":4,\"iin\":\"5724\",\"luhn\":false,\"damm\":true,\"verhoeff\":false,"
              "\"network\":null}"));
    EXPECT(req("cards.check_pan", "{\"pan\":\"2363\"}"),
           ok("{\"digits\":4,\"iin\":\"2363\",\"luhn\":false,\"damm\":false,\"verhoeff\":true,"
              "\"network\":null}"));
    EXPECT(req("cards.check_pan", "{\"pan\":\"12\"}"),
           ok("{\"digits\":2,\"iin\":\"12\",\"luhn\":false,\"damm\":false,\"verhoeff\":false,"
              "\"network\":null}"));
    EXPECT(req("cards.check_pan", "{\"pan\":\"1\"}"), PERR("\\\"pan\\\" must be 2 to 19 digits"));
    EXPECT(req("cards.check_pan", "{\"pan\":\"12a4\"}"),
           PERR("\\\"pan\\\" must be 2 to 19 digits"));
    EXPECT(req("cards.check_pan", "{\"pan\":\"a234\"}"),
           PERR("\\\"pan\\\" must be 2 to 19 digits"));
    EXPECT(req("cards.check_pan", "{\"pan\":\"123/\"}"),
           PERR("\\\"pan\\\" must be 2 to 19 digits"));
    EXPECT(req("cards.check_pan", "{\"pan\":\"123:\"}"),
           PERR("\\\"pan\\\" must be 2 to 19 digits"));
    EXPECT(req("cards.check_pan", "{\"pan\":\"12345678901234567890\"}"),
           PERR("\\\"pan\\\" must be a string of at most 19 bytes"));
    EXPECT(req("cards.check_pan", "{}"), PERR("missing string parameter \\\"pan\\\""));
}

static void test_issuer(void)
{
    EXPECT(req("cards.open_account", "{}"), ERR("no issuer yet: call cards.issuer first"));
    EXPECT(req("cards.authorize", "{}"), ERR("no issuer yet: call cards.issuer first"));
    EXPECT(req("cards.card", "{\"card\":0}"), ERR("no issuer yet: call cards.issuer first"));
    EXPECT(req("cards.issuer", "{}"), PERR("missing string parameter \\\"network\\\""));
    EXPECT(req("cards.issuer", "{\"network\":\"visa\"}"),
           PERR("\\\"network\\\" must be dragon, phoenix or thunderbird"));
    EXPECT(req("cards.issuer", "{\"network\":\"phoenix\",\"issuer_code\":\"42\"}"),
           PERR("\\\"issuer_code\\\" must be 3 digits"));
    EXPECT(req("cards.issuer", "{\"network\":\"phoenix\",\"issuer_code\":\"4a2\"}"),
           ERR("issuer: the configuration was refused"));
    EXPECT(req("cards.issuer", "{\"network\":\"phoenix\",\"issuer_code\":\"12345678\"}"),
           PERR("\\\"issuer_code\\\" must be a string of at most 7 bytes"));
    EXPECT(req("cards.issuer", "{\"network\":\"phoenix\",\"fee_minor\":1000001}"),
           PERR("\\\"fee_minor\\\" must be 0 to 1000000"));
    EXPECT(req("cards.issuer", "{\"network\":\"phoenix\",\"fee_minor\":-1}"),
           PERR("\\\"fee_minor\\\" must be 0 to 1000000"));
    EXPECT(req("cards.issuer", "{\"network\":\"phoenix\",\"fee_minor\":1.5}"),
           PERR("\\\"fee_minor\\\" must be an integer or a decimal string (no fraction or "
                "exponent)"));
    EXPECT(req("cards.issuer", "{\"network\":\"phoenix\",\"seed\":\"00\"}"),
           PERR("\\\"seed\\\" must be 32 bytes of hex"));
    RFAIL = true;
    EXPECT(req("cards.issuer", "{\"network\":\"phoenix\"}"),
           errc(-32603, "no random bytes from the OS"));
    RFAIL = false;
    EXPECT(req("cards.status", "{}"), ok("{\"issuer\":false,\"in_memory\":true}"));

    /* Phoenix, issuer 042, a flat merchant fee of 25, the seed from the OS */
    cn_issuer_cfg_t cfg;
    cn_cfg_default(&cfg, CN_NET_PHOENIX);
    memcpy(cfg.issuer_code, "042", 4);
    cfg.merchant_fee.kind = CN_FEE_FLAT_PER_TXN;
    cfg.merchant_fee.amount_minor = 25;
    seed_like_fake(cfg.issuer_seed, 0);
    CHECK(cn_issuer_init(&S, &cfg) == CN_OK, "shadow issuer");
    RCOUNT = 0;
    EXPECT(req("cards.issuer", "{\"network\":\"phoenix\",\"issuer_code\":\"042\",\"fee_minor\":"
                               "\"25\"}"),
           ok("%s", status_json()));
    EXPECT(req("cards.issuer", "{\"network\":\"dragon\"}"),
           ERR("the issuer is already set up for this run"));
    EXPECT(req("cards.status", "{}"), ok("%s", status_json()));
}

static void test_accounts_and_cards(void)
{
    uint32_t a, k;
    uint8_t e[32];
    CHECK(cn_open_account(&S, T0, &a) == CN_OK && a == 0, "shadow account");
    EXPECT(req("cards.open_account", "{\"now\":%lld}", U(T0)), ok("%s", acct_json(0)));
    EXPECT(req("cards.open_account", "{\"now\":946684799}"),
           PERR("\\\"now\\\" must be unix seconds from 2000 to 2136"));
    EXPECT(req("cards.open_account", "{\"now\":5241652096}"),
           PERR("\\\"now\\\" must be unix seconds from 2000 to 2136"));
    EXPECT(req("cards.open_account", "{\"now\":\"x\"}"),
           PERR("\\\"now\\\" must be an integer (no fraction or exponent)"));
    EXPECT(req("cards.account", "{\"account\":0}"), ok("%s", acct_json(0)));
    EXPECT(req("cards.account", "{\"account\":1}"), ERR("account 1: not found"));
    EXPECT(req("cards.account", "{\"account\":8}"), ERR("account 8: not found"));
    EXPECT(req("cards.account", "{\"account\":7}"), ERR("account 7: not found"));
    EXPECT(req("cards.account", "{}"), PERR("missing integer parameter \\\"account\\\""));

    /* issue: Social is a Crown form, never a card */
    ent(e, 1);
    char *pk = sdup(hx(PKA, CN_PK_BYTES));
    EXPECT(req("cards.issue", "{\"account\":0,\"form\":4,\"holder_pk\":\"%s\",\"entropy\":\"%s\"}",
               pk, hx(e, 32)),
           ERR("issue: that capital form cannot carry a card"));
    EXPECT(req("cards.issue", "{\"account\":0,\"form\":0,\"holder_pk\":\"00\"}"),
           PERR("\\\"holder_pk\\\" must be 1952 bytes of hex"));
    EXPECT(req("cards.issue", "{\"account\":1,\"form\":0,\"holder_pk\":\"%s\"}", pk),
           ERR("account 1: not found"));
    EXPECT(req("cards.issue", "{\"account\":0,\"holder_pk\":\"%s\"}", pk),
           PERR("missing integer parameter \\\"form\\\""));
    EXPECT(req("cards.issue",
               "{\"account\":0,\"form\":0,\"holder_pk\":\"%s\",\"entropy\":\"0011\"}", pk),
           PERR("\\\"entropy\\\" must be 32 bytes of hex"));
    CHECK(cn_issue_card(&S, a, CN_FORM_FINANCIAL, PKA, e, T0, &k) == CN_OK && k == 0, "shadow");
    EXPECT(req("cards.issue",
               "{\"account\":0,\"form\":0,\"holder_pk\":\"%s\",\"entropy\":\"%s\",\"now\":%lld}",
               pk, hx(e, 32), U(T0)),
           ok("%s", card_json(0)));
    CHECK(!strncmp(S.cards[0].pan, "882042", 6), "a Phoenix PAN of issuer 042");
    /* entropy from the OS (the fake source's next draw is number 1) */
    char *pkb = sdup(hx(PKB, CN_PK_BYTES));
    seed_like_fake(e, 1);
    CHECK(cn_issue_card(&S, a, CN_FORM_MATERIAL, PKB, e, T0, &k) == CN_OK && k == 1, "shadow 2");
    EXPECT(req("cards.issue", "{\"account\":0,\"form\":1,\"holder_pk\":\"%s\",\"now\":%lld}", pkb,
               U(T0)),
           ok("%s", card_json(1)));
    RFAIL = true;
    EXPECT(req("cards.issue", "{\"account\":0,\"form\":0,\"holder_pk\":\"%s\"}", pk),
           errc(-32603, "no random bytes from the OS"));
    RFAIL = false;

    /* the card by index and by PAN, and check_pan on it */
    EXPECT(req("cards.card", "{\"card\":0}"), ok("%s", card_json(0)));
    EXPECT(req("cards.card", "{\"pan\":\"%s\"}", S.cards[1].pan), ok("%s", card_json(1)));
    EXPECT(req("cards.card", "{\"pan\":\"8820420000000000\"}"), ERR("card: not found"));
    EXPECT(req("cards.card", "{\"pan\":\"882042\"}"), ERR("card: not found"));
    EXPECT(req("cards.card", "{\"card\":2}"), ERR("card 2: not found"));
    EXPECT(req("cards.card", "{\"card\":16}"), ERR("card 16: not found"));
    EXPECT(req("cards.card", "{\"card\":15}"), ERR("card 15: not found"));
    EXPECT(req("cards.card", "{}"), PERR("missing integer parameter \\\"card\\\""));
    EXPECT(req("cards.card", "{\"pan\":7}"),
           PERR("\\\"pan\\\" must be a string of at most 17 bytes"));
    char *chk = sdup(rpc(req("cards.check_pan", "{\"pan\":\"%s\"}", S.cards[0].pan)));
    CHECK(strstr(chk, "\"damm\":true") && strstr(chk, "\"network\":\"Phoenix\"}") &&
              strstr(chk, "\"iin\":\"882042\""),
          "check_pan names the network: %s", chk);
    free(chk);
    free(pk);
    free(pkb);
}

static void test_authorize_flow(void)
{
    cn_auth_req_t r;
    const char *pan = S.cards[0].pan;
    char *want;

    /* the challenge the holder signs */
    uint8_t ch[CN_CHALLENGE_MAX];
    uint32_t n = 0;
    mkreq(&r, pan, 10000, 1, T0);
    CHECK(cn_challenge(&r, ch, &n) == CN_OK, "shadow challenge");
    want = sdup(ok("{\"challenge\":\"%s\",\"context\":\"ZXV-CARDNET-ARQC-v1\",\"time\":%lld}",
                   hx(ch, n), U(T0)));
    EXPECT(req("cards.challenge", "{%s}", fields(&r)), want);
    free(want);
    EXPECT(req("cards.challenge", "{%s}", fieldx(&r, "terminal_id", "\"T1\"")),
           ERR("challenge: bad argument"));

    /* approved, a replay, a bad signature, the wrong holder */
    uint32_t a1 = AUTHORIZE(&r, SKA);
    CHECK(S.auths[a1].state == CN_AUTH_APPROVED, "approved");
    AUTHORIZE(&r, SKA); /* replay: the same ATC */
    CHECK(S.auths[1].decline == CN_DECL_REPLAY, "replay declined");
    mkreq(&r, pan, 500, 2, T0);
    AUTHORIZE(&r, SKB);
    CHECK(S.auths[2].decline == CN_DECL_BAD_SIG, "wrong key declined");
    /* the currency must be rail 555 */
    mkreq(&r, pan, 500, 3, T0);
    r.currency = 840;
    cn_holder_sign(SKA, &r, NULL, SIG);
    uint32_t ai;
    CHECK(cn_authorize(&S, &r, SIG, &ai) == CN_OK && S.auths[ai].decline == CN_DECL_CURRENCY,
          "shadow currency");
    want = sdup(ok("%s", auth_json(ai)));
    EXPECT(req("cards.authorize", "{%s,\"currency\":840,\"sig\":\"%s\"}", fields(&r),
               hx(SIG, CN_SIG_BYTES)),
           want);
    free(want);
    /* a second approval, reversed before capture */
    mkreq(&r, pan, 700, 4, T0);
    uint32_t a2 = AUTHORIZE(&r, SKA);
    EXPECT(req("cards.account", "{\"account\":0}"), ok("%s", acct_json(0)));
    CHECK(S.accounts[0].held == 10700, "two holds");

    /* parameter errors */
    EXPECT(req("cards.authorize", "{%s}", fields(&r)), PERR("missing hex parameter \\\"sig\\\""));
    EXPECT(req("cards.authorize", "{%s,\"sig\":\"00\"}", fields(&r)),
           PERR("\\\"sig\\\" must be 3309 bytes of hex"));
    EXPECT(req("cards.authorize",
               "{\"pan\":\"%s\",\"amount_minor\":1,\"form\":0,\"atc\":5,"
               "\"un\":\"00000000\",\"stan\":1,\"terminal_id\":\"TERM0001\","
               "\"merchant_id\":\"MERCHANT0000042\"}",
               pan),
           PERR("missing integer parameter \\\"time\\\""));
    EXPECT(req("cards.authorize", "{%s}", fieldx(&r, "amount_minor", "0")),
           PERR("\\\"amount_minor\\\" must be 1 to 999999999999"));
    EXPECT(req("cards.authorize", "{%s}", fieldx(&r, "amount_minor", "1000000000000")),
           PERR("\\\"amount_minor\\\" must be 1 to 999999999999"));
    EXPECT(req("cards.authorize", "{%s}", fieldx(&r, "un", "\"000000\"")),
           PERR("\\\"un\\\" must be 4 bytes of hex"));
    EXPECT(req("cards.authorize", "{%s}", fieldx(&r, "pan", "\"88204200000000001\"")),
           PERR("pan, terminal_id or merchant_id is too long (16, 8, 15)"));
    EXPECT(req("cards.authorize", "{%s}", fieldx(&r, "terminal_id", "\"TERM00012\"")),
           PERR("pan, terminal_id or merchant_id is too long (16, 8, 15)"));
    EXPECT(req("cards.authorize", "{%s}", fieldx(&r, "merchant_id", "\"MERCHANT00000421\"")),
           PERR("pan, terminal_id or merchant_id is too long (16, 8, 15)"));
    EXPECT(req("cards.authorize", "{%s,\"currency\":-1}", fields(&r)),
           PERR("\\\"currency\\\" is out of range"));
    EXPECT(req("cards.authorize", "{%s}", fieldx(&r, "stan", "\"1\"")),
           PERR("\\\"stan\\\" must be an integer (no fraction or exponent)"));
    EXPECT(req("cards.authorize", "{\"emv\":\"e100\"}"),
           PERR("\\\"emv\\\" is not a valid authorization container"));
    EXPECT(req("cards.authorize", "{\"emv\":\"e\"}"),
           PERR("\\\"emv\\\" must be a hex string of at most 3500 bytes"));

    /* capture the first: DR 555 / CR 777 with the flat fee of 25 */
    CHECK(cn_settle(&S, a1) == CN_OK, "shadow settle");
    EXPECT(req("cards.capture", "{\"auth\":%u}", (unsigned) a1), ok("%s", posted_json(a1)));
    CHECK(S.receipts[0].fee == 25 && S.legs[1].amount == 9975, "the fee leg");
    EXPECT(req("cards.capture", "{\"auth\":%u}", (unsigned) a1),
           ERR("capture: not allowed in its current state"));
    EXPECT(req("cards.capture", "{\"auth\":%u}", (unsigned) S.n_auths),
           ERR("auth %u: not found", (unsigned) S.n_auths));
    EXPECT(req("cards.capture", "{\"auth\":1}"), ERR("capture: not allowed in its current state"));
    EXPECT(req("cards.capture", "{}"), PERR("missing integer parameter \\\"auth\\\""));

    /* reverse the second (release the hold); refusals name the state */
    EXPECT(req("cards.reverse", "{\"auth\":%u}", (unsigned) a1),
           ERR("reverse: auth %u is settled, not approved", (unsigned) a1));
    EXPECT(req("cards.reverse", "{\"auth\":1}"), ERR("reverse: auth 1 is declined, not approved"));
    EXPECT(req("cards.refund", "{\"auth\":%u}", (unsigned) a2),
           ERR("refund: auth %u is approved, not settled", (unsigned) a2));
    EXPECT(req("cards.reverse", "{\"auth\":%u,\"now\":1}", (unsigned) a2),
           PERR("\\\"now\\\" must be unix seconds from 2000 to 2136"));
    CHECK(cn_reverse(&S, a2, T0) == CN_OK, "shadow reverse");
    EXPECT(req("cards.reverse", "{\"auth\":%u,\"now\":%lld}", (unsigned) a2, U(T0)),
           ok("{\"auth\":%u,\"state\":\"reversed\",\"released_minor\":\"700\"}", (unsigned) a2));
    EXPECT(req("cards.reverse", "{\"auth\":%u}", (unsigned) a2),
           ERR("reverse: auth %u is reversed, not approved", (unsigned) a2));
    EXPECT(req("cards.account", "{\"account\":0}"), ok("%s", acct_json(0)));

    /* a third approval, captured, then refunded in full */
    mkreq(&r, pan, 3000, 5, T0 + 60);
    uint32_t a3 = AUTHORIZE(&r, SKA);
    CHECK(cn_settle(&S, a3) == CN_OK, "shadow settle 3");
    EXPECT(req("cards.capture", "{\"auth\":%u}", (unsigned) a3), ok("%s", posted_json(a3)));
    CHECK(cn_reverse(&S, a3, T0 + 120) == CN_OK, "shadow refund");
    EXPECT(req("cards.refund", "{\"auth\":%u,\"now\":%lld}", (unsigned) a3, U(T0 + 120)),
           ok("%s", posted_json(a3)));
    CHECK(S.auths[a3].state == CN_AUTH_REVERSED && S.receipts[2].kind == CN_TXN_REFUND,
          "a refund receipt");
    EXPECT(req("cards.refund", "{\"auth\":%u}", (unsigned) a3),
           ERR("refund: auth %u is reversed, not settled", (unsigned) a3));
    EXPECT(req("cards.refund", "{\"auth\":%u,\"now\":0}", (unsigned) a3),
           PERR("\\\"now\\\" must be unix seconds from 2000 to 2136"));
    EXPECT(req("cards.status", "{}"), ok("%s", status_json()));
    EXPECT(req("cards.account", "{\"account\":0}"), ok("%s", acct_json(0)));
}

static void test_iso8583(void)
{
    /* the 0110 an authorization returns, read back */
    cn_auth_req_t r;
    mkreq(&r, S.cards[0].pan, 1234, 6, T0 + 30);
    uint32_t ai = AUTHORIZE(&r, SKA);
    static cn8583_msg_t m;
    static uint8_t w[CN8583_MAX_WIRE];
    uint32_t n = 0;
    CHECK(cn8583_response_for(&m, 110, &S.auths[ai]) == CN8583_OK &&
              cn8583_pack(&m, w, sizeof w, &n) == CN8583_OK,
          "shadow 0110");
    EXPECT(req("cards.iso8583_parse", "{\"hex\":\"%s\"}", hx(w, n)),
           ok("{\"mti\":\"0110\",\"fields\":{\"3\":\"000000\",\"4\":\"000000001234\",\"11\":"
              "\"100006\",\"37\":\"000000100006\",\"38\":\"%s\",\"39\":\"00\",\"41\":"
              "\"TERM0001\"}}",
              S.auths[ai].approval));
    /* a 0100 with VSS fields and a secondary bitmap-free layout */
    cn_holder_sign(SKA, &r, NULL, SIG);
    CHECK(cn8583_from_auth_req(&m, 100, &r, SIG) == CN8583_OK &&
              cn8583_pack(&m, w, sizeof w, &n) == CN8583_OK,
          "a 0100");
    const char *got = rpc(req("cards.iso8583_parse", "{\"hex\":\"%s\"}", hx(w, n)));
    char want[200];
    snprintf(want, sizeof want, "{\"mti\":\"0100\",\"fields\":{\"2\":\"%s\",", r.pan);
    CHECK(strstr(got, want) && strstr(got, "\"42\":\"MERCHANT0000042\",\"49\":\"555\""),
          "0100 parsed: %.300s", got);
    EXPECT(req("cards.iso8583_parse", "{\"hex\":\"30313130\"}"),
           PERR("\\\"hex\\\" is not an ISO 8583 message this codec reads"));
    EXPECT(req("cards.iso8583_parse", "{\"hex\":\"zz\"}"),
           PERR("\\\"hex\\\" must be a hex string of at most 4200 bytes"));
    EXPECT(req("cards.iso8583_parse", "{}"), PERR("missing hex parameter \\\"hex\\\""));
}

static void test_emv(void)
{
    static uint8_t buf[CN_EMV_AUTH_MAX];
    uint32_t n = 0, ai = 0;
    cn_auth_req_t r;
    const cn_card_t *k = &S.cards[0];
    mkreq(&r, k->pan, 4999, 7, T0 + 90);
    cn_holder_sign(SKA, &r, NULL, SIG);
    CHECK(cn_emv_encode_auth(&r, k->exp_year, k->exp_month, SIG, buf, sizeof buf, &n) == CN_TLV_OK,
          "EMV container");
    CHECK(cn_authorize(&S, &r, SIG, &ai) == CN_OK && S.auths[ai].state == CN_AUTH_APPROVED,
          "shadow");
    char *want = sdup(ok("%s", auth_json(ai)));
    EXPECT(req("cards.authorize", "{\"emv\":\"%s\"}", hx(buf, n)), want);
    free(want);
}

static void test_freeze_reissue(void)
{
    cn_auth_req_t r;
    EXPECT(req("cards.freeze", "{\"card\":0,\"frozen\":1}"),
           PERR("\\\"frozen\\\" must be true or false"));
    CHECK(cn_set_frozen(&S, 0, true) == CN_OK, "shadow freeze");
    EXPECT(req("cards.freeze", "{\"card\":0}"), ok("%s", card_json(0)));
    mkreq(&r, S.cards[0].pan, 100, 8, T0);
    AUTHORIZE(&r, SKA);
    CHECK(S.auths[S.n_auths - 1].decline == CN_DECL_FROZEN, "frozen declined");
    CHECK(cn_set_frozen(&S, 0, false) == CN_OK, "shadow thaw");
    EXPECT(req("cards.freeze", "{\"card\":0,\"frozen\":false}"), ok("%s", card_json(0)));
    EXPECT(req("cards.freeze", "{\"card\":3}"), ERR("card 3: not found"));

    /* reissue: a new PAN for the same account, the old card revoked */
    uint8_t e[32];
    uint32_t nk;
    ent(e, 9);
    char old[17];
    memcpy(old, S.cards[0].pan, 17);
    CHECK(cn_reissue_card(&S, 0, NULL, e, T0, &nk) == CN_OK, "shadow reissue");
    EXPECT(req("cards.reissue", "{\"card\":0,\"entropy\":\"%s\",\"now\":%lld}", hx(e, 32), U(T0)),
           ok("%s", card_json(nk)));
    EXPECT(req("cards.card", "{\"card\":0}"), ok("%s", card_json(0)));
    mkreq(&r, old, 100, 9, T0);
    AUTHORIZE(&r, SKA);
    CHECK(S.auths[S.n_auths - 1].decline == CN_DECL_REVOKED, "the old PAN is revoked");
    EXPECT(req("cards.reissue", "{\"card\":0,\"entropy\":\"%s\"}", hx(e, 32)),
           ERR("reissue: not allowed in its current state"));
    EXPECT(req("cards.reissue", "{\"card\":%u,\"holder_pk\":\"0011\"}", (unsigned) nk),
           PERR("\\\"holder_pk\\\" must be 1952 bytes of hex"));
    /* a new holder key on the next reissue */
    uint32_t nk2;
    ent(e, 10);
    CHECK(cn_reissue_card(&S, nk, PKB, e, T0, &nk2) == CN_OK, "shadow reissue 2");
    EXPECT(req("cards.reissue",
               "{\"card\":%u,\"holder_pk\":\"%s\",\"entropy\":\"%s\",\"now\":%lld}", (unsigned) nk,
               hx(PKB, CN_PK_BYTES), hx(e, 32), U(T0)),
           ok("%s", card_json(nk2)));
    mkreq(&r, S.cards[nk2].pan, 100, 1, T0);
    AUTHORIZE(&r, SKB);
    CHECK(S.auths[S.n_auths - 1].state == CN_AUTH_APPROVED, "the new key signs");
    RFAIL = true;
    EXPECT(req("cards.reissue", "{\"card\":%u}", (unsigned) nk2),
           errc(-32603, "no random bytes from the OS"));
    RFAIL = false;
}

static void test_statements(void)
{
    /* pay, advance past the cycle, then past the due day */
    EXPECT(req("cards.pay", "{\"account\":0,\"amount_minor\":0}"),
           PERR("\\\"amount_minor\\\" must be positive"));
    EXPECT(req("cards.pay", "{\"account\":0,\"amount_minor\":\"999999999\"}"),
           ERR("pay: the amount is zero or more than is owed"));
    EXPECT(req("cards.pay", "{\"account\":3,\"amount_minor\":1}"), ERR("account 3: not found"));
    /* the open approvals settle into this cycle */
    for (uint32_t i = 0; i < S.n_auths; i++)
        if (S.auths[i].state == CN_AUTH_APPROVED) {
            CHECK(cn_settle(&S, i) == CN_OK, "shadow settle %u", (unsigned) i);
            EXPECT(req("cards.capture", "{\"auth\":%u}", (unsigned) i), ok("%s", posted_json(i)));
        }
    CHECK(cn_pay(&S, 0, 1000, T0 + 100) == CN_OK, "shadow pay");
    EXPECT(req("cards.pay", "{\"account\":0,\"amount_minor\":1000,\"now\":%lld}", U(T0 + 100)),
           ok("%s", acct_json(0)));
    cn_time_t t1 = T0 + 31u * 86400u;
    CHECK(cn_advance(&S, t1) == CN_OK, "shadow advance");
    EXPECT(req("cards.advance", "{\"now\":%lld}", U(t1)),
           ok("{\"statements\":1,\"new_statements\":1,\"delinquent\":0}"));
    EXPECT(req("cards.account", "{\"account\":0}"), ok("%s", acct_json(0)));
    CHECK(S.accounts[0].billed > 0, "billed");
    cn_time_t t2 = T0 + 60u * 86400u;
    CHECK(cn_advance(&S, t2) == CN_OK, "shadow advance 2");
    EXPECT(req("cards.advance", "{\"now\":%lld}", U(t2)),
           ok("{\"statements\":1,\"new_statements\":0,\"delinquent\":1}"));
    EXPECT(req("cards.account", "{\"account\":0}"), ok("%s", acct_json(0)));
    EXPECT(req("cards.advance", "{\"now\":12}"),
           PERR("\\\"now\\\" must be unix seconds from 2000 to 2136"));
    /* a delinquent account is declined; paying the bill clears it */
    cn_auth_req_t r;
    uint32_t k = S.n_auths;
    for (uint32_t i = 0; i < CN_MAX_CARDS; i++)
        if (S.cards[i].used && S.cards[i].state == CN_CARD_ACTIVE && S.cards[i].form == 0) k = i;
    mkreq(&r, S.cards[k].pan, 100, 2, t2);
    AUTHORIZE(&r, SKB);
    CHECK(S.auths[S.n_auths - 1].decline == CN_DECL_DELINQUENT, "delinquent declined");
    uint64_t billed = S.accounts[0].billed;
    CHECK(cn_pay(&S, 0, billed, t2) == CN_OK, "shadow pay bill");
    EXPECT(req("cards.pay", "{\"account\":0,\"amount_minor\":%llu,\"now\":%lld}",
               (unsigned long long) billed, U(t2)),
           ok("%s", acct_json(0)));
    CHECK(S.accounts[0].state == CN_ACCT_ACTIVE, "active again");
    EXPECT(req("cards.status", "{}"), ok("%s", status_json()));
}

static void test_full_tables(void)
{
    /* every authorization is recorded, approved or not; the 33rd is refused */
    cn_auth_req_t r;
    memset(SIG, 0, sizeof SIG);
    mkreq(&r, "8800000000000000", 1, 1, T0);
    while (S.n_auths < CN_MAX_AUTHS) {
        uint32_t ai;
        CHECK(cn_authorize(&S, &r, SIG, &ai) == CN_OK, "shadow fill");
        rpc(req("cards.authorize", "{%s,\"sig\":\"%s\"}", fields(&r), hx(SIG, CN_SIG_BYTES)));
    }
    EXPECT(req("cards.authorize", "{%s,\"sig\":\"%s\"}", fields(&r), hx(SIG, CN_SIG_BYTES)),
           ERR("authorize: a fixed table is full (in-memory limits)"));
    EXPECT(req("cards.status", "{}"), ok("%s", status_json()));
    /* the edges of cardnet's clock: 2000-01-01 and 2^32-1 s later */
    uint32_t ea;
    CHECK(cn_open_account(&S, 0, &ea) == CN_OK, "shadow account at time 0");
    EXPECT(req("cards.open_account", "{\"now\":946684800}"), ok("%s", acct_json(ea)));
    CHECK(cn_open_account(&S, UINT32_MAX, &ea) == CN_OK, "shadow account at the last second");
    EXPECT(req("cards.open_account", "{\"now\":5241652095}"), ok("%s", acct_json(ea)));
    /* accounts: 8 */
    uint32_t a;
    while (cn_open_account(&S, T0, &a) == CN_OK)
        EXPECT(req("cards.open_account", "{\"now\":%lld}", U(T0)), ok("%s", acct_json(a)));
    EXPECT(req("cards.open_account", "{}"),
           ERR("open_account: a fixed table is full (in-memory limits)"));
    EXPECT(req("cards.account", "{\"account\":7}"), ok("%s", acct_json(7)));
}

static void test_off_keeps_state(void)
{
    EXPECT(req("features.set", "{\"feature\":\"cards\",\"enabled\":false}"),
           ok("{\"feature\":\"cards\",\"enabled\":false,\"regulated\":true,\"saved\":false}"));
    EXPECT(req("cards.pay", "{\"account\":0,\"amount_minor\":1}"),
           errc(-32012, "cards is off: start zxvd with --enable cards, or call features.set "
                        "(regulated: test or licensed use only)"));
    EXPECT(req("cards.status", "{}"), ok("%s", status_json()));
    CHECK(zxvd_rpc_enable("cards") == 0, "on again");
    EXPECT(req("cards.account", "{\"account\":0}"), ok("%s", acct_json(0)));
    /* close (zxvd_rpc_close / reset) forgets the issuer: in memory only */
    fresh();
    zxvd_rpc_enable("cards");
    EXPECT(req("cards.status", "{}"), ok("{\"issuer\":false,\"in_memory\":true}"));
    EXPECT(req("cards.issuer", "{\"network\":\"thunderbird\",\"seed\":\"%s\"}",
               "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"),
           ok("{\"issuer\":true,\"network\":\"Thunderbird\",\"prefix\":\"884\",\"issuer_code\":"
              "\"001\",\"fee_minor\":\"0\",\"accounts\":0,\"cards\":0,\"approved\":0,"
              "\"declined\":0,\"txns\":0,\"receipts\":0,\"statements\":0,\"ledger_balanced\":true,"
              "\"receipts_ok\":true,\"in_memory\":true}"));
    EXPECT(req("cards.status", "{}"),
           ok("{\"issuer\":true,\"network\":\"Thunderbird\",\"prefix\":\"884\",\"issuer_code\":"
              "\"001\",\"fee_minor\":\"0\",\"accounts\":0,\"cards\":0,\"approved\":0,"
              "\"declined\":0,\"txns\":0,\"receipts\":0,\"statements\":0,\"ledger_balanced\":true,"
              "\"receipts_ok\":true,\"in_memory\":true}"));
    /* a dragon issuer from the OS clock: open_account without "now" */
    fresh();
    zxvd_rpc_enable("cards");
    rpc(req("cards.issuer", "{\"network\":\"dragon\",\"fee_minor\":0}"));
    CHECK(strstr(rpc(req("cards.status", "{}")), "\"network\":\"Dragon\",\"prefix\":\"880\""),
          "dragon");
    EXPECT(req("cards.open_account", "{}"),
           ok("{\"account\":0,\"state\":\"active\",\"held_minor\":\"0\",\"unbilled_minor\":\"0\","
              "\"billed_minor\":\"0\",\"owed_minor\":\"0\",\"due\":null}"));
    zxvd_rpc_groups_reset();
}

int main(void)
{
    uint8_t s[32];
    for (int i = 0; i < 32; i++) s[i] = (uint8_t) (i * 7 + 1);
    pq_mldsa65_keygen(s, PKA, SKA);
    s[0] ^= 0xFF;
    pq_mldsa65_keygen(s, PKB, SKB);
    cn_civil_t c0 = {2026, 10, 9, 12, 0, 0};
    CHECK(cn_time_from_civil(&c0, &T0), "T0");

    test_switch();
    test_check_pan();
    test_issuer();
    test_accounts_and_cards();
    test_authorize_flow();
    test_iso8583();
    test_emv();
    test_freeze_reissue();
    test_statements();
    test_full_tables();
    test_off_keeps_state();
    free(OUT.p);
    if (fails) {
        printf("[FAIL] test_zxvd_cards: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("[PASS] test_zxvd_cards: %d checks\n", checks);
    return 0;
}
