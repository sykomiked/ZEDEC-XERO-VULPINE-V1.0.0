/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_validate.c — records from each adapter checked against
 * examples/market.zc, evidence records, host attestation, libzxv tags and
 * actions, and the quarantine sink (kit API 0.2).
 *
 *   test_validate examples/market.zc */
#include "za_test.h"

static zc_contract *C;
static uint8_t C_DIGEST[32];

static unsigned char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    long n;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    b = (unsigned char *) malloc((size_t) n + 1u);
    if (b && fread(b, 1, (size_t) n, f) != (size_t) n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    if (b) b[n] = 0;
    *len = (size_t) n;
    return b;
}

/* Ingest, validate against fn with `log`, return the validate status. */
static int32_t run(int32_t kind, const char *source, const char *payload, const char *fn,
                   zc_evlog *log, za_record **out) {
    za_record *r = ingest(kind, source, payload);
    int32_t st;
    if (!r) return -9999;
    CHECK(lp_of(r) == ZXV_LP_NEITHER, "%s: the payload itself is accepted", fn);
    st = za_validate(r, C, fn, log);
    if (out) *out = r;
    else za_record_destroy(r);
    return st;
}

static void expect_pass(int32_t kind, const char *source, const char *payload, const char *fn,
                        const char *what) {
    za_record *r = NULL;
    int32_t st = run(kind, source, payload, fn, NULL, &r);
    CHECK(st == ZXV_OK, "%s: validates (got %s)", what, za_status_name(st));
    CHECK(r && lp_of(r) == ZXV_LP_NEITHER && strcmp(tag_text(r), "neither/neutral/emerging") == 0,
          "%s: passing a contract leaves the record unverified, %s", what, r ? tag_text(r) : "-");
    za_record_destroy(r);
}

static void expect_fail(int32_t kind, const char *source, const char *payload, const char *fn,
                        int32_t za_reason, int32_t zc_reason, const char *needle, const char *what) {
    zc_evlog *log = NULL;
    za_record *r = NULL;
    uint64_t count = 0;
    uint32_t origin = 9, index = 0, nf = 0;
    int32_t part = 0, reason = -1, tag = -1, st;
    uint8_t cd[32], sd[32], rd[32];
    char text[300];
    size_t tl = 0;
    const char *msg = "";
    CHECK(zc_evlog_create(8, &log) == ZXV_OK, "log");
    st = run(kind, source, payload, fn, log, &r);
    CHECK(st == ZA_E_REFUSED, "%s: refused (got %s)", what, za_status_name(st));
    CHECK(r && lp_of(r) == ZXV_LP_FALSE && strcmp(tag_text(r), "false/neutral/emerging") == 0,
          "%s: labelled refused", what);
    CHECK(r && reason0(r, &msg) == za_reason && strstr(msg, needle) != NULL, "%s: reason %s '%s'", what,
          za_reason_name(reason0(r, NULL)), msg);
    CHECK(r && za_record_field_count(r, &nf) == ZXV_OK && nf > 0,
          "%s: a record refused by validation keeps its fields for inspection", what);
    CHECK(zc_evlog_count(log, &count) == ZXV_OK && count == 1, "%s: one evidence record", what);
    CHECK(zc_evlog_get(log, 1, &origin, &part, &index, &reason, &tag, cd, sd, NULL, text, sizeof text,
                       &tl) == ZXV_OK, "%s: evidence readable", what);
    CHECK(origin == ZC_ORIGIN_ADAPTER && part == ZC_PART_PARAMS && reason == zc_reason,
          "%s: evidence origin %u part %d reason %s", what, (unsigned) origin, (int) part,
          zc_reason_name(reason));
    CHECK(r && za_record_digest(r, rd) == ZXV_OK && memcmp(rd, sd, 32) == 0 &&
              memcmp(cd, C_DIGEST, 32) == 0, "%s: evidence cites the contract and the record", what);
    CHECK(strstr(text, needle) != NULL, "%s: evidence text '%s'", what, text);
    CHECK(zc_evlog_verify(log) == ZXV_OK, "%s: chain verifies", what);
    CHECK(r && za_validate(r, C, fn, log) == ZXV_E_STATE, "%s: a refused record is not re-validated", what);
    za_record_destroy(r);
    zc_evlog_destroy(log);
}

