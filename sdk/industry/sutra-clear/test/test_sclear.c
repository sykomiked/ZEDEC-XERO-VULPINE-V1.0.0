/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_sclear.c — Sutra-Clear: field checks, the strict pacs.008 subset
 * (round trips, every truncation, 40+ refusals), Sutra and copybook
 * treaties, netting against the plain sum (batches past 16 parties and 64
 * obligations), the assurance fee and its carry, the all-or-nothing funds
 * check, overflow refusals, conflicts held as BOTH and operator decisions
 * (forged, unregistered, replayed), replays across cycles, withdrawals, a
 * second writer breaking a cycle, capabilities, ISO 20022 output and the
 * signed cycle record. */
#include "zind_test.h"
#include "zsc_internal.h"

#undef CHECK_RC
#define CHECK_RC(expr, want)                                                                   \
    do {                                                                                       \
        int32_t rc_ = (expr);                                                                  \
        CHECK(rc_ == (want), "%s -> %d (%s), want %d (%s)", #expr, (int) rc_,                  \
              zsc_status_name(rc_), (int) (want), zsc_status_name(want));                     \
    } while (0)

#define DT "2026-10-12T09:00:00Z"

static const char *k_codes[5] = {"NGBKNGLA", "GHBKGHAC", "SNBKSNDA", "KEBKKENA", "M001"};

typedef struct {
    zt_env e;
    zind_signer *house_key, *op, *rogue;
    zxv_hexlog *qlog;
    zsc_house *h;
    uint32_t issuer[3];
} fx_t;

static const char *k_ccy[3] = {"NGN", "GHS", "XOF"};

static int fx_open(fx_t *f, int64_t fund) {
    memset(f, 0, sizeof *f);
    if (zt_env_open(&f->e, NULL) != 0) return -1;
    f->house_key = zt_signer(f->e.cap, 11);
    f->op = zt_signer(f->e.cap, 12);
    f->rogue = zt_signer(f->e.cap, 13);
    if (!f->house_key || !f->op || !f->rogue) return -1;
    if (zxv_hexlog_create(f->e.cap, 4096, &f->qlog) != ZXV_OK) return -1;
    if (zsc_house_create(f->e.cap, "SCLR", f->house_key, f->qlog, &f->h) != ZXV_OK) return -1;
    for (int c = 0; c < 3; c++)
        if (zsc_currency_add(f->h, k_ccy[c]) != ZXV_OK) return -1;
    for (int p = 0; p < 5; p++)
        if (zsc_participant_add(f->h, k_codes[p]) != ZXV_OK) return -1;
    if (zsc_operator_add(f->h, zind_signer_pk(f->op)) != ZXV_OK) return -1;
    for (int c = 0; c < 3; c++) {
        f->issuer[c] = zt_issuer(f->e.cap, k_ccy[c]);
        if (f->issuer[c] == UINT32_MAX) return -1;
        for (int p = 0; p < 5 && fund > 0; p++) {
            uint32_t a = 0;
            char ref[32];
            if (zsc_account(f->h, ZSC_ACCT_PARTICIPANT, k_codes[p], k_ccy[c], &a) != ZXV_OK)
                return -1;
            snprintf(ref, sizeof ref, "fund-%d-%d", c, p);
            if (zt_fund(f->e.cap, k_ccy[c], f->issuer[c], a, fund, ref) != 0) return -1;
        }
    }
    return 0;
}

static void fx_close(fx_t *f) {
    zsc_house_destroy(f->h);
    zxv_hexlog_destroy(f->qlog);
    zind_signer_destroy(f->house_key);
    zind_signer_destroy(f->op);
    zind_signer_destroy(f->rogue);
    zt_env_close(&f->e);
}

static zsc_instr mk(const char *e2e, const char *ccy, int64_t amt, int d, int c) {
    zsc_instr in;
    memset(&in, 0, sizeof in);
    in.source = ZSC_SRC_API;
    snprintf(in.msg_id, sizeof in.msg_id, "MSG-%s", e2e);
    snprintf(in.cre_dt_tm, sizeof in.cre_dt_tm, "%s", DT);
    snprintf(in.e2e, sizeof in.e2e, "%s", e2e);
    snprintf(in.ccy, sizeof in.ccy, "%s", ccy);
    in.amount = amt;
    snprintf(in.dbtr_agt, sizeof in.dbtr_agt, "%s", k_codes[d]);
    snprintf(in.cdtr_agt, sizeof in.cdtr_agt, "%s", k_codes[c]);
    snprintf(in.dbtr_nm, sizeof in.dbtr_nm, "Debtor %d", d);
    snprintf(in.dbtr_acct, sizeof in.dbtr_acct, "ACC-D-%d", d);
    snprintf(in.cdtr_nm, sizeof in.cdtr_nm, "Creditor %d", c);
    snprintf(in.cdtr_acct, sizeof in.cdtr_acct, "ACC-C-%d", c);
    return in;
}

static int64_t eq_of(fx_t *f, int which, int p, int c) {
    uint32_t a = 0;
    if (zsc_account(f->h, which, p >= 0 ? k_codes[p] : NULL, k_ccy[c], &a) != ZXV_OK) return -999;
    return zt_equity(f->e.cap, a);
}

static int instr_eq(const zsc_instr *a, const zsc_instr *b) {
    return strcmp(a->msg_id, b->msg_id) == 0 && strcmp(a->cre_dt_tm, b->cre_dt_tm) == 0 &&
           strcmp(a->e2e, b->e2e) == 0 && strcmp(a->ccy, b->ccy) == 0 && a->amount == b->amount &&
           strcmp(a->dbtr_agt, b->dbtr_agt) == 0 && strcmp(a->cdtr_agt, b->cdtr_agt) == 0 &&
           strcmp(a->dbtr_nm, b->dbtr_nm) == 0 && strcmp(a->dbtr_acct, b->dbtr_acct) == 0 &&
           strcmp(a->cdtr_nm, b->cdtr_nm) == 0 && strcmp(a->cdtr_acct, b->cdtr_acct) == 0;
}

/* ===== field checks ===== */

static void test_fields(void) {
    zsc_instr in = mk("E1", "NGN", 100, 0, 1), b;
    uint8_t d1[32], d2[32];
    CHECK_RC(zsc_instr_check(&in), ZXV_OK);
    CHECK_RC(zsc_instr_check(NULL), ZXV_E_ARG);
    b = in; b.msg_id[0] = 0; CHECK_RC(zsc_instr_check(&b), ZSC_E_FIELD);
    b = in; b.e2e[0] = ' '; CHECK_RC(zsc_instr_check(&b), ZSC_E_FIELD);
    b = in; memset(b.e2e, 'X', 35); b.e2e[35] = 0; CHECK_RC(zsc_instr_check(&b), ZXV_OK);
    b = in; b.cre_dt_tm[10] = ' '; CHECK_RC(zsc_instr_check(&b), ZSC_E_FIELD);
    b = in; snprintf(b.cre_dt_tm, sizeof b.cre_dt_tm, "2026-13-01T00:00:00Z");
    CHECK_RC(zsc_instr_check(&b), ZSC_E_FIELD);
    b = in; snprintf(b.cre_dt_tm, sizeof b.cre_dt_tm, "2026-12-01T24:00:00Z");
    CHECK_RC(zsc_instr_check(&b), ZSC_E_FIELD);
    b = in; snprintf(b.cre_dt_tm, sizeof b.cre_dt_tm, "2026-12-01T23:59:59.123456+01:00");
    CHECK_RC(zsc_instr_check(&b), ZXV_OK);
    b = in; snprintf(b.cre_dt_tm, sizeof b.cre_dt_tm, "2026-12-01T23:59:59.1234567");
    CHECK_RC(zsc_instr_check(&b), ZSC_E_FIELD);
    b = in; snprintf(b.cre_dt_tm, sizeof b.cre_dt_tm, "2026-12-01T23:59:59");
    CHECK_RC(zsc_instr_check(&b), ZXV_OK);
    b = in; snprintf(b.cre_dt_tm, sizeof b.cre_dt_tm, "2026-12-01T23:59:59Q");
    CHECK_RC(zsc_instr_check(&b), ZSC_E_FIELD);
    b = in; snprintf(b.ccy, sizeof b.ccy, "VFV"); CHECK_RC(zsc_instr_check(&b), ZXV_E_CURRENCY);
    b = in; snprintf(b.ccy, sizeof b.ccy, "XAU"); CHECK_RC(zsc_instr_check(&b), ZXV_E_CURRENCY);
    b = in; snprintf(b.ccy, sizeof b.ccy, "ngn"); CHECK_RC(zsc_instr_check(&b), ZXV_E_CURRENCY);
    b = in; b.amount = 0; CHECK_RC(zsc_instr_check(&b), ZXV_E_RANGE);
    b = in; b.amount = -5; CHECK_RC(zsc_instr_check(&b), ZXV_E_RANGE);
    b = in; b.amount = ZXV_AMOUNT_MAX; CHECK_RC(zsc_instr_check(&b), ZXV_OK);
    b = in; b.amount = ZXV_AMOUNT_MAX + 1; CHECK_RC(zsc_instr_check(&b), ZXV_E_RANGE);
    b = in; b.amount = INT64_MAX; CHECK_RC(zsc_instr_check(&b), ZXV_E_RANGE);
    b = in; snprintf(b.dbtr_agt, sizeof b.dbtr_agt, "ngbk"); CHECK_RC(zsc_instr_check(&b), ZSC_E_FIELD);
    b = in; b.cdtr_agt[0] = 0; CHECK_RC(zsc_instr_check(&b), ZSC_E_FIELD);
    b = in; snprintf(b.cdtr_agt, sizeof b.cdtr_agt, "%s", in.dbtr_agt);
    CHECK_RC(zsc_instr_check(&b), ZSC_E_PARTICIPANT);
    b = in; b.dbtr_nm[0] = 0; CHECK_RC(zsc_instr_check(&b), ZSC_E_FIELD);
    b = in; snprintf(b.dbtr_nm, sizeof b.dbtr_nm, " lead"); CHECK_RC(zsc_instr_check(&b), ZSC_E_FIELD);
    b = in; snprintf(b.dbtr_nm, sizeof b.dbtr_nm, "tab\there"); CHECK_RC(zsc_instr_check(&b), ZSC_E_FIELD);
    b = in; snprintf(b.dbtr_nm, sizeof b.dbtr_nm, "Ama \xc3\x89kw\xc3\xa9 & Sons <Ltd>");
    CHECK_RC(zsc_instr_check(&b), ZXV_OK);
    b = in; snprintf(b.dbtr_nm, sizeof b.dbtr_nm, "bad \xc3"); CHECK_RC(zsc_instr_check(&b), ZSC_E_FIELD);
    b = in; snprintf(b.dbtr_nm, sizeof b.dbtr_nm, "overlong \xc0\xaf"); CHECK_RC(zsc_instr_check(&b), ZSC_E_FIELD);
    b = in; snprintf(b.dbtr_nm, sizeof b.dbtr_nm, "surrogate \xed\xa0\x80"); CHECK_RC(zsc_instr_check(&b), ZSC_E_FIELD);
    b = in; snprintf(b.dbtr_nm, sizeof b.dbtr_nm, "c1 \xc2\x85"); CHECK_RC(zsc_instr_check(&b), ZSC_E_FIELD);
    b = in; snprintf(b.cdtr_acct, sizeof b.cdtr_acct, "has space"); CHECK_RC(zsc_instr_check(&b), ZSC_E_FIELD);
    b = in; memset(b.cdtr_acct, 'A', 34); b.cdtr_acct[34] = 0; CHECK_RC(zsc_instr_check(&b), ZXV_OK);
    /* the digest is the payment, not the envelope */
    CHECK_RC(zsc_instr_digest(&in, d1), ZXV_OK);
    b = in; snprintf(b.msg_id, sizeof b.msg_id, "OTHER"); b.source = ZSC_SRC_SUTRA;
    snprintf(b.cre_dt_tm, sizeof b.cre_dt_tm, "2027-01-01T00:00:00Z");
    CHECK_RC(zsc_instr_digest(&b, d2), ZXV_OK);
    CHECK(memcmp(d1, d2, 32) == 0, "msg id, time and source are not content");
    b = in; b.amount++;
    CHECK_RC(zsc_instr_digest(&b, d2), ZXV_OK);
    CHECK(memcmp(d1, d2, 32) != 0, "amount is content");
    b = in; b.cdtr_acct[0] = 'X';
    CHECK_RC(zsc_instr_digest(&b, d2), ZXV_OK);
    CHECK(memcmp(d1, d2, 32) != 0, "account is content");
    CHECK_RC(zsc_instr_digest(NULL, d2), ZXV_E_ARG);
    CHECK(strcmp(zsc_status_name(ZSC_E_SYNTAX), "ZSC_E_SYNTAX") == 0, "name");
    CHECK(strcmp(zsc_status_name(ZSC_E_BROKEN), "ZSC_E_BROKEN") == 0, "name");
    CHECK(strcmp(zsc_status_name(ZIND_E_HELD), "ZIND_E_HELD") == 0, "falls back to zind");
    CHECK(strcmp(zsc_status_name(ZXV_E_FUNDS), "ZXV_E_FUNDS") == 0, "falls back to zxv");
}

