/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zxvd_pay.c — zxvd_pay.h P1-P5 through zxvd's JSON-RPC lines, on the
 * real libzxv and ident. */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "zxvd_pay.h"
#include "zxvd_ext.h"
#include "zxvd_rpc.h"
#include "ident.h"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            fprintf(stderr, "  FAIL %s:%d ", __FILE__, __LINE__);                                  \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, "\n");                                                                 \
        }                                                                                          \
    } while (0)

static zxvd_buf_t OUT;

/* One request line; the reply stays in OUT (NUL-terminated). */
static const char *rpc(const char *method, const char *params)
{
    zxvd_buf_t line = {0};
    zxvd_buf_printf(&line, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\"", method);
    if (params) zxvd_buf_printf(&line, ",\"params\":%s", params);
    zxvd_buf_raw(&line, "}", 1);
    free(OUT.p);
    memset(&OUT, 0, sizeof OUT);
    zxvd_rpc_handle_line(line.p, line.len, &OUT);
    zxvd_buf_raw(&OUT, "", 1);
    free(line.p);
    return OUT.p;
}

static bool has(const char *method, const char *params, const char *want)
{
    return strstr(rpc(method, params), want) != NULL;
}

/* The whole reply is {"jsonrpc":"2.0","id":1,"result":<res>}. */
static bool ok_is(const char *method, const char *params, const char *res)
{
    char want[4096];
    snprintf(want, sizeof want, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":%s}", res);
    return !strcmp(rpc(method, params), want);
}

/* The reply is error `code` with exactly `msg` (no data). */
static bool err_is(const char *method, const char *params, int code, const char *msg)
{
    char want[1024];
    snprintf(want, sizeof want,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":%d,\"message\":\"%s\"}}", code,
             msg);
    return !strcmp(rpc(method, params), want);
}

/* A libzxv refusal with this status name (error -32000). */
static bool zerr(const char *method, const char *params, const char *name)
{
    char want[96];
    snprintf(want, sizeof want, "\"error\":{\"code\":-32000,\"message\":\"%s", name);
    return strstr(rpc(method, params), want) != NULL;
}

/* ===================== books closed ===================== */

static void test_closed(void)
{
    static const char *const M[] = {"iso20022.render", "ledger.hold", "ledger.release",
                                    "ledger.refund", "ledger.holds"};
    for (unsigned i = 0; i < sizeof M / sizeof M[0]; i++)
        CHECK(err_is(M[i], "{}", -32603, "the books are not open"), "%s with the books closed: %s",
              M[i], OUT.p);
}

/* ===================== P6 ===================== */

static void test_switches(void)
{
#define REG " (regulated: test or licensed use only)"
#define OFFMSG(f) f " is off: start zxvd with --enable " f ", or call features.set"
    static const char *const M[][2] = {
        {"netting.open", OFFMSG("netting") REG},  {"netting.list", OFFMSG("netting") REG},
        {"netting.drop", OFFMSG("netting") REG},  {"passkey.register", OFFMSG("passkeys")},
        {"passkey.verify", OFFMSG("passkeys")},   {"ledger.hold", OFFMSG("escrow") REG},
        {"ledger.release", OFFMSG("escrow") REG}, {"ledger.refund", OFFMSG("escrow") REG},
    };
    for (unsigned i = 0; i < sizeof M / sizeof M[0]; i++)
        CHECK(err_is(M[i][0], "{}", ZXVD_E_OFF, M[i][1]), "%s off: %s", M[i][0], OUT.p);
    /* compute-only methods and the holds read answer while everything is off */
    CHECK(has("card.check", "{\"network\":\"dragon\",\"pan\":\"79927398713\"}",
              "\"valid\":true"),
          "card.check needs no switch: %s", OUT.p);
    CHECK(err_is("ledger.holds", NULL, -32603, "the books are not open"),
          "ledger.holds is always served: %s", OUT.p);
    CHECK(zxvd_rpc_enable("netting") == 0 && has("netting.list", NULL, "{\"cycles\":[]}") &&
              err_is("ledger.hold", NULL, ZXVD_E_OFF, OFFMSG("escrow") REG),
          "one switch turns on one feature: %s", OUT.p);
    CHECK(zxvd_rpc_enable("escrow,passkeys") == 0 && zxvd_rpc_enabled("escrow") &&
              zxvd_rpc_enabled("passkeys") && zxvd_rpc_enabled("netting"),
          "every payment feature on");
    zxvd_pay_close();
    CHECK(err_is("netting.list", NULL, -32601, "method not found"), "closed: %s", OUT.p);
    CHECK(zxvd_pay_open() == 0 && zxvd_rpc_enabled("escrow") &&
              has("netting.list", NULL, "{\"cycles\":[]}"),
          "a reopen keeps the switches: %s", OUT.p);
#undef OFFMSG
#undef REG
}

/* ===================== P1 ===================== */

#define P8_FIELDS                                                                                  \
    "\"msg_id\":\"MSG-1\",\"cre_dt_tm\":\"2026-10-09T12:00:00Z\",\"debtor_name\":\"Ada & "         \
    "Sons\",\"debtor_acct\":\"NG0001\",\"creditor_name\":\"Kwame\",\"creditor_acct\":\"GH0002\","  \
    "\"end_to_end_id\":\"E2E-1\""

