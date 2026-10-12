/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ze_pkg.c — packages of types: construction, the declaration rules both
 * front ends share, encoding widths and sizes, and the public accessors.
 *
 * Every declaration goes through ze_decl_* here, whichever syntax it came
 * from, so an Ada spec and an Eve file are checked by the same code and
 * build the same ze_titem. Integer code only (R11). */
#include <stdlib.h>
#include <string.h>

#include "ze_internal.h"

void ze_scalar_width(int64_t lo, int64_t hi, uint32_t *width, int32_t *is_signed) {
    if (lo >= 0) {
        *is_signed = 0;
        *width = hi <= 0xFF ? 1u : hi <= 0xFFFF ? 2u : hi <= 0xFFFFFFFFLL ? 4u : 8u;
    } else {
        *is_signed = 1;
        *width = (lo >= -128 && hi <= 127)               ? 1u
                 : (lo >= -32768 && hi <= 32767)         ? 2u
                 : (lo >= -2147483648LL && hi <= 2147483647LL) ? 4u
                                                         : 8u;
    }
}

int32_t ze_pkg_add_literal(ze_pkg *p, const char *name, uint32_t *out) {
    if (p->nlits >= ZE_MAX_LITERALS) {
        ze_set_error("more than 65536 enumeration literals in one package");
        return ZE_E_LIMIT;
    }
    if (p->nlits == p->cap_lits) {
        uint32_t nc = p->cap_lits ? p->cap_lits * 2u : 32u;
        char(*nl)[ZE_MAX_NAME + 1] = realloc(p->lits, (size_t) nc * sizeof *nl);
        if (!nl) return ZXV_E_NOMEM;
        p->lits = nl;
        p->cap_lits = nc;
    }
    strcpy(p->lits[p->nlits], name);
    *out = p->nlits++;
    return ZXV_OK;
}

int32_t ze_pkg_add_component(ze_pkg *p, const char *name, uint32_t type, uint32_t *out) {
    if (p->ncomps >= ZE_MAX_COMPS) {
        ze_set_error("more than 16384 record components in one package");
        return ZE_E_LIMIT;
    }
    if (p->ncomps == p->cap_comps) {
        uint32_t nc = p->cap_comps ? p->cap_comps * 2u : 32u;
        ze_component *n2 = (ze_component *) realloc(p->comps, (size_t) nc * sizeof *n2);
        if (!n2) return ZXV_E_NOMEM;
        p->comps = n2;
        p->cap_comps = nc;
    }
    memset(&p->comps[p->ncomps], 0, sizeof p->comps[0]);
    strcpy(p->comps[p->ncomps].name, name);
    p->comps[p->ncomps].type = type;
    *out = p->ncomps++;
    return ZXV_OK;
}

int32_t ze_pkg_add_type(ze_pkg *p, ze_titem *it, uint32_t *out) {
    ze_type *t = &it->t;
    if (p->ntypes >= ZE_MAX_TYPES) {
        ze_set_error("more than 4096 types (named and anonymous) in one package");
        return ZE_E_LIMIT;
    }
    switch (t->kind) {
    case ZE_K_INT:
    case ZE_K_DEC:
    case ZE_K_ENUM:
        ze_scalar_width(t->lo, t->hi, &it->width, &it->is_signed);
        t->size = it->width;
        break;
    case ZE_K_TEXT: t->size = t->count; break;
    case ZE_K_ARRAY: {
        uint64_t n = 1, es = p->types[t->elem].t.size;
        for (uint32_t d = 0; d < t->dims; d++) {
            uint64_t ext = (uint64_t) (it->dim[d].hi - it->dim[d].lo) + 1u;
            if (ext > ZE_MAX_SIZE || n * ext > ZE_MAX_SIZE) {
                ze_set_error("the array has more than 2**20 elements");
                return ZE_E_LIMIT;
            }
            n *= ext;
        }
        if (n * es > ZE_MAX_SIZE) {
            ze_set_error("the array's encoding is larger than 1 MiB");
            return ZE_E_LIMIT;
        }
        t->count = (uint32_t) n;
        t->size = n * es;
        break;
    }
    default: break; /* RECORD: ze_pkg_finish_record, or copied for a subtype */
    }
    if (p->ntypes == p->cap_types) {
        uint32_t nc = p->cap_types ? p->cap_types * 2u : 32u;
        ze_titem *n2 = (ze_titem *) realloc(p->types, (size_t) nc * sizeof *n2);
        if (!n2) return ZXV_E_NOMEM;
        p->types = n2;
        p->cap_types = nc;
    }
    if (!t->is_subtype) t->base = p->ntypes;
    p->types[p->ntypes] = *it;
    *out = p->ntypes++;
    return ZXV_OK;
}