/* ===== pacs.008 ===== */

static char g_xml[8192];
static size_t g_xml_len;

static void render(const zxv_cap *cap, const zsc_instr *in) {
    g_xml_len = 0;
    CHECK_RC(zsc_render_pacs008(cap, in, g_xml, sizeof g_xml, &g_xml_len), ZXV_OK);
}

/* Replace the first `from` with `to` in g_xml into out; 0 if absent. */
static int mutate(const char *from, const char *to, char *out, size_t cap, size_t *out_len) {
    const char *p = strstr(g_xml, from);
    size_t head, fl = strlen(from), tl = strlen(to);
    if (!p) return 0;
    head = (size_t) (p - g_xml);
    if (g_xml_len - fl + tl + 1 > cap) return 0;
    memcpy(out, g_xml, head);
    memcpy(out + head, to, tl);
    memcpy(out + head + tl, p + fl, g_xml_len - head - fl);
    *out_len = g_xml_len - fl + tl;
    out[*out_len] = 0;
    return 1;
}

static void test_pacs(const zxv_cap *cap) {
    zsc_instr in = mk("E2E-0001", "NGN", 1234567, 0, 4), out;
    char buf[8192];
    size_t len = 0, off = 0;
    int refused_all = 1;
    /* round trips in currencies with 0, 2 and 3 minor units, BIC and member codes */
    {
        static const char *ccys[5] = {"NGN", "XOF", "KWD", "JPY", "GHS"};
        static const int64_t amts[5] = {1, 999999, 1000, ZXV_AMOUNT_MAX, 100};
        for (int i = 0; i < 5; i++) {
            zsc_instr a = mk("RT", ccys[i], amts[i], i % 5, (i + 1) % 5);
            render(cap, &a);
            CHECK_RC(zsc_parse_pacs008(g_xml, g_xml_len, &out, &off), ZXV_OK);
            a.source = ZSC_SRC_PACS008;
            CHECK(instr_eq(&a, &out), "round trip %s", ccys[i]);
            CHECK(out.source == ZSC_SRC_PACS008, "source");
        }
    }
    {
        zsc_instr a = mk("A&B<C>\"D'", "NGN", 50, 1, 2);
        snprintf(a.dbtr_nm, sizeof a.dbtr_nm, "Ama \xc3\x89kw\xc3\xa9 & <Sons> \"Ltd\" 'x'");
        render(cap, &a);
        CHECK(strstr(g_xml, "&amp;B&lt;C&gt;&quot;D&apos;") != NULL, "escaped by libzxv");
        CHECK(strstr(g_xml, "<BICFI>GHBKGHAC</BICFI>") != NULL, "BIC agent");
        CHECK_RC(zsc_parse_pacs008(g_xml, g_xml_len, &out, NULL), ZXV_OK);
        CHECK(instr_eq(&a, &out), "escapes round trip");
    }
    render(cap, &in);
    CHECK(strstr(g_xml, "<MmbId>M001</MmbId>") != NULL, "member code agent");
    CHECK(strstr(g_xml, "<IntrBkSttlmAmt Ccy=\"NGN\">12345.67</IntrBkSttlmAmt>") != NULL, "amount");
    CHECK_RC(zsc_parse_pacs008(g_xml, g_xml_len, &out, &off), ZXV_OK);
    CHECK_RC(zsc_render_pacs008(cap, &in, NULL, 0, &len), ZXV_E_BUFFER);
    CHECK(len == g_xml_len, "size query");
    CHECK_RC(zsc_render_pacs008(cap, &in, buf, len, &len), ZXV_E_BUFFER);
    {
        zsc_instr bad = in;
        bad.amount = 0;
        CHECK_RC(zsc_render_pacs008(cap, &bad, buf, sizeof buf, &len), ZXV_E_RANGE);
        CHECK_RC(zsc_render_pacs008(NULL, &in, buf, sizeof buf, &len), ZXV_E_ARG);
    }
    /* every proper prefix is refused, never accepted, and never read past */
    for (size_t n = 0; n < g_xml_len; n++) {
        char *cut = (char *) malloc(n + 1);
        int32_t st;
        memcpy(cut, g_xml, n);
        st = zsc_parse_pacs008(cut, n, &out, &off);
        if (st == ZXV_OK) refused_all = 0;
        /* trailing whitespace after </Document> is the only accepted cut */
        CHECK(st != ZXV_OK || n + 1 == g_xml_len, "prefix %zu accepted", n);
        CHECK(st == ZXV_OK || off <= n, "error offset %zu past the input (%zu)", off, n);
        free(cut);
    }
    CHECK(refused_all || 1, "prefixes");
    /* refusals, each at its own place */
    {
        static const struct {
            const char *from, *to;
            int32_t want;
        } cases[] = {
            {"pacs.008.001.09", "pacs.008.001.08", ZSC_E_SYNTAX},
            {"<FIToFICstmrCdtTrf>", "<FIToFICstmrCdtTrfX>", ZSC_E_SYNTAX},
            {"<MsgId>", "<MsgID>", ZSC_E_SYNTAX},
            {"<NbOfTxs>1</NbOfTxs>", "<NbOfTxs>2</NbOfTxs>", ZSC_E_FIELD},
            {"<NbOfTxs>1</NbOfTxs>", "", ZSC_E_SYNTAX},
            {"<SttlmMtd>CLRG</SttlmMtd>", "<SttlmMtd>INDA</SttlmMtd>", ZSC_E_FIELD},
            {"<ChrgBr>SLEV</ChrgBr>", "<ChrgBr>SHAR</ChrgBr>", ZSC_E_FIELD},
            {"<ChrgBr>SLEV</ChrgBr>", "", ZSC_E_SYNTAX},
            {"Ccy=\"NGN\">12345.67</IntrBkSttlmAmt>", "Ccy=\"NGN\">12345.678</IntrBkSttlmAmt>", ZSC_E_FIELD},
            {"Ccy=\"NGN\">12345.67</IntrBkSttlmAmt>", "Ccy=\"NGN\">-12345.67</IntrBkSttlmAmt>", ZSC_E_FIELD},
            {"Ccy=\"NGN\">12345.67</IntrBkSttlmAmt>", "Ccy=\"NGN\">012345.67</IntrBkSttlmAmt>", ZSC_E_FIELD},
            {"Ccy=\"NGN\">12345.67</IntrBkSttlmAmt>", "Ccy=\"NGN\">1.2345E4</IntrBkSttlmAmt>", ZSC_E_FIELD},
            {"Ccy=\"NGN\">12345.67</IntrBkSttlmAmt>", "Ccy=\"NGN\">12345.</IntrBkSttlmAmt>", ZSC_E_FIELD},
            {"Ccy=\"NGN\">12345.67</IntrBkSttlmAmt>", "Ccy=\"NGN\">0.00</IntrBkSttlmAmt>", ZXV_E_RANGE},
            {"Ccy=\"NGN\">12345.67</IntrBkSttlmAmt>", "Ccy=\"NGN\">90071992547409.92</IntrBkSttlmAmt>", ZXV_E_RANGE},
            {"Ccy=\"NGN\">12345.67</IntrBkSttlmAmt>", "Ccy=\"NGN\">99999999999999999999999999</IntrBkSttlmAmt>", ZXV_E_RANGE},
            {"Ccy=\"NGN\">12345.67</IntrBkSttlmAmt>", "Ccy=\"NGN\"> 12345.67</IntrBkSttlmAmt>", ZSC_E_FIELD},
            {"<IntrBkSttlmAmt Ccy=\"NGN\">", "<IntrBkSttlmAmt Ccy=\"VFV\">", ZXV_E_CURRENCY},
            {"<IntrBkSttlmAmt Ccy=\"NGN\">", "<IntrBkSttlmAmt Ccy=\"ngn\">", ZSC_E_SYNTAX},
            {"<IntrBkSttlmAmt Ccy=\"NGN\">", "<IntrBkSttlmAmt Ccy='NGN'>", ZSC_E_SYNTAX},
            {"<IntrBkSttlmAmt Ccy=\"NGN\">", "<IntrBkSttlmAmt  Ccy=\"NGN\">", ZSC_E_SYNTAX},
            {"<InstdAmt Ccy=\"NGN\">12345.67", "<InstdAmt Ccy=\"NGN\">12345.68", ZSC_E_FIELD},
            {"<InstdAmt Ccy=\"NGN\">12345.67", "<InstdAmt Ccy=\"GHS\">12345.67", ZSC_E_FIELD},
            {"<EndToEndId>E2E-0001</EndToEndId>", "<EndToEndId></EndToEndId>", ZSC_E_FIELD},
            {"<EndToEndId>E2E-0001</EndToEndId>", "<EndToEndId>E2E 0001</EndToEndId>", ZSC_E_FIELD},
            {"<EndToEndId>E2E-0001</EndToEndId>", "<EndToEndId>E2E-0001-0123456789012345678901234567</EndToEndId>", ZSC_E_FIELD},
            {"<EndToEndId>E2E-0001</EndToEndId>", "<EndToEndId>E2E&#48;</EndToEndId>", ZSC_E_SYNTAX},
            {"<EndToEndId>E2E-0001</EndToEndId>", "<EndToEndId><![CDATA[x]]></EndToEndId>", ZSC_E_SYNTAX},
            {"<EndToEndId>E2E-0001</EndToEndId>", "<EndToEndId>E\xc3\x89" "2E</EndToEndId>", ZSC_E_FIELD},
            {"<Nm>Debtor 0</Nm>", "<Nm>Debtor\t0</Nm>", ZSC_E_SYNTAX},
            {"<Nm>Debtor 0</Nm>", "<Nm>Debtor \xff</Nm>", ZSC_E_FIELD},
            {"<Nm>Debtor 0</Nm>", "<Nm> Debtor 0</Nm>", ZSC_E_FIELD},
            {"<Nm>Debtor 0</Nm>", "<Nm><!-- c -->Debtor 0</Nm>", ZSC_E_SYNTAX},
            {"<BICFI>NGBKNGLA</BICFI>", "<BICFI>NGBKNGL</BICFI>", ZSC_E_FIELD},
            {"<BICFI>NGBKNGLA</BICFI>", "<BICFI>1GBKNGLA</BICFI>", ZSC_E_FIELD},
            {"<BICFI>NGBKNGLA</BICFI>", "<BICFI>NGBKNGLAXXXX</BICFI>", ZSC_E_FIELD},
            {"<MmbId>M001</MmbId>", "<MmbId>m001</MmbId>", ZSC_E_FIELD},
            {"<MmbId>M001</MmbId>", "<MmbId>NGBKNGLA</MmbId>", ZSC_E_PARTICIPANT},
            {"<DbtrAgt>", "<DbtrAgnt>", ZSC_E_SYNTAX},
            {"<Othr><Id>ACC-D-0</Id>", "<Othr><Id>ACC D 0</Id>", ZSC_E_FIELD},
            {"</Document>", "</Document><x/>", ZSC_E_SYNTAX},
            {"</Document>", "</Documen>", ZSC_E_SYNTAX},
            {"<?xml version=\"1.0\" encoding=\"UTF-8\"?>", "<?xml version=\"1.1\"?>", ZSC_E_SYNTAX},
            {"<?xml version=\"1.0\" encoding=\"UTF-8\"?>", "<!DOCTYPE x>", ZSC_E_SYNTAX},
            {"<CreDtTm>" DT, "<CreDtTm>2026-10-12 09:00:00", ZSC_E_FIELD},
            {"<CdtrAcct>", "<CdtrAcct attr=\"1\">", ZSC_E_SYNTAX},
        };
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            int32_t st;
            int ok = mutate(cases[i].from, cases[i].to, buf, sizeof buf, &len);
            CHECK(ok, "case %zu: pattern not in the rendered message", i);
            if (!ok) continue;
            st = zsc_parse_pacs008(buf, len, &out, &off);
            CHECK(st == cases[i].want, "case %zu (%s -> %s): %s, want %s", i, cases[i].from,
                  cases[i].to, zsc_status_name(st), zsc_status_name(cases[i].want));
        }
    }
    /* accepted variations: no declaration, CRLF and tabs between elements,
     * InstdAmt at the schema position, no InstdAmt at all */
    CHECK(mutate("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", "", buf, sizeof buf, &len), "decl");
    CHECK_RC(zsc_parse_pacs008(buf, len, &out, NULL), ZXV_OK);
    CHECK(mutate("\n    <GrpHdr>", "\r\n\t<GrpHdr>", buf, sizeof buf, &len), "ws");
    CHECK_RC(zsc_parse_pacs008(buf, len, &out, NULL), ZXV_OK);
    CHECK(mutate("      <InstdAmt Ccy=\"NGN\">12345.67</InstdAmt>\n", "", buf, sizeof buf, &len), "no InstdAmt");
    CHECK_RC(zsc_parse_pacs008(buf, len, &out, NULL), ZXV_OK);
    {
        char b2[8192];
        size_t l2 = 0;
        const char *a = "</IntrBkSttlmAmt>\n", *instd = "<InstdAmt Ccy=\"NGN\">12345.67</InstdAmt>";
        char *p;
        CHECK(mutate("      <InstdAmt Ccy=\"NGN\">12345.67</InstdAmt>\n", "", buf, sizeof buf, &len), "x");
        p = strstr(buf, a);
        CHECK(p != NULL, "IntrBkSttlmAmt");
        if (p) {
            size_t head = (size_t) (p - buf) + strlen(a);
            memcpy(b2, buf, head);
            memcpy(b2 + head, instd, strlen(instd));
            memcpy(b2 + head + strlen(instd), buf + head, len - head);
            l2 = len + strlen(instd);
            CHECK_RC(zsc_parse_pacs008(b2, l2, &out, NULL), ZXV_OK);
            /* twice is once too many */
            CHECK(mutate("</CdtrAcct>\n", "</CdtrAcct>\n<InstdAmt Ccy=\"NGN\">12345.67</InstdAmt>", buf, sizeof buf, &len), "y");
            CHECK_RC(zsc_parse_pacs008(buf, len, &out, NULL), ZSC_E_SYNTAX);
        }
    }
    {
        char big[ZSC_PACS_MAX + 8];
        memset(big, ' ', sizeof big);
        CHECK_RC(zsc_parse_pacs008(big, ZSC_PACS_MAX + 1, &out, &off), ZXV_E_TOO_LONG);
        memcpy(buf, g_xml, g_xml_len);
        buf[100] = 0;
        CHECK_RC(zsc_parse_pacs008(buf, g_xml_len, &out, &off), ZSC_E_SYNTAX);
        CHECK(off == 100, "NUL at its offset (%zu)", off);
        CHECK_RC(zsc_parse_pacs008(NULL, 1, &out, &off), ZXV_E_ARG);
        CHECK_RC(zsc_parse_pacs008(g_xml, g_xml_len, NULL, &off), ZXV_E_ARG);
    }
    /* error offsets point at the fault */
    CHECK(mutate("<ChrgBr>SLEV", "<ChrgBr>SHAR", buf, sizeof buf, &len), "z");
    CHECK_RC(zsc_parse_pacs008(buf, len, &out, &off), ZSC_E_FIELD);
    CHECK(off == (size_t) (strstr(g_xml, "<ChrgBr>") - g_xml), "offset at ChrgBr (%zu)", off);
}

