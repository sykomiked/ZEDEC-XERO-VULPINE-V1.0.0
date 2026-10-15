/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_idle.c — zxv_idle.h I1-I6 and zxv_receipt.h E1-E4 (R24).
 *   sh kernel/arch/hosted/test_hosted.sh runs it; argv[1] is a scratch dir. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"
#include "zxv_idle.h"
#include "zxv_receipt.h"

static int failures = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        int ok_ = (c); /* once: several checks have side effects */                                \
        if (!ok_) failures++;                                                                      \
        printf(ok_ ? "  ok   " : "  FAIL ");                                                       \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

static char DIR_[900];

static void path(char *out, size_t cap, const char *name)
{
    snprintf(out, cap, "%s/%s", DIR_, name);
}

/* a file of n bytes (a fixed pattern) and its SHA-256 as hex */
static void make_file(const char *p, size_t n, char hexout[65])
{
    FILE *f = fopen(p, "wb");
    sha256_ctx_t c;
    sha256_init(&c);
    for (size_t i = 0; i < n; i++) {
        uint8_t b = (uint8_t) (i * 131u + 7u);
        fputc(b, f);
        sha256_update(&c, &b, 1);
    }
    fclose(f);
    uint8_t h[32];
    sha256_final(&c, h);
    for (int i = 0; i < 32; i++) snprintf(hexout + 2 * i, 3, "%02x", h[i]);
}

static int run_to_end(zxv_idle_t *q, zxv_idle_outcome_t *o, uint32_t *slices)
{
    int r;
    *slices = 0;
    while ((r = zxv_idle_slice(q, o)) == 0) (*slices)++;
    (*slices)++;
    return r;
}

static int fake_sign(void *ctx, const uint8_t hash[32], char *sig, size_t cap, char signer[65])
{
    (void) ctx;
    if (cap < 70) return 0;
    for (int i = 0; i < 32; i++) snprintf(sig + 2 * i, 3, "%02x", (uint8_t) (hash[i] ^ 0x5a));
    snprintf(signer, 65, "%064d", 7);
    return 1;
}

static int fail_sign(void *ctx, const uint8_t hash[32], char *sig, size_t cap, char signer[65])
{
    (void) ctx, (void) hash, (void) sig, (void) cap, (void) signer;
    return 0;
}

