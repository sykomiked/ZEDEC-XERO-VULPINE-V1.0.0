/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zau_audit.c — options and the checks of one audit. Integer code only
 * (R11). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zau_internal.h"

/* Appends to buf (capacity cap) at n and returns the new length, clamped so
 * the text is truncated, never overrun. */
static size_t appendf(char *buf, size_t cap, size_t n, const char *fmt, ...) {
    va_list ap;
    int r;
    if (n >= cap) return n;
    va_start(ap, fmt);
    r = vsnprintf(buf + n, cap - n, fmt, ap);
    va_end(ap);
    if (r < 0) return n;
    return (size_t) r >= cap - n ? cap - 1u : n + (size_t) r;
}

/* The host functions behind each Z-Wasm grant (z-wasm src/zw_host.c,
 * README "Host functions"). test_audit checks this table against a real
 * Z-Wasm host: each import loads under its grant and not without it. */
static const struct {
    const char *name, *sig;
    uint32_t grant;
} k_hostfns[] = {
    {"rat_add", "i(IIIIi)", ZW_GRANT_RATIONAL},       {"rat_sub", "i(IIIIi)", ZW_GRANT_RATIONAL},
    {"rat_mul", "i(IIIIi)", ZW_GRANT_RATIONAL},       {"rat_div", "i(IIIIi)", ZW_GRANT_RATIONAL},
    {"rat_cmp", "i(IIIIi)", ZW_GRANT_RATIONAL},       {"rat_normalize", "i(IIi)", ZW_GRANT_RATIONAL},
    {"lp_not", "i(i)", ZW_GRANT_LOGIC},               {"lp_and", "i(ii)", ZW_GRANT_LOGIC},
    {"lp_or", "i(ii)", ZW_GRANT_LOGIC},               {"lp_from_evidence", "i(ii)", ZW_GRANT_LOGIC},
    {"propose", "i(iIIii)", ZW_GRANT_PROPOSE},
};

int32_t zau_options_create(zau_options **out_options) {
    zau_options *o;
    if (!out_options) return ZXV_E_ARG;
    o = (zau_options *) calloc(1, sizeof *o);
    if (!o) return ZXV_E_NOMEM;
    if (zw_policy_create(&o->policy) != ZXV_OK) {
        free(o);
        return ZXV_E_NOMEM;
    }
    *out_options = o;
    return ZXV_OK;
}

void zau_options_destroy(zau_options *options) {
    if (!options) return;
    zw_policy_destroy(options->policy);
    free(options);
}

int32_t zau_options_grant(zau_options *options, uint32_t grants) {
    if (!options || (grants & ~(ZW_GRANT_RATIONAL | ZW_GRANT_LOGIC | ZW_GRANT_PROPOSE))) {
        zau_set_error("zau_options_grant: unknown grant bits");
        return ZXV_E_ARG;
    }
    for (size_t i = 0; i < sizeof k_hostfns / sizeof k_hostfns[0]; i++) {
        if (!(grants & k_hostfns[i].grant)) continue;
        int32_t st = zw_policy_allow_import(options->policy, "zxv", k_hostfns[i].name, k_hostfns[i].sig);
        if (st != ZXV_OK && st != ZXV_E_DUPLICATE) return st;
    }
    return ZXV_OK;
}

int32_t zau_options_allow_import(zau_options *options, const char *module, const char *name,
                                 const char *signature) {
    if (!options) return ZXV_E_ARG;
    return zw_policy_allow_import(options->policy, module, name, signature);
}

int32_t zau_options_set_limit(zau_options *options, int32_t which, uint64_t value) {
    if (!options) return ZXV_E_ARG;
    return zw_policy_set_limit(options->policy, which, value);
}

int32_t zau_options_set_flags(zau_options *options, uint32_t flags) {
    if (!options || (flags & ~(ZAU_F_STRICT | ZAU_F_NO_ANALYSIS))) return ZXV_E_ARG;
    options->flags = flags;
    return ZXV_OK;
}