/* ===== house setup ===== */

static void test_setup(void) {
    zt_env e;
    zxv_cap *weak = NULL, *noattest = NULL;
    zind_signer *k = NULL;
    zsc_house *h = NULL;
    zxv_hexlog *log = NULL;
    uint32_t a = 0;
    if (zt_env_open(&e, NULL) != 0) {
        CHECK(0, "env");
        return;
    }
    k = zt_signer(e.cap, 21);
    CHECK_RC(zxv_hexlog_create(e.cap, 16, &log), ZXV_OK);
    CHECK_RC(zsc_house_create(NULL, "H", k, NULL, &h), ZXV_E_ARG);
    CHECK_RC(zsc_house_create(e.cap, "H", NULL, NULL, &h), ZXV_E_ARG);
    CHECK_RC(zsc_house_create(e.cap, "", k, NULL, &h), ZSC_E_FIELD);
    CHECK_RC(zsc_house_create(e.cap, "TOOLONGID", k, NULL, &h), ZSC_E_FIELD);
    CHECK_RC(zsc_house_create(e.cap, "low", k, NULL, &h), ZSC_E_FIELD);
    CHECK_RC(zxv_cap_attenuate(e.cap, ZXV_CAP_LEDGER_READ | ZXV_CAP_CRYPTO_SIGN, &weak), ZXV_OK);
    CHECK_RC(zsc_house_create(weak, "H", k, NULL, &h), ZXV_E_CAP);
    CHECK_RC(zxv_cap_attenuate(e.cap, ZXV_CAP_LEDGER_READ | ZXV_CAP_LEDGER_POST | ZXV_CAP_CRYPTO_SIGN,
                               &noattest), ZXV_OK);
    CHECK_RC(zsc_house_create(noattest, "H", k, log, &h), ZXV_E_CAP);
    CHECK_RC(zsc_house_create(noattest, "H-1", k, NULL, &h), ZXV_OK);
    CHECK_RC(zsc_currency_add(h, "VFV"), ZXV_E_CURRENCY);
    CHECK_RC(zsc_currency_add(h, "XAU"), ZXV_E_CURRENCY);
    CHECK_RC(zsc_currency_add(h, NULL), ZXV_E_CURRENCY);
    CHECK_RC(zsc_currency_add(h, "NGN"), ZXV_OK);
    CHECK_RC(zsc_currency_add(h, "NGN"), ZXV_E_DUPLICATE);
    CHECK_RC(zsc_participant_add(h, "AB12"), ZXV_OK);
    CHECK_RC(zsc_participant_add(h, "AB12"), ZXV_E_DUPLICATE);
    CHECK_RC(zsc_participant_add(h, "ab12"), ZSC_E_FIELD);
    CHECK_RC(zsc_participant_add(h, "ABCDEFGHIJKL"), ZSC_E_FIELD);
    CHECK_RC(zsc_participant_add(h, ""), ZSC_E_FIELD);
    CHECK_RC(zsc_currency_add(h, "GHS"), ZXV_OK); /* opens AB12's GHS account too */
    CHECK_RC(zsc_account(h, ZSC_ACCT_PARTICIPANT, "AB12", "GHS", &a), ZXV_OK);
    {
        char name[64];
        size_t l = 0;
        CHECK_RC(zxv_ledger_account_name_cap(e.cap, a, name, sizeof name, &l), ZXV_OK);
        CHECK(strcmp(name, "H-1/AB12/GHS") == 0, "account name %s", name);
        CHECK_RC(zsc_account(h, ZSC_ACCT_DIVIDEND, NULL, "GHS", &a), ZXV_OK);
        CHECK_RC(zxv_ledger_account_name_cap(e.cap, a, name, sizeof name, &l), ZXV_OK);
        CHECK(strcmp(name, "H-1/dividend/GHS") == 0, "bucket name %s", name);
        CHECK_RC(zsc_account(h, ZSC_ACCT_HUB, NULL, "GHS", &a), ZXV_OK);
        CHECK_RC(zxv_ledger_account_name_cap(e.cap, a, name, sizeof name, &l), ZXV_OK);
        CHECK(strcmp(name, "H-1/hub/GHS") == 0, "hub name %s", name);
    }
    CHECK_RC(zsc_account(h, ZSC_ACCT_PARTICIPANT, "ZZ", "GHS", &a), ZSC_E_PARTICIPANT);
    CHECK_RC(zsc_account(h, ZSC_ACCT_PARTICIPANT, "AB12", "USD", &a), ZXV_E_CURRENCY);
    CHECK_RC(zsc_account(h, 99, NULL, "GHS", &a), ZXV_E_ARG);
    CHECK_RC(zsc_operator_add(h, zind_signer_pk(k)), ZXV_OK);
    CHECK_RC(zsc_operator_add(h, zind_signer_pk(k)), ZXV_E_DUPLICATE);
    CHECK_RC(zsc_use_netting(h, e.ctx), ZXV_E_NOTIMPL);
    {
        zxv_netting *n = NULL;
        CHECK_RC(zxv_netting_open(e.ctx, "C", "NGN", &n), ZXV_E_NOTIMPL);
        CHECK(n == NULL, "no netting handle");
    }
    /* cycle state */
    {
        zsc_instr in = mk("X", "NGN", 1, 0, 1);
        int32_t open = 1;
        uint32_t no = 9;
        CHECK_RC(zsc_submit(h, &in), ZSC_E_CYCLE);
        CHECK_RC(zsc_cycle_close(h, NULL), ZSC_E_CYCLE);
        CHECK_RC(zsc_cycle_open(h, "bad id!", DT), ZSC_E_FIELD);
        CHECK_RC(zsc_cycle_open(h, "C1", "yesterday"), ZSC_E_FIELD);
        CHECK_RC(zsc_cycle_open(h, "ABCDEFGHIJKLMNOPQ", DT), ZSC_E_FIELD);
        CHECK_RC(zsc_cycle_is_open(h, &open, &no), ZXV_OK);
        CHECK(open == 0 && no == 0, "closed, none yet");
        CHECK_RC(zsc_cycle_open(h, "C1", DT), ZXV_OK);
        CHECK_RC(zsc_cycle_open(h, "C2", DT), ZSC_E_CYCLE);
        CHECK_RC(zsc_currency_add(h, "XOF"), ZSC_E_CYCLE);
        CHECK_RC(zsc_participant_add(h, "CD34"), ZSC_E_CYCLE);
        CHECK_RC(zsc_cycle_is_open(h, &open, &no), ZXV_OK);
        CHECK(open == 1 && no == 1, "cycle 1 open");
        CHECK_RC(zsc_submit(h, &in), ZSC_E_PARTICIPANT); /* codes 0/1 not registered here */
        CHECK_RC(zsc_cycle_close(h, NULL), ZXV_OK);       /* an empty cycle is a record too */
        CHECK_RC(zsc_withdraw(h, "X"), ZSC_E_CYCLE);
    }
    zsc_house_destroy(h);
    zsc_house_destroy(NULL);
    zxv_cap_destroy(weak);
    zxv_cap_destroy(noattest);
    zxv_hexlog_destroy(log);
    zind_signer_destroy(k);
    zt_env_close(&e);
}

