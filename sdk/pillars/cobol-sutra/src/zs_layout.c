/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zs_layout.c — the record layout both front ends build: validation,
 * sizes and offsets, the canonical Sutra text and digest, the copybook
 * printer, introspection and references ("LINE-QTY(2)"). Integer code
 * only (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zs_internal.h"

/* ===== builder ===== */

int32_t zs_builder_init(zs_builder *b) {
    b->l = (zs_layout *) calloc(1, sizeof *b->l);
    b->cap = 0;
    if (!b->l) return ZXV_E_NOMEM;
    b->l->overpunch = ZS_OVERPUNCH_IBM;
    return ZXV_OK;
}

void zs_builder_free(zs_builder *b) {
    zs_layout_destroy(b->l);
    b->l = NULL;
}

int32_t zs_builder_add(zs_builder *b, zs_item *item, uint32_t *out) {
    zs_layout *l = b->l;
    if (l->n >= ZS_MAX_FIELDS) return ZXV_E_CAPACITY;
    if (l->n == b->cap) {
        uint32_t nc = b->cap ? b->cap * 2u : 16u;
        zs_item *ni;
        if (nc > ZS_MAX_FIELDS) nc = ZS_MAX_FIELDS;
        ni = (zs_item *) realloc(l->items, (size_t) nc * sizeof *ni);
        if (!ni) return ZXV_E_NOMEM;
        l->items = ni;
        b->cap = nc;
    }
    l->items[l->n] = *item;
    if (l->n > 0) l->items[item->f.parent].nchild++;
    *out = l->n++;
    return ZXV_OK;
}

int zs_valid_name(const char *s, size_t n) {
    int letter = 0;
    if (n == 0 || n > ZS_MAX_NAME || s[0] == '-' || s[n - 1u] == '-') return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) letter = 1;
        else if (!(c >= '0' && c <= '9') && c != '-') return 0;
    }
    return letter;
}

static uint64_t elementary_size(const zs_item *it) {
    const zs_field *f = &it->f;
    if (f->kind == ZS_K_TEXT) return f->digits;
    switch (f->usage) {
    case ZS_U_COMP: return f->digits <= 4u ? 2u : f->digits <= 9u ? 4u : 8u;
    case ZS_U_COMP3: return f->digits / 2u + 1u;
    default:
        return (uint64_t) f->digits +
               ((f->sign == ZS_SIGN_LEADING_SEP || f->sign == ZS_SIGN_TRAILING_SEP) ? 1u : 0u);
    }
}

static const char *usage_word(int32_t u) {
    return u == ZS_U_COMP ? "COMP" : u == ZS_U_COMP3 ? "COMP-3" : "DISPLAY";
}

static uint32_t name_hash(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (uint8_t) *s) * 16777619u;
    return h;
}

#define FAIL_AT(it, st, ...) \
    zs_diag_set(d, (st), text, len, (it)->pos.offset, __VA_ARGS__)

