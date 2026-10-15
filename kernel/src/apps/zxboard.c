/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxboard.c — the Circuit board's model (zxboard.h). */
#include "zxboard.h"

static bool seq(const char *a, const char *b)
{
    uint32_t i = 0;
    for (; a[i] && a[i] == b[i]; i++) {
    }
    return a[i] == b[i];
}

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static bool scopy(char *d, uint32_t cap, const char *s)
{
    uint32_t n = slen(s);
    if (n + 1u > cap) return false;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    d[n] = 0;
    return true;
}

static void zero(void *p, uint32_t n)
{
    uint8_t *z = p;
    for (uint32_t i = 0; i < n; i++) z[i] = 0;
}

static bool ident(const char *w)
{
    if (!w || !w[0]) return false;
    for (uint32_t i = 0; w[i]; i++) {
        char c = w[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
                  (i && c >= '0' && c <= '9');
        if (!ok) return false;
    }
    return slen(w) + 1u <= ZXAPP_ID_MAX;
}

static int fail(zxboard_t *b, int rc, const char *m1, const char *m2)
{
    b->err[0] = 0;
    uint32_t k = 0;
    for (const char *p = m1; p && *p && k + 1u < ZXAPP_ERR_MAX; p++) b->err[k++] = *p;
    for (const char *p = m2; p && *p && k + 1u < ZXAPP_ERR_MAX; p++) b->err[k++] = *p;
    b->err[k] = 0;
    return rc;
}

int zxboard_init(zxboard_t *b, const zxapp_registry_t *reg, const char *id, const char *name)
{
    zero(b, sizeof *b);
    b->reg = reg;
    if (!reg || !ident(id)) return fail(b, ZXAPP_E_ARG, "an app id is a short identifier", 0);
    scopy(b->id, sizeof b->id, id);
    scopy(b->name, sizeof b->name, name ? name : id);
    scopy(b->entry, sizeof b->entry, "main");
    b->c.kind = ZXAPP_KIND_NUM;
    b->c.out_x = 720, b->c.out_y = 200, b->c.out_placed = true;
    return ZXAPP_OK;
}

/* ---- CB1: the shelf ----------------------------------------------------------- */

uint32_t zxboard_shelf_count(const zxboard_t *b)
{
    uint32_t n = 0;
    for (uint32_t i = 0; b->reg && i < b->reg->n; i++) n += b->reg->b[i]->nprov;
    return n;
}

bool zxboard_shelf(const zxboard_t *b, uint32_t i, zxboard_item_t *out)
{
    for (uint32_t k = 0; b->reg && k < b->reg->n; k++) {
        const zxapp_t *a = b->reg->b[k];
        if (i >= a->nprov) {
            i -= a->nprov;
            continue;
        }
        out->from = a;
        out->prov = &a->prov[i];
        uint32_t n = 0, il = slen(a->id), el = slen(a->prov[i].entry);
        if (il + 1u + el + 1u > ZXAPP_ENTRY_MAX) return false;
        for (uint32_t q = 0; q < il; q++) out->ref[n++] = a->id[q];
        out->ref[n++] = '.';
        for (uint32_t q = 0; q < el; q++) out->ref[n++] = a->prov[i].entry[q];
        out->ref[n] = 0;
        return true;
    }
    return false;
}

static int ver_cmp(const uint16_t x[3], const uint16_t y[3])
{
    for (uint32_t i = 0; i < 3u; i++)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

/* The newest registered block or app with this id. */
static const zxapp_t *newest(const zxapp_registry_t *r, const char *id, uint32_t idl)
{
    const zxapp_t *best = 0;
    for (uint32_t i = 0; r && i < r->n; i++) {
        const zxapp_t *a = r->b[i];
        if (slen(a->id) != idl) continue;
        bool same = true;
        for (uint32_t q = 0; q < idl && same; q++) same = a->id[q] == id[q];
        if (same && (!best || ver_cmp(a->ver, best->ver) > 0)) best = a;
    }
    return best;
}

const zxapp_provide_t *zxboard_contract(const zxboard_t *b, const char *ref, const zxapp_t **from)
{
    uint32_t dot = 0;
    while (ref[dot] && ref[dot] != '.') dot++;
    if (from) *from = 0;
    if (!ref[dot]) return 0;
    const zxapp_t *a = newest(b->reg, ref, dot);
    for (uint32_t i = 0; a && i < a->nprov; i++)
        if (seq(a->prov[i].entry, ref + dot + 1u)) {
            if (from) *from = a;
            return &a->prov[i];
        }
    return 0;
}

/* ---- CB2: editing ------------------------------------------------------------- */

static bool name_taken(const zxboard_t *b, const char *n, int32_t except_part)
{
    if (seq(n, "out")) return true;
    for (uint32_t i = 0; i < b->c.ninputs; i++)
        if (seq(b->c.input[i], n)) return true;
    for (uint32_t i = 0; i < b->c.nparts; i++)
        if ((int32_t) i != except_part && seq(b->c.part[i].name, n)) return true;
    return false;
}

/* Every source that took (kind, index): unwired when it was `gone`, the
 * index above it moved down one. */
static void renumber(zxboard_t *b, uint8_t kind, uint32_t gone)
{
    zxwire_src_t *s[ZXWIRE_PARTS * ZXAPP_INPUTS + 1u];
    uint32_t n = 0;
    for (uint32_t i = 0; i < b->c.nparts; i++)
        for (uint32_t j = 0; j < b->c.part[i].nports; j++) s[n++] = &b->c.part[i].src[j];
    s[n++] = &b->c.out;
    for (uint32_t i = 0; i < n; i++) {
        if (s[i]->kind != kind) continue;
        if (s[i]->index == gone)
            zero(s[i], sizeof *s[i]);
        else if (s[i]->index > gone)
            s[i]->index--;
    }
}

int zxboard_input_add(zxboard_t *b, const char *name)
{
    if (!ident(name)) return fail(b, ZXAPP_E_ARG, "an input's name is a short identifier", 0);
    if (name_taken(b, name, -1)) return fail(b, ZXAPP_E_NAME, "a name used twice: ", name);
    if (b->c.ninputs >= ZXAPP_INPUTS)
        return fail(b, ZXAPP_E_FULL, "an app has at most 8 inputs", 0);
    uint32_t i = b->c.ninputs++;
    scopy(b->c.input[i], ZXAPP_ID_MAX, name);
    b->c.in_x[i] = 40, b->c.in_y[i] = 80 + 96 * (int32_t) i, b->c.in_placed[i] = true;
    return (int) i;
}

int zxboard_input_remove(zxboard_t *b, uint32_t i)
{
    if (i >= b->c.ninputs) return fail(b, ZXAPP_E_ARG, "no such input", 0);
    renumber(b, ZXWIRE_SRC_INPUT, i);
    for (uint32_t k = i; k + 1u < b->c.ninputs; k++) {
        scopy(b->c.input[k], ZXAPP_ID_MAX, b->c.input[k + 1u]);
        b->c.in_x[k] = b->c.in_x[k + 1u], b->c.in_y[k] = b->c.in_y[k + 1u];
        b->c.in_placed[k] = b->c.in_placed[k + 1u];
    }
    b->c.ninputs--;
    return ZXAPP_OK;
}

int zxboard_answer_kind(zxboard_t *b, uint8_t kind)
{
    if (kind != ZXAPP_KIND_NUM && kind != ZXAPP_KIND_TRUTH)
        return fail(b, ZXAPP_E_ARG, "an app answers numbers or a truth", 0);
    b->c.kind = kind;
    return ZXAPP_OK;
}

int zxboard_part_add(zxboard_t *b, const char *ref, int32_t x, int32_t y)
{
    const zxapp_provide_t *C = ref ? zxboard_contract(b, ref, 0) : 0;
    if (!C) return fail(b, ZXAPP_E_NAME, "not on the shelf: ", ref);
    if (b->c.nparts >= ZXWIRE_PARTS)
        return fail(b, ZXAPP_E_FULL, "a board has at most 16 parts", 0);
    if (x < 0 || y < 0 || x > 100000 || y > 100000) return fail(b, ZXAPP_E_ARG, "off the board", 0);
    zxwire_part_t *P = &b->c.part[b->c.nparts];
    zero(P, sizeof *P);
    if (!scopy(P->entry, sizeof P->entry, ref)) return fail(b, ZXAPP_E_FULL, "a long name: ", ref);
    /* its name: the entry's last word, then 2, 3, ... until it is free */
    const char *last = ref;
    for (const char *q = ref; *q; q++)
        if (*q == '.') last = q + 1;
    char base[ZXAPP_ID_MAX - 4u];
    uint32_t bl = 0;
    while (last[bl] && bl + 1u < sizeof base) base[bl] = last[bl], bl++;
    base[bl] = 0;
    scopy(P->name, sizeof P->name, base);
    for (uint32_t k = 2; name_taken(b, P->name, -1) && k < 100u; k++) {
        scopy(P->name, sizeof P->name, base);
        uint32_t n = bl;
        if (k >= 10u) P->name[n++] = (char) ('0' + k / 10u);
        P->name[n++] = (char) ('0' + k % 10u);
        P->name[n] = 0;
    }
    for (uint32_t k = 0; k < C->ninputs; k++) scopy(P->port[k], ZXAPP_ID_MAX, C->input[k]);
    P->nports = C->ninputs;
    P->x = x, P->y = y, P->placed = true;
    return (int) b->c.nparts++;
}

int zxboard_part_remove(zxboard_t *b, uint32_t part)
{
    if (part >= b->c.nparts) return fail(b, ZXAPP_E_ARG, "no such part", 0);
    renumber(b, ZXWIRE_SRC_PART, part);
    for (uint32_t k = part; k + 1u < b->c.nparts; k++) b->c.part[k] = b->c.part[k + 1u];
    b->c.nparts--;
    return ZXAPP_OK;
}

int zxboard_part_rename(zxboard_t *b, uint32_t part, const char *name)
{
    if (part >= b->c.nparts) return fail(b, ZXAPP_E_ARG, "no such part", 0);
    if (!ident(name)) return fail(b, ZXAPP_E_ARG, "a part's name is a short identifier", 0);
    if (name_taken(b, name, (int32_t) part))
        return fail(b, ZXAPP_E_NAME, "a name used twice: ", name);
    scopy(b->c.part[part].name, ZXAPP_ID_MAX, name);
    return ZXAPP_OK;
}

static int src_ok(zxboard_t *b, const zxwire_src_t *s)
{
    if (s->kind == ZXWIRE_SRC_INPUT && s->index >= b->c.ninputs)
        return fail(b, ZXAPP_E_ARG, "no such input", 0);
    if (s->kind == ZXWIRE_SRC_PART && s->index >= b->c.nparts)
        return fail(b, ZXAPP_E_ARG, "no such part", 0);
    if (s->kind == ZXWIRE_SRC_CONST && (!s->nconst || s->nconst > ZXWIRE_CONST))
        return fail(b, ZXAPP_E_ARG, "a setting is 1 to 8 whole numbers", 0);
    if (s->kind > ZXWIRE_SRC_CONST) return fail(b, ZXAPP_E_ARG, "not a source", 0);
    return ZXAPP_OK;
}

int zxboard_wire(zxboard_t *b, uint32_t part, uint32_t port, const zxwire_src_t *src)
{
    if (part >= b->c.nparts || port >= b->c.part[part].nports)
        return fail(b, ZXAPP_E_ARG, "no such input on that part", 0);
    int rc = src_ok(b, src);
    if (rc != ZXAPP_OK) return rc;
    if (src->kind == ZXWIRE_SRC_PART && src->index == part)
        return fail(b, ZXAPP_E_ARG, "a part cannot take its own answer", 0);
    b->c.part[part].src[port] = *src;
    return ZXAPP_OK;
}

int zxboard_out(zxboard_t *b, const zxwire_src_t *src)
{
    int rc = src_ok(b, src);
    if (rc != ZXAPP_OK) return rc;
    b->c.out = *src;
    return ZXAPP_OK;
}

zxwire_src_t zxboard_src_input(uint32_t i)
{
    zxwire_src_t s;
    zero(&s, sizeof s);
    s.kind = ZXWIRE_SRC_INPUT;
    s.index = (uint8_t) i;
    return s;
}

zxwire_src_t zxboard_src_part(uint32_t i)
{
    zxwire_src_t s = zxboard_src_input(i);
    s.kind = ZXWIRE_SRC_PART;
    return s;
}

bool zxboard_src_setting(const char *t, zxwire_src_t *out)
{
    zero(out, sizeof *out);
    out->kind = ZXWIRE_SRC_CONST;
    uint32_t i = 0;
    for (;;) {
        while (t[i] == ' ' || t[i] == ',') i++;
        if (!t[i]) return out->nconst > 0;
        bool neg = t[i] == '-';
        if (neg) i++;
        uint32_t digits = 0;
        int64_t v = 0;
        for (; t[i] >= '0' && t[i] <= '9'; i++, digits++) {
            if (digits >= 18u) return false;
            v = v * 10 + (t[i] - '0');
        }
        if (!digits || (t[i] && t[i] != ' ' && t[i] != ',') || out->nconst >= ZXWIRE_CONST)
            return false;
        out->konst[out->nconst++] = neg ? -v : v;
    }
}

int zxboard_move(zxboard_t *b, int node, uint32_t i, int32_t x, int32_t y)
{
    if (x < 0 || y < 0 || x > 100000 || y > 100000) return fail(b, ZXAPP_E_ARG, "off the board", 0);
    if (node == ZXBOARD_NODE_PART && i < b->c.nparts) {
        b->c.part[i].x = x, b->c.part[i].y = y, b->c.part[i].placed = true;
    } else if (node == ZXBOARD_NODE_INPUT && i < b->c.ninputs) {
        b->c.in_x[i] = x, b->c.in_y[i] = y, b->c.in_placed[i] = true;
    } else if (node == ZXBOARD_NODE_OUT) {
        b->c.out_x = x, b->c.out_y = y, b->c.out_placed = true;
    } else
        return fail(b, ZXAPP_E_ARG, "no such place", 0);
    return ZXAPP_OK;
}

/* ---- CB3: every wire's state --------------------------------------------------- */

static void why(zxboard_check_t *k, const char *a, const char *b, const char *c, const char *d)
{
    if (k->why[0]) return; /* the first problem speaks */
    const char *m[4] = {a, b, c, d};
    uint32_t n = 0;
    for (uint32_t i = 0; i < 4u; i++)
        for (const char *p = m[i]; p && *p && n + 1u < ZXAPP_ERR_MAX; p++) k->why[n++] = *p;
    k->why[n] = 0;
}

void zxboard_check(const zxboard_t *b, zxboard_check_t *k)
{
    zero(k, sizeof *k);
    const zxwire_circuit_t *c = &b->c;
    bool takes[ZXWIRE_PARTS][ZXWIRE_PARTS]; /* takes[i][j]: i reaches back to j's answer */
    zero(takes, sizeof takes);
    for (uint32_t i = 0; i < c->nparts; i++) {
        const zxapp_provide_t *C = zxboard_contract(b, c->part[i].entry, 0);
        k->part[i] = C ? ZXWIRE_OK : ZXWIRE_OPEN;
        k->kind[i] = C ? C->kind : 0;
        for (uint32_t j = 0; j < c->part[i].nports; j++)
            if (c->part[i].src[j].kind == ZXWIRE_SRC_PART) takes[i][c->part[i].src[j].index] = true;
    }
    for (uint32_t m = 0; m < c->nparts; m++) /* transitive closure: 16 parts at most */
        for (uint32_t i = 0; i < c->nparts; i++)
            for (uint32_t j = 0; j < c->nparts; j++) takes[i][j] |= takes[i][m] && takes[m][j];
    for (uint32_t i = 0; i < c->nparts; i++) k->loop[i] = takes[i][i];
    for (uint32_t i = 0; i < c->nparts; i++) {
        const zxwire_part_t *P = &c->part[i];
        if (k->part[i] != ZXWIRE_OK) why(k, P->name, ": ", P->entry, " is not on the shelf");
        for (uint32_t j = 0; j < P->nports; j++) {
            const zxwire_src_t *s = &P->src[j];
            int32_t st = ZXWIRE_OK;
            if (s->kind == ZXWIRE_SRC_NONE) {
                st = ZXWIRE_OPEN;
                why(k, P->name, ".", P->port[j], " is not wired");
            } else if (s->kind == ZXWIRE_SRC_PART) {
                uint32_t f = s->index;
                if (takes[f][i]) {
                    st = ZXWIRE_HELD;
                    why(k, P->name, ".", P->port[j], " is on a loop");
                } else if (!k->kind[f])
                    st = ZXWIRE_OPEN;
                else if (k->kind[f] != ZXAPP_KIND_NUM) {
                    st = ZXWIRE_HELD;
                    why(k, P->name, ".", P->port[j], " takes whole numbers, and gets a truth");
                }
            }
            if (k->part[i] != ZXWIRE_OK) st = ZXWIRE_OPEN;
            k->port[i][j] = st;
        }
    }
    const zxwire_src_t *o = &c->out;
    uint8_t ok = zxwire_src_kind(c, o, k->kind);
    if (o->kind == ZXWIRE_SRC_NONE) {
        k->out = ZXWIRE_OPEN;
        why(k, "the answer is not wired", 0, 0, 0);
    } else if (!ok)
        k->out = ZXWIRE_OPEN;
    else if (ok != c->kind) {
        k->out = ZXWIRE_HELD;
        why(k, "the answer is wired to ", ok == ZXAPP_KIND_TRUTH ? "a truth" : "numbers",
            c->kind == ZXAPP_KIND_TRUTH ? ", and the app answers a truth"
                                        : ", and the app answers numbers",
            0);
    } else
        k->out = ZXWIRE_OK;
    for (uint32_t i = 0; i <= c->nparts; i++) {
        uint32_t n = i < c->nparts ? c->part[i].nports : 1u;
        for (uint32_t j = 0; j < n; j++) {
            int32_t st = i < c->nparts ? k->port[i][j] : k->out;
            if (st == ZXWIRE_OK)
                k->ok++;
            else if (st == ZXWIRE_HELD)
                k->held++;
            else
                k->open++;
        }
    }
    k->lp = k->held ? ZXAPP_BOTH : k->open ? ZXAPP_NEITHER : ZXAPP_TRUE;
}

/* ---- CB4: saving ------------------------------------------------------------- */

typedef struct {
    char *s;
    uint32_t cap, n;
    bool over;
} out_t;

static void put(out_t *o, const char *t)
{
    for (; t && *t; t++) {
        if (o->n + 1u >= o->cap) {
            o->over = true;
            return;
        }
        o->s[o->n++] = *t;
    }
}

static void put_i(out_t *o, int64_t v)
{
    char t[24];
    uint32_t k = 0;
    uint64_t u = v < 0 ? (uint64_t) (-(v + 1)) + 1u : (uint64_t) v;
    do {
        t[k++] = (char) ('0' + u % 10u);
        u /= 10u;
    } while (u);
    if (v < 0) t[k++] = '-';
    char r[24];
    for (uint32_t i = 0; i < k; i++) r[i] = t[k - 1u - i];
    r[k] = 0;
    put(o, r);
}

static void put_src(out_t *o, const zxwire_circuit_t *c, const zxwire_src_t *s)
{
    if (s->kind == ZXWIRE_SRC_INPUT)
        put(o, c->input[s->index]);
    else if (s->kind == ZXWIRE_SRC_PART)
        put(o, c->part[s->index].name);
    else
        for (uint32_t i = 0; i < s->nconst; i++) {
            if (i) put(o, ",");
            put_i(o, s->konst[i]);
        }
}

static void put_at(out_t *o, const char *name, int32_t x, int32_t y)
{
    put(o, "at ");
    put(o, name);
    put(o, " ");
    put_i(o, x);
    put(o, " ");
    put_i(o, y);
    put(o, "\n");
}

static void put_provides(out_t *o, const char *entry, uint8_t kind, const char (*in)[ZXAPP_ID_MAX],
                         uint32_t n)
{
    put(o, "provides ");
    put(o, entry);
    put(o, kind == ZXAPP_KIND_TRUTH ? " truth" : " num");
    for (uint32_t i = 0; i < n; i++) {
        put(o, " ");
        put(o, in[i]);
    }
    put(o, "\n");
}

int32_t zxboard_save(zxboard_t *b, char *buf, uint32_t cap)
{
    zxboard_check_t k;
    zxboard_check(b, &k);
    for (uint32_t i = 0; i < b->c.nparts; i++)
        if (k.loop[i]) return fail(b, ZXAPP_E_CONTRACT, "not saved: ", k.why);
    if (k.open) return fail(b, ZXAPP_E_CONTRACT, "not saved: ", k.why);
    if (!buf || cap < 2u) return fail(b, ZXAPP_E_BUFFER, "no room", 0);
    const zxwire_circuit_t *c = &b->c;
    /* every source first: the lowest-numbered part whose sources are all out */
    uint32_t order[ZXWIRE_PARTS], n = 0;
    bool done[ZXWIRE_PARTS] = {false};
    while (n < c->nparts)
        for (uint32_t i = 0; i < c->nparts; i++) {
            if (done[i]) continue;
            bool ready = true;
            for (uint32_t j = 0; j < c->part[i].nports && ready; j++)
                ready = c->part[i].src[j].kind != ZXWIRE_SRC_PART || done[c->part[i].src[j].index];
            if (!ready) continue;
            done[i] = true;
            order[n++] = i;
            break;
        }
    out_t o = {buf, cap, 0, false};
    put(&o, "zxapp 1\nid ");
    put(&o, b->id);
    put(&o, "\nname ");
    put(&o, b->name);
    put(&o, "\n");
    if (b->author[0]) {
        put(&o, "author ");
        put(&o, b->author);
        put(&o, "\n");
    }
    put(&o, "version 1.0.0\nview auto\n");
    /* one use per block, in the order the parts first need them */
    const zxapp_t *used[ZXWIRE_PARTS];
    uint32_t nused = 0;
    for (uint32_t q = 0; q < n; q++) {
        const zxapp_t *from = 0;
        zxboard_contract(b, c->part[order[q]].entry, &from);
        bool seen = false;
        for (uint32_t u = 0; u < nused && !seen; u++) seen = used[u] == from;
        if (seen || !from) continue;
        used[nused++] = from;
        put(&o, "use ");
        put(&o, from->id);
        put(&o, " ");
        put_i(&o, from->ver[0]);
        put(&o, ".");
        put_i(&o, from->ver[1]);
        put(&o, "\n");
    }
    /* C1: what the circuit wires is exported with it, so the app is a block too */
    for (uint32_t q = 0; q < n; q++) {
        const zxwire_part_t *P = &c->part[order[q]];
        bool seen = false;
        for (uint32_t r = 0; r < q && !seen; r++) seen = seq(c->part[order[r]].entry, P->entry);
        if (seen) continue;
        const zxapp_provide_t *C = zxboard_contract(b, P->entry, 0);
        put_provides(&o, P->entry, C->kind, C->input, C->ninputs);
    }
    put_provides(&o, b->entry, c->kind, c->input, c->ninputs);
    put(&o, "logic wire ");
    put(&o, b->entry);
    put(&o, " <<\n");
    for (uint32_t q = 0; q < n; q++) {
        const zxwire_part_t *P = &c->part[order[q]];
        put(&o, "part ");
        put(&o, P->name);
        put(&o, " ");
        put(&o, P->entry);
        for (uint32_t j = 0; j < P->nports; j++) {
            put(&o, " ");
            put(&o, P->port[j]);
            put(&o, "=");
            zxwire_src_t s = P->src[j];
            put_src(&o, c, &s);
        }
        put(&o, "\n");
    }
    put(&o, "out ");
    put_src(&o, c, &c->out);
    put(&o, "\n");
    for (uint32_t i = 0; i < c->ninputs; i++) put_at(&o, c->input[i], c->in_x[i], c->in_y[i]);
    for (uint32_t q = 0; q < n; q++)
        put_at(&o, c->part[order[q]].name, c->part[order[q]].x, c->part[order[q]].y);
    put_at(&o, "out", c->out_x, c->out_y);
    put(&o, ".\n");
    if (o.over) return fail(b, ZXAPP_E_BUFFER, "the manifest does not fit", 0);
    buf[o.n] = 0;
    b->err[0] = 0;
    return (int32_t) o.n;
}

/* ---- CB5, CB6 ---------------------------------------------------------------- */

int zxboard_open(zxboard_t *b, const zxapp_registry_t *reg, const zxapp_t *a, const char *entry)
{
    const zxapp_logic_t *L = a && entry ? zxapp_find(a, entry) : 0;
    while (L && L->lang != ZXAPP_LANG_WIRE) { /* the circuit among its languages (A5) */
        const zxapp_logic_t *next = 0;
        for (const zxapp_logic_t *q = L + 1; q < a->logic + a->n && !next; q++)
            if (seq(q->entry, entry)) next = q;
        L = next;
    }
    int rc = zxboard_init(b, reg, a ? a->id : 0, a ? a->name : 0);
    if (rc != ZXAPP_OK) return rc;
    if (!L || !L->src) return fail(b, ZXAPP_E_NAME, "no circuit named ", entry);
    scopy(b->author, sizeof b->author, a->author);
    scopy(b->entry, sizeof b->entry, entry);
    if (zxwire_parse(&b->c, zxapp_contract(a, entry), L->src, L->src_len) != ZXAPP_OK)
        return fail(b, ZXAPP_E_SYNTAX, "the circuit does not parse: ", b->c.err);
    if (!b->c.out_placed) b->c.out_x = 720, b->c.out_y = 200, b->c.out_placed = true;
    for (uint32_t i = 0; i < b->c.ninputs; i++)
        if (!b->c.in_placed[i]) b->c.in_x[i] = 40, b->c.in_y[i] = 80 + 96 * (int32_t) i;
    for (uint32_t i = 0; i < b->c.nparts; i++) { /* ports in their contract's order */
        zxwire_part_t *P = &b->c.part[i];
        if (!P->placed) P->x = 240 + 200 * (int32_t) (i % 3u), P->y = 80 + 140 * (int32_t) (i / 3u);
        P->placed = true;
        const zxapp_provide_t *C = zxboard_contract(b, P->entry, 0);
        if (!C) continue;
        zxwire_part_t was = *P;
        for (uint32_t q = 0; q < C->ninputs; q++) {
            scopy(P->port[q], ZXAPP_ID_MAX, C->input[q]);
            zero(&P->src[q], sizeof P->src[q]);
            for (uint32_t j = 0; j < was.nports; j++)
                if (seq(was.port[j], C->input[q])) P->src[q] = was.src[j];
        }
        P->nports = C->ninputs;
    }
    for (uint32_t i = 0; i < b->c.ninputs; i++) b->c.in_placed[i] = true;
    return ZXAPP_OK;
}

int zxboard_try(zxboard_t *b, zxapp_host_t *h, const zxapp_arg_t *args, uint32_t nargs,
                zxapp_num_t *out, uint32_t cap, uint32_t *n_out, int32_t *lp)
{
    int32_t len = zxboard_save(b, b->text, sizeof b->text);
    if (len < 0) return len;
    int rc = zxapp_parse(&b->app, b->text, (uint32_t) len);
    if (rc == ZXAPP_OK) rc = zxapp_compose(&b->app, b->reg);
    if (rc == ZXAPP_OK) {
        int got = zxapp_load(&b->app, h);
        rc = got < 0 ? got : ZXAPP_OK;
    }
    if (rc == ZXAPP_OK)
        rc = zxapp_call_witnessed(&b->app, h, b->entry, args, nargs, out, cap, n_out, lp);
    if (rc != ZXAPP_OK) fail(b, rc, b->app.err, 0);
    zxapp_unload(&b->app, h);
    return rc;
}
