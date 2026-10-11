/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_record.c — value strings <-> typed fields: decoding, canonical
 * encoding, keys from references, getters and setters, every kind of
 * refusal, and a seeded round trip over random schemas and values. */
#include "zm_internal.h"
#include "zm_test.h"

static zm_schema *g_s;

static zm_record *dec(const char *ref, const char *value, int32_t *st) {
    zm_record *r = NULL;
    *st = zm_node_decode(g_s, ref, strlen(ref), value, strlen(value), &r);
    return r;
}

static void decode_vectors(void) {
    static const struct {
        const char *ref, *value;
        int32_t status;
        const char *canonical; /* on ZXV_OK */
        const char *msg;       /* on refusal: a substring of the last error */
    } k[] = {
        {"^ZXPAY(7,1)", "FUND-0001^1000^NGN^1^2^67854^N", ZXV_OK, "FUND-0001^1000^NGN^1^2^67854^N", NULL},
        {"^ZXPAY(7,1)", "R^12.5^NGN^1^2", ZXV_OK, "R^12.5^NGN^1^2", NULL},
        {"^ZXPAY(7,1)", "R^-.25^NGN^1^2^^^^", ZXV_OK, "R^-.25^NGN^1^2", NULL},
        {"^ZXPAY(7,1)", "R^0^NGN^0^0^0", ZXV_OK, "R^0^NGN^0^0^0", NULL},
        {"^ZXPAY(7,1)", "R^9999999999999.99^NGN^9999999999^1^2980013^Z^^memo text, with commas; and \"quotes\"", ZXV_OK,
         "R^9999999999999.99^NGN^9999999999^1^2980013^Z^^memo text, with commas; and \"quotes\"", NULL},
        {"^ZXPAY(7,1)", "caf\xc3\xa9^1^EUR^1^2", ZXV_OK, "caf\xc3\xa9^1^EUR^1^2", NULL},
        {"^ZXPAY(7)", "67854^3^1250.5^TEST BATCH", ZXV_OK, "67854^3^1250.5^TEST BATCH", NULL},
        {"^ZXPAY(7)", "67854", ZXV_OK, "67854", NULL},
        {"^ZXPAY(\"B\",\"PAY-1\",7,1)", "", ZXV_OK, "", NULL},
        {"^ZXPAY(\"B\",\"A\"\"B\",7,1)", "", ZXV_OK, "", NULL},
        /* numbers must be in M canonical form */
        {"^ZXPAY(7,1)", "R^12.50^NGN^1^2", ZM_E_DATA, NULL, "\"12.50\" is not a number in M canonical form"},
        {"^ZXPAY(7,1)", "R^0.5^NGN^1^2", ZM_E_DATA, NULL, "piece 2 (amount)"},
        {"^ZXPAY(7,1)", "R^+1^NGN^1^2", ZM_E_DATA, NULL, "canonical"},
        {"^ZXPAY(7,1)", "R^1E3^NGN^1^2", ZM_E_DATA, NULL, "canonical"},
        {"^ZXPAY(7,1)", "R^ 1^NGN^1^2", ZM_E_DATA, NULL, "canonical"},
        {"^ZXPAY(7,1)", "R^-0^NGN^1^2", ZM_E_DATA, NULL, "canonical"},
        {"^ZXPAY(7,1)", "R^1.^NGN^1^2", ZM_E_DATA, NULL, "canonical"},
        {"^ZXPAY(7,1)", "R^1^NGN^01^2", ZM_E_DATA, NULL, "piece 4 (from)"},
        {"^ZXPAY(7,1)", "R^1^NGN^1^2^1E2", ZM_E_DATA, NULL, "piece 6 (value-date)"},
        /* and fit the field exactly */
        {"^ZXPAY(7,1)", "R^1.005^NGN^1^2", ZM_E_FIT, NULL, "more than 2 decimals (never rounded)"},
        {"^ZXPAY(7,1)", "R^10000000000000^NGN^1^2", ZM_E_FIT, NULL, "does not fit NUMBER(15,2)"},
        {"^ZXPAY(7,1)", "R^1^NGN^1.5^2", ZM_E_FIT, NULL, "not a whole number"},
        {"^ZXPAY(7,1)", "R^1^NGN^12345678901^2", ZM_E_FIT, NULL, "more than 10 digits"},
        {"^ZXPAY(7,1)", "R^1^NGN^1^2^2980014", ZM_E_FIT, NULL, "outside 0..2980013"},
        {"^ZXPAY(7,1)", "R^1^NGN^1^2^-1", ZM_E_FIT, NULL, "outside"},
        {"^ZXPAY(7,1)", "R^1^NGN^1^2^1.5", ZM_E_FIT, NULL, "not a whole number"},
        {"^ZXPAY(7,1)", "ABCDEFGHIJKLMNOPQ^1^NGN^1^2", ZM_E_FIT, NULL, "TEXT(16) holds 16 (never truncated)"},
        {"^ZXPAY(7,1)", "R^1^NGNX^1^2", ZM_E_FIT, NULL, "piece 3 (ccy)"},
        /* text */
        {"^ZXPAY(7,1)", "R\x01^1^NGN^1^2", ZM_E_DATA, NULL, "control character"},
        {"^ZXPAY(7,1)", "R\x7f^1^NGN^1^2", ZM_E_DATA, NULL, "control character"},
        {"^ZXPAY(7,1)", "R\xff^1^NGN^1^2", ZM_E_DATA, NULL, "UTF-8"},
        {"^ZXPAY(7,1)", "R\xc3^1^NGN^1^2", ZM_E_DATA, NULL, "UTF-8"},
        /* required and undeclared pieces */
        {"^ZXPAY(7,1)", "^1^NGN^1^2", ZM_E_DATA, NULL, "piece 1 (ref) is required"},
        {"^ZXPAY(7,1)", "R^1^NGN^1", ZM_E_DATA, NULL, "piece 5 (to) is required"},
        {"^ZXPAY(7,1)", "", ZM_E_DATA, NULL, "required"},
        {"^ZXPAY(7,1)", "R^1^NGN^1^2^^^x", ZM_E_DATA, NULL, "piece 8 (\"x\") is not declared"},
        {"^ZXPAY(7,1)", "R^1^NGN^1^2^^^^m^extra", ZM_E_DATA, NULL, "piece 10"},
        {"^ZXPAY(\"B\",\"PAY-1\",7,1)", "x", ZM_E_DATA, NULL, "piece 1"},
        {"^ZXPAY(7)", "", ZM_E_DATA, NULL, "opened"},
        /* references */
        {"^ZXPAY(7,1,2)", "R^1^NGN^1^2", ZM_E_KEY, NULL, "matches no node"},
        {"^ZXPAY", "", ZM_E_KEY, NULL, "matches no node"},
        {"^ZXPAX(7,1)", "", ZM_E_KEY, NULL, "the schema is for ^ZXPAY"},
        {"^zxpay(7,1)", "", ZM_E_KEY, NULL, "the schema is for ^ZXPAY"},
        {"^ZXPAY(7,\"X\")", "", ZM_E_KEY, NULL, "\"X\" is a string; key seq is a number"},
        {"^ZXPAY(7,1.5)", "", ZM_E_KEY, NULL, "not a whole number"},
        {"^ZXPAY(7,1234567890)", "", ZM_E_KEY, NULL, "more than 9 digits"},
        {"^ZXPAY(7,01)", "", ZM_E_KEY, NULL, "not a canonical number"},
        {"^ZXPAY(7,1E3)", "", ZM_E_KEY, NULL, "not a canonical number"},
        {"^ZXPAY(7,)", "", ZM_E_KEY, NULL, "is empty"},
        {"^ZXPAY(7,\"\")", "", ZM_E_KEY, NULL, "null subscripts"},
        {"^ZXPAY(7,1", "", ZM_E_KEY, NULL, "final"},
        {"^ZXPAY(7,1))", "", ZM_E_KEY, NULL, "final"},
        {"^ZXPAY(7,\"1)", "", ZM_E_KEY, NULL, "not closed"},
        {"^ZXPAY(\"B\",12,7,1)", "", ZM_E_KEY, NULL, "is a number; key ref is TEXT"},
        {"^ZXPAY(\"B\",\"12\",7,1)", "", ZM_E_KEY, NULL, "is a number"},
        {"^ZXPAY(\"C\",\"X\",7,1)", "", ZM_E_KEY, NULL, "subscript 1 is C; the node has \"B\""},
        {"^ZXPAY(\"B\",\"ABCDEFGHIJKLMNOPQ\",7,1)", "", ZM_E_KEY, NULL, "TEXT(16)"},
        {"ZXPAY(7,1)", "", ZM_E_KEY, NULL, "starts with \"^\""},
        {"^ZXPAY(7,\"a\x01\")", "", ZM_E_KEY, NULL, "control character"},
        {"^(7)", "", ZM_E_KEY, NULL, "not a global name"},
        /* a long name of control bytes: the message quotes it safely (a fuzz find) */
        {"^\x01\x02\x03\x04\x05\x06\x07\x08\x0b\x0c\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17"
         "\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f\x7f\x01\x02\x03\x04\x05\x06\x07\x08\x0b\x0c\x0e\x0f(1)",
         "", ZM_E_KEY, NULL, "\\x01\\x02"},
    };
    for (size_t i = 0; i < sizeof k / sizeof *k; i++) {
        int32_t st;
        zm_record *r = dec(k[i].ref, k[i].value, &st);
        CHECK(st == k[i].status, "case %zu %s = %s: %s, want %s", i, k[i].ref, k[i].value,
              zm_status_name(st), zm_status_name(k[i].status));
        if (st == ZXV_OK && r) {
            CHECK(strcmp(enc(r), k[i].canonical) == 0, "case %zu canonical %s", i, enc(r));
            /* the canonical string decodes to the same canonical string */
            {
                static char c[1 << 16];
                zm_record *r2;
                memcpy(c, enc(r), strlen(enc(r)) + 1u);
                r2 = dec(k[i].ref, c, &st);
                CHECK(st == ZXV_OK && r2 && strcmp(enc(r2), c) == 0, "case %zu re-decodes", i);
                zm_record_destroy(r2);
            }
        } else if (k[i].msg) {
            CHECK(strstr(last_err(), k[i].msg) != NULL, "case %zu message '%s' lacks '%s'", i,
                  last_err(), k[i].msg);
        }
        zm_record_destroy(r);
    }
}

