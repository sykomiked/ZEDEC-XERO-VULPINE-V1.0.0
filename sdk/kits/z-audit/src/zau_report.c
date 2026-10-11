/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zau_report.c — the report: accessors, JSON and text. Integer code only
 * (R11). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zau_internal.h"

static const char *const k_ids[ZAU_CHECKS] = {"module.validate", "module.float-simd", "module.imports",
                                              "contract.parse",  "exports.declared",  "contract.bounds",
                                              "exports.analysis"};
static const char *const k_status[4] = {"pass", "warn", "fail", "skip"};
static const char *const k_sev[3] = {"info", "warning", "error"};

static const char k_note[] =
    "Bounded static checks. A pass does not prove that no Forbidden state is reachable or that the "
    "plugin is correct; the runtime guards (Z-Wasm validation and fuel, z-contract call checks) still "
    "apply to every call.";

void zau_report_destroy(zau_report *report) {
    if (!report) return;
    for (uint32_t i = 0; i < report->nf; i++) free(report->f[i].message);
    free(report->f);
    free(report);
}

int32_t zau_report_verdict(const zau_report *report, int32_t *out_pass, uint32_t *out_errors,
                           uint32_t *out_warnings) {
    if (!report) return ZXV_E_ARG;
    if (out_pass) *out_pass = report->errors == 0 && (!report->strict || report->warnings == 0);
    if (out_errors) *out_errors = report->errors;
    if (out_warnings) *out_warnings = report->warnings;
    return ZXV_OK;
}

int32_t zau_report_check_count(const zau_report *report, uint32_t *out_count) {
    if (!report || !out_count) return ZXV_E_ARG;
    *out_count = ZAU_CHECKS;
    return ZXV_OK;
}

int32_t zau_report_check(const zau_report *report, uint32_t index, const char **out_id,
                         int32_t *out_status, const char **out_summary) {
    if (!report) return ZXV_E_ARG;
    if (index >= ZAU_CHECKS) return ZXV_E_NOT_FOUND;
    if (out_id) *out_id = k_ids[index];
    if (out_status) *out_status = report->checks[index].status;
    if (out_summary) *out_summary = report->checks[index].summary;
    return ZXV_OK;
}

int32_t zau_report_finding_count(const zau_report *report, uint32_t *out_count) {
    if (!report || !out_count) return ZXV_E_ARG;
    *out_count = report->nf;
    return ZXV_OK;
}

int32_t zau_report_finding(const zau_report *report, uint32_t index, uint32_t *out_check,
                           int32_t *out_severity, const char **out_code,
                           const char **out_message, const char **out_export,
                           int64_t *out_func, int64_t *out_offset) {
    const zau_finding *f;
    if (!report) return ZXV_E_ARG;
    if (index >= report->nf) return ZXV_E_NOT_FOUND;
    f = &report->f[index];
    if (out_check) *out_check = f->check;
    if (out_severity) *out_severity = f->severity;
    if (out_code) *out_code = f->code;
    if (out_message) *out_message = f->message;
    if (out_export) *out_export = f->exp;
    if (out_func) *out_func = f->func;
    if (out_offset) *out_offset = f->offset;
    return ZXV_OK;
}

/* ----- a growable text buffer ----- */
typedef struct {
    char *b;
    size_t n, cap;
    int oom;
} buf;

static void put(buf *o, const char *s, size_t n) {
    if (o->oom) return;
    if (o->n + n + 1u > o->cap) {
        size_t nc = o->cap ? o->cap : 1024u;
        while (nc < o->n + n + 1u) nc *= 2u;
        char *nb = (char *) realloc(o->b, nc);
        if (!nb) {
            o->oom = 1;
            return;
        }
        o->b = nb;
        o->cap = nc;
    }
    memcpy(o->b + o->n, s, n);
    o->n += n;
    o->b[o->n] = '\0';
}

static void puts_(buf *o, const char *s) { put(o, s, strlen(s)); }

