/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_hexsink.c — the ready-made quarantine sink (kit API 0.2): contract
 * violations written as Forbidden records into a libzxv ABI 1.3
 * zxv_hexlog, the ATTEST capability, refusal paths and fail-closed. */
#include "zc_test.h"

static zxv_ctx *ctx;
static zxv_cap *root, *attest, *noattest;

static void setup(void) {
    CHECK(zxv_ctx_create(&ctx) == ZXV_OK && zxv_cap_create(ctx, 7, NULL, ZXV_CAP_ALL, &root) == ZXV_OK &&
              zxv_cap_attenuate(root, ZXV_CAP_ATTEST, &attest) == ZXV_OK &&
              zxv_cap_attenuate(root, ZXV_CAP_LEDGER_READ, &noattest) == ZXV_OK,
          "context and caps");
}

static void teardown(void) {
    zxv_cap_destroy(noattest);
    zxv_cap_destroy(attest);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
}

static int32_t tag_of(const char *text) {
    int32_t t = -1;
    (void) zc_tag_parse(text, &t);
    return t;
}

static void test_create(void) {
    zxv_hexlog *q = NULL;
    zc_hexsink *s = NULL;
    CHECK(zxv_hexlog_create(attest, 8, &q) == ZXV_OK, "hexlog");
    CHECK(zc_hexsink_create(noattest, q, 0, &s) == ZXV_E_CAP && s == NULL, "a cap without ATTEST is refused");
    CHECK(zc_hexsink_create(NULL, q, 0, &s) == ZXV_E_ARG && zc_hexsink_create(attest, NULL, 0, &s) == ZXV_E_ARG &&
              zc_hexsink_create(attest, q, 0, NULL) == ZXV_E_ARG && zc_hexsink_create(attest, q, 0x2u, &s) == ZXV_E_ARG,
          "NULL arguments and unknown flags");
    CHECK(zc_hexsink_create(attest, q, 0, &s) == ZXV_OK && s != NULL, "create with ATTEST");
    CHECK(zc_evlog_attach_hexsink(NULL, s) == ZXV_E_ARG && zc_hexsink_halt_on_full(NULL, ctx) == ZXV_E_ARG &&
              zc_hexsink_stats(NULL, NULL, NULL, NULL) == ZXV_E_ARG, "NULL handles");
    uint64_t w = 9, k = 9;
    int32_t last = 9;
    CHECK(zc_hexsink_stats(s, &w, &k, &last) == ZXV_OK && w == 0 && k == 0 && last == ZXV_OK, "fresh stats");
    zc_hexsink_destroy(s);
    zc_hexsink_destroy(NULL);
    zxv_hexlog_destroy(q);
}

