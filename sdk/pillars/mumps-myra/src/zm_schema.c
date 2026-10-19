/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zm_schema.c — the canonical schema both front ends build: validation,
 * canonical MYRA text, the global-map printer, the digest and the field
 * accessors. Integer code only (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zm_internal.h"

zm_schema *zm_schema_new(void) { return (zm_schema *) calloc(1, sizeof(zm_schema)); }

void zm_schema_destroy(zm_schema *s) {
    if (!s) return;
    for (uint32_t i = 0; i < s->n; i++) free(s->nodes[i].f);
    free(s->nodes);
    free(s);
}

zm_node *zm_schema_add_node(zm_schema *s, int32_t *st) {
    zm_node *nd;
    if (s->n >= ZM_MAX_NODES) {
        *st = ZXV_E_CAPACITY;
        return NULL;
    }
    if (s->n == s->cap) {
        uint32_t nc = s->cap ? s->cap * 2u : 4u;
        zm_node *nn = (zm_node *) realloc(s->nodes, nc * sizeof *nn);
        if (!nn) {
            *st = ZXV_E_NOMEM;
            return NULL;
        }
        s->nodes = nn;
        s->cap = nc;
    }
    nd = &s->nodes[s->n++];
    memset(nd, 0, sizeof *nd);
    *st = ZXV_OK;
    return nd;
}

int32_t zm_node_add_field(zm_node *nd, const zm_item *it) {
    if (nd->nf >= ZM_MAX_SUBS + ZM_MAX_PIECES) return ZXV_E_CAPACITY;
    if (nd->nf == nd->fcap) {
        uint32_t nc = nd->fcap ? nd->fcap * 2u : 8u;
        zm_item *nf = (zm_item *) realloc(nd->f, nc * sizeof *nf);
        if (!nf) return ZXV_E_NOMEM;
        nd->f = nf;
        nd->fcap = nc;
    }
    nd->f[nd->nf++] = *it;
    return ZXV_OK;
}

int zm_valid_name(const char *s, size_t n) {
    int letter = 0;
    if (n == 0 || n > ZM_MAX_NAME) return 0;
    if (!((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z'))) return 0;
    if (s[n - 1u] == '-') return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) letter = 1;
        else if (!((c >= '0' && c <= '9') || c == '-')) return 0;
    }
    return letter;
}

int zm_valid_gname(const char *s, size_t n) {
    if (n == 0 || n > ZM_MAX_GNAME) return 0;
    if (!(s[0] == '%' || (s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z'))) return 0;
    for (size_t i = 1; i < n; i++) {
        char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
            return 0;
    }
    return 1;
}

void zm_lower(char *dst, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        dst[i] = (c >= 'A' && c <= 'Z') ? (char) (c + 32) : c;
    }
    dst[n] = '\0';
}

int zm_ieq(const char *a, const char *b) {
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x = (char) (x - 32);
        if (y >= 'a' && y <= 'z') y = (char) (y - 32);
        if (x != y) return 0;
        if (!x) return 1;
    }
}

const char *zm_type_check(zm_field *f) {
    switch (f->type) {
    case ZM_T_TEXT:
        if (f->len < 1u || f->len > ZM_MAX_TEXTLEN) return "TEXT(n) needs n in 1..32767";
        f->scale = 0;
        return NULL;
    case ZM_T_INT:
        if (f->len < 1u || f->len > ZM_MAX_DIGITS) return "INTEGER(n) needs n in 1..18";
        f->scale = 0;
        return NULL;
    case ZM_T_NUM:
        if (f->len < 1u || f->len > ZM_MAX_DIGITS) return "NUMBER(p,s) needs p in 1..18";
        if (f->scale > f->len) return "NUMBER(p,s) needs s <= p";
        if (f->scale == 0) f->type = ZM_T_INT; /* NUMBER(p,0) is INTEGER(p) */
        return NULL;
    case ZM_T_DATE:
        f->len = 0;
        f->scale = 0;
        return NULL;
    default: return "unknown type";
    }
}

int32_t zm_value_check(const zm_field *f, const char *s, size_t n, int64_t *out_v, char *msg,
                       size_t cap) {
    char sh[64];
    int64_t v = 0;
    int32_t st;
    msg[0] = '\0';
    if (f->type == ZM_T_TEXT) {
        if (n > f->len) {
            snprintf(msg, cap, "\"%s\" is %zu bytes; TEXT(%u) holds %u (never truncated)",
                     zm_show(s, n, sh, sizeof sh), n, (unsigned) f->len, (unsigned) f->len);
            return ZM_E_FIT;
        }
        if (zm_utf8_check((const uint8_t *) s, n) != n) {
            snprintf(msg, cap, "\"%s\" is not valid UTF-8", zm_show(s, n, sh, sizeof sh));
            return ZM_E_DATA;
        }
        for (size_t i = 0; i < n; i++) {
            unsigned char c = (unsigned char) s[i];
            if (c == '^') {
                snprintf(msg, cap, "\"%s\" holds a \"^\", the piece delimiter",
                         zm_show(s, n, sh, sizeof sh));
                return ZM_E_DATA;
            }
            if (c < 0x20u || c == 0x7Fu) {
                snprintf(msg, cap, "\"%s\" holds the control character \\x%02x",
                         zm_show(s, n, sh, sizeof sh), (unsigned) c);
                return ZM_E_DATA;
            }
        }
        return ZXV_OK;
    }
    {
        uint32_t digits = f->type == ZM_T_DATE ? 7u : f->len;
        uint32_t scale = f->type == ZM_T_NUM ? f->scale : 0u;
        st = zm_cnum_to_dec(s, n, digits, scale, &v);
    }
    if (st == ZM_E_DATA) {
        snprintf(msg, cap, "\"%s\" is not a number in M canonical form",
                 zm_show(s, n, sh, sizeof sh));
        return st;
    }
    if (st == ZM_E_FIT) {
        zm_cnum c;
        int frac = zm_cnum_split(s, n, &c) && c.fn > 0;
        if (f->type == ZM_T_INT || f->type == ZM_T_DATE) {
            if (frac)
                snprintf(msg, cap, "%s is not a whole number (never rounded)",
                         zm_show(s, n, sh, sizeof sh));
            else if (f->type == ZM_T_DATE)
                snprintf(msg, cap, "$H day %s is outside 0..%d", zm_show(s, n, sh, sizeof sh),
                         ZM_MAX_DATE);
            else
                snprintf(msg, cap, "%s has more than %u digits", zm_show(s, n, sh, sizeof sh),
                         (unsigned) f->len);
        } else if (frac && c.fn > f->scale) {
            snprintf(msg, cap, "%s has more than %u decimals (never rounded)",
                     zm_show(s, n, sh, sizeof sh), (unsigned) f->scale);
        } else {
            snprintf(msg, cap, "%s does not fit NUMBER(%u,%u)", zm_show(s, n, sh, sizeof sh),
                     (unsigned) f->len, (unsigned) f->scale);
        }
        return st;
    }
    if (f->type == ZM_T_DATE && (v < 0 || v > ZM_MAX_DATE)) {
        snprintf(msg, cap, "$H day %s is outside 0..%d", zm_show(s, n, sh, sizeof sh),
                 ZM_MAX_DATE);
        return ZM_E_FIT;
    }
    if (out_v) *out_v = v;
    return ZXV_OK;
}

int32_t zm_key_check(const zm_field *f, const char *s, size_t n, int isnum, int64_t *out_v,
                     char *msg, size_t cap) {
    char sh[64], why[200];
    int32_t st;
    if (f->type == ZM_T_TEXT) {
        if (isnum) {
            snprintf(msg, cap, "subscript %s is a number; key %s is TEXT",
                     zm_show(s, n, sh, sizeof sh), f->name);
            return ZM_E_KEY;
        }
        if (n == 0) {
            snprintf(msg, cap, "key %s is empty", f->name);
            return ZM_E_KEY;
        }
    } else if (!isnum) {
        snprintf(msg, cap, "subscript \"%s\" is a string; key %s is a number",
                 zm_show(s, n, sh, sizeof sh), f->name);
        return ZM_E_KEY;
    }
    st = zm_value_check(f, s, n, out_v, why, sizeof why);
    if (st != ZXV_OK) {
        snprintf(msg, cap, "key %s: %s", f->name, why);
        return ZM_E_KEY;
    }
    return ZXV_OK;
}

void zm_sb_type(zm_sb *sb, const zm_field *f, int gmap) {
    switch (f->type) {
    case ZM_T_TEXT:
        zm_sb_str(sb, gmap ? "TEXT(" : "t");
        zm_sb_u64(sb, f->len);
        if (gmap) zm_sb_chr(sb, ')');
        break;
    case ZM_T_INT:
        zm_sb_str(sb, gmap ? "INTEGER(" : "i");
        zm_sb_u64(sb, f->len);
        if (gmap) zm_sb_chr(sb, ')');
        break;
    case ZM_T_NUM:
        zm_sb_str(sb, gmap ? "NUMBER(" : "n");
        zm_sb_u64(sb, f->len);
        zm_sb_chr(sb, gmap ? ',' : '.');
        zm_sb_u64(sb, f->scale);
        if (gmap) zm_sb_chr(sb, ')');
        break;
    default: zm_sb_str(sb, gmap ? "DATE" : "d"); break;
    }
}

/* ---- validation ---- */

static int item_cmp(const zm_item *a, const zm_item *b) {
    int ak = a->f.sub > 0, bk = b->f.sub > 0;
    if (ak != bk) return ak ? -1 : 1;
    if (ak) return a->f.sub < b->f.sub ? -1 : (a->f.sub > b->f.sub);
    return a->f.piece < b->f.piece ? -1 : (a->f.piece > b->f.piece);
}

/* Could subscript x of node a and subscript y of node b hold the same value? */
static int sub_overlap(const zm_node *a, const zm_sub *x, const zm_node *b, const zm_sub *y) {
    char msg[300];
    if (x->kind == ZM_SUB_LIT && y->kind == ZM_SUB_LIT)
        return x->isnum == y->isnum && x->litlen == y->litlen &&
               memcmp(x->lit, y->lit, x->litlen) == 0;
    if (x->kind == ZM_SUB_LIT)
        return zm_key_check(&b->f[y->field].f, x->lit, x->litlen, x->isnum, NULL, msg,
                            sizeof msg) == ZXV_OK;
    if (y->kind == ZM_SUB_LIT)
        return zm_key_check(&a->f[x->field].f, y->lit, y->litlen, y->isnum, NULL, msg,
                            sizeof msg) == ZXV_OK;
    /* two keys: every numeric type holds 0; TEXT never holds a number */
    return (a->f[x->field].f.type == ZM_T_TEXT) == (b->f[y->field].f.type == ZM_T_TEXT);
}

static int32_t finish_node(zm_node *nd, const char *text, size_t len, zm_diag *d) {
    uint32_t map[ZM_MAX_SUBS + ZM_MAX_PIECES];
    zm_item *sorted;
    uint32_t i, j;
    for (i = 0; i < nd->nf; i++) {
        zm_item *it = &nd->f[i];
        const char *why = zm_type_check(&it->f);
        if (why) return zm_diag_set(d, ZM_E_SYNTAX, text, len, it->pos.offset, "%s: %s",
                                    it->f.name, why);
        if (it->f.sub) {
            if (it->f.sub > nd->nsubs)
                return zm_diag_set(d, ZM_E_SYNTAX, text, len, it->pos.offset,
                                   "key %s is not a subscript of the node", it->f.name);
            it->f.required = 1;
        } else if (it->f.piece < 1u || it->f.piece > ZM_MAX_PIECES) {
            return zm_diag_set(d, ZM_E_SYNTAX, text, len, it->pos.offset,
                               "piece number %u is outside 1..%u", (unsigned) it->f.piece,
                               (unsigned) ZM_MAX_PIECES);
        }
        for (j = 0; j < i; j++) {
            if (strcmp(nd->f[j].f.name, it->f.name) == 0)
                return zm_diag_set(d, ZM_E_SYNTAX, text, len, it->pos.offset,
                                   "the name %s is used twice in this node", it->f.name);
            if (!it->f.sub && !nd->f[j].f.sub && nd->f[j].f.piece == it->f.piece)
                return zm_diag_set(d, ZM_E_SYNTAX, text, len, it->pos.offset,
                                   "piece %u is declared twice (%s and %s)",
                                   (unsigned) it->f.piece, nd->f[j].f.name, it->f.name);
        }
    }
    /* every key subscript is bound to one key field */
    for (i = 0; i < nd->nsubs; i++) {
        if (nd->subs[i].kind != ZM_SUB_KEY) continue;
        if (nd->subs[i].field >= nd->nf || nd->f[nd->subs[i].field].f.sub != i + 1u)
            return zm_diag_set(d, ZM_E_SYNTAX, text, len, nd->pos.offset,
                               "subscript %u has no KEY declaration", (unsigned) i + 1u);
    }
    for (i = 0; i < nd->nf; i++)
        if (nd->f[i].f.sub && (nd->subs[nd->f[i].f.sub - 1u].kind != ZM_SUB_KEY ||
                               nd->subs[nd->f[i].f.sub - 1u].field != i))
            return zm_diag_set(d, ZM_E_SYNTAX, text, len, nd->f[i].pos.offset,
                               "key %s is not a subscript of the node", nd->f[i].f.name);
    /* canonical order: keys by subscript, then pieces by number (stable) */
    sorted = (zm_item *) malloc((nd->nf ? nd->nf : 1u) * sizeof *sorted);
    if (!sorted) return ZXV_E_NOMEM;
    {
        uint32_t ord[ZM_MAX_SUBS + ZM_MAX_PIECES];
        for (i = 0; i < nd->nf; i++) ord[i] = i;
        for (i = 1; i < nd->nf; i++) { /* insertion sort: n <= 286 */
            uint32_t v = ord[i];
            j = i;
            while (j > 0 && item_cmp(&nd->f[ord[j - 1u]], &nd->f[v]) > 0) {
                ord[j] = ord[j - 1u];
                j--;
            }
            ord[j] = v;
        }
        for (i = 0; i < nd->nf; i++) {
            sorted[i] = nd->f[ord[i]];
            map[ord[i]] = i;
        }
    }
    for (i = 0; i < nd->nsubs; i++)
        if (nd->subs[i].kind == ZM_SUB_KEY) nd->subs[i].field = map[nd->subs[i].field];
    if (nd->nf) memcpy(nd->f, sorted, nd->nf * sizeof *sorted);
    free(sorted);
    nd->nkeys = nd->npieces = nd->width = 0;
    for (i = 0; i < nd->nf; i++) {
        if (nd->f[i].f.sub) nd->nkeys++;
        else {
            nd->npieces++;
            if (nd->f[i].f.piece > nd->width) nd->width = nd->f[i].f.piece;
        }
    }
    return ZXV_OK;
}

int32_t zm_schema_finish(zm_schema *s, const char *text, size_t len, zm_diag *d) {
    int32_t st;
    if (s->n == 0) return zm_diag_set(d, ZM_E_SYNTAX, text, len, len, "no nodes are declared");
    if (!zm_valid_gname(s->gname, strlen(s->gname)))
        return zm_diag_set(d, ZM_E_SYNTAX, text, len, 0, "no global name");
    for (uint32_t i = 0; i < s->n; i++)
        if ((st = finish_node(&s->nodes[i], text, len, d)) != ZXV_OK) return st;
    for (uint32_t i = 0; i < s->n; i++)
        for (uint32_t j = 0; j < i; j++) {
            const zm_node *a = &s->nodes[j], *b = &s->nodes[i];
            uint32_t k;
            if (a->nsubs != b->nsubs) continue;
            for (k = 0; k < a->nsubs; k++)
                if (!sub_overlap(a, &a->subs[k], b, &b->subs[k])) break;
            if (k == a->nsubs)
                return zm_diag_set(d, ZM_E_SYNTAX, text, len, b->pos.offset,
                                   "this node and node %u (line %u) can match the same "
                                   "reference; make a literal subscript or a key type differ",
                                   (unsigned) j + 1u, (unsigned) a->pos.line);
        }
    return ZXV_OK;
}

/* ---- printers ---- */

static void sb_upper(zm_sb *sb, const char *s) {
    char t[ZM_MAX_NAME + 1];
    size_t n = strlen(s);
    if (n > ZM_MAX_NAME) n = ZM_MAX_NAME;
    for (size_t i = 0; i < n; i++) t[i] = (s[i] >= 'a' && s[i] <= 'z') ? (char) (s[i] - 32) : s[i];
    zm_sb_put(sb, t, n);
}

static void sb_pattern(zm_sb *sb, const zm_schema *s, const zm_node *nd, int gmap) {
    zm_sb_chr(sb, '^');
    zm_sb_str(sb, s->gname);
    if (!nd->nsubs) return;
    zm_sb_chr(sb, '(');
    for (uint32_t i = 0; i < nd->nsubs; i++) {
        const zm_sub *x = &nd->subs[i];
        if (i) zm_sb_chr(sb, ',');
        if (x->kind == ZM_SUB_LIT) {
            zm_sb_sub(sb, x->lit, x->litlen, x->isnum);
        } else if (gmap) {
            sb_upper(sb, nd->f[x->field].f.name);
        } else {
            zm_sb_str(sb, nd->f[x->field].f.name);
            zm_sb_chr(sb, ':');
            zm_sb_type(sb, &nd->f[x->field].f, 0);
        }
    }
    zm_sb_chr(sb, ')');
}

static void sb_myra(zm_sb *sb, const zm_schema *s) {
    for (uint32_t i = 0; i < s->n; i++) {
        const zm_node *nd = &s->nodes[i];
        uint32_t k = nd->nkeys;
        sb_pattern(sb, s, nd, 0);
        zm_sb_chr(sb, '=');
        for (uint32_t p = 1; p <= nd->width; p++) {
            if (p > 1) zm_sb_chr(sb, '^');
            if (k < nd->nf && nd->f[k].f.piece == p) {
                zm_sb_str(sb, nd->f[k].f.name);
                zm_sb_chr(sb, ':');
                zm_sb_type(sb, &nd->f[k].f, 0);
                if (nd->f[k].f.required) zm_sb_chr(sb, '!');
                k++;
            }
        }
        zm_sb_chr(sb, '\n');
    }
}

int32_t zm_schema_myra(const zm_schema *s, char *buf, size_t cap, size_t *out_len) {
    zm_sb sb;
    if (!s || !out_len) return ZXV_E_ARG;
    zm_sb_init(&sb, SIZE_MAX / 2u);
    sb_myra(&sb, s);
    return zm_sb_out(&sb, buf, cap, out_len);
}

int32_t zm_schema_gmap(const zm_schema *s, char *buf, size_t cap, size_t *out_len) {
    zm_sb sb;
    if (!s || !out_len) return ZXV_E_ARG;
    zm_sb_init(&sb, SIZE_MAX / 2u);
    zm_sb_str(&sb, "GLOBAL ^");
    zm_sb_str(&sb, s->gname);
    zm_sb_chr(&sb, '\n');
    for (uint32_t i = 0; i < s->n; i++) {
        const zm_node *nd = &s->nodes[i];
        zm_sb_str(&sb, "NODE ");
        sb_pattern(&sb, s, nd, 1);
        zm_sb_chr(&sb, '\n');
        for (uint32_t k = 0; k < nd->nf; k++) {
            const zm_field *f = &nd->f[k].f;
            if (f->sub) {
                zm_sb_str(&sb, "  KEY ");
            } else {
                zm_sb_str(&sb, "  PIECE ");
                zm_sb_u64(&sb, f->piece);
                zm_sb_chr(&sb, ' ');
            }
            sb_upper(&sb, f->name);
            zm_sb_chr(&sb, ' ');
            zm_sb_type(&sb, f, 1);
            if (!f->sub && f->required) zm_sb_str(&sb, " REQUIRED");
            zm_sb_chr(&sb, '\n');
        }
    }
    return zm_sb_out(&sb, buf, cap, out_len);
}

int32_t zm_schema_digest(const zm_schema *s, uint8_t out[32]) {
    zm_sb sb;
    int32_t st;
    if (!s || !out) return ZXV_E_ARG;
    zm_sb_init(&sb, SIZE_MAX / 2u);
    sb_myra(&sb, s);
    if (sb.err != ZXV_OK) {
        st = sb.err;
        zm_sb_free(&sb);
        return st;
    }
    st = zxv_sha3_256((const uint8_t *) (sb.b ? sb.b : ""), sb.n, out);
    zm_sb_free(&sb);
    return st;
}

int32_t zm_schema_same(const zm_schema *a, const zm_schema *b, int32_t *out_same) {
    uint8_t da[32], db[32];
    int32_t st;
    if (!a || !b || !out_same) return ZXV_E_ARG;
    if ((st = zm_schema_digest(a, da)) != ZXV_OK) return st;
    if ((st = zm_schema_digest(b, db)) != ZXV_OK) return st;
    *out_same = memcmp(da, db, 32) == 0;
    return ZXV_OK;
}

int32_t zm_schema_global(const zm_schema *s, char *buf, size_t cap, size_t *out_len) {
    if (!s) return ZXV_E_ARG;
    return zm_out_text(s->gname, strlen(s->gname), buf, cap, out_len);
}

/* ---- accessors ---- */

int32_t zm_node_count(const zm_schema *s, uint32_t *out_count) {
    if (!s || !out_count) return ZXV_E_ARG;
    *out_count = s->n;
    return ZXV_OK;
}

int32_t zm_node_info_at(const zm_schema *s, uint32_t node, zm_node_info *out) {
    if (!s || !out) return ZXV_E_ARG;
    if (node >= s->n) return ZXV_E_NOT_FOUND;
    out->subs = s->nodes[node].nsubs;
    out->keys = s->nodes[node].nkeys;
    out->pieces = s->nodes[node].npieces;
    out->width = s->nodes[node].width;
    return ZXV_OK;
}

int32_t zm_node_pattern(const zm_schema *s, uint32_t node, char *buf, size_t cap,
                        size_t *out_len) {
    zm_sb sb;
    if (!s || !out_len) return ZXV_E_ARG;
    if (node >= s->n) return ZXV_E_NOT_FOUND;
    zm_sb_init(&sb, SIZE_MAX / 2u);
    sb_pattern(&sb, s, &s->nodes[node], 0);
    return zm_sb_out(&sb, buf, cap, out_len);
}

int32_t zm_field_at(const zm_schema *s, uint32_t node, uint32_t index, zm_field *out) {
    if (!s || !out) return ZXV_E_ARG;
    if (node >= s->n || index >= s->nodes[node].nf) return ZXV_E_NOT_FOUND;
    *out = s->nodes[node].f[index].f;
    return ZXV_OK;
}

int32_t zm_field_find(const zm_schema *s, uint32_t node, const char *name, uint32_t *out_index) {
    if (!s || !name || !out_index) return ZXV_E_ARG;
    if (node >= s->n) return ZXV_E_NOT_FOUND;
    for (uint32_t i = 0; i < s->nodes[node].nf; i++)
        if (zm_ieq(s->nodes[node].f[i].f.name, name)) {
            *out_index = i;
            return ZXV_OK;
        }
    zm_set_errorf("no field %s in node %u", name, (unsigned) node + 1u);
    return ZXV_E_NOT_FOUND;
}