/* ----- module.validate ----- */
static void check_validate(zau_report *r, const zw_policy *p, const uint8_t *w, size_t len) {
    int32_t reason = 0;
    uint32_t sec = 0, func = 0;
    uint64_t off = 0;
    char msg[512];
    int32_t st = zw_validate(p, w, len, &reason, &sec, &func, &off, msg, sizeof msg, NULL);
    if (st == ZW_OK) {
        zau_summary(r, ZAU_C_VALIDATE, "the Z-Wasm profile validator accepts the module (%zu bytes)", len);
        return;
    }
    zau_add(r, ZAU_C_VALIDATE, ZAU_ERROR, zw_reason_name(reason), NULL,
            func == ZW_NO_FUNC ? -1 : (int64_t) func, (int64_t) off, "%s", msg);
    zau_summary(r, ZAU_C_VALIDATE, "the Z-Wasm profile validator refuses the module: %s",
                zw_reason_name(reason));
}

/* ----- module.imports ----- */
static void check_imports(zau_report *r, const zw_policy *p, const zau_module *m) {
    uint32_t allowed = 0;
    for (uint32_t i = 0; i < m->nimports; i++) {
        const zau_import *im = &m->imports[i];
        static const char *const kinds[4] = {"function", "table", "memory", "global"};
        if (im->kind != 0) {
            zau_add(r, ZAU_C_IMPORTS, ZAU_ERROR, "IMPORT_KIND", NULL, -1, (int64_t) im->off,
                    "%s.%s is a %s import; a plugin may import only functions", im->mod, im->name,
                    kinds[im->kind & 3u]);
            continue;
        }
        char sig[40];
        const zau_ftype *t = im->type < m->ntypes ? &m->types[im->type] : NULL;
        if (!t) {
            zau_add(r, ZAU_C_IMPORTS, ZAU_ERROR, "IMPORT_TYPE", NULL, -1, (int64_t) im->off,
                    "%s.%s names type %u, which does not exist", im->mod, im->name, (unsigned) im->type);
            continue;
        }
        zau_sig(t, sig, sizeof sig);
        const zw_allow *hit = NULL;
        for (uint32_t k = 0; k < p->n_allow && !im->cut; k++)
            if (strcmp(p->allow[k].module, im->mod) == 0 && strcmp(p->allow[k].name, im->name) == 0)
                hit = &p->allow[k];
        if (!hit) {
            zau_add(r, ZAU_C_IMPORTS, ZAU_ERROR, "IMPORT_DENIED", NULL, -1, (int64_t) im->off,
                    "%s.%s %s is not on the allow-list (grant it, or remove the import)", im->mod,
                    im->name, sig);
        } else if (strcmp(hit->sig, sig) != 0) {
            zau_add(r, ZAU_C_IMPORTS, ZAU_ERROR, "IMPORT_SIG", NULL, -1, (int64_t) im->off,
                    "%s.%s is imported as %s; the host provides %s", im->mod, im->name, sig, hit->sig);
        } else {
            allowed++;
            zau_add(r, ZAU_C_IMPORTS, ZAU_INFO, "IMPORT_ALLOWED", NULL, -1, (int64_t) im->off,
                    "%s.%s %s is allowed", im->mod, im->name, sig);
        }
    }
    zau_summary(r, ZAU_C_IMPORTS, "%u import(s), %u allowed", (unsigned) m->nimports, (unsigned) allowed);
}

/* ----- exports.declared ----- */
typedef struct {
    const zau_export *e;
    uint32_t fidx;
} bound_export;

static const char *kind_name(int32_t k) {
    switch (k) {
    case ZC_T_INT: return "int";
    case ZC_T_RAT: return "rat";
    case ZC_T_TRUTH: return "truth";
    case ZC_T_TAG: return "tag";
    case ZC_T_BYTES: return "bytes";
    default: return "?";
    }
}