static void test_render(void)
{
    const char *r =
        rpc("iso20022.render", "{\"kind\":\"pacs008\",\"fields\":{" P8_FIELDS
                               ",\"ccy\":\"NGN\"},\"amounts\":{\"settlement\":250050}}");
    CHECK(strstr(r, "\"result\":{\"kind\":\"pacs008\",\"xml\":\"") &&
              strstr(r, "pacs.008.001.09") && strstr(r, "2500.50") && strstr(r, "Ada &amp; Sons") &&
              strstr(r, "NGN"),
          "a pacs.008 renders: %.300s", r);
    CHECK(has("iso20022.render",
              "{\"kind\":\"pacs008\",\"fields\":{" P8_FIELDS
              ",\"ccy\":\"BHD\"},\"amounts\":{\"settlement\":\"1234567\"},\"tx_count\":1}",
              "1234.567"),
          "minor units from ISO 4217 (BHD has 3), a decimal string amount");
    CHECK(zerr("iso20022.render",
               "{\"kind\":\"pacs008\",\"fields\":{" P8_FIELDS
               ",\"ccy\":\"VFV\"},\"amounts\":{\"settlement\":1}}",
               "ZXV_E_CURRENCY"),
          "VFV never goes on the wire: %s", OUT.p);
    CHECK(zerr("iso20022.render", "{\"kind\":\"pacs008\",\"fields\":{\"msg_id\":\"M\"}}",
               "ZXV_E_FIELD"),
          "mandatory fields are libzxv's to check: %s", OUT.p);
    CHECK(zerr("iso20022.render",
               "{\"kind\":\"pacs008\",\"fields\":{\"msg_id\":"
               "\"123456789012345678901234567890123456\"}}",
               "ZXV_E_TOO_LONG"),
          "a field over its maximum: %s", OUT.p);
    CHECK(zerr("iso20022.render",
               "{\"kind\":\"pacs008\",\"fields\":{" P8_FIELDS
               ",\"ccy\":\"NGN\"},\"amounts\":{\"settlement\":1},\"tx_count\":0}",
               "ZXV_E_"),
          "a zero transaction count: %s", OUT.p);
    CHECK(err_is("iso20022.render", "{\"kind\":\"pacs009\",\"fields\":{}}", -32602,
                 "\\\"kind\\\" must be \\\"pacs008\\\" or \\\"camt053\\\""),
          "kind: %s", OUT.p);
    CHECK(err_is("iso20022.render", "{\"fields\":{}}", -32602,
                 "missing string parameter \\\"kind\\\""),
          "no kind: %s", OUT.p);
    CHECK(err_is("iso20022.render", "{\"kind\":\"\"}", -32602, "\\\"kind\\\" must not be empty"),
          "empty kind: %s", OUT.p);
    CHECK(err_is("iso20022.render", "{\"kind\":\"pacs008\"}", -32602,
                 "missing object parameter \\\"fields\\\""),
          "no fields: %s", OUT.p);
    CHECK(err_is("iso20022.render", "{\"kind\":\"pacs008\",\"fields\":[]}", -32602,
                 "\\\"fields\\\" must be an object"),
          "fields not an object: %s", OUT.p);
    CHECK(err_is("iso20022.render", "{\"kind\":\"pacs008\",\"fields\":{},\"amounts\":[]}", -32602,
                 "\\\"amounts\\\" must be an object"),
          "amounts not an object: %s", OUT.p);
    CHECK(err_is("iso20022.render", "{\"kind\":\"camt053\",\"fields\":{},\"entries\":{}}", -32602,
                 "\\\"entries\\\" must be an array"),
          "entries not an array: %s", OUT.p);
    CHECK(err_is("iso20022.render", "{\"kind\":\"pacs008\",\"fields\":{\"bic\":\"X\"}}", -32602,
                 "unknown field \\\"bic\\\": fields are msg_id, cre_dt_tm, debtor_name, "
                 "debtor_acct, creditor_name, creditor_acct, end_to_end_id, ccy, acct_id"),
          "unknown field: %s", OUT.p);
    CHECK(err_is("iso20022.render", "{\"kind\":\"pacs008\",\"fields\":{\"msg_id\":5}}", -32602,
                 "\\\"fields.msg_id\\\" must be a string of at most 255 bytes with no NUL"),
          "a field that is not a string: %s", OUT.p);
    CHECK(err_is("iso20022.render", "{\"kind\":\"pacs008\",\"fields\":{\"msg_id\":\"a\\u0000b\"}}",
                 -32602, "\\\"fields.msg_id\\\" must be a string of at most 255 bytes with no NUL"),
          "a field with NUL: %s", OUT.p);
    CHECK(err_is(
              "iso20022.render",
              "{\"kind\":\"pacs008\",\"fields\":{\"msg_id\":\"M\"},\"amounts\":{\"settlement\":1}}",
              -32602, "amounts and entries need \\\"fields.ccy\\\""),
          "amounts need a currency: %s", OUT.p);
    CHECK(err_is("iso20022.render",
                 "{\"kind\":\"pacs008\",\"fields\":{\"ccy\":\"NGN\"},\"amounts\":{\"fee\":1}}",
                 -32602, "amounts are settlement, opening and closing"),
          "unknown amount: %s", OUT.p);
    CHECK(
        err_is(
            "iso20022.render",
            "{\"kind\":\"pacs008\",\"fields\":{\"ccy\":\"NGN\"},\"amounts\":{\"settlement\":1.5}}",
            -32602,
            "\\\"amounts.settlement\\\" must be an integer or a decimal string of minor units"),
        "a fraction is refused, never rounded: %s", OUT.p);
    CHECK(err_is("iso20022.render", "{\"kind\":\"pacs008\",\"fields\":{},\"tx_count\":-1}", -32602,
                 "\\\"tx_count\\\" is out of range"),
          "tx_count range: %s", OUT.p);

    /* camt.053: OPBD + booked entries must equal CLBD */
#define C53                                                                                        \
    "{\"kind\":\"camt053\",\"fields\":{\"msg_id\":\"STMT-1\",\"cre_dt_tm\":\"2026-10-09T23:59:"    \
    "59Z\",\"acct_id\":\"NG0001\",\"ccy\":\"NGN\"},\"amounts\":{\"opening\":0,\"closing\":%s},"    \
    "\"entries\":[%s]}"
    char p[1024];
    snprintf(p, sizeof p, C53, "1250025",
             "{\"amount_minor\":1500075,\"direction\":\"credit\"},{\"amount_minor\":\"250050\","
             "\"direction\":\"debit\",\"status\":\"booked\"},{\"amount_minor\":5,\"direction\":"
             "\"credit\",\"status\":\"pending\"},{\"amount_minor\":7,\"direction\":\"debit\","
             "\"status\":\"info\"}");
    r = rpc("iso20022.render", p);
    CHECK(strstr(r, "\"kind\":\"camt053\"") && strstr(r, "camt.053.001.08") &&
              strstr(r, "12500.25"),
          "a camt.053 renders: %.300s", r);
    snprintf(p, sizeof p, C53, "1250000",
             "{\"amount_minor\":1500075,\"direction\":\"credit\"},{\"amount_minor\":250050,"
             "\"direction\":\"debit\"}");
    CHECK(zerr("iso20022.render", p, "ZXV_E_FIELD"), "a statement must reconcile: %s", OUT.p);
    snprintf(p, sizeof p, C53, "0", "5");
    CHECK(err_is("iso20022.render", p, -32602, "each entry must be an object"), "entry: %s", OUT.p);
    snprintf(p, sizeof p, C53, "0", "{\"direction\":\"credit\"}");
    CHECK(err_is("iso20022.render", p, -32602,
                 "entry 0: \\\"amount_minor\\\" must be an integer or a decimal string"),
          "entry amount: %s", OUT.p);
    snprintf(p, sizeof p, C53, "0",
             "{\"amount_minor\":1,\"direction\":\"credit\"},{\"amount_minor\":1}");
    CHECK(err_is("iso20022.render", p, -32602,
                 "entry 1: \\\"direction\\\" must be \\\"credit\\\" or \\\"debit\\\""),
          "entry direction: %s", OUT.p);
    snprintf(p, sizeof p, C53, "0", "{\"amount_minor\":1,\"direction\":\"up\"}");
    CHECK(err_is("iso20022.render", p, -32602,
                 "entry 0: \\\"direction\\\" must be \\\"credit\\\" or \\\"debit\\\""),
          "entry direction value: %s", OUT.p);
    snprintf(p, sizeof p, C53, "0",
             "{\"amount_minor\":1,\"direction\":\"debit\",\"status\":\"x\"}");
    CHECK(err_is("iso20022.render", p, -32602,
                 "entry 0: \\\"status\\\" must be \\\"booked\\\", \\\"pending\\\" or \\\"info\\\""),
          "entry status: %s", OUT.p);
    snprintf(p, sizeof p, C53, "0", "{\"amount_minor\":-1,\"direction\":\"debit\"}");
    CHECK(zerr("iso20022.render", p, "ZXV_E_RANGE"), "an entry is a magnitude: %s", OUT.p);
    CHECK(
        zerr("iso20022.render",
             "{\"kind\":\"pacs008\",\"fields\":{\"ccy\":\"NGN\"},\"entries\":[{\"amount_minor\":1,"
             "\"direction\":\"credit\"}]}",
             "ZXV_E_"),
        "a pacs.008 has no statement entries: %s", OUT.p);
    CHECK(zerr("iso20022.render", "{\"kind\":\"camt053\",\"fields\":{\"debtor_name\":\"X\"}}",
               "ZXV_E_FIELD"),
          "a field the kind does not take: %s", OUT.p);
}