int32_t ze_pkg_finish_record(ze_pkg *p, uint32_t type) {
    ze_titem *it = &p->types[type];
    uint64_t off = 0;
    for (uint32_t i = 0; i < it->t.count; i++) {
        ze_component *c = &p->comps[it->comp0 + i];
        c->offset = off;
        off += p->types[c->type].t.size;
        if (off > ZE_MAX_SIZE) {
            ze_set_error("the record's encoding is larger than 1 MiB");
            return ZE_E_LIMIT;
        }
    }
    it->t.size = off;
    return ZXV_OK;
}

int32_t ze_pkg_new(ze_pkg **out) {
    ze_pkg *p = (ze_pkg *) calloc(1, sizeof *p);
    ze_titem b;
    uint32_t idx, l0, l1;
    if (!p) return ZXV_E_NOMEM;
    memset(&b, 0, sizeof b);
    strcpy(b.t.name, "boolean");
    b.t.kind = ZE_K_ENUM;
    b.t.lo = 0;
    b.t.hi = 1;
    b.t.count = 2;
    if (ze_pkg_add_literal(p, "false", &l0) != ZXV_OK || ze_pkg_add_literal(p, "true", &l1) != ZXV_OK ||
        ze_pkg_add_type(p, &b, &idx) != ZXV_OK) {
        ze_pkg_destroy(p);
        return ZXV_E_NOMEM;
    }
    *out = p;
    return ZXV_OK;
}

void ze_pkg_destroy(ze_pkg *p) {
    if (!p) return;
    free(p->types);
    free(p->comps);
    free(p->lits);
    free(p);
}

uint32_t ze_pkg_lookup(const ze_pkg *p, const char *lower) {
    for (uint32_t i = 0; i < p->ntypes; i++)
        if (p->types[i].t.name[0] && strcmp(p->types[i].t.name, lower) == 0) return i;
    return UINT32_MAX;
}

int ze_pkg_is_literal(const ze_pkg *p, const char *lower) {
    for (uint32_t i = 2; i < p->nlits; i++)
        if (strcmp(p->lits[i], lower) == 0) return 1;
    return 0;
}

/* ---- declarations ---- */

int32_t ze_check_new_name(ze_lexer *lx, const ze_pkg *p, const ze_tok *name, char *lower) {
    const char *s = lx->text + name->off;
    if (name->kind != ZT_ID) return ze_lex_error_at(lx, name, "a name");
    ze_lower(lower, s, name->len);
    if (ze_reserved(s, name->len))
        return ZE_ERR(lx, ZE_E_NAME, name->off, "'%s' is an Ada reserved word", lower);
    if (ze_predefined(s, name->len))
        return ZE_ERR(lx, ZE_E_NAME, name->off, "'%s' is a predefined name and cannot be declared here", lower);
    if (strcmp(lower, p->name) == 0)
        return ZE_ERR(lx, ZE_E_NAME, name->off, "'%s' is the package's own name", lower);
    if (ze_pkg_lookup(p, lower) != UINT32_MAX)
        return ZE_ERR(lx, ZE_E_NAME, name->off, "'%s' is declared twice", lower);
    if (ze_pkg_is_literal(p, lower))
        return ZE_ERR(lx, ZE_E_NAME, name->off, "'%s' is already an enumeration literal", lower);
    return ZXV_OK;
}

static int32_t add(ze_lexer *lx, ze_pkg *p, ze_titem *it, uint32_t *out) {
    int32_t st = ze_pkg_add_type(p, it, out);
    if (st == ZE_E_LIMIT) {
        char why[256];
        size_t n = 0;
        if (ze_last_error(why, sizeof why, &n) != ZXV_OK) why[0] = '\0';
        return ZE_ERR(lx, st, it->at, "%s", why);
    }
    return st;
}