static void test_idle(void)
{
    printf("I1 queue\n");
    static zxv_idle_t q;
    zxv_idle_init(&q);
    CHECK(zxv_idle_cost(&q) == 0 && zxv_idle_slice(&q, NULL) == -1, "empty: no cost, no slice");
    char h[65], p[1000], d[64];
    path(p, sizeof p, "pin_a");
    make_file(p, 2u * ZXV_IDLE_CHUNK + 1000u, h);
    CHECK(zxv_idle_add_pin(&q, p, "zz", 0) == -1, "a pin needs 64 hex digits");
    CHECK(zxv_idle_add_pin(
              &q, p, "g000000000000000000000000000000000000000000000000000000000000000", 0) == -1,
          "a pin needs hex digits");
    uint32_t added = 0;
    while (zxv_idle_add_selftest(&q, added) == 0) added++;
    CHECK(added == ZXV_IDLE_CAP && zxv_idle_queued(&q) == ZXV_IDLE_CAP,
          "a full ring refuses the next task (%u queued)", (unsigned) added);
    zxv_idle_clear(&q);
    CHECK(zxv_idle_queued(&q) == 0, "clear empties the ring");

    printf("I2-I5 slices and results\n");
    zxv_idle_outcome_t o;
    uint32_t n;
    CHECK(zxv_idle_add_pin(&q, p, h, 3) == 0 && zxv_idle_cost(&q) == 1, "one token per slice");
    CHECK(zxv_idle_slice(&q, &o) == 0, "a big file is not hashed in one slice");
    zxv_idle_describe(&q, d, sizeof d);
    CHECK(!strcmp(d, "pin 3 at 1048576 bytes"), "progress is kept between slices: %s", d);
    CHECK(run_to_end(&q, &o, &n) == 1 && o.result == ZXV_IDLE_PASS && o.tag == 3 && n == 2,
          "the right hash passes after 3 slices in all: %s", o.why);
    char bad[65];
    memcpy(bad, h, 65);
    bad[10] = bad[10] == '0' ? '1' : '0';
    zxv_idle_add_pin(&q, p, bad, 4);
    CHECK(run_to_end(&q, &o, &n) == 1 && o.result == ZXV_IDLE_FAIL && strstr(o.why, "differs"),
          "a changed byte of the pin fails: %s", o.why);
    path(p, sizeof p, "no_such_file");
    zxv_idle_add_pin(&q, p, h, 5);
    CHECK(zxv_idle_slice(&q, &o) == 1 && o.result == ZXV_IDLE_FAIL && strstr(o.why, "opened"),
          "a missing file fails at once");
    path(p, sizeof p, "pin_exact");
    make_file(p, ZXV_IDLE_CHUNK, h);
    zxv_idle_add_pin(&q, p, h, 6);
    CHECK(run_to_end(&q, &o, &n) == 1 && o.result == ZXV_IDLE_PASS && n == 2,
          "a file of exactly one chunk ends on the next slice");
    path(p, sizeof p, "pin_empty");
    make_file(p, 0, h);
    zxv_idle_add_pin(&q, p, h, 7);
    CHECK(zxv_idle_slice(&q, &o) == 1 && o.result == ZXV_IDLE_PASS, "an empty file hashes too");
    zxv_idle_add_selftest(&q, 9);
    CHECK(zxv_idle_slice(&q, &o) == 1 && o.result == ZXV_IDLE_PASS && o.kind == ZXV_IDLE_SELFTEST,
          "the self-test passes in one slice: %s", o.why);
    CHECK(q.finished == 6 && q.failed == 2 && zxv_idle_queued(&q) == 0,
          "counts: %llu finished, %llu failed", (unsigned long long) q.finished,
          (unsigned long long) q.failed);

    printf("I3 a request between slices\n");
    path(p, sizeof p, "pin_a");
    make_file(p, 2u * ZXV_IDLE_CHUNK + 1000u, h);
    zxv_idle_add_pin(&q, p, h, 0);
    zxv_idle_slice(&q, NULL);
    /* the host serves a person here; the task's state is untouched */
    zxv_idle_describe(&q, d, sizeof d);
    CHECK(!strcmp(d, "pin 0 at 1048576 bytes"), "paused mid-file: %s", d);
    CHECK(run_to_end(&q, &o, &n) == 1 && o.result == ZXV_IDLE_PASS,
          "and it resumes to the same result");
    zxv_idle_clear(&q);
}

