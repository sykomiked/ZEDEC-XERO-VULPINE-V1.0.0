/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ze_print.c — the canonical Eve text, its digest, and the Ada printer.
 *
 * Canonical Eve: "eve name", then one line per named type in declaration
 * order; lower-case names; integers in plain decimal; decimal bounds with
 * exactly the type's decimals; a range left out when it equals the base's
 * whole range; literal array indexes written "lo .. hi"; record
 * components one per line, indented two spaces; no comments. Integer code
 * only (R11). */
#include <stdlib.h>
#include <string.h>

#include "ze_internal.h"

/* "lo .. hi" of a scalar type in its base's terms. */
static void bounds(ze_sb *s, const ze_pkg *p, const ze_titem *it, int ada) {
    for (int k = 0; k < 2; k++) {
        int64_t v = k ? it->t.hi : it->t.lo;
        if (k) ze_sb_str(s, " .. ");
        if (it->t.kind == ZE_K_ENUM) {
            const char *lit = p->lits[it->lit0 + (uint32_t) v];
            if (ada) {
                char b[ZE_MAX_NAME + 1];
                ze_ada_case(b, lit);
                ze_sb_str(s, b);
            } else {
                ze_sb_str(s, lit);
            }
        } else if (it->t.kind == ZE_K_DEC) {
            ze_sb_dec(s, v, it->t.scale);
            if (ada && it->t.scale == 0) ze_sb_str(s, ".0");
        } else {
            ze_sb_i64(s, v);
        }
    }
}

static void name_of(ze_sb *s, const char *lower, int ada) {
    if (ada) {
        char b[ZE_MAX_NAME + 1];
        ze_ada_case(b, lower);
        ze_sb_str(s, b);
    } else {
        ze_sb_str(s, lower);
    }
}

/* How a type is written where it is used. */
static void use_of(ze_sb *s, const ze_pkg *p, uint32_t ty, int ada) {
    const ze_titem *it = &p->types[ty];
    if (it->t.name[0]) {
        name_of(s, it->t.name, ada);
        return;
    }
    if (it->t.kind == ZE_K_TEXT && !it->t.is_subtype) {
        ze_sb_str(s, ada ? "String (1 .. " : "str(");
        ze_sb_i64(s, it->t.count);
        ze_sb_str(s, ")");
        return;
    }
    /* an anonymous subtype: its base and, when it narrows, the range */
    use_of(s, p, it->t.base, ada);
    if (it->has_range) {
        ze_sb_str(s, ada ? " range " : " ");
        bounds(s, p, it, ada);
    }
}

static void dims_of(ze_sb *s, const ze_pkg *p, const ze_titem *it, int ada) {
    for (uint32_t d = 0; d < it->t.dims; d++) {
        if (d) ze_sb_str(s, ", ");
        if (it->dim[d].index == UINT32_MAX) {
            ze_sb_i64(s, it->dim[d].lo);
            ze_sb_str(s, " .. ");
            ze_sb_i64(s, it->dim[d].hi);
        } else {
            use_of(s, p, it->dim[d].index, ada);
        }
    }
}

static void delta_of(ze_sb *s, uint32_t scale) {
    if (scale == 0) {
        ze_sb_str(s, "1.0");
        return;
    }
    ze_sb_str(s, "0.");
    for (uint32_t k = 1; k < scale; k++) ze_sb_str(s, "0");
    ze_sb_str(s, "1");
}

static void print_eve(ze_sb *s, const ze_pkg *p) {
    ze_sb_str(s, "eve ");
    ze_sb_str(s, p->name);
    ze_sb_str(s, "\n");
    for (uint32_t i = 1; i < p->ntypes; i++) {
        const ze_titem *it = &p->types[i];
        if (!it->t.name[0]) continue;
        ze_sb_str(s, it->t.name);
        if (it->t.kind == ZE_K_RECORD && !it->t.is_subtype) {
            ze_sb_str(s, " {\n");
            for (uint32_t c = 0; c < it->t.count; c++) {
                const ze_component *cp = &p->comps[it->comp0 + c];
                ze_sb_str(s, "  ");
                ze_sb_str(s, cp->name);
                ze_sb_str(s, ": ");
                use_of(s, p, cp->type, 0);
                ze_sb_str(s, "\n");
            }
            ze_sb_str(s, "}\n");
            continue;
        }
        ze_sb_str(s, " = ");
        if (it->t.is_subtype) {
            use_of(s, p, it->t.base, 0);
            if (it->has_range) {
                ze_sb_str(s, " ");
                bounds(s, p, it, 0);
            }
        } else {
            switch (it->t.kind) {
            case ZE_K_INT: bounds(s, p, it, 0); break;
            case ZE_K_DEC:
                ze_sb_str(s, "dec(");
                ze_sb_i64(s, it->t.digits);
                ze_sb_str(s, ", ");
                ze_sb_i64(s, it->t.scale);
                ze_sb_str(s, ")");
                if (it->has_range) {
                    ze_sb_str(s, " ");
                    bounds(s, p, it, 0);
                }
                break;
            case ZE_K_ENUM:
                ze_sb_str(s, "(");
                for (uint32_t k = 0; k < it->t.count; k++) {
                    if (k) ze_sb_str(s, ", ");
                    ze_sb_str(s, p->lits[it->lit0 + k]);
                }
                ze_sb_str(s, ")");
                break;
            case ZE_K_TEXT:
                ze_sb_str(s, "str(");
                ze_sb_i64(s, it->t.count);
                ze_sb_str(s, ")");
                break;
            case ZE_K_ARRAY:
                ze_sb_str(s, "[");
                dims_of(s, p, it, 0);
                ze_sb_str(s, "] ");
                use_of(s, p, it->t.elem, 0);
                break;
            default: break;
            }
        }
        ze_sb_str(s, "\n");
    }
}

