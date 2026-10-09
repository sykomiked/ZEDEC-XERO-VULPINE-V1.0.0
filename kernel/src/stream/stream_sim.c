/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* stream_sim.c — deterministic network simulator for the swarm scheduler,
 * and the client-server baseline. See stream_sim.h. Integer only, no libc,
 * no division; all memory comes from the caller's arena. */
#include "stream_sim.h"

#define WHEEL    64u
#define ADV_RING 8u
#ifndef ADV_INT
#    define ADV_INT 10u
#endif
#define ADV_FULL    20u                   /* rounds: a full advert every 2 s */
#define STREAM_ID   0x5A58565354524D31ull /* "ZXVSTRM1" */
#define NONE32      0xFFFFFFFFu
#define EV_PER_NODE 3072u
#define REQ_BUF     512u
#define SEND_BUF    1024u
#define CS_SEGS     256u
#define CS_Q        2048u

enum { EV_MANIFEST = 1, EV_NOTICE, EV_REQ, EV_DATA, EV_PUSH, EV_REJECT, EV_ADVERT };

typedef struct {
    uint32_t next, seq, from, to, stamp;
    uint16_t tag;
    uint8_t type, fr, pc, urgent;
} ev_t;

typedef struct {
    ssw_node_t node;
    uint8_t active, probe, origin;
    uint32_t id, join, acc, up, dn;
    uint32_t adv_cd, adv_head, adv_full_cd;
    bool adv_force;
    uint32_t adv_stamp[ADV_RING], adv_len[ADV_RING];
    uint8_t adv[ADV_RING][SSW_ADV_MAX];
    uint32_t aud_seq[SSW_WIN];
    uint8_t aud[SSW_WIN][SSW_MAX_FR][SSW_BM];
    uint16_t sent[128], reqs[128]; /* rows per tick, last tps ticks */
    uint32_t sent_sum, req_sum, sent_now, req_now;
    uint32_t nsess;
} snode_t;

struct ssim {
    ssim_params_t p;
    uint32_t nn; /* origin + audience + probe slot */
    snode_t *nd;
    ev_t *ev;
    uint32_t nev, freeh;
    uint32_t wh[WHEEL], wt[WHEEL];
    uint64_t rng;
    uint32_t now, next_seq, next_pub, gen, ring_pos;
    uint8_t nbm[8][SSW_MAX_PEERS][SSW_MAX_FR][SSW_BM];
    uint32_t nbm_seq[8];
    uint32_t nchurn;
    uint32_t churn_tick[SSIM_MAX_CHURN], churn_slot[SSIM_MAX_CHURN];
    uint32_t rejoin_tick[SSIM_MAX_VIEWERS + 2];
    uint32_t join_tick[SSIM_MAX_VIEWERS + 2];
    ssim_result_t r;
    ssw_req_t rq[REQ_BUF];
    ssw_send_t sd[SEND_BUF];
    ssw_notice_t no[SSW_MAX_PEERS * SSW_MAX_FR];
    ssw_advert_t adv;
};

/* ---- helpers ---- */

static void mcopy(void *d, const void *s, size_t n)
{
    uint8_t *a = (uint8_t *) d;
    const uint8_t *b = (const uint8_t *) s;
    while (n--) *a++ = *b++;
}

static void mzero(void *p, size_t n)
{
    uint8_t *d = (uint8_t *) p;
    for (size_t i = 0; i < n; i++) d[i] = 0;
}