static void test_receipts(void)
{
    printf("E1-E2 sealing\n");
    char p[1000], err[200];
    path(p, sizeof p, "receipts.jsonl");
    remove(p);
    zxv_receipt_chain_t ch;
    zxv_receipt_init(&ch);
    FILE *f = fopen(p, "ab");
    zxv_receipt_t r = {"ask", "What is \"17\" times 23?", "391", "Spinewyrm", "", "TRUE", 4,
                       37,    "pin 0 at 1048576 bytes"};
    uint8_t h1[32], h2[32];
    CHECK(zxv_receipt_seal(&ch, &r, NULL, NULL, f, h1) == 0 && ch.seq == 1, "first receipt");
    r.question = "Who wrote Hamlet?";
    r.shown = "Shakespeare";
    r.paused = "none";
    CHECK(zxv_receipt_seal(&ch, &r, fake_sign, NULL, f, h2) == 0 && ch.seq == 2 &&
              !memcmp(ch.prev, h2, 32),
          "second receipt, signed");
    CHECK(zxv_receipt_seal(&ch, &r, fail_sign, NULL, f, NULL) == -1 && ch.seq == 2,
          "a failed signature writes nothing and keeps the chain");
    fclose(f);
    FILE *in = fopen(p, "rb");
    char line[8192];
    CHECK(fgets(line, sizeof line, in) && !strstr(line, "What is") &&
              strstr(line, "\"seq\":1,\"prev\":\"0000"),
          "a receipt holds hashes of the texts, not the texts");
    CHECK(fgets(line, sizeof line, in) && strstr(line, "\"sig\":\"") && strstr(line, "\"signer\":"),
          "a signed receipt carries sig and signer");
    CHECK(!fgets(line, sizeof line, in), "exactly two lines");
    fclose(in);

    printf("E4 replay\n");
    uint64_t n = 0;
    zxv_receipt_chain_t back;
    CHECK(zxv_receipt_verify(p, &n, &back, err, sizeof err) == 0 && n == 2 &&
              !memcmp(back.prev, h2, 32),
          "the file verifies: %llu receipts", (unsigned long long) n);
    CHECK(zxv_receipt_resume(p, &back, err, sizeof err) == 0 && back.seq == 2,
          "a restart resumes the chain at 2");
    f = fopen(p, "ab");
    CHECK(zxv_receipt_seal(&back, &r, NULL, NULL, f, NULL) == 0 && back.seq == 3, "and goes on");
    fclose(f);
    CHECK(zxv_receipt_verify(p, &n, NULL, err, sizeof err) == 0 && n == 3, "three verify");

    /* tamper: change one character of the first receipt's verdict */
    in = fopen(p, "rb");
    char all[40000];
    size_t len = fread(all, 1, sizeof all - 1, in);
    fclose(in);
    all[len] = 0;
    char *v = strstr(all, "\"TRUE\"");
    v[1] = 'F';
    FILE *t = fopen(p, "wb");
    fwrite(all, 1, len, t);
    fclose(t);
    CHECK(zxv_receipt_verify(p, &n, NULL, err, sizeof err) == -1 && strstr(err, "receipt 1") &&
              strstr(err, "does not match"),
          "a changed receipt is caught: %s", err);
    CHECK(zxv_receipt_resume(p, &back, err, sizeof err) == -1, "and a restart refuses that file");
    /* drop the first line: the chain breaks at the new first receipt */
    v[1] = 'T';
    char *second = strchr(all, '\n') + 1;
    t = fopen(p, "wb");
    fwrite(second, 1, strlen(second), t);
    fclose(t);
    CHECK(zxv_receipt_verify(p, &n, NULL, err, sizeof err) == -1 && strstr(err, "receipt 1"),
          "a removed receipt is caught: %s", err);
    /* splice: another chain's receipt 2 in place of this chain's receipt 2;
     * its seq and its own hash are right, only prev gives it away */
    char q2[1000];
    path(q2, sizeof q2, "other.jsonl");
    remove(q2);
    zxv_receipt_chain_t oc;
    zxv_receipt_init(&oc);
    f = fopen(q2, "ab");
    zxv_receipt_t o1 = {"ask", "other", "other", "Kindlewyrm", "", "NEITHER", 1, 1, "none"};
    zxv_receipt_seal(&oc, &o1, NULL, NULL, f, NULL);
    zxv_receipt_seal(&oc, &r, NULL, NULL, f, NULL);
    fclose(f);
    in = fopen(q2, "rb");
    char other[8192], other2[8192];
    CHECK(fgets(other, sizeof other, in) && fgets(other2, sizeof other2, in), "another chain");
    fclose(in);
    char *end_of_first = strchr(all, '\n') + 1;
    t = fopen(p, "wb");
    fwrite(all, 1, (size_t) (end_of_first - all), t);
    fputs(other2, t);
    fclose(t);
    CHECK(zxv_receipt_verify(p, &n, NULL, err, sizeof err) == -1 && strstr(err, "receipt 2") &&
              strstr(err, "prev"),
          "a receipt spliced in from another chain is caught: %s", err);
    path(p, sizeof p, "none.jsonl");
    CHECK(zxv_receipt_verify(p, &n, NULL, err, sizeof err) == 0 && n == 0, "no file: 0 receipts");
}

int main(int argc, char **argv)
{
    snprintf(DIR_, sizeof DIR_, "%s", argc > 1 ? argv[1] : ".");
    test_idle();
    test_receipts();
    if (failures) {
        printf("FAILED: %d\n", failures);
        return 1;
    }
    printf("\nall passed\n");
    return 0;
}