static void print_ada(ze_sb *s, const ze_pkg *p) {
    ze_sb_str(s, "package ");
    name_of(s, p->name, 1);
    ze_sb_str(s, " is\n");
    for (uint32_t i = 1; i < p->ntypes; i++) {
        const ze_titem *it = &p->types[i];
        if (!it->t.name[0]) continue;
        if (it->t.is_subtype || it->t.kind == ZE_K_TEXT) {
            ze_sb_str(s, "   subtype ");
            name_of(s, it->t.name, 1);
            ze_sb_str(s, " is ");
            if (it->t.is_subtype) {
                use_of(s, p, it->t.base, 1);
                if (it->has_range) {
                    ze_sb_str(s, " range ");
                    bounds(s, p, it, 1);
                }
            } else {
                ze_sb_str(s, "String (1 .. ");
                ze_sb_i64(s, it->t.count);
                ze_sb_str(s, ")");
            }
            ze_sb_str(s, ";\n");
            continue;
        }
        ze_sb_str(s, "   type ");
        name_of(s, it->t.name, 1);
        ze_sb_str(s, " is ");
        switch (it->t.kind) {
        case ZE_K_INT:
            ze_sb_str(s, "range ");
            bounds(s, p, it, 1);
            break;
        case ZE_K_DEC:
            ze_sb_str(s, "delta ");
            delta_of(s, it->t.scale);
            ze_sb_str(s, " digits ");
            ze_sb_i64(s, it->t.digits);
            if (it->has_range) {
                ze_sb_str(s, " range ");
                bounds(s, p, it, 1);
            }
            break;
        case ZE_K_ENUM:
            ze_sb_str(s, "(");
            for (uint32_t k = 0; k < it->t.count; k++) {
                if (k) ze_sb_str(s, ", ");
                name_of(s, p->lits[it->lit0 + k], 1);
            }
            ze_sb_str(s, ")");
            break;
        case ZE_K_ARRAY:
            ze_sb_str(s, "array (");
            dims_of(s, p, it, 1);
            ze_sb_str(s, ") of ");
            use_of(s, p, it->t.elem, 1);
            break;
        case ZE_K_RECORD:
            ze_sb_str(s, "record\n");
            for (uint32_t c = 0; c < it->t.count; c++) {
                const ze_component *cp = &p->comps[it->comp0 + c];
                ze_sb_str(s, "      ");
                name_of(s, cp->name, 1);
                ze_sb_str(s, " : ");
                use_of(s, p, cp->type, 1);
                ze_sb_str(s, ";\n");
            }
            ze_sb_str(s, "   end record");
            break;
        default: break;
        }
        ze_sb_str(s, ";\n");
    }
    ze_sb_str(s, "end ");
    name_of(s, p->name, 1);
    ze_sb_str(s, ";\n");
}

static int32_t emit(ze_sb *s, char *buf, size_t cap, size_t *out_len) {
    int32_t st;
    if (s->failed) {
        free(s->p);
        return ZXV_E_NOMEM;
    }
    st = ze_out_text(s->p ? s->p : "", s->n, buf, cap, out_len);
    free(s->p);
    return st;
}

int32_t ze_pkg_eve(const ze_pkg *p, char *buf, size_t cap, size_t *out_len) {
    ze_sb s = {0};
    if (!p || !out_len) return ZXV_E_ARG;
    print_eve(&s, p);
    return emit(&s, buf, cap, out_len);
}

int32_t ze_pkg_ada(const ze_pkg *p, char *buf, size_t cap, size_t *out_len) {
    ze_sb s = {0};
    if (!p || !out_len) return ZXV_E_ARG;
    print_ada(&s, p);
    return emit(&s, buf, cap, out_len);
}

int32_t ze_pkg_digest(const ze_pkg *p, uint8_t out[32]) {
    ze_sb s = {0};
    int32_t st;
    if (!p || !out) return ZXV_E_ARG;
    print_eve(&s, p);
    if (s.failed) {
        free(s.p);
        return ZXV_E_NOMEM;
    }
    st = zxv_sha3_256((const uint8_t *) (s.p ? s.p : ""), s.n, out);
    free(s.p);
    return st;
}

int32_t ze_pkg_same(const ze_pkg *a, const ze_pkg *b, int32_t *out_same) {
    uint8_t da[32], db[32];
    int32_t st;
    if (!a || !b || !out_same) return ZXV_E_ARG;
    if ((st = ze_pkg_digest(a, da)) != ZXV_OK) return st;
    if ((st = ze_pkg_digest(b, db)) != ZXV_OK) return st;
    *out_same = memcmp(da, db, 32) == 0;
    return ZXV_OK;
}