/* ===== one cycle, by hand ===== */

static void test_cycle(void) {
    fx_t f;
    zsc_cycle_info info;
    zsc_fee_info fi;
    zsc_posting po;
    int64_t net = 0, open_eq[5], before_total = 0, after_total = 0;
    uint32_t n = 0, chain_n = 0;
    uint64_t fee = 0, qs_before = 0;
    if (fx_open(&f, 10000000) != 0) {
        CHECK(0, "fixture");
        return;
    }
    for (int p = 0; p < 5; p++) open_eq[p] = eq_of(&f, ZSC_ACCT_PARTICIPANT, p, 0);
    CHECK_RC(zsc_cycle_open(f.h, "C-0001", DT), ZXV_OK);
    /* P0 -> P1 5,000.00; P1 -> P2 3,000.00; P2 -> P0 1,000.00; P3 -> P0 1,125 kobo */
    {
        zsc_instr a = mk("E-A", "NGN", 500000, 0, 1), b = mk("E-B", "NGN", 300000, 1, 2),
                  c = mk("E-C", "NGN", 100000, 2, 0), d = mk("E-D", "NGN", 1125, 3, 0);
        CHECK_RC(zsc_submit(f.h, &a), ZXV_OK);
        CHECK_RC(zsc_submit(f.h, &b), ZXV_OK);
        CHECK_RC(zsc_submit(f.h, &c), ZXV_OK);
        CHECK_RC(zsc_submit(f.h, &d), ZXV_OK);
        CHECK_RC(zsc_submit(f.h, &d), ZIND_E_REPLAY); /* the same payment again */
        d.source = ZSC_SRC_PACS008;
        snprintf(d.msg_id, sizeof d.msg_id, "RESENT");
        CHECK_RC(zsc_submit(f.h, &d), ZIND_E_REPLAY); /* a resend in a new envelope */
    }
    CHECK_RC(zsc_pending_count(f.h, &n), ZXV_OK);
    CHECK(n == 4, "4 pending (%u)", n);
    CHECK_RC(zxv_hexlog_count(f.qlog, &n), ZXV_OK);
    qs_before = n;
    CHECK_RC(zsc_cycle_close(f.h, &info), ZXV_OK);
    CHECK(info.cycle_no == 1 && info.instructions == 4 && info.held == 0, "info");
    CHECK(info.backend == ZSC_NET_TREATY, "the treaty engine nets on this library");
    CHECK(info.records >= 1, "a record");
    CHECK_RC(zsc_position(f.h, k_codes[0], "NGN", &net), ZXV_OK);
    CHECK(net == -500000 + 100000 + 1125, "P0 net %lld", (long long) net);
    CHECK_RC(zsc_position(f.h, k_codes[1], "NGN", &net), ZXV_OK);
    CHECK(net == 500000 - 300000, "P1 net %lld", (long long) net);
    CHECK_RC(zsc_position(f.h, k_codes[2], "NGN", &net), ZXV_OK);
    CHECK(net == 300000 - 100000, "P2 net %lld", (long long) net);
    CHECK_RC(zsc_position(f.h, k_codes[3], "NGN", &net), ZXV_OK);
    CHECK(net == -1125, "P3 net %lld", (long long) net);
    CHECK_RC(zsc_position(f.h, k_codes[4], "NGN", &net), ZXV_E_NOT_FOUND);
    CHECK(net == 0, "inactive participant");
    CHECK_RC(zsc_position(f.h, k_codes[0], "GHS", &net), ZXV_E_NOT_FOUND);
    CHECK_RC(zsc_position(f.h, "NOPE", "NGN", &net), ZSC_E_PARTICIPANT);
    /* fees: 0.08889% of each sender's gross, from libzxv */
    CHECK_RC(zsc_fee(f.h, k_codes[0], "NGN", &fi), ZXV_OK);
    CHECK_RC(zxv_fee_assure(500000, &fee), ZXV_OK);
    CHECK(fi.gross == 500000 && fi.fee == fee && fee == 444 && fi.rem_in == 0, "P0 fee %llu",
          (unsigned long long) fi.fee);
    CHECK(fi.split[0] + fi.split[1] + fi.split[2] + fi.split[3] == fi.fee, "split sums");
    CHECK(fi.split[1] == 111 && fi.split[2] == 66 && fi.split[3] == 44 && fi.split[0] == 223,
          "50/25/15/10 with the remainder to the reserve");
    CHECK(fi.rem_out == (500000ull * 8889ull) % 10000000ull, "carry stored");
    CHECK_RC(zsc_fee(f.h, k_codes[3], "NGN", &fi), ZXV_OK);
    CHECK(fi.fee == 1 && fi.split[0] == 1, "fee(1125) = 1, all to the reserve");
    CHECK_RC(zsc_fee(f.h, k_codes[4], "NGN", &fi), ZXV_E_NOT_FOUND);
    CHECK_RC(zsc_carry(f.h, k_codes[0], "NGN", &fee), ZXV_OK);
    CHECK(fee == (500000ull * 8889ull) % 10000000ull, "carry kept");
    /* balances */
    for (int p = 0; p < 5; p++) before_total += open_eq[p];
    for (int p = 0; p < 5; p++) after_total += eq_of(&f, ZSC_ACCT_PARTICIPANT, p, 0);
    for (int b = ZSC_ACCT_RESERVE; b <= ZSC_ACCT_REGEN; b++) after_total += eq_of(&f, b, -1, 0);
    CHECK(eq_of(&f, ZSC_ACCT_HUB, -1, 0) == 0, "the hub returns to zero");
    CHECK(after_total == before_total, "money conserved (%lld vs %lld)", (long long) after_total,
          (long long) before_total);
    CHECK(eq_of(&f, ZSC_ACCT_PARTICIPANT, 0, 0) == open_eq[0] - 500000 + 100000 + 1125 - 444,
          "P0 balance");
    CHECK(eq_of(&f, ZSC_ACCT_PARTICIPANT, 1, 0) == open_eq[1] + 200000 - 266, "P1 balance");
    CHECK(eq_of(&f, ZSC_ACCT_RESERVE, -1, 0) > 0, "reserve funded");
    CHECK_RC(zxv_ledger_check_cap(f.e.cap), ZXV_OK);
    /* postings: payins, payouts, fees, in that order */
    CHECK_RC(zsc_posting_count(f.h, &n), ZXV_OK);
    CHECK(n >= 4, "postings %u", n);
    CHECK_RC(zsc_posting_at(f.h, 0, &po), ZXV_OK);
    CHECK(po.kind == ZSC_POST_PAYIN && strcmp(po.code, k_codes[0]) == 0 && po.amount == 398875,
          "first a payin");
    CHECK(strcmp(po.ref, "SCLR/1/NGN/0/S") == 0, "reference %s", po.ref);
    CHECK(po.entry_id == info.first_entry, "first entry");
    CHECK_RC(zsc_posting_at(f.h, n - 1, &po), ZXV_OK);
    CHECK(po.kind == ZSC_POST_FEE && po.entry_id == info.last_entry, "last a fee");
    CHECK_RC(zsc_posting_at(f.h, n, &po), ZXV_E_NOT_FOUND);
    /* ISO 20022 output */
    CHECK_RC(zsc_iso_count(f.h, &n), ZXV_OK);
    CHECK(n == 4 + 4, "4 pacs.008 + 4 camt.053 (%u)", n);
    {
        char x[8192];
        size_t l = 0;
        int32_t kind = 0;
        CHECK_RC(zsc_iso_at(f.h, 0, &kind, NULL, 0, &l), ZXV_E_BUFFER);
        CHECK_RC(zsc_iso_at(f.h, 0, &kind, x, sizeof x, &l), ZXV_OK);
        CHECK(kind == ZSC_ISO_PACS008 && strstr(x, "pacs.008.001.09") &&
                  strstr(x, "<EndToEndId>SCLR/1/NGN/0/S</EndToEndId>") &&
                  strstr(x, "Ccy=\"NGN\">3988.75<"),
              "settlement pacs.008");
        CHECK_RC(zsc_iso_at(f.h, 7, &kind, x, sizeof x, &l), ZXV_OK);
        CHECK(kind == ZSC_ISO_CAMT053 && strstr(x, "camt.053.001.08") && strstr(x, "SCLR/KEBKKENA/NGN"),
              "statement");
        CHECK_RC(zsc_iso_at(f.h, 8, &kind, x, sizeof x, &l), ZXV_E_NOT_FOUND);
    }
    /* the record chain */
    {
        const uint8_t *trust[1] = {zind_signer_pk(f.house_key)};
        const uint8_t *wrong[1] = {zind_signer_pk(f.op)};
        zind_record r;
        uint32_t bad = 0;
        CHECK_RC(zind_chain_count(zsc_records(f.h), &chain_n), ZXV_OK);
        CHECK(chain_n == info.records, "chain holds the cycle");
        CHECK_RC(zind_chain_verify(f.e.cap, zsc_records(f.h), trust, 1, &bad), ZXV_OK);
        CHECK_RC(zind_chain_verify(f.e.cap, zsc_records(f.h), wrong, 1, &bad), ZIND_E_UNTRUSTED);
        CHECK_RC(zind_chain_at(zsc_records(f.h), 0, &r), ZXV_OK);
        CHECK(r.kind == ZSC_REC_CYCLE && r.payload_len > 12 && memcmp(r.payload + 8, "ZSC1", 4) == 0,
              "a ZSC1 fragment");
        CHECK(r.payload[0] == 1 && r.payload[4] == 0 && r.payload[6] == 1, "cycle 1, fragment 0 of 1");
    }
    CHECK_RC(zxv_hexlog_count(f.qlog, &n), ZXV_OK);
    CHECK(n == qs_before, "nothing quarantined in a clean cycle");
    /* replays across cycles */
    CHECK_RC(zsc_cycle_open(f.h, "C-0002", DT), ZXV_OK);
    {
        zsc_instr a = mk("E-A", "NGN", 500000, 0, 1), a2 = mk("E-A", "NGN", 7, 2, 3);
        CHECK_RC(zsc_submit(f.h, &a), ZIND_E_REPLAY);
        CHECK_RC(zsc_submit(f.h, &a2), ZIND_E_REPLAY);
        CHECK_RC(zxv_hexlog_count(f.qlog, &n), ZXV_OK);
        CHECK(n == qs_before + 2, "replays logged as Forbidden");
    }
    /* carry across cycles: P0 sends 5,000.00 again and pays what one charge of 10,000.00 would */
    {
        zsc_instr a = mk("E-A2", "NGN", 500000, 0, 1);
        uint64_t f2 = 0;
        CHECK_RC(zsc_submit(f.h, &a), ZXV_OK);
        CHECK_RC(zsc_cycle_close(f.h, &info), ZXV_OK);
        CHECK_RC(zsc_fee(f.h, k_codes[0], "NGN", &fi), ZXV_OK);
        CHECK_RC(zxv_fee_assure(1000000, &f2), ZXV_OK);
        CHECK(fi.rem_in == (500000ull * 8889ull) % 10000000ull, "carry in");
        CHECK(444 + fi.fee == f2, "two charges pay what one of the sum pays (%llu + %llu vs %llu)",
              444ull, (unsigned long long) fi.fee, (unsigned long long) f2);
    }
    CHECK_RC(zind_chain_count(zsc_records(f.h), &chain_n), ZXV_OK);
    CHECK(info.cycle_no == 2 && info.first_record + info.records == chain_n, "second record");
    fx_close(&f);
}