static uint64_t mix64(uint64_t z)
{
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* uniform in [0, n) without division */
static uint32_t below64(uint64_t h, uint32_t n)
{
    return (uint32_t) (((h >> 32) * (uint64_t) n) >> 32);
}

static uint32_t rnd_below(ssim_t *s, uint32_t n)
{
    s->rng ^= s->rng >> 12;
    s->rng ^= s->rng << 25;
    s->rng ^= s->rng >> 27;
    return below64(s->rng * 0x2545F4914F6CDD1Dull, n);
}

static uint64_t vhash(const ssim_params_t *p, uint32_t i, uint32_t salt)
{
    return mix64(p->seed ^ ((uint64_t) i << 20) ^ ((uint64_t) salt << 48));
}

/* Per-viewer attributes, identical in both simulators. */
static void viewer_attr(const ssim_params_t *p, uint32_t i, uint32_t *up, uint32_t *dn,
                        uint32_t *acc, uint32_t *join)
{
    uint32_t cls = (uint32_t) (vhash(p, i, 1) >> 62);
    *up = p->up[cls];
    *dn = p->dn[cls];
    if (below64(vhash(p, i, 2), 100) < p->free_rider_pct) *up = 0;
    uint32_t span = p->lat_max >= p->lat_min ? p->lat_max - p->lat_min + 1 : 1;
    *acc = p->lat_min + below64(vhash(p, i, 3), span);
    uint32_t jw = p->join_to > p->join_from ? p->join_to - p->join_from : 1;
    *join = p->join_from + below64(vhash(p, i, 4), jw);
}

void ssim_defaults(ssim_params_t *p)
{
    if (!p) return;
    mzero(p, sizeof *p);
    p->n_viewers = 16;
    p->seed = 0x5A58560000000001ull;
    p->ticks = 4000;
    p->tps = 100;
    p->seg_ticks = 100;
    p->k = 128;
    p->nfr = 4;          /* R = 512 rows/s = 10 240 payload bytes/s */
    p->origin_up = 1024; /* 2R */
    p->up[0] = 256;      /* 0.5R  (25%) */
    p->up[1] = 768;      /* 1.5R  (50%) */
    p->up[2] = 768;
    p->up[3] = 1536; /* 3R    (25%) */
    p->dn[0] = 2048;
    p->dn[1] = 4096;
    p->dn[2] = 4096;
    p->dn[3] = 8192;
    p->lat_min = 1;
    p->lat_max = 5;
    p->loss_ppm = 10000;
    p->join_from = 300;
    p->join_to = 1000;
    p->degree = 10;
    p->origin_slots = 16;
    p->probe_tick = 2000;
    p->probe_dn = 2048; /* 4R: the joiner's own download cap */
    p->probe_up = 768;
    p->probe_segs = 12;
    p->probe_start = 6;
    p->n_push = 20; /* pieces: 160 rows */
    p->push_fanout = 1;
    p->live_back = 1;
    p->startup_segs = 2;
    p->urgent_ticks = 60;
    p->rescue_ticks = 40;
}

static size_t al16(size_t x)
{
    return (x + 15u) & ~(size_t) 15u;
}

size_t ssim_arena_bytes(const ssim_params_t *p)
{
    if (!p || p->n_viewers > SSIM_MAX_VIEWERS) return 0;
    size_t nn = (size_t) p->n_viewers + 2u;
    return al16(sizeof(ssim_t)) + al16(nn * sizeof(snode_t)) +
           al16(nn * EV_PER_NODE * sizeof(ev_t));
}

/* ---- events ---- */

static void ev_send(ssim_t *s, uint32_t delay, const ev_t *e)
{
    if (delay < 1) delay = 1;
    if (delay >= WHEEL || s->freeh == NONE32) {
        s->r.event_overflow++;
        return;
    }
    uint32_t i = s->freeh;
    ev_t *d = &s->ev[i];
    s->freeh = d->next;
    mcopy(d, e, sizeof *d);
    d->next = NONE32;
    uint32_t w = (s->now + delay) & (WHEEL - 1u);
    if (s->wt[w] == NONE32)
        s->wh[w] = i;
    else
        s->ev[s->wt[w]].next = i;
    s->wt[w] = i;
}

static snode_t *live(ssim_t *s, uint32_t id)
{
    uint32_t i = id & 0xFFu;
    if (i >= s->nn) return 0;
    snode_t *n = &s->nd[i];
    return (n->active && n->id == id) ? n : 0;
}

static uint32_t lat(const snode_t *a, const snode_t *b)
{
    return a->acc + b->acc;
}

static void mk(ev_t *e, uint8_t type, uint32_t from, uint32_t to, uint32_t seq)
{
    mzero(e, sizeof *e);
    e->type = type;
    e->from = from;
    e->to = to;
    e->seq = seq;
}

/* ---- topology ---- */

static bool connect(ssim_t *s, snode_t *a, snode_t *b)
{
    if (a == b || !a->active || !b->active) return false;
    if (ssw_peer_find(&a->node, b->id) >= 0) return false;
    if (ssw_peer_count(&a->node) >= SSW_MAX_PEERS || ssw_peer_count(&b->node) >= SSW_MAX_PEERS)
        return false;
    if (ssw_peer_add(&a->node, b->id, b->origin) < 0) return false;
    if (ssw_peer_add(&b->node, a->id, a->origin) < 0) {
        ssw_peer_remove(&a->node, b->id);
        return false;
    }
    a->adv_cd = 0; /* a full advert to the new neighbour at once */
    b->adv_cd = 0;
    a->adv_force = b->adv_force = true;
    (void) s;
    return true;
}

static uint32_t viewer_peers(const snode_t *n)
{
    uint32_t c = 0;
    for (uint32_t i = 0; i < SSW_MAX_PEERS; i++)
        c += n->node.peer[i].used && !n->node.peer[i].origin;
    return c;
}

static void wire(ssim_t *s, snode_t *n, uint32_t want)
{
    snode_t *o = &s->nd[0];
    if (ssw_peer_count(&o->node) < s->p.origin_slots) connect(s, n, o);
    for (uint32_t tries = 0; tries < 96 && viewer_peers(n) < want; tries++) {
        uint32_t j = 1 + rnd_below(s, s->nn - 1);
        snode_t *m = &s->nd[j];
        if (!m->active || m == n) continue;
        if (ssw_peer_count(&m->node) >= SSW_MAX_PEERS - 1u) continue;
        connect(s, n, m);
    }
}

static void session_end(ssim_t *s, snode_t *n)
{
    ssim_result_t *r = &s->r;
    const ssw_node_t *x = &n->node;
    if (n->probe) {
        r->probe_up_rows += x->st.up_rows;
        r->probe_rx_rows += (x->st.rx_new + x->st.rx_late + x->st.rx_dup) * SSW_PIECE_ROWS;
    } else {
        r->sessions++;
        if (x->player.started) {
            r->started++;
            uint32_t su = x->player.start_tick - n->join;
            r->startup_ticks_sum += su;
            if (su > r->startup_max) r->startup_max = su;
        } else {
            r->not_started++;
        }
        r->stalls += x->player.stalls;
        if (x->player.stalls) r->stalled_sessions++;
        if (n->up == 0) {
            r->zero_up_sessions++;
            if (x->player.stalls) r->zero_up_stalled++;
        }
        r->stall_ticks += x->player.stall_ticks;
        r->played_segs += x->player.played;
        r->useful_rows += x->st.freights_done * s->p.k;
        r->rx_rows += (x->st.rx_new + x->st.rx_late + x->st.rx_dup) * SSW_PIECE_ROWS;
        r->active_ticks += s->now - n->join;
        r->peer_up_rows += x->st.up_rows;
    }
    r->timeouts += x->st.timeouts;
    r->late_rows += x->st.rx_late * SSW_PIECE_ROWS;
    r->rejects += x->st.rejects_rx;
    r->freights_completed += x->st.freights_done;
}

static void join(ssim_t *s, uint32_t slot, bool probe)
{
    snode_t *n = &s->nd[slot];
    s->gen++;
    uint32_t up, dn, acc, jt;
    /* the first session of a slot matches the client-server baseline */
    viewer_attr(&s->p, n->nsess ? slot + (s->gen << 8) : slot, &up, &dn, &acc, &jt);
    n->nsess++;
    if (probe) {
        up = s->p.probe_up;
        dn = s->p.probe_dn;
    }
    ssw_config_t c;
    ssw_config_default(&c);
    c.ticks_per_sec = s->p.tps;
    c.up_rows_per_sec = up;
    c.dn_rows_per_sec = dn;
    c.seg_ticks = s->p.seg_ticks;
    c.urgent_ticks = s->p.urgent_ticks;
    c.rescue_ticks = s->p.rescue_ticks;
    c.live_back = s->p.live_back;
    c.startup_segs = s->p.startup_segs;
    c.n_push = s->p.n_push;
    c.push_fanout = s->p.push_fanout;
    if (probe) {
        c.mode = SSW_MODE_VOD;
        c.start_seq = s->p.probe_start;
    }
    n->id = slot | (s->gen << 8);
    ssw_init(&n->node, &c, STREAM_ID, n->id, s->now);
    n->active = 1;
    n->probe = probe;
    n->origin = 0;
    n->join = s->now;
    n->acc = acc;
    n->up = up;
    n->dn = dn;
    n->adv_cd = 0;
    n->adv_head = 0;
    for (uint32_t i = 0; i < ADV_RING; i++) n->adv_stamp[i] = NONE32;
    for (uint32_t i = 0; i < SSW_WIN; i++) n->aud_seq[i] = NONE32;
    mzero(n->sent, sizeof n->sent);
    mzero(n->reqs, sizeof n->reqs);
    n->sent_sum = n->req_sum = n->sent_now = n->req_now = 0;
    wire(s, n, probe ? SSW_MAX_PEERS - 1u : s->p.degree);
    /* the manifests of the window, fetched from a neighbour on join */
    uint32_t first = s->next_seq > SSW_WIN - 2u ? s->next_seq - (SSW_WIN - 2u) : 0;
    for (uint32_t q = first; q < s->next_seq; q++) {
        ev_t e;
        mk(&e, EV_MANIFEST, s->nd[0].id, n->id, q);
        ev_send(s, 2u * n->acc + s->p.lat_max, &e);
    }
}

static void leave(ssim_t *s, uint32_t slot)
{
    snode_t *n = &s->nd[slot];
    if (!n->active) return;
    session_end(s, n);
    n->active = 0;
    for (uint32_t i = 0; i < SSW_MAX_PEERS; i++) {
        if (!n->node.peer[i].used) continue;
        snode_t *m = live(s, n->node.peer[i].id);
        if (!m) continue;
        int pi = ssw_peer_find(&m->node, n->id);
        if (pi >= 0) {
            /* Requests m had in flight to n are released by the library;
             * re-asking those ids elsewhere is legitimate: clear the audit. */
            for (uint32_t t = 0; t < SSW_INFLIGHT; t++) {
                const ssw_infl_t *e = &m->node.infl[t];
                if (!e->used || e->peer != pi) continue;
                uint32_t sl = e->seq & (SSW_WIN - 1u);
                if (m->aud_seq[sl] == e->seq)
                    m->aud[sl][e->fr][e->pc >> 3] &= (uint8_t) ~(1u << (e->pc & 7));
            }
        }
        ssw_peer_remove(&m->node, n->id);
        if (!m->origin && viewer_peers(m) < ((s->p.degree + 1u) >> 1))
            wire(s, m, m->probe ? SSW_MAX_PEERS - 1u : s->p.degree);
    }
}

/* ---- event handling ---- */

static void audit_clear(snode_t *n, uint32_t seq, uint8_t fr, uint8_t pc)
{
    uint32_t sl = seq & (SSW_WIN - 1u);
    if (n->aud_seq[sl] == seq) n->aud[sl][fr][pc >> 3] &= (uint8_t) ~(1u << (pc & 7));
}

static void handle(ssim_t *s, const ev_t *e)
{
    snode_t *to = live(s, e->to);
    if (!to) return;
    ssw_node_t *x = &to->node;
    int pi = e->type == EV_MANIFEST ? 0 : ssw_peer_find(x, e->from);
    if (pi < 0) return;
    switch (e->type) {
    case EV_MANIFEST:
        ssw_seg_known(x, e->seq, s->p.k, s->p.nfr);
        break;
    case EV_NOTICE: {
        uint32_t b = e->seq & 7u;
        if (s->nbm_seq[b] == e->seq) ssw_on_notice(x, pi, e->seq, e->fr, s->nbm[b][e->tag][e->fr]);
        break;
    }
    case EV_REQ: {
        int rc = ssw_on_request(x, pi, e->seq, e->fr, e->pc, e->tag, e->urgent);
        if (rc != SSW_OK) {
            snode_t *fr = live(s, e->from);
            if (fr) {
                ev_t r;
                mk(&r, EV_REJECT, to->id, fr->id, e->seq);
                r.urgent = rc == SSW_ERR_FULL; /* busy */
                r.tag = e->tag;
                r.fr = e->fr;
                r.pc = e->pc;
                ev_send(s, lat(to, fr), &r);
            }
        }
        break;
    }
    case EV_DATA:
    case EV_PUSH: {
        unsigned rc = ssw_on_piece(x, pi, e->seq, e->fr, e->pc, e->tag);
        if (rc & SSW_PC_DUP) s->r.dup_rx++;
        break;
    }
    case EV_REJECT: {
        const ssw_infl_t *in = e->tag < SSW_INFLIGHT ? &x->infl[e->tag] : 0;
        if (in && in->used && in->peer == pi && in->seq == e->seq && in->fr == e->fr &&
            in->pc == e->pc)
            audit_clear(to, e->seq, e->fr, e->pc);
        ssw_on_reject(x, pi, e->tag, e->urgent);
        break;
    }
    case EV_ADVERT: {
        snode_t *fr = live(s, e->from);
        if (!fr) break;
        uint32_t b = e->tag;
        if (b >= ADV_RING || fr->adv_stamp[b] != e->stamp) break;
        if (ssw_advert_parse(fr->adv[b], fr->adv_len[b], &s->adv) != SSW_OK) {
            s->r.event_overflow++; /* our own encoder produced garbage: flag it */
            break;
        }
        ssw_advert_apply(x, pi, &s->adv);
        break;
    }
    default:
        break;
    }
}

/* ---- the run ---- */

ssim_t *ssim_create(const ssim_params_t *p, void *arena, size_t len)
{
    if (!p || !arena || p->n_viewers > SSIM_MAX_VIEWERS || p->tps == 0 || p->tps > 128 ||
        p->seg_ticks == 0 || p->k < 1 || p->k > 168 || p->nfr < 1 || p->nfr > SSW_MAX_FR ||
        2u * p->lat_max + 2u * p->lat_max + 2u >= WHEEL || p->lat_min < 1 ||
        p->churn_leaves > SSIM_MAX_CHURN)
        return 0;
    if (((uintptr_t) arena & 15u) || len < ssim_arena_bytes(p)) return 0;
    uint8_t *a = (uint8_t *) arena;
    ssim_t *s = (ssim_t *) a;
    mzero(s, sizeof *s);
    mcopy(&s->p, p, sizeof s->p);
    s->nn = p->n_viewers + 2u;
    s->nd = (snode_t *) (a + al16(sizeof(ssim_t)));
    s->ev = (ev_t *) (a + al16(sizeof(ssim_t)) + al16(s->nn * sizeof(snode_t)));
    s->nev = s->nn * EV_PER_NODE;
    for (uint32_t i = 0; i < s->nev; i++) s->ev[i].next = i + 1 < s->nev ? i + 1 : NONE32;
    s->freeh = 0;
    for (uint32_t i = 0; i < WHEEL; i++) s->wh[i] = s->wt[i] = NONE32;
    for (uint32_t i = 0; i < 8; i++) s->nbm_seq[i] = NONE32;
    s->rng = mix64(p->seed ^ 0xC0FFEEull) | 1u;
    s->next_pub = p->seg_ticks;
    for (uint32_t i = 0; i < s->nn; i++) {
        s->nd[i].active = 0;
        s->nd[i].nsess = 0;
        s->nd[i].id = NONE32;
        s->rejoin_tick[i] = NONE32;
        s->join_tick[i] = NONE32;
    }
    /* origin */
    snode_t *o = &s->nd[0];
    ssw_config_t c;
    ssw_config_default(&c);
    c.ticks_per_sec = p->tps;
    c.up_rows_per_sec = p->origin_up;
    c.dn_rows_per_sec = 0;
    c.seg_ticks = p->seg_ticks;
    c.is_origin = 1;
    c.n_push = p->n_push;
    c.push_fanout = p->push_fanout;
    o->id = 0;
    if (ssw_init(&o->node, &c, STREAM_ID, 0, 0) != SSW_OK) return 0;
    o->active = 1;
    o->origin = 1;
    o->acc = p->lat_min;
    o->up = p->origin_up;
    for (uint32_t i = 0; i < ADV_RING; i++) o->adv_stamp[i] = NONE32;
    for (uint32_t i = 0; i < SSW_WIN; i++) o->aud_seq[i] = NONE32;
    /* plans */
    for (uint32_t i = 1; i <= p->n_viewers; i++) {
        uint32_t up, dn, acc, jt;
        viewer_attr(p, i, &up, &dn, &acc, &jt);
        s->join_tick[i] = jt;
    }
    if (p->probe) s->join_tick[s->nn - 1] = p->probe_tick;
    s->nchurn = 0;
    for (uint32_t c2 = 0; c2 < p->churn_leaves && p->n_viewers; c2++) {
        uint64_t h = vhash(p, 1000 + c2, 9);
        uint32_t span = p->churn_to > p->churn_from ? p->churn_to - p->churn_from : 1;
        s->churn_tick[s->nchurn] = p->churn_from + below64(h, span);
        s->churn_slot[s->nchurn] = 1 + below64(mix64(h), p->n_viewers);
        s->nchurn++;
    }
    return s;
}

static void audit_tick(ssim_t *s, snode_t *n)
{
    uint32_t pos = s->ring_pos;
    n->sent_sum = n->sent_sum - n->sent[pos] + n->sent_now;
    n->sent[pos] = (uint16_t) n->sent_now;
    n->req_sum = n->req_sum - n->reqs[pos] + n->req_now;
    n->reqs[pos] = (uint16_t) n->req_now;
    n->sent_now = n->req_now = 0;
    const uint64_t tps = s->p.tps;
    /* token bucket: up/tps rows per tick plus a burst of 2 pieces + 1 tick */
    if ((uint64_t) n->sent_sum * tps > (uint64_t) n->up * (tps + 1u) + 16u * tps)
        s->r.cap_violations++;
    if (!n->origin && (uint64_t) n->req_sum * tps > (uint64_t) n->dn * (tps + 5u) + 24u * tps)
        s->r.dn_violations++;
}

static void publish(ssim_t *s)
{
    snode_t *o = &s->nd[0];
    uint32_t seq = s->next_seq++;
    uint32_t nno = 0;
    ssw_origin_publish(&o->node, seq, s->p.k, s->p.nfr, s->no, SSW_MAX_PEERS * SSW_MAX_FR, &nno);
    uint32_t b = seq & 7u;
    s->nbm_seq[b] = seq;
    /* manifest: origin neighbours first (same link, before the notices and
     * rows), everyone else by flooding a few hops later */
    for (uint32_t i = 1; i < s->nn; i++) {
        snode_t *v = &s->nd[i];
        if (!v->active) continue;
        ev_t e;
        mk(&e, EV_MANIFEST, o->id, v->id, seq);
        int pi = ssw_peer_find(&o->node, v->id);
        ev_send(s, pi >= 0 ? lat(o, v) : 2u * v->acc + 2u * s->p.lat_max + 1u, &e);
    }
    for (uint32_t i = 0; i < nno; i++) {
        const ssw_notice_t *no = &s->no[i];
        for (uint32_t x = 0; x < SSW_BM; x++) s->nbm[b][no->peer][no->fr][x] = no->bm[x];
        snode_t *v = live(s, o->node.peer[no->peer].id);
        if (!v) continue;
        ev_t e;
        mk(&e, EV_NOTICE, o->id, v->id, seq);
        e.fr = no->fr;
        e.tag = no->peer;
        ev_send(s, lat(o, v), &e);
        s->r.notice_bytes += SSW_NOTICE_LEN;
    }
}

int ssim_run(ssim_t *s, ssim_result_t *res)
{
    if (!s || !res) return -1;
    const ssim_params_t *p = &s->p;
    for (s->now = 0; s->now < p->ticks; s->now++) {
        const uint32_t now = s->now;
        /* joins, churn, rejoins */
        for (uint32_t i = 1; i < s->nn; i++) {
            if (s->join_tick[i] == now) join(s, i, p->probe && i == s->nn - 1);
            if (s->rejoin_tick[i] == now) {
                s->rejoin_tick[i] = NONE32;
                join(s, i, false);
            }
        }
        for (uint32_t c = 0; c < s->nchurn; c++) {
            if (s->churn_tick[c] != now) continue;
            uint32_t sl = s->churn_slot[c];
            if (!s->nd[sl].active || s->nd[sl].probe) continue;
            leave(s, sl);
            if (p->churn_rejoin) s->rejoin_tick[sl] = now + p->tps;
        }
        /* clocks */
        for (uint32_t i = 0; i < s->nn; i++)
            if (s->nd[i].active) ssw_tick(&s->nd[i].node, now);
        if (now == s->next_pub) {
            publish(s);
            s->next_pub += p->seg_ticks;
        }
        /* deliver */
        uint32_t w = now & (WHEEL - 1u);
        uint32_t h = s->wh[w];
        s->wh[w] = s->wt[w] = NONE32;
        while (h != NONE32) {
            ev_t e;
            mcopy(&e, &s->ev[h], sizeof e);
            s->ev[h].next = s->freeh;
            s->freeh = h;
            handle(s, &e);
            h = e.next;
        }
        /* adverts */
        for (uint32_t i = 0; i < s->nn; i++) {
            snode_t *n = &s->nd[i];
            if (!n->active) continue;
            if (n->adv_cd) {
                n->adv_cd--;
                continue;
            }
            n->adv_cd = ADV_INT - 1u;
            uint32_t b = n->adv_head;
            n->adv_head = (b + 1u) & (ADV_RING - 1u);
            /* deltas every ADV_INT, a full advert every ADV_FULL rounds and
             * whenever a neighbour is new */
            bool full = n->adv_force || n->adv_full_cd == 0;
            if (full) {
                n->adv_force = false;
                n->adv_full_cd = ADV_FULL - 1u;
            } else {
                n->adv_full_cd--;
            }
            int len = ssw_advert_next(&n->node, n->adv[b], SSW_ADV_MAX, full);
            if (len <= 0) continue;
            n->adv_len[b] = (uint32_t) len;
            n->adv_stamp[b] = now;
            for (uint32_t k = 0; k < SSW_MAX_PEERS; k++) {
                if (!n->node.peer[k].used) continue;
                snode_t *m = live(s, n->node.peer[k].id);
                if (!m) continue;
                ev_t e;
                mk(&e, EV_ADVERT, n->id, m->id, 0);
                e.tag = (uint16_t) b;
                e.stamp = now;
                ev_send(s, lat(n, m), &e);
                s->r.advert_bytes += (uint32_t) len;
            }
        }
        /* pulls */
        for (uint32_t i = 1; i < s->nn; i++) {
            snode_t *n = &s->nd[i];
            if (!n->active || ((now ^ i) & 1u)) continue;
            uint32_t nr = ssw_schedule(&n->node, s->rq, REQ_BUF);
            uint32_t peers_used = 0;
            for (uint32_t q = 0; q < nr; q++) {
                const ssw_req_t *r = &s->rq[q];
                uint32_t sl = r->seq & (SSW_WIN - 1u);
                if (n->aud_seq[sl] != r->seq) {
                    n->aud_seq[sl] = r->seq;
                    mzero(n->aud[sl], sizeof n->aud[sl]);
                }
                uint8_t *ab = &n->aud[sl][r->fr][r->pc >> 3];
                if (*ab & (1u << (r->pc & 7))) s->r.req_twice++;
                *ab |= (uint8_t) (1u << (r->pc & 7));
                const ssw_oseg_t *os = &n->node.seg[sl];
                if (os->used && os->seq == r->seq && os->fr[r->fr].nhave >= os->k && os->known)
                    s->r.req_after_done++;
                if (!(peers_used & (1u << (r->peer & 31)))) {
                    peers_used |= 1u << (r->peer & 31);
                    s->r.request_bytes += SSW_REQ_HDR;
                }
                s->r.request_bytes += SSW_REQ_ENTRY;
                n->req_now += SSW_PIECE_ROWS;
                snode_t *m = live(s, n->node.peer[r->peer].id);
                if (!m) continue;
                ev_t e;
                mk(&e, EV_REQ, n->id, m->id, r->seq);
                e.fr = r->fr;
                e.pc = r->pc;
                e.tag = r->tag;
                e.urgent = r->urgent;
                ev_send(s, lat(n, m), &e);
            }
        }
        /* uploads */
        for (uint32_t i = 0; i < s->nn; i++) {
            snode_t *n = &s->nd[i];
            if (!n->active) continue;
            uint32_t ns = ssw_upload(&n->node, s->sd, SEND_BUF);
            for (uint32_t q = 0; q < ns; q++) {
                const ssw_send_t *d = &s->sd[q];
                snode_t *m = live(s, n->node.peer[d->peer].id);
                if (d->kind != SSW_SEND_REJECT) n->sent_now += SSW_PIECE_ROWS;
                if (!m) continue;
                ev_t e;
                uint8_t type = d->kind == SSW_SEND_PUSH   ? EV_PUSH
                               : d->kind == SSW_SEND_DATA ? EV_DATA
                                                          : EV_REJECT;
                mk(&e, type, n->id, m->id, d->seq);
                e.fr = d->fr;
                e.pc = d->pc;
                e.tag = d->tag;
                if (type != EV_REJECT && rnd_below(s, 1000000u) < p->loss_ppm) {
                    s->r.lost_rows += SSW_PIECE_ROWS;
                    continue;
                }
                ev_send(s, lat(n, m), &e);
            }
        }
        /* audits and the probe */
        for (uint32_t i = 0; i < s->nn; i++)
            if (s->nd[i].active) audit_tick(s, &s->nd[i]);
        if (++s->ring_pos >= p->tps) s->ring_pos = 0;
        if (p->probe) {
            snode_t *pr = &s->nd[s->nn - 1];
            if (pr->active && pr->probe && !s->r.probe_done) {
                bool all = true;
                for (uint32_t q = 0; q < p->probe_segs && all; q++)
                    all = ssw_seg_complete(&pr->node, p->probe_start + q);
                if (all) {
                    s->r.probe_done = 1;
                    s->r.probe_done_ticks = now - pr->join;
                }
            }
        }
    }
    /* close every open session */
    for (uint32_t i = 1; i < s->nn; i++) {
        snode_t *n = &s->nd[i];
        if (!n->active) continue;
        if (n->probe) {
            s->r.probe_started = n->node.player.started;
            s->r.probe_startup_ticks = n->node.player.start_tick - n->join;
        }
        session_end(s, n);
    }
    s->r.origin_up_rows = s->nd[0].node.st.up_rows;
    mcopy(res, &s->r, sizeof *res);
    return 0;
}

const ssw_node_t *ssim_node(const ssim_t *s, uint32_t i)
{
    return (s && i < s->nn) ? &s->nd[i].node : 0;
}

bool ssim_active(const ssim_t *s, uint32_t i)
{
    return s && i < s->nn && s->nd[i].active;
}

uint32_t ssim_produced(const ssim_t *s)
{
    return s ? s->next_seq : 0;
}

/* ---- client-server baseline ---- */

typedef struct {
    ssw_player_t pl;
    uint8_t active, probe;
    uint32_t up, dn, acc, join;
    int32_t dn_credit;
    uint16_t have[CS_SEGS], infl[CS_SEGS];
    uint32_t q_tick[CS_Q], q_seq[CS_Q];
    uint8_t q_lost[CS_Q];
    uint32_t qh, qn;
    uint64_t rx;
    uint32_t need;
} csv_t;

static bool cs_done(void *ctx, uint32_t seq)
{
    const csv_t *v = (const csv_t *) ctx;
    return seq < CS_SEGS && v->have[seq] >= v->need;
}

size_t ssim_cs_arena_bytes(const ssim_params_t *p)
{
    if (!p || p->n_viewers > SSIM_MAX_VIEWERS) return 0;
    return al16(((size_t) p->n_viewers + 1u) * sizeof(csv_t));
}

int ssim_run_cs(const ssim_params_t *p, void *arena, size_t len, ssim_result_t *r)
{
    if (!p || !arena || !r || len < ssim_cs_arena_bytes(p) || p->tps == 0 || p->seg_ticks == 0)
        return -1;
    if ((uint64_t) p->seg_ticks * (CS_SEGS - 1u) < p->ticks) return -1;
    mzero(r, sizeof *r);
    const uint32_t nv = p->n_viewers + (p->probe ? 1u : 0u);
    csv_t *v = (csv_t *) arena;
    mzero(v, (size_t) nv * sizeof(csv_t));
    const uint32_t need = (uint32_t) p->k * p->nfr;
    const int32_t tps = (int32_t) p->tps;
    uint32_t joins[SSIM_MAX_VIEWERS + 1];
    for (uint32_t i = 0; i < nv; i++) {
        uint32_t jt;
        viewer_attr(p, i + 1, &v[i].up, &v[i].dn, &v[i].acc, &jt);
        joins[i] = jt;
        v[i].need = need;
        if (p->probe && i == nv - 1) {
            v[i].probe = 1;
            v[i].dn = p->probe_dn;
            joins[i] = p->probe_tick;
        }
    }
    uint64_t rng = mix64(p->seed ^ 0xBA5Eull) | 1u;
    int32_t credit = 0;
    uint32_t newest = NONE32, next_pub = p->seg_ticks, rr = 0;
    for (uint32_t now = 0; now < p->ticks; now++) {
        if (now == next_pub) {
            newest = newest == NONE32 ? 0 : newest + 1;
            next_pub += p->seg_ticks;
        }
        for (uint32_t i = 0; i < nv; i++) {
            csv_t *c = &v[i];
            if (joins[i] == now) {
                c->active = 1;
                c->join = now;
                ssw_player_init(&c->pl, p->seg_ticks, p->startup_segs, now);
                if (c->probe) ssw_player_set_start(&c->pl, p->probe_start);
            }
            if (!c->active) continue;
            while (c->qn && c->q_tick[c->qh] <= now) {
                uint32_t q = c->q_seq[c->qh];
                if (c->infl[q]) c->infl[q]--;
                if (!c->q_lost[c->qh]) {
                    c->have[q]++;
                    c->rx++;
                }
                c->qh = (c->qh + 1u) & (CS_Q - 1u);
                c->qn--;
            }
            if (!c->probe && newest != NONE32) {
                uint32_t edge = newest >= p->live_back ? newest - p->live_back : 0;
                if (!c->pl.start_known || (!c->pl.started && edge > c->pl.start_seq + 2u))
                    ssw_player_set_start(&c->pl, edge); /* same rule as the swarm */
            }
            ssw_player_step(&c->pl, now, cs_done, c);
            c->dn_credit += (int32_t) c->dn;
            int32_t dcap = 8 * tps + 4 * (int32_t) c->dn;
            if (c->dn_credit > dcap) c->dn_credit = dcap;
        }
        credit += (int32_t) p->origin_up;
        if (credit > 2 * tps + (int32_t) p->origin_up) credit = 2 * tps + (int32_t) p->origin_up;
        if (newest == NONE32) continue;
        /* fair share: one pc per wanting viewer per pass */
        for (bool progress = true; progress && credit >= tps;) {
            progress = false;
            for (uint32_t k = 0; k < nv && credit >= tps; k++) {
                uint32_t i = rr + k;
                if (i >= nv) i -= nv;
                csv_t *c = &v[i];
                if (!c->active || !c->pl.start_known || c->dn_credit < tps || c->qn >= CS_Q)
                    continue;
                uint32_t lo = c->pl.started ? c->pl.play_seq + 1 : c->pl.start_seq;
                uint32_t hi = newest;
                if (c->probe && hi > lo + (SSW_WIN - 2u)) hi = lo + (SSW_WIN - 2u);
                uint32_t q = lo;
                while (q <= hi && q < CS_SEGS && (uint32_t) c->have[q] + c->infl[q] >= need) q++;
                if (q > hi || q >= CS_SEGS) continue;
                c->infl[q]++;
                c->dn_credit -= tps;
                credit -= tps;
                r->origin_up_rows++;
                uint32_t slot = (c->qh + c->qn) & (CS_Q - 1u);
                rng ^= rng >> 12;
                rng ^= rng << 25;
                rng ^= rng >> 27;
                uint8_t lost = below64(rng * 0x2545F4914F6CDD1Dull, 1000000u) < p->loss_ppm;
                if (lost) r->lost_rows++;
                c->q_tick[slot] = now + c->acc + p->lat_min;
                c->q_seq[slot] = q;
                c->q_lost[slot] = lost;
                c->qn++;
                progress = true;
            }
            rr = rr + 1 >= nv ? 0 : rr + 1;
        }
        if (p->probe) {
            csv_t *c = &v[nv - 1];
            if (c->active && !r->probe_done) {
                bool all = true;
                for (uint32_t q = 0; q < p->probe_segs && all; q++)
                    all = c->have[p->probe_start + q] >= need;
                if (all) {
                    r->probe_done = 1;
                    r->probe_done_ticks = now - c->join;
                }
            }
        }
    }
    for (uint32_t i = 0; i < nv; i++) {
        csv_t *c = &v[i];
        if (!c->active) continue;
        uint64_t useful = 0;
        for (uint32_t q = 0; q < CS_SEGS; q++) useful += c->have[q] >= need ? need : c->have[q];
        if (c->probe) {
            r->probe_started = c->pl.started;
            r->probe_startup_ticks = c->pl.start_tick - c->join;
            r->probe_rx_rows = c->rx;
            continue;
        }
        r->sessions++;
        if (c->pl.started) {
            r->started++;
            uint32_t su = c->pl.start_tick - c->join;
            r->startup_ticks_sum += su;
            if (su > r->startup_max) r->startup_max = su;
        } else {
            r->not_started++;
        }
        r->stalls += c->pl.stalls;
        if (c->pl.stalls) r->stalled_sessions++;
        r->stall_ticks += c->pl.stall_ticks;
        r->played_segs += c->pl.played;
        r->useful_rows += useful;
        r->rx_rows += c->rx;
        r->active_ticks += p->ticks - c->join;
    }
    return 0;
}
