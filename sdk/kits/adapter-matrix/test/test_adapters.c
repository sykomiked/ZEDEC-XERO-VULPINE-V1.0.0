/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_adapters.c — exact decimals, and the JSON, MQTT and FIX adapters:
 * accepted records with their exact fields and labels, refusals with their
 * reasons, limits, and a published FIX known-answer message. No network. */
#include "za_test.h"

typedef struct {
    const char *text;
    int32_t st;
    int64_t num, den;
} dec_case;

static const dec_case k_dec[] = {
    {"0", ZXV_OK, 0, 1},
    {"-0", ZXV_OK, 0, 1},
    {"-0.000", ZXV_OK, 0, 1},
    {"0.1", ZXV_OK, 1, 10},
    {"12.75", ZXV_OK, 51, 4},
    {"-12.50", ZXV_OK, -25, 2},
    {"1.5e-3", ZXV_OK, 3, 2000},
    {"7E+2", ZXV_OK, 700, 1},
    {"1e0", ZXV_OK, 1, 1},
    {"0e999999999999", ZXV_OK, 0, 1},
    {"0.30000000000000004", ZXV_OK, INT64_C(7500000000000001), INT64_C(25000000000000000)},
    {"1.50000000000000000000000000", ZXV_OK, 3, 2},
    {"5e-19", ZXV_OK, 1, INT64_C(2000000000000000000)},
    {"9223372036854775807", ZXV_OK, INT64_MAX, 1},
    {"-9223372036854775807", ZXV_OK, -INT64_MAX, 1},
    {"922337203685477580.7", ZXV_OK, INT64_MAX, 10},
    {"1e18", ZXV_OK, INT64_C(1000000000000000000), 1},
    {"1e-18", ZXV_OK, 1, INT64_C(1000000000000000000)},
    {"00012", ZXV_OK, 12, 1},
    {"120e-1", ZXV_OK, 12, 1},
    {"9223372036854775808", ZXV_E_RANGE, 0, 0},
    {"-9223372036854775808", ZXV_E_RANGE, 0, 0},
    {"1e19", ZXV_E_RANGE, 0, 0},
    {"3e-19", ZXV_E_RANGE, 0, 0},
    {"123456789012345678901", ZXV_E_RANGE, 0, 0},
    {"1e400", ZXV_E_RANGE, 0, 0},
    {"1e-400", ZXV_E_RANGE, 0, 0},
    {"", ZXV_E_FORMAT, 0, 0},
    {"-", ZXV_E_FORMAT, 0, 0},
    {".5", ZXV_E_FORMAT, 0, 0},
    {"5.", ZXV_E_FORMAT, 0, 0},
    {"1e", ZXV_E_FORMAT, 0, 0},
    {"1e+", ZXV_E_FORMAT, 0, 0},
    {"+1", ZXV_E_FORMAT, 0, 0},
    {"1.2.3", ZXV_E_FORMAT, 0, 0},
    {"0x10", ZXV_E_FORMAT, 0, 0},
    {" 1", ZXV_E_FORMAT, 0, 0},
    {"1 ", ZXV_E_FORMAT, 0, 0},
    {"NaN", ZXV_E_FORMAT, 0, 0},
    {"inf", ZXV_E_FORMAT, 0, 0},
    {"1,5", ZXV_E_FORMAT, 0, 0},
};

static void test_decimal(void) {
    for (size_t i = 0; i < sizeof k_dec / sizeof k_dec[0]; i++) {
        const dec_case *c = &k_dec[i];
        int64_t n = 99, d = 99;
        int32_t st = za_decimal_parse(c->text, strlen(c->text), &n, &d);
        CHECK(st == c->st, "\"%s\": status %s, want %s", c->text, za_status_name(st), za_status_name(c->st));
        if (st == ZXV_OK && c->st == ZXV_OK)
            CHECK(n == c->num && d == c->den, "\"%s\" = %lld/%lld, want %lld/%lld", c->text, (long long) n,
                  (long long) d, (long long) c->num, (long long) c->den);
        /* agree with libzxv's own exact parser where its grammar applies */
        int plain = c->st == ZXV_OK && !strpbrk(c->text, "eE");
        if (plain) {
            int64_t zn = 0, zd = 0;
            CHECK(zxv_rat_parse(c->text, &zn, &zd) == ZXV_OK && zn == n && zd == d,
                  "\"%s\" agrees with zxv_rat_parse", c->text);
        }
    }
    int64_t n, d;
    CHECK(za_decimal_parse("12", 1, &n, &d) == ZXV_OK && n == 1, "length bounds the text");
    CHECK(za_decimal_parse(NULL, 0, &n, &d) == ZXV_E_ARG, "NULL text");
}

