/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* qnet.c — cycle-cancellation netting solvers (greedy, exact, annealing).
 *
 * WHAT IT DOES: see qnet.h. In short, three ways of finding a circulation f
 * (0 <= f <= cap, conservation at every member) that maximises the gross
 * obligations eliminated, sum f. Net positions never change.
 *
 * HONEST LIMITS
 *   - qn_exact_optimum is the optimum; qn_anneal is a classical heuristic
 *     (parallel tempering) that can at best tie it. Nothing here is quantum.
 *   - Fixed capacity, integer-only, no allocation, no libc.
 *   - Iteration guards return QN_ERR_LIMIT rather than a partial answer.
 */
#include "qnet.h"

#define QN_INF32 0x3FFFFFFF

static const uint32_t qn_exp_tab[QN_EXP_TABLE_N] = {
    16777216u, 15760736u, 14805841u, 13908801u, 13066109u, 12274473u, 11530801u, 10832185u,
    10175896u, 9559370u,  8980197u,  8436114u,  7924996u,  7444844u,  6993784u,  6570052u,
    6171993u,  5798051u,  5446765u,  5116762u,  4806753u,  4515526u,  4241945u,  3984938u,
    3743503u,  3516696u,  3303630u,  3103473u,  2915443u,  2738805u,  2572869u,  2416987u,
    2270549u,  2132984u,  2003753u,  1882351u,  1768306u,  1661169u,  1560524u,  1465977u,
    1377158u,  1293720u,  1215337u,  1141704u,  1072532u,  1007550u,  946506u,   889160u,
    835288u,   784681u,   737139u,   692478u,   650523u,   611110u,   574085u,   539303u,
    506628u,   475933u,   447098u,   420009u,   394562u,   370657u,   348200u,   327104u,
    307285u,   288668u,   271178u,   254749u,   239314u,   224815u,   211194u,   198398u,
    186378u,   175086u,   164478u,   154513u,   145151u,   136357u,   128096u,   120335u,
    113044u,   106195u,   99761u,    93717u,    88039u,    82705u,    77694u,    72987u,
    68565u,    64411u,    60508u,    56842u,    53398u,    50163u,    47124u,    44269u,
    41587u,    39067u,    36700u,    34476u,    32388u,    30425u,    28582u,    26850u,
    25224u,    23695u,    22260u,    20911u,    19644u,    18454u,    17336u,    16286u,
    15299u,    14372u,    13501u,    12683u,    11915u,    11193u,    10515u,    9878u,
    9279u,     8717u,     8189u,     7693u,     7227u,     6789u,     6378u,     5991u,
    5628u,     5287u,     4967u,     4666u,     4383u,     4118u,     3868u,     3634u,
    3414u,     3207u,     3013u,     2830u,     2659u,     2497u,     2346u,     2204u,
    2070u,     1945u,     1827u,     1716u,     1612u,     1515u,     1423u,     1337u,
    1256u,     1180u,     1108u,     1041u,     978u,      919u,      863u,      811u,
    762u,      716u,      672u,      631u,      593u,      557u,      523u,      492u,
    462u,      434u,      408u,      383u,      360u,      338u,      318u,      298u,
    280u,      263u,      247u,      232u,      218u,      205u,      193u,      181u,
    170u,      160u,      150u,      141u,      132u,      124u,      117u,      110u,
    103u,      97u,       91u,       85u,       80u,       75u,       71u,       67u,
    63u,       59u,       55u,       52u,       49u,       46u,       43u,       40u,
    38u,       36u,       33u,       31u,       30u,       28u,       26u,       24u,
    23u,       22u,       20u,       19u,       18u,       17u,       16u,       15u,
    14u,       13u,       12u,       12u,       11u,       10u,       10u,       9u,
    8u,        8u,        7u,        7u,        7u,        6u,        6u,        5u,
    5u,        5u,        5u,        4u,        4u,        4u,        4u,        3u,
    3u,        3u,        3u,        3u,        2u,        2u,        2u,        2u,
};