/* ===================== P2 ===================== */

static void test_card(void)
{
    CHECK(ok_is("card.check", "{\"network\":\"dragon\",\"pan\":\"79927398713\"}",
                "{\"network\":\"dragon\",\"valid\":true}"),
          "Luhn valid: %s", OUT.p);
    CHECK(ok_is("card.check", "{\"network\":\"dragon\",\"pan\":\"79927398710\"}",
                "{\"network\":\"dragon\",\"valid\":false}"),
          "Luhn invalid: %s", OUT.p);
    CHECK(ok_is("card.check", "{\"network\":\"phoenix\",\"pan\":\"5724\"}",
                "{\"network\":\"phoenix\",\"valid\":true}"),
          "Damm: %s", OUT.p);
    CHECK(ok_is("card.check", "{\"network\":\"phoenix\",\"pan\":\"5723\"}",
                "{\"network\":\"phoenix\",\"valid\":false}"),
          "Damm invalid: %s", OUT.p);
    CHECK(ok_is("card.check", "{\"network\":\"thunderbird\",\"pan\":\"2363\"}",
                "{\"network\":\"thunderbird\",\"valid\":true}"),
          "Verhoeff: %s", OUT.p);
    CHECK(err_is("card.check", "{\"network\":\"visa\",\"pan\":\"1\"}", -32602,
                 "\\\"network\\\" must be \\\"phoenix\\\", \\\"dragon\\\" or \\\"thunderbird\\\""),
          "network: %s", OUT.p);
    CHECK(
        err_is("card.check", "{\"pan\":\"1\"}", -32602, "missing string parameter \\\"network\\\""),
        "no network: %s", OUT.p);
    CHECK(err_is("card.check", "{\"network\":\"dragon\"}", -32602,
                 "missing string parameter \\\"pan\\\""),
          "no pan: %s", OUT.p);
    CHECK(zerr("card.check", "{\"network\":\"dragon\",\"pan\":\"7992a\"}",
               "ZXV_E_ARG: PAN must be decimal digits"),
          "digits only: %s", OUT.p);
    CHECK(zerr("card.check", "{\"network\":\"dragon\",\"pan\":\"7\"}",
               "ZXV_E_ARG: PAN must be 2..19 digits"),
          "length: %s", OUT.p);
}

/* ===================== P3 ===================== */