int32_t zs_builder_finish(zs_builder *b, const char *text, size_t len, zs_diag *d,
                          zs_layout **out) {
    zs_layout *l = b->l;
    zs_item *it = l->items;
    uint64_t *cursor = NULL;
    uint32_t *slots = NULL;
    int32_t st = ZXV_OK;
    const uint32_t nslots = 8192u;

    if (l->n == 0) return zs_diag_set(d, ZS_E_SYNTAX, text, len, len, "no record declared");
    if (it[0].f.occurs) return FAIL_AT(&it[0], ZS_E_SYNTAX, "a record (01) cannot have OCCURS");
    if (it[0].f.kind != ZS_K_GROUP)
        return FAIL_AT(&it[0], ZS_E_UNSUPPORTED,
                       "an elementary 01 (a record with a PIC) is not supported; put its field "
                       "under a 01 group");

    /* 1. USAGE: a group's USAGE applies to every item under it. */
    for (uint32_t i = 1; i < l->n; i++) {
        zs_item *p = &it[it[i].f.parent];
        if (p->usage_set) {
            if (!it[i].usage_set) {
                it[i].f.usage = p->f.usage;
                it[i].usage_set = 1;
            } else if (it[i].f.usage != p->f.usage) {
                return FAIL_AT(&it[i], ZS_E_SYNTAX, "%s is %s inside a %s group", it[i].f.name,
                               usage_word(it[i].f.usage), usage_word(p->f.usage));
            }
        }
    }
    /* 2. per-item rules; the sign follows from the picture, the SIGN
     * clause and the (possibly inherited) usage */
    for (uint32_t i = 0; i < l->n; i++) {
        zs_item *x = &it[i];
        zs_field *f = &x->f;
        if (f->kind == ZS_K_GROUP && x->nchild == 0)
            return FAIL_AT(x, ZS_E_SYNTAX, "group %s has no items: give it a PIC or items under it",
                           f->name);
        if (f->kind != ZS_K_NUM && x->sign_clause)
            return FAIL_AT(x, ZS_E_SYNTAX, "a SIGN clause needs a numeric item (%s)", f->name);
        if (f->kind == ZS_K_TEXT && f->usage != ZS_U_DISPLAY)
            return FAIL_AT(x, ZS_E_SYNTAX, "PIC X item %s cannot be %s", f->name,
                           usage_word(f->usage));
        if (f->kind != ZS_K_NUM) {
            f->sign = ZS_SIGN_NONE;
            continue;
        }
        if (x->sign_clause && !x->is_signed)
            return FAIL_AT(x, ZS_E_SYNTAX, "a SIGN clause needs a signed picture (S9...) on %s",
                           f->name);
        if (x->sign_clause && x->sign_clause != ZS_SIGN_TRAILING && f->usage != ZS_U_DISPLAY)
            return FAIL_AT(x, ZS_E_SYNTAX, "SIGN LEADING or SEPARATE applies only to DISPLAY items (%s)",
                           f->name);
        f->sign = !x->is_signed ? ZS_SIGN_NONE : x->sign_clause ? x->sign_clause : ZS_SIGN_TRAILING;
    }
    /* 3. sizes, children before parents (reverse declaration order) */
    for (uint32_t i = l->n; i-- > 0;) {
        zs_field *f = &it[i].f;
        uint64_t mult = f->occurs ? f->occurs : 1u;
        if (f->kind != ZS_K_GROUP) f->size = elementary_size(&it[i]);
        if (f->size > ZS_MAX_RECORD || f->size * mult > ZS_MAX_RECORD)
            return FAIL_AT(&it[i], ZS_E_UNSUPPORTED, "%s is larger than 1048576 bytes", f->name);
        if (i > 0) {
            zs_field *p = &it[f->parent].f;
            p->size += f->size * mult;
            if (p->size > ZS_MAX_RECORD)
                return FAIL_AT(&it[f->parent], ZS_E_UNSUPPORTED, "%s is larger than 1048576 bytes",
                               p->name);
        }
    }
    l->size = it[0].f.size;
    /* 4. offsets and subscripts, parents before children */
    cursor = (uint64_t *) calloc(l->n, sizeof *cursor);
    slots = (uint32_t *) malloc(nslots * sizeof *slots);
    if (!cursor || !slots) {
        st = ZXV_E_NOMEM;
        goto out;
    }
    memset(slots, 0xFF, nslots * sizeof *slots);
    it[0].f.offset = 0;
    it[0].f.dims = 0;
    it[0].rel = 0;
    for (uint32_t i = 1; i < l->n; i++) {
        zs_field *f = &it[i].f, *p = &it[f->parent].f;
        it[i].rel = cursor[f->parent];
        cursor[f->parent] += f->size * (f->occurs ? f->occurs : 1u);
        f->offset = p->offset + it[i].rel;
        f->dims = p->dims + (f->occurs ? 1u : 0u);
        if (f->dims > ZS_MAX_DIMS) {
            st = FAIL_AT(&it[i], ZS_E_UNSUPPORTED, "more than 7 nested OCCURS");
            goto out;
        }
    }
    /* 5. names are unique (FILLER aside): no OF/IN qualification */
    for (uint32_t i = 0; i < l->n; i++) {
        uint32_t h;
        if (strcmp(it[i].f.name, "FILLER") == 0) continue;
        h = name_hash(it[i].f.name) & (nslots - 1u);
        while (slots[h] != UINT32_MAX) {
            if (strcmp(it[slots[h]].f.name, it[i].f.name) == 0) {
                st = FAIL_AT(&it[i], ZS_E_UNSUPPORTED,
                             "duplicate data name %s (qualified names, OF/IN, are not supported)",
                             it[i].f.name);
                goto out;
            }
            h = (h + 1u) & (nslots - 1u);
        }
        slots[h] = i;
    }
    /* groups carry no usage of their own once it has been handed down */
    for (uint32_t i = 0; i < l->n; i++)
        if (it[i].f.kind == ZS_K_GROUP) it[i].f.usage = ZS_U_DISPLAY;
    *out = l;
    b->l = NULL;
out:
    free(cursor);
    free(slots);
    return st;
}