static void putf(buf *o, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
static void putf(buf *o, const char *fmt, ...) {
    char t[1024];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    put(o, t, (size_t) n < sizeof t ? (size_t) n : sizeof t - 1u);
}

/* 1 if s has n bytes before its NUL. */
static int present(const char *s, size_t n) {
    for (size_t k = 0; k < n; k++)
        if (!s[k]) return 0;
    return 1;
}

/* A JSON string: control characters, quotes and backslashes escaped;
 * any byte that is not part of a valid UTF-8 sequence becomes '?'. */
static void jstr(buf *o, const char *s) {
    if (!s) {
        puts_(o, "null");
        return;
    }
    puts_(o, "\"");
    for (size_t i = 0; s[i];) {
        unsigned char c = (unsigned char) s[i];
        size_t len = c < 0x80u ? 1u : (c & 0xE0u) == 0xC0u ? 2u : (c & 0xF0u) == 0xE0u ? 3u
                                     : (c & 0xF8u) == 0xF0u ? 4u : 0u;
        if (len == 1u) {
            if (c == '"') puts_(o, "\\\"");
            else if (c == '\\') puts_(o, "\\\\");
            else if (c == '\n') puts_(o, "\\n");
            else if (c < 0x20u || c == 0x7Fu) putf(o, "\\u%04x", (unsigned) c);
            else put(o, s + i, 1);
            i++;
        } else if (len && present(s + i, len) && zw_utf8_ok((const uint8_t *) s + i, len)) {
            put(o, s + i, len);
            i += len;
        } else {
            puts_(o, "?");
            i++;
        }
    }
    puts_(o, "\"");
}

static void hex(buf *o, const uint8_t *h) {
    for (int i = 0; i < 32; i++) putf(o, "%02x", h[i]);
}

static int32_t emit(buf *o, char *dst, size_t cap, size_t *out_len) {
    if (o->oom) {
        free(o->b);
        return ZXV_E_NOMEM;
    }
    *out_len = o->n;
    if (!dst || cap <= o->n) {
        free(o->b);
        return ZXV_E_BUFFER;
    }
    memcpy(dst, o->b ? o->b : "", o->n + 1u);
    free(o->b);
    return ZXV_OK;
}

int32_t zau_report_json(const zau_report *report, const char *module_label,
                        const char *contract_label, char *dst, size_t cap, size_t *out_len) {
    buf o = {NULL, 0, 0, 0};
    int32_t pass = 0;
    if (!report || !out_len) return ZXV_E_ARG;
    (void) zau_report_verdict(report, &pass, NULL, NULL);
    puts_(&o, "{\n  \"tool\": \"z-audit\",\n");
    putf(&o, "  \"version\": \"%d.%d.%d\",\n", ZAU_VERSION_MAJOR, ZAU_VERSION_MINOR, ZAU_VERSION_PATCH);
    puts_(&o, "  \"module\": {\"label\": ");
    jstr(&o, module_label);
    putf(&o, ", \"bytes\": %zu, \"sha3_256\": \"", report->module_len);
    hex(&o, report->module_hash);
    puts_(&o, "\"},\n  \"contract\": {\"label\": ");
    jstr(&o, contract_label);
    puts_(&o, ", \"digest_sha3_256\": ");
    if (report->has_contract) {
        puts_(&o, "\"");
        hex(&o, report->contract_digest);
        puts_(&o, "\"");
    } else {
        puts_(&o, "null");
    }
    putf(&o, "},\n  \"result\": \"%s\",\n  \"strict\": %s,\n  \"errors\": %u,\n  \"warnings\": %u,\n",
         pass ? "pass" : "fail", report->strict ? "true" : "false", (unsigned) report->errors,
         (unsigned) report->warnings);
    puts_(&o, "  \"checks\": [\n");
    for (int i = 0; i < ZAU_CHECKS; i++) {
        const zau_check *c = &report->checks[i];
        putf(&o, "    {\"id\": \"%s\", \"status\": \"%s\", \"summary\": ", k_ids[i], k_status[c->status & 3]);
        jstr(&o, c->summary);
        putf(&o, ", \"errors\": %u, \"warnings\": %u, \"notes\": %u, \"suppressed\": %u}%s\n",
             (unsigned) c->n[ZAU_ERROR], (unsigned) c->n[ZAU_WARNING], (unsigned) c->n[ZAU_INFO],
             (unsigned) c->suppressed, i + 1 < ZAU_CHECKS ? "," : "");
    }
    puts_(&o, "  ],\n  \"findings\": [\n");
    for (uint32_t i = 0; i < report->nf; i++) {
        const zau_finding *f = &report->f[i];
        putf(&o, "    {\"check\": \"%s\", \"severity\": \"%s\", \"code\": ", k_ids[f->check % ZAU_CHECKS],
             k_sev[f->severity % 3]);
        jstr(&o, f->code);
        puts_(&o, ", \"message\": ");
        jstr(&o, f->message);
        puts_(&o, ", \"export\": ");
        jstr(&o, f->exp[0] ? f->exp : NULL);
        if (f->func >= 0) putf(&o, ", \"func\": %lld", (long long) f->func);
        else puts_(&o, ", \"func\": null");
        if (f->offset >= 0) putf(&o, ", \"offset\": %lld}", (long long) f->offset);
        else puts_(&o, ", \"offset\": null}");
        puts_(&o, i + 1u < report->nf ? ",\n" : "\n");
    }
    puts_(&o, "  ],\n  \"note\": ");
    jstr(&o, k_note);
    puts_(&o, "\n}\n");
    return emit(&o, dst, cap, out_len);
}

int32_t zau_report_text(const zau_report *report, const char *module_label,
                        const char *contract_label, char *dst, size_t cap, size_t *out_len) {
    static const char *const tag[4] = {"PASS", "WARN", "FAIL", "SKIP"};
    static const char *const sev[3] = {"note", "warning", "error"};
    buf o = {NULL, 0, 0, 0};
    char ml[128], cl[128];
    int32_t pass = 0;
    if (!report || !out_len) return ZXV_E_ARG;
    (void) zau_report_verdict(report, &pass, NULL, NULL);
    (void) zau_clean(ml, sizeof ml, (const uint8_t *) (module_label ? module_label : "module"),
                     strlen(module_label ? module_label : "module"));
    (void) zau_clean(cl, sizeof cl, (const uint8_t *) (contract_label ? contract_label : "(no contract)"),
                     strlen(contract_label ? contract_label : "(no contract)"));
    putf(&o, "z-audit %d.%d.%d: %s against %s\n", ZAU_VERSION_MAJOR, ZAU_VERSION_MINOR, ZAU_VERSION_PATCH,
         ml, cl);
    putf(&o, "  module   %zu bytes, sha3-256 ", report->module_len);
    hex(&o, report->module_hash);
    puts_(&o, "\n");
    if (report->has_contract) {
        puts_(&o, "  contract sha3-256 ");
        hex(&o, report->contract_digest);
        puts_(&o, " (canonical text)\n");
    }
    puts_(&o, "\n");
    for (int i = 0; i < ZAU_CHECKS; i++) {
        const zau_check *c = &report->checks[i];
        putf(&o, "%s  %-18s %s\n", tag[c->status & 3], k_ids[i], c->summary);
        for (uint32_t k = 0; k < report->nf; k++) {
            const zau_finding *f = &report->f[k];
            if (f->check != (uint32_t) i) continue;
            if (f->severity == ZAU_INFO && c->status == ZAU_PASS && i != ZAU_C_ANALYSIS) continue;
            putf(&o, "      %-7s %-22s %s", sev[f->severity % 3], f->code, f->message);
            if (f->func >= 0) putf(&o, " [function %lld]", (long long) f->func);
            if (f->offset >= 0) putf(&o, " [byte %lld]", (long long) f->offset);
            puts_(&o, "\n");
        }
        if (c->suppressed) putf(&o, "      ... and %u more\n", (unsigned) c->suppressed);
    }
    putf(&o, "\nRESULT: %s (%u error(s), %u warning(s)%s)\n", pass ? "PASS" : "FAIL",
         (unsigned) report->errors, (unsigned) report->warnings, report->strict ? ", strict" : "");
    putf(&o, "%s\n", k_note);
    return emit(&o, dst, cap, out_len);
}