static void test_netting(void)
{
    CHECK(ok_is("netting.list", NULL, "{\"cycles\":[]}"), "no cycles: %s", OUT.p);
    CHECK(ok_is("netting.open", "{\"cycle\":\"C1\",\"currency\":\"NGN\"}",
                "{\"cycle\":\"C1\",\"currency\":\"NGN\",\"closed\":false,\"participants\":[]}"),
          "open: %s", OUT.p);
    CHECK(err_is("netting.open", "{\"cycle\":\"C1\",\"currency\":\"GHS\"}", -32020,
                 "a netting cycle \\\"C1\\\" is already held"),
          "one name once: %s", OUT.p);
    CHECK(zerr("netting.open", "{\"cycle\":\"C2\",\"currency\":\"VFV\"}", "ZXV_E_CURRENCY"),
          "VFV is no netting currency: %s", OUT.p);
    CHECK(err_is("netting.open", "{\"cycle\":\"C2\"}", -32602,
                 "missing string parameter \\\"currency\\\""),
          "no currency: %s", OUT.p);
    CHECK(err_is("netting.open", "{\"cycle\":\"\",\"currency\":\"NGN\"}", -32602,
                 "\\\"cycle\\\" must not be empty"),
          "empty name: %s", OUT.p);
    CHECK(has("netting.add", "{\"cycle\":\"C1\",\"participant\":\"BANK-A\"}",
              "\"participants\":[\"BANK-A\"]}"),
          "add A: %s", OUT.p);
    CHECK(has("netting.add", "{\"cycle\":\"C1\",\"participant\":\"BANK-B\"}",
              "\"participants\":[\"BANK-A\",\"BANK-B\"]}"),
          "add B");
    CHECK(has("netting.add", "{\"cycle\":\"C1\",\"participant\":\"BANK-C\"}", "\"BANK-C\"]}"),
          "add C");
    CHECK(zerr("netting.add", "{\"cycle\":\"C1\",\"participant\":\"BANK-A\"}", "ZXV_E_DUPLICATE"),
          "a participant once: %s", OUT.p);
    CHECK(err_is("netting.add", "{\"cycle\":\"C9\",\"participant\":\"X\"}", -32020,
                 "no netting cycle \\\"C9\\\" is held"),
          "unknown cycle: %s", OUT.p);
    CHECK(err_is("netting.positions", "{\"cycle\":\"C1\"}", -32020,
                 "netting cycle \\\"C1\\\" is still open: close it first"),
          "no positions while open: %s", OUT.p);
#define SUB(id, d, k, a)                                                                           \
    "{\"cycle\":\"C1\",\"payment_id\":\"" id "\",\"debtor\":\"" d "\",\"creditor\":\"" k           \
    "\",\"amount_minor\":" a "}"
    CHECK(ok_is("netting.submit", SUB("P1", "BANK-A", "BANK-B", "10000"),
                "{\"cycle\":\"C1\",\"payment_id\":\"P1\",\"accepted\":true}"),
          "submit: %s", OUT.p);
    CHECK(has("netting.submit", SUB("P2", "BANK-B", "BANK-C", "\"7000\""), "\"accepted\":true"),
          "an amount as a decimal string");
    CHECK(has("netting.submit", SUB("P3", "BANK-C", "BANK-A", "2500"), "\"accepted\":true"),
          "submit P3");
    CHECK(has("netting.submit", SUB("P1", "BANK-A", "BANK-B", "10000"), "\"accepted\":true"),
          "the same obligation again: idempotent");
    CHECK(zerr("netting.submit", SUB("P1", "BANK-A", "BANK-B", "9999"), "ZXV_E_DUPLICATE"),
          "same id, other amount: %s", OUT.p);
    CHECK(zerr("netting.submit", SUB("P4", "BANK-A", "BANK-Z", "1"), "ZXV_E_NOT_FOUND"),
          "unknown creditor: %s", OUT.p);
    CHECK(zerr("netting.submit", SUB("P4", "BANK-A", "BANK-A", "1"), "ZXV_E_ARG"), "to itself: %s",
          OUT.p);
    CHECK(zerr("netting.submit", SUB("P4", "BANK-A", "BANK-B", "0"), "ZXV_E_RANGE"), "zero: %s",
          OUT.p);
    CHECK(err_is("netting.submit", SUB("P4", "BANK-A", "BANK-B", "1.5"), -32602,
                 "\\\"amount_minor\\\" must be an integer or a decimal string (no fraction or "
                 "exponent)"),
          "a fraction: %s", OUT.p);
    CHECK(err_is("netting.submit", "{\"cycle\":\"C1\",\"payment_id\":\"P4\",\"debtor\":\"BANK-A\"}",
                 -32602, "missing string parameter \\\"creditor\\\""),
          "no creditor: %s", OUT.p);
    const char *closed =
        "{\"cycle\":\"C1\",\"currency\":\"NGN\",\"closed\":true,\"participants\":[\"BANK-A\","
        "\"BANK-B\",\"BANK-C\"],\"positions\":[{\"participant\":\"BANK-A\",\"net_minor\":\"-7500\"}"
        ","
        "{\"participant\":\"BANK-B\",\"net_minor\":\"3000\"},{\"participant\":\"BANK-C\","
        "\"net_minor\":\"4500\"}],\"sum_minor\":\"0\"}";
    CHECK(ok_is("netting.close", "{\"cycle\":\"C1\"}", closed), "close: %s", OUT.p);
    CHECK(ok_is("netting.positions", "{\"cycle\":\"C1\"}", closed), "positions: %s", OUT.p);
    CHECK(zerr("netting.close", "{\"cycle\":\"C1\"}", "ZXV_E_STATE"), "close once: %s", OUT.p);
    CHECK(zerr("netting.submit", SUB("P5", "BANK-A", "BANK-B", "1"), "ZXV_E_STATE"),
          "nothing after close: %s", OUT.p);
    CHECK(zerr("netting.add", "{\"cycle\":\"C1\",\"participant\":\"BANK-D\"}", "ZXV_E_STATE"),
          "no one after close: %s", OUT.p);
    char p[96];
    for (unsigned i = 2; i <= ZXVD_PAY_CYCLES; i++) {
        snprintf(p, sizeof p, "{\"cycle\":\"C%u\",\"currency\":\"XOF\"}", i);
        CHECK(has("netting.open", p, "\"closed\":false"), "open C%u", i);
    }
    CHECK(err_is("netting.open", "{\"cycle\":\"C99\",\"currency\":\"NGN\"}", -32020,
                 "at most 8 netting cycles are held: drop one first"),
          "the table is full: %s", OUT.p);
    CHECK(has("netting.list", NULL, "\"cycle\":\"C1\",\"currency\":\"NGN\",\"closed\":true") &&
              has("netting.list", NULL, "\"cycle\":\"C8\",\"currency\":\"XOF\",\"closed\":false"),
          "list: %s", OUT.p);
    CHECK(ok_is("netting.drop", "{\"cycle\":\"C1\"}", "{\"dropped\":\"C1\"}"), "drop: %s", OUT.p);
    CHECK(
        err_is("netting.drop", "{\"cycle\":\"C1\"}", -32020, "no netting cycle \\\"C1\\\" is held"),
        "dropped once: %s", OUT.p);
    CHECK(has("netting.open", "{\"cycle\":\"C99\",\"currency\":\"NGN\"}", "\"cycle\":\"C99\""),
          "a dropped slot is free again");
    CHECK(has("netting.add", "{\"cycle\":\"C99\",\"participant\":\"SOLO\"}", "SOLO") &&
              has("netting.close", "{\"cycle\":\"C99\"}",
                  "\"positions\":[{\"participant\":\"SOLO\",\"net_minor\":\"0\"}],\"sum_minor\":"
                  "\"0\""),
          "a cycle with no obligations: %s", OUT.p);
}