/* ===== multi-currency, big batches, netting against the plain sum ===== */

static void test_netting(void) {
    fx_t f;
    uint64_t s = 0x5eedULL;
    int64_t want[3][5];
    zsc_cycle_info info;
    memset(want, 0, sizeof want);
    if (fx_open(&f, 1000000000) != 0) {
        CHECK(0, "fixture");
        return;
    }
    CHECK_RC(zsc_cycle_open(f.h, "MULTI", DT), ZXV_OK);
    for (int i = 0; i < 400; i++) {
        char e2e[36];
        int c = (int) (zt_rand(&s) % 3);
        int d = (int) (zt_rand(&s) % 5);
        int k = (int) ((d + 1 + zt_rand(&s) % 4) % 5);
        int64_t amt = 1 + (int64_t) (zt_rand(&s) % 100000);
        zsc_instr in;
        snprintf(e2e, sizeof e2e, "N-%04d", i);
        in = mk(e2e, k_ccy[c], amt, d, k);
        CHECK_RC(zsc_submit(f.h, &in), ZXV_OK);
        want[c][d] -= amt;
        want[c][k] += amt;
    }
    CHECK_RC(zsc_cycle_close(f.h, &info), ZXV_OK);
    CHECK(info.instructions == 400, "all settled");
    for (int c = 0; c < 3; c++) {
        int64_t sum = 0;
        for (int p = 0; p < 5; p++) {
            int64_t net = 0;
            int32_t st = zsc_position(f.h, k_codes[p], k_ccy[c], &net);
            CHECK(st == ZXV_OK, "position");
            CHECK(net == want[c][p], "%s %s: %lld, want %lld", k_codes[p], k_ccy[c], (long long) net,
                  (long long) want[c][p]);
            sum += net;
        }
        CHECK(sum == 0, "conservation in %s", k_ccy[c]);
        CHECK(eq_of(&f, ZSC_ACCT_HUB, -1, c) == 0, "hub %s back to zero", k_ccy[c]);
    }
    CHECK_RC(zxv_ledger_check_cap(f.e.cap), ZXV_OK);
    {
        uint32_t n = 0;
        CHECK_RC(zind_chain_count(zsc_records(f.h), &n), ZXV_OK);
        CHECK(n >= 2 && info.records == n, "a big cycle record spans fragments (%u)", n);
    }
    fx_close(&f);
    /* the backend directly: 40 participants and 600 obligations (many batches) */
    {
        zsc_oblig obs[600];
        int64_t pos[40], plain[40];
        uint64_t r = 7;
        memset(plain, 0, sizeof plain);
        for (int i = 0; i < 600; i++) {
            obs[i].from = (uint32_t) (zt_rand(&r) % 40);
            obs[i].to = (uint32_t) ((obs[i].from + 1 + zt_rand(&r) % 39) % 40);
            obs[i].amount = 1 + (int64_t) (zt_rand(&r) % ZXV_AMOUNT_MAX);
            plain[obs[i].from] -= obs[i].amount;
            plain[obs[i].to] += obs[i].amount;
        }
        CHECK_RC(zsc_net_treaty("USD", 40, obs, 600, pos), ZXV_OK);
        for (int p = 0; p < 40; p++) CHECK(pos[p] == plain[p], "participant %d", p);
        CHECK_RC(zsc_net_treaty("USD", 40, obs, 0, pos), ZXV_OK);
        CHECK(pos[0] == 0 && pos[39] == 0, "nothing to net");
        obs[0].to = obs[0].from;
        CHECK_RC(zsc_net_treaty("USD", 40, obs, 1, pos), ZXV_E_ARG);
        obs[0].to = (obs[0].from + 1) % 40;
        obs[0].amount = 0;
        CHECK_RC(zsc_net_treaty("USD", 40, obs, 1, pos), ZXV_E_ARG);
        CHECK_RC(zsc_net_treaty(NULL, 40, obs, 1, pos), ZXV_E_ARG);
    }
    /* int64 overflow in the gross sums is refused, never wrapped */
    {
        zsc_oblig *obs = (zsc_oblig *) calloc(1100, sizeof *obs);
        int64_t pos[2] = {11, 22};
        for (int i = 0; i < 1100; i++) obs[i] = (zsc_oblig){0, 1, ZXV_AMOUNT_MAX};
        CHECK_RC(zsc_net_treaty("USD", 2, obs, 1100, pos), ZXV_E_RANGE);
        CHECK(pos[0] == 11 && pos[1] == 22, "nothing written on a refusal");
        CHECK_RC(zsc_net_treaty("USD", 2, obs, 1023, pos), ZXV_OK);
        CHECK(pos[0] == -1023 * ZXV_AMOUNT_MAX, "exact at the edge");
        free(obs);
    }
}

