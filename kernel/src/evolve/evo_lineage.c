/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* evo_lineage.c — signed fork records and the lineage DAG (F1-F4). */
#include "evo_util.h"

/* ===== Records ===== */
evo_status_t evo_fork_init(evo_fork_t *f, const uint8_t author_id[32], const char *label,
                           uint64_t stamp)
{
    if (!f || !author_id) return EVO_ERR_ARG;
    evo_zero(f, sizeof(*f));
    evo_cpy(f->author, author_id, 32);
    if (label) {
        uint32_t n = evo_strlen(label, EVO_LABEL_MAX);
        if (n > EVO_LABEL_MAX) return EVO_ERR_ARG;
        evo_cpy(f->label, label, n);
        f->label_len = (uint8_t) n;
    }
    f->stamp = stamp;
    return EVO_OK;
}

evo_status_t evo_fork_parent(evo_fork_t *f, const evo_cid_t *parent)
{
    if (!f || !parent) return EVO_ERR_ARG;
    uint32_t i = 0;
    while (i < f->n_parents && evo_cid_cmp(&f->parent[i], parent) < 0) i++;
    if (i < f->n_parents && evo_cid_eq(&f->parent[i], parent)) return EVO_ERR_DUP;
    if (f->n_parents >= EVO_LIN_MAX_PARENTS) return EVO_ERR_FULL;
    for (uint32_t j = f->n_parents; j > i; j--)
        evo_cpy(&f->parent[j], &f->parent[j - 1], sizeof(evo_cid_t));
    evo_cpy(&f->parent[i], parent, sizeof(evo_cid_t));
    f->n_parents++;
    return EVO_OK;
}

evo_status_t evo_fork_change(evo_fork_t *f, evo_change_op_t op, const char *slot,
                             const evo_cid_t *val, const evo_cid_t *old)
{
    evo_change_t c;
    if (!f || op < EVO_CH_ADD || op > EVO_CH_REMOVE) return EVO_ERR_ARG;
    evo_zero(&c, sizeof(c));
    if (!evo_set_name(c.slot, &c.slot_len, slot, EVO_NAME_MAX)) return EVO_ERR_ARG;
    c.op = (uint8_t) op;
    if (op != EVO_CH_REMOVE) {
        if (!val) return EVO_ERR_ARG;
        evo_cpy(&c.val, val, sizeof(c.val));
    }
    if (op != EVO_CH_ADD) {
        if (!old) return EVO_ERR_ARG;
        evo_cpy(&c.old, old, sizeof(c.old));
    }
    uint32_t i = 0;
    while (i < f->n_changes &&
           evo_name_cmp(f->ch[i].slot, f->ch[i].slot_len, c.slot, c.slot_len) < 0)
        i++;
    if (i < f->n_changes && evo_name_cmp(f->ch[i].slot, f->ch[i].slot_len, c.slot, c.slot_len) == 0)
        return EVO_ERR_DUP;
    if (f->n_changes >= EVO_LIN_MAX_CHANGES) return EVO_ERR_FULL;
    for (uint32_t j = f->n_changes; j > i; j--) evo_cpy(&f->ch[j], &f->ch[j - 1], sizeof(c));
    evo_cpy(&f->ch[i], &c, sizeof(c));
    f->n_changes++;
    return EVO_OK;
}

#define FORK_MAGIC EVO_MAGIC('E', 'V', 'L', '1')