/* 1 if fn binds to the export the way zc_wasm_attach would bind it. */
static int binds(zau_report *r, const zau_module *m, const zau_export *e, const zc_contract *c) {
    uint32_t np = 0, maxp = 0, flat = 0;
    int32_t has_res = 0, has_prop = 0, ok = 1, rtype = 0;
    const zau_ftype *t = zau_func_type(m, e->idx);
    char decl[200];
    size_t dl = 0;
    if (!t) {
        zau_add(r, ZAU_C_DECLARED, ZAU_ERROR, "EXPORT_INDEX", e->name, (int64_t) e->idx, (int64_t) e->off,
                "export %s names function %u, which does not exist", e->name, (unsigned) e->idx);
        return 0;
    }
    if (zc_fn_info(c, e->name, &np, &has_res, &has_prop, &maxp) != ZXV_OK) {
        zau_add(r, ZAU_C_DECLARED, ZAU_ERROR, "UNDECLARED", e->name, (int64_t) e->idx, (int64_t) e->off,
                "export %s has no declaration in the contract: add `fn %s(...)`", e->name, e->name);
        return 0;
    }
    dl = appendf(decl, sizeof decl, dl, "%s(", e->name);
    for (uint32_t i = 0; i < np; i++) {
        int32_t k = 0;
        (void) zc_slot_type(c, e->name, ZC_PART_PARAMS, i, &k, NULL);
        dl = appendf(decl, sizeof decl, dl, "%s%s", i ? ", " : "", kind_name(k));
        if (k == ZC_T_BYTES) {
            zau_add(r, ZAU_C_DECLARED, ZAU_ERROR, "MAPPING", e->name, (int64_t) e->idx, (int64_t) e->off,
                    "%s: parameter %u is bytes, which a Wasm export cannot take", e->name, (unsigned) i);
            ok = 0;
        }
        flat += k == ZC_T_RAT ? 2u : 1u;
    }
    dl = appendf(decl, sizeof decl, dl, ")");
    if (ok && flat != t->np) {
        zau_add(r, ZAU_C_DECLARED, ZAU_ERROR, "ARITY", e->name, (int64_t) e->idx, (int64_t) e->off,
                "%s: the declaration needs %u Wasm argument(s) (a rat takes two), the export has %u",
                e->name, (unsigned) flat, (unsigned) t->np);
        ok = 0;
    }
    for (uint32_t i = 0; i < t->np && i < ZAU_MAX_SIG; i++)
        if (t->p[i] != WT_I32 && t->p[i] != WT_I64) ok = 0; /* reported by the float / validate checks */
    if (has_res) (void) zc_slot_type(c, e->name, ZC_PART_RESULT, 0, &rtype, NULL);
    if (has_res && (rtype == ZC_T_RAT || rtype == ZC_T_BYTES)) {
        zau_add(r, ZAU_C_DECLARED, ZAU_ERROR, "MAPPING", e->name, (int64_t) e->idx, (int64_t) e->off,
                "%s: a %s result cannot come back from a Wasm export (propose a rat instead)", e->name,
                kind_name(rtype));
        ok = 0;
    } else if (has_res && t->nr != 1) {
        zau_add(r, ZAU_C_DECLARED, ZAU_ERROR, "RESULT_MISSING", e->name, (int64_t) e->idx, (int64_t) e->off,
                "%s: the declaration has a result, the export returns %u value(s)", e->name, (unsigned) t->nr);
        ok = 0;
    } else if (!has_res && t->nr != 0) {
        zau_add(r, ZAU_C_DECLARED, ZAU_ERROR, "RESULT_UNDECLARED", e->name, (int64_t) e->idx, (int64_t) e->off,
                "%s: the export returns a value but the declaration has no `-> type`", e->name);
        ok = 0;
    }
    if (ok)
        zau_add(r, ZAU_C_DECLARED, ZAU_INFO, "DECLARED", e->name, (int64_t) e->idx, (int64_t) e->off,
                "%s is declared as %s%s%s%s", e->name, decl, has_res ? " -> " : "",
                has_res ? kind_name(rtype) : "", has_prop ? ", with proposals" : "");
    return ok;
}

/* ----- contract.bounds ----- */
static void bounds_slot(zau_report *r, const char *fn, const char *what, const zc_bounds *b) {
    int f = zau_rat_feasible(b);
    if (f == 0)
        zau_add(r, ZAU_C_BOUNDS, ZAU_ERROR, "EMPTY_BOUNDS", fn, -1, -1,
                "%s: no rational in the %s range meets its den rule: every value would be refused", fn, what);
    else if (f < 0)
        zau_add(r, ZAU_C_BOUNDS, ZAU_INFO, "BOUNDS_UNDETERMINED", fn, -1, -1,
                "%s: whether any rational in the %s range meets its den rule was not settled within the "
                "bound (denominators up to 4096)", fn, what);
}