static void init_item(ze_titem *it, const char *name, int32_t kind, uint64_t at) {
    memset(it, 0, sizeof *it);
    strcpy(it->t.name, name);
    it->t.kind = kind;
    it->at = at;
}

int32_t ze_decl_int(ze_lexer *lx, ze_pkg *p, const char *name, int64_t lo, int64_t hi, uint64_t at,
                    uint32_t *out) {
    ze_titem it;
    if (lo > hi) return ZE_ERR(lx, ZE_E_CONSTRAINT, at, "the range is empty (null ranges are not supported)");
    init_item(&it, name, ZE_K_INT, at);
    it.t.lo = lo;
    it.t.hi = hi;
    return add(lx, p, &it, out);
}

int32_t ze_decl_dec(ze_lexer *lx, ze_pkg *p, const char *name, uint32_t digits, uint32_t scale,
                    int has_range, int64_t lo, int64_t hi, uint64_t at, uint32_t *out) {
    ze_titem it;
    int64_t full;
    if (digits < 1 || digits > ZE_MAX_DIGITS)
        return ZE_ERR(lx, ZE_E_CONSTRAINT, at, "digits must be 1 .. 18 (the value is an int64)");
    if (scale > ZE_MAX_DIGITS) return ZE_ERR(lx, ZE_E_CONSTRAINT, at, "the delta must be 1.0 .. 1.0E-18");
    full = ze_pow10[digits] - 1;
    if (has_range) {
        if (lo > hi) return ZE_ERR(lx, ZE_E_CONSTRAINT, at, "the range is empty (null ranges are not supported)");
        if (lo < -full || hi > full)
            return ZE_ERR(lx, ZE_E_CONSTRAINT, at, "the range does not fit %u digits", (unsigned) digits);
        if (lo == -full && hi == full) has_range = 0;
    }
    init_item(&it, name, ZE_K_DEC, at);
    it.t.digits = digits;
    it.t.scale = scale;
    it.t.lo = has_range ? lo : -full;
    it.t.hi = has_range ? hi : full;
    it.has_range = has_range;
    return add(lx, p, &it, out);
}

int32_t ze_decl_text(ze_lexer *lx, ze_pkg *p, const char *name, int64_t n, uint64_t at, uint32_t *out) {
    ze_titem it;
    if (n < 1) return ZE_ERR(lx, ZE_E_CONSTRAINT, at, "a String needs at least one character");
    if (n > (int64_t) ZE_MAX_STRING) return ZE_ERR(lx, ZE_E_LIMIT, at, "a String longer than 65535 characters");
    init_item(&it, name, ZE_K_TEXT, at);
    it.t.count = (uint32_t) n;
    return add(lx, p, &it, out);
}

int32_t ze_decl_subtype(ze_lexer *lx, ze_pkg *p, const char *name, uint32_t base, int has_range,
                        int64_t lo, int64_t hi, uint64_t at, uint32_t *out) {
    ze_titem it = p->types[base];
    int32_t k = it.t.kind;
    if (has_range) {
        if (k != ZE_K_INT && k != ZE_K_DEC && k != ZE_K_ENUM)
            return ZE_ERR(lx, ZE_E_CONSTRAINT, at, "only a scalar type takes a range constraint");
        if (lo > hi) return ZE_ERR(lx, ZE_E_CONSTRAINT, at, "the range is empty (null ranges are not supported)");
        if (lo < it.t.lo || hi > it.t.hi)
            return ZE_ERR(lx, ZE_E_CONSTRAINT, at, "the range is outside the range of %s",
                          it.t.name[0] ? it.t.name : "its base");
        if (lo == it.t.lo && hi == it.t.hi) has_range = 0;
    }
    strcpy(it.t.name, name);
    it.t.is_subtype = 1;
    it.t.base = base;
    it.has_range = has_range;
    it.at = at;
    if (has_range) {
        it.t.lo = lo;
        it.t.hi = hi;
    }
    return add(lx, p, &it, out);
}

