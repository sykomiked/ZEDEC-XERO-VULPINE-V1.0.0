/* reality.c — The Reality Engine. See reality.h. */
#include "reality.h"

static int32_t clampi(int64_t v) {
    if (v > 0x7fffffffLL) return 0x7fffffff;
    if (v < -0x7fffffffLL) return -0x7fffffff;
    return (int32_t)v;
}
void reality_init(reality_engine_t *e, const sigil_t *s) {
    if (!e) return;
    for (uint32_t i = 0; i < sizeof(*e); i++) ((uint8_t *)e)[i] = 0;
    e->sig = s;
    for (uint32_t i = 0; i < SIG_MAX_NODES; i++) { e->bound[i] = -1; e->op[i] = REAL_OP_SUM; }
}

void reality_set_sensor_ops(reality_engine_t *e, const reality_sensor_ops_t *ops) {
    if (!e) return;
    if (ops) e->ops = *ops; else { e->ops.read = 0; e->ops.ctx = 0; }
}

/* ---- sources ---- */
static int32_t src_alloc(reality_engine_t *e) {
    for (uint32_t i = 0; i < REALITY_MAX_SOURCES; i++)
        if (!e->src[i].in_use) { e->src[i].in_use = true; return (int32_t)i; }
    return -1;
}
int32_t reality_add_const(reality_engine_t *e, reality_val_t v) {
    if (!e) return -1;
    int32_t i = src_alloc(e);
    if (i < 0) return -1;
    e->src[i].kind = REAL_SRC_CONST; e->src[i].p0 = v; e->src[i].value = v; e->src[i].valid = true; return i;
}
int32_t reality_add_ramp(reality_engine_t *e, int32_t base, int32_t per_tick) {
    if (!e) return -1;
    int32_t i = src_alloc(e);
    if (i < 0) return -1;
    e->src[i].kind = REAL_SRC_RAMP; e->src[i].p0 = base; e->src[i].p1 = per_tick; e->src[i].valid = true; return i;
}
int32_t reality_add_osc(reality_engine_t *e, int32_t amplitude, int32_t period) {
    if (!e || period < 2) return -1;
    int32_t i = src_alloc(e);
    if (i < 0) return -1;
    e->src[i].kind = REAL_SRC_OSC; e->src[i].p0 = amplitude; e->src[i].p1 = period; e->src[i].valid = true; return i;
}
int32_t reality_add_seq(reality_engine_t *e, const reality_val_t *seq, uint32_t len) {
    if (!e || !seq || len == 0) return -1;
    int32_t i = src_alloc(e);
    if (i < 0) return -1;
    e->src[i].kind = REAL_SRC_SEQ; e->src[i].seq = seq; e->src[i].seq_len = len; e->src[i].valid = true; return i;
}
int32_t reality_add_feed(reality_engine_t *e, reality_val_t initial) {
    if (!e) return -1;
    int32_t i = src_alloc(e);
    if (i < 0) return -1;
    e->src[i].kind = REAL_SRC_FEED; e->src[i].value = initial; e->src[i].valid = true; return i;
}
int32_t reality_add_sensor(reality_engine_t *e, uint32_t sensor_id) {
    if (!e) return -1;
    int32_t i = src_alloc(e);
    if (i < 0) return -1;
    e->src[i].kind = REAL_SRC_SENSOR; e->src[i].sensor_id = sensor_id; e->src[i].valid = false; return i;
}
bool reality_feed(reality_engine_t *e, int32_t source, reality_val_t v) {
    if (!e || source < 0 || (uint32_t)source >= REALITY_MAX_SOURCES) return false;
    if (!e->src[source].in_use || e->src[source].kind != REAL_SRC_FEED) return false;
    e->src[source].value = v; e->src[source].valid = true; return true;
}

/* ---- wiring ---- */
bool reality_bind(reality_engine_t *e, uint8_t node, int32_t source) {
    if (!e || !e->sig || node >= e->sig->n_nodes) return false;
    if (source < 0 || (uint32_t)source >= REALITY_MAX_SOURCES || !e->src[source].in_use) return false;
    e->bound[node] = (int8_t)source; e->op[node] = REAL_OP_INPUT; return true;
}
bool reality_set_op(reality_engine_t *e, uint8_t node, reality_op_t op) {
    if (!e || !e->sig || node >= e->sig->n_nodes) return false;
    e->op[node] = op; return true;
}
bool reality_set_thresh(reality_engine_t *e, uint8_t node, int32_t thresh, int32_t hi) {
    if (!e || !e->sig || node >= e->sig->n_nodes) return false;
    e->thresh_p[node] = thresh; e->thresh_hi[node] = hi; return true;
}