uint32_t evo_fork_encode(const evo_fork_t *f, uint8_t *out, uint32_t cap)
{
    if (!f || !out || f->n_parents > EVO_LIN_MAX_PARENTS || f->n_changes > EVO_LIN_MAX_CHANGES ||
        f->label_len > EVO_LABEL_MAX)
        return 0;
    evo_w_t w = {out, cap, 0, false};
    evo_w_u32(&w, FORK_MAGIC);
    evo_w_u8(&w, f->n_parents);
    for (uint32_t i = 0; i < f->n_parents; i++) {
        if (i && evo_cid_cmp(&f->parent[i - 1], &f->parent[i]) >= 0) return 0;
        evo_w_bytes(&w, f->parent[i].b, EVO_CID_LEN);
    }
    evo_w_bytes(&w, f->author, 32);
    evo_w_u8(&w, f->label_len);
    evo_w_bytes(&w, f->label, f->label_len);
    evo_w_u64(&w, f->stamp);
    evo_w_u8(&w, f->n_changes);
    for (uint32_t i = 0; i < f->n_changes; i++) {
        const evo_change_t *c = &f->ch[i];
        if (c->op < EVO_CH_ADD || c->op > EVO_CH_REMOVE || !c->slot_len ||
            c->slot_len > EVO_NAME_MAX)
            return 0;
        if (i && evo_name_cmp(f->ch[i - 1].slot, f->ch[i - 1].slot_len, c->slot, c->slot_len) >= 0)
            return 0;
        evo_w_u8(&w, c->op);
        evo_w_u8(&w, c->slot_len);
        evo_w_bytes(&w, c->slot, c->slot_len);
        if (c->op != EVO_CH_REMOVE) evo_w_bytes(&w, c->val.b, EVO_CID_LEN);
        if (c->op != EVO_CH_ADD) evo_w_bytes(&w, c->old.b, EVO_CID_LEN);
    }
    return w.err ? 0 : w.len;
}

evo_status_t evo_fork_decode(const uint8_t *in, uint32_t len, evo_fork_t *f)
{
    if (!in || !f) return EVO_ERR_ARG;
    evo_zero(f, sizeof(*f));
    evo_r_t r = {in, len, 0, false};
    if (evo_r_le(&r, 4) != FORK_MAGIC) return EVO_ERR_PARSE;
    f->n_parents = (uint8_t) evo_r_le(&r, 1);
    if (f->n_parents > EVO_LIN_MAX_PARENTS) return EVO_ERR_PARSE;
    for (uint32_t i = 0; i < f->n_parents; i++) {
        evo_r_copy(&r, f->parent[i].b, EVO_CID_LEN);
        if (i && evo_cid_cmp(&f->parent[i - 1], &f->parent[i]) >= 0) return EVO_ERR_PARSE;
    }
    evo_r_copy(&r, f->author, 32);
    f->label_len = (uint8_t) evo_r_le(&r, 1);
    if (f->label_len > EVO_LABEL_MAX) return EVO_ERR_PARSE;
    evo_r_copy(&r, f->label, f->label_len);
    f->stamp = evo_r_le(&r, 8);
    f->n_changes = (uint8_t) evo_r_le(&r, 1);
    if (r.err || f->n_changes > EVO_LIN_MAX_CHANGES) return EVO_ERR_PARSE;
    for (uint32_t i = 0; i < f->n_changes; i++) {
        evo_change_t *c = &f->ch[i];
        c->op = (uint8_t) evo_r_le(&r, 1);
        c->slot_len = (uint8_t) evo_r_le(&r, 1);
        if (r.err || c->op < EVO_CH_ADD || c->op > EVO_CH_REMOVE || !c->slot_len ||
            c->slot_len > EVO_NAME_MAX)
            return EVO_ERR_PARSE;
        evo_r_copy(&r, c->slot, c->slot_len);
        if (c->op != EVO_CH_REMOVE) evo_r_copy(&r, c->val.b, EVO_CID_LEN);
        if (c->op != EVO_CH_ADD) evo_r_copy(&r, c->old.b, EVO_CID_LEN);
        if (r.err) return EVO_ERR_PARSE;
        for (uint32_t k = 0; k < c->slot_len; k++)
            if (c->slot[k] == 0) return EVO_ERR_PARSE;
        if (i && evo_name_cmp(f->ch[i - 1].slot, f->ch[i - 1].slot_len, c->slot, c->slot_len) >= 0)
            return EVO_ERR_PARSE;
    }
    if (r.err || r.pos != len) return EVO_ERR_PARSE;
    return EVO_OK;
}