static void expect_refused(za_record *r, int32_t reason, const char *msg_sub, const char *what) {
    const char *m = NULL;
    uint32_t nf = 9;
    CHECK(r != NULL, "%s: a record", what);
    if (!r) return;
    CHECK(lp_of(r) == ZXV_LP_FALSE && strcmp(tag_text(r), "false/neutral/emerging") == 0,
          "%s: refused (%s)", what, tag_text(r));
    CHECK(reason0(r, &m) == reason && strstr(m, msg_sub), "%s: reason %s \"%s\", want %s \"%s\"", what,
          za_reason_name(reason0(r, NULL)), m, za_reason_name(reason), msg_sub);
    CHECK(za_record_field_count(r, &nf) == ZXV_OK && nf == 0, "%s: a refused record keeps no fields", what);
    za_record_destroy(r);
}

static void expect_accepted(za_record *r, uint32_t nfields, const char *what) {
    uint32_t nf = 999, nr = 9;
    CHECK(r != NULL, "%s: a record", what);
    if (!r) return;
    CHECK(lp_of(r) == ZXV_LP_NEITHER && strcmp(tag_text(r), "neither/neutral/emerging") == 0,
          "%s: accepted as neither/neutral/emerging (got %s; %s)", what, tag_text(r),
          za_record_reason(r, 0, NULL, NULL) == ZXV_OK ? "refused" : "");
    CHECK(za_record_reason_count(r, &nr) == ZXV_OK && nr == 0, "%s: no reasons", what);
    CHECK(za_record_field_count(r, &nf) == ZXV_OK && nf == nfields, "%s: %u fields, want %u", what,
          (unsigned) nf, (unsigned) nfields);
}