static void fields(void) {
    int32_t st, isnull = -1;
    zm_record *r = dec("^ZXPAY(7,2)", "PAY-0001^250.25^NGN^1^2^67854^N^^synthetic memo", &st);
    int64_t v = 0, num = 0, den = 0;
    uint32_t sc = 99, node = 9;
    char b[256];
    size_t n = 0;
    CHECK(st == ZXV_OK && r, "decode");
    if (!r) return;
    CHECK(zm_record_node(r, &node) == ZXV_OK && node == 0, "node 0");
    CHECK(strcmp(val(r, "batch"), "7") == 0 && strcmp(val(r, "SEQ"), "2") == 0, "keys");
    CHECK(strcmp(val(r, "amount"), "250.25") == 0, "amount display");
    CHECK(strcmp(val(r, "value-date"), "2026-10-11") == 0, "date display");
    CHECK(strcmp(val(r, "memo"), "synthetic memo") == 0, "memo");
    CHECK(strcmp(val(r, "missing"), "<ZXV_E_NOT_FOUND>") == 0, "unknown field");
    CHECK(zm_get_decimal(r, "amount", &v, &sc) == ZXV_OK && v == 25025 && sc == 2, "decimal");
    CHECK(zm_get_rat(r, "amount", &num, &den) == ZXV_OK && num == 1001 && den == 4, "rat 1001/4");
    CHECK(zm_get_decimal(r, "value-date", &v, &sc) == ZXV_OK && v == 67854 && sc == 0, "$H");
    CHECK(zm_get_decimal(r, "ref", &v, &sc) == ZXV_E_ARG, "TEXT is not a number");
    CHECK(zm_get_mtext(r, "amount", b, sizeof b, &n) == ZXV_OK && strcmp(b, "250.25") == 0, "mtext");
    CHECK(zm_get_mtext(r, "value-date", b, sizeof b, &n) == ZXV_OK && strcmp(b, "67854") == 0, "date mtext");
    CHECK(zm_is_null(r, "status", &isnull) == ZXV_OK && isnull == 0, "status set");
    /* setters */
    CHECK(zm_put_value(r, "amount", "-0012.500", 9) == ZXV_OK && strcmp(val(r, "amount"), "-12.50") == 0,
          "put -0012.500");
    CHECK(zm_get_mtext(r, "amount", b, sizeof b, &n) == ZXV_OK && strcmp(b, "-12.5") == 0, "M text -12.5");
    CHECK(zm_put_value(r, "amount", "3/4", 3) == ZXV_OK && strcmp(val(r, "amount"), "0.75") == 0, "3/4");
    CHECK(zm_put_value(r, "amount", "1/3", 3) == ZM_E_FIT, "1/3 refused");
    CHECK(strcmp(val(r, "amount"), "0.75") == 0, "nothing written on refusal");
    CHECK(zm_put_value(r, "amount", "1.234", 5) == ZM_E_FIT, "1.234 refused");
    CHECK(zm_put_value(r, "amount", "1e3", 3) == ZXV_E_FORMAT, "1e3 is not decimal text");
    CHECK(zm_put_value(r, "amount", "10000000000000", 14) == ZM_E_FIT, "too many digits");
    CHECK(zm_put_value(r, "amount", "9999999999999.99", 16) == ZXV_OK, "the largest amount");
    CHECK(zm_put_decimal(r, "amount", 12345, 3) == ZM_E_FIT, "12.345");
    CHECK(zm_put_decimal(r, "amount", 12340, 3) == ZXV_OK && strcmp(val(r, "amount"), "12.34") == 0, "12.340");
    CHECK(zm_put_decimal(r, "amount", 5, 0) == ZXV_OK && strcmp(val(r, "amount"), "5.00") == 0, "5");
    CHECK(zm_put_decimal(r, "amount", INT64_MAX, 0) == ZM_E_FIT, "int64 max");
    CHECK(zm_put_decimal(r, "amount", 1, 19) == ZXV_E_ARG, "scale 19");
    CHECK(zm_put_rat(r, "amount", -1, 8) == ZM_E_FIT, "-1/8 needs 3 places");
    CHECK(zm_put_rat(r, "amount", 1, -4) == ZXV_OK && strcmp(val(r, "amount"), "-0.25") == 0, "1/-4");
    CHECK(zm_put_rat(r, "amount", 1, 0) != ZXV_OK, "1/0");
    CHECK(zm_put_value(r, "value-date", "2000-02-29", 10) == ZXV_OK &&
              zm_get_mtext(r, "value-date", b, sizeof b, &n) == ZXV_OK && strcmp(b, "58133") == 0,
          "2000-02-29 is 58133");
    CHECK(zm_put_value(r, "value-date", "1900-02-29", 10) == ZXV_E_FORMAT, "not a date");
    CHECK(zm_put_value(r, "value-date", "1840-12-30", 10) == ZM_E_FIT, "before day 0");
    CHECK(zm_put_value(r, "value-date", "2026-1-01", 9) == ZXV_E_FORMAT, "YYYY-MM-DD only");
    CHECK(zm_put_decimal(r, "value-date", 2980014, 0) == ZM_E_FIT, "after 9999-12-31");
    CHECK(zm_put_decimal(r, "value-date", 0, 0) == ZXV_OK && strcmp(val(r, "value-date"), "1840-12-31") == 0, "day 0");
    CHECK(zm_put_value(r, "memo", "a^b", 3) == ZM_E_DATA, "a ^ in text");
    CHECK(zm_put_value(r, "memo", "a\tb", 3) == ZM_E_DATA, "a tab in text");
    CHECK(zm_put_value(r, "ref", "ABCDEFGHIJKLMNOPQ", 17) == ZM_E_FIT, "17 bytes into TEXT(16)");
    CHECK(zm_put_value(r, "ref", "ABCDEFGHIJKLMNOP", 16) == ZXV_OK, "16 bytes fit");
    CHECK(zm_put_value(r, "status", "", 0) == ZXV_OK && zm_is_null(r, "status", &isnull) == ZXV_OK && isnull == 1,
          "empty text makes a piece null");
    CHECK(zm_get_decimal(r, "count", &v, &sc) == ZXV_E_NOT_FOUND, "count is in node 2");
    CHECK(zm_put_null(r, "memo") == ZXV_OK && strcmp(val(r, "memo"), "") == 0, "null memo");
    CHECK(zm_put_null(r, "batch") == ZXV_E_ARG, "a key cannot be null");
    CHECK(zm_put_value(r, "seq", "", 0) == ZXV_E_ARG, "nor empty");
    CHECK(zm_put_value(r, "seq", "12", 2) == ZXV_OK, "but it can change");
    CHECK(strcmp(enc(r), "ABCDEFGHIJKLMNOP^-.25^NGN^1^2^0") == 0, "encoded: %s", enc(r));
    CHECK(zm_record_ref(r, b, sizeof b, &n) == ZXV_OK && strcmp(b, "^ZXPAY(7,12)") == 0, "ref: %s", b);
    CHECK(zm_put_null(r, "amount") == ZXV_OK && strcmp(enc(r), "<ZM_E_DATA>") == 0, "required amount");
    CHECK(zm_get_decimal(r, "amount", &v, &sc) == ZXV_E_FIELD, "null amount");
    CHECK(zm_record_encode(r, NULL, 0, &n) == ZM_E_DATA, "encode checks first");
    zm_record_destroy(r);

    /* a fresh record: keys from a reference */
    {
        zm_record *x = NULL;
        CHECK(zm_record_create(g_s, 2, &x) == ZXV_OK, "create the index node");
        CHECK(zm_record_ref(x, b, sizeof b, &n) == ZXV_E_FIELD, "no keys yet");
        CHECK(zm_record_set_ref(x, "^ZXPAY(7,1)", 11) == ZM_E_KEY, "a node 1 reference");
        CHECK(zm_record_set_ref(x, "^ZXPAY(\"B\",\"say \"\"hi\"\"\",7,1)", 28) == ZXV_OK, "quotes");
        CHECK(strcmp(val(x, "ref"), "say \"hi\"") == 0, "unquoted: %s", val(x, "ref"));
        CHECK(zm_record_ref(x, b, sizeof b, &n) == ZXV_OK &&
                  strcmp(b, "^ZXPAY(\"B\",\"say \"\"hi\"\"\",7,1)") == 0,
              "requoted: %s", b);
        CHECK(zm_record_ref(x, b, 5, &n) == ZXV_E_BUFFER && n == strlen("^ZXPAY(\"B\",\"say \"\"hi\"\"\",7,1)"), "R4");
        CHECK(zm_put_value(x, "ref", "12", 2) == ZM_E_KEY, "a TEXT key cannot be a number");
        CHECK(zm_put_value(x, "ref", "1E3", 3) == ZXV_OK, "1E3 is a string");
        CHECK(zm_record_ref(x, b, sizeof b, &n) == ZXV_OK && strcmp(b, "^ZXPAY(\"B\",\"1E3\",7,1)") == 0, "%s", b);
        CHECK(zm_record_clear(x) == ZXV_OK && zm_record_ref(x, b, sizeof b, &n) == ZXV_E_FIELD, "clear");
        zm_record_destroy(x);
        CHECK(zm_record_create(g_s, 3, &x) == ZXV_E_NOT_FOUND, "no node 4");
        CHECK(zm_record_create(NULL, 0, &x) == ZXV_E_ARG, "NULL schema");
    }
    /* limits */
    {
        char *big = (char *) malloc(ZM_MAX_VALUE + 2u);
        zm_record *x = NULL;
        zm_record_create(g_s, 0, &x);
        memset(big, 'a', ZM_MAX_VALUE + 1u);
        CHECK(zm_record_decode(x, big, ZM_MAX_VALUE + 1u) == ZXV_E_TOO_LONG, "value too long");
        CHECK(zm_record_decode(x, NULL, 3) == ZXV_E_ARG, "NULL value");
        CHECK(zm_record_decode(NULL, "a", 1) == ZXV_E_ARG, "NULL record");
        memset(big, '^', 300);
        CHECK(zm_record_decode(x, big, 300) == ZM_E_DATA, "300 empty pieces: required ones missing");
        zm_record_destroy(x);
        free(big);
    }
    /* the canonical value of the stock price node */
    {
        zm_schema *s = myra("^ZXSTK(site:t8,item:i12,\"PRICE\",effective:d)=price:n12.2!^^ccy:t3!");
        zm_record *x = NULL;
        CHECK(s && zm_node_decode(s, "^ZXSTK(\"NORTH\",42,\"PRICE\",67854)", strlen("^ZXSTK(\"NORTH\",42,\"PRICE\",67854)"), "12.5^^USD", 9, &x) == ZXV_OK,
              "stock price");
        if (x) {
            CHECK(strcmp(val(x, "effective"), "2026-10-11") == 0, "a DATE key");
            CHECK(zm_put_value(x, "effective", "2027-01-01", 10) == ZXV_OK &&
                      zm_record_ref(x, b, sizeof b, &n) == ZXV_OK &&
                      strcmp(b, "^ZXSTK(\"NORTH\",42,\"PRICE\",67936)") == 0,
                  "new key: %s", b);
        }
        zm_record_destroy(x);
        zm_schema_destroy(s);
    }
}