uint32_t evo_fork_wire(const uint8_t *body, uint32_t body_len, const uint8_t *pk, uint32_t pk_len,
                       const uint8_t *sig, uint32_t sig_len, uint8_t *out, uint32_t cap)
{
    if (!body || !pk || !sig || !out) return 0;
    evo_w_t w = {out, cap, 0, false};
    evo_w_u32(&w, body_len);
    evo_w_bytes(&w, body, body_len);
    evo_w_u32(&w, pk_len);
    evo_w_bytes(&w, pk, pk_len);
    evo_w_u32(&w, sig_len);
    evo_w_bytes(&w, sig, sig_len);
    return w.err ? 0 : w.len;
}

/* ===== DAG ===== */
void evo_dag_init(evo_dag_t *g)
{
    evo_zero(g, sizeof(*g));
}

int32_t evo_dag_find(const evo_dag_t *g, const evo_cid_t *cid)
{
    for (uint32_t i = 0; i < g->n; i++)
        if (evo_cid_eq(&g->node[i].cid, cid)) return (int32_t) i;
    return -1;
}

bool evo_dag_is_ancestor(const evo_dag_t *g, uint32_t anc, uint32_t of)
{
    if (anc >= g->n || of >= g->n) return false;
    return (g->node[of].anc >> anc) & 1u;
}

/* The distinct head values of one slot over the node set `set` (F3). A
 * DELETE head counts as a value of its own (del[i]). Every node in a DAG is
 * conflict-free, and a head over a union is a head in some member's set, so
 * at most one distinct value per parent: EVO_LIN_MAX_PARENTS suffices. */
typedef struct {
    uint32_t nv;
    evo_cid_t v[EVO_LIN_MAX_PARENTS];
    bool del[EVO_LIN_MAX_PARENTS];
} heads_t;

static const evo_change_t *node_write(const evo_dag_node_t *n, const char *s, uint32_t sl)
{
    for (uint32_t c = 0; c < n->f.n_changes; c++)
        if (evo_name_cmp(n->f.ch[c].slot, n->f.ch[c].slot_len, s, sl) == 0) return &n->f.ch[c];
    return 0;
}

static evo_status_t slot_heads(const evo_dag_t *g, uint64_t set, const char *s, uint32_t sl,
                               heads_t *h)
{
    h->nv = 0;
    for (uint32_t i = 0; i < g->n; i++) {
        if (!((set >> i) & 1u)) continue;
        const evo_change_t *c = node_write(&g->node[i], s, sl);
        if (!c) continue;
        bool superseded = false;
        for (uint32_t j = 0; j < g->n && !superseded; j++)
            if (j != i && ((set >> j) & 1u) && ((g->node[j].anc >> i) & 1u) &&
                node_write(&g->node[j], s, sl))
                superseded = true;
        if (superseded) continue;
        bool del = c->op == EVO_CH_REMOVE;
        bool seen = false;
        for (uint32_t k = 0; k < h->nv && !seen; k++)
            seen = h->del[k] == del && (del || evo_cid_eq(&h->v[k], &c->val));
        if (seen) continue;
        if (h->nv >= EVO_LIN_MAX_PARENTS) return EVO_ERR_CONFLICT;
        h->del[h->nv] = del;
        evo_cpy(&h->v[h->nv], &c->val, sizeof(evo_cid_t));
        h->nv++;
    }
    return EVO_OK;
}

/* Calls fn once per distinct slot written anywhere in `set`. */
typedef evo_status_t (*slot_fn)(const evo_dag_t *g, const char *s, uint32_t sl, const heads_t *h,
                                void *ctx);

static evo_status_t for_each_slot(const evo_dag_t *g, uint64_t set, slot_fn fn, void *ctx)
{
    for (uint32_t i = 0; i < g->n; i++) {
        if (!((set >> i) & 1u)) continue;
        const evo_dag_node_t *n = &g->node[i];
        for (uint32_t c = 0; c < n->f.n_changes; c++) {
            const char *s = n->f.ch[c].slot;
            uint32_t sl = n->f.ch[c].slot_len;
            bool earlier = false;
            for (uint32_t j = 0; j <= i && !earlier; j++) {
                if (!((set >> j) & 1u)) continue;
                uint32_t lim = j == i ? c : g->node[j].f.n_changes;
                for (uint32_t d = 0; d < lim && !earlier; d++)
                    earlier = evo_name_cmp(g->node[j].f.ch[d].slot, g->node[j].f.ch[d].slot_len, s,
                                           sl) == 0;
            }
            if (earlier) continue;
            heads_t h;
            evo_status_t st = slot_heads(g, set, s, sl, &h);
            if (st == EVO_OK) st = fn(g, s, sl, &h, ctx);
            if (st != EVO_OK) return st;
        }
    }
    return EVO_OK;
}