static void test_json(void) {
    int64_t n = 0, d = 0;
    za_record *r = ingest(ZA_SRC_JSON, "https://bank.example/api/payments/1",
                          "{\"amount\": {\"value\": \"12.50\", \"currency\": \"NGN\"},\n"
                          " \"reference\": \"INV-1\", \"urgent\": true, \"fee\": 0.1, \"note\": null}");
    expect_accepted(r, 6, "payment");
    CHECK(field_is_text(r, "amount.value", "12.50") && field_is_text(r, "amount.currency", "NGN") &&
              field_is_text(r, "reference", "INV-1"), "nested paths and text");
    CHECK(field_rat(r, "fee", &n, &d) == 0 && n == 1 && d == 10, "0.1 is exactly 1/10");
    CHECK(field_rat(r, "urgent", &n, &d) == 0 && n == 1 && d == 1, "true is BOOL 1");
    uint32_t i = 0;
    int32_t kind = 0;
    const char *name = NULL, *src = NULL;
    CHECK(za_field_find(r, "note", &i) == ZXV_OK && za_field(r, i, &name, &kind) == ZXV_OK && kind == ZA_F_NULL,
          "null");
    CHECK(za_field_rat(r, i, &n, &d) == ZXV_E_STATE, "null is not a number");
    CHECK(za_record_source(r, &kind, &src) == ZXV_OK && kind == ZA_SRC_JSON &&
              strcmp(src, "https://bank.example/api/payments/1") == 0, "source label");
    CHECK(za_field_find(r, "missing", &i) == ZXV_E_NOT_FOUND, "find missing");
    CHECK(za_field(r, 6, &name, &kind) == ZXV_E_NOT_FOUND, "field past the end");
    uint8_t d1[32], d2[32];
    za_record *r2 = ingest(ZA_SRC_JSON, "https://bank.example/api/payments/1",
                           "{\"amount\":{\"value\":\"12.50\",\"currency\":\"NGN\"},\"reference\":\"INV-1\","
                           "\"urgent\":true,\"fee\":0.1,\"note\":null}");
    CHECK(za_record_digest(r, d1) == ZXV_OK && za_record_digest(r2, d2) == ZXV_OK && memcmp(d1, d2, 32) == 0,
          "whitespace does not change the digest");
    za_record_destroy(r2);
    r2 = ingest(ZA_SRC_JSON, "https://bank.example/api/payments/1",
                "{\"amount\":{\"value\":\"12.51\",\"currency\":\"NGN\"},\"reference\":\"INV-1\","
                "\"urgent\":true,\"fee\":0.1,\"note\":null}");
    CHECK(za_record_digest(r2, d2) == ZXV_OK && memcmp(d1, d2, 32) != 0, "a value changes the digest");
    za_record_destroy(r2);
    za_record_destroy(r);

    r = ingest(ZA_SRC_JSON, "", "{\"items\": [{\"p\": 1.25}, {\"p\": 2}, []], \"n\": -7e-2}");
    expect_accepted(r, 3, "arrays");
    CHECK(field_rat(r, "items.0.p", &n, &d) == 0 && n == 5 && d == 4, "items.0.p = 5/4");
    CHECK(field_rat(r, "items.1.p", &n, &d) == 0 && n == 2 && d == 1, "items.1.p = 2");
    CHECK(field_rat(r, "n", &n, &d) == 0 && n == -7 && d == 100, "-7e-2 = -7/100");
    const uint8_t *t = NULL;
    size_t tl = 0;
    CHECK(za_field_find(r, "n", &i) == ZXV_OK && za_field_text(r, i, &t, &tl) == ZXV_OK && tl == 5 &&
              memcmp(t, "-7e-2", 5) == 0, "a number keeps its original text");
    za_record_destroy(r);

    r = ingest(ZA_SRC_JSON, "", "  42 ");
    expect_accepted(r, 1, "scalar root");
    CHECK(field_rat(r, "value", &n, &d) == 0 && n == 42, "root scalar is 'value'");
    za_record_destroy(r);
    r = ingest(ZA_SRC_JSON, "", "{}");
    expect_accepted(r, 0, "empty object");
    za_record_destroy(r);
    r = ingest(ZA_SRC_JSON, "", "{\"e\":\"\\u00e9\\ud83d\\ude00\\n\\\"\\/\",\"z\":\"a\\u0000b\"}");
    expect_accepted(r, 2, "escapes");
    CHECK(field_is_text(r, "e", "\xc3\xa9\xf0\x9f\x98\x80\n\"/"), "\\u escapes and a surrogate pair decode to UTF-8");
    CHECK(za_field_find(r, "z", &i) == ZXV_OK && za_field_text(r, i, &t, &tl) == ZXV_OK && tl == 3 && t[1] == 0,
          "an escaped NUL is kept with an explicit length");
    za_record_destroy(r);

    expect_refused(ingest(ZA_SRC_JSON, "", "{"), ZA_R_MALFORMED, "JSON at byte 1: expected a key string", "unterminated");
    expect_refused(ingest(ZA_SRC_JSON, "", ""), ZA_R_MALFORMED, "expected a value", "empty");
    expect_refused(ingest(ZA_SRC_JSON, "", "{\"a\":1,}"), ZA_R_MALFORMED, "byte 7: expected a key string", "trailing comma");
    expect_refused(ingest(ZA_SRC_JSON, "", "{\"a\":01}"), ZA_R_MALFORMED, "expected ',' or '}'", "leading zero");
    expect_refused(ingest(ZA_SRC_JSON, "", "{\"a\":1}{\"b\":2}"), ZA_R_MALFORMED, "text after the JSON value", "two values");
    expect_refused(ingest(ZA_SRC_JSON, "", "{\"a\":1,\"a\":2}"), ZA_R_DUPLICATE, "given twice", "duplicate key");
    expect_refused(ingest(ZA_SRC_JSON, "", "{\"a.b\":1,\"a\":{\"b\":2}}"), ZA_R_DUPLICATE, "given twice", "dotted key collision");
    expect_refused(ingest(ZA_SRC_JSON, "", "{\"a\":1e400}"), ZA_R_NUMBER, "byte 5: a number with no exact int64", "1e400");
    expect_refused(ingest(ZA_SRC_JSON, "", "{\"a\":0.12345678901234567890123}"), ZA_R_NUMBER, "nothing is rounded", "too precise");
    expect_refused(ingest(ZA_SRC_JSON, "", "{\"a\":\"\\ud800\"}"), ZA_R_UTF8, "unpaired surrogate", "lone high surrogate");
    expect_refused(ingest(ZA_SRC_JSON, "", "{\"a\":\"\\udc00\"}"), ZA_R_UTF8, "unpaired surrogate", "lone low surrogate");
    expect_refused(ingest(ZA_SRC_JSON, "", "{\"a\":\"\xff\"}"), ZA_R_UTF8, "not valid UTF-8", "raw invalid UTF-8");
    expect_refused(ingest(ZA_SRC_JSON, "", "\xef\xbb\xbf{}"), ZA_R_MALFORMED, "byte order mark", "BOM");
    expect_refused(ingest(ZA_SRC_JSON, "", "{\"a\":\"x\ty\"}"), ZA_R_MALFORMED, "control character", "raw tab");
    expect_refused(ingest(ZA_SRC_JSON, "", "{\"a\\u0000\":1}"), ZA_R_MALFORMED, "a key contains NUL", "NUL key");
    expect_refused(ingest(ZA_SRC_JSON, "", "{\"\":1}"), ZA_R_UNSUPPORTED, "empty key", "empty key");
    expect_refused(ingest(ZA_SRC_JSON, "", "{\"a\":{\"\":[]}}"), ZA_R_UNSUPPORTED, "empty key", "nested empty key");
    expect_refused(ingest(ZA_SRC_JSON, "", "{\"a\":\"\\x\"}"), ZA_R_MALFORMED, "unknown escape", "bad escape");
    expect_refused(ingest(ZA_SRC_JSON, "", "{\"a\":tru}"), ZA_R_MALFORMED, "unknown literal", "bad literal");
    expect_refused(ingest(ZA_SRC_JSON, "", "{\"a\":NaN}"), ZA_R_MALFORMED, "unexpected character", "NaN");
    expect_refused(ingest(ZA_SRC_JSON, "", "{\"a\":-}"), ZA_R_MALFORMED, "bad number", "bare minus");
    expect_refused(ingest(ZA_SRC_JSON, "", "{\"a\":1.}"), ZA_R_MALFORMED, "no digit after '.'", "1.");
    expect_refused(ingest(ZA_SRC_JSON, "", "[1 2]"), ZA_R_MALFORMED, "expected ',' or ']'", "missing comma");

    /* limits: depth 32 accepted, 33 refused; 1024 fields, 1025 refused; a
     * 256-byte path refused; a payload over 1 MiB refused */
    char *big = (char *) malloc(ZA_MAX_PAYLOAD + 16u);
    size_t k = 0;
    for (int lvl = 0; lvl < 32; lvl++) big[k++] = '[';
    big[k++] = '1';
    for (int lvl = 0; lvl < 32; lvl++) big[k++] = ']';
    big[k] = 0;
    r = ingest(ZA_SRC_JSON, "", big);
    expect_accepted(r, 1, "depth 32");
    za_record_destroy(r);
    k = 0;
    for (int lvl = 0; lvl < 33; lvl++) big[k++] = '[';
    big[k++] = '1';
    for (int lvl = 0; lvl < 33; lvl++) big[k++] = ']';
    big[k] = 0;
    expect_refused(ingest(ZA_SRC_JSON, "", big), ZA_R_LIMIT, "nested deeper than 32", "depth 33");
    k = (size_t) snprintf(big, 16, "[");
    for (int f = 0; f < 1024; f++) k += (size_t) snprintf(big + k, 16, "%s%d", f ? "," : "", f);
    snprintf(big + k, 4, "]");
    r = ingest(ZA_SRC_JSON, "", big);
    expect_accepted(r, 1024, "1024 fields");
    za_record_destroy(r);
    snprintf(big + k, 8, ",1]");
    expect_refused(ingest(ZA_SRC_JSON, "", big), ZA_R_LIMIT, "more than 1024 fields", "1025 fields");
    k = (size_t) snprintf(big, 8, "{\"");
    for (int c = 0; c < 256; c++) big[k++] = 'k';
    snprintf(big + k, 8, "\":1}");
    expect_refused(ingest(ZA_SRC_JSON, "", big), ZA_R_LIMIT, "longer than 255 bytes", "256-byte key");
    big[2 + 255] = '"';
    snprintf(big + 2 + 256, 8, ":1}");
    r = ingest(ZA_SRC_JSON, "", big);
    expect_accepted(r, 1, "255-byte key");
    za_record_destroy(r);
    memset(big, ' ', ZA_MAX_PAYLOAD + 1u);
    big[0] = '1';
    r = NULL;
    CHECK(za_ingest(ZA_SRC_JSON, "", (const uint8_t *) big, ZA_MAX_PAYLOAD + 1u, &r) == ZXV_OK, "ingest 1 MiB + 1");
    expect_refused(r, ZA_R_LIMIT, "over the 1048576-byte limit", "over 1 MiB");
    r = NULL;
    CHECK(za_ingest(ZA_SRC_JSON, "", (const uint8_t *) big, ZA_MAX_PAYLOAD, &r) == ZXV_OK, "ingest 1 MiB");
    expect_accepted(r, 1, "1 MiB of '1' and spaces");
    za_record_destroy(r);
    free(big);

    CHECK(za_ingest(ZA_SRC_JSON, "", NULL, 3, &r) == ZXV_E_ARG && za_ingest(9, "", (const uint8_t *) "1", 1, &r) ==
              ZXV_E_ARG && za_ingest(ZA_SRC_JSON, "", (const uint8_t *) "1", 1, NULL) == ZXV_E_ARG, "API misuse");
}