/* ===================== P4 ===================== */

static void det_random(void *ctx, uint8_t *out, uint32_t n)
{
    uint8_t *k = ctx;
    for (uint32_t i = 0; i < n; i++) out[i] = (uint8_t) (*k += 37u);
}

static void hex(zxvd_buf_t *b, const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) zxvd_buf_printf(b, "%02x", p[i]);
}

static id_passkey_t PKEY, PKEY2;

static void test_passkey(void)
{
    uint8_t seed = 1, dev[ID_DEV_ID] = {1}, user[16] = {2};
    id_host_t h = {&seed, det_random, NULL, NULL, NULL, NULL};
    static uint8_t reg[ID_AUTHDATA_MAX], auth[ID_AUTHDATA_MIN], sig[ID_MLDSA65_SIG];
    uint8_t cdh[ID_HASH];
    uint32_t rl = 0;
    CHECK(id_passkey_create(&PKEY, &h, "zxv.id", dev, user) == ID_OK &&
              id_passkey_register(&PKEY, true, reg, sizeof reg, &rl) == ID_OK &&
              rl == ID_AUTHDATA_MAX,
          "an ML-DSA-65 passkey");
    zxvd_buf_t p = {0}, cid = {0};
    hex(&cid, PKEY.cred_id, ID_CRED_ID);
    zxvd_buf_raw(&p, "{\"rp_id\":\"zxv.id\",\"authenticator_data\":\"", 40);
    hex(&p, reg, rl);
    zxvd_buf_raw(&p, "\"}", 2);
    char want[160];
    snprintf(want, sizeof want, "{\"credential_id\":\"%s\",\"sign_count\":0,\"registered\":1}",
             cid.p);
    CHECK(ok_is("passkey.register", p.p, want), "register: %s", OUT.p);
    CHECK(err_is("passkey.register", p.p, -32020, "that credential is already registered"),
          "once: %s", OUT.p);
    p.p[11] = 'y'; /* "zyv.id" */
    CHECK(err_is("passkey.register", p.p, -32020,
                 "the authenticator data is for another rp_id or lacks user verification"),
          "another relying party: %s", OUT.p);
    p.p[11] = 'x';
    free(p.p);
    memset(&p, 0, sizeof p);
    zxvd_buf_raw(&p, "{\"rp_id\":\"zxv.id\",\"authenticator_data\":\"", 40);
    hex(&p, reg, 10);
    zxvd_buf_raw(&p, "\"}", 2);
    CHECK(err_is("passkey.register", p.p, -32602,
                 "\\\"authenticator_data\\\" must be 2033 bytes as hex"),
          "length: %s", OUT.p);

    /* a passkey registered without user verification */
    CHECK(id_passkey_create(&PKEY2, &h, "zxv.id", dev, user) == ID_OK &&
              id_passkey_register(&PKEY2, false, reg, sizeof reg, &rl) == ID_OK,
          "a second passkey, no UV");
    free(p.p);
    memset(&p, 0, sizeof p);
    zxvd_buf_raw(&p, "{\"rp_id\":\"zxv.id\",\"authenticator_data\":\"", 40);
    hex(&p, reg, rl);
    zxvd_buf_raw(&p, "\"}", 2);
    CHECK(err_is("passkey.register", p.p, -32020,
                 "the authenticator data is for another rp_id or lacks user verification"),
          "UV required by default: %s", OUT.p);
    p.p[p.len - 2] = 0;
    zxvd_buf_t q = {0};
    zxvd_buf_printf(&q, "%s\",\"require_uv\":false}", p.p);
    CHECK(has("passkey.register", q.p, "\"registered\":2"), "UV not required: %s", OUT.p);
    reg[32] = 0; /* flags: not UP, not AT */
    free(q.p);
    memset(&q, 0, sizeof q);
    zxvd_buf_raw(&q, "{\"rp_id\":\"zxv.id\",\"require_uv\":false,\"authenticator_data\":\"", 59);
    hex(&q, reg, rl);
    zxvd_buf_raw(&q, "\"}", 2);
    CHECK(err_is("passkey.register", q.p, -32020,
                 "the authenticator data is not a device-bound ML-DSA-65 passkey registration"),
          "not a registration: %s", OUT.p);
    CHECK(err_is("passkey.register", "{\"rp_id\":\"zxv.id\",\"authenticator_data\":\"0g\"}", -32602,
                 "\\\"authenticator_data\\\" must be 2033 bytes as hex"),
          "not hex: %s", OUT.p);
    CHECK(err_is("passkey.register", "{\"authenticator_data\":\"00\"}", -32602,
                 "missing string parameter \\\"rp_id\\\""),
          "no rp_id: %s", OUT.p);

    /* assertions */
    uint8_t chal[32] = {7};
    CHECK(id_client_data_hash("webauthn.get", chal, sizeof chal, "https://zxv.id", cdh) == ID_OK &&
              id_passkey_assert(&PKEY, &h, true, cdh, auth, sig) == ID_OK,
          "an assertion");
    zxvd_buf_t v = {0};
    zxvd_buf_printf(&v, "{\"rp_id\":\"zxv.id\",\"credential_id\":\"%s\",\"authenticator_data\":\"",
                    cid.p);
    hex(&v, auth, sizeof auth);
    zxvd_buf_raw(&v, "\",\"client_data_hash\":\"", 22);
    hex(&v, cdh, sizeof cdh);
    zxvd_buf_raw(&v, "\",\"signature\":\"", 15);
    size_t sig_at = v.len;
    hex(&v, sig, sizeof sig);
    zxvd_buf_raw(&v, "\"}", 2);
    snprintf(want, sizeof want, "{\"credential_id\":\"%s\",\"verified\":true,\"sign_count\":1}",
             cid.p);
    CHECK(ok_is("passkey.verify", v.p, want), "verify: %s", OUT.p);
    CHECK(err_is("passkey.verify", v.p, -32020, "replayed: the signature counter did not increase"),
          "the same assertion again: %s", OUT.p);
    CHECK(id_passkey_assert(&PKEY, &h, true, cdh, auth, sig) == ID_OK, "a second assertion");
    free(v.p);
    memset(&v, 0, sizeof v);
    zxvd_buf_printf(&v, "{\"rp_id\":\"zxv.id\",\"credential_id\":\"%s\",\"authenticator_data\":\"",
                    cid.p);
    hex(&v, auth, sizeof auth);
    zxvd_buf_raw(&v, "\",\"client_data_hash\":\"", 22);
    hex(&v, cdh, sizeof cdh);
    zxvd_buf_raw(&v, "\",\"signature\":\"", 15);
    hex(&v, sig, sizeof sig);
    zxvd_buf_raw(&v, "\"}", 2);
    char keep = v.p[sig_at];
    v.p[sig_at] = keep == '0' ? '1' : '0';
    CHECK(err_is("passkey.verify", v.p, -32020,
                 "the assertion did not verify (signature, rp_id, user presence or user "
                 "verification)"),
          "a changed signature: %s", OUT.p);
    v.p[sig_at] = keep;
    v.p[11] = 'y';
    CHECK(err_is("passkey.verify", v.p, -32020,
                 "the assertion did not verify (signature, rp_id, user presence or user "
                 "verification)"),
          "another relying party: %s", OUT.p);
    v.p[11] = 'x';
    snprintf(want, sizeof want, "{\"credential_id\":\"%s\",\"verified\":true,\"sign_count\":2}",
             cid.p);
    CHECK(ok_is("passkey.verify", v.p, want), "the second assertion verifies: %s", OUT.p);
    char *c0 = strstr(v.p, cid.p);
    c0[0] = c0[0] == 'f' ? '0' : 'f';
    CHECK(
        err_is("passkey.verify", v.p, -32020, "no passkey with that \\\"credential_id\\\" is held"),
        "unknown credential: %s", OUT.p);
    CHECK(err_is("passkey.verify", "{\"rp_id\":\"zxv.id\",\"credential_id\":\"00\"}", -32602,
                 "\\\"credential_id\\\" must be 16 bytes as hex"),
          "credential id length: %s", OUT.p);

    /* an assertion without UV, for the key registered without it */
    CHECK(id_passkey_assert(&PKEY2, &h, false, cdh, auth, sig) == ID_OK, "assert without UV");
    free(v.p);
    memset(&v, 0, sizeof v);
    zxvd_buf_raw(&v, "{\"rp_id\":\"zxv.id\",\"credential_id\":\"", 35);
    hex(&v, PKEY2.cred_id, ID_CRED_ID);
    zxvd_buf_raw(&v, "\",\"authenticator_data\":\"", 24);
    hex(&v, auth, sizeof auth);
    zxvd_buf_raw(&v, "\",\"client_data_hash\":\"", 22);
    hex(&v, cdh, sizeof cdh);
    zxvd_buf_raw(&v, "\",\"signature\":\"", 15);
    hex(&v, sig, sizeof sig);
    zxvd_buf_raw(&v, "\"", 1);
    zxvd_buf_t w = {0};
    zxvd_buf_printf(&w, "%s}", v.p);
    CHECK(err_is("passkey.verify", w.p, -32020,
                 "the assertion did not verify (signature, rp_id, user presence or user "
                 "verification)"),
          "UV required by default: %s", OUT.p);
    free(w.p);
    memset(&w, 0, sizeof w);
    zxvd_buf_printf(&w, "%s,\"require_uv\":false}", v.p);
    CHECK(has("passkey.verify", w.p, "\"verified\":true,\"sign_count\":1}"), "UV not required: %s",
          OUT.p);
    free(w.p);
    free(v.p);
    free(p.p);
    free(q.p);
    free(cid.p);
    id_passkey_wipe(&PKEY);
    id_passkey_wipe(&PKEY2);
}