static evo_status_t parent_set(const evo_dag_t *g, const evo_cid_t *parents, uint32_t n,
                               uint64_t *set, uint8_t *pidx)
{
    *set = 0;
    for (uint32_t p = 0; p < n; p++) {
        int32_t k = evo_dag_find(g, &parents[p]);
        if (k < 0) return EVO_ERR_UNKNOWN_PARENT;
        if (pidx) pidx[p] = (uint8_t) k;
        *set |= g->node[k].anc | ((uint64_t) 1 << k);
    }
    return EVO_OK;
}

/* F4: check the record's changes against its parents' merged state. */
typedef struct {
    const evo_fork_t *f;
    uint32_t resolved; /* bit c: change c matched a merged slot */
} vctx_t;

static evo_status_t validate_slot(const evo_dag_t *g, const char *s, uint32_t sl, const heads_t *h,
                                  void *ctx)
{
    (void) g;
    vctx_t *v = (vctx_t *) ctx;
    const evo_change_t *c = 0;
    uint32_t ci = 0;
    for (; ci < v->f->n_changes; ci++)
        if (evo_name_cmp(v->f->ch[ci].slot, v->f->ch[ci].slot_len, s, sl) == 0) {
            c = &v->f->ch[ci];
            break;
        }
    bool present = false, absent = false; /* some head holds a value / is a delete */
    for (uint32_t k = 0; k < h->nv; k++) {
        if (h->del[k])
            absent = true;
        else
            present = true;
    }
    if (!c) return h->nv > 1 ? EVO_ERR_CONFLICT : EVO_OK; /* unresolved conflict */
    v->resolved |= 1u << ci;
    if (c->op == EVO_CH_ADD) return absent ? EVO_OK : EVO_ERR_CONFLICT;
    if (!present) return EVO_ERR_CONFLICT;
    for (uint32_t k = 0; k < h->nv; k++)
        if (!h->del[k] && evo_cid_eq(&h->v[k], &c->old)) return EVO_OK;
    return EVO_ERR_CONFLICT; /* old does not match the current value */
}

evo_status_t evo_dag_add_wire(evo_dag_t *g, const uint8_t *wire, uint32_t len, evo_verify_fn vf,
                              void *vctx, uint32_t *idx)
{
    if (!g || !wire || !vf) return EVO_ERR_ARG;
    evo_r_t r = {wire, len, 0, false};
    uint32_t bl = (uint32_t) evo_r_le(&r, 4);
    const uint8_t *body = evo_r_bytes(&r, bl);
    uint32_t pl = (uint32_t) evo_r_le(&r, 4);
    const uint8_t *pk = evo_r_bytes(&r, pl);
    uint32_t sl = (uint32_t) evo_r_le(&r, 4);
    const uint8_t *sig = evo_r_bytes(&r, sl);
    if (r.err || r.pos != len || bl > EVO_LIN_BODY_MAX) return EVO_ERR_PARSE;

    evo_dag_node_t nd;
    evo_zero(&nd, sizeof(nd));
    evo_status_t st = evo_fork_decode(body, bl, &nd.f);
    if (st != EVO_OK) return st;
    evo_cid_of(body, bl, &nd.cid);
    int32_t have = evo_dag_find(g, &nd.cid);
    if (have >= 0) {
        if (idx) *idx = (uint32_t) have;
        return EVO_ERR_DUP;
    }
    uint8_t kid[32];
    evo_key_id(pk, pl, kid);
    if (evo_cmp(kid, nd.f.author, 32) != 0) return EVO_ERR_BAD_SIG;
    if (!vf(pk, pl, body, bl, sig, sl, vctx)) return EVO_ERR_BAD_SIG;

    uint64_t set;
    st = parent_set(g, nd.f.parent, nd.f.n_parents, &set, nd.pidx);
    if (st != EVO_OK) return st;
    if (g->n >= EVO_DAG_MAX) return EVO_ERR_FULL;
    vctx_t v = {&nd.f, 0};
    st = for_each_slot(g, set, validate_slot, &v);
    if (st != EVO_OK) return st;
    for (uint32_t c = 0; c < nd.f.n_changes; c++) /* slots new to this lineage */
        if (!((v.resolved >> c) & 1u) && nd.f.ch[c].op != EVO_CH_ADD) return EVO_ERR_CONFLICT;
    nd.anc = set;
    evo_cpy(&g->node[g->n], &nd, sizeof(nd));
    if (idx) *idx = g->n;
    g->n++;
    return EVO_OK;
}