static void bounds_export(zau_report *r, const zau_module *m, const bound_export *be, const zc_contract *c) {
    const zau_ftype *t = zau_func_type(m, be->fidx);
    const char *fn = be->e->name;
    uint32_t np = 0, k = 0;
    int32_t has_res = 0;
    zc_bounds b;
    if (!t || zc_fn_info(c, fn, &np, &has_res, NULL, NULL) != ZXV_OK) return;
    for (uint32_t i = 0; i < np; i++) {
        const char *pname = "";
        int32_t ty = 0;
        if (zc_slot_bounds(c, fn, ZC_PART_PARAMS, i, &b) != ZXV_OK) return;
        (void) zc_slot_type(c, fn, ZC_PART_PARAMS, i, &ty, &pname);
        if (b.type == ZC_T_INT && k < ZAU_MAX_SIG && t->p[k] == WT_I32) {
            int64_t lo = b.has_lo ? b.lo_num : INT64_MIN, hi = b.has_hi ? b.hi_num : INT64_MAX;
            if (hi < INT32_MIN || lo > INT32_MAX)
                zau_add(r, ZAU_C_BOUNDS, ZAU_ERROR, "I32_RANGE", fn, (int64_t) be->fidx, -1,
                        "%s: parameter %u '%s' allows no value an i32 parameter can carry", fn,
                        (unsigned) i, pname);
            else if (lo < INT32_MIN || hi > INT32_MAX)
                zau_add(r, ZAU_C_BOUNDS, ZAU_WARNING, "I32_RANGE", fn, (int64_t) be->fidx, -1,
                        "%s: parameter %u '%s' is an i32 in the export but its range is wider than int32: "
                        "the guard checks the int64 the caller passes, the export sees its low 32 bits; "
                        "keep the range inside [-2147483648..2147483647]", fn, (unsigned) i, pname);
        }
        if (b.type == ZC_T_RAT && k + 1u < ZAU_MAX_SIG && (t->p[k] == WT_I32 || t->p[k + 1u] == WT_I32))
            zau_add(r, ZAU_C_BOUNDS, ZAU_WARNING, "I32_RANGE", fn, (int64_t) be->fidx, -1,
                    "%s: rat parameter %u '%s' reaches the export through an i32; use two i64 parameters "
                    "to carry an int64 numerator and denominator", fn, (unsigned) i, pname);
        k += b.type == ZC_T_RAT ? 2u : 1u;
    }
    if (has_res && t->nr == 1 && t->r[0] == WT_I32 && zc_slot_bounds(c, fn, ZC_PART_RESULT, 0, &b) == ZXV_OK &&
        b.type == ZC_T_INT) {
        int64_t lo = b.has_lo ? b.lo_num : INT64_MIN, hi = b.has_hi ? b.hi_num : INT64_MAX;
        if (hi < INT32_MIN || lo > INT32_MAX)
            zau_add(r, ZAU_C_BOUNDS, ZAU_ERROR, "I32_RANGE", fn, (int64_t) be->fidx, -1,
                    "%s: the declared result range has no value an i32 result can have: every call would "
                    "be stopped", fn);
    }
}

static void check_bounds_all(zau_report *r, const zc_contract *c, uint32_t *slots) {
    uint32_t nf = 0;
    (void) zc_fn_count(c, &nf);
    for (uint32_t i = 0; i < nf; i++) {
        const char *fn = NULL;
        uint32_t np = 0;
        int32_t has_prop = 0;
        zc_bounds b;
        char what[96];
        if (zc_fn_name(c, i, &fn) != ZXV_OK || zc_fn_info(c, fn, &np, NULL, &has_prop, NULL) != ZXV_OK) continue;
        for (uint32_t k = 0; k < np; k++) {
            const char *pname = "";
            int32_t ty = 0;
            (*slots)++;
            if (zc_slot_bounds(c, fn, ZC_PART_PARAMS, k, &b) != ZXV_OK) continue;
            (void) zc_slot_type(c, fn, ZC_PART_PARAMS, k, &ty, &pname);
            snprintf(what, sizeof what, "parameter '%s'", pname);
            bounds_slot(r, fn, what, &b);
        }
        if (zc_slot_bounds(c, fn, ZC_PART_RESULT, 0, &b) == ZXV_OK) {
            (*slots)++;
            bounds_slot(r, fn, "result", &b);
        }
        if (has_prop && zc_slot_bounds(c, fn, ZC_PART_PROPOSALS, 0, &b) == ZXV_OK) {
            (*slots)++;
            bounds_slot(r, fn, "proposes", &b);
        }
    }
}