void zs_layout_destroy(zs_layout *l) {
    if (!l) return;
    free(l->items);
    free(l);
}

/* ===== printing ===== */

typedef struct {
    char *p;
    size_t n, cap;
    int oom;
} sbuf;

static void sb_put(sbuf *s, const char *t, size_t k) {
    if (s->oom) return;
    if (s->n + k + 1u > s->cap) {
        size_t nc = s->cap ? s->cap : 256u;
        char *np;
        while (nc < s->n + k + 1u) nc *= 2u;
        np = (char *) realloc(s->p, nc);
        if (!np) {
            s->oom = 1;
            return;
        }
        s->p = np;
        s->cap = nc;
    }
    memcpy(s->p + s->n, t, k);
    s->n += k;
    s->p[s->n] = '\0';
}

static void sb_str(sbuf *s, const char *t) { sb_put(s, t, strlen(t)); }

static void sb_lower(sbuf *s, const char *t) {
    char b[ZS_MAX_NAME + 1];
    size_t k = strlen(t);
    for (size_t i = 0; i < k; i++) b[i] = (t[i] >= 'A' && t[i] <= 'Z') ? (char) (t[i] + 32) : t[i];
    sb_put(s, b, k);
}

/* "S9(13)V9(2)" / "X(12)", upper or lower case */
static void picture_text(const zs_field *f, int lower, char *b, size_t cap) {
    uint32_t ints = f->digits - f->scale;
    size_t k = 0;
    if (f->kind == ZS_K_TEXT) {
        snprintf(b, cap, "%c(%u)", lower ? 'x' : 'X', (unsigned) f->digits);
        return;
    }
    if (f->sign != ZS_SIGN_NONE) b[k++] = lower ? 's' : 'S';
    b[k] = '\0';
    if (ints) k += (size_t) snprintf(b + k, cap - k, "9(%u)", (unsigned) ints);
    if (f->scale) snprintf(b + k, cap - k, "%c9(%u)", lower ? 'v' : 'V', (unsigned) f->scale);
}

static const char *sign_text(int32_t sign) {
    switch (sign) {
    case ZS_SIGN_LEADING: return "sign leading";
    case ZS_SIGN_TRAILING_SEP: return "sign trailing separate";
    case ZS_SIGN_LEADING_SEP: return "sign leading separate";
    default: return NULL;
    }
}

static int32_t emit(sbuf *s, char *buf, size_t cap, size_t *out_len) {
    int32_t st;
    if (s->oom) {
        free(s->p);
        return ZXV_E_NOMEM;
    }
    st = zs_out_text(s->p ? s->p : "", s->n, buf, cap, out_len);
    free(s->p);
    return st;
}

static void sutra_text(const zs_layout *l, sbuf *s) {
    sb_str(s, "record ");
    sb_lower(s, l->items[0].f.name);
    sb_str(s, "\n");
    for (uint32_t i = 1; i < l->n; i++) {
        const zs_field *f = &l->items[i].f;
        char t[64];
        for (uint32_t k = 0; k < f->depth; k++) sb_str(s, "  ");
        sb_lower(s, f->name);
        if (f->occurs) {
            snprintf(t, sizeof t, "[%u]", (unsigned) f->occurs);
            sb_str(s, t);
        }
        sb_str(s, ":");
        if (f->kind != ZS_K_GROUP) {
            const char *sg = sign_text(f->sign);
            picture_text(f, 1, t, sizeof t);
            sb_str(s, " ");
            sb_str(s, t);
            if (f->usage == ZS_U_COMP) sb_str(s, " comp");
            if (f->usage == ZS_U_COMP3) sb_str(s, " comp-3");
            if (sg) {
                sb_str(s, " ");
                sb_str(s, sg);
            }
        }
        sb_str(s, "\n");
    }
}

int32_t zs_layout_sutra(const zs_layout *l, char *buf, size_t cap, size_t *out_len) {
    sbuf s = {0};
    if (!l || !out_len) return ZXV_E_ARG;
    sutra_text(l, &s);
    return emit(&s, buf, cap, out_len);
}