static void test_payment(void) {
    expect_pass(ZA_SRC_JSON, "https://bank.example/api",
                "{\"amount\": {\"value\": 1250.50, \"currency\": \"EUR\"}, \"reference\": \"INV-2026-0042\","
                " \"urgent\": false, \"note\": \"fields the contract does not name are allowed\"}",
                "payment", "payment");
    expect_pass(ZA_SRC_JSON, "", "{\"amount\": {\"value\": \"0.10\", \"currency\": \"USD\"}, \"reference\": \"R\","
                                 " \"urgent\": \"true\"}", "payment", "amount as decimal text, urgent as text");
    expect_fail(ZA_SRC_JSON, "", "{\"amount\": {\"value\": 12.345, \"currency\": \"EUR\"}, \"reference\": \"R\","
                                 " \"urgent\": true}", "payment", ZA_R_CONTRACT, ZC_V_DENOMINATOR, "den",
                "a third decimal place");
    expect_fail(ZA_SRC_JSON, "", "{\"amount\": {\"value\": -1, \"currency\": \"EUR\"}, \"reference\": \"R\","
                                 " \"urgent\": true}", "payment", ZA_R_CONTRACT, ZC_V_BELOW, "below",
                "a negative amount");
    expect_fail(ZA_SRC_JSON, "", "{\"amount\": {\"value\": 5, \"currency\": \"EURO\"}, \"reference\": \"R\","
                                 " \"urgent\": true}", "payment", ZA_R_CONTRACT, ZC_V_LENGTH, "outside bytes[3..3]",
                "a four-letter currency");
    expect_fail(ZA_SRC_JSON, "", "{\"amount\": {\"value\": 5, \"currency\": \"EUR\"}, \"urgent\": true}", "payment",
                ZA_R_MISSING, ZC_V_ARITY, "'reference' has no field", "no reference");
    expect_fail(ZA_SRC_JSON, "", "{\"amount\": {\"value\": 5, \"currency\": \"EUR\"}, \"reference\": \"R\","
                                 " \"urgent\": \"maybe\"}", "payment", ZA_R_TYPE, ZC_V_KIND, "cannot be truth",
                "urgent is a word");
    expect_fail(ZA_SRC_JSON, "", "{\"amount\": {\"value\": \"five\", \"currency\": \"EUR\"}, \"reference\": \"R\","
                                 " \"urgent\": true}", "payment", ZA_R_TYPE, ZC_V_KIND, "cannot be rat",
                "amount is a word");
    expect_fail(ZA_SRC_JSON, "", "{\"amount\": {\"value\": null, \"currency\": \"EUR\"}, \"reference\": \"R\","
                                 " \"urgent\": true}", "payment", ZA_R_TYPE, ZC_V_KIND, "null", "amount is null");
    expect_fail(ZA_SRC_JSON, "", "{\"amount\": {\"value\": 5, \"currency\": \"EUR\"}, \"reference\": \"R\","
                                 " \"urgent\": \"neither\"}", "payment", ZA_R_CONTRACT, ZC_V_NOT_IN_SET, "",
                "urgent outside {true, false}");
}

static void test_new_order(void) {
    static const char good[] = "35=D|49=BUYSIDE|56=EXCHANGE|34=2|52=20261011-10:00:00.000|11=ORD-0001|"
                               "55=ACME|54=1|38=100|40=2|44=12.3450|60=20261011-10:00:00.000|";
    char m[1024];
    snprintf(m, sizeof m, "%s", fix_msg("FIX.4.4", good, '|'));
    expect_pass(ZA_SRC_FIX_PIPE, "", m, "new_order", "FIX new order");
    snprintf(m, sizeof m, "%s", fix_msg("FIX.4.4", good, 1));
    expect_pass(ZA_SRC_FIX, "", m, "new_order", "FIX new order (SOH)");
    snprintf(m, sizeof m, "%s", fix_msg("FIX.4.4", "35=D|49=BUYSIDE|56=EXCHANGE|34=3|52=20261011-10:00:01.000|"
                                                   "11=ORD-0002|55=ACME|54=1|38=100|40=2|44=12.34567|"
                                                   "60=20261011-10:00:01.000|", '|'));
    expect_fail(ZA_SRC_FIX_PIPE, "", m, "new_order", ZA_R_CONTRACT, ZC_V_DENOMINATOR, "den",
                "a price finer than the tick");
    snprintf(m, sizeof m, "%s", fix_msg("FIX.4.4", "35=D|55=ACME|54=1|38=0|44=1|", '|'));
    expect_fail(ZA_SRC_FIX_PIPE, "", m, "new_order", ZA_R_CONTRACT, ZC_V_BELOW, "below", "zero quantity");
    snprintf(m, sizeof m, "%s", fix_msg("FIX.4.4", "35=D|55=ACME|54=1|38=2.5|44=1|", '|'));
    expect_fail(ZA_SRC_FIX_PIPE, "", m, "new_order", ZA_R_CONTRACT, ZC_V_NOT_INTEGER, "", "fractional quantity");
    snprintf(m, sizeof m, "%s", fix_msg("FIX.4.4", "35=D|55=ACME|38=1|44=1|", '|'));
    expect_fail(ZA_SRC_FIX_PIPE, "", m, "new_order", ZA_R_MISSING, ZC_V_ARITY, "'Side'", "no Side");
}