uint32_t qn_exp_q24(uint32_t i)
{
    return i < QN_EXP_TABLE_N ? qn_exp_tab[i] : 0u;
}

uint64_t qn_rng_next(uint64_t *state)
{
    uint64_t x;
    if (!state) return 0;
    x = *state;
    if (x == 0) x = 0x9E3779B97F4A7C15ull;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

/* ------------------------------------------------------------------ graph */

qn_status_t qn_graph_init(qn_graph_t *g, uint32_t num_nodes)
{
    if (!g) return QN_ERR_NULL;
    if (num_nodes == 0 || num_nodes > QN_MAX_NODES) return QN_ERR_RANGE;
    g->num_nodes = num_nodes;
    g->num_edges = 0;
    return QN_OK;
}

qn_status_t qn_graph_add(qn_graph_t *g, uint32_t from, uint32_t to, int64_t amount,
                         uint32_t *out_index)
{
    uint32_t i;
    if (!g) return QN_ERR_NULL;
    if (g->num_nodes == 0 || g->num_nodes > QN_MAX_NODES) return QN_ERR_RANGE;
    if (from >= g->num_nodes || to >= g->num_nodes) return QN_ERR_RANGE;
    if (from == to) return QN_ERR_SELF;
    if (amount <= 0) return QN_ERR_CAP;
    if (g->num_edges >= QN_MAX_EDGES) return QN_ERR_FULL;
    i = g->num_edges++;
    g->from[i] = from;
    g->to[i] = to;
    g->cap[i] = amount;
    if (out_index) *out_index = i;
    return QN_OK;
}

qn_status_t qn_graph_check(const qn_graph_t *g)
{
    int64_t total = 0;
    uint32_t e;
    if (!g) return QN_ERR_NULL;
    if (g->num_nodes == 0 || g->num_nodes > QN_MAX_NODES) return QN_ERR_RANGE;
    if (g->num_edges > QN_MAX_EDGES) return QN_ERR_FULL;
    for (e = 0; e < g->num_edges; e++) {
        if (g->from[e] >= g->num_nodes || g->to[e] >= g->num_nodes) return QN_ERR_RANGE;
        if (g->from[e] == g->to[e]) return QN_ERR_SELF;
        if (g->cap[e] <= 0) return QN_ERR_CAP;
        if (__builtin_add_overflow(total, g->cap[e], &total)) return QN_ERR_OVERFLOW;
    }
    return QN_OK;
}

static qn_status_t qn__flow_bounds(const qn_graph_t *g, const int64_t *flow)
{
    uint32_t e;
    for (e = 0; e < g->num_edges; e++)
        if (flow[e] < 0 || flow[e] > g->cap[e]) return QN_ERR_INFEASIBLE;
    return QN_OK;
}

qn_status_t qn_net_positions(const qn_graph_t *g, const int64_t *flow, int64_t *nets)
{
    qn_status_t st;
    uint32_t e, v;
    if (!g || !nets) return QN_ERR_NULL;
    st = qn_graph_check(g);
    if (st != QN_OK) return st;
    if (flow) {
        st = qn__flow_bounds(g, flow);
        if (st != QN_OK) return st;
    }
    for (v = 0; v < g->num_nodes; v++) {
        int64_t in = 0, out = 0;
        for (e = 0; e < g->num_edges; e++) {
            int64_t r = g->cap[e] - (flow ? flow[e] : 0);
            /* r <= cap, and cap sums were checked: these cannot overflow. */
            if (g->to[e] == v) in += r;
            if (g->from[e] == v) out += r;
        }
        nets[v] = in - out;
    }
    return QN_OK;
}

qn_status_t qn_verify(const qn_graph_t *g, const int64_t *flow, int64_t *eliminated)
{
    qn_status_t st;
    int64_t sum = 0;
    uint32_t e, v;
    if (!g || !flow) return QN_ERR_NULL;
    st = qn_graph_check(g);
    if (st != QN_OK) return st;
    st = qn__flow_bounds(g, flow);
    if (st != QN_OK) return st;
    for (v = 0; v < g->num_nodes; v++) {
        int64_t in = 0, out = 0;
        for (e = 0; e < g->num_edges; e++) {
            if (g->to[e] == v) in += flow[e];
            if (g->from[e] == v) out += flow[e];
        }
        if (in != out) return QN_ERR_INFEASIBLE;
    }
    for (e = 0; e < g->num_edges; e++) sum += flow[e]; /* <= total gross */
    if (eliminated) *eliminated = sum;
    return QN_OK;
}

static void qn__build_adj(const qn_graph_t *g, qn_workspace_t *ws)
{
    uint32_t v, e;
    for (v = 0; v < g->num_nodes; v++) ws->head[v] = -1;
    for (e = g->num_edges; e-- > 0;) {
        ws->next[e] = ws->head[g->from[e]];
        ws->head[g->from[e]] = (int32_t) e;
    }
}

static void qn__res_clear(qn_result_t *out)
{
    uint32_t e;
    for (e = 0; e < QN_MAX_EDGES; e++) out->flow[e] = 0;
    out->eliminated = 0;
    out->cycles = 0;
    out->iterations = 0;
}

static qn_status_t qn__finish(const qn_graph_t *g, qn_result_t *out)
{
    qn_status_t st = qn_verify(g, out->flow, &out->eliminated);
    if (st != QN_OK) {
        qn__res_clear(out);
        return QN_ERR_INTERNAL;
    }
    return QN_OK;
}

/* ------------------------------------------------------------------ greedy */

/* Cancels cycles among ws->rem > 0 (in place), adding the cancelled amounts
 * to flow. Adjacency must be built. Each cancellation zeroes at least one
 * edge, so at most num_edges cycles are found. */
static void qn__greedy_core(const qn_graph_t *g, qn_workspace_t *ws, int64_t *flow,
                            uint32_t *cycles, uint32_t *iters)
{
    uint32_t n = g->num_nodes, s, v;
    for (v = 0; v < n; v++) ws->color[v] = 0;
    for (;;) {
        int found = 0;
        (*iters)++;
        for (s = 0; s < n && !found; s++) {
            uint32_t sp = 0;
            if (ws->color[s] != 0) continue;
            ws->stack[sp++] = s;
            ws->color[s] = 1;
            ws->iter[s] = ws->head[s];
            ws->pe[s] = -1;
            while (sp > 0) {
                uint32_t x = ws->stack[sp - 1u], w;
                int32_t e = ws->iter[x];
                while (e >= 0 && ws->rem[e] == 0) e = ws->next[e];
                if (e < 0) {
                    ws->iter[x] = -1;
                    ws->color[x] = 2;
                    sp--;
                    continue;
                }
                ws->iter[x] = ws->next[e];
                w = g->to[e];
                if (ws->color[w] == 0) {
                    ws->color[w] = 1;
                    ws->pe[w] = e;
                    ws->iter[w] = ws->head[w];
                    ws->stack[sp++] = w;
                } else if (ws->color[w] == 1) {
                    int64_t m = ws->rem[e];
                    uint32_t y = x;
                    while (y != w) {
                        int32_t pe = ws->pe[y];
                        if (ws->rem[pe] < m) m = ws->rem[pe];
                        y = g->from[pe];
                    }
                    ws->rem[e] -= m;
                    flow[e] += m;
                    y = x;
                    while (y != w) {
                        int32_t pe = ws->pe[y];
                        ws->rem[pe] -= m;
                        flow[pe] += m;
                        y = g->from[pe];
                    }
                    (*cycles)++;
                    /* Grey nodes go back to white; black nodes reach no
                     * cycle and removing capacity cannot give them one. */
                    while (sp > 0) ws->color[ws->stack[--sp]] = 0;
                    found = 1;
                }
            }
        }
        if (!found) return;
    }
}

qn_status_t qn_greedy_cycles(const qn_graph_t *g, qn_workspace_t *ws, qn_result_t *out)
{
    qn_status_t st;
    uint32_t e;
    if (!g || !ws || !out) return QN_ERR_NULL;
    st = qn_graph_check(g);
    if (st != QN_OK) return st;
    qn__res_clear(out);
    qn__build_adj(g, ws);
    for (e = 0; e < g->num_edges; e++) ws->rem[e] = g->cap[e];
    qn__greedy_core(g, ws, out->flow, &out->cycles, &out->iterations);
    return qn__finish(g, out);
}

/* ------------------------------------------------------------------ exact */

/* Residual arc a: a < m is edge a forward (cost -1, residual cap - f),
 * a >= m is edge a - m backward (cost +1, residual f). */
static uint32_t qn__arc_tail(const qn_graph_t *g, uint32_t a)
{
    return a < g->num_edges ? g->from[a] : g->to[a - g->num_edges];
}

static int64_t qn__arc_res(const qn_graph_t *g, const int64_t *f, uint32_t a)
{
    uint32_t m = g->num_edges;
    return a < m ? g->cap[a] - f[a] : f[a - m];
}

/* One minimum-mean cycle step (Karp). Returns 1 and cancels a cycle if the
 * minimum mean is negative, 0 if no negative cycle exists, <0 on error. */
static int qn__karp_step(const qn_graph_t *g, qn_workspace_t *ws, int64_t *f)
{
    uint32_t n = g->num_nodes, m = g->num_edges, k, v, e;
    int32_t best_num = 0, best_den = 1, bv = -1;
    for (v = 0; v < n; v++) {
        ws->kd[0][v] = 0;
        ws->kp[0][v] = -1;
    }
    for (k = 1; k <= n; k++) {
        for (v = 0; v < n; v++) {
            ws->kd[k][v] = QN_INF32;
            ws->kp[k][v] = -1;
        }
        for (e = 0; e < m; e++) {
            uint32_t u = g->from[e], w = g->to[e];
            if (f[e] < g->cap[e] && ws->kd[k - 1u][u] != QN_INF32 &&
                ws->kd[k - 1u][u] - 1 < ws->kd[k][w]) {
                ws->kd[k][w] = ws->kd[k - 1u][u] - 1;
                ws->kp[k][w] = (int16_t) e;
            }
            if (f[e] > 0 && ws->kd[k - 1u][w] != QN_INF32 && ws->kd[k - 1u][w] + 1 < ws->kd[k][u]) {
                ws->kd[k][u] = ws->kd[k - 1u][w] + 1;
                ws->kp[k][u] = (int16_t) (e + m);
            }
        }
    }
    for (v = 0; v < n; v++) {
        int32_t vn = 0, vd = 1, have = 0;
        if (ws->kd[n][v] == QN_INF32) continue;
        for (k = 0; k < n; k++) {
            int32_t num, den;
            if (ws->kd[k][v] == QN_INF32) continue;
            num = ws->kd[n][v] - ws->kd[k][v];
            den = (int32_t) (n - k);
            if (!have || num * vd > vn * den) {
                vn = num;
                vd = den;
                have = 1;
            }
        }
        if (have && (bv < 0 || vn * best_den < best_num * vd)) {
            best_num = vn;
            best_den = vd;
            bv = (int32_t) v;
        }
    }
    if (bv < 0 || best_num >= 0) return 0;
    /* Walk back along the n-arc walk to bv; the first repeated vertex closes
     * a cycle, and every cycle on this walk has the minimum mean. */
    {
        uint32_t x = (uint32_t) bv, lo = 0, hi = 0, j;
        int found = 0;
        int64_t d = 0;
        int32_t cost = 0;
        for (v = 0; v < n; v++) ws->seen[v] = -1;
        ws->seen[x] = (int32_t) n;
        for (k = n; k >= 1u; k--) {
            int16_t a = ws->kp[k][x];
            if (a < 0) return -1;
            ws->walk[k] = (uint16_t) a;
            x = qn__arc_tail(g, (uint32_t) a);
            if (ws->seen[x] >= 0) {
                lo = k;
                hi = (uint32_t) ws->seen[x];
                found = 1;
                break;
            }
            ws->seen[x] = (int32_t) (k - 1u);
        }
        if (!found) return -1;
        for (j = lo; j <= hi; j++) {
            uint32_t a = ws->walk[j];
            int64_t r = qn__arc_res(g, f, a);
            if (j == lo || r < d) d = r;
            cost += a < m ? -1 : 1;
        }
        if (cost >= 0 || d <= 0) return -1;
        for (j = lo; j <= hi; j++) {
            uint32_t a = ws->walk[j];
            if (a < m)
                f[a] += d;
            else
                f[a - m] -= d;
        }
    }
    return 1;
}

qn_status_t qn_exact_optimum(const qn_graph_t *g, qn_workspace_t *ws, qn_result_t *out)
{
    qn_status_t st;
    if (!g || !ws || !out) return QN_ERR_NULL;
    st = qn_graph_check(g);
    if (st != QN_OK) return st;
    qn__res_clear(out);
    for (;;) {
        int r;
        if (out->iterations >= QN_EXACT_MAX_ITERS) {
            qn__res_clear(out);
            return QN_ERR_LIMIT;
        }
        out->iterations++;
        r = qn__karp_step(g, ws, out->flow);
        if (r < 0) {
            qn__res_clear(out);
            return QN_ERR_INTERNAL;
        }
        if (r == 0) break;
        out->cycles++;
    }
    st = qn__finish(g, out);
    if (st != QN_OK) return st;
    if (qn_check_optimal(g, out->flow, ws) != QN_OK) {
        qn__res_clear(out);
        return QN_ERR_INTERNAL;
    }
    return QN_OK;
}

qn_status_t qn_check_optimal(const qn_graph_t *g, const int64_t *flow, qn_workspace_t *ws)
{
    qn_status_t st;
    uint32_t n, m, pass, e, v;
    if (!g || !flow || !ws) return QN_ERR_NULL;
    st = qn_verify(g, flow, (int64_t *) 0);
    if (st != QN_OK) return st;
    n = g->num_nodes;
    m = g->num_edges;
    for (v = 0; v < n; v++) ws->bf[v] = 0;
    /* Distances from a virtual source joined to every member by a 0 arc.
     * Without a negative cycle they settle within n - 1 passes. */
    for (pass = 0; pass <= n; pass++) {
        int changed = 0;
        for (e = 0; e < m; e++) {
            uint32_t u = g->from[e], w = g->to[e];
            if (flow[e] < g->cap[e] && ws->bf[u] - 1 < ws->bf[w]) {
                ws->bf[w] = ws->bf[u] - 1;
                changed = 1;
            }
            if (flow[e] > 0 && ws->bf[w] + 1 < ws->bf[u]) {
                ws->bf[u] = ws->bf[w] + 1;
                changed = 1;
            }
        }
        if (!changed) return QN_OK;
    }
    return QN_ERR_NOT_OPTIMAL;
}

/* ------------------------------------------------------------------ anneal */

qn_status_t qn_anneal_defaults(qn_anneal_params_t *p)
{
    if (!p) return QN_ERR_NULL;
    p->replicas = 8;
    p->sweeps = 200;
    p->moves_per_sweep = 64;
    p->swap_interval = 1;
    p->max_cycle_len = 6;
    p->polish = 1;
    return QN_OK;
}

static qn_status_t qn__params_check(const qn_anneal_params_t *p)
{
    uint64_t total;
    if (p->replicas == 0 || p->replicas > QN_MAX_REPLICAS) return QN_ERR_PARAM;
    if (p->sweeps == 0 || p->moves_per_sweep == 0 || p->swap_interval == 0) return QN_ERR_PARAM;
    if (p->max_cycle_len < 2u || p->max_cycle_len > QN_CYC_MAX_LEN) return QN_ERR_PARAM;
    if (p->polish > 1u) return QN_ERR_PARAM;
    total = (uint64_t) p->replicas * (uint64_t) p->sweeps;
    if (total > QN_ANNEAL_MAX_MOVES) return QN_ERR_PARAM;
    total *= (uint64_t) p->moves_per_sweep; /* <= 2^32 * 2^32: fits */
    if (total > QN_ANNEAL_MAX_MOVES) return QN_ERR_PARAM;
    return QN_OK;
}

/* 64-bit unsigned division by shift-and-subtract, so 32-bit targets need
 * no libgcc helper (__udivdi3). d must be non-zero. */
static uint64_t qn__udiv64(uint64_t n, uint64_t d, uint64_t *rem)
{
    uint64_t q = 0, r = 0;
    int i;
    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= 1ull << i;
        }
    }
    if (rem) *rem = r;
    return q;
}