typedef struct {
    evo_state_t *st;
} sctx_t;

static evo_status_t collect_slot(const evo_dag_t *g, const char *s, uint32_t sl, const heads_t *h,
                                 void *ctx)
{
    (void) g;
    evo_state_t *st = ((sctx_t *) ctx)->st;
    if (h->nv != 1) return EVO_ERR_CONFLICT; /* cannot happen for an accepted node */
    if (h->del[0]) return EVO_OK;
    uint32_t i = 0;
    while (i < st->n && evo_name_cmp(st->s[i].slot, st->s[i].slot_len, s, sl) < 0) i++;
    if (st->n >= EVO_STATE_MAX) return EVO_ERR_FULL;
    for (uint32_t j = st->n; j > i; j--) evo_cpy(&st->s[j], &st->s[j - 1], sizeof(evo_slot_t));
    evo_zero(&st->s[i], sizeof(evo_slot_t));
    st->s[i].slot_len = (uint8_t) sl;
    evo_cpy(st->s[i].slot, s, sl);
    evo_cpy(&st->s[i].val, &h->v[0], sizeof(evo_cid_t));
    st->n++;
    return EVO_OK;
}

evo_status_t evo_dag_state(const evo_dag_t *g, uint32_t idx, evo_state_t *out)
{
    if (!g || !out || idx >= g->n) return EVO_ERR_ARG;
    evo_zero(out, sizeof(*out));
    sctx_t c = {out};
    return for_each_slot(g, g->node[idx].anc | ((uint64_t) 1 << idx), collect_slot, &c);
}

typedef struct {
    char (*names)[EVO_NAME_MAX];
    uint32_t cap, n;
} cctx_t;

static evo_status_t conflict_slot(const evo_dag_t *g, const char *s, uint32_t sl, const heads_t *h,
                                  void *ctx)
{
    (void) g;
    cctx_t *c = (cctx_t *) ctx;
    if (h->nv <= 1) return EVO_OK;
    if (c->n < c->cap) {
        evo_zero(c->names[c->n], EVO_NAME_MAX);
        evo_cpy(c->names[c->n], s, sl);
    }
    c->n++;
    return EVO_OK;
}

int32_t evo_dag_conflicts(const evo_dag_t *g, const evo_cid_t *parents, uint32_t n_parents,
                          char names[][EVO_NAME_MAX], uint32_t cap)
{
    uint64_t set;
    if (!g || !parents || n_parents > EVO_LIN_MAX_PARENTS) return -1;
    if (parent_set(g, parents, n_parents, &set, 0) != EVO_OK) return -1;
    cctx_t c = {names, names ? cap : 0, 0};
    if (for_each_slot(g, set, conflict_slot, &c) != EVO_OK) return -1;
    return (int32_t) c.n;
}

const evo_cid_t *evo_state_get(const evo_state_t *st, const char *slot)
{
    uint32_t n = evo_strlen(slot, EVO_NAME_MAX);
    for (uint32_t i = 0; i < st->n; i++)
        if (evo_name_cmp(st->s[i].slot, st->s[i].slot_len, slot, n) == 0) return &st->s[i].val;
    return 0;
}