static void test_reading(void) {
    expect_pass(ZA_SRC_MQTT, "plant/7/boiler", "{\"value\": 87.125, \"unit\": \"C\"}", "reading", "MQTT reading");
    expect_fail(ZA_SRC_MQTT, "plant/7/boiler", "{\"value\": 151, \"unit\": \"C\"}", "reading", ZA_R_CONTRACT,
                ZC_V_ABOVE, "above", "out of physical range");
    expect_fail(ZA_SRC_MQTT, "plant/7/boiler", "{\"value\": 0.0001, \"unit\": \"C\"}", "reading", ZA_R_CONTRACT,
                ZC_V_DENOMINATOR, "den", "finer than the sensor resolves");
    expect_fail(ZA_SRC_MQTT, "plant/7/boiler", "21.5", "reading", ZA_R_MISSING, ZC_V_ARITY, "'unit'",
                "a bare decimal has no unit");
}

static void test_evidence_fn(void) {
    expect_pass(ZA_SRC_JSON, "", "{\"state\": \"neither/neutral/emerging\", \"score\": 0.5}", "evidence",
                "a tag field");
    expect_pass(ZA_SRC_JSON, "", "{\"state\": \"neither/entropic/potential\", \"score\": 1}", "evidence",
                "a tag matched by a wildcard");
    expect_fail(ZA_SRC_JSON, "", "{\"state\": \"true/neutral/manifest\", \"score\": 1}", "evidence", ZA_R_CONTRACT,
                ZC_V_NOT_IN_SET, "", "a payload cannot claim to be true");
    expect_fail(ZA_SRC_JSON, "", "{\"state\": \"verified\", \"score\": 1}", "evidence", ZA_R_TYPE, ZC_V_KIND,
                "cannot be tag", "not a tag");
    expect_fail(ZA_SRC_JSON, "", "{\"state\": 3, \"score\": 1}", "evidence", ZA_R_TYPE, ZC_V_KIND, "cannot be tag",
                "a number for a tag");
}

/* A sink that sees every record; refuses when told to. */
struct sink_state {
    int calls, refuse;
    uint32_t origin;
    int32_t tag;
};

static int32_t sink(void *user, uint64_t seq, uint32_t origin, int32_t tag, int32_t reason,
                    const uint8_t subject[32], const uint8_t hash[32], const char *text) {
    struct sink_state *s = (struct sink_state *) user;
    (void) seq;
    (void) reason;
    (void) subject;
    (void) hash;
    (void) text;
    s->calls++;
    s->origin = origin;
    s->tag = tag;
    return s->refuse;
}