/* fixed format: columns 1-7 blank, code in 8..72 */
typedef struct {
    sbuf *s;
    size_t col;   /* 1-based column of the next character */
    size_t cont;  /* indent column for a continued entry  */
} cbw;

static void cb_word(cbw *w, const char *t) {
    size_t k = strlen(t);
    if (w->col > w->cont && w->col + 1u + k > 73u) {
        sb_str(w->s, "\n");
        w->col = 1;
    }
    if (w->col == 1) {
        for (size_t i = 1; i < w->cont; i++) sb_str(w->s, " ");
        w->col = w->cont;
    } else {
        sb_str(w->s, " ");
        w->col++;
    }
    sb_put(w->s, t, k);
    w->col += k;
}

/* the period that ends an entry, kept inside column 72 */
static void cb_end(cbw *w) {
    if (w->col > 72u) {
        sb_str(w->s, "\n");
        for (size_t i = 1; i < w->cont; i++) sb_str(w->s, " ");
    }
    sb_str(w->s, ".\n");
}

int32_t zs_layout_copybook(const zs_layout *l, char *buf, size_t cap, size_t *out_len) {
    sbuf s = {0};
    uint32_t maxd = 0;
    if (!l || !out_len) return ZXV_E_ARG;
    for (uint32_t i = 0; i < l->n; i++)
        if (l->items[i].f.depth > maxd) maxd = l->items[i].f.depth;
    sb_str(&s, "      * generated by libzsutra from the canonical layout\n");
    for (uint32_t i = 0; i < l->n; i++) {
        const zs_field *f = &l->items[i].f;
        uint32_t lv = f->depth == 0 ? 1u : maxd <= 9u ? f->depth * 5u : f->depth + 1u;
        uint32_t ind = f->depth < 6u ? f->depth : 6u;
        cbw w = {&s, 1, 8u + 4u * ind};
        char t[64];
        snprintf(t, sizeof t, "%02u", (unsigned) lv);
        cb_word(&w, t);
        w.cont = 12u + 4u * ind;
        cb_word(&w, f->name);
        if (f->kind != ZS_K_GROUP) {
            cb_word(&w, "PIC");
            picture_text(f, 0, t, sizeof t);
            cb_word(&w, t);
            if (f->usage != ZS_U_DISPLAY) cb_word(&w, usage_word(f->usage));
            if (f->sign == ZS_SIGN_LEADING || f->sign == ZS_SIGN_LEADING_SEP) {
                cb_word(&w, "SIGN");
                cb_word(&w, "LEADING");
            } else if (f->sign == ZS_SIGN_TRAILING_SEP) {
                cb_word(&w, "SIGN");
                cb_word(&w, "TRAILING");
            }
            if (f->sign == ZS_SIGN_LEADING_SEP || f->sign == ZS_SIGN_TRAILING_SEP)
                cb_word(&w, "SEPARATE");
        }
        if (f->occurs) {
            snprintf(t, sizeof t, "%u", (unsigned) f->occurs);
            cb_word(&w, "OCCURS");
            cb_word(&w, t);
        }
        cb_end(&w);
    }
    return emit(&s, buf, cap, out_len);
}

int32_t zs_layout_digest(const zs_layout *l, uint8_t out[32]) {
    sbuf s = {0};
    int32_t st;
    if (!l || !out) return ZXV_E_ARG;
    sutra_text(l, &s);
    if (s.oom) {
        free(s.p);
        return ZXV_E_NOMEM;
    }
    st = zxv_sha3_256((const uint8_t *) s.p, s.n, out);
    free(s.p);
    return st;
}

int32_t zs_layout_same(const zs_layout *a, const zs_layout *b, int32_t *out_same) {
    uint8_t da[32], db[32];
    int32_t st;
    if (!a || !b || !out_same) return ZXV_E_ARG;
    if ((st = zs_layout_digest(a, da)) != ZXV_OK) return st;
    if ((st = zs_layout_digest(b, db)) != ZXV_OK) return st;
    *out_same = memcmp(da, db, 32) == 0;
    return ZXV_OK;
}

int32_t zs_layout_size(const zs_layout *l, uint64_t *out_bytes) {
    if (!l || !out_bytes) return ZXV_E_ARG;
    *out_bytes = l->size;
    return ZXV_OK;
}

