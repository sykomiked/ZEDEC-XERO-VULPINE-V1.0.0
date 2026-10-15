/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_events.c — read a ZXV serial log and check the chiglet governor's
 * event log (kernel/src/chiglet/chg_abi.h A8).
 *
 *   zxv_events [log]      (standard input when no file is named)
 *
 * The kernel prints each journal record as
 *   [EVT] <seq> <record bytes, hex> <head after it, hex>
 * and, on the console command `events`,
 *   [EVT] head <records> <head, hex>
 *   [EVT] key <ML-DSA-65 public key, hex>
 *   [EVT] sig <ML-DSA-65 signature, hex>
 * signed over records LE8 || head with context "zxv/chga/head/v1".
 *
 * The viewer recomputes every head itself, SHA-256("zxv/chga/event/v1" ||
 * previous head || record bytes) from 32 zero bytes, with its own decoder
 * (not the kernel's encoder), so a changed, removed, added or reordered
 * record breaks the chain at that line. A line "[BOOT] Chiglet governor"
 * starts a new boot and a new chain. Each record is shown in words.
 *
 * Exit 0: every chain whole and every signature verified; 1: a check
 * failed (the first failure is named); 2: the log could not be read, or it
 * holds no event at all.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "chg_abi.h"
#include "pq_security.h"
#include "sha256.h"

#define LINE_MAX_BYTES (2u * PQ_MLDSA65_SIG_BYTES + 64u)

static const char *const OUTCOME[] = {"ran", "failed", "refused", "held", "dropped"};
static const char *const VERDICT[] = {"neither", "true", "false", "both"};

static int hexval(char h)
{
    if (h >= '0' && h <= '9') return h - '0';
    if (h >= 'a' && h <= 'f') return h - 'a' + 10;
    return -1;
}

/* exactly n bytes of lowercase hex, ending at a space, CR, LF or NUL */
static bool unhex(const char *s, uint8_t *out, size_t n, const char **end)
{
    for (size_t i = 0; i < n; i++) {
        int hi = hexval(s[2 * i]), lo = hi < 0 ? -1 : hexval(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t) (hi * 16 + lo);
    }
    s += 2 * n;
    if (*s != ' ' && *s != '\r' && *s != '\n' && *s != 0) return false;
    *end = s;
    return true;
}

static bool udec(const char *s, uint64_t *v, const char **end)
{
    uint64_t n = 0;
    const char *p = s;
    while (*p >= '0' && *p <= '9') {
        uint64_t d = (uint64_t) (*p - '0');
        if (n > (UINT64_MAX - d) / 10u) return false;
        n = n * 10u + d;
        p++;
    }
    if (p == s || *p != ' ') return false;
    *v = n;
    *end = p + 1;
    return true;
}

static bool line_end(const char *s)
{
    while (*s == '\r' || *s == '\n') s++;
    return *s == 0;
}

static uint64_t le(const uint8_t *b, uint32_t n)
{
    uint64_t v = 0;
    for (uint32_t k = 0; k < n; k++) v |= (uint64_t) b[k] << (8 * k);
    return v;
}

typedef struct {
    uint8_t head[CHGA_DIGEST]; /* the chain so far in this boot */
    uint64_t records;          /* records in this boot */
    uint64_t total;            /* records in the whole log */
    uint32_t boot;             /* boots seen (a governor line starts one) */
    bool have_sign;            /* a head line is waiting for its key and sig */
    uint64_t sign_n;
    uint8_t sign_head[CHGA_DIGEST];
    bool have_key;
    uint8_t key[PQ_MLDSA65_PK_BYTES];
    uint32_t signatures;
} state_t;

static int fail(unsigned long line, const char *why)
{
    printf("FAIL line %lu: %s\n", line, why);
    return 1;
}

/* one record: decode, check its fields, fold it, compare the head */
static int record(state_t *st, unsigned long ln, const char *p)
{
    uint64_t seq;
    uint8_t b[CHGA_REC_BYTES], head[CHGA_DIGEST], want[CHGA_DIGEST];
    const char *q;
    if (!udec(p, &seq, &q) || !unhex(q, b, sizeof b, &q) || *q != ' ' ||
        !unhex(q + 1, head, sizeof head, &q) || !line_end(q))
        return fail(ln, "not an event line: [EVT] <seq> <record hex> <head hex>");
    uint8_t op = b[8], from = b[9], outcome = b[10], verdict = b[11], nargs = b[16];
    int32_t rc = (int32_t) (uint32_t) le(b + 12, 4);
    if (le(b, 8) != seq) return fail(ln, "the record's sequence is not the one printed");
    if (op >= CHGA_OPS || from > CHGA_FROM_PERSON || outcome > CHGA_DROPPED || verdict > 3 ||
        nargs != chga_op_nargs(op))
        return fail(ln, "the record holds a field no call can have");
    for (uint32_t i = nargs; i < CHGA_ARGS; i++)
        if (le(b + 17 + 8 * i, 8) != 0) return fail(ln, "an unused argument is not zero");
    static const char TAG[] = "zxv/chga/event/v1";
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) TAG, sizeof TAG - 1);
    sha256_update(&c, st->head, CHGA_DIGEST);
    sha256_update(&c, b, sizeof b);
    sha256_final(&c, want);
    if (memcmp(want, head, CHGA_DIGEST) != 0)
        return fail(ln, "the chain breaks here: a record was changed, removed, added or moved");
    memcpy(st->head, head, CHGA_DIGEST);
    st->records++;
    st->total++;
    printf("  #%llu %s (%s) %s, gate %s", (unsigned long long) seq, chga_op_name(op),
           from == CHGA_FROM_PERSON ? "you" : "chiglet", OUTCOME[outcome], VERDICT[verdict]);
    if (outcome == CHGA_FAILED) printf(", code %d", (int) rc);
    for (uint32_t i = 0; i < nargs; i++)
        printf("%s%llu", i ? " " : "  args ", (unsigned long long) le(b + 17 + 8 * i, 8));
    printf("\n");
    return 0;
}