static void test_log_failures(void) {
    static const char bad[] = "{\"value\": 999, \"unit\": \"C\"}";
    zc_evlog *log = NULL;
    za_record *r = NULL;
    struct sink_state ss = {0, 0, 0, 0};
    uint64_t count = 0;
    char tn[64];
    size_t tl = 0;

    CHECK(zc_evlog_create(1, &log) == ZXV_OK, "log of one");
    CHECK(run(ZA_SRC_MQTT, "t", bad, "reading", log, NULL) == ZA_E_REFUSED, "first refusal recorded");
    CHECK(run(ZA_SRC_MQTT, "t", bad, "reading", log, &r) == ZXV_E_CAPACITY, "a full log is reported");
    CHECK(r && lp_of(r) == ZXV_LP_FALSE, "and the record is refused anyway (fail closed)");
    za_record_destroy(r);
    zc_evlog_destroy(log);

    CHECK(zc_evlog_create(8, &log) == ZXV_OK && zc_evlog_set_sink(log, sink, &ss) == ZXV_OK, "log with sink");
    CHECK(run(ZA_SRC_MQTT, "t", bad, "reading", log, NULL) == ZA_E_REFUSED && ss.calls == 1 &&
              ss.origin == ZC_ORIGIN_ADAPTER, "the sink sees the adapter's evidence");
    CHECK(zc_tag_format(ss.tag, tn, sizeof tn, &tl) == ZXV_OK && strcmp(tn, "false/neutral/emerging") == 0,
          "the sink gets the record's tag: %s", tn);
    ss.refuse = 1;
    CHECK(run(ZA_SRC_MQTT, "t", bad, "reading", log, &r) == ZC_E_LOG, "a sink refusal is reported");
    CHECK(r && lp_of(r) == ZXV_LP_FALSE, "and the record is refused anyway");
    CHECK(zc_evlog_count(log, &count) == ZXV_OK && count == 1, "nothing committed for the refused append");
    za_record_destroy(r);
    zc_evlog_destroy(log);

    /* no log at all is allowed */
    CHECK(run(ZA_SRC_MQTT, "t", bad, "reading", NULL, NULL) == ZA_E_REFUSED, "validation without a log");
}

static void test_misuse(void) {
    za_record *r = ingest(ZA_SRC_JSON, "", "{\"value\": 1, \"unit\": \"C\"}");
    za_record *bad = ingest(ZA_SRC_JSON, "", "{");
    CHECK(za_validate(NULL, C, "reading", NULL) == ZXV_E_ARG && za_validate(r, NULL, "reading", NULL) == ZXV_E_ARG &&
              za_validate(r, C, NULL, NULL) == ZXV_E_ARG, "NULL arguments");
    CHECK(za_validate(r, C, "no_such_fn", NULL) == ZXV_E_NOT_FOUND, "unknown function");
    CHECK(lp_of(r) == ZXV_LP_NEITHER, "an unknown function does not refuse the record");
    CHECK(za_validate(bad, C, "reading", NULL) == ZXV_E_STATE, "a record refused at ingest");
    za_record_destroy(r);
    za_record_destroy(bad);
}

static void test_attest(void) {
    zxv_ctx *ctx = NULL;
    zxv_cap *root = NULL, *noattest = NULL, *child = NULL;
    za_record *r, *refused;
    CHECK(zxv_ctx_create(&ctx) == ZXV_OK && zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root) == ZXV_OK &&
              zxv_cap_attenuate(root, ZXV_CAP_ALL & ~ZXV_CAP_ATTEST, &noattest) == ZXV_OK &&
              zxv_cap_attenuate(root, ZXV_CAP_ATTEST, &child) == ZXV_OK, "caps");

    r = ingest(ZA_SRC_JSON, "", "{\"value\": 20, \"unit\": \"C\"}");
    CHECK(za_validate(r, C, "reading", NULL) == ZXV_OK, "validated");
    CHECK(za_record_attest(r, noattest, ZXV_LP_TRUE) == ZXV_E_CAP && lp_of(r) == ZXV_LP_NEITHER,
          "a cap without ZXV_CAP_ATTEST cannot attest");
    CHECK(za_record_attest(r, child, ZXV_LP_NEITHER) == ZXV_E_ARG && za_record_attest(r, child, 7) == ZXV_E_ARG,
          "attest TRUE, FALSE or BOTH only");
    CHECK(za_record_attest(NULL, child, ZXV_LP_TRUE) == ZXV_E_ARG && za_record_attest(r, NULL, ZXV_LP_TRUE) ==
              ZXV_E_ARG, "NULL arguments");
    CHECK(zxv_cap_set_attestation(child, ZXV_LP_BOTH) == ZXV_OK &&
              za_record_attest(r, child, ZXV_LP_TRUE) == ZXV_E_CAP, "a contradictorily attested cap cannot attest");
    CHECK(zxv_cap_set_attestation(child, ZXV_LP_TRUE) == ZXV_OK && za_record_attest(r, child, ZXV_LP_TRUE) == ZXV_OK,
          "the host attests TRUE");
    CHECK(lp_of(r) == ZXV_LP_TRUE && strcmp(tag_text(r), "true/neutral/manifest") == 0, "label %s", tag_text(r));
    CHECK(za_record_attest(r, child, ZXV_LP_FALSE) == ZXV_E_STATE, "attesting is once only");
    CHECK(za_validate(r, C, "reading", NULL) == ZXV_E_STATE, "an attested record is not re-validated");
    za_record_destroy(r);

    r = ingest(ZA_SRC_JSON, "", "{\"value\": 20, \"unit\": \"C\"}");
    CHECK(za_record_attest(r, child, ZXV_LP_BOTH) == ZXV_OK && strcmp(tag_text(r), "both/neutral/manifest") == 0,
          "BOTH: the host holds contradicting evidence");
    za_record_destroy(r);

    refused = ingest(ZA_SRC_JSON, "", "{\"value\": 1000, \"unit\": \"C\"}");
    CHECK(za_validate(refused, C, "reading", NULL) == ZA_E_REFUSED, "refused");
    CHECK(za_record_attest(refused, child, ZXV_LP_TRUE) == ZXV_E_STATE &&
              za_record_attest(refused, child, ZXV_LP_BOTH) == ZXV_E_STATE,
          "a refused record cannot be attested TRUE or BOTH");
    CHECK(za_record_attest(refused, child, ZXV_LP_FALSE) == ZXV_OK &&
              strcmp(tag_text(refused), "false/neutral/manifest") == 0, "it can be confirmed FALSE");
    za_record_destroy(refused);

    r = ingest(ZA_SRC_JSON, "", "{\"value\": 20, \"unit\": \"C\"}");
    CHECK(zxv_cap_revoke(root) == ZXV_OK && za_record_attest(r, child, ZXV_LP_TRUE) == ZXV_E_CAP,
          "revoking the parent stops the child");
    za_record_destroy(r);
    zxv_cap_destroy(child);
    zxv_cap_destroy(noattest);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
}