static void test_records(void) {
    zxv_hexlog *q = NULL;
    zc_hexsink *s = NULL;
    zc_evlog *log = NULL;
    uint8_t cd[32], sd[32], h1[32], h2[32], hh[32], subj[32];
    uint64_t seq = 0, hseq = 9;
    uint32_t count = 0, submitter = 0;
    uint8_t htag = 0, hreason = 0;
    memset(cd, 0xC1, 32);
    memset(sd, 0x5D, 32);
    CHECK(zxv_hexlog_create(attest, 16, &q) == ZXV_OK && zc_hexsink_create(attest, q, 0, &s) == ZXV_OK &&
              zc_evlog_create(16, &log) == ZXV_OK && zc_evlog_attach_hexsink(log, s) == ZXV_OK,
          "evidence log with the quarantine sink");

    /* an input refusal: false/neutral/potential -> false/forbidden/potential */
    CHECK(zc_evlog_append(log, ZC_ORIGIN_WASM, ZC_PART_PARAMS, 0, ZC_V_BELOW, tag_of("false/neutral/potential"), cd,
                          sd, "fee: below", &seq) == ZXV_OK && seq == 1,
          "input violation appended");
    CHECK(zxv_hexlog_count(q, &count) == ZXV_OK && count == 1, "one quarantine record");
    CHECK(zc_evlog_get(log, 1, NULL, NULL, NULL, NULL, NULL, NULL, NULL, h1, (char[64]){0}, 64, NULL) == ZXV_OK,
          "evidence hash");
    CHECK(zxv_hexlog_at(q, 0, &hseq, &htag, &hreason, &submitter, subj, NULL, NULL) == ZXV_OK && hseq == 0 &&
              htag == tag_of("false/forbidden/potential") && hreason == ZC_V_BELOW && submitter == ZC_ORIGIN_WASM &&
              memcmp(subj, h1, 32) == 0,
          "record: forbidden tag (base and phase kept), reason, submitter = origin, subject = evidence hash "
          "(tag %u = %s)", (unsigned) htag, zxv_hex_name(htag));

    /* an output violation keeps its tag */
    CHECK(zc_evlog_append(log, ZC_ORIGIN_ADAPTER, ZC_PART_RESULT, 0, ZC_V_ABOVE, tag_of("false/forbidden/manifest"),
                          cd, sd, "result above", &seq) == ZXV_OK && seq == 2,
          "output violation appended");
    CHECK(zc_evlog_get(log, 2, NULL, NULL, NULL, NULL, NULL, NULL, NULL, h2, (char[64]){0}, 64, NULL) == ZXV_OK &&
              zxv_hexlog_at(q, 1, NULL, &htag, &hreason, &submitter, subj, NULL, NULL) == ZXV_OK &&
              htag == tag_of("false/forbidden/manifest") && hreason == ZC_V_ABOVE &&
              submitter == ZC_ORIGIN_ADAPTER && memcmp(subj, h2, 32) == 0,
          "second record");
    {
        int32_t act = -1;
        CHECK(zxv_hex_action(htag, &act) == ZXV_OK && act == ZXV_HEX_ACT_ROLLBACK, "quarantined tags mean rollback");
    }
    /* a reason outside 0..254 */
    CHECK(zc_evlog_append(log, ZC_ORIGIN_HOST, ZC_PART_PARAMS, 0, 4000, tag_of("true/neutral/emerging"), NULL, NULL,
                          "host reason 4000", &seq) == ZXV_OK &&
              zxv_hexlog_at(q, 2, NULL, &htag, &hreason, NULL, NULL, NULL, NULL) == ZXV_OK &&
              hreason == ZC_HEXSINK_REASON_OTHER && htag == tag_of("true/forbidden/emerging"),
          "reason 4000 stored as 255; base kept");
    CHECK(zc_evlog_append(log, ZC_ORIGIN_HOST, ZC_PART_PARAMS, 0, -3, tag_of("both/negentropic/dissolving"), NULL,
                          NULL, "negative", &seq) == ZXV_OK &&
              zxv_hexlog_at(q, 3, NULL, &htag, &hreason, NULL, NULL, NULL, NULL) == ZXV_OK &&
              hreason == ZC_HEXSINK_REASON_OTHER && htag == tag_of("both/forbidden/dissolving"),
          "negative reason stored as 255");
    /* a tag that is not a tag: the sink refuses, so the evidence log does too */
    uint64_t before = 0, after = 0;
    zc_evlog_count(log, &before);
    CHECK(zc_evlog_append(log, ZC_ORIGIN_HOST, 0, 0, 1, 64, NULL, NULL, "bad tag", &seq) == ZC_E_LOG &&
              zc_evlog_append(log, ZC_ORIGIN_HOST, 0, 0, 1, -1, NULL, NULL, "bad tag", &seq) == ZC_E_LOG,
          "tags 64 and -1 are refused");
    zc_evlog_count(log, &after);
    CHECK(before == after && zxv_hexlog_count(q, &count) == ZXV_OK && count == 4, "nothing recorded for them");
    uint64_t w = 0, k = 0;
    int32_t last = 0;
    CHECK(zc_hexsink_stats(s, &w, &k, &last) == ZXV_OK && w == 4 && k == 0 && last == ZXV_E_ARG, "stats");
    CHECK(zxv_hexlog_verify(q) == ZXV_OK && zc_evlog_verify(log) == ZXV_OK, "both chains verify");
    CHECK(zxv_hexlog_head(q, hh) == ZXV_OK, "head");

    zc_evlog_destroy(log);
    zc_hexsink_destroy(s);
    zxv_hexlog_destroy(q);
}