int32_t ze_decl_array(ze_lexer *lx, ze_pkg *p, const char *name, const ze_dim *dims, uint32_t ndims,
                      uint32_t elem, uint64_t at, uint32_t *out) {
    ze_titem it;
    if (ndims < 1 || ndims > ZE_MAX_DIMS)
        return ZE_ERR(lx, ZE_E_LIMIT, at, "an array has 1 .. 4 dimensions");
    init_item(&it, name, ZE_K_ARRAY, at);
    for (uint32_t d = 0; d < ndims; d++) {
        if (dims[d].lo > dims[d].hi)
            return ZE_ERR(lx, ZE_E_CONSTRAINT, at, "dimension %u is empty (null ranges are not supported)",
                          (unsigned) d + 1u);
        it.dim[d] = dims[d];
    }
    it.t.dims = ndims;
    it.t.elem = elem;
    return add(lx, p, &it, out);
}

int32_t ze_enum_pos(const ze_pkg *p, uint32_t base, const char *lower, uint32_t *out_pos) {
    const ze_titem *it = &p->types[base];
    for (uint32_t i = 0; i < it->t.count; i++)
        if (strcmp(p->lits[it->lit0 + i], lower) == 0) {
            *out_pos = i;
            return ZXV_OK;
        }
    return ZXV_E_NOT_FOUND;
}

/* ---- public accessors ---- */

int32_t ze_pkg_name(const ze_pkg *p, char *buf, size_t cap, size_t *out_len) {
    if (!p) return ZXV_E_ARG;
    return ze_out_text(p->name, strlen(p->name), buf, cap, out_len);
}

int32_t ze_type_count(const ze_pkg *p, uint32_t *out) {
    if (!p || !out) return ZXV_E_ARG;
    *out = p->ntypes;
    return ZXV_OK;
}

int32_t ze_type_at(const ze_pkg *p, uint32_t index, ze_type *out) {
    if (!p || !out) return ZXV_E_ARG;
    if (index >= p->ntypes) return ZXV_E_NOT_FOUND;
    *out = p->types[index].t;
    return ZXV_OK;
}

int32_t ze_type_find(const ze_pkg *p, const char *name, uint32_t *out) {
    char low[ZE_MAX_NAME + 1];
    size_t n;
    uint32_t i;
    if (!p || !name || !out) return ZXV_E_ARG;
    n = strlen(name);
    if (n == 0 || n > ZE_MAX_NAME) return ZXV_E_NOT_FOUND;
    ze_lower(low, name, n);
    i = ze_pkg_lookup(p, low);
    if (i == UINT32_MAX) {
        ze_set_errorf("no type named '%s'", low);
        return ZXV_E_NOT_FOUND;
    }
    *out = i;
    return ZXV_OK;
}

int32_t ze_array_dim(const ze_pkg *p, uint32_t type, uint32_t dim, ze_dim *out) {
    if (!p || !out || type >= p->ntypes || p->types[type].t.kind != ZE_K_ARRAY) return ZXV_E_ARG;
    if (dim >= p->types[type].t.dims) return ZXV_E_NOT_FOUND;
    *out = p->types[type].dim[dim];
    return ZXV_OK;
}

int32_t ze_component_at(const ze_pkg *p, uint32_t type, uint32_t index, ze_component *out) {
    if (!p || !out || type >= p->ntypes || p->types[type].t.kind != ZE_K_RECORD) return ZXV_E_ARG;
    if (index >= p->types[type].t.count) return ZXV_E_NOT_FOUND;
    *out = p->comps[p->types[type].comp0 + index];
    return ZXV_OK;
}

int32_t ze_enum_literal(const ze_pkg *p, uint32_t type, uint32_t pos, char *buf, size_t cap,
                        size_t *out_len) {
    const ze_titem *it;
    if (!p || type >= p->ntypes || p->types[type].t.kind != ZE_K_ENUM) return ZXV_E_ARG;
    it = &p->types[type];
    if (pos >= it->t.count) return ZXV_E_NOT_FOUND;
    return ze_out_text(p->lits[it->lit0 + pos], strlen(p->lits[it->lit0 + pos]), buf, cap, out_len);
}

/* ---- enumerations and records ---- */

