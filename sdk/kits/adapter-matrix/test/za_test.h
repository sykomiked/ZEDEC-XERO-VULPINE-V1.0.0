/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* za_test.h — the check macro and helpers shared by the adapter tests. */
#ifndef ZA_TEST_H
#define ZA_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zadapter.h"

static int g_checks, g_fail;

static inline const char *last_err(void) {
    static char b[600];
    size_t n = 0;
    if (za_last_error(b, sizeof b, &n) != ZXV_OK) b[0] = '\0';
    return b;
}

#define CHECK(cond, ...)                                                                       \
    do {                                                                                       \
        g_checks++;                                                                            \
        if (!(cond)) {                                                                         \
            g_fail++;                                                                          \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                               \
            fprintf(stderr, __VA_ARGS__);                                                      \
            fprintf(stderr, " [last error: %s]\n", last_err());                                \
        }                                                                                      \
    } while (0)

/* Ingest a NUL-terminated payload. */
static inline za_record *ingest(int32_t kind, const char *source, const char *payload) {
    za_record *r = NULL;
    if (za_ingest(kind, source, (const uint8_t *) payload, strlen(payload), &r) != ZXV_OK) return NULL;
    return r;
}

static inline int32_t lp_of(const za_record *r) {
    int32_t lp = -1;
    (void) za_record_state(r, &lp, NULL);
    return lp;
}

static inline const char *tag_text(const za_record *r) {
    static char b[64];
    int32_t tag = -1;
    size_t n = 0;
    (void) za_record_state(r, NULL, &tag);
    if (zc_tag_format(tag, b, sizeof b, &n) != ZXV_OK) return "?";
    return b;
}

/* The first refusal reason (0 if none) and its message. */
static inline int32_t reason0(const za_record *r, const char **msg) {
    int32_t code = 0;
    const char *m = "";
    if (za_record_reason(r, 0, &code, &m) != ZXV_OK) code = 0;
    if (msg) *msg = m;
    return code;
}

/* Field `name` as a rational; 0 on success. */
static inline int field_rat(const za_record *r, const char *name, int64_t *n, int64_t *d) {
    uint32_t i = 0;
    if (za_field_find(r, name, &i) != ZXV_OK) return -1;
    return za_field_rat(r, i, n, d) == ZXV_OK ? 0 : -1;
}

/* Field `name` equals the text `want` (kind TEXT); 1 if so. */
static inline int field_is_text(const za_record *r, const char *name, const char *want) {
    uint32_t i = 0;
    int32_t kind = 0;
    const uint8_t *b = NULL;
    size_t n = 0;
    if (za_field_find(r, name, &i) != ZXV_OK || za_field(r, i, NULL, &kind) != ZXV_OK ||
        kind != ZA_F_TEXT || za_field_text(r, i, &b, &n) != ZXV_OK)
        return 0;
    return n == strlen(want) && memcmp(b, want, n) == 0;
}

/* Build a FIX message from its body ("35=D|49=..|...|", '|' delimited):
 * BeginString, BodyLength and CheckSum are computed. delim is '|' or 1. */
static inline const char *fix_msg(const char *begin, const char *body, char delim) {
    static char out[4096];
    char b[3500];
    size_t bl = strlen(body);
    unsigned sum = 0;
    snprintf(b, sizeof b, "%s", body);
    for (size_t i = 0; i < bl; i++)
        if (b[i] == '|') b[i] = delim;
    int n = snprintf(out, sizeof out, "8=%s%c9=%zu%c%s", begin, delim, bl, delim, b);
    for (int i = 0; i < n; i++) sum += (unsigned char) (out[i] == delim ? 1 : out[i]);
    snprintf(out + n, sizeof out - (size_t) n, "10=%03u%c", sum % 256u, delim);
    return out;
}

#endif /* ZA_TEST_H */