/* Kit API 0.2: libzxv tags, actions, and the quarantine sink. */
static int32_t action_of(const za_record *r) {
    int32_t a = -1;
    return za_record_action(r, &a) == ZXV_OK ? a : -1;
}

static void test_hex(void) {
    static const char good[] = "{\"value\": 20, \"unit\": \"C\"}";
    static const char bad[] = "{\"value\": 999, \"unit\": \"C\"}";
    zxv_ctx *ctx = NULL;
    zxv_cap *root = NULL, *attest = NULL;
    zxv_hexlog *q = NULL;
    zc_hexsink *hs = NULL;
    zc_evlog *log = NULL;
    za_record *r = NULL;
    int32_t lp = -1, tag = -1;
    uint8_t base = 9, pol = 9, ph = 9, htag = 0, hreason = 0, subj[32], eh[32];
    uint32_t count = 9, submitter = 9;
    char text[300];

    r = ingest(ZA_SRC_JSON, "", good);
    CHECK(r && za_record_state(r, &lp, &tag) == ZXV_OK && tag >= 0 && tag <= 63 &&
              zxv_hex_unpack((uint8_t) tag, &base, &pol, &ph) == ZXV_OK && base == ZXV_HEX_BASE_NEITHER &&
              pol == ZXV_HEX_POL_NEUTRAL && ph == ZXV_HEX_PHASE_EMERGING &&
              strcmp(zxv_hex_name((uint8_t) tag), "neither/neutral/emerging") == 0,
          "an accepted record carries the libzxv tag neither/neutral/emerging");
    CHECK(action_of(r) == ZXV_HEX_ACT_WAIT, "accepted, unverified: libzxv says WAIT");
    CHECK(za_record_action(NULL, &lp) == ZXV_E_ARG && za_record_action(r, NULL) == ZXV_E_ARG, "action NULLs");
    za_record_destroy(r);
    r = ingest(ZA_SRC_JSON, "", "{");
    CHECK(r && action_of(r) == ZXV_HEX_ACT_REFUSE && strcmp(tag_text(r), "false/neutral/emerging") == 0,
          "refused at ingest: REFUSE");
    za_record_destroy(r);

    CHECK(zxv_ctx_create(&ctx) == ZXV_OK && zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root) == ZXV_OK &&
              zxv_cap_attenuate(root, ZXV_CAP_ATTEST, &attest) == ZXV_OK, "caps");
    r = ingest(ZA_SRC_JSON, "", good);
    CHECK(za_record_attest(r, attest, ZXV_LP_TRUE) == ZXV_OK && za_record_state(r, NULL, &tag) == ZXV_OK &&
              zxv_hex_unpack((uint8_t) tag, &base, &pol, &ph) == ZXV_OK && base == ZXV_HEX_BASE_TRUE &&
              ph == ZXV_HEX_PHASE_MANIFEST && action_of(r) == ZXV_HEX_ACT_PROCEED,
          "attested TRUE: true/neutral/manifest, PROCEED");
    za_record_destroy(r);
    r = ingest(ZA_SRC_JSON, "", good);
    CHECK(za_record_attest(r, attest, ZXV_LP_BOTH) == ZXV_OK && action_of(r) == ZXV_HEX_ACT_ESCALATE,
          "attested BOTH: ESCALATE");
    za_record_destroy(r);

    /* the quarantine sink: every validation refusal becomes a Forbidden record */
    CHECK(zxv_hexlog_create(attest, 2, &q) == ZXV_OK && zc_hexsink_create(attest, q, 0, &hs) == ZXV_OK &&
              zc_evlog_create(8, &log) == ZXV_OK && zc_evlog_attach_hexsink(log, hs) == ZXV_OK,
          "evidence log with the quarantine sink");
    CHECK(run(ZA_SRC_MQTT, "plant/7", good, "reading", log, NULL) == ZXV_OK &&
              zxv_hexlog_count(q, &count) == ZXV_OK && count == 0,
          "a passing record is not quarantined");
    CHECK(run(ZA_SRC_MQTT, "plant/7", bad, "reading", log, &r) == ZA_E_REFUSED, "a refused record");
    CHECK(zxv_hexlog_count(q, &count) == ZXV_OK && count == 1 &&
              zxv_hexlog_at(q, 0, NULL, &htag, &hreason, &submitter, subj, NULL, NULL) == ZXV_OK &&
              strcmp(zxv_hex_name(htag), "false/forbidden/emerging") == 0 && hreason == ZC_V_ABOVE &&
              submitter == ZC_ORIGIN_ADAPTER,
          "quarantined as %s, reason %u, submitter %u", zxv_hex_name(htag), (unsigned) hreason,
          (unsigned) submitter);
    CHECK(zc_evlog_get(log, 1, NULL, NULL, NULL, NULL, NULL, NULL, NULL, eh, text, sizeof text, NULL) == ZXV_OK &&
              memcmp(eh, subj, 32) == 0,
          "the quarantine record points at the evidence record");
    CHECK(strcmp(tag_text(r), "false/neutral/emerging") == 0 && action_of(r) == ZXV_HEX_ACT_REFUSE,
          "the record itself stays false/neutral/emerging (refused, not forbidden work)");
    za_record_destroy(r);
    r = NULL;
    CHECK(run(ZA_SRC_MQTT, "plant/7", bad, "reading", log, NULL) == ZA_E_REFUSED &&
              run(ZA_SRC_MQTT, "plant/7", bad, "reading", log, &r) == ZC_E_LOG,
          "a full quarantine log refuses the evidence (ZC_E_LOG)");
    CHECK(r && lp_of(r) == ZXV_LP_FALSE, "and the record is refused anyway (fail closed)");
    CHECK(zxv_hexlog_verify(q) == ZXV_OK && zc_evlog_verify(log) == ZXV_OK, "both chains verify");
    za_record_destroy(r);
    zc_evlog_destroy(log);
    zc_hexsink_destroy(hs);
    zxv_hexlog_destroy(q);
    zxv_cap_destroy(attest);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
}

int main(int argc, char **argv) {
    size_t len = 0;
    unsigned char *text;
    char msg[300];
    uint32_t line = 0, col = 0;
    if (argc != 2) {
        fprintf(stderr, "usage: test_validate CONTRACT.zc\n");
        return 2;
    }
    text = slurp(argv[1], &len);
    if (!text || zc_parse((const char *) text, len, &C, &line, &col, NULL, msg, sizeof msg, NULL) != ZXV_OK) {
        fprintf(stderr, "test_validate: %s:%u:%u: %s\n", argv[1], (unsigned) line, (unsigned) col,
                text ? msg : "cannot read");
        return 2;
    }
    free(text);
    zc_contract_digest(C, C_DIGEST);
    printf("test_validate: records against %s\n", argv[1]);
    test_payment();
    test_new_order();
    test_reading();
    test_evidence_fn();
    test_log_failures();
    test_misuse();
    test_attest();
    test_hex();
    zc_contract_destroy(C);
    printf("test_validate: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