int32_t ze_decl_literal(ze_lexer *lx, ze_pkg *p, uint32_t lit0, const ze_tok *name) {
    char low[ZE_MAX_NAME + 1];
    uint32_t idx;
    int32_t st;
    if (name->kind == ZT_CHAR)
        return ZE_ERR(lx, ZE_E_UNSUPPORTED, name->off, "character literals in an enumeration are not supported");
    if (name->kind != ZT_ID) return ze_lex_error_at(lx, name, "an enumeration literal");
    ze_lower(low, lx->text + name->off, name->len);
    if (ze_reserved(low, name->len)) return ZE_ERR(lx, ZE_E_NAME, name->off, "'%s' is an Ada reserved word", low);
    if (ze_predefined(low, name->len))
        return ZE_ERR(lx, ZE_E_NAME, name->off, "'%s' is a predefined name and cannot be a literal here", low);
    if (ze_pkg_lookup(p, low) != UINT32_MAX || strcmp(low, p->name) == 0)
        return ZE_ERR(lx, ZE_E_NAME, name->off, "'%s' is already the name of a type or the package", low);
    for (uint32_t i = lit0; i < p->nlits; i++)
        if (strcmp(p->lits[i], low) == 0)
            return ZE_ERR(lx, ZE_E_NAME, name->off, "the literal '%s' appears twice", low);
    if (p->nlits - lit0 >= ZE_MAX_ENUM)
        return ZE_ERR(lx, ZE_E_LIMIT, name->off, "an enumeration has at most 4096 literals");
    st = ze_pkg_add_literal(p, low, &idx);
    if (st == ZE_E_LIMIT) return ZE_ERR(lx, st, name->off, "more than 65536 enumeration literals in one package");
    return st;
}

int32_t ze_decl_enum(ze_lexer *lx, ze_pkg *p, const char *name, uint32_t lit0, uint64_t at, uint32_t *out) {
    ze_titem it;
    if (p->nlits == lit0) return ZE_ERR(lx, ZE_E_SYNTAX, at, "an enumeration needs at least one literal");
    init_item(&it, name, ZE_K_ENUM, at);
    it.lit0 = lit0;
    it.t.count = p->nlits - lit0;
    it.t.lo = 0;
    it.t.hi = (int64_t) it.t.count - 1;
    return add(lx, p, &it, out);
}

int32_t ze_decl_component(ze_lexer *lx, ze_pkg *p, uint32_t comp0, const ze_tok *name, uint32_t type) {
    char low[ZE_MAX_NAME + 1];
    uint32_t idx;
    int32_t st;
    if (name->kind != ZT_ID) return ze_lex_error_at(lx, name, "a component name");
    ze_lower(low, lx->text + name->off, name->len);
    if (ze_reserved(low, name->len)) return ZE_ERR(lx, ZE_E_NAME, name->off, "'%s' is an Ada reserved word", low);
    if (ze_predefined(low, name->len) || ze_pkg_lookup(p, low) != UINT32_MAX || ze_pkg_is_literal(p, low) ||
        strcmp(low, p->name) == 0)
        return ZE_ERR(lx, ZE_E_NAME, name->off,
                      "the component '%s' has the name of a type, a literal or the package", low);
    for (uint32_t i = comp0; i < p->ncomps; i++)
        if (strcmp(p->comps[i].name, low) == 0)
            return ZE_ERR(lx, ZE_E_NAME, name->off, "the component '%s' appears twice", low);
    st = ze_pkg_add_component(p, low, type, &idx);
    if (st == ZE_E_LIMIT) return ZE_ERR(lx, st, name->off, "more than 16384 record components in one package");
    return st;
}

int32_t ze_decl_record(ze_lexer *lx, ze_pkg *p, const char *name, uint32_t comp0, uint64_t at, uint32_t *out) {
    ze_titem it;
    int32_t st;
    if (p->ncomps == comp0) return ZE_ERR(lx, ZE_E_UNSUPPORTED, at, "a record needs at least one component");
    init_item(&it, name, ZE_K_RECORD, at);
    it.comp0 = comp0;
    it.t.count = p->ncomps - comp0;
    if ((st = add(lx, p, &it, out)) != ZXV_OK) return st;
    st = ze_pkg_finish_record(p, *out);
    if (st == ZE_E_LIMIT) {
        p->ntypes--;
        return ZE_ERR(lx, st, at, "the record's encoding is larger than 1 MiB");
    }
    return st;
}