int32_t reality_on(reality_engine_t *e, uint8_t node, reality_cmp_t cmp,
                   reality_val_t threshold, reality_react_fn fn, void *ctx) {
    if (!e || !fn) return -1;
    for (uint32_t i = 0; i < REALITY_MAX_REACTIONS; i++)
        if (!e->rx[i].in_use) {
            e->rx[i].in_use = true; e->rx[i].node = node; e->rx[i].cmp = cmp;
            e->rx[i].threshold = threshold; e->rx[i].fn = fn; e->rx[i].ctx = ctx;
            e->rx[i].armed = false;
            return (int32_t)i;
        }
    return -1;
}

uint32_t reality_lanes(const reality_engine_t *e) {
    return (e && e->sig) ? sig_fabric_lanes(e->sig) : 0;
}

/* ---- sampling ---- */
static void sample_sources(reality_engine_t *e, uint64_t ordinal) {
    for (uint32_t i = 0; i < REALITY_MAX_SOURCES; i++) {
        reality_source_t *s = &e->src[i];
        if (!s->in_use) continue;
        switch (s->kind) {
        case REAL_SRC_CONST: s->value = s->p0; s->valid = true; break;
        case REAL_SRC_RAMP:
            s->value = clampi((int64_t)s->p0 + (int64_t)s->p1 * (int64_t)ordinal);
            s->valid = true; break;
        case REAL_SRC_OSC: {
            int32_t per = s->p1, amp = s->p0;
            int32_t ph = (int32_t)(ordinal % (uint64_t)per);
            int32_t half = per / 2; if (half < 1) half = 1;
            if (ph < half) s->value = clampi((int64_t)amp * ph / half);
            else           s->value = clampi((int64_t)amp * (per - ph) / half);
            s->valid = true; break;
        }
        case REAL_SRC_SEQ:
            s->value = s->seq[ordinal % s->seq_len]; s->valid = true; break;
        case REAL_SRC_FEED: /* value already holds the last pushed sample */
            s->valid = true; break;
        case REAL_SRC_SENSOR: {
            reality_val_t out = s->value;   /* keep last on failure */
            if (e->ops.read && e->ops.read(s->sensor_id, &out, e->ops.ctx) == 0) {
                s->value = out; s->valid = true;
            } else {
                s->valid = false;           /* NOT BOUND — never invent a reading */
            }
            break;
        }
        }
    }
}

/* ---- predecessors (earlier-wave neighbours), node-index sorted ---- */
static uint32_t predecessors(const reality_engine_t *e, uint8_t n,
                             uint8_t *out, uint32_t cap) {
    const sigil_t *s = e->sig;
    uint32_t m = 0;
    for (uint32_t ei = 0; ei < s->n_edges; ei++) {
        uint8_t a = s->edge[ei].a, b = s->edge[ei].b, nb;
        if (a == n) nb = b; else if (b == n) nb = a; else continue;
        if (nb >= s->n_nodes) continue;
        /* A neighbour feeds this node if it is a bound INPUT (a source, always
         * available from the start of the tick) OR it fires in an earlier
         * wave. Without the input clause, a multi-input node reached by the
         * BFS before one of its own inputs would miss that input — the inputs
         * are data, not part of the dependency ordering. */
        if (e->bound[nb] < 0 && e->node_wave[nb] >= e->node_wave[n])
            continue;                                         /* not a predecessor */
        /* insert sorted by node index, skip duplicates */
        bool dup = false; for (uint32_t j = 0; j < m; j++) if (out[j] == nb) dup = true;
        if (dup) continue;
        if (m >= cap) continue;
        uint32_t p = m;
        while (p > 0 && out[p-1] > nb) { out[p] = out[p-1]; p--; }
        out[p] = nb; m++;
    }
    return m;
}

