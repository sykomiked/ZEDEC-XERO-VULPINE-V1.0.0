/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zau_test.h — helpers shared by the z-audit tests: the check macro, a
 * strict JSON syntax checker, and report queries. Test use only. */
#ifndef ZAU_TEST_H
#define ZAU_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zaudit.h"
#include "zwasm.h"

#if defined(__GNUC__)
#define ZAU_UNUSED __attribute__((unused))
#else
#define ZAU_UNUSED
#endif

static int g_checks ZAU_UNUSED, g_fail ZAU_UNUSED;

#define CHECK(cond, ...)                                                                       \
    do {                                                                                       \
        g_checks++;                                                                            \
        if (!(cond)) {                                                                         \
            g_fail++;                                                                          \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                               \
            fprintf(stderr, __VA_ARGS__);                                                      \
            fprintf(stderr, "\n");                                                             \
        }                                                                                      \
    } while (0)

/* ----- a strict JSON syntax checker (RFC 8259 grammar, integers only) ----- */
typedef struct {
    const char *s;
    size_t n, i;
    int depth;
} js;

static inline int js_value(js *j);

static inline void js_ws(js *j) {
    while (j->i < j->n && (j->s[j->i] == ' ' || j->s[j->i] == '\n' || j->s[j->i] == '\t' || j->s[j->i] == '\r'))
        j->i++;
}

static inline int js_string(js *j) {
    if (j->i >= j->n || j->s[j->i] != '"') return 0;
    j->i++;
    while (j->i < j->n) {
        unsigned char c = (unsigned char) j->s[j->i++];
        if (c == '"') return 1;
        if (c < 0x20u) return 0;
        if (c == '\\') {
            if (j->i >= j->n) return 0;
            c = (unsigned char) j->s[j->i++];
            if (c == 'u') {
                for (int k = 0; k < 4; k++) {
                    if (j->i >= j->n) return 0;
                    char h = j->s[j->i++];
                    if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F'))) return 0;
                }
            } else if (!strchr("\"\\/bfnrt", (int) c)) {
                return 0;
            }
        }
    }
    return 0;
}

static inline int js_number(js *j) {
    size_t st = j->i;
    if (j->i < j->n && j->s[j->i] == '-') j->i++;
    if (j->i >= j->n || j->s[j->i] < '0' || j->s[j->i] > '9') return 0;
    if (j->s[j->i] == '0') j->i++;
    else
        while (j->i < j->n && j->s[j->i] >= '0' && j->s[j->i] <= '9') j->i++;
    return j->i > st;
}

static inline int js_lit(js *j, const char *w) {
    size_t n = strlen(w);
    if (j->n - j->i < n || memcmp(j->s + j->i, w, n) != 0) return 0;
    j->i += n;
    return 1;
}

static inline int js_value(js *j) {
    js_ws(j);
    if (j->i >= j->n || ++j->depth > 64) return 0;
    int ok = 0;
    char c = j->s[j->i];
    if (c == '{' || c == '[') {
        char close = c == '{' ? '}' : ']';
        j->i++;
        js_ws(j);
        if (j->i < j->n && j->s[j->i] == close) {
            j->i++;
            ok = 1;
        } else {
            for (;;) {
                if (c == '{') {
                    js_ws(j);
                    if (!js_string(j)) break;
                    js_ws(j);
                    if (j->i >= j->n || j->s[j->i] != ':') break;
                    j->i++;
                }
                if (!js_value(j)) break;
                js_ws(j);
                if (j->i < j->n && j->s[j->i] == ',') {
                    j->i++;
                    continue;
                }
                if (j->i < j->n && j->s[j->i] == close) {
                    j->i++;
                    ok = 1;
                }
                break;
            }
        }
    } else if (c == '"') {
        ok = js_string(j);
    } else if (c == 't') {
        ok = js_lit(j, "true");
    } else if (c == 'f') {
        ok = js_lit(j, "false");
    } else if (c == 'n') {
        ok = js_lit(j, "null");
    } else {
        ok = js_number(j);
    }
    j->depth--;
    return ok;
}

static inline int json_ok(const char *s, size_t n) {
    js j = {s, n, 0, 0};
    if (!js_value(&j)) return 0;
    js_ws(&j);
    return j.i == n;
}

/* ----- report queries ----- */
static char *report_json(const zau_report *r, const char *ml, const char *cl, size_t *len) {
    size_t n = 0;
    char *b;
    if (zau_report_json(r, ml, cl, NULL, 0, &n) != ZXV_E_BUFFER) return NULL;
    b = (char *) malloc(n + 1u);
    if (b && zau_report_json(r, ml, cl, b, n + 1u, &n) != ZXV_OK) {
        free(b);
        return NULL;
    }
    if (len) *len = n;
    return b;
}

static char *report_text(const zau_report *r, size_t *len) {
    size_t n = 0;
    char *b;
    if (zau_report_text(r, "m.wasm", "c.zc", NULL, 0, &n) != ZXV_E_BUFFER) return NULL;
    b = (char *) malloc(n + 1u);
    if (b && zau_report_text(r, "m.wasm", "c.zc", b, n + 1u, &n) != ZXV_OK) {
        free(b);
        return NULL;
    }
    if (len) *len = n;
    return b;
}

/* Number of findings with this code (and severity, -1 = any; export, NULL = any). */
static inline uint32_t count_code(const zau_report *r, const char *code, int32_t sev, const char *exp) {
    uint32_t n = 0, k = 0;
    (void) zau_report_finding_count(r, &n);
    for (uint32_t i = 0; i < n; i++) {
        const char *c = NULL, *e = NULL;
        int32_t s = -1;
        if (zau_report_finding(r, i, NULL, &s, &c, NULL, &e, NULL, NULL) != ZXV_OK) continue;
        if (strcmp(c, code) == 0 && (sev < 0 || s == sev) && (!exp || strcmp(e, exp) == 0)) k++;
    }
    return k;
}

/* The same, within one check. */
static inline uint32_t count_in(const zau_report *r, uint32_t check, const char *code) {
    uint32_t n = 0, k = 0;
    (void) zau_report_finding_count(r, &n);
    for (uint32_t i = 0; i < n; i++) {
        const char *c = NULL;
        uint32_t ck = 99;
        if (zau_report_finding(r, i, &ck, NULL, &c, NULL, NULL, NULL, NULL) != ZXV_OK) continue;
        if (ck == check && strcmp(c, code) == 0) k++;
    }
    return k;
}

static inline int32_t check_status(const zau_report *r, uint32_t idx) {
    int32_t st = -1;
    (void) zau_report_check(r, idx, NULL, &st, NULL);
    return st;
}

static inline int passed(const zau_report *r) {
    int32_t p = -1;
    (void) zau_report_verdict(r, &p, NULL, NULL);
    return p == 1;
}

/* Print a report's text (for a failing test). */
static inline void dump(const zau_report *r) {
    char *t = report_text(r, NULL);
    if (t) fprintf(stderr, "%s\n", t);
    free(t);
}

#endif /* ZAU_TEST_H */