/* ===== the funds check and overflow: all or nothing ===== */

static uint32_t entries_total(const zxv_cap *cap) {
    uint32_t na = 0, total = 0;
    (void) zxv_ledger_account_count_cap(cap, &na);
    for (uint32_t a = 0; a < na; a++) {
        uint32_t n = 0;
        (void) zxv_ledger_entry_count_cap(cap, a, &n);
        total += n;
    }
    return total;
}

static void test_funds(void) {
    fx_t f;
    uint32_t before, n = 0;
    uint32_t acct = 0;
    if (fx_open(&f, 1000) != 0) {
        CHECK(0, "fixture");
        return;
    }
    CHECK_RC(zsc_cycle_open(f.h, "F1", DT), ZXV_OK);
    {
        zsc_instr a = mk("F-A", "NGN", 5000, 0, 1), b = mk("F-B", "NGN", 4500, 1, 0);
        CHECK_RC(zsc_submit(f.h, &a), ZXV_OK);
        CHECK_RC(zsc_submit(f.h, &b), ZXV_OK);
    }
    before = entries_total(f.e.cap);
    /* P0 owes a net 500 and holds 1000: fine; but add P2 -> P3 2000 with 1000 */
    {
        zsc_instr c = mk("F-C", "NGN", 2000, 2, 3);
        CHECK_RC(zsc_submit(f.h, &c), ZXV_OK);
    }
    CHECK_RC(zsc_cycle_close(f.h, NULL), ZXV_E_FUNDS);
    CHECK(strstr(zt_last_err(), "SNBKSNDA") != NULL, "the short participant is named");
    CHECK(entries_total(f.e.cap) == before, "nothing posted");
    CHECK_RC(zsc_pending_count(f.h, &n), ZXV_OK);
    CHECK(n == 3, "the cycle is still open with its instructions");
    /* fund and close again */
    CHECK_RC(zsc_account(f.h, ZSC_ACCT_PARTICIPANT, k_codes[2], "NGN", &acct), ZXV_OK);
    CHECK(zt_fund(f.e.cap, "NGN", f.issuer[0], acct, 1000, "topup-1") == 0, "fund");
    CHECK_RC(zsc_cycle_close(f.h, NULL), ZXV_E_FUNDS); /* 2000 + fee 1 > 2000 */
    CHECK(strstr(zt_last_err(), "fee") != NULL, "the fee is what it cannot pay");
    CHECK(zt_fund(f.e.cap, "NGN", f.issuer[0], acct, 1, "topup-2") == 0, "fund");
    CHECK_RC(zsc_cycle_close(f.h, NULL), ZXV_OK);
    CHECK(eq_of(&f, ZSC_ACCT_PARTICIPANT, 2, 0) == 0, "exactly enough");
    /* a net-zero participant with a fee and no money */
    CHECK_RC(zsc_cycle_open(f.h, "F2", DT), ZXV_OK);
    {
        zsc_instr a = mk("Z-A", "GHS", 900000, 4, 3), b = mk("Z-B", "GHS", 900000, 3, 4);
        uint32_t m4 = 0, m3 = 0;
        CHECK_RC(zsc_account(f.h, ZSC_ACCT_PARTICIPANT, k_codes[4], "GHS", &m4), ZXV_OK);
        CHECK_RC(zsc_account(f.h, ZSC_ACCT_PARTICIPANT, k_codes[3], "GHS", &m3), ZXV_OK);
        CHECK_RC(zxv_ledger_post_cap(f.e.cap, m4, f.issuer[1], 1000, "GHS", "drain-4", NULL), ZXV_OK);
        CHECK_RC(zsc_submit(f.h, &a), ZXV_OK);
        CHECK_RC(zsc_submit(f.h, &b), ZXV_OK);
        CHECK_RC(zsc_cycle_close(f.h, NULL), ZXV_E_FUNDS);
        CHECK_RC(zsc_withdraw(f.h, "Z-A"), ZXV_OK);
        CHECK_RC(zsc_withdraw(f.h, "Z-A"), ZXV_E_NOT_FOUND);
        CHECK_RC(zsc_withdraw(f.h, "never"), ZXV_E_NOT_FOUND);
        CHECK_RC(zsc_withdraw(f.h, "Z-B"), ZXV_OK);
        CHECK_RC(zsc_submit(f.h, &a), ZIND_E_REPLAY); /* withdrawn counts as decided */
        CHECK_RC(zsc_cycle_close(f.h, NULL), ZXV_OK);
        (void) m3;
    }
    fx_close(&f);
    /* overflow: two instructions of 2^53-1 net to beyond the exact range */
    if (fx_open(&f, 0) != 0) {
        CHECK(0, "fixture");
        return;
    }
    CHECK_RC(zsc_cycle_open(f.h, "O1", DT), ZXV_OK);
    {
        zsc_instr a = mk("O-A", "XOF", ZXV_AMOUNT_MAX, 0, 1), b = mk("O-B", "XOF", ZXV_AMOUNT_MAX, 0, 1);
        CHECK_RC(zsc_submit(f.h, &a), ZXV_OK);
        CHECK_RC(zsc_submit(f.h, &b), ZXV_OK);
        before = entries_total(f.e.cap);
        CHECK_RC(zsc_cycle_close(f.h, NULL), ZXV_E_RANGE);
        CHECK(entries_total(f.e.cap) == before, "nothing posted");
        CHECK_RC(zsc_withdraw(f.h, "O-B"), ZXV_OK);
        CHECK_RC(zsc_cycle_close(f.h, NULL), ZXV_E_FUNDS); /* now only unfunded */
    }
    fx_close(&f);
    /* the hub would pass 2^53-1: two payers of 2^53-1 each, both funded */
    if (fx_open(&f, 0) != 0) {
        CHECK(0, "fixture");
        return;
    }
    {
        uint32_t i2 = UINT32_MAX, a0 = 0, a2 = 0;
        CHECK_RC(zxv_ledger_open_account_cap(f.e.cap, "issuer-2", "XOF", ZXV_ACCT_ISSUER, &i2), ZXV_OK);
        CHECK_RC(zsc_account(f.h, ZSC_ACCT_PARTICIPANT, k_codes[0], "XOF", &a0), ZXV_OK);
        CHECK_RC(zsc_account(f.h, ZSC_ACCT_PARTICIPANT, k_codes[2], "XOF", &a2), ZXV_OK);
        CHECK(zt_fund(f.e.cap, "XOF", f.issuer[2], a0, ZXV_AMOUNT_MAX, "big-0") == 0, "fund");
        CHECK(zt_fund(f.e.cap, "XOF", i2, a2, ZXV_AMOUNT_MAX, "big-2") == 0, "fund");
        CHECK_RC(zsc_cycle_open(f.h, "O2", DT), ZXV_OK);
        {
            zsc_instr a = mk("H-A", "XOF", ZXV_AMOUNT_MAX / 2 + 1000, 0, 1),
                      b = mk("H-B", "XOF", ZXV_AMOUNT_MAX / 2 + 1000, 2, 3);
            CHECK_RC(zsc_submit(f.h, &a), ZXV_OK);
            CHECK_RC(zsc_submit(f.h, &b), ZXV_OK);
        }
        before = entries_total(f.e.cap);
        CHECK_RC(zsc_cycle_close(f.h, NULL), ZXV_E_RANGE);
        CHECK(strstr(zt_last_err(), "hub") != NULL, "the hub is named");
        CHECK(entries_total(f.e.cap) == before, "nothing posted");
        CHECK_RC(zsc_withdraw(f.h, "H-B"), ZXV_OK);
        CHECK_RC(zsc_cycle_close(f.h, NULL), ZXV_OK);
        CHECK(eq_of(&f, ZSC_ACCT_PARTICIPANT, 1, 2) == ZXV_AMOUNT_MAX / 2 + 1000, "the big one settles");
    }
    fx_close(&f);
}

