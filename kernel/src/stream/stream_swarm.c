/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* stream_swarm.c — swarm scheduler: adverts, deadline-aware rarest-first
 * pulls, reciprocity-weighted DRR uploads under a hard cap, origin push with
 * notices, the playback clock, and the wire codecs. See stream_swarm.h for
 * the policy and the HONEST LIMITS. Freestanding: no libc, no division. */
#include "stream_swarm.h"

#define SLOT_MASK  ((uint32_t) SSW_WIN - 1u)
#define BIG_TICKS  0x3FFFFFFFu
#define MINRTT_INF 0xFFFFFFu

/* ---- helpers ---- */

static void zero(void *p, uint32_t n)
{
    uint8_t *d = (uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

static void copy(uint8_t *d, const uint8_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static bool bit(const uint8_t *bm, uint32_t i)
{
    return (bm[i >> 3] >> (i & 7)) & 1u;
}

static void bset(uint8_t *bm, uint32_t i)
{
    bm[i >> 3] |= (uint8_t) (1u << (i & 7));
}

static void bclr(uint8_t *bm, uint32_t i)
{
    bm[i >> 3] &= (uint8_t) ~(1u << (i & 7));
}

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static uint32_t get16(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

static bool magic(const uint8_t *p, char c)
{
    return p[0] == 'Z' && p[1] == 'X' && p[2] == 'S' && p[3] == (uint8_t) c;
}

/* ---- playback clock ---- */

void ssw_player_init(ssw_player_t *p, uint32_t seg_ticks, uint8_t startup_segs, uint32_t now)
{
    if (!p) return;
    zero(p, sizeof *p);
    p->seg_ticks = seg_ticks ? seg_ticks : 1;
    p->startup_segs = startup_segs ? startup_segs : 1;
    p->join_tick = now;
}

void ssw_player_set_start(ssw_player_t *p, uint32_t seq)
{
    if (!p || p->started) return;
    p->start_known = true;
    p->start_seq = seq;
}

void ssw_player_step(ssw_player_t *p, uint32_t now, ssw_complete_fn done, void *ctx)
{
    if (!p || !done || !p->start_known) return;
    if (!p->started) {
        for (uint32_t i = 0; i < p->startup_segs; i++)
            if (!done(ctx, p->start_seq + i)) return;
        p->started = true;
        p->start_tick = now;
        p->play_seq = p->start_seq;
        p->play_until = now + p->seg_ticks;
        p->played = 1;
        return;
    }
    if (p->stalled) {
        if (done(ctx, p->play_seq + 1)) {
            p->stalled = false;
            p->play_seq++;
            p->play_until = now + p->seg_ticks;
            p->played++;
        } else {
            p->stall_ticks++;
        }
        return;
    }
    while (now >= p->play_until) {
        if (done(ctx, p->play_seq + 1)) {
            p->play_seq++;
            p->play_until += p->seg_ticks;
            p->played++;
        } else {
            p->stalled = true;
            p->stalls++;
            p->stall_ticks++;
            return;
        }
    }
}

static uint32_t mul_ticks(uint32_t segs, uint32_t seg_ticks)
{
    if (segs > 4u * SSW_WIN) segs = 4u * SSW_WIN;
    return segs * seg_ticks;
}

uint32_t ssw_player_deadline(const ssw_player_t *p, uint32_t seq, uint32_t now)
{
    if (!p || !p->start_known) return now + BIG_TICKS;
    if (!p->started) {
        if (seq < p->start_seq) return now + BIG_TICKS;
        if (seq - p->start_seq < p->startup_segs) return now;
        return now + mul_ticks(seq - p->start_seq, p->seg_ticks);
    }
    if (seq <= p->play_seq) return now;
    const uint32_t ahead = mul_ticks(seq - p->play_seq - 1, p->seg_ticks);
    if (p->stalled) return now + ahead;
    return p->play_until + ahead;
}

/* ---- window ---- */

static ssw_oseg_t *slot_get(ssw_node_t *n, uint32_t seq)
{
    if (!n->any || seq < n->base || seq - n->base >= SSW_WIN) return 0;
    ssw_oseg_t *s = &n->seg[seq & SLOT_MASK];
    return (s->used && s->seq == seq) ? s : 0;
}

static const ssw_oseg_t *slot_cget(const ssw_node_t *n, uint32_t seq)
{
    if (!n->any || seq < n->base || seq - n->base >= SSW_WIN) return 0;
    const ssw_oseg_t *s = &n->seg[seq & SLOT_MASK];
    return (s->used && s->seq == seq) ? s : 0;
}

/* Slot for seq, allocating (and moving the window forward) if needed. */
static ssw_oseg_t *slot_alloc_ex(ssw_node_t *n, uint32_t seq, bool advance)
{
    if (!n->any) {
        if (!advance) return 0;
        n->any = true;
        n->base = seq;
    }
    if (seq < n->base) {
        /* A verified manifest older than the window start may still fit:
         * manifests can arrive out of order (a join batch overtaken by a
         * fresh one). Slots in [seq, base) hold nothing valid. */
        if (!advance || (n->newest != SSW_NONE && n->newest - seq >= SSW_WIN)) return 0;
        n->base = seq;
    }
    if (seq - n->base >= SSW_WIN) {
        if (!advance) return 0;
        n->base = seq - (SSW_WIN - 1u);
    }
    ssw_oseg_t *s = &n->seg[seq & SLOT_MASK];
    if (s->used && s->seq == seq) return s;
    /* Reassign: views of this slot carry the old seq and no longer match,
     * so every count here restarts from zero. */
    zero(s, sizeof *s);
    s->used = 1;
    s->seq = seq;
    s->learned = n->now;
    return s;
}

/* Unverified input (rows, notices, adverts) never moves the window: only a
 * verified manifest (ssw_seg_known) does. */
static ssw_oseg_t *slot_alloc(ssw_node_t *n, uint32_t seq)
{
    return slot_alloc_ex(n, seq, false);
}

/* ---- init / neighbours ---- */

void ssw_config_default(ssw_config_t *c)
{
    if (!c) return;
    zero(c, sizeof *c);
    c->ticks_per_sec = 100;
    c->up_rows_per_sec = 1000;
    c->dn_rows_per_sec = 4000;
    c->seg_ticks = 100;
    c->urgent_ticks = 60; /* 0.6 s at 100 ticks/s: wider made N = 128 congestion-prone */
    c->rescue_ticks = 40;
    c->mode = SSW_MODE_LIVE;
    c->live_back = 1;
    c->startup_segs = 2;
    c->prefetch_segs = SSW_WIN - 4;
    c->urgent_extra = 1;
    c->n_push = 20; /* 160 rows: k + 25% at k = 128 */
    c->push_fanout = 1;
}

int ssw_init(ssw_node_t *n, const ssw_config_t *cfg, uint64_t stream_id, uint32_t self_id,
             uint32_t now)
{
    if (!n || !cfg || !cfg->ticks_per_sec || !cfg->seg_ticks || cfg->mode > SSW_MODE_VOD)
        return SSW_ERR_ARG;
    if (cfg->n_push > SSW_PIECES || cfg->push_fanout < 1 || cfg->push_fanout > 2)
        return SSW_ERR_ARG;
    if (cfg->ticks_per_sec > 100000u || cfg->up_rows_per_sec > 100000000u ||
        cfg->dn_rows_per_sec > 100000000u)
        return SSW_ERR_ARG;
    zero(n, sizeof *n);
    copy((uint8_t *) &n->cfg, (const uint8_t *) cfg, (uint32_t) sizeof *cfg); /* no memcpy */
    if (n->cfg.prefetch_segs == 0 || n->cfg.prefetch_segs > SSW_WIN - 2)
        n->cfg.prefetch_segs = SSW_WIN - 2;
    n->stream_id = stream_id;
    n->self_id = self_id;
    n->now = now;
    n->newest = SSW_NONE;
    n->dirty_lo = 1;
    n->dirty_hi = 0;
    n->nfree = SSW_INFLIGHT;
    for (uint32_t i = 0; i < SSW_INFLIGHT; i++) n->freelist[i] = (uint16_t) (SSW_INFLIGHT - 1u - i);
    ssw_player_init(&n->player, cfg->seg_ticks, cfg->startup_segs, now);
    if (cfg->mode == SSW_MODE_VOD) ssw_player_set_start(&n->player, cfg->start_seq);
    return SSW_OK;
}

int ssw_peer_find(const ssw_node_t *n, uint32_t id)
{
    if (!n) return SSW_ERR_ARG;
    for (int i = 0; i < SSW_MAX_PEERS; i++)
        if (n->peer[i].used && n->peer[i].id == id) return i;
    return SSW_ERR_NOTFOUND;
}

uint32_t ssw_peer_count(const ssw_node_t *n)
{
    uint32_t c = 0;
    if (!n) return 0;
    for (int i = 0; i < SSW_MAX_PEERS; i++) c += n->peer[i].used;
    return c;
}

int ssw_peer_add(ssw_node_t *n, uint32_t id, bool is_origin)
{
    if (!n || id == n->self_id) return SSW_ERR_ARG;
    if (ssw_peer_find(n, id) >= 0) return SSW_ERR_ARG;
    for (int i = 0; i < SSW_MAX_PEERS; i++) {
        ssw_peer_t *p = &n->peer[i];
        if (p->used) continue;
        zero(p, sizeof *p);
        p->used = 1;
        p->id = id;
        p->origin = is_origin ? 1 : 0;
        p->infl_cap = 2;
        p->srtt8 = 8u * 20u;
        p->minrtt = MINRTT_INF;
        p->minrtt_prev = MINRTT_INF;
        p->declared_up = SSW_NONE;
        for (uint32_t s = 0; s < SSW_WIN; s++) p->v[s].seq = SSW_NONE;
        return i;
    }
    return SSW_ERR_FULL;
}

static void cnt_add(ssw_ofr_t *f, const uint8_t *bm)
{
    for (uint32_t b = 0; b < SSW_BM; b++) {
        uint8_t x = bm[b];
        for (uint32_t i = 0; x; i++, x >>= 1)
            if ((x & 1u) && f->cnt[b * 8 + i] < 255) {
                f->cnt[b * 8 + i]++;
                f->tot++;
            }
    }
}

static void cnt_sub(ssw_ofr_t *f, const uint8_t *bm)
{
    for (uint32_t b = 0; b < SSW_BM; b++) {
        uint8_t x = bm[b];
        for (uint32_t i = 0; x; i++, x >>= 1)
            if ((x & 1u) && f->cnt[b * 8 + i]) {
                f->cnt[b * 8 + i]--;
                f->tot--;
            }
    }
}

/* Change one view entry, keeping the counts of own slot `o` exact. */
static void view_set(ssw_ofr_t *o, ssw_vfr_t *v, uint8_t nst, const uint8_t *nbm)
{
    if (v->st == SSW_ST_COMPLETE && o->ncomp) o->ncomp--;
    if (v->st == SSW_ST_PARTIAL && nst == SSW_ST_PARTIAL) {
        for (uint32_t b = 0; b < SSW_BM; b++) {
            uint8_t gone = (uint8_t) (v->bm[b] & ~nbm[b]), add = (uint8_t) (nbm[b] & ~v->bm[b]);
            for (uint32_t i = 0; i < 8; i++) {
                uint32_t r = b * 8 + i;
                if (((gone >> i) & 1u) && o->cnt[r]) {
                    o->cnt[r]--;
                    o->tot--;
                }
                if (((add >> i) & 1u) && o->cnt[r] < 255) {
                    o->cnt[r]++;
                    o->tot++;
                }
            }
        }
    } else {
        if (v->st == SSW_ST_PARTIAL) cnt_sub(o, v->bm);
        if (nst == SSW_ST_PARTIAL) cnt_add(o, nbm);
    }
    if (nst == SSW_ST_COMPLETE && o->ncomp < 255) o->ncomp++;
    v->st = nst;
    if (nst == SSW_ST_PARTIAL)
        copy(v->bm, nbm, SSW_BM);
    else
        zero(v->bm, SSW_BM);
}

static void infl_free(ssw_node_t *n, uint16_t tag, bool unask)
{
    ssw_infl_t *e = &n->infl[tag];
    if (!e->used) return;
    ssw_oseg_t *s = slot_get(n, e->seq);
    if (s) {
        ssw_ofr_t *f = &s->fr[e->fr];
        if (f->ninfl) f->ninfl--;
        if (unask && !bit(f->have, e->pc)) bclr(f->asked, e->pc);
    }
    ssw_peer_t *p = &n->peer[e->peer];
    if (p->infl) p->infl--;
    e->used = 0;
    n->freelist[n->nfree++] = tag;
}

int ssw_peer_remove(ssw_node_t *n, uint32_t id)
{
    int pi = ssw_peer_find(n, id);
    if (pi < 0) return pi;
    ssw_peer_t *p = &n->peer[pi];
    static const uint8_t none[SSW_BM] = {0};
    for (uint32_t s = 0; s < SSW_WIN; s++) {
        ssw_oseg_t *o = &n->seg[s];
        if (!o->used || p->v[s].seq != o->seq) continue;
        for (uint32_t f = 0; f < SSW_MAX_FR; f++) view_set(&o->fr[f], &p->v[s].fr[f], 0, none);
    }
    for (uint32_t t = 0; t < SSW_INFLIGHT; t++)
        if (n->infl[t].used && n->infl[t].peer == pi) infl_free(n, (uint16_t) t, true);
    const uint32_t dropped = (uint32_t) p->qu_n + p->qn_n;
    n->queued = n->queued > dropped ? n->queued - dropped : 0;
    for (uint32_t k = 0; k < SSW_PUSH_SEGS; k++)
        for (uint32_t f = 0; f < SSW_MAX_FR; f++)
            for (uint32_t r = 0; r < SSW_PIECES; r++)
                for (uint32_t c = 0; c < 2; c++)
                    if (n->push[k].tgt[f][r][c] == pi) n->push[k].tgt[f][r][c] = 0xFF;
    p->used = 0;
    return SSW_OK;
}

/* ---- segments ---- */

static void mark_dirty(ssw_node_t *n, uint32_t seq, ssw_ofr_t *f)
{
    f->dirty = 1;
    if (n->dirty_lo > n->dirty_hi) {
        n->dirty_lo = n->dirty_hi = seq;
        return;
    }
    if (seq < n->dirty_lo) n->dirty_lo = seq;
    if (seq > n->dirty_hi) n->dirty_hi = seq;
}

static void check_done(ssw_node_t *n, ssw_oseg_t *s, ssw_ofr_t *f)
{
    if (s->known && !f->done && f->nhave >= SSW_KP(s->k)) {
        f->done = 1;
        s->ndone++;
        n->st.freights_done++;
    }
}

int ssw_seg_known(ssw_node_t *n, uint32_t seq, uint8_t k, uint8_t nfr)
{
    if (!n || k < 1 || k > 168 || nfr < 1 || nfr > SSW_MAX_FR) return SSW_ERR_ARG;
    ssw_oseg_t *s = slot_alloc_ex(n, seq, true);
    if (!s) return SSW_ERR_NOTFOUND;
    if (s->known) return (s->k == k && s->nfr == nfr) ? SSW_OK : SSW_ERR_FORMAT;
    s->known = 1;
    s->k = k;
    s->nfr = nfr;
    s->learned = n->now;
    for (uint32_t f = 0; f < nfr; f++) check_done(n, s, &s->fr[f]);
    if (n->newest == SSW_NONE || seq > n->newest) n->newest = seq;
    return SSW_OK;
}

int ssw_origin_publish(ssw_node_t *n, uint32_t seq, uint8_t k, uint8_t nfr, ssw_notice_t *out,
                       uint32_t cap, uint32_t *nout)
{
    if (!n || !n->cfg.is_origin || !nout || (cap && !out)) return SSW_ERR_ARG;
    *nout = 0;
    int rc = ssw_seg_known(n, seq, k, nfr);
    if (rc) return rc;
    ssw_oseg_t *s = slot_get(n, seq);
    for (uint32_t f = 0; f < nfr; f++) {
        if (!s->fr[f].done) {
            s->fr[f].done = 1;
            s->ndone++;
        }
        mark_dirty(n, seq, &s->fr[f]);
    }
    /* push targets and their weights: declared upload (1 until known) */
    uint8_t tg[SSW_MAX_PEERS];
    int32_t wt[SSW_MAX_PEERS];
    uint32_t nt = 0;
    int32_t total = 0;
    for (uint32_t i = 0; i < SSW_MAX_PEERS; i++) {
        const ssw_peer_t *p = &n->peer[i];
        if (!p->used || p->origin) continue;
        uint32_t w = p->declared_up == SSW_NONE ? 1u : p->declared_up;
        if (w > 1000000u) w = 1000000u;
        if (w == 0) continue;
        wt[nt] = (int32_t) w;
        total += (int32_t) w;
        tg[nt++] = (uint8_t) i;
    }
    if (nt == 0 || n->cfg.n_push == 0) return SSW_OK;
    if (cap < nt * nfr) return SSW_ERR_SPACE;
    /* pick the inactive (or oldest) push slot */
    uint32_t best = 0;
    for (uint32_t i = 0; i < SSW_PUSH_SEGS; i++) {
        if (!n->push[i].active) {
            best = i;
            break;
        }
        if (n->push[i].seq < n->push[best].seq) best = i;
    }
    ssw_push_t *ps = &n->push[best];
    for (uint32_t f = 0; f < SSW_MAX_FR; f++)
        for (uint32_t r = 0; r < SSW_PIECES; r++) ps->tgt[f][r][0] = ps->tgt[f][r][1] = 0xFF;
    ps->seq = seq;
    ps->nfr = nfr;
    ps->pc = 0;
    ps->fr = 0;
    ps->copy = 0;
    ps->active = 1;
    for (uint32_t t = 0; t < nt; t++)
        for (uint32_t f = 0; f < nfr; f++) {
            ssw_notice_t *no = &out[t * nfr + f];
            no->peer = tg[t];
            no->fr = (uint8_t) f;
            no->seq = seq;
            zero(no->bm, SSW_BM);
        }
    uint32_t fan = n->cfg.push_fanout;
    if (fan > nt) fan = nt;
    for (uint32_t r = 0; r < n->cfg.n_push; r++)
        for (uint32_t f = 0; f < nfr; f++) {
            uint32_t prev = SSW_NONE;
            for (uint32_t c = 0; c < fan; c++) {
                /* smooth weighted round robin (no division) */
                uint32_t pick = SSW_NONE;
                for (uint32_t t = 0; t < nt; t++) {
                    ssw_peer_t *p = &n->peer[tg[t]];
                    p->wrr += wt[t];
                    if (t != prev && (pick == SSW_NONE || p->wrr > n->peer[tg[pick]].wrr)) pick = t;
                }
                n->peer[tg[pick]].wrr -= total;
                prev = pick;
                ps->tgt[f][r][c] = tg[pick];
                bset(out[pick * nfr + f].bm, r);
            }
        }
    *nout = nt * nfr;
    return SSW_OK;
}

bool ssw_seg_complete(const ssw_node_t *n, uint32_t seq)
{
    const ssw_oseg_t *s = n ? slot_cget(n, seq) : 0;
    return s && s->known && s->ndone == s->nfr;
}

static bool complete_cb(void *ctx, uint32_t seq)
{
    return ssw_seg_complete((const ssw_node_t *) ctx, seq);
}

bool ssw_pieces_held(const ssw_node_t *n, uint32_t seq, uint8_t fr, uint8_t bm[SSW_BM])
{
    const ssw_oseg_t *s = n ? slot_cget(n, seq) : 0;
    if (!s || fr >= SSW_MAX_FR || !bm) return false;
    copy(bm, s->fr[fr].have, SSW_BM);
    return true;
}

bool ssw_is_urgent(const ssw_node_t *n, uint32_t seq)
{
    if (!n) return false;
    return ssw_player_deadline(&n->player, seq, n->now) <= n->now + n->cfg.urgent_ticks;
}

/* ---- incoming ---- */

void ssw_on_notice(ssw_node_t *n, int pi, uint32_t seq, uint8_t fr, const uint8_t bm[SSW_BM])
{
    if (!n || pi < 0 || pi >= SSW_MAX_PEERS || !n->peer[pi].used || fr >= SSW_MAX_FR || !bm) return;
    ssw_oseg_t *s = slot_alloc(n, seq);
    if (!s) return;
    ssw_ofr_t *f = &s->fr[fr];
    uint32_t c = 0;
    for (uint32_t r = 0; r < SSW_PIECES; r++)
        if (bit(bm, r) && !bit(f->have, r) && !bit(f->asked, r)) {
            bset(f->asked, r);
            c++;
        }
    f->push_exp = (uint16_t) (f->push_exp + c);
    f->push_until = n->now + 2u * n->cfg.seg_ticks;
}

static uint32_t weight_of(const ssw_node_t *n, const ssw_peer_t *p);

int ssw_on_request(ssw_node_t *n, int pi, uint32_t seq, uint8_t fr, uint8_t pc, uint16_t tag,
                   bool urgent)
{
    if (!n || pi < 0 || pi >= SSW_MAX_PEERS || !n->peer[pi].used || fr >= SSW_MAX_FR ||
        pc >= SSW_PIECES || tag == SSW_TAG_PUSH)
        return SSW_ERR_ARG;
    const ssw_oseg_t *s = slot_get(n, seq);
    if (!s || !(s->fr[fr].done || bit(s->fr[fr].have, pc))) {
        n->st.rejects_tx++;
        return SSW_ERR_NOTFOUND;
    }
    ssw_peer_t *p = &n->peer[pi];
    ssw_req_t *q = urgent ? p->qu : p->qn;
    uint8_t *h = urgent ? &p->qu_h : &p->qn_h, *c = urgent ? &p->qu_n : &p->qn_n;
    /* Queue management: refuse what would wait > 250 ms (urgent: 500 ms)
     * behind everything already queued. */
    const uint64_t piece = (uint64_t) SSW_PIECE_ROWS * n->cfg.ticks_per_sec;
    const uint64_t lim = (uint64_t) n->cfg.up_rows_per_sec * (urgent ? 50u : 25u);
    bool full = *c >= SSW_PQ || (n->queued >= 4 && (uint64_t) n->queued * piece >= lim);
    if (!full && n->queued > (uint32_t) p->qu_n + p->qn_n &&
        (uint64_t) n->queued * piece * 2u >= lim) {
        /* No monopoly: once the queue is half full and others are waiting
         * too, a requester may hold at most its weighted share of the
         * budget (weight / sum of the weights of every queuing requester),
         * and at least 2. */
        const uint32_t own = (uint32_t) p->qu_n + p->qn_n;
        uint32_t wsum = own ? 0u : weight_of(n, p);
        for (uint32_t i = 0; i < SSW_MAX_PEERS; i++) {
            const ssw_peer_t *o = &n->peer[i];
            if (o->used && (o->qu_n || o->qn_n)) wsum += weight_of(n, o);
        }
        if (own >= 2 && (uint64_t) own * wsum * piece >= lim * weight_of(n, p)) full = true;
    }
    if (full) {
        n->st.rejects_tx++;
        return SSW_ERR_FULL;
    }
    ssw_req_t *e = &q[(uint32_t) (*h + *c) & (SSW_PQ - 1u)];
    e->peer = (uint8_t) pi;
    e->seq = seq;
    e->fr = fr;
    e->pc = pc;
    e->tag = tag;
    e->urgent = urgent ? 1 : 0;
    (*c)++;
    n->queued++;
    return SSW_OK;
}

unsigned ssw_on_piece(ssw_node_t *n, int pi, uint32_t seq, uint8_t fr, uint8_t pc, uint16_t tag)
{
    if (!n || pi < 0 || pi >= SSW_MAX_PEERS || !n->peer[pi].used || fr >= SSW_MAX_FR ||
        pc >= SSW_PIECES)
        return SSW_PC_IGNORED;
    ssw_peer_t *p = &n->peer[pi];
    if (tag != SSW_TAG_PUSH) {
        if (tag < SSW_INFLIGHT) {
            ssw_infl_t *e = &n->infl[tag];
            if (e->used && e->peer == pi && e->seq == seq && e->fr == fr && e->pc == pc) {
                uint32_t rtt = n->now - e->sent;
                p->srtt8 = p->srtt8 + rtt - (p->srtt8 >> 3);
                if (rtt < p->minrtt) p->minrtt = rtt;
                infl_free(n, tag, false);
            }
        }
    } else {
        n->st.rx_push++;
    }
    p->got_tick++;
    p->recv_recent++;
    p->recv_total++;
    ssw_oseg_t *s = slot_alloc(n, seq);
    if (!s) {
        n->st.rx_ignored++;
        return SSW_PC_IGNORED;
    }
    ssw_ofr_t *f = &s->fr[fr];
    if (tag == SSW_TAG_PUSH && f->push_exp) f->push_exp--;
    if (bit(f->have, pc)) {
        n->st.rx_dup++;
        return SSW_PC_DUP;
    }
    bset(f->have, pc);
    bset(f->asked, pc);
    f->nhave++;
    mark_dirty(n, seq, f);
    if (f->done) {
        n->st.rx_late++;
        return SSW_PC_NEW | SSW_PC_LATE;
    }
    n->st.rx_new++;
    check_done(n, s, f);
    return f->done ? (SSW_PC_NEW | SSW_PC_COMPLETE) : SSW_PC_NEW;
}

void ssw_on_reject(ssw_node_t *n, int pi, uint16_t tag, bool busy)
{
    if (!n || pi < 0 || pi >= SSW_MAX_PEERS || tag >= SSW_INFLIGHT) return;
    ssw_infl_t *e = &n->infl[tag];
    if (!e->used || e->peer != pi) return;
    infl_free(n, tag, true);
    if (busy) n->peer[pi].reject_until = n->now + 20;
    n->st.rejects_rx++;
}

/* ---- clock ---- */

void ssw_tick(ssw_node_t *n, uint32_t now)
{
    if (!n) return;
    n->now = now;
    const ssw_config_t *c = &n->cfg;
    const int32_t tps = (int32_t) c->ticks_per_sec;
    const int32_t pcost = tps * (int32_t) SSW_PIECE_ROWS;
    int32_t ucap = 2 * pcost + (int32_t) c->up_rows_per_sec;
    n->up_credit += (int32_t) c->up_rows_per_sec;
    if (n->up_credit > ucap) n->up_credit = ucap;
    int32_t dcap = 2 * pcost + 4 * (int32_t) c->dn_rows_per_sec;
    n->dn_credit += (int32_t) c->dn_rows_per_sec;
    if (n->dn_credit > dcap) n->dn_credit = dcap;

    bool second = false, epoch = false;
    if (++n->sec_ctr >= c->ticks_per_sec) {
        n->sec_ctr = 0;
        second = true;
        if (++n->epoch_ctr >= 5) {
            n->epoch_ctr = 0;
            epoch = true;
        }
    }
    for (uint32_t i = 0; i < SSW_MAX_PEERS; i++) {
        ssw_peer_t *p = &n->peer[i];
        if (!p->used) continue;
        p->rate = p->rate - (p->rate >> 4) + (p->got_tick << 4);
        p->got_tick = 0;
        if (second) {
            p->recv_recent >>= 1;
            p->sent_recent >>= 1;
        }
        if (epoch) {
            p->minrtt_prev = p->minrtt;
            p->minrtt = MINRTT_INF;
        }
        uint32_t mr = p->minrtt < p->minrtt_prev ? p->minrtt : p->minrtt_prev;
        if (mr == MINRTT_INF) mr = p->srtt8 >> 3;
        if (mr > 1000) mr = 1000;
        uint32_t cap = ((p->rate * mr) >> 7) + 1; /* 2 x BDP + 1 piece; AQM bounds queues */
        if (cap > SSW_PQ) cap = SSW_PQ;
        p->infl_cap = (uint16_t) cap;
    }
    if ((now & 3u) == 0) {
        for (uint32_t t = 0; t < SSW_INFLIGHT; t++) {
            ssw_infl_t *e = &n->infl[t];
            if (!e->used) continue;
            uint32_t to = 3u * (n->peer[e->peer].srtt8 >> 3) + 20u;
            if (now - e->sent > to) {
                infl_free(n, (uint16_t) t, false); /* id stays asked: abandoned */
                n->st.timeouts++;
            }
        }
    }
    if (!c->is_origin) {
        if (c->mode == SSW_MODE_LIVE && n->newest != SSW_NONE) {
            /* Start live_back behind the newest; a live viewer that has not
             * started yet and fell 3+ segments behind re-anchors to the
             * edge (a live player never waits for history). */
            const uint32_t edge = n->newest >= c->live_back ? n->newest - c->live_back : 0;
            if (!n->player.start_known || (!n->player.started && edge > n->player.start_seq + 2u))
                ssw_player_set_start(&n->player, edge);
        }
        ssw_player_step(&n->player, now, complete_cb, n);
    }
}

/* ---- pull scheduling ---- */

typedef struct {
    uint32_t seq;
    uint32_t copies; /* rarity: fewer copies first */
    uint8_t fr, urgent, rescue;
} cand_t;

static bool peer_usable(const ssw_node_t *n, const ssw_peer_t *p)
{
    return p->used && p->infl < p->infl_cap && (int32_t) (p->reject_until - n->now) <= 0;
}

static bool issue(ssw_node_t *n, uint32_t pi, uint32_t seq, ssw_ofr_t *f, uint8_t fr, uint32_t pc,
                  bool urgent, ssw_req_t *out, uint32_t *nout)
{
    const int32_t pcost = (int32_t) (n->cfg.ticks_per_sec * SSW_PIECE_ROWS);
    if (n->nfree == 0 || n->dn_credit < pcost) return false;
    uint16_t tag = n->freelist[--n->nfree];
    ssw_infl_t *e = &n->infl[tag];
    e->used = 1;
    e->seq = seq;
    e->sent = n->now;
    e->peer = (uint8_t) pi;
    e->fr = fr;
    e->pc = (uint8_t) pc;
    bset(f->asked, pc);
    f->ninfl++;
    n->peer[pi].infl++;
    n->dn_credit -= pcost;
    n->st.req_sent++;
    ssw_req_t *r = &out[(*nout)++];
    r->peer = (uint8_t) pi;
    r->seq = seq;
    r->fr = fr;
    r->pc = (uint8_t) pc;
    r->tag = tag;
    r->urgent = urgent ? 1 : 0;
    return true;
}

static bool has_origin(const ssw_node_t *n)
{
    for (uint32_t i = 0; i < SSW_MAX_PEERS; i++)
        if (n->peer[i].used && n->peer[i].origin) return true;
    return false;
}

/* Ask peer pi for up to `want` ids of (seq, fr). Returns ids asked. */
static uint32_t ask_peer(ssw_node_t *n, uint32_t pi, ssw_oseg_t *s, uint8_t fr, uint32_t want,
                         bool urgent, bool push_pref, ssw_req_t *out, uint32_t *nout, uint32_t cap)
{
    ssw_peer_t *p = &n->peer[pi];
    const ssw_vseg_t *v = &p->v[s->seq & SLOT_MASK];
    if (v->seq != s->seq) return 0;
    const ssw_vfr_t *vf = &v->fr[fr];
    if (vf->st == SSW_ST_NONE) return 0;
    ssw_ofr_t *f = &s->fr[fr];
    uint32_t room = (uint32_t) (p->infl_cap - p->infl);
    if (want > room) want = room;
    if (want > cap - *nout) want = cap - *nout;
    uint32_t got = 0;
    if (vf->st == SSW_ST_COMPLETE) {
        /* Any id it lacks: systematic ids first (cheap to decode), or, while
         * the origin may still push to us, ids past the push range. */
        uint32_t start = push_pref ? n->cfg.n_push : 0;
        for (uint32_t i = 0; i < SSW_PIECES && got < want; i++) {
            uint32_t r = (start + i) & (SSW_PIECES - 1u);
            if (bit(f->asked, r) || bit(f->have, r)) continue;
            if (!issue(n, pi, s->seq, f, fr, r, urgent, out, nout)) return got;
            got++;
        }
        return got;
    }
    /* Partial holder: its ids we lack, rarest first. */
    uint8_t cand[SSW_PIECES];
    uint32_t nc = 0, minc = 255;
    for (uint32_t b = 0; b < SSW_BM; b++) {
        uint8_t x = (uint8_t) (vf->bm[b] & ~f->have[b] & ~f->asked[b]);
        for (uint32_t i = 0; x; i++, x >>= 1)
            if (x & 1u) {
                uint32_t r = b * 8 + i;
                cand[nc++] = (uint8_t) r;
                if (f->cnt[r] < minc) minc = f->cnt[r];
            }
    }
    if (nc <= want) {
        for (uint32_t i = 0; i < nc; i++) {
            if (!issue(n, pi, s->seq, f, fr, cand[i], urgent, out, nout)) return got;
            got++;
        }
        return got;
    }
    for (uint32_t t = minc; t <= 255 && got < want; t++) {
        for (uint32_t i = 0; i < nc && got < want; i++) {
            if (f->cnt[cand[i]] != t || bit(f->asked, cand[i])) continue;
            if (!issue(n, pi, s->seq, f, fr, cand[i], urgent, out, nout)) return got;
            got++;
        }
        if (t == 255) break;
    }
    return got;
}

static int32_t need_of(const ssw_node_t *n, const ssw_oseg_t *s, const ssw_ofr_t *f, bool urgent)
{
    int32_t target = (int32_t) SSW_KP(s->k) + (urgent ? (int32_t) n->cfg.urgent_extra : 0);
    int32_t pend = (int32_t) f->ninfl;
    if ((int32_t) (f->push_until - n->now) > 0) pend += (int32_t) f->push_exp;
    return target - (int32_t) f->nhave - pend;
}

uint32_t ssw_schedule(ssw_node_t *n, ssw_req_t *out, uint32_t cap)
{
    if (!n || !out || n->cfg.is_origin || !n->player.start_known) return 0;
    const int32_t pcost = (int32_t) (n->cfg.ticks_per_sec * SSW_PIECE_ROWS);
    if (n->newest == SSW_NONE || n->dn_credit < pcost) return 0;
    const ssw_player_t *pl = &n->player;
    uint32_t lo = pl->started ? pl->play_seq + 1 : pl->start_seq;
    uint32_t hi = n->newest;
    if (n->cfg.mode == SSW_MODE_VOD && hi > lo && hi - lo > n->cfg.prefetch_segs)
        hi = lo + n->cfg.prefetch_segs;
    if (lo < n->base) lo = n->base;
    if (hi < lo) return 0;
    if (hi - n->base >= SSW_WIN) hi = n->base + SSW_WIN - 1u;

    cand_t cand[SSW_WIN * SSW_MAX_FR];
    uint32_t nc = 0;
    for (uint32_t q = lo; q <= hi; q++) {
        ssw_oseg_t *s = slot_get(n, q);
        if (!s || !s->known) continue;
        uint32_t dl = ssw_player_deadline(pl, q, n->now);
        bool urg = dl <= n->now + n->cfg.urgent_ticks;
        bool res = dl <= n->now + n->cfg.rescue_ticks;
        for (uint32_t f = 0; f < s->nfr; f++) {
            if (s->fr[f].done || need_of(n, s, &s->fr[f], urg) <= 0) continue;
            cand_t *c = &cand[nc++];
            c->seq = q;
            c->fr = (uint8_t) f;
            c->urgent = urg;
            c->rescue = res;
            c->copies = (uint32_t) s->fr[f].ncomp * SSW_KP(s->k) + s->fr[f].tot;
        }
        if (q == 0xFFFFFFFFu) break;
    }
    if (!nc) return 0;

    /* best neighbours first: measured rate plus a prior from the declared
     * upload, so a fresh neighbour is tried and a weak one is not swamped */
    uint8_t fast[SSW_MAX_PEERS];
    uint32_t score[SSW_MAX_PEERS];
    uint32_t np = 0;
    for (uint32_t i = 0; i < SSW_MAX_PEERS; i++) {
        const ssw_peer_t *p = &n->peer[i];
        if (!p->used) continue;
        uint32_t du = p->declared_up == SSW_NONE ? 256u : p->declared_up;
        if (du > 0xFFFFFu) du = 0xFFFFFu;
        score[i] = p->rate + (du >> 3);
        uint32_t j = np++;
        while (j > 0 && score[fast[j - 1]] < score[i]) {
            fast[j] = fast[j - 1];
            j--;
        }
        fast[j] = (uint8_t) i;
    }
    const bool orig = has_origin(n);
    uint32_t nout = 0;

    /* Pass 1: urgent, earliest deadline first (cand is in seq order). */
    for (uint32_t ci = 0; ci < nc && nout < cap; ci++) {
        const cand_t *c = &cand[ci];
        if (!c->urgent) continue;
        ssw_oseg_t *s = slot_get(n, c->seq);
        ssw_ofr_t *f = &s->fr[c->fr];
        bool pp = orig && n->now - s->learned < 2u * n->cfg.seg_ticks;
        int32_t need = need_of(n, s, f, true);
        bool supplier = false;
        for (uint32_t k = 0; k < np && need > 0 && nout < cap; k++) {
            uint32_t pi = fast[k];
            if (n->peer[pi].origin) continue;
            const ssw_vseg_t *v = &n->peer[pi].v[c->seq & SLOT_MASK];
            if (v->seq == c->seq && v->fr[c->fr].st != SSW_ST_NONE) supplier = true;
            if (!peer_usable(n, &n->peer[pi])) continue;
            need -= (int32_t) ask_peer(n, pi, s, c->fr, (uint32_t) need, true, pp, out, &nout, cap);
        }
        /* The origin is the last resort, and only for a real deadline: a
         * viewer still filling its startup buffer has nothing playing, and
         * a flash crowd of joiners must not drain the origin's uplink (it
         * waits for the swarm instead, unless nobody near has had the
         * segment for two segment durations). */
        const bool playing = n->player.started;
        if (need > 0 &&
            ((c->rescue && playing) ||
             (!supplier && (playing || n->now - s->learned >= 2u * n->cfg.seg_ticks)))) {
            for (uint32_t k = 0; k < np && need > 0 && nout < cap; k++) {
                uint32_t pi = fast[k];
                if (!n->peer[pi].origin || !peer_usable(n, &n->peer[pi])) continue;
                need -= (int32_t) ask_peer(n, pi, s, c->fr, (uint32_t) need, true, false, out,
                                           &nout, cap);
            }
        }
        if (n->dn_credit < pcost) return nout;
    }

    /* Pass 2: the rest, rarest first (fewest copies, then seq). */
    uint16_t ord[SSW_WIN * SSW_MAX_FR];
    uint32_t no = 0;
    for (uint32_t ci = 0; ci < nc; ci++) {
        if (cand[ci].urgent) continue;
        uint32_t j = no++;
        while (j > 0 && cand[ord[j - 1]].copies > cand[ci].copies) {
            ord[j] = ord[j - 1];
            j--;
        }
        ord[j] = (uint16_t) ci;
    }
    for (uint32_t oi = 0; oi < no && nout < cap; oi++) {
        const cand_t *c = &cand[ord[oi]];
        ssw_oseg_t *s = slot_get(n, c->seq);
        ssw_ofr_t *f = &s->fr[c->fr];
        bool pp = orig && n->now - s->learned < 2u * n->cfg.seg_ticks;
        int32_t need = need_of(n, s, f, false);
        bool any_room = false;
        for (uint32_t k = 0; k < np && need > 0 && nout < cap; k++) {
            uint32_t pi = fast[k];
            ssw_peer_t *p = &n->peer[pi];
            if (p->origin || !peer_usable(n, p)) continue;
            any_room = true;
            need -=
                (int32_t) ask_peer(n, pi, s, c->fr, (uint32_t) need, false, pp, out, &nout, cap);
        }
        if (!any_room || n->dn_credit < pcost) break;
    }
    return nout;
}

/* ---- uploads ---- */

static uint32_t weight_of(const ssw_node_t *n, const ssw_peer_t *p)
{
    uint32_t w = 1;
    if (p->recv_recent >= 16) w++;
    if (p->recv_recent >= 64) w++;
    if (p->recv_recent >= 256) w++;
    if (n->cfg.weight_hook) w = n->cfg.weight_hook(n->cfg.hook_ctx, p->id, w);
    if (w < 1) w = 1;
    if (w > SSW_WEIGHT_MAX) w = SSW_WEIGHT_MAX;
    return w;
}

static bool can_serve(ssw_node_t *n, uint32_t seq, uint8_t fr, uint8_t pc)
{
    const ssw_oseg_t *s = slot_get(n, seq);
    return s && (s->fr[fr].done || bit(s->fr[fr].have, pc));
}

static void sent_row(ssw_node_t *n, ssw_peer_t *p)
{
    n->up_credit -= (int32_t) (n->cfg.ticks_per_sec * SSW_PIECE_ROWS);
    n->st.up_rows += SSW_PIECE_ROWS;
    p->sent_recent++;
    p->sent_total++;
    if (n->cfg.tithe_hook) n->cfg.tithe_hook(n->cfg.hook_ctx, n->self_id, p->id, SSW_PIECE_ROWS);
}

static void serve_class(ssw_node_t *n, uint32_t cls, ssw_send_t *out, uint32_t cap, uint32_t *nout)
{
    const int32_t tps = (int32_t) (n->cfg.ticks_per_sec * SSW_PIECE_ROWS); /* piece cost */
    uint32_t rr = n->rr[cls];
    if (rr >= SSW_MAX_PEERS) rr = 0;
    for (;;) {
        bool progressed = false;
        for (uint32_t k = 0; k < SSW_MAX_PEERS; k++) {
            uint32_t pi = rr + k;
            if (pi >= SSW_MAX_PEERS) pi -= SSW_MAX_PEERS;
            ssw_peer_t *p = &n->peer[pi];
            ssw_req_t *q = cls ? p->qn : p->qu;
            uint8_t *h = cls ? &p->qn_h : &p->qu_h, *c = cls ? &p->qn_n : &p->qu_n;
            if (!p->used || *c == 0) continue;
            uint32_t w = weight_of(n, p);
            if (k == 0 && n->rr_keep[cls]) {
                n->rr_keep[cls] = 0; /* resume a quantum cut short by the credit */
            } else {
                p->deficit[cls] += w;
                if (p->deficit[cls] > 2u * w) p->deficit[cls] = 2u * w;
            }
            while (p->deficit[cls] >= 1 && *c && *nout < cap && n->up_credit >= tps) {
                ssw_req_t *e = &q[*h];
                *h = (uint8_t) ((*h + 1u) & (SSW_PQ - 1u));
                (*c)--;
                if (n->queued) n->queued--;
                ssw_send_t *o = &out[(*nout)++];
                o->peer = (uint8_t) pi;
                o->seq = e->seq;
                o->fr = e->fr;
                o->pc = e->pc;
                o->tag = e->tag;
                if (can_serve(n, e->seq, e->fr, e->pc)) {
                    o->kind = SSW_SEND_DATA;
                    sent_row(n, p);
                    p->deficit[cls]--;
                } else {
                    o->kind = SSW_SEND_REJECT;
                    n->st.rejects_tx++;
                }
                progressed = true;
            }
            if (*c == 0) p->deficit[cls] = 0;
            if (*nout >= cap || n->up_credit < tps) {
                /* out of credit mid-quantum: stay on this peer next time */
                if (*c && p->deficit[cls] >= 1) {
                    n->rr[cls] = (uint8_t) pi;
                    n->rr_keep[cls] = 1;
                } else {
                    n->rr[cls] = (uint8_t) (pi + 1 >= SSW_MAX_PEERS ? 0 : pi + 1);
                    n->rr_keep[cls] = 0;
                }
                return;
            }
        }
        if (!progressed) break;
    }
    n->rr[cls] = (uint8_t) (rr + 1 >= SSW_MAX_PEERS ? 0 : rr + 1);
    n->rr_keep[cls] = 0;
}

static void serve_push(ssw_node_t *n, ssw_send_t *out, uint32_t cap, uint32_t *nout)
{
    const int32_t tps = (int32_t) (n->cfg.ticks_per_sec * SSW_PIECE_ROWS); /* piece cost */
    for (;;) {
        ssw_push_t *ps = 0;
        for (uint32_t i = 0; i < SSW_PUSH_SEGS; i++)
            if (n->push[i].active && (!ps || n->push[i].seq < ps->seq)) ps = &n->push[i];
        if (!ps) return;
        while (*nout < cap && n->up_credit >= tps) {
            if (ps->pc >= n->cfg.n_push) {
                ps->active = 0;
                break;
            }
            uint8_t t = ps->tgt[ps->fr][ps->pc][ps->copy];
            const uint8_t fr = ps->fr, pc = (uint8_t) ps->pc;
            if (++ps->copy >= n->cfg.push_fanout || ps->copy >= 2) {
                ps->copy = 0;
                if (++ps->fr >= ps->nfr) {
                    ps->fr = 0;
                    ps->pc++;
                }
            }
            if (t == 0xFF || !n->peer[t].used) continue;
            ssw_send_t *o = &out[(*nout)++];
            o->peer = t;
            o->kind = SSW_SEND_PUSH;
            o->seq = ps->seq;
            o->fr = fr;
            o->pc = pc;
            o->tag = SSW_TAG_PUSH;
            sent_row(n, &n->peer[t]);
            n->st.up_push++;
        }
        if (ps->active) return; /* out of credit or space */
    }
}

uint32_t ssw_upload(ssw_node_t *n, ssw_send_t *out, uint32_t cap)
{
    if (!n || !out) return 0;
    uint32_t nout = 0;
    /* The origin pushes first: the push is the only way new content enters
     * the swarm, and rescue pulls must never starve it. */
    if (n->cfg.is_origin) serve_push(n, out, cap, &nout);
    serve_class(n, 0, out, cap, &nout);
    serve_class(n, 1, out, cap, &nout);
    return nout;
}

/* ---- adverts ---- */

static int advert_range(const ssw_node_t *n, uint8_t *out, uint32_t cap, uint32_t lo, uint32_t nseg,
                        uint8_t flags)
{
    const uint32_t ne = nseg * SSW_MAX_FR, sb = (ne + 3u) >> 2;
    out[0] = 'Z';
    out[1] = 'X';
    out[2] = 'S';
    out[3] = 'A';
    out[4] = 1;
    out[5] = (uint8_t) ((n->cfg.is_origin ? SSW_ADV_ORIGIN : 0) | flags);
    out[6] = (uint8_t) nseg;
    out[7] = SSW_MAX_FR;
    put64(out + 8, n->stream_id);
    put32(out + 16, n->self_id);
    put32(out + 20, lo);
    put32(out + 24, n->newest);
    put32(out + 28, n->cfg.up_rows_per_sec);
    if (SSW_ADV_HDR + sb > cap) return SSW_ERR_SPACE;
    uint8_t *st = out + SSW_ADV_HDR;
    zero(st, sb);
    uint32_t p = SSW_ADV_HDR + sb;
    for (uint32_t e = 0, si = 0; si < nseg; si++)
        for (uint32_t fi = 0; fi < SSW_MAX_FR; fi++, e++) {
            const ssw_oseg_t *s = slot_cget(n, lo + si);
            const ssw_ofr_t *f = s ? &s->fr[fi] : 0;
            uint32_t v = SSW_ST_NONE;
            if (f && f->done)
                v = SSW_ST_COMPLETE;
            else if (f && f->nhave)
                v = f->nhave >= SSW_PIECES ? SSW_ST_COMPLETE : SSW_ST_PARTIAL;
            st[e >> 2] |= (uint8_t) (v << (2 * (e & 3)));
            if (v == SSW_ST_PARTIAL) {
                if (p + SSW_BM > cap) return SSW_ERR_SPACE;
                copy(out + p, f->have, SSW_BM);
                p += SSW_BM;
            }
        }
    return (int) p;
}

int ssw_advert_build(const ssw_node_t *n, uint8_t *out, uint32_t cap)
{
    if (!n || !out || cap < SSW_ADV_HDR) return SSW_ERR_ARG;
    uint32_t lo = SSW_NONE, hi = 0;
    for (uint32_t i = 0; n->any && i < SSW_WIN; i++) {
        const ssw_oseg_t *s = slot_cget(n, n->base + i);
        if (!s) continue;
        bool info = false;
        for (uint32_t f = 0; f < SSW_MAX_FR; f++) info |= s->fr[f].nhave || s->fr[f].done;
        if (!info) continue;
        if (lo == SSW_NONE) lo = n->base + i;
        hi = n->base + i;
    }
    if (lo == SSW_NONE) return advert_range(n, out, cap, n->base, 0, 0);
    return advert_range(n, out, cap, lo, hi - lo + 1, 0);
}

static uint32_t fr_state(const ssw_ofr_t *f)
{
    if (f->done) return SSW_ST_COMPLETE;
    if (f->nhave) return f->nhave >= SSW_PIECES ? SSW_ST_COMPLETE : SSW_ST_PARTIAL;
    return SSW_ST_NONE;
}

/* After an advert: forget what was reported, keep the dirty range over
 * what was held back. */
static void settle_dirty(ssw_node_t *n)
{
    n->dirty_lo = 1;
    n->dirty_hi = 0;
    for (uint32_t i = 0; n->any && i < SSW_WIN; i++) {
        ssw_oseg_t *s = slot_get(n, n->base + i);
        if (!s) continue;
        for (uint32_t f = 0; f < SSW_MAX_FR; f++)
            if (s->fr[f].dirty) mark_dirty(n, n->base + i, &s->fr[f]);
    }
}

/* Delta: header (nseg = entry count), then per changed freight
 * [offset from base u8][fr | state << 4], plus its bitmap when partial. */
int ssw_advert_next(ssw_node_t *n, uint8_t *out, uint32_t cap, bool full)
{
    if (!n || !out || cap < SSW_ADV_HDR) return SSW_ERR_ARG;
    if (!full) {
        uint32_t lo = n->dirty_lo, hi = n->dirty_hi;
        if (lo > hi || !n->any) return 0;
        if (lo < n->base) lo = n->base;
        if (hi - n->base >= SSW_WIN) hi = n->base + SSW_WIN - 1u;
        uint32_t cnt = 0, p = SSW_ADV_HDR;
        for (uint32_t q = lo; q <= hi && lo <= hi && !full; q++) {
            ssw_oseg_t *s = slot_get(n, q);
            if (!s) continue;
            for (uint32_t f = 0; f < SSW_MAX_FR; f++) {
                ssw_ofr_t *fr = &s->fr[f];
                if (!fr->dirty) continue;
                const uint32_t v = fr_state(fr);
                if (cnt >= SSW_ADV_DELTA_MAX || p + 2u + SSW_BM > cap) {
                    full = true; /* too many changes: send everything */
                    break;
                }
                fr->dirty = 0;
                out[p++] = (uint8_t) (q - lo);
                out[p++] = (uint8_t) (f | (v << 4));
                if (v == SSW_ST_PARTIAL) {
                    copy(out + p, fr->have, SSW_BM);
                    p += SSW_BM;
                }
                cnt++;
            }
        }
        if (!full) {
            settle_dirty(n);
            if (cnt == 0) return 0;
            /* compact header: the delta is bound to the session that
             * carried the last full advert, so stream, sender, newest and
             * declared upload are not repeated; the entries move up */
            out[0] = 'Z';
            out[1] = 'X';
            out[2] = 'S';
            out[3] = 'A';
            out[4] = 1;
            out[5] = (uint8_t) ((n->cfg.is_origin ? SSW_ADV_ORIGIN : 0) | SSW_ADV_DELTA);
            out[6] = (uint8_t) cnt;
            out[7] = SSW_MAX_FR;
            put32(out + 8, lo);
            const uint32_t body = p - SSW_ADV_HDR;
            for (uint32_t i = 0; i < body; i++) out[SSW_ADV_DELTA_HDR + i] = out[SSW_ADV_HDR + i];
            return (int) (SSW_ADV_DELTA_HDR + body);
        }
    }
    int rc = ssw_advert_build(n, out, cap);
    if (rc >= 0) {
        for (uint32_t i = 0; i < SSW_WIN; i++)
            for (uint32_t f = 0; f < SSW_MAX_FR; f++) n->seg[i].fr[f].dirty = 0;
        n->dirty_lo = 1;
        n->dirty_hi = 0;
    }
    return rc;
}

int ssw_advert_parse(const uint8_t *in, uint32_t len, ssw_advert_t *a)
{
    if (!in || !a) return SSW_ERR_ARG;
    if (len < SSW_ADV_DELTA_HDR || !magic(in, 'A') || in[4] != 1) return SSW_ERR_FORMAT;
    if (!(in[5] & SSW_ADV_DELTA) && len < SSW_ADV_HDR) return SSW_ERR_FORMAT;
    const uint32_t flags = in[5], nseg = in[6], nfr = in[7];
    if ((flags & ~(SSW_ADV_ORIGIN | SSW_ADV_DELTA)) || nseg > SSW_WIN || nfr < 1 ||
        nfr > SSW_MAX_FR)
        return SSW_ERR_FORMAT;
    const uint32_t base = (flags & SSW_ADV_DELTA) ? get32(in + 8) : get32(in + 20);
    if (flags & SSW_ADV_DELTA) {
        /* nseg = entry count; entries strictly increasing (offset, fr) */
        if (nseg < 1 || nfr != SSW_MAX_FR || base > 0xFFFFFFFFu - (SSW_WIN - 1u))
            return SSW_ERR_FORMAT;
        zero(a->present, sizeof a->present);
        zero(a->state, sizeof a->state);
        uint32_t p = SSW_ADV_DELTA_HDR, last = 0, maxoff = 0;
        for (uint32_t i = 0; i < nseg; i++) {
            if (p + 2 > len) return SSW_ERR_FORMAT;
            const uint32_t off = in[p], f = in[p + 1] & 0x0Fu, v = in[p + 1] >> 4;
            const uint32_t e = off * SSW_MAX_FR + f;
            if (off >= SSW_WIN || f >= SSW_MAX_FR || v > 2 || (i && e <= last))
                return SSW_ERR_FORMAT;
            p += 2;
            a->state[e] = (uint8_t) v;
            a->present[e] = 1;
            if (v == SSW_ST_PARTIAL) {
                if (p + SSW_BM > len) return SSW_ERR_FORMAT;
                uint8_t any = 0, all = 0xFF;
                for (uint32_t b = 0; b < SSW_BM; b++) {
                    any |= in[p + b];
                    all &= in[p + b];
                }
                if (!any || all == 0xFF) return SSW_ERR_FORMAT;
                copy(a->bm[e], in + p, SSW_BM);
                p += SSW_BM;
            }
            last = e;
            if (off + 1 > maxoff) maxoff = off + 1;
        }
        if (p != len) return SSW_ERR_FORMAT;
        a->flags = (uint8_t) flags;
        a->nseg = (uint8_t) maxoff;
        a->nfr = (uint8_t) nfr;
        a->stream_id = 0;
        a->sender = 0;
        a->base = base;
        a->newest = SSW_NONE;
        a->up_rows_per_sec = SSW_NONE;
        return SSW_OK;
    }
    if (nseg && base > 0xFFFFFFFFu - (nseg - 1)) return SSW_ERR_FORMAT;
    const uint32_t ne = nseg * nfr, sb = (ne + 3u) >> 2;
    if (len < SSW_ADV_HDR + sb) return SSW_ERR_FORMAT;
    const uint8_t *st = in + SSW_ADV_HDR;
    uint32_t np = 0;
    for (uint32_t e = 0; e < sb * 4; e++) {
        uint32_t v = (st[e >> 2] >> (2 * (e & 3))) & 3u;
        if (e >= ne) {
            if (v) return SSW_ERR_FORMAT;
            continue;
        }
        if (v == 3) return SSW_ERR_FORMAT;
        if (v == SSW_ST_PARTIAL) np++;
    }
    if (len != SSW_ADV_HDR + sb + np * SSW_BM) return SSW_ERR_FORMAT;
    const uint8_t *bm = in + SSW_ADV_HDR + sb;
    for (uint32_t e = 0; e < ne; e++) {
        uint32_t v = (st[e >> 2] >> (2 * (e & 3))) & 3u;
        a->state[e] = (uint8_t) v;
        if (v != SSW_ST_PARTIAL) continue;
        uint8_t any = 0, all = 0xFF;
        for (uint32_t b = 0; b < SSW_BM; b++) {
            any |= bm[b];
            all &= bm[b];
        }
        if (!any || all == 0xFF) return SSW_ERR_FORMAT;
        copy(a->bm[e], bm, SSW_BM);
        bm += SSW_BM;
    }
    a->flags = (uint8_t) flags;
    a->nseg = (uint8_t) nseg;
    a->nfr = (uint8_t) nfr;
    a->stream_id = get64(in + 8);
    a->sender = get32(in + 16);
    a->base = base;
    a->newest = get32(in + 24);
    a->up_rows_per_sec = get32(in + 28);
    return SSW_OK;
}

int ssw_advert_apply(ssw_node_t *n, int pi, const ssw_advert_t *a)
{
    if (!n || !a || pi < 0 || pi >= SSW_MAX_PEERS || !n->peer[pi].used) return SSW_ERR_ARG;
    ssw_peer_t *p = &n->peer[pi];
    if (!(a->flags & SSW_ADV_DELTA)) {
        if (a->stream_id != n->stream_id || a->sender != p->id) return SSW_ERR_ARG;
        p->declared_up = a->up_rows_per_sec;
    } else if (!p->full_adv) {
        return SSW_ERR_ARG; /* a delta needs a full advert first */
    }
    if (!(a->flags & SSW_ADV_DELTA)) p->full_adv = 1;
    if (!n->any) return SSW_OK;
    static const uint8_t none[SSW_BM] = {0};
    for (uint32_t i = 0; i < SSW_WIN; i++) {
        const uint32_t seq = n->base + i;
        const bool inside = a->nseg && seq >= a->base && seq - a->base < a->nseg;
        ssw_oseg_t *o;
        if (inside) {
            const uint8_t *pc = &a->state[(seq - a->base) * a->nfr];
            bool info = false;
            for (uint32_t f = 0; f < a->nfr; f++) info |= pc[f] != SSW_ST_NONE;
            o = info ? slot_alloc(n, seq) : slot_get(n, seq);
        } else {
            if (a->flags & SSW_ADV_DELTA) continue; /* unchanged */
            o = slot_get(n, seq);
        }
        if (!o) continue;
        ssw_vseg_t *v = &p->v[seq & SLOT_MASK];
        if (v->seq != seq) {
            v->seq = seq;
            for (uint32_t f = 0; f < SSW_MAX_FR; f++) {
                v->fr[f].st = SSW_ST_NONE;
                zero(v->fr[f].bm, SSW_BM);
            }
        }
        for (uint32_t f = 0; f < SSW_MAX_FR; f++) {
            uint8_t nst = SSW_ST_NONE;
            const uint8_t *nbm = none;
            if (inside && f < a->nfr) {
                uint32_t e = (seq - a->base) * a->nfr + f;
                nst = a->state[e];
                if (nst == SSW_ST_PARTIAL) nbm = a->bm[e];
            }
            if ((a->flags & SSW_ADV_DELTA) &&
                !(inside && f < a->nfr && a->present[(seq - a->base) * a->nfr + f]))
                continue; /* not listed: unchanged */
            if (nst == v->fr[f].st && nst != SSW_ST_PARTIAL) continue;
            view_set(&o->fr[f], &v->fr[f], nst, nbm);
        }
    }
    return SSW_OK;
}

/* ---- request / notice / rows codecs ---- */

int ssw_req_encode(uint64_t stream_id, const ssw_req_t *r, uint32_t count, uint8_t *out,
                   uint32_t cap)
{
    if (!r || !out || count < 1 || count > SSW_REQ_MAX) return SSW_ERR_ARG;
    const uint32_t len = SSW_REQ_HDR + count * SSW_REQ_ENTRY;
    if (len > cap) return SSW_ERR_SPACE;
    out[0] = 'Z';
    out[1] = 'X';
    out[2] = 'S';
    out[3] = 'Q';
    out[4] = 1;
    out[5] = (uint8_t) count;
    put64(out + 6, stream_id);
    for (uint32_t i = 0; i < count; i++) {
        uint8_t *e = out + SSW_REQ_HDR + i * SSW_REQ_ENTRY;
        if (r[i].fr >= SSW_MAX_FR || r[i].pc >= SSW_PIECES || r[i].tag == SSW_TAG_PUSH)
            return SSW_ERR_ARG;
        put32(e, r[i].seq);
        e[4] = r[i].fr;
        e[5] = r[i].pc;
        put16(e + 6, r[i].tag);
        e[8] = r[i].urgent ? 1 : 0;
    }
    return (int) len;
}

int ssw_req_parse(const uint8_t *in, uint32_t len, uint64_t *stream_id, ssw_req_t *r, uint32_t cap,
                  uint32_t *count)
{
    if (!in || !stream_id || !r || !count) return SSW_ERR_ARG;
    if (len < SSW_REQ_HDR || !magic(in, 'Q') || in[4] != 1) return SSW_ERR_FORMAT;
    const uint32_t c = in[5];
    if (c < 1 || c > SSW_REQ_MAX || len != SSW_REQ_HDR + c * SSW_REQ_ENTRY) return SSW_ERR_FORMAT;
    if (c > cap) return SSW_ERR_SPACE;
    for (uint32_t i = 0; i < c; i++) {
        const uint8_t *e = in + SSW_REQ_HDR + i * SSW_REQ_ENTRY;
        if (e[4] >= SSW_MAX_FR || e[5] >= SSW_PIECES || get16(e + 6) == SSW_TAG_PUSH ||
            (e[8] & ~1u))
            return SSW_ERR_FORMAT;
    }
    for (uint32_t i = 0; i < c; i++) {
        const uint8_t *e = in + SSW_REQ_HDR + i * SSW_REQ_ENTRY;
        r[i].peer = 0;
        r[i].seq = get32(e);
        r[i].fr = e[4];
        r[i].pc = e[5];
        r[i].tag = (uint16_t) get16(e + 6);
        r[i].urgent = e[8];
    }
    *stream_id = get64(in + 6);
    *count = c;
    return SSW_OK;
}

int ssw_notice_encode(uint64_t stream_id, uint32_t seq, uint8_t fr, const uint8_t bm[SSW_BM],
                      uint8_t *out, uint32_t cap)
{
    if (!bm || !out || fr >= SSW_MAX_FR) return SSW_ERR_ARG;
    if (cap < SSW_NOTICE_LEN) return SSW_ERR_SPACE;
    out[0] = 'Z';
    out[1] = 'X';
    out[2] = 'S';
    out[3] = 'N';
    out[4] = 1;
    out[5] = fr;
    out[6] = 0;
    out[7] = 0;
    put64(out + 8, stream_id);
    put32(out + 16, seq);
    copy(out + 20, bm, SSW_BM);
    return SSW_NOTICE_LEN;
}

int ssw_notice_parse(const uint8_t *in, uint32_t len, uint64_t *stream_id, uint32_t *seq,
                     uint8_t *fr, uint8_t bm[SSW_BM])
{
    if (!in || !stream_id || !seq || !fr || !bm) return SSW_ERR_ARG;
    if (len != SSW_NOTICE_LEN || !magic(in, 'N') || in[4] != 1) return SSW_ERR_FORMAT;
    if (in[5] >= SSW_MAX_FR || in[6] || in[7]) return SSW_ERR_FORMAT;
    uint8_t any = 0;
    for (uint32_t b = 0; b < SSW_BM; b++) any |= in[20 + b];
    if (!any) return SSW_ERR_FORMAT;
    *stream_id = get64(in + 8);
    *seq = get32(in + 16);
    *fr = in[5];
    copy(bm, in + 20, SSW_BM);
    return SSW_OK;
}

int ssw_piece_encode(uint64_t stream_id, uint32_t seq, uint8_t fr, uint8_t pc, bool pushed,
                     uint16_t tag, const uint8_t frames[SSW_PIECE_ROWS * 21], uint8_t *out,
                     uint32_t cap)
{
    if (!frames || !out || fr >= SSW_MAX_FR || pc >= SSW_PIECES) return SSW_ERR_ARG;
    if (pushed != (tag == SSW_TAG_PUSH)) return SSW_ERR_ARG;
    for (uint32_t i = 0; i < SSW_PIECE_ROWS; i++)
        if (frames[i * 21] != (uint8_t) (pc * SSW_PIECE_ROWS + i)) return SSW_ERR_ARG;
    if (cap < SSW_PIECE_LEN) return SSW_ERR_SPACE;
    out[0] = 'Z';
    out[1] = 'X';
    out[2] = 'S';
    out[3] = 'R';
    out[4] = 1;
    out[5] = fr;
    out[6] = pc;
    out[7] = pushed ? 1 : 0;
    put64(out + 8, stream_id);
    put32(out + 16, seq);
    put16(out + 20, tag);
    copy(out + SSW_PIECE_HDR, frames, SSW_PIECE_ROWS * 21);
    return (int) SSW_PIECE_LEN;
}

int ssw_piece_parse(const uint8_t *in, uint32_t len, uint64_t *stream_id, uint32_t *seq,
                    uint8_t *fr, uint8_t *pc, bool *pushed, uint16_t *tag,
                    uint8_t frames[SSW_PIECE_ROWS * 21])
{
    if (!in || !stream_id || !seq || !fr || !pc || !pushed || !tag || !frames) return SSW_ERR_ARG;
    if (len != SSW_PIECE_LEN || !magic(in, 'R') || in[4] != 1) return SSW_ERR_FORMAT;
    if (in[5] >= SSW_MAX_FR || in[6] >= SSW_PIECES || (in[7] & ~1u)) return SSW_ERR_FORMAT;
    const bool pu = in[7] & 1u;
    const uint32_t t = get16(in + 20);
    if (pu != (t == SSW_TAG_PUSH)) return SSW_ERR_FORMAT;
    for (uint32_t i = 0; i < SSW_PIECE_ROWS; i++)
        if (in[SSW_PIECE_HDR + i * 21] != (uint8_t) (in[6] * SSW_PIECE_ROWS + i))
            return SSW_ERR_FORMAT;
    *stream_id = get64(in + 8);
    *seq = get32(in + 16);
    *fr = in[5];
    *pc = in[6];
    *pushed = pu;
    *tag = (uint16_t) t;
    copy(frames, in + SSW_PIECE_HDR, SSW_PIECE_ROWS * 21);
    return SSW_OK;
}