/* ===================== P5 ===================== */

static void escrow_setup(void)
{
    CHECK(has("currency.enable", "{\"code\":\"NGN\"}", "\"enabled\""), "NGN");
    CHECK(has("currency.enable", "{\"code\":\"GHS\"}", "\"enabled\""), "GHS");
    /* the earnings bridge opened none; the first accounts are 0, 1, 2, 3 */
    CHECK(ok_is("ledger.open_account",
                "{\"name\":\"issuer\",\"currency\":\"NGN\",\"kind\":\"issuer\"}",
                "{\"account\":0}"),
          "issuer: %s", OUT.p);
    CHECK(ok_is("ledger.open_account", "{\"name\":\"buyer\",\"currency\":\"NGN\"}",
                "{\"account\":1}"),
          "buyer");
    CHECK(ok_is("ledger.open_account", "{\"name\":\"seller\",\"currency\":\"NGN\"}",
                "{\"account\":2}"),
          "seller");
    CHECK(
        ok_is("ledger.open_account", "{\"name\":\"cedi\",\"currency\":\"GHS\"}", "{\"account\":3}"),
        "cedi");
    CHECK(has("ledger.post",
              "{\"from\":0,\"to\":1,\"amount_minor\":10000,\"currency\":\"NGN\",\"reference\":"
              "\"fund\"}",
              "entry_id"),
          "fund the buyer");
}