int32_t zs_layout_set_overpunch(zs_layout *l, int32_t style) {
    if (!l || (style != ZS_OVERPUNCH_IBM && style != ZS_OVERPUNCH_ASCII)) return ZXV_E_ARG;
    l->overpunch = style;
    return ZXV_OK;
}

int32_t zs_field_count(const zs_layout *l, uint32_t *out_count) {
    if (!l || !out_count) return ZXV_E_ARG;
    *out_count = l->n;
    return ZXV_OK;
}

int32_t zs_field_at(const zs_layout *l, uint32_t index, zs_field *out_field) {
    if (!l || !out_field) return ZXV_E_ARG;
    if (index >= l->n) return ZXV_E_NOT_FOUND;
    *out_field = l->items[index].f;
    return ZXV_OK;
}

/* ===== references ===== */

int32_t zs_find(const zs_layout *l, const char *ref, uint32_t *out_index, uint64_t *out_offset) {
    char name[ZS_MAX_NAME + 1];
    uint32_t subs[ZS_MAX_DIMS], nsubs = 0, idx = UINT32_MAX;
    size_t k = 0;
    const char *p = ref;
    uint32_t chain[ZS_MAX_DIMS], nchain = 0;
    uint64_t off;
    if (!l || !ref) return ZXV_E_ARG;
    while (*p == ' ') p++;
    while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
           *p == '-') {
        if (k >= ZS_MAX_NAME) {
            zs_set_errorf("reference '%.40s': name longer than 30 characters", ref);
            return ZXV_E_FORMAT;
        }
        name[k++] = (*p >= 'a' && *p <= 'z') ? (char) (*p - 32) : *p;
        p++;
    }
    name[k] = '\0';
    while (*p == ' ') p++;
    if (*p == '(') {
        p++;
        for (;;) {
            uint64_t v = 0;
            int digits = 0;
            while (*p == ' ' || *p == ',') p++;
            if (*p == ')') break;
            while (*p >= '0' && *p <= '9') {
                v = v * 10u + (uint64_t) (*p++ - '0');
                if (v > ZS_MAX_OCCURS) v = (uint64_t) ZS_MAX_OCCURS + 1u;
                digits = 1;
            }
            if (!digits || nsubs >= ZS_MAX_DIMS) {
                zs_set_errorf("reference '%.60s': expected subscripts like (2) or (1, 3)", ref);
                return ZXV_E_FORMAT;
            }
            subs[nsubs++] = (uint32_t) v;
            if (*p != ' ' && *p != ',' && *p != ')') {
                zs_set_errorf("reference '%.60s': expected subscripts like (2) or (1, 3)", ref);
                return ZXV_E_FORMAT;
            }
        }
        p++;
        while (*p == ' ') p++;
    }
    if (*p != '\0' || k == 0) {
        zs_set_errorf("reference '%.60s': expected NAME or NAME(subscripts)", ref);
        return ZXV_E_FORMAT;
    }
    if (strcmp(name, "FILLER") != 0)
        for (uint32_t i = 0; i < l->n; i++)
            if (strcmp(l->items[i].f.name, name) == 0) {
                idx = i;
                break;
            }
    if (idx == UINT32_MAX) {
        zs_set_errorf("no data item named %s", name);
        return ZXV_E_NOT_FOUND;
    }
    if (nsubs != l->items[idx].f.dims) {
        zs_set_errorf("%s needs %u subscript(s), %u given", name, (unsigned) l->items[idx].f.dims,
                      (unsigned) nsubs);
        return ZXV_E_RANGE;
    }
    /* the OCCURS items from this one up, then outermost first */
    for (uint32_t i = idx;; i = l->items[i].f.parent) {
        if (l->items[i].f.occurs) chain[nchain++] = i;
        if (i == 0) break;
    }
    off = l->items[idx].f.offset;
    for (uint32_t s = 0; s < nsubs; s++) {
        const zs_field *f = &l->items[chain[nchain - 1u - s]].f;
        if (subs[s] < 1u || subs[s] > f->occurs) {
            zs_set_errorf("subscript %u of %s is %u; %s OCCURS %u", (unsigned) (s + 1u), name,
                          (unsigned) subs[s], f->name, (unsigned) f->occurs);
            return ZXV_E_RANGE;
        }
        off += (uint64_t) (subs[s] - 1u) * f->size;
    }
    if (out_index) *out_index = idx;
    if (out_offset) *out_offset = off;
    return ZXV_OK;
}
