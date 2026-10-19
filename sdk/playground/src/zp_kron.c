/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zp_kron.c - KRON steps (sdk/pillars/apl-kron): exact APL/K arrays.
 *
 * The program's free names are bound from the inputs: canonical value
 * text such as (vec 1 2 3), or a plain list "3 1 4 1r2" (a vector), or a
 * single number or truth word. The program's value is the step's output,
 * named by the `out=` option (default: the step's name).
 *
 * Step options:
 *   out=NAME       the output's name
 *   spell=apl|k    the spelling (default: detected)
 *   evidence       the value is a 2-row matrix: row 0 counts evidence FOR
 *                  each item, row 1 evidence AGAINST. Each column becomes
 *                  libzxv's zxv_lp_from_evidence: for and against -> BOTH
 *                  (held for review), for only -> TRUE, against only ->
 *                  FALSE, none -> NEITHER. This is how conflicting reports
 *                  are kept, not averaged away.
 *   labels=NAME    an input holding one label per item, used in the notes
 */
#include "zp_internal.h"

#include <zkron.h>
#include <zxv_api.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BIND 64u

static int is_canon_value(const char *v) { return v[0] == '('; }

/* Build canonical value text for a binding. */
static char *value_text(const char *v) {
    size_t n = strlen(v);
    int words = 0;
    char *t;
    for (size_t i = 0; i < n; i++)
        if (v[i] == ' ' && i + 1u < n && v[i + 1u] != ' ') words++;
    if (is_canon_value(v) || words == 0) return strdup(v);
    t = (char *) malloc(n + 16u);
    if (!t) return NULL;
    {
        int truth = strstr(v, "true") || strstr(v, "false") || strstr(v, "neither") || strstr(v, "both");
        snprintf(t, n + 16u, "(%s %s)", truth ? "tvec" : "vec", v);
    }
    return t;
}

static int32_t parse_value(const char *text, kr_value **out, char *why, size_t whycap) {
    char *vt = value_text(text);
    size_t n;
    char *t;
    kr_program *p = NULL;
    kr_diag d;
    int32_t st;
    if (!vt) return ZXV_E_NOMEM;
    n = strlen(vt);
    t = (char *) malloc(n + 8u);
    if (!t) {
        free(vt);
        return ZXV_E_NOMEM;
    }
    memcpy(t, "kron\n", 5);
    memcpy(t + 5, vt, n + 1u);
    free(vt);
    memset(&d, 0, sizeof d);
    st = kr_parse(t, n + 5u, KR_SPELL_CANON, &p, &d);
    free(t);
    if (st == ZXV_OK) {
        uint32_t nf = 0;
        kr_free_count(p, &nf);
        if (nf) {
            snprintf(why, whycap, "a value cannot name variables");
            kr_program_destroy(p);
            return KR_E_NAME;
        }
        st = kr_eval(p, NULL, 0, 0, out, &d);
    }
    if (st != ZXV_OK) snprintf(why, whycap, "%s", d.msg);
    kr_program_destroy(p);
    return st;
}

/* The i-th space-separated word of s, or "" */
static void word_at(const char *s, uint32_t i, char *buf, size_t cap) {
    uint32_t k = 0;
    buf[0] = '\0';
    if (!s) return;
    while (*s) {
        const char *e;
        while (*s == ' ') s++;
        if (!*s) break;
        e = s;
        while (*e && *e != ' ') e++;
        if (k++ == i) {
            size_t n = (size_t) (e - s);
            if (n >= cap) n = cap - 1u;
            memcpy(buf, s, n);
            buf[n] = '\0';
            return;
        }
        s = e;
    }
}

static void evidence(zp_step *s, const char *name, const kr_value *v) {
    int32_t kind = 0;
    uint32_t rank = 0;
    int64_t shape[2] = {0, 0};
    uint64_t count = 0;
    int64_t *num, *den;
    int32_t *lp;
    const char *labels = zp_step_opt(s, "labels") ? zp_input(s, zp_step_opt(s, "labels")) : NULL;
    kr_value_info(v, &kind, &rank, shape, &count);
    if (kind != KR_KIND_NUM || rank != 2 || shape[0] != 2 || shape[1] < 1 || shape[1] > 4096) {
        zp_refuse(s, KR_E_RANK, "KR_E_RANK", 0, 0,
                  "an evidence step must give a 2-row matrix: counts for, then counts against");
        return;
    }
    num = (int64_t *) calloc((size_t) count, sizeof *num);
    den = (int64_t *) calloc((size_t) count, sizeof *den);
    lp = (int32_t *) calloc((size_t) shape[1], sizeof *lp);
    if (!num || !den || !lp) {
        zp_refuse(s, ZXV_E_NOMEM, NULL, 0, 0, "out of memory");
        goto done;
    }
    kr_value_rats(v, num, den, count);
    for (int64_t j = 0; j < shape[1]; j++) {
        int64_t pos = num[j], neg = num[shape[1] + j];
        if (den[j] != 1 || den[shape[1] + j] != 1 || pos < 0 || neg < 0 || pos > 0xffffffffLL ||
            neg > 0xffffffffLL) {
            zp_refuse(s, KR_E_DOMAIN, "KR_E_DOMAIN", 0, 0, "evidence counts must be whole numbers >= 0");
            goto done;
        }
        zxv_lp_from_evidence((uint32_t) pos, (uint32_t) neg, &lp[j]);
    }
    zp_truths(s, name, lp, (uint32_t) shape[1]);
    if (labels) {
        zp_buf t = {0};
        for (int64_t j = 0; j < shape[1]; j++) {
            char lab[64];
            if (lp[j] != ZXV_LP_TRUE) continue;
            word_at(labels, (uint32_t) j, lab, sizeof lab);
            zp_buf_puts(&t, t.len ? ", " : "");
            zp_buf_puts(&t, lab[0] ? lab : "?");
        }
        zp_notef(s, "%s: TRUE for %s", name, t.p ? t.p : "none");
        zp_buf_free(&t);
    }
    for (int64_t j = 0; j < shape[1]; j++) {
        char lab[64];
        word_at(labels, (uint32_t) j, lab, sizeof lab);
        if (!lab[0]) snprintf(lab, sizeof lab, "item %lld", (long long) j);
        if (lp[j] == ZXV_LP_BOTH) {
            char why[200];
            snprintf(why, sizeof why, "%s: %lld source(s) confirm and %lld contradict; held for a person to resolve",
                     lab, (long long) num[j], (long long) num[shape[1] + j]);
            zp_hold(s, why);
        } else if (lp[j] == ZXV_LP_NEITHER) {
            char why[160];
            snprintf(why, sizeof why, "%s: no source has reported yet", lab);
            zp_pending(s, why);
        }
    }
done:
    free(num);
    free(den);
    free(lp);
}

void zp_lang_kron(zp_step *s, const char *src, size_t len) {
    kr_program *p = NULL;
    kr_diag d;
    kr_value *b[MAX_BIND];
    kr_binding kb[MAX_BIND];
    kr_value *v = NULL;
    uint32_t nf = 0, nb = 0;
    int32_t spell = KR_SPELL_AUTO, st;
    const char *sp = zp_step_opt(s, "spell");
    const char *out = zp_step_opt(s, "out") ? zp_step_opt(s, "out") : zp_step_name(s);
    char nbuf[KR_MAX_NAME + 1];
    if (sp && strcmp(sp, "apl") == 0) spell = KR_SPELL_APL;
    if (sp && strcmp(sp, "k") == 0) spell = KR_SPELL_K;
    memset(&d, 0, sizeof d);
    st = kr_parse(src, len, spell, &p, &d);
    if (st != ZXV_OK) {
        zp_refuse(s, st, kr_status_name(st), d.line, d.col, d.msg);
        return;
    }
    kr_free_count(p, &nf);
    for (uint32_t i = 0; i < nf && i < MAX_BIND; i++) {
        size_t nl = 0;
        const char *val;
        char why[300];
        kr_free_at(p, i, nbuf, sizeof nbuf, &nl);
        val = zp_input(s, nbuf);
        if (!val) {
            char msg[128];
            snprintf(msg, sizeof msg, "needs an input named %s", nbuf);
            zp_pending(s, msg);
            continue;
        }
        st = parse_value(val, &b[nb], why, sizeof why);
        if (st != ZXV_OK) {
            char msg[420];
            snprintf(msg, sizeof msg, "input %s: %s", nbuf, why);
            zp_refuse(s, st, kr_status_name(st), 0, 0, msg);
            goto done;
        }
        kb[nb].name = strdup(nbuf);
        kb[nb].value = b[nb];
        if (!kb[nb].name) {
            kr_value_destroy(b[nb]);
            zp_refuse(s, ZXV_E_NOMEM, NULL, 0, 0, "out of memory");
            goto done;
        }
        nb++;
    }
    if (nb < nf) goto done; /* pending: a name had no input */
    st = kr_eval(p, kb, nb, 0, &v, &d);
    if (st != ZXV_OK) {
        zp_refuse(s, st, kr_status_name(st), d.line, d.col, d.msg);
        goto done;
    }
    if (zp_step_opt(s, "evidence")) {
        evidence(s, out, v);
    } else {
        int32_t kind = 0;
        uint32_t rank = 0;
        int64_t shape[2];
        uint64_t count = 0;
        kr_value_info(v, &kind, &rank, shape, &count);
        if (kind == KR_KIND_TRUTH) {
            int32_t *lp = (int32_t *) calloc(count ? (size_t) count : 1u, sizeof *lp);
            if (!lp) {
                zp_refuse(s, ZXV_E_NOMEM, NULL, 0, 0, "out of memory");
                goto done;
            }
            kr_value_truths(v, lp, count);
            zp_truths(s, out, lp, (uint32_t) count);
            free(lp);
        } else {
            size_t tl = 0;
            char *t;
            kr_value_text(v, NULL, 0, &tl);
            t = (char *) malloc(tl + 1u);
            if (!t) {
                zp_refuse(s, ZXV_E_NOMEM, NULL, 0, 0, "out of memory");
                goto done;
            }
            kr_value_text(v, t, tl + 1u, &tl);
            t[tl] = '\0';
            zp_output(s, out, t);
            free(t);
        }
    }
done:
    for (uint32_t i = 0; i < nb; i++) {
        free((char *) kb[i].name);
        kr_value_destroy(b[i]);
    }
    if (v) kr_value_destroy(v);
    kr_program_destroy(p);
}