static void test_mqtt(void) {
    int64_t n = 0, d = 0;
    const char *src = NULL;
    za_record *r = ingest(ZA_SRC_MQTT, "plant/7/temp", "21.5");
    expect_accepted(r, 1, "decimal payload");
    CHECK(field_rat(r, "value", &n, &d) == 0 && n == 43 && d == 2, "21.5 = 43/2");
    CHECK(za_record_source(r, NULL, &src) == ZXV_OK && strcmp(src, "plant/7/temp") == 0, "the topic is the source");
    za_record_destroy(r);
    r = ingest(ZA_SRC_MQTT, "plant/7/temp", "  -3e2\r\n");
    expect_accepted(r, 1, "exponent payload");
    CHECK(field_rat(r, "value", &n, &d) == 0 && n == -300 && d == 1, "-3e2 = -300");
    za_record_destroy(r);
    r = ingest(ZA_SRC_MQTT, "plant/7/boiler", "{\"value\": 87.125, \"unit\": \"C\"}");
    expect_accepted(r, 2, "JSON payload");
    CHECK(field_rat(r, "value", &n, &d) == 0 && n == 697 && d == 8 && field_is_text(r, "unit", "C"), "87.125 = 697/8");
    za_record_destroy(r);
    r = ingest(ZA_SRC_MQTT, "door/1", "OPEN");
    expect_accepted(r, 1, "text payload");
    CHECK(field_is_text(r, "value", "OPEN"), "text value");
    za_record_destroy(r);
    r = ingest(ZA_SRC_MQTT, "door/1", "");
    expect_accepted(r, 0, "empty payload (retained-message clear)");
    za_record_destroy(r);
    expect_refused(ingest(ZA_SRC_MQTT, "plant/7/temp", "1e400"), ZA_R_NUMBER, "no exact int64", "1e400");
    expect_refused(ingest(ZA_SRC_MQTT, "plant/+/temp", "1"), ZA_R_TOPIC, "wildcard", "+ in a topic");
    expect_refused(ingest(ZA_SRC_MQTT, "plant/#", "1"), ZA_R_TOPIC, "wildcard", "# in a topic");
    expect_refused(ingest(ZA_SRC_MQTT, "", "1"), ZA_R_TOPIC, "1..65535 bytes", "empty topic");
    expect_refused(ingest(ZA_SRC_MQTT, "a/\xff", "1"), ZA_R_TOPIC, "not valid UTF-8", "bad UTF-8 topic");
    expect_refused(ingest(ZA_SRC_MQTT, "bin", "\x08\xff\xfe"), ZA_R_UTF8, "neither JSON", "binary payload");
    expect_refused(ingest(ZA_SRC_MQTT, "j", "{\"a\":"), ZA_R_MALFORMED, "expected a value", "broken JSON payload");
    za_record *x = NULL;
    CHECK(za_ingest(ZA_SRC_MQTT, NULL, (const uint8_t *) "1", 1, &x) == ZXV_E_ARG, "MQTT needs a topic");
}