static void test_escrow(const char *books)
{
    escrow_setup();
    CHECK(ok_is("ledger.holds", NULL, "{\"holds\":[],\"escrow_accounts\":[],\"unmatched\":0}"),
          "no holds yet: %s", OUT.p);
    const char *h1 = "{\"hold\":1,\"state\":\"held\",\"from\":1,\"to\":2,\"amount_minor\":\"2500\","
                     "\"currency\":\"NGN\",\"escrow_account\":4,\"entry_id\":\"2\","
                     "\"closed_entry_id\":null,\"releases_on\":\"owner\"}";
    CHECK(ok_is("ledger.hold", "{\"from\":1,\"to\":2,\"amount_minor\":2500}", h1), "hold: %s",
          OUT.p);
    CHECK(has("ledger.accounts", NULL,
              "{\"account\":4,\"name\":\"escrow: NGN\",\"currency\":"
              "\"NGN\",\"kind\":\"holder\"}"),
          "the escrow account: %s", OUT.p);
    CHECK(has("ledger.balance", "{\"account\":1}", "\"equity_minor\":\"7500\"") &&
              has("ledger.balance", "{\"account\":4}", "\"equity_minor\":\"2500\""),
          "the funds moved to escrow");
    char want[1024];
    snprintf(want, sizeof want,
             "{\"holds\":[%s],\"escrow_accounts\":[{\"account\":4,\"currency\":\"NGN\","
             "\"balance_minor\":\"2500\",\"held_minor\":\"2500\",\"consistent\":true}],"
             "\"unmatched\":0}",
             h1);
    CHECK(ok_is("ledger.holds", NULL, want), "holds: %s", OUT.p);
    CHECK(ok_is("ledger.release", "{\"hold\":1}",
                "{\"hold\":1,\"state\":\"released\",\"from\":1,\"to\":2,\"amount_minor\":\"2500\","
                "\"currency\":\"NGN\",\"escrow_account\":4,\"entry_id\":\"2\","
                "\"closed_entry_id\":\"3\",\"releases_on\":\"owner\"}"),
          "release: %s", OUT.p);
    CHECK(has("ledger.balance", "{\"account\":2}", "\"equity_minor\":\"2500\"") &&
              has("ledger.balance", "{\"account\":4}", "\"equity_minor\":\"0\""),
          "the seller has it");
    CHECK(err_is("ledger.release", "{\"hold\":1}", -32020, "hold 1 is already released"),
          "release once: %s", OUT.p);
    CHECK(err_is("ledger.refund", "{\"hold\":1}", -32020, "hold 1 is already released"),
          "no refund after release: %s", OUT.p);
    CHECK(has("ledger.hold", "{\"from\":1,\"to\":2,\"amount_minor\":\"1000\"}",
              "{\"hold\":2,\"state\":\"held\""),
          "hold 2: %s", OUT.p);
    CHECK(has("ledger.refund", "{\"hold\":2}", "\"hold\":2,\"state\":\"refunded\""), "refund: %s",
          OUT.p);
    CHECK(has("ledger.balance", "{\"account\":1}", "\"equity_minor\":\"7500\""),
          "the buyer has it back");
    CHECK(err_is("ledger.refund", "{\"hold\":2}", -32020, "hold 2 is already refunded"),
          "refund once: %s", OUT.p);
    CHECK(err_is("ledger.release", "{\"hold\":9}", -32020, "no hold 9"), "unknown hold: %s", OUT.p);
    CHECK(err_is("ledger.release", "{}", -32602, "missing integer parameter \\\"hold\\\""),
          "no hold: %s", OUT.p);
    CHECK(err_is("ledger.hold", "{\"from\":1,\"to\":1,\"amount_minor\":1}", -32602,
                 "\\\"from\\\" and \\\"to\\\" must differ"),
          "to itself: %s", OUT.p);
    CHECK(err_is("ledger.hold", "{\"from\":1,\"to\":3,\"amount_minor\":1}", -32602,
                 "\\\"from\\\" holds NGN and \\\"to\\\" holds GHS: a hold is in one currency"),
          "one currency: %s", OUT.p);
    CHECK(err_is("ledger.hold", "{\"from\":4,\"to\":2,\"amount_minor\":1}", -32602,
                 "an escrow account is moved only by ledger.release and ledger.refund"),
          "from escrow: %s", OUT.p);
    CHECK(err_is("ledger.hold", "{\"from\":1,\"to\":4,\"amount_minor\":1}", -32602,
                 "an escrow account is moved only by ledger.release and ledger.refund"),
          "to escrow: %s", OUT.p);
    CHECK(zerr("ledger.hold", "{\"from\":1,\"to\":99,\"amount_minor\":1}", "ZXV_E_NOT_FOUND"),
          "unknown payee: %s", OUT.p);
    CHECK(zerr("ledger.hold", "{\"from\":99,\"to\":1,\"amount_minor\":1}", "ZXV_E_NOT_FOUND"),
          "unknown payer: %s", OUT.p);
    CHECK(zerr("ledger.hold", "{\"from\":1,\"to\":2,\"amount_minor\":7501}", "ZXV_E_FUNDS"),
          "no more than the payer holds: %s", OUT.p);
    CHECK(zerr("ledger.hold", "{\"from\":1,\"to\":2,\"amount_minor\":0}", "ZXV_E_RANGE"),
          "a positive amount: %s", OUT.p);
    CHECK(err_is("ledger.hold", "{\"from\":1,\"to\":2}", -32602,
                 "missing integer parameter \\\"amount_minor\\\""),
          "no amount: %s", OUT.p);
    CHECK(err_is("ledger.hold", "{\"from\":1,\"amount_minor\":1}", -32602,
                 "missing integer parameter \\\"to\\\""),
          "no payee: %s", OUT.p);
    /* a reference the next hold would use, taken by an ordinary posting */
    CHECK(
        has("ledger.post",
            "{\"from\":1,\"to\":2,\"amount_minor\":1,\"currency\":\"NGN\",\"reference\":\"escrow 3 "
            "to 2\"}",
            "entry_id"),
        "a posting that takes \"escrow 3 to 2\"");
    CHECK(has("ledger.hold", "{\"from\":1,\"to\":2,\"amount_minor\":500}", "{\"hold\":4,"),
          "the number is skipped: %s", OUT.p);
    /* a cedi hold opens its own escrow account */
    CHECK(
        has("ledger.open_account", "{\"name\":\"cedi 2\",\"currency\":\"GHS\"}", "{\"account\":5}"),
        "a second cedi account");
    CHECK(has("ledger.open_account",
              "{\"name\":\"cedi issuer\",\"currency\":\"GHS\",\"kind\":\"issuer\"}",
              "{\"account\":6}"),
          "a cedi issuer");
    CHECK(has("ledger.post",
              "{\"from\":6,\"to\":3,\"amount_minor\":900,\"currency\":\"GHS\",\"reference\":"
              "\"cedi\"}",
              "entry_id"),
          "fund the cedi account");
    CHECK(has("ledger.hold", "{\"from\":3,\"to\":5,\"amount_minor\":900}",
              "{\"hold\":5,\"state\":\"held\",\"from\":3,\"to\":5,\"amount_minor\":\"900\","
              "\"currency\":\"GHS\",\"escrow_account\":7,"),
          "a GHS hold: %s", OUT.p);
    /* a posting into escrow that is no hold is reported, not hidden */
    CHECK(
        has("ledger.post",
            "{\"from\":0,\"to\":4,\"amount_minor\":3,\"currency\":\"NGN\",\"reference\":\"stray\"}",
            "entry_id"),
        "a stray posting into escrow");
    CHECK(has("ledger.holds", NULL,
              "\"escrow_accounts\":[{\"account\":4,\"currency\":\"NGN\",\"balance_minor\":\"503\","
              "\"held_minor\":\"500\",\"consistent\":false},{\"account\":7,\"currency\":\"GHS\","
              "\"balance_minor\":\"900\",\"held_minor\":\"900\",\"consistent\":true}],"
              "\"unmatched\":1}"),
          "unmatched and inconsistent: %s", OUT.p);
    const char *before = strstr(rpc("ledger.holds", NULL), "\"result\"");
    char *copy = strdup(before);
    /* P5: the holds come back from the books file */
    if (books) {
        zxvd_pay_close();
        zxvd_rpc_books_close();
        CHECK(zxvd_rpc_books_file(books) == 0 && zxvd_rpc_books_open() == 0 && zxvd_pay_open() == 0,
              "reopen from the books file");
        CHECK(!strcmp(strstr(rpc("ledger.holds", NULL), "\"result\""), copy),
              "the same holds after a restart: %s", OUT.p);
        CHECK(has("ledger.release", "{\"hold\":4}", "\"state\":\"released\""),
              "a held hold is still held, and released after the restart: %s", OUT.p);
        CHECK(has("ledger.hold", "{\"from\":1,\"to\":2,\"amount_minor\":1}", "{\"hold\":6,"),
              "numbering carries on: %s", OUT.p);
    }
    free(copy);
}