/* floor(a * b / c) for a >= 0, 0 <= b < 2^20, c > 0, saturating at 2^40.
 * Very large c is scaled down first (approximate; only used to decide an
 * acceptance probability). */
static uint64_t qn__muldiv_sat(uint64_t a, uint64_t b, uint64_t c)
{
    uint64_t q, r;
    while (c > (1ull << 40)) {
        a >>= 1;
        c >>= 1;
    }
    q = qn__udiv64(a, c, &r);
    if (q >= (1ull << 40)) return 1ull << 40;
    q *= b;
    if (q >= (1ull << 40)) return 1ull << 40;
    q += qn__udiv64(r * b, c, (uint64_t *) 0);
    return q < (1ull << 40) ? q : (1ull << 40);
}

int qn_metropolis_accept(uint64_t *rng, uint64_t x16)
{
    if (!rng) return 0;
    if (x16 >= QN_EXP_TABLE_N) return 0;
    return (qn_rng_next(rng) & 0xFFFFFFull) < (uint64_t) qn_exp_tab[x16];
}

/* Enumerates simple cycles of length <= maxlen with the smallest member as
 * root, in index order, under a cycle cap and a per-root work budget. */
static void qn__enum_cycles(const qn_graph_t *g, qn_workspace_t *ws, uint32_t maxlen)
{
    uint32_t n = g->num_nodes, s, v;
    ws->num_cycles = 0;
    for (v = 0; v < n; v++) ws->onpath[v] = 0;
    for (s = 0; s < n && ws->num_cycles < QN_MAX_CYCLES; s++) {
        uint32_t budget = (QN_MAX_CYCLES - ws->num_cycles) / (n - s);
        uint32_t found = 0, steps = 0, depth = 0;
        if (budget == 0) budget = 1;
        ws->onpath[s] = 1;
        ws->iter[0] = ws->head[s];
        for (;;) {
            int32_t e = ws->iter[depth];
            uint32_t w;
            if (e < 0 || found >= budget || steps >= 65536u) {
                if (depth == 0) break;
                depth--;
                ws->onpath[g->to[ws->walk[depth]]] = 0;
                continue;
            }
            ws->iter[depth] = ws->next[e];
            steps++;
            w = g->to[e];
            if (w == s) {
                uint32_t c = ws->num_cycles, i;
                for (i = 0; i < depth; i++) ws->cyc[c][i] = ws->walk[i];
                ws->cyc[c][depth] = (uint16_t) e;
                ws->cyc_len[c] = (uint8_t) (depth + 1u);
                ws->num_cycles++;
                found++;
            } else if (w > s && !ws->onpath[w] && depth + 1u < maxlen) {
                ws->walk[depth] = (uint16_t) e;
                depth++;
                ws->onpath[w] = 1;
                ws->iter[depth] = ws->head[w];
            }
        }
        ws->onpath[s] = 0;
    }
}