static void test_forbidden_only(void) {
    zxv_hexlog *q = NULL;
    zc_hexsink *s = NULL;
    zc_evlog *log = NULL;
    uint32_t count = 9;
    uint64_t seq = 0, w = 0, k = 0;
    CHECK(zxv_hexlog_create(attest, 4, &q) == ZXV_OK &&
              zc_hexsink_create(attest, q, ZC_HEXSINK_FORBIDDEN_ONLY, &s) == ZXV_OK && zc_evlog_create(8, &log) == ZXV_OK &&
              zc_evlog_attach_hexsink(log, s) == ZXV_OK,
          "FORBIDDEN_ONLY sink");
    CHECK(zc_evlog_append(log, ZC_ORIGIN_WASM, 1, 0, ZC_V_BELOW, tag_of("false/neutral/potential"), NULL, NULL, "in",
                          &seq) == ZXV_OK &&
              zxv_hexlog_count(q, &count) == ZXV_OK && count == 0,
          "an input refusal is skipped (only in the evidence log)");
    CHECK(zc_evlog_append(log, ZC_ORIGIN_WASM, 2, 0, ZC_V_ABOVE, tag_of("false/forbidden/manifest"), NULL, NULL, "out",
                          &seq) == ZXV_OK &&
              zxv_hexlog_count(q, &count) == ZXV_OK && count == 1,
          "an output violation is quarantined");
    CHECK(zc_hexsink_stats(s, &w, &k, NULL) == ZXV_OK && w == 1 && k == 1, "1 written, 1 skipped");
    zc_evlog_destroy(log);
    zc_hexsink_destroy(s);
    zxv_hexlog_destroy(q);
}

static void test_fail_closed(void) {
    zxv_hexlog *q = NULL;
    zc_hexsink *s = NULL;
    zc_evlog *log = NULL;
    uint64_t seq = 0, n = 0;
    int32_t last = 0, poisoned = 0;
    int32_t ft = tag_of("false/forbidden/manifest");
    CHECK(zxv_hexlog_create(attest, 2, &q) == ZXV_OK && zc_hexsink_create(attest, q, 0, &s) == ZXV_OK &&
              zc_evlog_create(8, &log) == ZXV_OK && zc_evlog_attach_hexsink(log, s) == ZXV_OK,
          "a quarantine log of 2");
    CHECK(zc_evlog_append(log, 1, 2, 0, 6, ft, NULL, NULL, "a", &seq) == ZXV_OK &&
              zc_evlog_append(log, 1, 2, 0, 6, ft, NULL, NULL, "b", &seq) == ZXV_OK,
          "two records fit");
    CHECK(zc_evlog_append(log, 1, 2, 0, 6, ft, NULL, NULL, "c", &seq) == ZC_E_LOG, "the third is refused");
    CHECK(zc_evlog_count(log, &n) == ZXV_OK && n == 2 && zc_hexsink_stats(s, NULL, NULL, &last) == ZXV_OK &&
              last == ZXV_E_CAPACITY,
          "the evidence log did not record it either; last status CAPACITY");
    CHECK(zxv_ctx_is_poisoned(ctx, &poisoned) == ZXV_OK && poisoned == 0, "no halt without halt_on_full");
    zc_evlog_destroy(log);
    zc_hexsink_destroy(s);
    zxv_hexlog_destroy(q);

    /* a revoked cap: libzxv refuses the append */
    zxv_cap *child = NULL;
    CHECK(zxv_cap_attenuate(root, ZXV_CAP_ATTEST, &child) == ZXV_OK && zxv_hexlog_create(child, 4, &q) == ZXV_OK &&
              zc_hexsink_create(child, q, 0, &s) == ZXV_OK && zc_evlog_create(8, &log) == ZXV_OK &&
              zc_evlog_attach_hexsink(log, s) == ZXV_OK,
          "sink on a child cap");
    CHECK(zc_evlog_append(log, 1, 2, 0, 6, ft, NULL, NULL, "ok", &seq) == ZXV_OK, "works before revoke");
    CHECK(zxv_cap_revoke(child) == ZXV_OK && zc_evlog_append(log, 1, 2, 0, 6, ft, NULL, NULL, "x", &seq) == ZC_E_LOG &&
              zc_hexsink_stats(s, NULL, NULL, &last) == ZXV_OK && last == ZXV_E_CAP,
          "after revoke the record is refused (ZXV_E_CAP)");
    zc_evlog_destroy(log);
    zc_hexsink_destroy(s);
    zxv_hexlog_destroy(q);
    zxv_cap_destroy(child);

    /* a cap of another context */
    zxv_ctx *other = NULL;
    zxv_cap *oroot = NULL;
    CHECK(zxv_ctx_create(&other) == ZXV_OK && zxv_cap_create(other, 1, NULL, ZXV_CAP_ATTEST, &oroot) == ZXV_OK &&
              zxv_hexlog_create(attest, 4, &q) == ZXV_OK && zc_hexsink_create(oroot, q, 0, &s) == ZXV_OK &&
              zc_evlog_create(8, &log) == ZXV_OK && zc_evlog_attach_hexsink(log, s) == ZXV_OK,
          "a sink whose cap belongs to another context");
    CHECK(zc_evlog_append(log, 1, 2, 0, 6, ft, NULL, NULL, "x", &seq) == ZC_E_LOG &&
              zc_hexsink_stats(s, NULL, NULL, &last) == ZXV_OK && last == ZXV_E_CAP,
          "libzxv refuses it (ZXV_E_CAP)");
    zc_evlog_destroy(log);
    zc_hexsink_destroy(s);
    zxv_hexlog_destroy(q);
    zxv_cap_destroy(oroot);
    zxv_ctx_destroy(other);

    /* halt_on_full: the context is halted, and stays refused */
    zxv_ctx *hc = NULL;
    zxv_cap *hroot = NULL;
    CHECK(zxv_ctx_create(&hc) == ZXV_OK && zxv_cap_create(hc, 1, NULL, ZXV_CAP_ATTEST, &hroot) == ZXV_OK &&
              zxv_hexlog_create(hroot, 1, &q) == ZXV_OK && zc_hexsink_create(hroot, q, 0, &s) == ZXV_OK &&
              zc_hexsink_halt_on_full(s, hc) == ZXV_OK && zc_evlog_create(8, &log) == ZXV_OK &&
              zc_evlog_attach_hexsink(log, s) == ZXV_OK,
          "halt_on_full sink");
    CHECK(zc_evlog_append(log, 1, 2, 0, 6, ft, NULL, NULL, "a", &seq) == ZXV_OK &&
              zc_evlog_append(log, 1, 2, 0, 6, ft, NULL, NULL, "b", &seq) == ZC_E_LOG,
          "full");
    CHECK(zxv_ctx_is_poisoned(hc, &poisoned) == ZXV_OK && poisoned == 1, "the context was halted");
    CHECK(zc_evlog_append(log, 1, 2, 0, 6, ft, NULL, NULL, "c", &seq) == ZC_E_LOG &&
              zc_hexsink_stats(s, NULL, NULL, &last) == ZXV_OK && last == ZXV_E_POISONED,
          "every later record is refused (ZXV_E_POISONED)");
    zc_evlog_destroy(log);
    zc_hexsink_destroy(s);
    zxv_hexlog_destroy(q);
    zxv_cap_destroy(hroot);
    zxv_ctx_destroy(hc);
}