/* Random schemas and values: put -> encode -> decode -> the same values
 * and the same canonical text. */
static void seeded_round_trip(void) {
    int bad = 0, total = 0;
    for (int it = 0; it < 400; it++) {
        char t[4096], ref[512], v1[8192], v2[8192];
        size_t k = 0, rk = 0, n1 = 0, n2 = 0;
        uint32_t nk = 1u + (uint32_t) (rnd() % 3u), np = 1u + (uint32_t) (rnd() % 8u);
        zm_schema *s;
        zm_record *r = NULL, *r2 = NULL;
        app(t, sizeof t, &k, "^ZXR(");
        app(ref, sizeof ref, &rk, "^ZXR(");
        for (uint32_t i = 0; i < nk; i++) {
            app(t, sizeof t, &k, "%sk%u:i%u", i ? "," : "", (unsigned) i, 9u);
            app(ref, sizeof ref, &rk, "%s%llu", i ? "," : "", (unsigned long long) (rnd() % 1000000000u));
        }
        app(t, sizeof t, &k, ")=");
        app(ref, sizeof ref, &rk, ")");
        for (uint32_t p = 0; p < np; p++) {
            uint32_t kind = (uint32_t) (rnd() % 4u), d = 1u + (uint32_t) (rnd() % 18u);
            if (p) app(t, sizeof t, &k, "^");
            if (rnd() % 5u == 0) continue; /* an unused piece */
            if (kind == 0) app(t, sizeof t, &k, "p%u:t%u", (unsigned) p, 1u + (unsigned) (rnd() % 40u));
            else if (kind == 1) app(t, sizeof t, &k, "p%u:i%u", (unsigned) p, (unsigned) d);
            else if (kind == 2) app(t, sizeof t, &k, "p%u:n%u.%u", (unsigned) p, (unsigned) d, 1u + (unsigned) (rnd() % d));
            else app(t, sizeof t, &k, "p%u:d", (unsigned) p);
        }
        s = myra(t);
        if (!s) {
            bad++;
            continue;
        }
        if (zm_node_decode(s, ref, rk, "", 0, &r) != ZXV_OK) {
            bad++;
            zm_schema_destroy(s);
            continue;
        }
        for (int rep = 0; rep < 25; rep++) {
            zm_node_info info;
            zm_node_info_at(s, 0, &info);
            for (uint32_t f = info.keys; f < info.keys + info.pieces; f++) {
                zm_field fd;
                zm_field_at(s, 0, f, &fd);
                if (rnd() % 6u == 0) {
                    zm_put_null(r, fd.name);
                } else if (fd.type == ZM_T_TEXT) {
                    char x[64];
                    uint32_t len = (uint32_t) (rnd() % (fd.len + 1u));
                    for (uint32_t c = 0; c < len; c++) x[c] = (char) (' ' + 1 + (int) (rnd() % 93u));
                    for (uint32_t c = 0; c < len; c++)
                        if (x[c] == '^') x[c] = '~';
                    total++;
                    if (zm_put_value(r, fd.name, x, len) != ZXV_OK) bad++;
                } else if (fd.type == ZM_T_DATE) {
                    total++;
                    if (zm_put_decimal(r, fd.name, (int64_t) (rnd() % (ZM_MAX_DATE + 1u)), 0) != ZXV_OK) bad++;
                } else {
                    int64_t m = zm_pow10[fd.len];
                    int64_t x = (int64_t) (rnd() % (uint64_t) m);
                    if (rnd() & 1u) x = -x;
                    total++;
                    if (zm_put_decimal(r, fd.name, x, fd.scale) != ZXV_OK) bad++;
                }
            }
            if (zm_record_encode(r, v1, sizeof v1, &n1) != ZXV_OK) {
                bad++;
                continue;
            }
            total++;
            if (zm_node_decode(s, ref, rk, v1, n1, &r2) != ZXV_OK ||
                zm_record_encode(r2, v2, sizeof v2, &n2) != ZXV_OK || n1 != n2 ||
                memcmp(v1, v2, n1) != 0) {
                bad++;
                fprintf(stderr, "round trip: %s %s = %s\n", t, ref, v1);
            } else {
                for (uint32_t f = 0; f < info.keys + info.pieces; f++) {
                    zm_field fd;
                    char a[128], b[128];
                    size_t na = 0, nb = 0;
                    zm_field_at(s, 0, f, &fd);
                    total++;
                    if (zm_get_value(r, fd.name, a, sizeof a, &na) != ZXV_OK ||
                        zm_get_value(r2, fd.name, b, sizeof b, &nb) != ZXV_OK || strcmp(a, b) != 0)
                        bad++;
                    /* every numeric piece is canonical in M */
                    if (fd.type != ZM_T_TEXT) {
                        int32_t isnull = 1;
                        zm_is_null(r2, fd.name, &isnull);
                        if (!isnull && (zm_get_mtext(r2, fd.name, a, sizeof a, &na) != ZXV_OK ||
                                        !zm_is_canonic(a, na)))
                            bad++;
                    }
                }
            }
            zm_record_destroy(r2);
            r2 = NULL;
        }
        zm_record_destroy(r);
        zm_schema_destroy(s);
    }
    CHECK(bad == 0, "seeded round trip: %d of %d steps failed", bad, total);
    g_checks += total;
}

int main(void) {
    g_s = myra(k_pay_myra);
    CHECK(g_s != NULL, "payment schema");
    if (!g_s) return report("test_record");
    decode_vectors();
    fields();
    seeded_round_trip();
    zm_schema_destroy(g_s);
    return report("test_record");
}