uint32_t reality_tick(reality_engine_t *e, uint64_t ordinal) {
    if (!e || !e->sig || e->sig->n_nodes == 0) return 0;

    if (!e->scheduled) {
        if (!sig_schedule(e->sig, -1, &e->sched)) return 0;
        /* map node -> wave from the schedule */
        for (uint8_t w = 0; w < e->sched.n_waves; w++)
            for (uint8_t i = e->sched.wave_start[w]; i < e->sched.wave_start[w+1]; i++)
                e->node_wave[e->sched.order[i]] = w;
        e->scheduled = true;
    }

    sample_sources(e, ordinal);

    bool valid[SIG_MAX_NODES];
    for (uint32_t i = 0; i < SIG_MAX_NODES; i++) valid[i] = false;

    /* Latch ALL inputs first: a source value is available from the start of the
     * tick, independent of the schedule order. Doing this inside the wave loop
     * let a multi-input node that the BFS reached early read an input node that
     * had not been assigned yet. */
    for (uint8_t n = 0; n < e->sig->n_nodes; n++) {
        if (e->bound[n] >= 0) {
            reality_source_t *s = &e->src[e->bound[n]];
            e->val[n] = s->value;
            valid[n] = s->valid;
        }
    }

    for (uint8_t i = 0; i < e->sched.n_scheduled; i++) {
        uint8_t n = e->sched.order[i];

        if (e->bound[n] >= 0) continue;             /* input node, already latched */

        uint8_t pred[SIG_MAX_NODES];
        uint32_t np = predecessors(e, n, pred, SIG_MAX_NODES);
        int64_t sum = 0, mn = 0, mx = 0;
        bool pv = true;
        for (uint32_t j = 0; j < np; j++) {
            int32_t v = e->val[pred[j]];
            if (!valid[pred[j]]) pv = false;
            sum += v;
            if (j == 0) { mn = v; mx = v; }
            else { if (v < mn) mn = v; if (v > mx) mx = v; }
        }
        int32_t r;
        switch (e->op[n]) {
        case REAL_OP_INPUT: r = 0; break;           /* input op, nothing bound */
        case REAL_OP_SUM:   r = clampi(sum); break;
        case REAL_OP_MAX:   r = np ? (int32_t)mx : 0; break;
        case REAL_OP_MIN:   r = np ? (int32_t)mn : 0; break;
        case REAL_OP_MEAN:  r = np ? clampi(sum / (int64_t)np) : 0; break;
        case REAL_OP_DIFF: {
            int64_t d = np ? e->val[pred[0]] : 0;
            for (uint32_t j = 1; j < np; j++) d -= e->val[pred[j]];
            r = clampi(d); break;
        }
        case REAL_OP_ABS:   r = clampi(sum < 0 ? -sum : sum); break;
        case REAL_OP_THRESH:                        /* the nonlinear gate */
            r = (sum >= e->thresh_p[n]) ? e->thresh_hi[n] : 0; break;
        default:            r = clampi(sum); break;
        }
        e->val[n] = r;
        valid[n] = (np == 0) ? true : pv;           /* trivially valid if no inputs */
    }

    /* reactions — edge-triggered on the rising edge of the condition */
    for (uint32_t i = 0; i < REALITY_MAX_REACTIONS; i++) {
        reality_reaction_t *rx = &e->rx[i];
        if (!rx->in_use || rx->node >= e->sig->n_nodes) continue;
        int32_t v = e->val[rx->node];
        bool cond;
        switch (rx->cmp) {
        case REAL_CMP_GT: cond = v >  rx->threshold; break;
        case REAL_CMP_GE: cond = v >= rx->threshold; break;
        case REAL_CMP_LT: cond = v <  rx->threshold; break;
        case REAL_CMP_LE: cond = v <= rx->threshold; break;
        case REAL_CMP_EQ: cond = v == rx->threshold; break;
        default: cond = false; break;
        }
        if (cond && !rx->armed) { rx->armed = true; if (rx->fn) rx->fn(rx->node, v, rx->ctx); }
        else if (!cond) rx->armed = false;          /* re-arm when it falls back */
    }

    for (uint32_t i = 0; i < SIG_MAX_NODES; i++) e->node_valid[i] = valid[i];

    e->last_ordinal = ordinal;
    e->ticks++;
    return e->sched.n_scheduled;
}

reality_val_t reality_read(const reality_engine_t *e, uint8_t node) {
    if (!e || !e->sig || node >= e->sig->n_nodes) return 0;
    return e->val[node];
}

bool reality_node_valid(const reality_engine_t *e, uint8_t node) {
    if (!e || !e->sig || node >= e->sig->n_nodes) return false;
    return e->node_valid[node];
}