/* A host that chains the ready-made sink behind its own. */
typedef struct {
    zc_hexsink *hs;
    int calls;
} chain;

static int32_t chained(void *user, uint64_t seq, uint32_t origin, int32_t tag, int32_t reason,
                       const uint8_t subject[32], const uint8_t hash[32], const char *text) {
    chain *c = (chain *) user;
    c->calls++;
    return zc_hexsink_record(c->hs, seq, origin, tag, reason, subject, hash, text);
}

static void test_chain(void) {
    zxv_hexlog *q = NULL;
    zc_evlog *log = NULL;
    chain c = {NULL, 0};
    uint64_t seq = 0;
    uint32_t count = 0;
    CHECK(zxv_hexlog_create(attest, 4, &q) == ZXV_OK && zc_hexsink_create(attest, q, 0, &c.hs) == ZXV_OK &&
              zc_evlog_create(4, &log) == ZXV_OK && zc_evlog_set_sink(log, chained, &c) == ZXV_OK,
          "chained sink");
    CHECK(zc_evlog_append(log, 0, 1, 0, 2, tag_of("false/neutral/potential"), NULL, NULL, "x", &seq) == ZXV_OK &&
              c.calls == 1 && zxv_hexlog_count(q, &count) == ZXV_OK && count == 1,
          "the host's sink ran and the record was quarantined");
    CHECK(zc_hexsink_record(NULL, 1, 0, 0, 0, NULL, (const uint8_t[32]){0}, "") == ZXV_E_ARG &&
              zc_hexsink_record(c.hs, 1, 0, 0, 0, NULL, NULL, "") == ZXV_E_ARG,
          "zc_hexsink_record NULL handling");
    zc_evlog_destroy(log);
    zc_hexsink_destroy(c.hs);
    zxv_hexlog_destroy(q);
}

int main(void) {
    printf("test_hexsink: contract violations into the libzxv quarantine log\n");
    setup();
    test_create();
    test_records();
    test_forbidden_only();
    test_fail_closed();
    test_chain();
    teardown();
    printf("test_hexsink: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