static void finish_report(zau_report *r) {
    static const char *const sev[3] = {"note", "warning", "error"};
    for (int i = 0; i < ZAU_CHECKS; i++) {
        zau_check *c = &r->checks[i];
        c->status = c->skipped ? ZAU_SKIP : c->n[ZAU_ERROR] ? ZAU_FAIL : c->n[ZAU_WARNING] ? ZAU_WARN : ZAU_PASS;
        if (!c->summary[0]) {
            if (c->n[ZAU_ERROR] || c->n[ZAU_WARNING])
                zau_summary(r, (uint32_t) i, "%u %s(s), %u %s(s)", (unsigned) c->n[ZAU_ERROR], sev[2],
                            (unsigned) c->n[ZAU_WARNING], sev[1]);
            else
                zau_summary(r, (uint32_t) i, "ok (%u %s(s))", (unsigned) c->n[ZAU_INFO], sev[0]);
        }
    }
}

int32_t zau_audit(const zau_options *options, const uint8_t *wasm, size_t wasm_len,
                  const char *contract, size_t contract_len, zau_report **out_report) {
    zau_report *r;
    zau_module m;
    zc_contract *c = NULL;
    bound_export *bound = NULL;
    uint32_t nbound = 0;
    if (!options || !wasm || !out_report || (contract == NULL && contract_len != 0)) {
        zau_set_error("zau_audit: NULL argument");
        return ZXV_E_ARG;
    }
    r = (zau_report *) calloc(1, sizeof *r);
    if (!r) return ZXV_E_NOMEM;
    r->strict = (options->flags & ZAU_F_STRICT) ? 1 : 0;
    r->module_len = wasm_len;
    if (zxv_sha3_256(wasm, wasm_len, r->module_hash) != ZXV_OK) memset(r->module_hash, 0, 32);

    /* 1. the validator, the same filter a Z-Wasm host applies at load */
    check_validate(r, options->policy, wasm, wasm_len);

    /* 2. the walk: float / SIMD listing */
    zau_module_read(&m, wasm, wasm_len, r);
    if (!m.parsed) {
        int validated = r->checks[ZAU_C_VALIDATE].n[ZAU_ERROR] == 0;
        zau_add(r, ZAU_C_FLOAT, validated ? ZAU_ERROR : ZAU_INFO, "MALFORMED", NULL, -1, (int64_t) m.err_off,
                "the module could not be read past byte %zu (%s); the listing is incomplete", m.err_off, m.err);
    } else if (!r->checks[ZAU_C_FLOAT].n[ZAU_ERROR]) {
        zau_summary(r, ZAU_C_FLOAT, "no float or SIMD type or instruction");
    } else {
        zau_summary(r, ZAU_C_FLOAT, "%u float or SIMD use(s)", (unsigned) r->checks[ZAU_C_FLOAT].n[ZAU_ERROR]);
    }

    /* 3. imports */
    if (m.parsed) check_imports(r, options->policy, &m);
    else zau_skip(r, ZAU_C_IMPORTS, "the module could not be read");

    /* 4. the contract */
    if (!contract) {
        zau_add(r, ZAU_C_PARSE, ZAU_ERROR, "NO_CONTRACT", NULL, -1, -1,
                "no contract was given: every exported function needs a z-contract declaration");
    } else {
        uint32_t line = 0, col = 0;
        char msg[300];
        if (zc_parse(contract, contract_len, &c, &line, &col, NULL, msg, sizeof msg, NULL) != ZXV_OK) {
            zau_add(r, ZAU_C_PARSE, ZAU_ERROR, "SYNTAX", NULL, -1, -1, "line %u, column %u: %s",
                    (unsigned) line, (unsigned) col, msg);
            c = NULL;
        } else {
            uint32_t nf = 0;
            r->has_contract = 1;
            (void) zc_contract_digest(c, r->contract_digest);
            (void) zc_fn_count(c, &nf);
            zau_summary(r, ZAU_C_PARSE, "%u function declaration(s)", (unsigned) nf);
        }
    }

    /* 5. every exported function is declared and binds */
    if (!m.parsed) {
        zau_skip(r, ZAU_C_DECLARED, "the module could not be read");
    } else {
        uint32_t nfx = 0;
        bound = (bound_export *) calloc(m.nexports ? m.nexports : 1u, sizeof *bound);
        if (!bound) r->oom = 1;
        for (uint32_t i = 0; i < m.nexports && bound; i++) {
            const zau_export *e = &m.exports[i];
            if (e->kind != 0) {
                static const char *const kinds[4] = {"function", "table", "memory", "global"};
                zau_add(r, ZAU_C_DECLARED, ZAU_INFO, "NOT_A_FUNCTION", e->name, -1, (int64_t) e->off,
                        "export %s is a %s: no declaration needed", e->name, kinds[e->kind & 3u]);
                continue;
            }
            nfx++;
            if (!c) {
                if (!contract)
                    zau_add(r, ZAU_C_DECLARED, ZAU_ERROR, "UNDECLARED", e->name, (int64_t) e->idx,
                            (int64_t) e->off, "export %s has no declaration (no contract given)", e->name);
                continue;
            }
            if (e->cut) {
                zau_add(r, ZAU_C_DECLARED, ZAU_ERROR, "UNDECLARED", e->name, (int64_t) e->idx, (int64_t) e->off,
                        "export %s... has a name longer than a contract name can be", e->name);
                continue;
            }
            if (binds(r, &m, e, c)) {
                bound[nbound].e = e;
                bound[nbound].fidx = e->idx;
                nbound++;
            }
        }
        if (c) {
            uint32_t nf = 0;
            (void) zc_fn_count(c, &nf);
            for (uint32_t i = 0; i < nf; i++) {
                const char *fn = NULL;
                int found = 0;
                if (zc_fn_name(c, i, &fn) != ZXV_OK) continue;
                for (uint32_t k = 0; k < m.nexports; k++)
                    if (m.exports[k].kind == 0 && strcmp(m.exports[k].name, fn) == 0) found = 1;
                if (!found)
                    zau_add(r, ZAU_C_DECLARED, ZAU_WARNING, "NOT_EXPORTED", fn, -1, -1,
                            "the contract declares %s but the module does not export it", fn);
            }
            if (!r->checks[ZAU_C_DECLARED].n[ZAU_ERROR])
                zau_summary(r, ZAU_C_DECLARED, "%u exported function(s), all declared", (unsigned) nfx);
        } else if (contract) {
            zau_skip(r, ZAU_C_DECLARED, "the contract does not parse");
        }
    }

    /* 6. bounds */
    if (!c) {
        zau_skip(r, ZAU_C_BOUNDS, "no contract");
    } else {
        uint32_t slots = 0;
        check_bounds_all(r, c, &slots);
        for (uint32_t i = 0; i < nbound; i++) bounds_export(r, &m, &bound[i], c);
        if (!r->checks[ZAU_C_BOUNDS].n[ZAU_ERROR] && !r->checks[ZAU_C_BOUNDS].n[ZAU_WARNING])
            zau_summary(r, ZAU_C_BOUNDS, "%u declared slot(s) well formed", (unsigned) slots);
    }

    /* 7. bounded analysis */
    if (options->flags & ZAU_F_NO_ANALYSIS) {
        zau_skip(r, ZAU_C_ANALYSIS, "turned off");
    } else if (!c || !m.parsed) {
        zau_skip(r, ZAU_C_ANALYSIS, !m.parsed ? "the module could not be read" : "no contract");
    } else if (r->checks[ZAU_C_VALIDATE].n[ZAU_ERROR]) {
        zau_skip(r, ZAU_C_ANALYSIS, "the validator refused the module");
    } else {
        for (uint32_t i = 0; i < nbound; i++)
            zau_analyze(r, &m, wasm, bound[i].e->name, bound[i].fidx, c, bound[i].e->name);
        if (!r->checks[ZAU_C_ANALYSIS].n[ZAU_ERROR] && !r->checks[ZAU_C_ANALYSIS].n[ZAU_WARNING])
            zau_summary(r, ZAU_C_ANALYSIS, "%u export(s) looked at; no problem found within the bounds",
                        (unsigned) nbound);
    }

    finish_report(r);
    free(bound);
    zc_contract_destroy(c);
    zau_module_free(&m);
    if (r->oom) {
        zau_report_destroy(r);
        return ZXV_E_NOMEM;
    }
    *out_report = r;
    return ZXV_OK;
}