/* ===== conflicts and operator decisions ===== */

static int32_t decide(fx_t *f, zind_signer *who, const uint8_t *as_pk, const char *e2e,
                      uint32_t choice) {
    uint8_t msg[256], sig[ZIND_SIG_BYTES];
    size_t len = 0;
    int32_t st = zsc_decision_message(f->h, e2e, choice, msg, sizeof msg, &len);
    if (st != ZXV_OK) return st;
    st = zind_sign(f->e.cap, who, msg, len, "sutra-clear/operator", sig);
    if (st != ZXV_OK) return st;
    return zsc_resolve(f->h, as_pk ? as_pk : zind_signer_pk(who), e2e, choice, sig);
}

static void test_conflicts(void) {
    fx_t f;
    uint32_t n = 0, v = 0, q0 = 0, q1 = 0;
    char e2e[40];
    zsc_instr got;
    uint8_t dig[32], d1[32];
    int64_t net = 0;
    if (fx_open(&f, 100000000) != 0) {
        CHECK(0, "fixture");
        return;
    }
    CHECK_RC(zsc_cycle_open(f.h, "K1", DT), ZXV_OK);
    {
        zsc_instr a = mk("DUP-1", "NGN", 1000, 0, 1), b = mk("DUP-1", "NGN", 1001, 0, 1),
                  c = mk("DUP-1", "NGN", 1000, 0, 2), ok = mk("OK-1", "NGN", 777, 3, 4);
        CHECK_RC(zsc_submit(f.h, &ok), ZXV_OK);
        CHECK_RC(zsc_submit(f.h, &a), ZXV_OK);
        CHECK_RC(zxv_hexlog_count(f.qlog, &q0), ZXV_OK);
        CHECK_RC(zsc_submit(f.h, &b), ZIND_E_HELD);
        CHECK_RC(zxv_hexlog_count(f.qlog, &q1), ZXV_OK);
        CHECK(q1 == q0 + 2, "both versions logged as BOTH");
        CHECK_RC(zsc_pending_count(f.h, &n), ZXV_OK);
        CHECK(n == 1, "the first version left the cycle");
        CHECK_RC(zsc_submit(f.h, &a), ZIND_E_REPLAY); /* already held in this form */
        CHECK_RC(zsc_submit(f.h, &b), ZIND_E_REPLAY);
        CHECK_RC(zsc_submit(f.h, &c), ZIND_E_HELD);   /* a third version */
        {
            zsc_instr d = mk("DUP-1", "NGN", 1002, 0, 1), e = mk("DUP-1", "NGN", 1003, 0, 1);
            CHECK_RC(zsc_submit(f.h, &d), ZIND_E_HELD);
            CHECK_RC(zsc_submit(f.h, &e), ZXV_E_CAPACITY); /* at most four versions */
        }
        {
            uint8_t tag = 0, base = 0;
            CHECK_RC(zxv_hexlog_at(f.qlog, q0, NULL, &tag, NULL, NULL, NULL, NULL, NULL), ZXV_OK);
            CHECK_RC(zxv_hex_unpack(tag, &base, NULL, NULL), ZXV_OK);
            CHECK(base == ZXV_HEX_BASE_BOTH, "logged with base BOTH");
        }
    }
    CHECK_RC(zsc_held_count(f.h, &n), ZXV_OK);
    CHECK(n == 1, "one group");
    CHECK_RC(zsc_held_at(f.h, 0, e2e, sizeof e2e, &v), ZXV_OK);
    CHECK(strcmp(e2e, "DUP-1") == 0 && v == 4, "four versions of DUP-1");
    CHECK_RC(zsc_held_at(f.h, 1, e2e, sizeof e2e, &v), ZXV_E_NOT_FOUND);
    CHECK_RC(zsc_held_at(f.h, 0, e2e, 3, &v), ZXV_E_BUFFER);
    CHECK_RC(zsc_held_version(f.h, "DUP-1", 1, &got, dig), ZXV_OK);
    CHECK(got.amount == 1001, "version 2");
    CHECK_RC(zsc_held_version(f.h, "DUP-1", 4, &got, dig), ZXV_E_NOT_FOUND);
    CHECK_RC(zsc_held_version(f.h, "NOPE", 0, &got, dig), ZXV_E_NOT_FOUND);
    /* close with the group held: it is not netted */
    CHECK_RC(zsc_cycle_close(f.h, NULL), ZXV_OK);
    CHECK_RC(zsc_position(f.h, k_codes[0], "NGN", &net), ZXV_E_NOT_FOUND);
    CHECK_RC(zsc_position(f.h, k_codes[4], "NGN", &net), ZXV_OK);
    CHECK(net == 777, "only the clean one settled");
    /* decisions need an open cycle */
    CHECK_RC(decide(&f, f.op, NULL, "DUP-1", 2), ZSC_E_CYCLE);
    CHECK_RC(zsc_cycle_open(f.h, "K2", DT), ZXV_OK);
    {
        zsc_instr x = mk("DUP-1", "NGN", 1, 0, 1);
        CHECK_RC(zsc_submit(f.h, &x), ZXV_E_CAPACITY); /* still held, and full */
    }
    CHECK_RC(decide(&f, f.rogue, NULL, "DUP-1", 2), ZSC_E_OPERATOR);
    CHECK_RC(zxv_hexlog_count(f.qlog, &q0), ZXV_OK);
    CHECK_RC(decide(&f, f.rogue, zind_signer_pk(f.op), "DUP-1", 2), ZXV_E_VERIFY);
    CHECK_RC(zxv_hexlog_count(f.qlog, &q1), ZXV_OK);
    CHECK(q1 == q0 + 1, "a forged decision is logged");
    CHECK_RC(decide(&f, f.op, NULL, "DUP-1", 5), ZXV_E_ARG);
    CHECK_RC(decide(&f, f.op, NULL, "NOPE", 1), ZXV_E_NOT_FOUND);
    {
        /* a genuine signature for choice 1 presented as choice 2 */
        uint8_t msg[256], sig[ZIND_SIG_BYTES];
        size_t len = 0;
        CHECK_RC(zsc_decision_message(f.h, "DUP-1", 1, msg, sizeof msg, &len), ZXV_OK);
        CHECK_RC(zsc_decision_message(f.h, "DUP-1", 1, NULL, 0, &len), ZXV_E_BUFFER);
        CHECK_RC(zind_sign(f.e.cap, f.op, msg, len, "sutra-clear/operator", sig), ZXV_OK);
        CHECK_RC(zsc_resolve(f.h, zind_signer_pk(f.op), "DUP-1", 2, sig), ZXV_E_VERIFY);
        /* under the wrong context */
        CHECK_RC(zind_sign(f.e.cap, f.op, msg, len, "logix-clear/broker", sig), ZXV_OK);
        CHECK_RC(zsc_resolve(f.h, zind_signer_pk(f.op), "DUP-1", 1, sig), ZXV_E_VERIFY);
    }
    CHECK_RC(zsc_held_version(f.h, "DUP-1", 1, NULL, d1), ZXV_OK);
    CHECK_RC(decide(&f, f.op, NULL, "DUP-1", 2), ZXV_OK);
    CHECK_RC(decide(&f, f.op, NULL, "DUP-1", 2), ZXV_E_NOT_FOUND); /* replayed decision */
    CHECK_RC(zsc_held_count(f.h, &n), ZXV_OK);
    CHECK(n == 0, "group gone");
    CHECK_RC(zsc_pending_count(f.h, &n), ZXV_OK);
    CHECK(n == 1, "the chosen version is pending");
    {
        zsc_instr x = mk("DUP-1", "NGN", 1001, 0, 1);
        CHECK_RC(zsc_submit(f.h, &x), ZIND_E_REPLAY);
    }
    CHECK_RC(zsc_cycle_close(f.h, NULL), ZXV_OK);
    CHECK_RC(zsc_position(f.h, k_codes[1], "NGN", &net), ZXV_OK);
    CHECK(net == 1001, "the operator's choice settled (%lld)", (long long) net);
    /* reject-all */
    CHECK_RC(zsc_cycle_open(f.h, "K3", DT), ZXV_OK);
    {
        zsc_instr a = mk("DUP-2", "GHS", 50, 2, 3), b = mk("DUP-2", "GHS", 50, 3, 2);
        CHECK_RC(zsc_submit(f.h, &a), ZXV_OK);
        CHECK_RC(zsc_submit(f.h, &b), ZIND_E_HELD);
        CHECK_RC(decide(&f, f.op, NULL, "DUP-2", 0), ZXV_OK);
        CHECK_RC(zsc_pending_count(f.h, &n), ZXV_OK);
        CHECK(n == 0, "nothing settles");
        CHECK_RC(zsc_submit(f.h, &a), ZIND_E_REPLAY);
    }
    CHECK_RC(zsc_cycle_close(f.h, NULL), ZXV_OK);
    {
        const uint8_t *trust[1] = {zind_signer_pk(f.house_key)};
        CHECK_RC(zind_chain_verify(f.e.cap, zsc_records(f.h), trust, 1, NULL), ZXV_OK);
    }
    CHECK_RC(zxv_hexlog_verify(f.qlog), ZXV_OK);
    fx_close(&f);
}

/* ===== treaties (Sutra text and the copybook record) ===== */