static uint64_t qn__beta(uint32_t r, uint32_t t, uint32_t sweeps)
{
    return ((uint64_t) 1u << r) * (1u + qn__udiv64((uint64_t) 63u * t, sweeps, (uint64_t *) 0));
}

qn_status_t qn_anneal(const qn_graph_t *g, const qn_anneal_params_t *p, uint64_t *rng,
                      qn_workspace_t *ws, qn_result_t *out)
{
    qn_status_t st;
    uint32_t m, e, r, t, i, R;
    int64_t umax = 1, best;
    if (!g || !p || !rng || !ws || !out) return QN_ERR_NULL;
    st = qn_graph_check(g);
    if (st != QN_OK) return st;
    st = qn__params_check(p);
    if (st != QN_OK) return st;
    if (*rng == 0) return QN_ERR_PARAM;
    qn__res_clear(out);
    m = g->num_edges;
    R = p->replicas;
    for (e = 0; e < m; e++)
        if (g->cap[e] > umax) umax = g->cap[e];
    qn__build_adj(g, ws);
    qn__enum_cycles(g, ws, p->max_cycle_len);
    for (r = 0; r < R; r++) {
        for (e = 0; e < m; e++) ws->rflow[r][e] = 0;
        ws->relim[r] = 0;
        ws->perm[r] = r;
    }
    best = 0;
    for (t = 0; t < p->sweeps && ws->num_cycles > 0; t++) {
        for (r = 0; r < R; r++) {
            uint32_t slot = ws->perm[r];
            int64_t *f = ws->rflow[slot];
            uint64_t beta = qn__beta(r, t, p->sweeps);
            for (i = 0; i < p->moves_per_sweep; i++) {
                uint64_t d1 = qn_rng_next(rng), d2 = qn_rng_next(rng);
                uint32_t k = (uint32_t) (d1 >> 32) % ws->num_cycles, len, j;
                int push = (int) (d1 & 1u), full = (int) ((d1 >> 1) & 1u);
                int64_t mx = INT64_MAX, d, gain;
                out->iterations++;
                len = ws->cyc_len[k];
                for (j = 0; j < len; j++) {
                    uint32_t ce = ws->cyc[k][j];
                    int64_t av = push ? g->cap[ce] - f[ce] : f[ce];
                    if (av < mx) mx = av;
                }
                if (mx <= 0) continue;
                if (full) {
                    d = mx;
                } else {
                    uint64_t rm;
                    (void) qn__udiv64(d2, (uint64_t) mx, &rm);
                    d = 1 + (int64_t) rm;
                }
                /* d <= each cap on the cycle, so d * len <= total gross. */
                if (__builtin_mul_overflow(d, (int64_t) len, &gain)) {
                    qn__res_clear(out);
                    return QN_ERR_INTERNAL;
                }
                if (!push && !qn_metropolis_accept(
                                 rng, qn__muldiv_sat((uint64_t) gain, beta, (uint64_t) umax)))
                    continue;
                for (j = 0; j < len; j++) f[ws->cyc[k][j]] += push ? d : -d;
                ws->relim[slot] += push ? gain : -gain;
                out->cycles++;
                if (ws->relim[slot] > best) {
                    best = ws->relim[slot];
                    for (e = 0; e < m; e++) out->flow[e] = f[e];
                }
            }
        }
        if ((t + 1u) % p->swap_interval == 0) {
            for (r = 0; r + 1u < R; r++) {
                uint32_t hot = ws->perm[r], cold = ws->perm[r + 1u];
                int64_t eh = ws->relim[hot], ec = ws->relim[cold];
                int sw = eh >= ec;
                if (!sw) {
                    uint64_t db = qn__beta(r + 1u, t, p->sweeps) - qn__beta(r, t, p->sweeps);
                    sw = qn_metropolis_accept(
                        rng, qn__muldiv_sat((uint64_t) (ec - eh), db, (uint64_t) umax));
                }
                if (sw) {
                    ws->perm[r] = cold;
                    ws->perm[r + 1u] = hot;
                }
            }
        }
    }
    if (p->polish) {
        for (e = 0; e < m; e++) ws->rem[e] = g->cap[e] - out->flow[e];
        qn__greedy_core(g, ws, out->flow, &out->cycles, &out->iterations);
    }
    return qn__finish(g, out);
}