static int sign_line(state_t *st, unsigned long ln, const char *p)
{
    const char *q;
    if (strncmp(p, "head ", 5) == 0) {
        uint8_t h[CHGA_DIGEST];
        if (!udec(p + 5, &st->sign_n, &q) || !unhex(q, h, sizeof h, &q) || !line_end(q))
            return fail(ln, "not a head line: [EVT] head <records> <head hex>");
        if (st->sign_n != st->records || memcmp(h, st->head, CHGA_DIGEST) != 0)
            return fail(ln, "the signed head is not the head of the records above it");
        memcpy(st->sign_head, h, CHGA_DIGEST);
        st->have_sign = true;
        st->have_key = false;
        return 0;
    }
    if (strncmp(p, "key ", 4) == 0) {
        if (!st->have_sign) return fail(ln, "a key with no head line before it");
        if (!unhex(p + 4, st->key, sizeof st->key, &q) || !line_end(q))
            return fail(ln, "not a key line: [EVT] key <ML-DSA-65 public key hex>");
        st->have_key = true;
        return 0;
    }
    if (strncmp(p, "sig ", 4) == 0) {
        static uint8_t sig[PQ_MLDSA65_SIG_BYTES];
        static const char CTX[] = "zxv/chga/head/v1";
        if (!st->have_key) return fail(ln, "a signature with no head and key before it");
        if (!unhex(p + 4, sig, sizeof sig, &q) || !line_end(q))
            return fail(ln, "not a signature line: [EVT] sig <ML-DSA-65 signature hex>");
        uint8_t msg[8 + CHGA_DIGEST];
        for (uint32_t k = 0; k < 8; k++) msg[k] = (uint8_t) (st->sign_n >> (8 * k));
        memcpy(msg + 8, st->sign_head, CHGA_DIGEST);
        if (!pq_mldsa65_verify(st->key, msg, sizeof msg, (const uint8_t *) CTX, sizeof CTX - 1,
                               sig))
            return fail(ln, "the signature does not verify against its key and head");
        uint8_t fp[32];
        sha256(st->key, sizeof st->key, fp);
        printf("  head of %llu record(s) signed, verified; key ", (unsigned long long) st->sign_n);
        for (uint32_t k = 0; k < 8; k++) printf("%02x", fp[k]);
        printf(" (this boot's key; it shows the log is whole, not which device wrote it)\n");
        st->have_sign = st->have_key = false;
        st->signatures++;
        return 0;
    }
    return -1;
}

int main(int argc, char **argv)
{
    if (argc > 2) {
        fprintf(stderr, "usage: zxv_events [serial log]\n");
        return 2;
    }
    FILE *f = argc == 2 ? fopen(argv[1], "r") : stdin;
    if (!f) {
        perror(argv[1]);
        return 2;
    }
    static char line[LINE_MAX_BYTES];
    static state_t st;
    unsigned long ln = 0;
    int rc = 0;
    while (rc == 0 && fgets(line, sizeof line, f)) {
        ln++;
        size_t n = strlen(line);
        if (n + 1 == sizeof line && line[n - 1] != '\n') {
            rc = fail(ln, "line too long for an event line");
            break;
        }
        if (strstr(line, "[BOOT] Chiglet governor")) {
            memset(st.head, 0, sizeof st.head);
            st.records = 0;
            st.have_sign = st.have_key = false;
            st.boot++;
            printf("boot %u\n", (unsigned) st.boot);
            continue;
        }
        if (strncmp(line, "[EVT] ", 6) != 0) continue;
        const char *p = line + 6;
        int s = sign_line(&st, ln, p);
        if (s >= 0) {
            rc = s;
            continue;
        }
        rc = record(&st, ln, p);
    }
    if (f != stdin) fclose(f);
    if (rc) return rc;
    if (st.total == 0) {
        printf("no events in this log\n");
        return 2;
    }
    if (st.have_sign) return fail(ln, "the log ends inside a signature block");
    printf("ok: %llu record(s) over %u boot(s), every chain whole, %u signature(s) verified\n",
           (unsigned long long) st.total, (unsigned) st.boot, (unsigned) st.signatures);
    return 0;
}