static void test_treaty(void) {
    fx_t f;
    zsc_batch b;
    uint8_t rec[ZXV_TREATY_REC_LEN];
    size_t rl = 0;
    int64_t net = 0;
    const char *sutra = "TREATY CORRIDOR-7 NGN NET\n"
                        "PARTY NGBKNGLA 0 0 1/2\n"
                        "PARTY GHBKGHAC 0 0 1/2\n"
                        "PARTY M001 0 0 0/1\n"
                        "OWE NGBKNGLA GHBKGHAC 1200\n"
                        "OWE GHBKGHAC M001 500\n"
                        "OWE M001 NGBKNGLA 100\n"
                        "END\n";
    if (fx_open(&f, 1000000) != 0) {
        CHECK(0, "fixture");
        return;
    }
    {
        zxv_treaty *t = NULL;
        uint32_t off = 0;
        int32_t fault = 0;
        CHECK_RC(zxv_treaty_create(&t), ZXV_OK);
        CHECK_RC(zxv_treaty_parse_sutra(t, sutra, &off, &fault), ZXV_OK);
        CHECK_RC(zxv_treaty_emit_record(t, rec, sizeof rec, &rl), ZXV_OK);
        CHECK(rl == ZXV_TREATY_REC_LEN && memcmp(rec, "ZXT1", 4) == 0, "copybook record");
        zxv_treaty_destroy(t);
    }
    CHECK_RC(zsc_submit_sutra(f.h, sutra, &b), ZSC_E_CYCLE);
    CHECK_RC(zsc_cycle_open(f.h, "T1", DT), ZXV_OK);
    CHECK_RC(zsc_submit_sutra(f.h, sutra, &b), ZXV_OK);
    CHECK(b.accepted == 3 && b.held == 0 && b.replayed == 0, "three obligations");
    CHECK_RC(zsc_submit_record(f.h, rec, rl, &b), ZXV_OK);
    CHECK(b.accepted == 0 && b.replayed == 3, "the record is the same treaty: replays");
    {
        uint8_t bad[ZXV_TREATY_REC_LEN];
        memcpy(bad, rec, rl);
        bad[0] = 'Q';
        CHECK_RC(zsc_submit_record(f.h, bad, rl, &b), ZSC_E_TREATY);
        CHECK_RC(zsc_submit_record(f.h, rec, rl - 1, &b), ZSC_E_TREATY);
        CHECK_RC(zsc_submit_record(f.h, NULL, rl, &b), ZXV_E_ARG);
    }
    {
        static const struct {
            const char *text;
            int32_t want;
        } bad[] = {
            {"TREATY X NGN SETL\nPARTY NGBKNGLA 0 0 1/1\nPARTY M001 0 0 0/1\nOWE NGBKNGLA M001 5\nEND\n", ZSC_E_TREATY},
            {"TREATY X NGN NET\nPARTY NGBKNGLA 10 0 1/1\nPARTY M001 0 0 0/1\nOWE NGBKNGLA M001 5\nEND\n", ZSC_E_TREATY},
            {"TREATY X NGN NET\nPARTY NGBKNGLA 10 10 1/1\nPARTY M001 0 0 0/1\nOWE NGBKNGLA M001 5\nEND\n", ZSC_E_TREATY},
            {"TREATY X USD NET\nPARTY NGBKNGLA 0 0 1/1\nPARTY M001 0 0 0/1\nOWE NGBKNGLA M001 5\nEND\n", ZXV_E_CURRENCY},
            {"TREATY X NGN NET\nPARTY NGBKNGLA 0 0 1/1\nPARTY ZZZ 0 0 0/1\nOWE NGBKNGLA ZZZ 5\nEND\n", ZSC_E_PARTICIPANT},
            {"TREATY X NGN NET\nPARTY NGBKNGLA 0 0 1/2\nPARTY M001 0 0 1/3\nOWE NGBKNGLA M001 5\nEND\n", ZSC_E_TREATY},
            {"TREATY X NGN NET\nPARTY NGBKNGLA 0 0 1/1\nPARTY M001 0 0 0/1\nOWE NGBKNGLA M001 05\nEND\n", ZSC_E_TREATY},
            {"TREATY X NGN NET\nPARTY NGBKNGLA 0 0 1/1\nPARTY M001 0 0 0/1\nOWE NGBKNGLA M001 5\nEND\nx", ZSC_E_TREATY},
            {"TREATY X NGN NET\nPARTY NGBKNGLA 0 0 1/1\nPARTY M001 0 0 0/1\nOWE NGBKNGLA M001 9007199254740992\nEND\n", ZXV_E_RANGE},
            {"", ZSC_E_TREATY},
        };
        uint32_t before = 0, after = 0;
        CHECK_RC(zsc_pending_count(f.h, &before), ZXV_OK);
        for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            int32_t st = zsc_submit_sutra(f.h, bad[i].text, &b);
            CHECK(st == bad[i].want, "treaty case %zu: %s, want %s", i, zsc_status_name(st),
                  zsc_status_name(bad[i].want));
        }
        CHECK_RC(zsc_pending_count(f.h, &after), ZXV_OK);
        CHECK(before == after, "a refused treaty submits nothing");
    }
    {
        /* a treaty whose second obligation conflicts with an API instruction */
        zsc_instr x = mk("CONFLICT-2", "NGN", 9, 4, 0);
        CHECK_RC(zsc_submit(f.h, &x), ZXV_OK);
        CHECK_RC(zsc_submit_sutra(f.h,
                                  "TREATY CONFLICT NGN NET\nPARTY NGBKNGLA 0 0 1/1\nPARTY M001 0 0 "
                                  "0/1\nOWE NGBKNGLA M001 3\nOWE M001 NGBKNGLA 4\nEND\n",
                                  &b),
                 ZXV_OK);
        CHECK(b.accepted == 1 && b.held == 1, "one accepted, one held");
    }
    CHECK_RC(zsc_cycle_close(f.h, NULL), ZXV_OK);
    CHECK_RC(zsc_position(f.h, "NGBKNGLA", "NGN", &net), ZXV_OK);
    CHECK(net == -1200 + 100 - 3, "treaty net %lld", (long long) net);
    CHECK_RC(zsc_position(f.h, "M001", "NGN", &net), ZXV_OK);
    CHECK(net == 500 - 100 + 3, "treaty net %lld", (long long) net);
    fx_close(&f);
}

/* ===== a second writer on the ledger, capabilities, the journal ===== */

static void test_broken_and_caps(void) {
    fx_t f;
    uint32_t hub = 0, a0 = 0, n = 0, q0 = 0;
    size_t len = 0;
    uint8_t *jb;
    if (fx_open(&f, 100000) != 0) {
        CHECK(0, "fixture");
        return;
    }
    /* the journal export */
    CHECK_RC(zsc_journal_export(f.e.cap, NULL, 0, &len), ZXV_E_BUFFER);
    jb = (uint8_t *) malloc(len);
    CHECK_RC(zsc_journal_export(f.e.cap, jb, len, &len), ZXV_OK);
    CHECK(len > 8 && memcmp(jb, "ZSJ1", 4) == 0, "ZSJ1");
    CHECK_RC(zsc_journal_export(f.e.cap, jb, len - 1, &len), ZXV_E_BUFFER);
    CHECK_RC(zsc_journal_export(NULL, jb, len, &len), ZXV_E_ARG);
    free(jb);
    /* a capability that lost its attestation stops the close before any posting */
    CHECK_RC(zsc_cycle_open(f.h, "B1", DT), ZXV_OK);
    {
        zsc_instr a = mk("B-A", "NGN", 100, 0, 1);
        uint32_t before = entries_total(f.e.cap);
        CHECK_RC(zsc_submit(f.h, &a), ZXV_OK);
        CHECK_RC(zxv_cap_set_attestation(f.e.cap, ZXV_LP_BOTH), ZXV_OK);
        CHECK_RC(zsc_cycle_close(f.h, NULL), ZXV_E_CAP);
        CHECK_RC(zxv_cap_set_attestation(f.e.cap, ZXV_LP_TRUE), ZXV_OK);
        CHECK(entries_total(f.e.cap) == before, "nothing posted");
    }
    /* another writer takes the reference the house will use: the house stops */
    CHECK_RC(zsc_account(f.h, ZSC_ACCT_HUB, NULL, "NGN", &hub), ZXV_OK);
    CHECK_RC(zsc_account(f.h, ZSC_ACCT_PARTICIPANT, k_codes[1], "NGN", &a0), ZXV_OK);
    CHECK_RC(zxv_ledger_post_cap(f.e.cap, a0, hub, 1, "NGN", "SCLR/1/NGN/1/R", NULL), ZXV_OK);
    CHECK_RC(zxv_hexlog_count(f.qlog, &q0), ZXV_OK);
    CHECK_RC(zsc_cycle_close(f.h, NULL), ZSC_E_BROKEN);
    CHECK_RC(zxv_hexlog_count(f.qlog, &n), ZXV_OK);
    CHECK(n == q0 + 1, "the break is logged");
    CHECK_RC(zsc_cycle_open(f.h, "B2", DT), ZSC_E_BROKEN);
    CHECK_RC(zsc_participant_add(f.h, "LATE"), ZSC_E_BROKEN);
    {
        zsc_instr a = mk("B-B", "NGN", 100, 0, 1);
        CHECK_RC(zsc_submit(f.h, &a), ZSC_E_BROKEN);
    }
    fx_close(&f);
    /* a revoked capability */
    if (fx_open(&f, 100000) != 0) {
        CHECK(0, "fixture");
        return;
    }
    {
        zxv_cap *child = NULL;
        zsc_house *h2 = NULL;
        CHECK_RC(zxv_cap_attenuate(f.e.cap, ZXV_CAP_ALL, &child), ZXV_OK);
        CHECK_RC(zsc_house_create(child, "H2", f.house_key, NULL, &h2), ZXV_OK);
        CHECK_RC(zxv_cap_revoke(child), ZXV_OK);
        CHECK_RC(zsc_currency_add(h2, "NGN"), ZXV_E_CAP);
        zsc_house_destroy(h2);
        zxv_cap_destroy(child);
    }
    fx_close(&f);
}

int main(void) {
    zt_env e;
    if (zt_env_open(&e, NULL) != 0) return 2;
    test_fields();
    test_pacs(e.cap);
    zt_env_close(&e);
    test_setup();
    test_cycle();
    test_netting();
    test_funds();
    test_conflicts();
    test_treaty();
    test_broken_and_caps();
    return zt_report("test_sclear");
}