/* Holds in a malformed escrow account are refused, not guessed. */
static void test_escrow_refs(void)
{
    escrow_setup();
    CHECK(has("ledger.open_account", "{\"name\":\"escrow: NGN\",\"currency\":\"NGN\"}",
              "{\"account\":4}"),
          "an escrow account opened by hand");
    static const char *const BAD[] = {"escrow 01 to 2",
                                      "escrow 1 to 02",
                                      "escrow 1 to 2x",
                                      "escrow 1 to ",
                                      "escrow x to 2",
                                      "escrow 1 release",
                                      "escrow 99999999999 to 2",
                                      "escrow 1 to 99999999999",
                                      "escrow 0 to 2",
                                      "escrow -1 to 2",
                                      "escrow 1"};
    char p[256];
    for (unsigned i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        snprintf(p, sizeof p,
                 "{\"from\":1,\"to\":4,\"amount_minor\":1,\"currency\":\"NGN\",\"reference\":"
                 "\"%s\"}",
                 BAD[i]);
        CHECK(has("ledger.post", p, "entry_id"), "post \"%s\": %s", BAD[i], OUT.p);
    }
    snprintf(p, sizeof p,
             "\"holds\":[],\"escrow_accounts\":[{\"account\":4,\"currency\":\"NGN\","
             "\"balance_minor\":\"%u\",\"held_minor\":\"0\",\"consistent\":false}],"
             "\"unmatched\":%u}",
             (unsigned) (sizeof BAD / sizeof BAD[0]), (unsigned) (sizeof BAD / sizeof BAD[0]));
    CHECK(has("ledger.holds", NULL, p), "none of them is a hold: %s", OUT.p);
    /* the account named for another currency is not an escrow account */
    CHECK(has("ledger.open_account", "{\"name\":\"escrow: NGN\",\"currency\":\"GHS\"}",
              "{\"account\":5}"),
          "a misnamed account");
    CHECK(has("ledger.holds", NULL, "\"escrow_accounts\":[{\"account\":4,"), "only account 4");
    /* the hand-made account is used: hold 100 is the first after the highest seen (99...) */
    CHECK(has("ledger.hold", "{\"from\":1,\"to\":2,\"amount_minor\":5}",
              "\"hold\":2,\"state\":\"held\",\"from\":1,\"to\":2,\"amount_minor\":\"5\","
              "\"currency\":\"NGN\",\"escrow_account\":4,"),
          "a hold numbered after the highest seen: %s", OUT.p);
    /* a release whose counterparty or amount does not match is not the hold's */
    CHECK(has("ledger.post",
              "{\"from\":4,\"to\":1,\"amount_minor\":5,\"currency\":\"NGN\",\"reference\":\"escrow "
              "2 release\"}",
              "entry_id"),
          "a release to the payer instead of the payee");
    CHECK(has("ledger.holds", NULL, "\"hold\":2,\"state\":\"held\"") &&
              has("ledger.holds", NULL, "\"unmatched\":12}"),
          "the hold is still held: %s", OUT.p);
    CHECK(zerr("ledger.release", "{\"hold\":2}", "ZXV_E_DUPLICATE"),
          "the reference is taken, so the release is refused: %s", OUT.p);
}

int main(void)
{
    char dir[] = "/tmp/zxvd-pay.XXXXXX";
    char books[64];
    if (!mkdtemp(dir)) return 2;
    snprintf(books, sizeof books, "%s/books", dir);

    CHECK(zxvd_pay_open() == 0, "open");
    test_switches();
    test_closed();
    test_card();
    test_netting(); /* the cycles need no books */
    CHECK(zxvd_rpc_books_file(NULL) == 0 && zxvd_rpc_books_open() == 0, "books in memory");
    test_render();
    test_passkey();
    test_escrow(NULL);
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file(NULL) == 0 && zxvd_rpc_books_open() == 0, "books again");
    CHECK(has("netting.list", NULL, "{\"cycles\":[{\"cycle\":\"C99\",\"currency\":\"NGN\""),
          "the cycles outlive a books reopen: %s", OUT.p);
    test_escrow_refs();
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file(books) == 0 && zxvd_rpc_books_open() == 0, "books in a file");
    test_escrow(books);
    zxvd_pay_close();
    CHECK(err_is("ledger.holds", NULL, -32601, "method not found"), "closed: %s", OUT.p);
    CHECK(zxvd_pay_open() == 0 && zxvd_pay_open() == 0 && has("ledger.holds", NULL, "\"holds\""),
          "opening twice holds one table");
    zxvd_pay_close();
    zxvd_rpc_books_close();
    unlink(books);
    rmdir(dir);
    free(OUT.p);
    if (fails) {
        printf("[FAIL] test_zxvd_pay: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("[PASS] test_zxvd_pay: %d checks\n", checks);
    return 0;
}