static void test_fix(void) {
    int64_t n = 0, d = 0;
    const char *src = NULL;
    static const char body[] = "35=D|49=BUYSIDE|56=EXCHANGE|34=2|52=20261011-10:00:00.000|11=ORD-0001|"
                               "55=ACME|54=1|38=100|40=2|44=12.3450|60=20261011-10:00:00.000|";
    /* the published example from the FIX community (BodyLength 65, CheckSum 062) */
    za_record *r = ingest(ZA_SRC_FIX_PIPE, "",
                          "8=FIX.4.2|9=65|35=A|49=SERVER|56=CLIENT|34=177|52=20090107-18:15:16|98=0|108=30|10=062|");
    expect_accepted(r, 10, "FIX known answer (logon)");
    CHECK(za_record_source(r, NULL, &src) == ZXV_OK && strcmp(src, "SERVER->CLIENT") == 0, "source %s", src);
    CHECK(field_rat(r, "HeartBtInt", &n, &d) == 0 && n == 30 && field_is_text(r, "MsgType", "A"), "logon fields");
    za_record_destroy(r);

    char m[1024]; /* fix_msg returns a static buffer: keep a copy */
    snprintf(m, sizeof m, "%s", fix_msg("FIX.4.4", body, '|'));
    CHECK(strcmp(m, "8=FIX.4.4|9=131|35=D|49=BUYSIDE|56=EXCHANGE|34=2|52=20261011-10:00:00.000|11=ORD-0001|55=ACME|"
                    "54=1|38=100|40=2|44=12.3450|60=20261011-10:00:00.000|10=023|") == 0,
          "the test's own framing matches an independent computation: %s", m);
    r = ingest(ZA_SRC_FIX_PIPE, "", m);
    expect_accepted(r, 15, "new order single");
    CHECK(field_rat(r, "Price", &n, &d) == 0 && n == 2469 && d == 200, "Price 12.3450 = 2469/200");
    CHECK(field_rat(r, "OrderQty", &n, &d) == 0 && n == 100 && d == 1, "OrderQty 100");
    CHECK(field_is_text(r, "Symbol", "ACME") && field_is_text(r, "Side", "1") && field_is_text(r, "ClOrdID", "ORD-0001"),
          "text fields");
    uint8_t dp[32], ds[32];
    za_record_digest(r, dp);
    za_record_destroy(r);
    /* the same message with real SOH delimiters gives the same fields */
    r = ingest(ZA_SRC_FIX, "", fix_msg("FIX.4.4", body, 1));
    expect_accepted(r, 15, "SOH-delimited");
    za_record_digest(r, ds);
    CHECK(memcmp(dp, ds, 32) != 0, "the source kind is part of the digest");
    CHECK(field_rat(r, "Price", &n, &d) == 0 && n == 2469 && d == 200, "SOH Price");
    za_record_destroy(r);
    r = ingest(ZA_SRC_FIX_PIPE, "venue-a", m);
    CHECK(r && za_record_source(r, NULL, &src) == ZXV_OK && strcmp(src, "venue-a") == 0, "a caller label is kept");
    za_record_destroy(r);

    /* repeating groups keep their order with an occurrence suffix */
    r = ingest(ZA_SRC_FIX_PIPE, "", fix_msg("FIX.4.4", "35=W|262=R1|268=2|269=0|270=1.5|269=1|270=1.625|", '|'));
    expect_accepted(r, 10, "market data");
    CHECK(field_rat(r, "MDEntryPx", &n, &d) == 0 && n == 3 && d == 2 && field_rat(r, "MDEntryPx.1", &n, &d) == 0 &&
              n == 13 && d == 8 && field_is_text(r, "MDEntryType.1", "1"), "occurrence suffixes");
    za_record_destroy(r);
    r = ingest(ZA_SRC_FIX_PIPE, "", fix_msg("FIX.4.4", "35=D|5001=custom|", '|'));
    expect_accepted(r, 5, "unknown tag");
    CHECK(field_is_text(r, "Tag5001", "custom"), "unknown tags are TagN");
    za_record_destroy(r);

    char bad[4096];
    snprintf(bad, sizeof bad, "%s", m);
    bad[strlen(bad) - 2] = bad[strlen(bad) - 2] == '9' ? '8' : '9'; /* break the checksum */
    expect_refused(ingest(ZA_SRC_FIX_PIPE, "", bad), ZA_R_FIX_CHECKSUM, "CheckSum says", "bad checksum");
    snprintf(bad, sizeof bad, "%s", m);
    bad[strstr(bad, "ACME") - bad] = 'B'; /* same length, different sum */
    expect_refused(ingest(ZA_SRC_FIX_PIPE, "", bad), ZA_R_FIX_CHECKSUM, "the bytes sum to", "altered body");
    snprintf(bad, sizeof bad, "%s", m);
    memcpy(strstr(bad, "9=131"), "9=130", 5);
    expect_refused(ingest(ZA_SRC_FIX_PIPE, "", bad), ZA_R_FIX_LENGTH, "BodyLength says 130, the body has 131", "bad length");
    expect_refused(ingest(ZA_SRC_FIX_PIPE, "", "9=5|8=FIX.4.4|35=0|10=000|"), ZA_R_FIX_HEADER, "must come first", "header order");
    expect_refused(ingest(ZA_SRC_FIX_PIPE, "", fix_msg("FIX.4.4", "49=X|35=0|", '|')), ZA_R_FIX_HEADER, "must come first",
                   "35 not third");
    expect_refused(ingest(ZA_SRC_FIX_PIPE, "", fix_msg("FIX.4.4", "35=0|8=FIX.4.4|", '|')), ZA_R_FIX_HEADER, "repeated",
                   "8 repeated");
    snprintf(bad, sizeof bad, "%s", m);
    bad[strlen(bad) - 1] = 0;
    expect_refused(ingest(ZA_SRC_FIX_PIPE, "", bad), ZA_R_MALFORMED, "does not end with a delimiter", "no trailing delimiter");
    expect_refused(ingest(ZA_SRC_FIX_PIPE, "", fix_msg("FIX.4.4", "35=D|044=1|", '|')), ZA_R_MALFORMED, "leading zero", "tag 044");
    expect_refused(ingest(ZA_SRC_FIX_PIPE, "", fix_msg("FIX.4.4", "35=D|44=|", '|')), ZA_R_MALFORMED, "empty value", "empty");
    expect_refused(ingest(ZA_SRC_FIX_PIPE, "", fix_msg("FIX.4.4", "35=D|abc|", '|')), ZA_R_MALFORMED, "expected TAG=VALUE",
                   "no tag");
    expect_refused(ingest(ZA_SRC_FIX_PIPE, "", fix_msg("FIX.4.4", "35=D|1234567890=x|", '|')), ZA_R_MALFORMED,
                   "longer than 9 digits", "10-digit tag");
    expect_refused(ingest(ZA_SRC_FIX_PIPE, "", fix_msg("FIX.4.4", "35=D|44=1e3|", '|')), ZA_R_NUMBER,
                   "Price (44) is not an exact decimal: \"1e3\"", "exponent price");
    expect_refused(ingest(ZA_SRC_FIX_PIPE, "", fix_msg("FIX.4.4", "35=D|38=ten|", '|')), ZA_R_NUMBER, "OrderQty", "word qty");
    expect_refused(ingest(ZA_SRC_FIX_PIPE, "", fix_msg("FIX.4.4", "35=D|95=1|96=a|", '|')), ZA_R_UNSUPPORTED,
                   "data field 96", "RawData");
    expect_refused(ingest(ZA_SRC_FIX_PIPE, "", fix_msg("FIX.4.4", "35=D|58=\xff|", '|')), ZA_R_UTF8, "not valid UTF-8",
                   "bad UTF-8 value");
    expect_refused(ingest(ZA_SRC_FIX_PIPE, "", ""), ZA_R_MALFORMED, "empty message", "empty");
    expect_refused(ingest(ZA_SRC_FIX, "", m), ZA_R_MALFORMED, "does not end with a delimiter", "pipes read as SOH");
}

static void test_names(void) {
    CHECK(strcmp(za_reason_name(ZA_R_FIX_CHECKSUM), "FIX_CHECKSUM") == 0 && strcmp(za_reason_name(0), "UNKNOWN") == 0 &&
              strcmp(za_reason_name(ZA_R_CONTRACT), "CONTRACT") == 0, "reason names");
    CHECK(strcmp(za_status_name(ZA_E_REFUSED), "ZA_E_REFUSED") == 0 &&
              strcmp(za_status_name(ZC_E_LOG), "ZC_E_LOG") == 0 && strcmp(za_status_name(ZXV_E_ARG), "ZXV_E_ARG") == 0,
          "status names");
}

int main(void) {
    printf("test_adapters: exact decimals, JSON, MQTT and FIX\n");
    test_decimal();
    test_json();
    test_mqtt();
    test_fix();
    test_names();
    printf("test_adapters: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
