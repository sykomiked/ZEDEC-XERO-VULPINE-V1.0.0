/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* aml_core.c — heap, values, namespace, parser and evaluator of the AML
 * interpreter (see aml.h for the contract).
 *
 * The evaluator runs straight off the byte code: no parse tree is built. A
 * method body is parsed each time it runs, which keeps memory flat and makes
 * forward references free (by the time a method runs every table is loaded,
 * so a call's argument count comes from the namespace). Table-level code is
 * the exception: it runs while the table loads, so a first pass declares
 * every Method, Device and Scope before the second pass executes anything,
 * and OperationRegion address/length terms are kept and evaluated on first
 * use, the way ACPICA defers them.
 *
 * Ownership: every aml_val_t has exactly one owner. A Store copies. A
 * reference (RefOf, Index, a name inside a package) names a location -- a
 * namespace node checked by generation, a Local/Arg slot checked by frame
 * serial, or a value the reference owns -- so it can never dangle into freed
 * memory: a stale reference reads as an error. */
#include "aml_int.h"

#define FLOW_BREAK    1
#define FLOW_CONTINUE 2
#define FLOW_RETURN   3

#define NODE_TEMP        1
#define NODE_PLACEHOLDER 2
#define NODE_DEAD        0xFF

#define AML_REVISION 0x20261012u

#define HDR_LIVE 0xA7000000u
#define HDR_FREE 0x5E000000u
#define HDR_MASK 0xFF000000u

/* ---- small helpers ---------------------------------------------------------- */
void aml_bzero(void *p, uint32_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

void aml_bcopy(void *d, const void *s, uint32_t n)
{
    volatile uint8_t *dd = (volatile uint8_t *) d;
    const volatile uint8_t *ss = (const volatile uint8_t *) s;
    for (uint32_t i = 0; i < n; i++) dd[i] = ss[i];
}

uint64_t aml_ones(const aml_ctx_t *c)
{
    return c->int64 ? ~0ULL : 0xFFFFFFFFULL;
}

static uint64_t imask(const aml_ctx_t *c, uint64_t v)
{
    return v & aml_ones(c);
}

static uint32_t int_bytes(const aml_ctx_t *c)
{
    return c->int64 ? 8u : 4u;
}

/* ---- log lines ---------------------------------------------------------------- */
typedef struct line {
    char b[160];
    uint32_t n;
} line_t;

static void ln_c(line_t *l, char ch)
{
    if (l->n + 1 < sizeof l->b) l->b[l->n++] = ch;
    l->b[l->n] = 0;
}
static void ln_s(line_t *l, const char *s)
{
    while (s && *s) ln_c(l, *s++);
}
static void ln_u(line_t *l, uint64_t v, int hex)
{
    char t[24];
    int i = 0;
    if (hex) ln_s(l, "0x");
    do {
        uint32_t d = (uint32_t) (hex ? v % 16 : v % 10);
        t[i++] = (char) (d < 10 ? '0' + d : 'A' + d - 10);
        v = hex ? v / 16 : v / 10;
    } while (v && i < 24);
    while (i) ln_c(l, t[--i]);
}

void aml_logf(aml_ctx_t *c, const char *a, const char *b, uint64_t v, int hex)
{
    if (!c->plat || !c->plat->log) return;
    line_t l;
    l.n = 0;
    l.b[0] = 0;
    ln_s(&l, "[AML] ");
    ln_s(&l, a);
    if (b) {
        ln_c(&l, ' ');
        ln_s(&l, b);
    }
    if (hex >= 0) {
        ln_c(&l, ' ');
        ln_u(&l, v, hex);
    }
    c->plat->log(l.b);
}

static int fail(aml_ctx_t *c, int err, const char *where)
{
    if (!c->last_error) { /* keep the innermost (first) cause */
        c->last_error = err;
        c->last_error_where = where;
    }
    return err;
}

/* ---- heap -----------------------------------------------------------------------
 * Size classes 16, 24, 32, 48, 64, 96, ... (x1.5 / x1.33 steps), each with
 * its own free list; fresh blocks come off the top of the arena. A block
 * carries an 8-byte header with its class and a live/free tag, so a double
 * free or a stray pointer is ignored instead of corrupting a list. */
static uint32_t cls_size(uint32_t k)
{
    return ((k & 1) ? 24u : 16u) << (k >> 1);
}

void *aml_alloc(aml_ctx_t *c, uint32_t n)
{
    if (n > 0x400000u) return 0;
    uint32_t need = n + 8, k = 0;
    while (k < 40 && cls_size(k) < need) k++;
    if (k >= 40) return 0;
    uint8_t *blk = (uint8_t *) c->freelist[k];
    if (blk) {
        void *next;
        aml_bcopy(&next, blk + 8, sizeof next);
        c->freelist[k] = next;
    } else {
        uint32_t sz = cls_size(k);
        if (sz > c->arena_size - c->arena_top) return 0;
        blk = c->arena + c->arena_top;
        c->arena_top += sz;
    }
    uint32_t tag = HDR_LIVE | k;
    aml_bcopy(blk, &tag, 4);
    aml_bcopy(blk + 4, &n, 4);
    aml_bzero(blk + 8, cls_size(k) - 8);
    c->heap_used += cls_size(k);
    if (c->heap_used > c->heap_peak) c->heap_peak = c->heap_used;
    return blk + 8;
}

void aml_free(aml_ctx_t *c, void *p)
{
    if (!p) return;
    uint8_t *blk = (uint8_t *) p - 8;
    if (blk < c->arena || blk >= c->arena + c->arena_top) return;
    uint32_t tag;
    aml_bcopy(&tag, blk, 4);
    if ((tag & HDR_MASK) != HDR_LIVE) return;
    uint32_t k = tag & 0xFF;
    if (k >= 40) return;
    tag = HDR_FREE | k;
    aml_bcopy(blk, &tag, 4);
    aml_bcopy(blk + 8, &c->freelist[k], sizeof(void *));
    c->freelist[k] = blk;
    c->heap_used -= cls_size(k);
}

/* ---- values ---------------------------------------------------------------------- */
aml_val_t aml_int(uint64_t v)
{
    aml_val_t r;
    aml_bzero(&r, sizeof r);
    r.type = AML_T_INT;
    r.u.i = v;
    return r;
}

static void val_zero(aml_val_t *v)
{
    aml_bzero(v, sizeof *v);
}

void aml_val_free(aml_ctx_t *c, aml_val_t *v)
{
    if (!v) return;
    switch (v->type) {
    case AML_T_STR:
    case AML_T_BUF:
        aml_free(c, v->u.bytes);
        break;
    case AML_T_PKG:
        if (v->u.elems) {
            for (uint32_t i = 0; i < v->len; i++) aml_val_free(c, &v->u.elems[i]);
            aml_free(c, v->u.elems);
        }
        break;
    case AML_T_REF:
        if (v->u.ref) {
            aml_val_free(c, &v->u.ref->owned);
            aml_free(c, v->u.ref);
        }
        break;
    default:
        break;
    }
    val_zero(v);
}

int aml_mkbuf(aml_ctx_t *c, aml_val_t *v, uint32_t len)
{
    val_zero(v);
    if (len > AML_MAX_OBJ) return AML_ENOMEM;
    v->u.bytes = (uint8_t *) aml_alloc(c, len ? len : 1);
    if (!v->u.bytes) return AML_ENOMEM;
    v->type = AML_T_BUF;
    v->len = len;
    return AML_OK;
}

int aml_mkstr(aml_ctx_t *c, aml_val_t *v, uint32_t len)
{
    val_zero(v);
    if (len > AML_MAX_OBJ) return AML_ENOMEM;
    v->u.bytes = (uint8_t *) aml_alloc(c, len + 1);
    if (!v->u.bytes) return AML_ENOMEM;
    v->type = AML_T_STR;
    v->len = len;
    return AML_OK;
}

static int mkpkg(aml_ctx_t *c, aml_val_t *v, uint32_t n)
{
    val_zero(v);
    if (n > AML_MAX_PKG) return AML_ENOMEM;
    v->u.elems = (aml_val_t *) aml_alloc(c, (n ? n : 1) * (uint32_t) sizeof(aml_val_t));
    if (!v->u.elems) return AML_ENOMEM;
    v->type = AML_T_PKG;
    v->len = n;
    return AML_OK;
}

static int mkref(aml_ctx_t *c, aml_val_t *v)
{
    val_zero(v);
    v->u.ref = (aml_ref_t *) aml_alloc(c, (uint32_t) sizeof(aml_ref_t));
    if (!v->u.ref) return AML_ENOMEM;
    v->type = AML_T_REF;
    return AML_OK;
}

aml_val_t aml_str(aml_ctx_t *c, const char *s)
{
    aml_val_t v;
    uint32_t n = 0;
    while (s && s[n] && n < AML_MAX_OBJ) n++;
    if (aml_mkstr(c, &v, n)) return v;
    aml_bcopy(v.u.bytes, s, n);
    return v;
}

aml_val_t aml_buf(aml_ctx_t *c, const void *b, uint32_t len)
{
    aml_val_t v;
    if (aml_mkbuf(c, &v, len)) return v;
    if (b) aml_bcopy(v.u.bytes, b, len);
    return v;
}

const aml_val_t *aml_pkg_elem(const aml_val_t *v, uint32_t i)
{
    if (!v || v->type != AML_T_PKG || i >= v->len) return 0;
    return &v->u.elems[i];
}

static int copy_depth(aml_ctx_t *c, aml_val_t *dst, const aml_val_t *src, uint32_t depth)
{
    int r;
    val_zero(dst);
    if (depth > 48) return AML_ELIMIT;
    switch (src->type) {
    case AML_T_STR:
        r = aml_mkstr(c, dst, src->len);
        if (!r) aml_bcopy(dst->u.bytes, src->u.bytes, src->len);
        return r;
    case AML_T_BUF:
        r = aml_mkbuf(c, dst, src->len);
        if (!r) aml_bcopy(dst->u.bytes, src->u.bytes, src->len);
        return r;
    case AML_T_PKG:
        r = mkpkg(c, dst, src->len);
        for (uint32_t i = 0; !r && i < src->len; i++)
            r = copy_depth(c, &dst->u.elems[i], &src->u.elems[i], depth + 1);
        if (r) aml_val_free(c, dst);
        return r;
    case AML_T_REF:
        r = mkref(c, dst);
        if (r) return r;
        dst->u.ref->base = src->u.ref->base;
        dst->u.ref->indexed = src->u.ref->indexed;
        dst->u.ref->index = src->u.ref->index;
        r = copy_depth(c, &dst->u.ref->owned, &src->u.ref->owned, depth + 1);
        if (r) aml_val_free(c, dst);
        return r;
    default:
        *dst = *src;
        return AML_OK;
    }
}

int aml_val_copy(aml_ctx_t *c, aml_val_t *dst, const aml_val_t *src)
{
    return copy_depth(c, dst, src, 0);
}

/* ---- namespace ---------------------------------------------------------------------- */
static uint32_t seg_at(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

static int lead_ok(uint8_t b)
{
    return (b >= 'A' && b <= 'Z') || b == '_';
}

static int name_ok(uint8_t b)
{
    return lead_ok(b) || (b >= '0' && b <= '9');
}

static int seg_ok(const uint8_t *p)
{
    return lead_ok(p[0]) && name_ok(p[1]) && name_ok(p[2]) && name_ok(p[3]);
}

aml_node_t *aml_ns_child(aml_node_t *scope, uint32_t name)
{
    if (!scope) return 0;
    for (aml_node_t *n = scope->child; n; n = n->next)
        if (n->name == name) return n;
    return 0;
}

aml_node_t *aml_ns_resolve(aml_ctx_t *c, aml_node_t *n)
{
    (void) c;
    for (int i = 0; n && n->v.type == AML_T_ALIAS && i < 8; i++) n = n->x.alias.target;
    if (n && n->v.type == AML_T_ALIAS) return 0;
    return n;
}

static aml_frame_t *cur_frame(aml_ctx_t *c)
{
    return c->depth ? &c->frames[c->depth - 1] : 0;
}

/* Running a table's own top-level code (not a method it calls). */
static int at_table_level(aml_ctx_t *c)
{
    return c->loading && c->depth && !c->frames[c->depth - 1].method;
}

static aml_node_t *node_new(aml_ctx_t *c, aml_node_t *parent, uint32_t name, uint8_t type)
{
    aml_node_t *n = c->node_free;
    if (n) {
        c->node_free = n->next;
        aml_bzero(n, (uint32_t) sizeof *n);
    } else {
        n = (aml_node_t *) aml_alloc(c, (uint32_t) sizeof(aml_node_t));
        if (!n) return 0;
    }
    n->name = name;
    n->gen = ++c->node_gen;
    n->v.type = type;
    n->parent = parent;
    if (parent) {
        aml_node_t **pp = &parent->child;
        while (*pp) pp = &(*pp)->next;
        *pp = n;
    }
    aml_frame_t *f = cur_frame(c);
    if (f && f->method) {
        n->temp = NODE_TEMP;
        n->tnext = f->temps;
        f->temps = n;
    }
    c->stats.objects++;
    if (type == AML_T_METHOD) c->stats.methods++;
    if (type == AML_T_DEVICE) c->stats.devices++;
    return n;
}

static void node_free_one(aml_ctx_t *c, aml_node_t *n)
{
    if (n->v.type == AML_T_BUFFIELD) aml_val_free(c, &n->x.bf.owned);
    if (n->v.type <= AML_T_PKG || n->v.type == AML_T_REF) aml_val_free(c, &n->v);
    if (c->stats.objects) c->stats.objects--;
    if (n->v.type == AML_T_METHOD && c->stats.methods) c->stats.methods--;
    if (n->v.type == AML_T_DEVICE && c->stats.devices) c->stats.devices--;
    n->gen = ++c->node_gen;
    n->temp = NODE_DEAD;
    n->v.type = AML_T_UNINIT;
    n->child = 0;
    n->parent = 0;
    n->next = c->node_free;
    c->node_free = n;
}

/* Unlink n and free it with its whole subtree, without recursion. */
static void node_destroy(aml_ctx_t *c, aml_node_t *n)
{
    if (!n || n->temp == NODE_DEAD) return;
    if (n->parent) {
        aml_node_t **pp = &n->parent->child;
        while (*pp && *pp != n) pp = &(*pp)->next;
        if (*pp) *pp = n->next;
    }
    aml_node_t *cur = n;
    for (;;) {
        if (cur->child) {
            aml_node_t *ch = cur->child;
            cur->child = ch->next;
            cur = ch;
            continue;
        }
        aml_node_t *par = cur->parent;
        int last = cur == n;
        node_free_one(c, cur);
        if (last) break;
        cur = par;
    }
}

uint32_t aml_node_type(const aml_node_t *n)
{
    if (!n) return AML_T_UNINIT;
    uint32_t t = n->v.type;
    if (t == AML_T_SCOPE || t == AML_T_ALIAS || t == AML_T_REF) return AML_T_UNINIT;
    return t;
}

aml_node_t *aml_node_parent(const aml_node_t *n)
{
    return n ? n->parent : 0;
}

void aml_node_name(const aml_node_t *n, char out[5])
{
    uint32_t v = n ? n->name : 0;
    for (int i = 0; i < 4; i++) out[i] = (char) ((v >> (8 * i)) & 0xFF);
    out[4] = 0;
    if (!n || !n->parent) out[0] = '\\', out[1] = 0;
}

uint32_t aml_node_path(const aml_node_t *n, char *out, uint32_t max)
{
    aml_node_t const *chain[32];
    uint32_t d = 0, o = 0;
    if (!max) return 0;
    for (const aml_node_t *p = n; p && p->parent && d < 32; p = p->parent) chain[d++] = p;
    if (o + 1 < max) out[o++] = '\\';
    while (d) {
        const aml_node_t *p = chain[--d];
        for (int i = 0; i < 4 && o + 1 < max; i++) out[o++] = (char) ((p->name >> (8 * i)) & 0xFF);
        if (d && o + 1 < max) out[o++] = '.';
    }
    out[o] = 0;
    return o;
}

/* ---- parser state ----------------------------------------------------------------- */
typedef struct st {
    const uint8_t *a;
    uint32_t pc, end;
    aml_node_t *scope;
} st_t;

typedef struct nstr {
    uint8_t root, up, nseg;
    const uint8_t *segs;
} nstr_t;

static int rd8(st_t *s, uint8_t *v)
{
    if (s->pc >= s->end) return AML_EPARSE;
    *v = s->a[s->pc++];
    return AML_OK;
}

static int rdle(st_t *s, uint32_t n, uint64_t *v)
{
    if (n > s->end - s->pc) return AML_EPARSE;
    uint64_t r = 0;
    for (uint32_t i = 0; i < n; i++) r |= (uint64_t) s->a[s->pc + i] << (8 * i);
    s->pc += n;
    *v = r;
    return AML_OK;
}

/* Raw PkgLength value (field element lengths). */
static int rd_pkglen_raw(st_t *s, uint32_t *len)
{
    uint8_t b0, b;
    if (rd8(s, &b0)) return AML_EPARSE;
    uint32_t nb = b0 >> 6;
    if (!nb) {
        *len = b0 & 0x3F;
        return AML_OK;
    }
    uint32_t v = b0 & 0x0F;
    for (uint32_t i = 0; i < nb; i++) {
        if (rd8(s, &b)) return AML_EPARSE;
        v |= (uint32_t) b << (4 + 8 * i);
    }
    *len = v;
    return AML_OK;
}

/* PkgLength of a body: *end is where the package ends, inside s->end. */
static int rd_pkglen(st_t *s, uint32_t *end)
{
    uint32_t start = s->pc, len;
    if (rd_pkglen_raw(s, &len)) return AML_EPARSE;
    if (len > s->end - start || start + len < s->pc) return AML_EPARSE;
    *end = start + len;
    return AML_OK;
}

static int is_name_start(uint8_t b)
{
    return lead_ok(b) || b == '\\' || b == '^' || b == 0x2E || b == 0x2F;
}

static int parse_nstr(st_t *s, nstr_t *n)
{
    uint8_t b;
    n->root = 0;
    n->up = 0;
    n->nseg = 0;
    n->segs = 0;
    if (s->pc < s->end && s->a[s->pc] == '\\') {
        n->root = 1;
        s->pc++;
    } else {
        while (s->pc < s->end && s->a[s->pc] == '^') {
            if (n->up == 255) return AML_EPARSE;
            n->up++;
            s->pc++;
        }
    }
    if (rd8(s, &b)) return AML_EPARSE;
    uint32_t cnt;
    if (b == 0x00) {
        cnt = 0;
    } else if (b == 0x2E) {
        cnt = 2;
    } else if (b == 0x2F) {
        if (rd8(s, &b)) return AML_EPARSE;
        if (!b) return AML_EPARSE;
        cnt = b;
    } else {
        s->pc--;
        cnt = 1;
    }
    if (cnt * 4 > s->end - s->pc) return AML_EPARSE;
    for (uint32_t i = 0; i < cnt; i++)
        if (!seg_ok(s->a + s->pc + 4 * i)) return AML_EPARSE;
    n->nseg = (uint8_t) cnt;
    n->segs = s->a + s->pc;
    s->pc += cnt * 4;
    return AML_OK;
}

/* Resolve all segments, or all but the last (want_parent). A single relative
 * segment searches the scope and then its parents (ACPI search rules) unless
 * want_parent is 2 (no upward search). */
static aml_node_t *resolve_n(aml_ctx_t *c, aml_node_t *scope, const nstr_t *n, int want_parent)
{
    aml_node_t *start = n->root ? c->root : (scope ? scope : c->root);
    for (uint32_t i = 0; i < n->up; i++) {
        if (!start->parent) return 0;
        start = start->parent;
    }
    uint32_t nseg = n->nseg;
    if (want_parent == 1) {
        if (!nseg) return 0;
        nseg--;
    }
    if (nseg == 1 && !n->root && !n->up && !want_parent) {
        uint32_t seg = seg_at(n->segs);
        for (aml_node_t *sc = start; sc; sc = sc->parent) {
            aml_node_t *f = aml_ns_child(sc, seg);
            if (f) return f;
        }
        return 0;
    }
    aml_node_t *cur = start;
    for (uint32_t i = 0; i < nseg && cur; i++) cur = aml_ns_child(cur, seg_at(n->segs + 4 * i));
    return cur;
}

static void nstr_text(const nstr_t *n, char *out, uint32_t max)
{
    uint32_t o = 0;
    if (!max) return;
    if (n->root && o + 1 < max) out[o++] = '\\';
    for (uint32_t i = 0; i < n->up && o + 1 < max; i++) out[o++] = '^';
    for (uint32_t i = 0; i < n->nseg; i++) {
        for (uint32_t j = 0; j < 4 && o + 1 < max; j++) out[o++] = (char) n->segs[4 * i + j];
        if (i + 1 < n->nseg && o + 1 < max) out[o++] = '.';
    }
    out[o] = 0;
}

/* ASCII path -> segments in a scratch array. */
static int ascii_path(const char *p, uint8_t *segs, uint32_t maxseg, nstr_t *n)
{
    n->root = 0;
    n->up = 0;
    n->nseg = 0;
    n->segs = segs;
    if (!p) return AML_EARG;
    if (*p == '\\') {
        n->root = 1;
        p++;
    }
    while (*p == '^') {
        if (n->up == 255) return AML_EARG;
        n->up++;
        p++;
    }
    while (*p) {
        if (n->nseg >= maxseg) return AML_EARG;
        uint8_t *sg = segs + 4 * n->nseg;
        uint32_t k = 0;
        while (*p && *p != '.' && k < 4) sg[k++] = (uint8_t) *p++;
        if (!k || (*p && *p != '.')) return AML_EARG;
        while (k < 4) sg[k++] = '_';
        if (!seg_ok(sg)) return AML_EARG;
        n->nseg++;
        if (*p == '.') {
            p++;
            if (!*p) return AML_EARG;
        }
    }
    return AML_OK;
}

static aml_node_t *lookup_ascii(aml_ctx_t *c, aml_node_t *scope, const char *path, int search)
{
    uint8_t segs[4 * 16];
    nstr_t n;
    if (!c || !c->root || ascii_path(path, segs, 16, &n)) return 0;
    return resolve_n(c, scope, &n, search ? 0 : 2);
}

aml_node_t *aml_lookup(aml_ctx_t *c, aml_node_t *scope, const char *path)
{
    return lookup_ascii(c, scope, path, 0);
}

/* Create the node a NameString names (its parent must exist). A placeholder
 * from the declaration pass of the same type is adopted instead. */
static int create_named(aml_ctx_t *c, aml_node_t *scope, const nstr_t *n, uint8_t type,
                        aml_node_t **out)
{
    if (!n->nseg) return AML_EPARSE;
    aml_node_t *parent = n->nseg == 1 ? (n->root ? c->root : scope ? scope : c->root) : 0;
    if (n->nseg == 1 && !n->root) {
        for (uint32_t i = 0; i < n->up && parent; i++) parent = parent->parent;
    } else if (n->nseg > 1) {
        parent = resolve_n(c, scope, n, 1);
    }
    if (!parent) return AML_ENOENT;
    uint32_t name = seg_at(n->segs + 4 * (n->nseg - 1));
    aml_node_t *ex = aml_ns_child(parent, name);
    if (ex) {
        if (ex->temp == NODE_PLACEHOLDER && ex->v.type == type) {
            ex->temp = 0;
            *out = ex;
            return AML_OK;
        }
        return AML_EEXIST;
    }
    aml_node_t *nn = node_new(c, parent, name, type);
    if (!nn) return AML_ENOMEM;
    *out = nn;
    return AML_OK;
}

/* ---- externals: argument counts of methods another table defines ------------ */
static void ext_add(aml_ctx_t *c, aml_node_t *scope, const uint8_t *name, uint8_t argc)
{
    for (uint32_t i = 0; i < c->n_ext; i++)
        if (c->ext[i].name == name) return;
    if (c->n_ext >= AML_MAX_EXTERNAL) return;
    c->ext[c->n_ext].scope = scope;
    c->ext[c->n_ext].name = name;
    c->ext[c->n_ext].argc = argc;
    c->n_ext++;
}

/* Last segment and absolute parent: enough to match "the same name". */
static int ext_argc(aml_ctx_t *c, aml_node_t *scope, const nstr_t *n)
{
    if (!n->nseg) return -1;
    uint32_t last = seg_at(n->segs + 4 * (n->nseg - 1));
    for (uint32_t i = 0; i < c->n_ext; i++) {
        st_t s;
        nstr_t e;
        s.a = c->ext[i].name;
        s.pc = 0;
        s.end = 1024;
        if (parse_nstr(&s, &e) || !e.nseg) continue;
        if (seg_at(e.segs + 4 * (e.nseg - 1)) != last) continue;
        aml_node_t *ep = e.nseg > 1 || e.root || e.up ? resolve_n(c, c->ext[i].scope, &e, 1)
                                                      : c->ext[i].scope;
        if (n->nseg == 1 && !n->root && !n->up) {
            for (aml_node_t *sc = scope; sc; sc = sc->parent)
                if (sc == ep) return c->ext[i].argc & 7;
        } else if (resolve_n(c, scope, n, 1) == ep) {
            return c->ext[i].argc & 7;
        }
    }
    return -1;
}

/* ---- skipping a term without running it -------------------------------------------
 * Shapes: T TermArg, S SuperName/Target, N NameString, B/W/D/Q immediate,
 * Z NUL-terminated string, P PkgLength (skips the rest of the object). */
static const char *op_shape(uint32_t op)
{
    if (op >= 0x60 && op <= 0x6E) return "";
    switch (op) {
    case 0x00: case 0x01: case 0xFF: case 0x9F: case 0xA3: case 0xA5: case 0xCC:
    case 0x5B30: case 0x5B31: case 0x5B33:
        return "";
    case 0x06: return "NN";
    case 0x08: return "NT";
    case 0x0A: return "B";
    case 0x0B: return "W";
    case 0x0C: return "D";
    case 0x0D: return "Z";
    case 0x0E: return "Q";
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0xA0: case 0xA1: case 0xA2:
    case 0x5B81: case 0x5B82: case 0x5B83: case 0x5B84: case 0x5B85: case 0x5B86: case 0x5B87:
        return "P";
    case 0x15: return "NBB";
    case 0x70: return "TS";
    case 0x71: return "S";
    case 0x72: case 0x73: case 0x74: case 0x77: case 0x79: case 0x7A: case 0x7B: case 0x7C:
    case 0x7D: case 0x7E: case 0x7F: case 0x84: case 0x85: case 0x88:
        return "TTS";
    case 0x75: case 0x76: case 0x87: case 0x8E: case 0x5B24: case 0x5B26: case 0x5B27:
    case 0x5B2A:
        return "S";
    case 0x78: return "TTSS";
    case 0x80: case 0x81: case 0x82: case 0x96: case 0x97: case 0x98: case 0x99: case 0x9D:
    case 0x5B28: case 0x5B29:
        return "TS";
    case 0x83: case 0x92: case 0xA4: case 0x5B21: case 0x5B22:
        return "T";
    case 0x86: return "ST";
    case 0x89: return "TBTBTT";
    case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8F: return "TTN";
    case 0x90: case 0x91: case 0x93: case 0x94: case 0x95: return "TT";
    case 0x9C: return "TTS";
    case 0x9E: return "TTTS";
    case 0x5B01: return "NB";
    case 0x5B02: return "N";
    case 0x5B12: return "SS";
    case 0x5B13: return "TTTN";
    case 0x5B1F: return "TTTTTT";
    case 0x5B20: return "NS";
    case 0x5B23: return "SW";
    case 0x5B25: return "ST";
    case 0x5B32: return "BDT";
    case 0x5B80: return "NBTT";
    case 0x5B88: return "NTTT";
    default: return 0;
    }
}

static int skip_term(aml_ctx_t *c, st_t *s);

static int skip_target(aml_ctx_t *c, st_t *s)
{
    nstr_t n;
    if (s->pc >= s->end) return AML_EPARSE;
    uint8_t b = s->a[s->pc];
    if (b == 0x00 || (b >= 0x60 && b <= 0x6E)) {
        s->pc++;
        return AML_OK;
    }
    if (b == 0x5B) {
        if (s->pc + 1 < s->end && s->a[s->pc + 1] == 0x31) {
            s->pc += 2;
            return AML_OK;
        }
        return AML_EPARSE;
    }
    if (is_name_start(b)) return parse_nstr(s, &n);
    if (b == 0x71 || b == 0x83 || b == 0x88) return skip_term(c, s);
    return AML_EPARSE;
}

static int skip_term(aml_ctx_t *c, st_t *s)
{
    int r = AML_OK;
    if (++c->nest > AML_MAX_NEST) {
        c->nest--;
        return AML_ELIMIT;
    }
    if (s->pc >= s->end) {
        r = AML_EPARSE;
        goto out;
    }
    uint8_t b = s->a[s->pc];
    if (is_name_start(b)) {
        nstr_t n;
        r = parse_nstr(s, &n);
        if (r) goto out;
        aml_node_t *m = aml_ns_resolve(c, resolve_n(c, s->scope, &n, 0));
        int argc = 0;
        if (m && m->v.type == AML_T_METHOD) {
            argc = m->x.m.flags & 7;
        } else if (!m) {
            argc = ext_argc(c, s->scope, &n);
            if (argc < 0) argc = 0;
        }
        for (int i = 0; i < argc && !r; i++) r = skip_term(c, s);
        goto out;
    }
    uint32_t op = b;
    s->pc++;
    if (b == 0x5B) {
        uint8_t e;
        if (rd8(s, &e)) {
            r = AML_EPARSE;
            goto out;
        }
        op = 0x5B00u | e;
    }
    const char *sh = op_shape(op);
    if (!sh) {
        r = AML_EPARSE;
        goto out;
    }
    for (; *sh && !r; sh++) {
        uint64_t v;
        uint32_t end;
        nstr_t n;
        switch (*sh) {
        case 'T': r = skip_term(c, s); break;
        case 'S': r = skip_target(c, s); break;
        case 'N': r = parse_nstr(s, &n); break;
        case 'B': r = rdle(s, 1, &v); break;
        case 'W': r = rdle(s, 2, &v); break;
        case 'D': r = rdle(s, 4, &v); break;
        case 'Q': r = rdle(s, 8, &v); break;
        case 'Z':
            while (s->pc < s->end && s->a[s->pc]) s->pc++;
            r = s->pc < s->end ? AML_OK : AML_EPARSE;
            s->pc++;
            break;
        case 'P':
            r = rd_pkglen(s, &end);
            if (!r) s->pc = end;
            break;
        default: r = AML_EPARSE; break;
        }
    }
out:
    c->nest--;
    return r;
}

/* ---- evaluator ------------------------------------------------------------------- */
static int eval_term(aml_ctx_t *c, st_t *s, aml_val_t *out);
static int exec_termlist(aml_ctx_t *c, st_t *s, uint32_t end);
static int read_node(aml_ctx_t *c, aml_node_t *n, aml_val_t *out);
static int store_node(aml_ctx_t *c, aml_node_t *n, const aml_val_t *v, int copyobj);

#define TGT_REF 6
#define SUP_NULL_OK 1
#define SUP_MISSING_OK 2

typedef struct tgt {
    uint8_t kind;
    uint8_t slot;
    aml_node_t *node;
    aml_val_t ref;
} tgt_t;

static int eval_int(aml_ctx_t *c, st_t *s, uint64_t *v)
{
    aml_val_t t;
    val_zero(&t);
    int r = eval_term(c, s, &t);
    if (!r) r = aml_to_int(c, &t, v);
    aml_val_free(c, &t);
    return r;
}

/* Value slot behind a location; *node set when it is a namespace node. */
static aml_val_t *loc_slot(aml_ctx_t *c, const aml_loc_t *l, aml_node_t **node)
{
    *node = 0;
    switch (l->kind) {
    case AML_LOC_NODE:
        if (!l->node || l->node->gen != l->serial) return 0;
        *node = l->node;
        return &l->node->v;
    case AML_LOC_LOCAL:
    case AML_LOC_ARG:
        if (l->depth >= c->depth || c->frames[l->depth].serial != l->serial) return 0;
        if (l->kind == AML_LOC_LOCAL) return l->slot < 8 ? &c->frames[l->depth].locals[l->slot] : 0;
        return l->slot < 7 ? &c->frames[l->depth].args[l->slot] : 0;
    default:
        return 0;
    }
}

static void loc_frame(aml_ctx_t *c, aml_loc_t *l, uint8_t kind, uint8_t slot)
{
    l->kind = kind;
    l->slot = slot;
    l->depth = (uint16_t) (c->depth - 1);
    l->serial = c->frames[c->depth - 1].serial;
    l->node = 0;
}

static void loc_node(aml_loc_t *l, aml_node_t *n)
{
    l->kind = AML_LOC_NODE;
    l->slot = 0;
    l->depth = 0;
    l->serial = n->gen;
    l->node = n;
}

/* The container a reference points into (data value), or 0. */
static aml_val_t *ref_base(aml_ctx_t *c, aml_ref_t *r, aml_node_t **node)
{
    if (r->base.kind == AML_LOC_OWNED) {
        *node = 0;
        return &r->owned;
    }
    return loc_slot(c, &r->base, node);
}

static int ref_read(aml_ctx_t *c, aml_ref_t *r, aml_val_t *out, uint32_t depth)
{
    aml_node_t *n;
    aml_val_t *b = ref_base(c, r, &n);
    val_zero(out);
    if (!b) return AML_ENOENT;
    if (!r->indexed) {
        if (n) return read_node(c, n, out);
        return aml_val_copy(c, out, b);
    }
    if (n && n->v.type > AML_T_PKG) return AML_ETYPE;
    if (r->index >= b->len) return AML_EBOUNDS;
    if (b->type == AML_T_PKG) return aml_val_copy(c, out, &b->u.elems[r->index]);
    if (b->type == AML_T_BUF || b->type == AML_T_STR) {
        *out = aml_int(b->u.bytes[r->index]);
        return AML_OK;
    }
    (void) depth;
    return AML_ETYPE;
}

static int ref_store(aml_ctx_t *c, aml_ref_t *r, const aml_val_t *v, int copyobj)
{
    aml_node_t *n;
    aml_val_t *b = ref_base(c, r, &n);
    int e;
    if (!b) return AML_ENOENT;
    if (!r->indexed) {
        if (n) return store_node(c, n, v, copyobj);
        aml_val_t t;
        e = aml_val_copy(c, &t, v);
        if (e) return e;
        aml_val_free(c, b);
        *b = t;
        return AML_OK;
    }
    if (n && n->v.type > AML_T_PKG) return AML_ETYPE;
    if (r->index >= b->len) return AML_EBOUNDS;
    if (b->type == AML_T_PKG) {
        aml_val_t t;
        e = aml_val_copy(c, &t, v);
        if (e) return e;
        aml_val_free(c, &b->u.elems[r->index]);
        b->u.elems[r->index] = t;
        return AML_OK;
    }
    if (b->type == AML_T_BUF || b->type == AML_T_STR) {
        uint64_t x;
        e = aml_to_int(c, v, &x);
        if (e) return e;
        b->u.bytes[r->index] = (uint8_t) x;
        return AML_OK;
    }
    return AML_ETYPE;
}

/* ---- conversions ------------------------------------------------------------------ */
static int hexval(uint8_t ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int to_int_d(aml_ctx_t *c, const aml_val_t *v, uint64_t *out, uint32_t depth)
{
    uint64_t r = 0;
    switch (v->type) {
    case AML_T_INT:
        r = v->u.i;
        break;
    case AML_T_STR: {
        uint32_t i = 0;
        while (i < v->len && (v->u.bytes[i] == ' ' || v->u.bytes[i] == '\t')) i++;
        if (i + 1 < v->len && v->u.bytes[i] == '0' &&
            (v->u.bytes[i + 1] == 'x' || v->u.bytes[i + 1] == 'X'))
            i += 2;
        for (uint32_t k = 0; i < v->len && k < 16; i++, k++) {
            int h = hexval(v->u.bytes[i]);
            if (h < 0) break;
            r = (r << 4) | (uint64_t) h;
        }
        break;
    }
    case AML_T_BUF: {
        uint32_t n = v->len < int_bytes(c) ? v->len : int_bytes(c);
        for (uint32_t i = 0; i < n; i++) r |= (uint64_t) v->u.bytes[i] << (8 * i);
        break;
    }
    case AML_T_REF: {
        aml_val_t t;
        if (depth > 8) return AML_ELIMIT;
        int e = ref_read(c, v->u.ref, &t, depth + 1);
        if (!e) e = to_int_d(c, &t, &r, depth + 1);
        aml_val_free(c, &t);
        if (e) return e;
        break;
    }
    default:
        return AML_ETYPE;
    }
    *out = imask(c, r);
    return AML_OK;
}

int aml_to_int(aml_ctx_t *c, const aml_val_t *v, uint64_t *out)
{
    return to_int_d(c, v, out, 0);
}

int aml_val_to_int(aml_ctx_t *c, const aml_val_t *v, uint64_t *out)
{
    if (!c || !v || !out) return AML_EARG;
    return to_int_d(c, v, out, 0);
}

int aml_to_buf(aml_ctx_t *c, const aml_val_t *v, aml_val_t *out)
{
    int r;
    switch (v->type) {
    case AML_T_INT:
        r = aml_mkbuf(c, out, int_bytes(c));
        for (uint32_t i = 0; !r && i < out->len; i++) out->u.bytes[i] = (uint8_t) (v->u.i >> (8 * i));
        return r;
    case AML_T_STR:
        r = aml_mkbuf(c, out, v->len + 1);
        if (!r) aml_bcopy(out->u.bytes, v->u.bytes, v->len);
        return r;
    case AML_T_BUF:
        return aml_val_copy(c, out, v);
    default:
        val_zero(out);
        return AML_ETYPE;
    }
}

static const char HEXD[] = "0123456789ABCDEF";

/* mode 0 implicit ("01 02", int as full-width hex), 1 ToHexString ("0x01,0x02"),
 * 2 ToDecimalString ("1,2", int in decimal). */
static int to_str(aml_ctx_t *c, const aml_val_t *v, aml_val_t *out, int mode)
{
    int r;
    if (v->type == AML_T_STR) return aml_val_copy(c, out, v);
    if (v->type == AML_T_INT) {
        char t[24];
        uint32_t n = 0;
        uint64_t x = v->u.i;
        if (mode == 2) {
            do {
                t[n++] = (char) ('0' + x % 10);
                x /= 10;
            } while (x);
        } else {
            for (uint32_t i = 0; i < 2 * int_bytes(c); i++, x >>= 4) t[n++] = HEXD[x & 15];
        }
        r = aml_mkstr(c, out, n);
        for (uint32_t i = 0; !r && i < n; i++) out->u.bytes[i] = (uint8_t) t[n - 1 - i];
        return r;
    }
    if (v->type == AML_T_BUF) {
        uint32_t per = mode == 1 ? 5 : mode == 2 ? 4 : 3, n = 0;
        if (v->len > AML_MAX_OBJ / 5) return AML_ENOMEM;
        r = aml_mkstr(c, out, v->len * per);
        if (r) return r;
        for (uint32_t i = 0; i < v->len; i++) {
            uint8_t b = v->u.bytes[i];
            if (i) out->u.bytes[n++] = mode == 0 ? ' ' : ',';
            if (mode == 2) {
                if (b >= 100) out->u.bytes[n++] = (uint8_t) ('0' + b / 100);
                if (b >= 10) out->u.bytes[n++] = (uint8_t) ('0' + (b / 10) % 10);
                out->u.bytes[n++] = (uint8_t) ('0' + b % 10);
            } else {
                if (mode == 1) {
                    out->u.bytes[n++] = '0';
                    out->u.bytes[n++] = 'x';
                }
                out->u.bytes[n++] = (uint8_t) HEXD[b >> 4];
                out->u.bytes[n++] = (uint8_t) HEXD[b & 15];
            }
        }
        out->len = n;
        out->u.bytes[n] = 0;
        return AML_OK;
    }
    val_zero(out);
    return AML_ETYPE;
}

/* Compare a to b converted to a's type: *cmp <0, 0, >0. */
static int compare(aml_ctx_t *c, const aml_val_t *a, const aml_val_t *b, int *cmp)
{
    int r;
    if (a->type == AML_T_INT || a->type == AML_T_REF) {
        uint64_t x, y;
        r = aml_to_int(c, a, &x);
        if (!r) r = aml_to_int(c, b, &y);
        if (!r) *cmp = x < y ? -1 : x > y ? 1 : 0;
        return r;
    }
    if (a->type != AML_T_STR && a->type != AML_T_BUF) return AML_ETYPE;
    aml_val_t t;
    r = a->type == AML_T_STR ? to_str(c, b, &t, 0) : aml_to_buf(c, b, &t);
    if (r) return r;
    uint32_t n = a->len < t.len ? a->len : t.len, i;
    *cmp = 0;
    for (i = 0; i < n; i++)
        if (a->u.bytes[i] != t.u.bytes[i]) {
            *cmp = a->u.bytes[i] < t.u.bytes[i] ? -1 : 1;
            break;
        }
    if (!*cmp && a->len != t.len) *cmp = a->len < t.len ? -1 : 1;
    aml_val_free(c, &t);
    return AML_OK;
}

/* ---- buffer fields ---------------------------------------------------------------- */
static uint64_t bits_get64(const uint8_t *p, uint32_t bitoff, uint32_t n)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < n; i++)
        if (p[(bitoff + i) >> 3] & (1u << ((bitoff + i) & 7))) v |= 1ULL << i;
    return v;
}

static int bf_container(aml_ctx_t *c, aml_node_t *f, aml_val_t **buf)
{
    aml_node_t *n;
    aml_val_t *b = f->x.bf.target.kind == AML_LOC_OWNED ? &f->x.bf.owned
                                                         : loc_slot(c, &f->x.bf.target, &n);
    if (!b) return AML_ENOENT;
    if (b->type != AML_T_BUF) return AML_ETYPE;
    if ((uint64_t) f->x.bf.bitoff + f->x.bf.bitlen > (uint64_t) b->len * 8) return AML_EBOUNDS;
    *buf = b;
    return AML_OK;
}

static int bf_read(aml_ctx_t *c, aml_node_t *f, aml_val_t *out)
{
    aml_val_t *b;
    int r = bf_container(c, f, &b);
    val_zero(out);
    if (r) return r;
    uint32_t off = f->x.bf.bitoff, len = f->x.bf.bitlen;
    if (len <= int_bytes(c) * 8) {
        *out = aml_int(bits_get64(b->u.bytes, off, len));
        return AML_OK;
    }
    r = aml_mkbuf(c, out, (len + 7) / 8);
    for (uint32_t i = 0; !r && i < len; i++)
        if (b->u.bytes[(off + i) >> 3] & (1u << ((off + i) & 7)))
            out->u.bytes[i >> 3] |= (uint8_t) (1u << (i & 7));
    return r;
}

static int bf_write(aml_ctx_t *c, aml_node_t *f, const aml_val_t *v)
{
    aml_val_t *b, src;
    int r = bf_container(c, f, &b);
    if (r) return r;
    if (v->type == AML_T_INT || v->type == AML_T_REF) {
        uint64_t x;
        r = aml_to_int(c, v, &x);
        if (r) return r;
        r = aml_mkbuf(c, &src, 8);
        for (uint32_t i = 0; !r && i < 8; i++) src.u.bytes[i] = (uint8_t) (x >> (8 * i));
    } else {
        r = aml_to_buf(c, v, &src);
        if (!r && v->type == AML_T_STR && src.len) src.len--; /* no NUL */
    }
    if (r) return r;
    uint32_t off = f->x.bf.bitoff, len = f->x.bf.bitlen;
    for (uint32_t i = 0; i < len; i++) {
        uint32_t bit = (i >> 3) < src.len ? (src.u.bytes[i >> 3] >> (i & 7)) & 1u : 0u;
        uint8_t m = (uint8_t) (1u << ((off + i) & 7));
        if (bit)
            b->u.bytes[(off + i) >> 3] |= m;
        else
            b->u.bytes[(off + i) >> 3] &= (uint8_t) ~m;
    }
    aml_val_free(c, &src);
    return AML_OK;
}

/* ---- reading and storing named objects ----------------------------------------------- */
static int read_node(aml_ctx_t *c, aml_node_t *n, aml_val_t *out)
{
    val_zero(out);
    n = aml_ns_resolve(c, n);
    if (!n) return AML_ENOENT;
    switch (n->v.type) {
    case AML_T_UNINIT:
    case AML_T_INT:
    case AML_T_STR:
    case AML_T_BUF:
    case AML_T_PKG:
    case AML_T_REF:
        return aml_val_copy(c, out, &n->v);
    case AML_T_FIELD:
        return aml_field_read(c, n, out);
    case AML_T_BUFFIELD:
        return bf_read(c, n, out);
    case AML_T_METHOD:
        if (n->x.m.flags & 7) return AML_ETYPE;
        return aml_call(c, n, 0, 0, out);
    default: {
        int r = mkref(c, out);
        if (!r) loc_node(&out->u.ref->base, n);
        return r;
    }
    }
}

static int is_data(uint8_t t)
{
    return t == AML_T_INT || t == AML_T_STR || t == AML_T_BUF || t == AML_T_PKG || t == AML_T_REF;
}

static int store_node(aml_ctx_t *c, aml_node_t *n, const aml_val_t *v, int copyobj)
{
    aml_val_t t;
    int r;
    n = aml_ns_resolve(c, n);
    if (!n) return AML_ENOENT;
    if (n->v.type == AML_T_FIELD) return aml_field_write(c, n, v);
    if (n->v.type == AML_T_BUFFIELD) return bf_write(c, n, v);
    if (n->v.type != AML_T_UNINIT && !is_data(n->v.type)) return AML_ETYPE;
    if (!is_data(v->type)) return AML_ETYPE;
    uint8_t nt = n->v.type;
    if (copyobj || nt == AML_T_UNINIT || nt == AML_T_PKG || nt == AML_T_REF ||
        v->type == AML_T_PKG || v->type == AML_T_REF) {
        r = aml_val_copy(c, &t, v);
    } else if (nt == AML_T_INT) {
        uint64_t x;
        r = aml_to_int(c, v, &x);
        t = aml_int(x);
    } else if (nt == AML_T_STR) {
        r = to_str(c, v, &t, 0);
    } else {
        r = aml_to_buf(c, v, &t);
        if (!r && n->v.len) { /* a named buffer keeps its length */
            uint32_t keep = n->v.len;
            for (uint32_t i = 0; i < keep; i++) n->v.u.bytes[i] = i < t.len ? t.u.bytes[i] : 0;
            aml_val_free(c, &t);
            return AML_OK;
        }
    }
    if (r) return r;
    aml_val_free(c, &n->v);
    n->v = t;
    return AML_OK;
}

static void log_debug(aml_ctx_t *c, const aml_val_t *v)
{
    c->debug_lines++;
    if (v->type == AML_T_INT) aml_logf(c, "Debug:", 0, v->u.i, 1);
    else if (v->type == AML_T_STR) aml_logf(c, "Debug:", (const char *) v->u.bytes, 0, -1);
    else if (v->type == AML_T_BUF) aml_logf(c, "Debug: buffer, bytes", 0, v->len, 0);
    else if (v->type == AML_T_PKG) aml_logf(c, "Debug: package, elements", 0, v->len, 0);
    else aml_logf(c, "Debug: object of type", 0, v->type, 0);
}

static int store_tgt(aml_ctx_t *c, tgt_t *t, const aml_val_t *v, int copyobj)
{
    aml_frame_t *f = cur_frame(c);
    aml_val_t *slot, tmp;
    int r;
    switch (t->kind) {
    case AML_LOC_NONE:
        return AML_OK;
    case AML_LOC_DEBUG:
        log_debug(c, v);
        return AML_OK;
    case AML_LOC_NODE:
        return store_node(c, t->node, v, copyobj);
    case TGT_REF:
        return ref_store(c, t->ref.u.ref, v, copyobj);
    case AML_LOC_LOCAL:
    case AML_LOC_ARG:
        if (!f) return AML_EPARSE;
        slot = t->kind == AML_LOC_LOCAL ? &f->locals[t->slot] : &f->args[t->slot];
        if (t->kind == AML_LOC_ARG && !copyobj && slot->type == AML_T_REF)
            return ref_store(c, slot->u.ref, v, 0);
        r = aml_val_copy(c, &tmp, v);
        if (r) return r;
        aml_val_free(c, slot);
        *slot = tmp;
        return AML_OK;
    default:
        return AML_ETYPE;
    }
}

static int parse_super(aml_ctx_t *c, st_t *s, tgt_t *t, int flags)
{
    t->kind = AML_LOC_NONE;
    t->slot = 0;
    t->node = 0;
    val_zero(&t->ref);
    if (s->pc >= s->end) return AML_EPARSE;
    uint8_t b = s->a[s->pc];
    if (b == 0x00) {
        if (!(flags & SUP_NULL_OK)) return AML_EPARSE;
        s->pc++;
        return AML_OK;
    }
    if (b >= 0x60 && b <= 0x6E) {
        if (!cur_frame(c)) return AML_EPARSE;
        s->pc++;
        t->kind = b <= 0x67 ? AML_LOC_LOCAL : AML_LOC_ARG;
        t->slot = (uint8_t) (b <= 0x67 ? b - 0x60 : b - 0x68);
        return AML_OK;
    }
    if (b == 0x5B) {
        if (s->pc + 1 < s->end && s->a[s->pc + 1] == 0x31) {
            s->pc += 2;
            t->kind = AML_LOC_DEBUG;
            return AML_OK;
        }
        return AML_EPARSE;
    }
    if (is_name_start(b)) {
        nstr_t n;
        int r = parse_nstr(s, &n);
        if (r) return r;
        aml_node_t *nd = aml_ns_resolve(c, resolve_n(c, s->scope, &n, 0));
        if (!nd) {
            if (!(flags & SUP_MISSING_OK)) {
                char p[64];
                nstr_text(&n, p, sizeof p);
                aml_logf(c, "name not found:", p, 0, -1);
            }
            return AML_ENOENT;
        }
        t->kind = AML_LOC_NODE;
        t->node = nd;
        return AML_OK;
    }
    if (b == 0x83) { /* DerefOf as a target: the location it names */
        aml_val_t v;
        s->pc++;
        int r = eval_term(c, s, &v);
        if (r) return r;
        if (v.type == AML_T_REF) {
            t->kind = TGT_REF;
            t->ref = v;
            return AML_OK;
        }
        if (v.type == AML_T_STR) {
            aml_node_t *nd = aml_ns_resolve(c, lookup_ascii(c, s->scope, (const char *) v.u.bytes, 1));
            aml_val_free(c, &v);
            if (!nd) return AML_ENOENT;
            t->kind = AML_LOC_NODE;
            t->node = nd;
            return AML_OK;
        }
        aml_val_free(c, &v);
        return AML_ETYPE;
    }
    if (b == 0x71 || b == 0x88) {
        aml_val_t v;
        int r = eval_term(c, s, &v);
        if (r) return r;
        if (v.type != AML_T_REF) {
            aml_val_free(c, &v);
            return AML_ETYPE;
        }
        t->kind = TGT_REF;
        t->ref = v;
        return AML_OK;
    }
    return AML_EPARSE;
}

static int read_tgt(aml_ctx_t *c, tgt_t *t, aml_val_t *out)
{
    aml_frame_t *f = cur_frame(c);
    val_zero(out);
    switch (t->kind) {
    case AML_LOC_NODE:
        return read_node(c, t->node, out);
    case TGT_REF:
        return ref_read(c, t->ref.u.ref, out, 0);
    case AML_LOC_LOCAL:
        return aml_val_copy(c, out, &f->locals[t->slot]);
    case AML_LOC_ARG:
        if (f->args[t->slot].type == AML_T_REF) return ref_read(c, f->args[t->slot].u.ref, out, 0);
        return aml_val_copy(c, out, &f->args[t->slot]);
    default:
        return AML_ETYPE;
    }
}

/* A reference value naming t's location. */
static int tgt_ref(aml_ctx_t *c, tgt_t *t, aml_val_t *out)
{
    int r;
    if (t->kind == TGT_REF) {
        *out = t->ref;
        val_zero(&t->ref);
        t->kind = AML_LOC_NONE;
        return AML_OK;
    }
    if (t->kind != AML_LOC_NODE && t->kind != AML_LOC_LOCAL && t->kind != AML_LOC_ARG)
        return AML_ETYPE;
    r = mkref(c, out);
    if (r) return r;
    if (t->kind == AML_LOC_NODE)
        loc_node(&out->u.ref->base, t->node);
    else
        loc_frame(c, &out->u.ref->base, t->kind, t->slot);
    return AML_OK;
}

/* Source operand that may be referenced in place (Index, CreateField): a
 * named data object or a Local/Arg slot stays where it is; anything else is
 * evaluated into *owned. */
static int parse_source(aml_ctx_t *c, st_t *s, aml_loc_t *loc, aml_val_t *owned)
{
    aml_frame_t *f = cur_frame(c);
    val_zero(owned);
    aml_bzero(loc, (uint32_t) sizeof *loc);
    if (s->pc >= s->end) return AML_EPARSE;
    uint8_t b = s->a[s->pc];
    if (b >= 0x60 && b <= 0x6E && f) {
        s->pc++;
        uint8_t kind = b <= 0x67 ? AML_LOC_LOCAL : AML_LOC_ARG;
        uint8_t slot = (uint8_t) (b <= 0x67 ? b - 0x60 : b - 0x68);
        aml_val_t *v = kind == AML_LOC_LOCAL ? &f->locals[slot] : &f->args[slot];
        if (v->type == AML_T_REF && !v->u.ref->indexed && v->u.ref->base.kind != AML_LOC_OWNED) {
            *loc = v->u.ref->base;
            return AML_OK;
        }
        loc_frame(c, loc, kind, slot);
        return AML_OK;
    }
    if (is_name_start(b)) {
        st_t save = *s;
        nstr_t n;
        int r = parse_nstr(s, &n);
        if (r) return r;
        aml_node_t *nd = aml_ns_resolve(c, resolve_n(c, s->scope, &n, 0));
        if (nd && is_data(nd->v.type) && nd->v.type != AML_T_REF) {
            loc_node(loc, nd);
            return AML_OK;
        }
        *s = save;
    }
    loc->kind = AML_LOC_OWNED;
    return eval_term(c, s, owned);
}

/* ---- method invocation ----------------------------------------------------------- */
static void frame_pop(aml_ctx_t *c)
{
    aml_frame_t *f = &c->frames[c->depth - 1];
    for (int i = 0; i < 8; i++) aml_val_free(c, &f->locals[i]);
    for (int i = 0; i < 7; i++) aml_val_free(c, &f->args[i]);
    aml_node_t *t = f->temps;
    f->temps = 0;
    while (t) {
        aml_node_t *nx = t->tnext;
        node_destroy(c, t);
        t = nx;
    }
    f->serial = 0;
    c->depth--;
}

static aml_frame_t *frame_push(aml_ctx_t *c, aml_node_t *method, aml_node_t *scope)
{
    if (c->depth >= AML_MAX_CALL_DEPTH) return 0;
    aml_frame_t *f = &c->frames[c->depth];
    aml_bzero(f, (uint32_t) sizeof *f);
    f->method = method;
    f->scope = scope;
    f->serial = ++c->serial;
    c->depth++;
    return f;
}

int aml_call(aml_ctx_t *c, aml_node_t *m, aml_val_t *args, uint32_t argc, aml_val_t *ret)
{
    int r;
    val_zero(ret);
    m = aml_ns_resolve(c, m);
    if (!m || m->v.type != AML_T_METHOD || argc > 7) {
        for (uint32_t i = 0; args && i < argc && i < 7; i++) aml_val_free(c, &args[i]);
        return AML_ETYPE;
    }
    if (m->x.m.native) {
        r = m->x.m.native(c, m, args, argc, ret);
        for (uint32_t i = 0; args && i < argc; i++) aml_val_free(c, &args[i]);
        return r;
    }
    aml_frame_t *f = frame_push(c, m, m);
    if (!f) {
        for (uint32_t i = 0; args && i < argc; i++) aml_val_free(c, &args[i]);
        return fail(c, AML_ELIMIT, "call depth");
    }
    for (uint32_t i = 0; args && i < argc; i++) {
        if (i < (uint32_t) (m->x.m.flags & 7))
            f->args[i] = args[i];
        else
            aml_val_free(c, &args[i]);
        val_zero(&args[i]);
    }
    st_t s;
    s.a = m->x.m.code;
    s.pc = 0;
    s.end = m->x.m.len;
    s.scope = m;
    r = exec_termlist(c, &s, s.end);
    if (r == FLOW_RETURN) {
        *ret = c->ret;
        val_zero(&c->ret);
        r = AML_OK;
    } else if (r == FLOW_BREAK || r == FLOW_CONTINUE) {
        r = AML_EPARSE;
    }
    aml_val_free(c, &c->ret);
    frame_pop(c);
    if (r) {
        aml_val_free(c, ret);
        fail(c, r, "method");
    }
    return r;
}

static int call_from_stream(aml_ctx_t *c, st_t *s, aml_node_t *m, aml_val_t *out)
{
    aml_val_t args[7];
    uint32_t argc = m->x.m.flags & 7;
    int r = AML_OK;
    for (uint32_t i = 0; i < 7; i++) val_zero(&args[i]);
    for (uint32_t i = 0; i < argc && !r; i++) r = eval_term(c, s, &args[i]);
    if (r) {
        for (uint32_t i = 0; i < argc; i++) aml_val_free(c, &args[i]);
        return r;
    }
    return aml_call(c, m, args, argc, out);
}

/* ---- data objects ---------------------------------------------------------------- */
static int parse_buffer(aml_ctx_t *c, st_t *s, aml_val_t *out)
{
    uint32_t end, save = s->end;
    uint64_t size;
    int r = rd_pkglen(s, &end);
    if (r) return r;
    s->end = end;
    r = eval_int(c, s, &size);
    s->end = save;
    if (r) return r;
    uint32_t init = end - s->pc;
    if (size > AML_MAX_OBJ) return AML_ENOMEM;
    r = aml_mkbuf(c, out, (uint32_t) size > init ? (uint32_t) size : init);
    if (r) return r;
    aml_bcopy(out->u.bytes, s->a + s->pc, init);
    s->pc = end;
    return AML_OK;
}

static int parse_package(aml_ctx_t *c, st_t *s, aml_val_t *out, int var)
{
    uint32_t end, save = s->end;
    uint64_t num;
    int r = rd_pkglen(s, &end);
    if (r) return r;
    s->end = end;
    if (var) {
        r = eval_int(c, s, &num);
    } else {
        uint8_t b;
        r = rd8(s, &b);
        num = b;
    }
    if (!r && num > AML_MAX_PKG) r = AML_ENOMEM;
    if (!r) r = mkpkg(c, out, (uint32_t) num);
    for (uint32_t i = 0; !r && s->pc < end; i++) {
        aml_val_t e;
        val_zero(&e);
        if (is_name_start(s->a[s->pc])) {
            nstr_t n;
            r = parse_nstr(s, &n);
            if (r) break;
            aml_node_t *nd = resolve_n(c, s->scope, &n, 0);
            aml_node_t *tn = nd ? aml_ns_resolve(c, nd) : 0;
            uint32_t ty = tn ? tn->v.type : 0;
            /* As ACPICA does: a name of a data object becomes its value; a
             * field is read too, except while a table loads (no hardware
             * access from a definition block); anything else (a device, a
             * method) stays a reference. */
            if (tn && (ty == AML_T_INT || ty == AML_T_STR || ty == AML_T_BUF ||
                       ty == AML_T_PKG ||
                       (!c->loading && (ty == AML_T_FIELD || ty == AML_T_BUFFIELD)))) {
                r = read_node(c, tn, &e);
            } else if (nd) {
                r = mkref(c, &e);
                if (!r) loc_node(&e.u.ref->base, nd);
            } else {
                char p[64];
                nstr_text(&n, p, sizeof p);
                e = aml_str(c, p);
                if (e.type != AML_T_STR) r = AML_ENOMEM;
            }
        } else {
            r = eval_term(c, s, &e);
        }
        if (r) break;
        if (i < out->len)
            out->u.elems[i] = e;
        else
            aml_val_free(c, &e);
    }
    s->end = save;
    if (r) {
        aml_val_free(c, out);
        return r;
    }
    s->pc = end;
    return AML_OK;
}

static int parse_string(aml_ctx_t *c, st_t *s, aml_val_t *out)
{
    uint32_t start = s->pc;
    while (s->pc < s->end && s->a[s->pc]) s->pc++;
    if (s->pc >= s->end) return AML_EPARSE;
    uint32_t n = s->pc - start;
    s->pc++;
    int r = aml_mkstr(c, out, n);
    if (!r) aml_bcopy(out->u.bytes, s->a + start, n);
    return r;
}

/* ---- operators ------------------------------------------------------------------ */
static int finish(aml_ctx_t *c, st_t *s, aml_val_t *res, aml_val_t *out)
{
    tgt_t t;
    int r = parse_super(c, s, &t, SUP_NULL_OK);
    if (!r) r = store_tgt(c, &t, res, 0);
    aml_val_free(c, &t.ref);
    if (r) {
        aml_val_free(c, res);
        return r;
    }
    *out = *res;
    return AML_OK;
}

static int finish_int(aml_ctx_t *c, st_t *s, uint64_t v, aml_val_t *out)
{
    aml_val_t res = aml_int(imask(c, v));
    return finish(c, s, &res, out);
}

static uint64_t bool_v(const aml_ctx_t *c, int b)
{
    return b ? aml_ones(c) : 0;
}

static int op_binary(aml_ctx_t *c, st_t *s, uint8_t op, aml_val_t *out)
{
    uint64_t a, b, r = 0;
    int e = eval_int(c, s, &a);
    if (!e) e = eval_int(c, s, &b);
    if (e) return e;
    switch (op) {
    case 0x72: r = a + b; break;
    case 0x74: r = a - b; break;
    case 0x77: r = a * b; break;
    case 0x79: r = b >= 64 ? 0 : a << b; break;
    case 0x7A: r = b >= 64 ? 0 : a >> b; break;
    case 0x7B: r = a & b; break;
    case 0x7C: r = ~(a & b); break;
    case 0x7D: r = a | b; break;
    case 0x7E: r = ~(a | b); break;
    case 0x7F: r = a ^ b; break;
    case 0x85:
        if (!b) return AML_EDIV0;
        r = a % b;
        break;
    default: return AML_EPARSE;
    }
    return finish_int(c, s, r, out);
}

static int op_divide(aml_ctx_t *c, st_t *s, aml_val_t *out)
{
    uint64_t a, b;
    int e = eval_int(c, s, &a);
    if (!e) e = eval_int(c, s, &b);
    if (e) return e;
    if (!b) return AML_EDIV0;
    aml_val_t rem = aml_int(a % b), quo;
    tgt_t t;
    e = parse_super(c, s, &t, SUP_NULL_OK);
    if (!e) e = store_tgt(c, &t, &rem, 0);
    aml_val_free(c, &t.ref);
    if (e) return e;
    quo = aml_int(a / b);
    return finish(c, s, &quo, out);
}

static int op_concat(aml_ctx_t *c, st_t *s, aml_val_t *out)
{
    aml_val_t a, b, res, t;
    int r;
    val_zero(&b);
    val_zero(&t);
    r = eval_term(c, s, &a);
    if (r) return r;
    r = eval_term(c, s, &b);
    if (r) goto done;
    if (a.type == AML_T_REF) {
        uint64_t x;
        r = aml_to_int(c, &a, &x);
        if (r) goto done;
        aml_val_free(c, &a);
        a = aml_int(x);
    }
    if (a.type == AML_T_INT) {
        uint64_t y;
        uint32_t w = int_bytes(c);
        r = aml_to_int(c, &b, &y);
        if (!r) r = aml_mkbuf(c, &res, 2 * w);
        if (r) goto done;
        for (uint32_t i = 0; i < w; i++) {
            res.u.bytes[i] = (uint8_t) (a.u.i >> (8 * i));
            res.u.bytes[w + i] = (uint8_t) (y >> (8 * i));
        }
    } else if (a.type == AML_T_STR) {
        r = to_str(c, &b, &t, 0);
        if (!r) r = aml_mkstr(c, &res, a.len + t.len);
        if (r) goto done;
        aml_bcopy(res.u.bytes, a.u.bytes, a.len);
        aml_bcopy(res.u.bytes + a.len, t.u.bytes, t.len);
    } else if (a.type == AML_T_BUF) {
        r = aml_to_buf(c, &b, &t);
        if (!r && b.type == AML_T_STR && t.len) t.len--;
        if (!r) r = aml_mkbuf(c, &res, a.len + t.len);
        if (r) goto done;
        aml_bcopy(res.u.bytes, a.u.bytes, a.len);
        aml_bcopy(res.u.bytes + a.len, t.u.bytes, t.len);
    } else {
        r = AML_ETYPE;
        goto done;
    }
    r = finish(c, s, &res, out);
done:
    aml_val_free(c, &a);
    aml_val_free(c, &b);
    aml_val_free(c, &t);
    return r;
}

static uint32_t rt_len(const aml_val_t *b)
{
    /* resource template length without its end tag (0x79 xx) */
    uint32_t i = 0;
    while (i < b->len) {
        uint8_t tag = b->u.bytes[i];
        uint32_t l;
        if (tag & 0x80) {
            if (i + 3 > b->len) return i;
            l = 3u + ((uint32_t) b->u.bytes[i + 1] | ((uint32_t) b->u.bytes[i + 2] << 8));
        } else {
            if (((tag >> 3) & 0x0F) == 0x0F) return i;
            l = 1u + (tag & 7u);
        }
        if (l > b->len - i) return i;
        i += l;
    }
    return i;
}

static int op_concatres(aml_ctx_t *c, st_t *s, aml_val_t *out)
{
    aml_val_t a, b, res;
    int r;
    val_zero(&b);
    r = eval_term(c, s, &a);
    if (!r) r = eval_term(c, s, &b);
    if (!r && (a.type != AML_T_BUF || b.type != AML_T_BUF)) r = AML_ETYPE;
    if (!r) {
        uint32_t la = rt_len(&a), lb = rt_len(&b);
        r = aml_mkbuf(c, &res, la + lb + 2);
        if (!r) {
            aml_bcopy(res.u.bytes, a.u.bytes, la);
            aml_bcopy(res.u.bytes + la, b.u.bytes, lb);
            res.u.bytes[la + lb] = 0x79;
            res.u.bytes[la + lb + 1] = 0;
            r = finish(c, s, &res, out);
        }
    }
    aml_val_free(c, &a);
    aml_val_free(c, &b);
    return r;
}

static int op_index(aml_ctx_t *c, st_t *s, aml_val_t *out)
{
    aml_loc_t loc;
    aml_val_t owned, res;
    uint64_t idx;
    int r = parse_source(c, s, &loc, &owned);
    if (r) return r;
    r = eval_int(c, s, &idx);
    if (!r) r = mkref(c, &res);
    if (r) {
        aml_val_free(c, &owned);
        return r;
    }
    aml_ref_t *ref = res.u.ref;
    ref->base = loc;
    ref->owned = owned;
    ref->indexed = 1;
    aml_node_t *n;
    aml_val_t *b = ref_base(c, ref, &n);
    if (!b || (b->type != AML_T_PKG && b->type != AML_T_BUF && b->type != AML_T_STR))
        r = AML_ETYPE;
    else if (idx >= b->len)
        r = AML_EBOUNDS;
    if (r) {
        aml_val_free(c, &res);
        return r;
    }
    ref->index = (uint32_t) idx;
    return finish(c, s, &res, out);
}

static int match_one(aml_ctx_t *c, uint8_t op, const aml_val_t *e, const aml_val_t *v, int *ok)
{
    int cmp, r;
    if (op == 0) {
        *ok = 1;
        return AML_OK;
    }
    r = compare(c, e, v, &cmp);
    if (r) return r;
    switch (op) {
    case 1: *ok = cmp == 0; break;
    case 2: *ok = cmp <= 0; break;
    case 3: *ok = cmp < 0; break;
    case 4: *ok = cmp >= 0; break;
    case 5: *ok = cmp > 0; break;
    default: return AML_EPARSE;
    }
    return AML_OK;
}

static int op_match(aml_ctx_t *c, st_t *s, aml_val_t *out)
{
    aml_val_t pkg, v1, v2;
    uint8_t o1 = 0, o2 = 0;
    uint64_t start;
    int r;
    val_zero(&v1);
    val_zero(&v2);
    r = eval_term(c, s, &pkg);
    if (!r) r = rd8(s, &o1);
    if (!r) r = eval_term(c, s, &v1);
    if (!r) r = rd8(s, &o2);
    if (!r) r = eval_term(c, s, &v2);
    if (!r) r = eval_int(c, s, &start);
    if (!r && pkg.type != AML_T_PKG) r = AML_ETYPE;
    if (!r && (o1 > 5 || o2 > 5)) r = AML_EPARSE;
    if (!r && start >= pkg.len) r = AML_EBOUNDS;
    uint64_t found = aml_ones(c);
    for (uint64_t i = start; !r && i < pkg.len; i++) {
        const aml_val_t *e = &pkg.u.elems[i];
        int a = 0, b = 0;
        if (e->type != AML_T_INT && e->type != AML_T_STR && e->type != AML_T_BUF) continue;
        r = match_one(c, o1, e, &v1, &a);
        if (!r && a) r = match_one(c, o2, e, &v2, &b);
        if (!r && a && b) {
            found = i;
            break;
        }
    }
    aml_val_free(c, &pkg);
    aml_val_free(c, &v1);
    aml_val_free(c, &v2);
    if (r) return r;
    *out = aml_int(found);
    return AML_OK;
}

static int op_mid(aml_ctx_t *c, st_t *s, aml_val_t *out)
{
    aml_val_t src, res;
    uint64_t idx, len;
    int r = eval_term(c, s, &src);
    if (!r) r = eval_int(c, s, &idx);
    if (!r) r = eval_int(c, s, &len);
    if (!r && src.type != AML_T_STR && src.type != AML_T_BUF) r = AML_ETYPE;
    if (r) {
        aml_val_free(c, &src);
        return r;
    }
    uint32_t from = idx >= src.len ? src.len : (uint32_t) idx;
    uint32_t n = len > (uint64_t) (src.len - from) ? src.len - from : (uint32_t) len;
    r = src.type == AML_T_STR ? aml_mkstr(c, &res, n) : aml_mkbuf(c, &res, n);
    if (!r) aml_bcopy(res.u.bytes, src.u.bytes + from, n);
    aml_val_free(c, &src);
    if (r) return r;
    return finish(c, s, &res, out);
}

static int op_tostring(aml_ctx_t *c, st_t *s, aml_val_t *out)
{
    aml_val_t src, res;
    uint64_t lim;
    int r = eval_term(c, s, &src);
    if (!r) r = eval_int(c, s, &lim);
    if (!r && src.type != AML_T_BUF) r = AML_ETYPE;
    if (r) {
        aml_val_free(c, &src);
        return r;
    }
    uint32_t n = 0;
    while (n < src.len && src.u.bytes[n] && n < lim) n++;
    r = aml_mkstr(c, &res, n);
    if (!r) aml_bcopy(res.u.bytes, src.u.bytes, n);
    aml_val_free(c, &src);
    if (r) return r;
    return finish(c, s, &res, out);
}

static int op_convert(aml_ctx_t *c, st_t *s, uint8_t op, aml_val_t *out)
{
    aml_val_t src, res;
    int r = eval_term(c, s, &src);
    if (r) return r;
    val_zero(&res);
    if (src.type == AML_T_REF) {
        uint64_t x;
        r = aml_to_int(c, &src, &x);
        aml_val_free(c, &src);
        if (r) return r;
        src = aml_int(x);
    }
    switch (op) {
    case 0x96: r = aml_to_buf(c, &src, &res); break;
    case 0x97: r = to_str(c, &src, &res, 2); break;
    case 0x98: r = to_str(c, &src, &res, 1); break;
    case 0x99:
        if (src.type == AML_T_STR) { /* explicit: decimal, or 0x hex */
            uint64_t v = 0;
            uint32_t i = 0;
            while (i < src.len && (src.u.bytes[i] == ' ' || src.u.bytes[i] == '\t')) i++;
            if (i + 1 < src.len && src.u.bytes[i] == '0' &&
                (src.u.bytes[i + 1] == 'x' || src.u.bytes[i + 1] == 'X')) {
                for (i += 2; i < src.len && hexval(src.u.bytes[i]) >= 0; i++)
                    v = (v << 4) | (uint64_t) hexval(src.u.bytes[i]);
            } else {
                for (; i < src.len && src.u.bytes[i] >= '0' && src.u.bytes[i] <= '9'; i++)
                    v = v * 10 + (uint64_t) (src.u.bytes[i] - '0');
            }
            res = aml_int(imask(c, v));
        } else {
            uint64_t v;
            r = aml_to_int(c, &src, &v);
            res = aml_int(v);
        }
        break;
    default: r = AML_EPARSE; break;
    }
    aml_val_free(c, &src);
    if (r) {
        aml_val_free(c, &res);
        return r;
    }
    return finish(c, s, &res, out);
}

static int op_sizeof(aml_ctx_t *c, st_t *s, aml_val_t *out)
{
    tgt_t t;
    aml_val_t v;
    int r = parse_super(c, s, &t, 0);
    if (!r) r = read_tgt(c, &t, &v);
    aml_val_free(c, &t.ref);
    if (r) return r;
    if (v.type == AML_T_REF) {
        aml_val_t d;
        r = ref_read(c, v.u.ref, &d, 0);
        aml_val_free(c, &v);
        if (r) return r;
        v = d;
    }
    if (v.type != AML_T_STR && v.type != AML_T_BUF && v.type != AML_T_PKG) r = AML_ETYPE;
    uint32_t n = v.len;
    aml_val_free(c, &v);
    if (r) return r;
    *out = aml_int(n);
    return AML_OK;
}

static uint32_t val_objtype(aml_ctx_t *c, const aml_val_t *v)
{
    if (v->type != AML_T_REF) return v->type <= AML_T_DEBUG ? v->type : AML_T_UNINIT;
    aml_node_t *n;
    aml_val_t *b = ref_base(c, v->u.ref, &n);
    if (!b) return AML_T_UNINIT;
    if (!v->u.ref->indexed) return n ? aml_node_type(aml_ns_resolve(c, n)) : val_objtype(c, b);
    if (b->type == AML_T_PKG && v->u.ref->index < b->len) {
        const aml_val_t *e = &b->u.elems[v->u.ref->index];
        return e->type == AML_T_REF ? AML_T_UNINIT : e->type;
    }
    return AML_T_BUFFIELD;
}

static int op_objtype(aml_ctx_t *c, st_t *s, aml_val_t *out)
{
    tgt_t t;
    int r = parse_super(c, s, &t, 0);
    if (r) return r;
    uint32_t ty = AML_T_UNINIT;
    aml_frame_t *f = cur_frame(c);
    switch (t.kind) {
    case AML_LOC_NODE: ty = aml_node_type(t.node); break;
    case AML_LOC_DEBUG: ty = AML_T_DEBUG; break;
    case AML_LOC_LOCAL: ty = val_objtype(c, &f->locals[t.slot]); break;
    case AML_LOC_ARG: ty = val_objtype(c, &f->args[t.slot]); break;
    case TGT_REF: ty = val_objtype(c, &t.ref); break;
    default: break;
    }
    aml_val_free(c, &t.ref);
    *out = aml_int(ty);
    return AML_OK;
}

static int op_incdec(aml_ctx_t *c, st_t *s, int inc, aml_val_t *out)
{
    tgt_t t;
    aml_val_t v;
    uint64_t x;
    int r = parse_super(c, s, &t, 0);
    if (!r) r = read_tgt(c, &t, &v);
    if (r) {
        aml_val_free(c, &t.ref);
        return r;
    }
    r = aml_to_int(c, &v, &x);
    aml_val_free(c, &v);
    if (!r) {
        v = aml_int(imask(c, inc ? x + 1 : x - 1));
        r = store_tgt(c, &t, &v, 0);
    }
    aml_val_free(c, &t.ref);
    if (r) return r;
    *out = v;
    return AML_OK;
}

static int op_derefof(aml_ctx_t *c, st_t *s, aml_val_t *out)
{
    aml_val_t v;
    int r = eval_term(c, s, &v);
    if (r) return r;
    if (v.type == AML_T_REF) {
        r = ref_read(c, v.u.ref, out, 0);
    } else if (v.type == AML_T_STR) {
        aml_node_t *n = lookup_ascii(c, s->scope, (const char *) v.u.bytes, 1);
        r = n ? read_node(c, n, out) : AML_ENOENT;
    } else {
        r = AML_ETYPE;
    }
    aml_val_free(c, &v);
    return r;
}

static uint64_t from_bcd(uint64_t v)
{
    uint64_t r = 0, m = 1;
    for (int i = 0; i < 16; i++, v >>= 4, m *= 10) r += (v & 15) * m;
    return r;
}

static uint64_t to_bcd(uint64_t v)
{
    uint64_t r = 0;
    for (int i = 0; i < 16 && v; i++, v /= 10) r |= (v % 10) << (4 * i);
    return r;
}

int aml_delay(aml_ctx_t *c, uint64_t us)
{
    if (us > AML_MAX_SLEEP_US || c->sleep_us + us > AML_MAX_SLEEP_US)
        return fail(c, AML_ELIMIT, "sleep budget");
    c->sleep_us += (uint32_t) us;
    c->fake_time += us * 10;
    if (c->plat && c->plat->delay_us) c->plat->delay_us(c->plat->ctx, (uint32_t) us);
    return AML_OK;
}

static int op_mutex(aml_ctx_t *c, st_t *s, int acquire, aml_val_t *out)
{
    tgt_t t;
    uint64_t tmo = 0;
    int r = parse_super(c, s, &t, 0);
    if (r) return r;
    if (acquire) r = rdle(s, 2, &tmo);
    aml_node_t *m = t.kind == AML_LOC_NODE ? t.node : 0;
    aml_val_free(c, &t.ref);
    if (r) return r;
    if (!m || m->v.type != AML_T_MUTEX) return AML_ETYPE;
    int gl = m->parent == c->root && m->name == seg_at((const uint8_t *) "_GL_");
    if (acquire) {
        if (gl && c->gl_count == 0 && c->plat && c->plat->global_lock &&
            !c->plat->global_lock(c->plat->ctx, 1)) {
            *out = aml_int(aml_ones(c)); /* timed out */
            return AML_OK;
        }
        if (gl) c->gl_count++;
        m->x.mx.depth++;
        *out = aml_int(0);
        return AML_OK;
    }
    if (!m->x.mx.depth) return AML_EMUTEX;
    m->x.mx.depth--;
    if (gl && c->gl_count && --c->gl_count == 0 && c->plat && c->plat->global_lock)
        c->plat->global_lock(c->plat->ctx, 0);
    return AML_OK;
}

static int op_event(aml_ctx_t *c, st_t *s, uint8_t op, aml_val_t *out)
{
    tgt_t t;
    uint64_t tmo = 0;
    int r = parse_super(c, s, &t, 0);
    if (!r && op == 0x25) r = eval_int(c, s, &tmo);
    aml_node_t *e = t.kind == AML_LOC_NODE ? t.node : 0;
    aml_val_free(c, &t.ref);
    if (r) return r;
    if (!e || e->v.type != AML_T_EVENT) return AML_ETYPE;
    if (op == 0x24) e->x.ev.count++;
    if (op == 0x26) e->x.ev.count = 0;
    if (op == 0x25) {
        /* single-threaded: nothing can signal while we wait */
        if (e->x.ev.count) {
            e->x.ev.count--;
            *out = aml_int(0);
        } else {
            *out = aml_int(aml_ones(c));
        }
    }
    return AML_OK;
}

static int op_notify(aml_ctx_t *c, st_t *s)
{
    tgt_t t;
    uint64_t v;
    int r = parse_super(c, s, &t, 0);
    if (!r) r = eval_int(c, s, &v);
    aml_node_t *n = t.kind == AML_LOC_NODE ? t.node : 0;
    if (!r && !n && t.kind == TGT_REF && !t.ref.u.ref->indexed) {
        aml_node_t *bn;
        if (ref_base(c, t.ref.u.ref, &bn) && bn) n = bn;
    }
    aml_val_free(c, &t.ref);
    if (r) return r;
    if (!n) return AML_ETYPE;
    uint32_t ty = n->v.type;
    if (ty != AML_T_DEVICE && ty != AML_T_THERMAL && ty != AML_T_PROC && ty != AML_T_POWER &&
        ty != AML_T_SCOPE)
        return AML_ETYPE;
    char p[64];
    aml_node_path(n, p, sizeof p);
    aml_logf(c, "Notify", p, v, 1);
    if (c->notify_count == AML_MAX_NOTIFY) {
        c->notify_dropped++;
        return AML_OK;
    }
    uint32_t at = (c->notify_head + c->notify_count) % AML_MAX_NOTIFY;
    c->notify[at].node = n;
    c->notify[at].value = (uint32_t) v;
    c->notify_count++;
    return AML_OK;
}

static int op_load(aml_ctx_t *c, st_t *s, aml_val_t *out);

/* Ext-prefixed expression opcodes. */
static int eval_ext(aml_ctx_t *c, st_t *s, uint8_t e, aml_val_t *out)
{
    uint64_t v;
    int r;
    switch (e) {
    case 0x12: { /* CondRefOf */
        tgt_t t;
        aml_val_t ref;
        r = parse_super(c, s, &t, SUP_MISSING_OK);
        if (r == AML_ENOENT) {
            r = parse_super(c, s, &t, SUP_NULL_OK);
            aml_val_free(c, &t.ref);
            if (r) return r;
            *out = aml_int(0);
            return AML_OK;
        }
        if (r) return r;
        r = tgt_ref(c, &t, &ref);
        aml_val_free(c, &t.ref);
        if (r) return r;
        r = parse_super(c, s, &t, SUP_NULL_OK);
        if (!r) r = store_tgt(c, &t, &ref, 0);
        aml_val_free(c, &t.ref);
        aml_val_free(c, &ref);
        if (r) return r;
        *out = aml_int(aml_ones(c));
        return AML_OK;
    }
    case 0x23: return op_mutex(c, s, 1, out);
    case 0x25: return op_event(c, s, e, out);
    case 0x28:
    case 0x29:
        r = eval_int(c, s, &v);
        if (r) return r;
        return finish_int(c, s, e == 0x28 ? from_bcd(v) : to_bcd(v), out);
    case 0x30: *out = aml_int(AML_REVISION); return AML_OK;
    case 0x33:
        v = c->plat && c->plat->timer_100ns ? c->plat->timer_100ns(c->plat->ctx) : c->fake_time;
        *out = aml_int(imask(c, v));
        return AML_OK;
    case 0x20: return op_load(c, s, out);
    case 0x1F: { /* LoadTable: tables outside the RSDT/XSDT walk are not loaded */
        for (int i = 0; i < 6; i++) {
            aml_val_t t;
            r = eval_term(c, s, &t);
            aml_val_free(c, &t);
            if (r) return r;
        }
        aml_logf(c, "LoadTable not supported; returning Zero", 0, 0, -1);
        *out = aml_int(0);
        return AML_OK;
    }
    default:
        return fail(c, AML_EPARSE, "ext opcode");
    }
}

static int eval_inner(aml_ctx_t *c, st_t *s, aml_val_t *out)
{
    uint8_t op;
    uint64_t v;
    int r;
    if (s->pc >= s->end) return AML_EPARSE;
    op = s->a[s->pc];
    if (is_name_start(op)) {
        nstr_t n;
        r = parse_nstr(s, &n);
        if (r) return r;
        aml_node_t *nd = aml_ns_resolve(c, resolve_n(c, s->scope, &n, 0));
        if (!nd) {
            char p[64];
            nstr_text(&n, p, sizeof p);
            aml_logf(c, "name not found:", p, 0, -1);
            return fail(c, AML_ENOENT, "name");
        }
        if (nd->v.type == AML_T_METHOD) return call_from_stream(c, s, nd, out);
        return read_node(c, nd, out);
    }
    s->pc++;
    switch (op) {
    case 0x00: *out = aml_int(0); return AML_OK;
    case 0x01: *out = aml_int(1); return AML_OK;
    case 0xFF: *out = aml_int(aml_ones(c)); return AML_OK;
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x0E:
        r = rdle(s, op == 0x0A ? 1 : op == 0x0B ? 2 : op == 0x0C ? 4 : 8, &v);
        if (!r) *out = aml_int(imask(c, v));
        return r;
    case 0x0D: return parse_string(c, s, out);
    case 0x11: return parse_buffer(c, s, out);
    case 0x12: return parse_package(c, s, out, 0);
    case 0x13: return parse_package(c, s, out, 1);
    case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
        if (!cur_frame(c)) return AML_EPARSE;
        return aml_val_copy(c, out, &cur_frame(c)->locals[op - 0x60]);
    case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6E:
        if (!cur_frame(c)) return AML_EPARSE;
        return aml_val_copy(c, out, &cur_frame(c)->args[op - 0x68]);
    case 0x70:
    case 0x9D: { /* Store, CopyObject */
        aml_val_t src;
        tgt_t t;
        r = eval_term(c, s, &src);
        if (r) return r;
        r = parse_super(c, s, &t, op == 0x70 ? SUP_NULL_OK : 0);
        if (!r) r = store_tgt(c, &t, &src, op == 0x9D);
        aml_val_free(c, &t.ref);
        if (r) {
            aml_val_free(c, &src);
            return r;
        }
        *out = src;
        return AML_OK;
    }
    case 0x71: { /* RefOf */
        tgt_t t;
        r = parse_super(c, s, &t, 0);
        if (!r) r = tgt_ref(c, &t, out);
        aml_val_free(c, &t.ref);
        return r;
    }
    case 0x72: case 0x74: case 0x77: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D:
    case 0x7E: case 0x7F: case 0x85:
        return op_binary(c, s, op, out);
    case 0x73: return op_concat(c, s, out);
    case 0x75: return op_incdec(c, s, 1, out);
    case 0x76: return op_incdec(c, s, 0, out);
    case 0x78: return op_divide(c, s, out);
    case 0x80:
        r = eval_int(c, s, &v);
        return r ? r : finish_int(c, s, ~v, out);
    case 0x81:
    case 0x82: { /* FindSetLeftBit / FindSetRightBit */
        uint64_t pos = 0;
        r = eval_int(c, s, &v);
        if (r) return r;
        for (uint32_t i = 0; i < 64 && v; i++)
            if (v & (1ULL << i)) {
                pos = i + 1;
                if (op == 0x82) break;
            }
        return finish_int(c, s, pos, out);
    }
    case 0x83: return op_derefof(c, s, out);
    case 0x84: return op_concatres(c, s, out);
    case 0x87: return op_sizeof(c, s, out);
    case 0x88: return op_index(c, s, out);
    case 0x89: return op_match(c, s, out);
    case 0x8E: return op_objtype(c, s, out);
    case 0x90:
    case 0x91: {
        uint64_t a, b;
        r = eval_int(c, s, &a);
        if (!r) r = eval_int(c, s, &b);
        if (!r) *out = aml_int(bool_v(c, op == 0x90 ? (a && b) : (a || b)));
        return r;
    }
    case 0x92:
        r = eval_int(c, s, &v);
        if (!r) *out = aml_int(bool_v(c, !v));
        return r;
    case 0x93:
    case 0x94:
    case 0x95: {
        aml_val_t a, b;
        int cmp = 0;
        val_zero(&b);
        r = eval_term(c, s, &a);
        if (!r) r = eval_term(c, s, &b);
        if (!r) r = compare(c, &a, &b, &cmp);
        aml_val_free(c, &a);
        aml_val_free(c, &b);
        if (!r) *out = aml_int(bool_v(c, op == 0x93 ? cmp == 0 : op == 0x94 ? cmp > 0 : cmp < 0));
        return r;
    }
    case 0x96: case 0x97: case 0x98: case 0x99:
        return op_convert(c, s, op, out);
    case 0x9C: return op_tostring(c, s, out);
    case 0x9E: return op_mid(c, s, out);
    case 0x5B: {
        uint8_t e;
        if (rd8(s, &e)) return AML_EPARSE;
        return eval_ext(c, s, e, out);
    }
    default:
        return fail(c, AML_EPARSE, "opcode");
    }
}

static int eval_term(aml_ctx_t *c, st_t *s, aml_val_t *out)
{
    val_zero(out);
    if (++c->nest > AML_MAX_NEST) {
        c->nest--;
        return fail(c, AML_ELIMIT, "nesting");
    }
    if (++c->ops > (c->op_limit ? c->op_limit : AML_MAX_OPS)) {
        c->nest--;
        return fail(c, AML_ELIMIT, "op budget");
    }
    int r = eval_inner(c, s, out);
    c->nest--;
    if (r) aml_val_free(c, out);
    return r;
}

/* ---- named object definitions ------------------------------------------------------- */
static int def_name(aml_ctx_t *c, st_t *s)
{
    nstr_t n;
    aml_val_t v;
    aml_node_t *nd;
    int r = parse_nstr(s, &n);
    if (r) return r;
    r = eval_term(c, s, &v);
    if (r) return r;
    if (!is_data(v.type)) {
        aml_val_free(c, &v);
        return AML_ETYPE;
    }
    r = create_named(c, s->scope, &n, v.type, &nd);
    if (r) {
        aml_val_free(c, &v);
        return r;
    }
    nd->v = v;
    return AML_OK;
}

static int def_scoped(aml_ctx_t *c, st_t *s, uint32_t op)
{
    uint32_t end, save = s->end;
    nstr_t n;
    aml_node_t *nd = 0;
    uint64_t a = 0, b = 0, d = 0;
    int r = rd_pkglen(s, &end);
    if (r) return r;
    s->end = end;
    r = parse_nstr(s, &n);
    if (!r && op == 0x5B83) {
        r = rdle(s, 1, &a);
        if (!r) r = rdle(s, 4, &b);
        if (!r) r = rdle(s, 1, &d);
    } else if (!r && op == 0x5B84) {
        r = rdle(s, 1, &a);
        if (!r) r = rdle(s, 2, &b);
    }
    if (!r) {
        if (op == 0x10) {
            nd = aml_ns_resolve(c, resolve_n(c, s->scope, &n, 0));
            if (!nd) {
                char p[64];
                nstr_text(&n, p, sizeof p);
                aml_logf(c, "Scope target not found:", p, 0, -1);
                r = AML_ENOENT;
            } else if (is_data(nd->v.type) || nd->v.type == AML_T_FIELD ||
                       nd->v.type == AML_T_BUFFIELD) {
                r = AML_ETYPE;
            }
        } else {
            uint8_t ty = op == 0x5B82   ? AML_T_DEVICE
                         : op == 0x5B83 ? AML_T_PROC
                         : op == 0x5B84 ? AML_T_POWER
                                        : AML_T_THERMAL;
            r = create_named(c, s->scope, &n, ty, &nd);
            if (!r && op == 0x5B83) {
                nd->x.cpu.id = (uint32_t) a;
                nd->x.cpu.pblk = (uint32_t) b;
                nd->x.cpu.plen = (uint32_t) d;
            }
            if (!r && op == 0x5B84) {
                nd->x.pr.level = (uint32_t) a;
                nd->x.pr.order = (uint32_t) b;
            }
        }
    }
    if (r) {
        s->end = save;
        return r;
    }
    aml_node_t *old = s->scope;
    s->scope = nd;
    r = exec_termlist(c, s, end);
    s->scope = old;
    s->end = save;
    if (r > 0 && r != FLOW_RETURN) r = AML_EPARSE;
    if (!r) s->pc = end;
    return r;
}

static int def_method(aml_ctx_t *c, st_t *s)
{
    uint32_t end;
    nstr_t n;
    uint8_t flags;
    aml_node_t *nd;
    int r = rd_pkglen(s, &end);
    if (!r) {
        uint32_t save = s->end;
        s->end = end;
        r = parse_nstr(s, &n);
        if (!r) r = rd8(s, &flags);
        s->end = save;
    }
    if (r) return r;
    const uint8_t *code = s->a + s->pc;
    uint32_t len = end - s->pc;
    s->pc = end;
    r = create_named(c, s->scope, &n, AML_T_METHOD, &nd);
    if (r) return r;
    nd->x.m.code = code;
    nd->x.m.len = len;
    nd->x.m.flags = flags;
    return AML_OK;
}

static int def_region(aml_ctx_t *c, st_t *s)
{
    nstr_t n;
    uint8_t space;
    aml_node_t *nd;
    int r = parse_nstr(s, &n);
    if (!r) r = rd8(s, &space);
    if (r) return r;
    uint32_t at = s->pc;
    uint64_t base = 0, len = 0;
    int defer = at_table_level(c);
    if (defer) {
        r = skip_term(c, s);
        if (!r) r = skip_term(c, s);
    } else {
        r = eval_int(c, s, &base);
        if (!r) r = eval_int(c, s, &len);
    }
    if (r) return r;
    r = create_named(c, s->scope, &n, AML_T_REGION, &nd);
    if (r) return r;
    nd->x.r.space = space;
    if (defer) {
        nd->x.r.defer = s->a + at;
        nd->x.r.defer_len = s->pc - at;
        nd->x.r.ready = 0;
    } else {
        nd->x.r.base = base;
        nd->x.r.len = len;
        nd->x.r.ready = 1;
    }
    /* the scope the address terms are evaluated in */
    nd->x.r.dscope = s->scope;
    c->stats.regions++;
    return AML_OK;
}

int aml_region_eval_deferred(aml_ctx_t *c, aml_node_t *rg)
{
    st_t s;
    uint64_t base, len;
    s.a = rg->x.r.defer;
    s.pc = 0;
    s.end = rg->x.r.defer_len;
    s.scope = rg->x.r.dscope ? rg->x.r.dscope : rg->parent;
    int r = eval_int(c, &s, &base);
    if (!r) r = eval_int(c, &s, &len);
    if (r) return r;
    rg->x.r.base = base;
    rg->x.r.len = len;
    rg->x.r.ready = 1;
    return AML_OK;
}

static int def_data_region(aml_ctx_t *c, st_t *s)
{
    nstr_t n;
    aml_val_t sig, oem, tid;
    aml_node_t *nd;
    int r = parse_nstr(s, &n);
    val_zero(&oem);
    val_zero(&tid);
    if (r) return r;
    r = eval_term(c, s, &sig);
    if (!r) r = eval_term(c, s, &oem);
    if (!r) r = eval_term(c, s, &tid);
    int found = -1;
    if (!r && sig.type == AML_T_STR && sig.len == 4) {
        for (uint32_t i = 0; i < c->n_tables && found < 0; i++) {
            int ok = 1;
            for (int k = 0; k < 4; k++)
                if (c->tables[i].sig[k] != (char) sig.u.bytes[k]) ok = 0;
            for (uint32_t k = 0; ok && oem.type == AML_T_STR && k < oem.len && k < 6; k++)
                if (c->tables[i].oem_id[k] != (char) oem.u.bytes[k]) ok = 0;
            for (uint32_t k = 0; ok && tid.type == AML_T_STR && k < tid.len && k < 8; k++)
                if (c->tables[i].oem_tid[k] != (char) tid.u.bytes[k]) ok = 0;
            if (ok) found = (int) i;
        }
    }
    aml_val_free(c, &sig);
    aml_val_free(c, &oem);
    aml_val_free(c, &tid);
    if (r) return r;
    if (found < 0) return AML_ENOENT;
    r = create_named(c, s->scope, &n, AML_T_REGION, &nd);
    if (r) return r;
    nd->x.r.space = AML_SPACE_TABLE;
    nd->x.r.map = (volatile uint8_t *) c->tables[found].aml;
    nd->x.r.len = c->tables[found].len;
    nd->x.r.ready = 1;
    c->stats.regions++;
    return AML_OK;
}

static int field_list(aml_ctx_t *c, st_t *s, uint32_t end, aml_node_t *proto)
{
    uint32_t bitoff = 0;
    uint8_t flags = proto->x.f.flags, attrib = 0, acc_len = 0;
    int r = AML_OK;
    while (!r && s->pc < end) {
        uint8_t b = s->a[s->pc];
        uint32_t len;
        uint8_t t, at;
        if (b == 0x00) {
            s->pc++;
            r = rd_pkglen_raw(s, &len);
            if (!r && len > 0x7FFFFFFFu - bitoff) r = AML_EPARSE;
            if (!r) bitoff += len;
        } else if (b == 0x01 || b == 0x03) {
            s->pc++;
            r = rd8(s, &t);
            if (!r) r = rd8(s, &at);
            if (!r && b == 0x03) r = rd8(s, &acc_len);
            if (!r && (t & 0x0F) > AML_ACC_BUF) r = AML_EPARSE;
            if (!r) {
                flags = (uint8_t) ((flags & 0xF0) | (t & 0x0F));
                attrib = at;
            }
        } else if (b == 0x02) {
            s->pc++;
            if (s->pc < end && s->a[s->pc] == 0x11) {
                uint32_t e2;
                s->pc++;
                r = rd_pkglen(s, &e2);
                if (!r) s->pc = e2;
            } else {
                nstr_t n;
                r = parse_nstr(s, &n);
            }
        } else if (lead_ok(b)) {
            if (end - s->pc < 4 || !seg_ok(s->a + s->pc)) return AML_EPARSE;
            uint32_t name = seg_at(s->a + s->pc);
            s->pc += 4;
            r = rd_pkglen_raw(s, &len);
            if (r) break;
            if (len > 0x7FFFFFFFu - bitoff || len > AML_MAX_OBJ * 8u) return AML_EPARSE;
            aml_node_t *scope = s->scope ? s->scope : c->root;
            if (aml_ns_child(scope, name)) {
                c->stats.errors++;
                aml_logf(c, "duplicate field name skipped", 0, 0, -1);
                if (!at_table_level(c)) return AML_EEXIST;
            } else {
                aml_node_t *f = node_new(c, scope, name, AML_T_FIELD);
                if (!f) return AML_ENOMEM;
                f->x.f = proto->x.f;
                f->x.f.flags = flags;
                f->x.f.attrib = attrib;
                f->x.f.acc_len = acc_len;
                f->x.f.bitoff = bitoff;
                f->x.f.bitlen = len;
                c->stats.fields++;
            }
            bitoff += len;
        } else {
            r = AML_EPARSE;
        }
    }
    return r;
}

static int def_field(aml_ctx_t *c, st_t *s, uint8_t e)
{
    uint32_t end, save = s->end;
    nstr_t n1, n2;
    aml_node_t proto;
    uint8_t flags;
    uint64_t bank = 0;
    int r = rd_pkglen(s, &end);
    if (r) return r;
    aml_bzero(&proto, (uint32_t) sizeof proto);
    s->end = end;
    r = parse_nstr(s, &n1);
    if (!r && e != 0x81) r = parse_nstr(s, &n2);
    if (!r && e == 0x87) r = eval_int(c, s, &bank);
    if (!r) r = rd8(s, &flags);
    if (!r) {
        aml_node_t *a = aml_ns_resolve(c, resolve_n(c, s->scope, &n1, 0));
        aml_node_t *b = e != 0x81 ? aml_ns_resolve(c, resolve_n(c, s->scope, &n2, 0)) : 0;
        if (!a || (e != 0x81 && !b)) {
            r = AML_ENOENT;
        } else if (e == 0x86 ? (a->v.type != AML_T_FIELD || b->v.type != AML_T_FIELD)
                             : (a->v.type != AML_T_REGION ||
                                (e == 0x87 && b->v.type != AML_T_FIELD))) {
            r = AML_ETYPE;
        } else if ((flags & 0x0F) > AML_ACC_BUF) {
            r = AML_EPARSE;
        } else {
            proto.x.f.kind = e == 0x81   ? AML_FIELD_NORMAL
                             : e == 0x86 ? AML_FIELD_INDEX
                                         : AML_FIELD_BANK;
            proto.x.f.flags = flags;
            proto.x.f.region = a;
            proto.x.f.data = b;
            proto.x.f.bank_val = bank;
            r = field_list(c, s, end, &proto);
        }
    }
    s->end = save;
    if (!r) s->pc = end;
    return r;
}

static int def_create_field(aml_ctx_t *c, st_t *s, uint32_t op)
{
    aml_loc_t loc;
    aml_val_t owned;
    uint64_t idx, nbits = 0;
    nstr_t n;
    aml_node_t *nd;
    int r = parse_source(c, s, &loc, &owned);
    if (r) return r;
    r = eval_int(c, s, &idx);
    if (!r && op == 0x5B13) r = eval_int(c, s, &nbits);
    if (!r) r = parse_nstr(s, &n);
    if (r) {
        aml_val_free(c, &owned);
        return r;
    }
    uint64_t bitoff, bitlen;
    switch (op) {
    case 0x8D: bitoff = idx, bitlen = 1; break;
    case 0x8C: bitoff = idx * 8, bitlen = 8; break;
    case 0x8B: bitoff = idx * 8, bitlen = 16; break;
    case 0x8A: bitoff = idx * 8, bitlen = 32; break;
    case 0x8F: bitoff = idx * 8, bitlen = 64; break;
    default: bitoff = idx, bitlen = nbits; break;
    }
    aml_node_t *bn;
    aml_val_t *b = loc.kind == AML_LOC_OWNED ? &owned : loc_slot(c, &loc, &bn);
    if (!b || b->type != AML_T_BUF)
        r = AML_ETYPE;
    else if (!bitlen || idx > 0xFFFFFFFFull || bitoff + bitlen > (uint64_t) b->len * 8)
        r = AML_EBOUNDS;
    if (!r) r = create_named(c, s->scope, &n, AML_T_BUFFIELD, &nd);
    if (r) {
        aml_val_free(c, &owned);
        return r;
    }
    nd->x.bf.target = loc;
    nd->x.bf.owned = owned;
    nd->x.bf.bitoff = (uint32_t) bitoff;
    nd->x.bf.bitlen = (uint32_t) bitlen;
    return AML_OK;
}

/* ---- statements ------------------------------------------------------------------- */
static int exec_if(aml_ctx_t *c, st_t *s)
{
    uint32_t end, save = s->end;
    uint64_t pred;
    int r = rd_pkglen(s, &end);
    if (r) return r;
    s->end = end;
    r = eval_int(c, s, &pred);
    s->end = save;
    if (r) return r;
    if (pred) {
        r = exec_termlist(c, s, end);
        if (r) return r;
        s->pc = end;
        if (s->pc < s->end && s->a[s->pc] == 0xA1) {
            uint32_t e2;
            s->pc++;
            r = rd_pkglen(s, &e2);
            if (r) return r;
            s->pc = e2;
        }
        return AML_OK;
    }
    s->pc = end;
    if (s->pc < s->end && s->a[s->pc] == 0xA1) {
        uint32_t e2;
        s->pc++;
        r = rd_pkglen(s, &e2);
        if (r) return r;
        r = exec_termlist(c, s, e2);
        if (r) return r;
        s->pc = e2;
    }
    return AML_OK;
}

static int exec_while(aml_ctx_t *c, st_t *s)
{
    uint32_t end, save = s->end, cond;
    int r = rd_pkglen(s, &end);
    if (r) return r;
    cond = s->pc;
    for (uint32_t it = 0;; it++) {
        uint64_t pred;
        if (it >= AML_MAX_LOOP) return fail(c, AML_ELIMIT, "While iterations");
        s->pc = cond;
        s->end = end;
        r = eval_int(c, s, &pred);
        s->end = save;
        if (r) return r;
        if (!pred) break;
        r = exec_termlist(c, s, end);
        if (r == FLOW_BREAK) break;
        if (r == FLOW_CONTINUE) continue;
        if (r) return r;
    }
    s->pc = end;
    return AML_OK;
}

static int exec_term(aml_ctx_t *c, st_t *s)
{
    uint8_t op = s->a[s->pc];
    aml_val_t v;
    int r;
    if (op == 0x5B && s->pc + 1 < s->end) {
        uint8_t e = s->a[s->pc + 1];
        switch (e) {
        case 0x01:
        case 0x02: {
            nstr_t n;
            aml_node_t *nd;
            uint8_t fl = 0;
            s->pc += 2;
            r = parse_nstr(s, &n);
            if (!r && e == 0x01) r = rd8(s, &fl);
            if (!r) r = create_named(c, s->scope, &n, e == 0x01 ? AML_T_MUTEX : AML_T_EVENT, &nd);
            if (!r && e == 0x01) nd->x.mx.sync = fl & 0x0F;
            return r;
        }
        case 0x13:
            s->pc += 2;
            return def_create_field(c, s, 0x5B13);
        case 0x21:
        case 0x22: {
            uint64_t t;
            s->pc += 2;
            r = eval_int(c, s, &t);
            if (r) return r;
            return aml_delay(c, e == 0x22 ? t * 1000 : t);
        }
        case 0x24:
        case 0x26:
            s->pc += 2;
            return op_event(c, s, e, &v);
        case 0x27:
            s->pc += 2;
            return op_mutex(c, s, 0, &v);
        case 0x2A: {
            tgt_t t;
            s->pc += 2;
            r = parse_super(c, s, &t, 0);
            aml_val_free(c, &t.ref);
            if (!r) aml_logf(c, "Unload ignored", 0, 0, -1);
            return r;
        }
        case 0x32: {
            uint64_t ty, code, arg;
            s->pc += 2;
            r = rdle(s, 1, &ty);
            if (!r) r = rdle(s, 4, &code);
            if (!r) r = eval_int(c, s, &arg);
            if (r) return r;
            aml_logf(c, "Fatal opcode, code", 0, code, 1);
            return fail(c, AML_EFATAL, "Fatal");
        }
        case 0x80:
            s->pc += 2;
            return def_region(c, s);
        case 0x81:
        case 0x86:
        case 0x87:
            s->pc += 2;
            return def_field(c, s, e);
        case 0x82:
        case 0x83:
        case 0x84:
        case 0x85:
            s->pc += 2;
            return def_scoped(c, s, 0x5B00u | e);
        case 0x88:
            s->pc += 2;
            return def_data_region(c, s);
        default:
            break;
        }
    }
    switch (op) {
    case 0x06: { /* Alias */
        nstr_t a, b;
        aml_node_t *src, *nd;
        s->pc++;
        r = parse_nstr(s, &a);
        if (!r) r = parse_nstr(s, &b);
        if (r) return r;
        src = aml_ns_resolve(c, resolve_n(c, s->scope, &a, 0));
        if (!src) return AML_ENOENT;
        r = create_named(c, s->scope, &b, AML_T_ALIAS, &nd);
        if (!r) nd->x.alias.target = src;
        return r;
    }
    case 0x08: s->pc++; return def_name(c, s);
    case 0x10: s->pc++; return def_scoped(c, s, 0x10);
    case 0x14: s->pc++; return def_method(c, s);
    case 0x15: { /* External */
        nstr_t n;
        uint64_t ty, argc;
        s->pc++;
        const uint8_t *at = s->a + s->pc;
        r = parse_nstr(s, &n);
        if (!r) r = rdle(s, 1, &ty);
        if (!r) r = rdle(s, 1, &argc);
        if (!r && ty == AML_T_METHOD) ext_add(c, s->scope, at, (uint8_t) argc);
        return r;
    }
    case 0x86: s->pc++; return op_notify(c, s);
    case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8F:
        s->pc++;
        return def_create_field(c, s, op);
    case 0xA0: s->pc++; return exec_if(c, s);
    case 0xA1: { /* a stray Else: skip it */
        uint32_t end;
        s->pc++;
        r = rd_pkglen(s, &end);
        if (!r) s->pc = end;
        return r;
    }
    case 0xA2: s->pc++; return exec_while(c, s);
    case 0xA3:
    case 0xCC: s->pc++; return AML_OK;
    case 0xA4:
        s->pc++;
        r = eval_term(c, s, &v); /* not into c->ret: a call inside uses it */
        if (r) return r;
        aml_val_free(c, &c->ret);
        c->ret = v;
        if (c->ret.type == AML_T_REF &&
            (c->ret.u.ref->base.kind == AML_LOC_LOCAL || c->ret.u.ref->base.kind == AML_LOC_ARG) &&
            c->ret.u.ref->base.depth + 1u == c->depth) {
            /* a reference to this frame's Local would dangle: return the value */
            aml_val_t d;
            r = ref_read(c, c->ret.u.ref, &d, 0);
            aml_val_free(c, &c->ret);
            if (r) return r;
            c->ret = d;
        }
        return FLOW_RETURN;
    case 0xA5: s->pc++; return FLOW_BREAK;
    case 0x9F: s->pc++; return FLOW_CONTINUE;
    default: break;
    }
    r = eval_term(c, s, &v);
    aml_val_free(c, &v);
    return r;
}

static int exec_termlist(aml_ctx_t *c, st_t *s, uint32_t end)
{
    uint32_t save = s->end;
    s->end = end;
    while (s->pc < end) {
        uint32_t start = s->pc;
        int r = exec_term(c, s);
        if (!r) continue;
        if (r < 0 && at_table_level(c) && r != AML_ELIMIT && r != AML_ENOMEM) {
            /* table level: report, step over the term, keep loading */
            st_t k = *s;
            c->stats.errors++;
            if (!c->load_err) c->load_err = r;
            aml_logf(c, "load error, term skipped at offset", aml_strerror(r), start, 1);
            k.pc = start;
            if (!skip_term(c, &k) && k.pc > start) {
                s->pc = k.pc;
                continue;
            }
            /* not even skippable (an opcode that does not exist): step over
             * the opcode byte(s) and resynchronise, as ACPICA does; bounded
             * because every step moves forward and errors are capped */
            if (++c->load_skips <= AML_MAX_LOAD_SKIPS) {
                s->pc = start + (s->a[start] == 0x5B && start + 1 < end ? 2u : 1u);
                continue;
            }
        }
        s->end = save;
        return r;
    }
    s->end = save;
    return AML_OK;
}

/* ---- declaration pass ------------------------------------------------------------------
 * Declares Scope/Device/Processor/PowerResource/ThermalZone containers and
 * Methods so table-level code can call a method defined later in the table.
 * Anything it cannot step over ends the pass for that list; the execution
 * pass then does all the real work anyway. */
static void decl_pass(aml_ctx_t *c, st_t *s, uint32_t end, uint32_t depth)
{
    uint32_t save = s->end;
    if (depth > 32) return;
    s->end = end;
    while (s->pc < end) {
        uint8_t op = s->a[s->pc];
        uint32_t e2, at = s->pc;
        nstr_t n;
        if (op == 0x10 || op == 0x14 ||
            (op == 0x5B && s->pc + 1 < end && s->a[s->pc + 1] >= 0x82 && s->a[s->pc + 1] <= 0x85)) {
            uint32_t ext = op == 0x5B ? s->a[s->pc + 1] : 0;
            s->pc += op == 0x5B ? 2 : 1;
            if (rd_pkglen(s, &e2)) break;
            s->end = e2;
            if (parse_nstr(s, &n)) break;
            s->end = end;
            if (op == 0x14) {
                if (s->pc >= e2) break;
                uint8_t fl = s->a[s->pc++];
                aml_node_t *nd;
                if (!create_named(c, s->scope, &n, AML_T_METHOD, &nd)) {
                    nd->temp = NODE_PLACEHOLDER;
                    nd->x.m.code = s->a + s->pc;
                    nd->x.m.len = e2 - s->pc;
                    nd->x.m.flags = fl;
                }
            } else {
                aml_node_t *nd = 0;
                if (op == 0x10) {
                    nd = aml_ns_resolve(c, resolve_n(c, s->scope, &n, 0));
                } else {
                    uint8_t ty = ext == 0x82   ? AML_T_DEVICE
                                 : ext == 0x83 ? AML_T_PROC
                                 : ext == 0x84 ? AML_T_POWER
                                               : AML_T_THERMAL;
                    uint32_t extra = ext == 0x83 ? 6 : ext == 0x84 ? 3 : 0;
                    if (extra > e2 - s->pc) break;
                    s->pc += extra;
                    if (!create_named(c, s->scope, &n, ty, &nd)) nd->temp = NODE_PLACEHOLDER;
                    else nd = 0;
                }
                if (nd) {
                    aml_node_t *old = s->scope;
                    s->scope = nd;
                    decl_pass(c, s, e2, depth + 1);
                    s->scope = old;
                }
            }
            s->pc = e2;
            continue;
        }
        if (op == 0x15) {
            s->pc++;
            const uint8_t *nm = s->a + s->pc;
            uint64_t ty, argc;
            if (parse_nstr(s, &n) || rdle(s, 1, &ty) || rdle(s, 1, &argc)) break;
            if (ty == AML_T_METHOD) ext_add(c, s->scope, nm, (uint8_t) argc);
            continue;
        }
        s->pc = at;
        if (skip_term(c, s) || s->pc <= at) break;
    }
    s->end = save;
}

/* ---- tables ------------------------------------------------------------------------ */
static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

void aml_budget_reset(aml_ctx_t *c)
{
    c->ops = 0;
    c->sleep_us = 0;
    c->nest = 0;
}

int aml_load_table(aml_ctx_t *c, const void *table, uint32_t len)
{
    const uint8_t *t = (const uint8_t *) table;
    if (!c || !c->root || !t || len < 36) return AML_EARG;
    uint32_t tl = rd32le(t + 4);
    if (tl < 36 || tl > len) return fail(c, AML_EPARSE, "table length");
    if (c->n_tables >= AML_MAX_TABLES) return AML_ENOMEM;
    if (c->depth >= AML_MAX_CALL_DEPTH) return AML_ELIMIT;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < tl; i++) sum = (uint8_t) (sum + t[i]);
    if (sum) aml_logf(c, "table checksum mismatch (loaded anyway)", 0, sum, 1);
    int dsdt = t[0] == 'D' && t[1] == 'S' && t[2] == 'D' && t[3] == 'T';
    if (dsdt || !c->stats.tables) {
        c->dsdt_rev = t[8];
        c->int64 = t[8] >= 2;
    }
    uint32_t ti = c->n_tables++;
    c->tables[ti].aml = t;
    c->tables[ti].len = tl;
    for (int i = 0; i < 4; i++) c->tables[ti].sig[i] = (char) t[i];
    for (int i = 0; i < 6; i++) c->tables[ti].oem_id[i] = (char) t[10 + i];
    for (int i = 0; i < 8; i++) c->tables[ti].oem_tid[i] = (char) t[16 + i];
    c->stats.tables++;

    uint8_t save_loading = c->loading;
    if (!c->top) {
        aml_budget_reset(c);
        c->last_error = 0;
    }
    c->top++;
    frame_push(c, 0, c->root);
    st_t s;
    s.a = t;
    s.pc = 36;
    s.end = tl;
    s.scope = c->root;
    c->loading = 1;
    uint32_t save_skips = c->load_skips;
    int save_err = c->load_err;
    c->load_skips = 0;
    c->load_err = 0;
    decl_pass(c, &s, tl, 0);
    s.pc = 36;
    s.scope = c->root;
    int r = exec_termlist(c, &s, tl);
    if (r == FLOW_RETURN) {
        aml_val_free(c, &c->ret);
        r = AML_OK;
    } else if (r > 0) {
        r = AML_EPARSE;
    }
    frame_pop(c);
    c->loading = save_loading;
    c->load_skips = save_skips;
    int skipped = c->load_err;
    c->load_err = save_err;
    c->top--;
    if (r) {
        c->stats.errors++;
        return fail(c, r, "table");
    }
    /* terms were skipped (each counted): the rest loaded, but report it */
    if (skipped) return fail(c, skipped, "table");
    return AML_OK;
}

static int op_load(aml_ctx_t *c, st_t *s, aml_val_t *out)
{
    nstr_t n;
    tgt_t t;
    aml_val_t data;
    int r = parse_nstr(s, &n);
    if (r) return r;
    aml_node_t *src = aml_ns_resolve(c, resolve_n(c, s->scope, &n, 0));
    if (!src) return AML_ENOENT;
    val_zero(&data);
    if (src->v.type == AML_T_REGION) {
        uint64_t hdr = 0;
        r = aml_region_prepare(c, src);
        if (!r && src->x.r.len < 36) r = AML_EBOUNDS;
        if (!r) r = aml_region_access(c, src, 4, 4, 0, &hdr);
        if (!r && (hdr < 36 || hdr > src->x.r.len || hdr > AML_MAX_OBJ)) r = AML_EBOUNDS;
        if (!r) r = aml_mkbuf(c, &data, (uint32_t) hdr);
        for (uint32_t i = 0; !r && i < hdr; i++) {
            uint64_t b;
            r = aml_region_access(c, src, i, 1, 0, &b);
            data.u.bytes[i] = (uint8_t) b;
        }
    } else {
        r = read_node(c, src, &data);
        if (!r && data.type != AML_T_BUF) r = AML_ETYPE;
    }
    if (r) {
        aml_val_free(c, &data);
        return r;
    }
    /* a table that is already loaded is refused, as ACPICA does */
    for (uint32_t i = 0; i < c->n_tables && !r; i++) {
        if (c->tables[i].len != data.len) continue;
        uint32_t k = 0;
        while (k < data.len && c->tables[i].aml[k] == data.u.bytes[k]) k++;
        if (k == data.len) r = AML_EEXIST;
    }
    uint8_t *keep = 0;
    uint32_t klen = data.len;
    if (r) {
        aml_val_free(c, &data);
    } else {
        /* the table must outlive this call: keep the bytes (never freed) */
        keep = data.u.bytes;
        val_zero(&data);
        r = aml_load_table(c, keep, klen);
    }
    aml_logf(c, "Load: dynamic table", aml_strerror(r), klen, 0);
    aml_val_t h = aml_int(r ? 0 : c->n_tables);
    int e = parse_super(c, s, &t, SUP_NULL_OK);
    if (!e) e = store_tgt(c, &t, &h, 0);
    aml_val_free(c, &t.ref);
    if (e) return e;
    *out = aml_int(r ? 0 : aml_ones(c));
    return AML_OK;
}

/* ---- predefined objects ---------------------------------------------------------- */
static const char *const osi_yes[] = {
    "Windows 2000",      "Windows 2001",      "Windows 2001 SP1",
    "Windows 2001.1",    "Windows 2001 SP2",  "Windows 2001.1 SP1",
    "Windows 2006",      "Windows 2006.1",    "Windows 2006 SP1",
    "Windows 2006 SP2",  "Windows 2009",      "Windows 2012",
    "Windows 2013",      "Windows 2015",      "Windows 2016",
    "Windows 2017",      "Windows 2017.2",    "Windows 2018",
    "Windows 2018.2",    "Windows 2019",      "Windows 2020",
    "Windows 2021",      "Windows 2022",      "Module Device",
    "Processor Device",  "3.0 Thermal Model", "3.0 _SCP Extensions",
    "Processor Aggregator Device", "Extended Address Space Descriptor",
};

static int native_osi(aml_ctx_t *c, aml_node_t *m, aml_val_t *args, uint32_t argc, aml_val_t *ret)
{
    (void) m;
    *ret = aml_int(0);
    if (argc < 1 || args[0].type != AML_T_STR) return AML_ETYPE;
    for (uint32_t i = 0; i < sizeof osi_yes / sizeof osi_yes[0]; i++) {
        const char *w = osi_yes[i];
        uint32_t k = 0;
        while (w[k] && k < args[0].len && w[k] == (char) args[0].u.bytes[k]) k++;
        if (!w[k] && k == args[0].len) {
            *ret = aml_int(aml_ones(c));
            return AML_OK;
        }
    }
    return AML_OK;
}

int aml_init(aml_ctx_t *c, const aml_plat_t *plat, void *arena, uint32_t arena_size)
{
    if (!c || !arena || arena_size < 0x10000) return AML_EARG;
    aml_bzero(c, (uint32_t) sizeof *c);
    c->plat = plat;
    uintptr_t a = (uintptr_t) arena, al = (a + 15) & ~(uintptr_t) 15;
    c->arena = (uint8_t *) al;
    c->arena_size = arena_size - (uint32_t) (al - a);
    c->int64 = 1;
    c->root = node_new(c, 0, seg_at((const uint8_t *) "\\___"), AML_T_SCOPE);
    if (!c->root) return AML_ENOMEM;
    static const char *const scopes[] = {"_GPE", "_PR_", "_SI_", "_TZ_"};
    for (int i = 0; i < 4; i++)
        if (!node_new(c, c->root, seg_at((const uint8_t *) scopes[i]), AML_T_SCOPE))
            return AML_ENOMEM;
    if (!node_new(c, c->root, seg_at((const uint8_t *) "_SB_"), AML_T_DEVICE)) return AML_ENOMEM;
    aml_node_t *gl = node_new(c, c->root, seg_at((const uint8_t *) "_GL_"), AML_T_MUTEX);
    aml_node_t *osi = node_new(c, c->root, seg_at((const uint8_t *) "_OSI"), AML_T_METHOD);
    aml_node_t *os = node_new(c, c->root, seg_at((const uint8_t *) "_OS_"), AML_T_UNINIT);
    aml_node_t *rev = node_new(c, c->root, seg_at((const uint8_t *) "_REV"), AML_T_INT);
    if (!gl || !osi || !os || !rev) return AML_ENOMEM;
    osi->x.m.flags = 1;
    osi->x.m.native = native_osi;
    os->v = aml_str(c, "Microsoft Windows NT");
    if (os->v.type != AML_T_STR) return AML_ENOMEM;
    rev->v = aml_int(2);
    /* predefined objects are not counted as table objects */
    c->stats.objects = 0;
    c->stats.methods = 0;
    c->stats.devices = 0;
    return AML_OK;
}

/* ---- public evaluation ------------------------------------------------------------- */
int aml_eval_node(aml_ctx_t *c, aml_node_t *n, const aml_val_t *args, uint32_t argc,
                  aml_val_t *ret)
{
    aml_val_t a[7];
    int r = AML_OK;
    val_zero(ret);
    n = aml_ns_resolve(c, n);
    if (!n) return AML_ENOENT;
    if (argc > 7) return AML_EARG;
    for (uint32_t i = 0; i < 7; i++) val_zero(&a[i]);
    for (uint32_t i = 0; i < argc && !r; i++) r = aml_val_copy(c, &a[i], &args[i]);
    if (r) {
        for (uint32_t i = 0; i < argc; i++) aml_val_free(c, &a[i]);
        return r;
    }
    if (!c->top) {
        aml_budget_reset(c);
        c->last_error = 0;
        c->last_error_where = 0;
    }
    c->top++;
    if (n->v.type == AML_T_METHOD) {
        r = aml_call(c, n, a, argc, ret);
    } else {
        for (uint32_t i = 0; i < argc; i++) aml_val_free(c, &a[i]);
        r = read_node(c, n, ret);
    }
    c->top--;
    return r;
}

int aml_eval(aml_ctx_t *c, aml_node_t *scope, const char *path, const aml_val_t *args,
             uint32_t argc, aml_val_t *result)
{
    aml_val_t ret;
    if (result) val_zero(result);
    if (!c || !c->root || !path) return AML_EARG;
    aml_node_t *n = aml_lookup(c, scope, path);
    if (!n) return AML_ENOENT;
    int r = aml_eval_node(c, n, args, argc, &ret);
    if (result && !r)
        *result = ret;
    else
        aml_val_free(c, &ret);
    return r;
}

int aml_eval_int(aml_ctx_t *c, aml_node_t *scope, const char *path, uint64_t *out)
{
    aml_val_t v;
    int r = aml_eval(c, scope, path, 0, 0, &v);
    if (r) return r;
    r = aml_to_int(c, &v, out);
    aml_val_free(c, &v);
    return r;
}

int aml_notify_pop(aml_ctx_t *c, aml_notify_t *out)
{
    while (c && c->notify_count) {
        aml_notify_t e = c->notify[c->notify_head];
        c->notify_head = (c->notify_head + 1) % AML_MAX_NOTIFY;
        c->notify_count--;
        if (e.node && e.node->temp != NODE_DEAD) {
            *out = e;
            return 1;
        }
    }
    return 0;
}

aml_stats_t aml_stats(aml_ctx_t *c)
{
    aml_stats_t s = c->stats;
    s.heap_used = c->heap_used;
    s.heap_peak = c->heap_peak;
    s.heap_size = c->arena_size;
    return s;
}

const char *aml_strerror(int err)
{
    switch (err) {
    case AML_OK: return "ok";
    case AML_EPARSE: return "malformed AML";
    case AML_ENOENT: return "name not found";
    case AML_ETYPE: return "wrong operand type";
    case AML_ENOMEM: return "out of memory";
    case AML_ELIMIT: return "budget exceeded";
    case AML_EBOUNDS: return "out of bounds";
    case AML_EDIV0: return "divide by zero";
    case AML_EFATAL: return "Fatal opcode";
    case AML_EUNSUP: return "unsupported";
    case AML_EEXIST: return "already exists";
    case AML_EIO: return "region access failed";
    case AML_EARG: return "bad argument";
    case AML_EMUTEX: return "mutex not held";
    default: return "error";
    }
}
