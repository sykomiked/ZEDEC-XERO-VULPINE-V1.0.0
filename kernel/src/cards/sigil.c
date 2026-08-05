/* sigil.c — the spell shape as a circuit diagram. See sigil.h. */
#include "sigil.h"

void sig_init(sigil_t *s, uint32_t card_index) {
    if (!s) return;
    s->card_index = card_index;
    s->fab_n = 0; s->fab_k = 0;
    s->cols = 0; s->rows = 0; s->pitch = 0;
    s->n_nodes = 0; s->n_edges = 0;
}

int32_t sig_find_node(const sigil_t *s, uint8_t col, uint8_t row) {
    if (!s) return -1;
    for (uint8_t i = 0; i < s->n_nodes; i++)
        if (s->node[i].col == col && s->node[i].row == row) return (int32_t)i;
    return -1;
}

bool sig_add_node(sigil_t *s, uint8_t col, uint8_t row) {
    if (!s || s->n_nodes >= SIG_MAX_NODES) return false;
    if (sig_find_node(s, col, row) >= 0) return false;   /* one node per site */
    s->node[s->n_nodes].col = col;
    s->node[s->n_nodes].row = row;
    s->n_nodes++;
    if (col + 1 > s->cols) s->cols = (uint8_t)(col + 1);
    if (row + 1 > s->rows) s->rows = (uint8_t)(row + 1);
    return true;
}

bool sig_add_edge(sigil_t *s, uint8_t a, uint8_t b) {
    if (!s || s->n_edges >= SIG_MAX_EDGES) return false;
    if (a == b || a >= s->n_nodes || b >= s->n_nodes) return false;
    for (uint8_t i = 0; i < s->n_edges; i++) {          /* no parallel edges */
        uint8_t x = s->edge[i].a, y = s->edge[i].b;
        if ((x == a && y == b) || (x == b && y == a)) return false;
    }
    s->edge[s->n_edges].a = a;
    s->edge[s->n_edges].b = b;
    s->n_edges++;
    return true;
}

uint32_t sig_gcd(uint32_t a, uint32_t b) {
    while (b) { uint32_t t = a % b; a = b; b = t; }
    return a;
}

/* ------------------------------------------------------------ the fabric */
/* A star polygon {N/k} is the circulant graph C(N,{k}). Walking from any
 * node in steps of k returns to the start after N/gcd(N,k) hops, so the
 * figure decomposes into exactly gcd(N,k) disjoint orbits. Those orbits do
 * not share a node, so they are independent: gcd IS the lane count. */
uint32_t sig_fabric_lanes(const sigil_t *s) {
    if (!s || s->fab_n == 0 || s->fab_k == 0) return 0;
    return sig_gcd(s->fab_n, s->fab_k);
}

bool sig_fabric_unicursal(const sigil_t *s) {
    return sig_fabric_lanes(s) == 1u;
}

uint32_t sig_fabric_degree(const sigil_t *s) {
    if (!s || s->fab_n == 0 || s->fab_k == 0) return 0;
    /* +k and -k coincide when 2k == N, so the figure is a set of diameters */
    return (2u * (uint32_t)s->fab_k == (uint32_t)s->fab_n) ? 1u : 2u;
}

/* Worst-case hops within one lane. A lane holds N/gcd nodes joined in a
 * cycle, and a cycle of length L has diameter L/2. */
uint32_t sig_fabric_diameter(const sigil_t *s) {
    uint32_t g = sig_fabric_lanes(s);
    if (!g) return 0;
    uint32_t lane_len = (uint32_t)s->fab_n / g;
    return lane_len / 2u;
}

/* ----------------------------------------------------------- the circuit */
uint32_t sig_degree(const sigil_t *s, uint8_t node) {
    if (!s || node >= s->n_nodes) return 0;
    uint32_t d = 0;
    for (uint8_t i = 0; i < s->n_edges; i++)
        if (s->edge[i].a == node || s->edge[i].b == node) d++;
    return d;
}

uint32_t sig_odd_vertices(const sigil_t *s) {
    if (!s) return 0;
    uint32_t n = 0;
    for (uint8_t i = 0; i < s->n_nodes; i++)
        if (sig_degree(s, i) & 1u) n++;
    return n;
}

uint32_t sig_components(const sigil_t *s) {
    if (!s || s->n_nodes == 0) return 0;
    bool seen[SIG_MAX_NODES];
    for (uint8_t i = 0; i < s->n_nodes; i++) seen[i] = false;
    uint8_t stack[SIG_MAX_NODES];
    uint32_t comps = 0;
    for (uint8_t root = 0; root < s->n_nodes; root++) {
        if (seen[root]) continue;
        comps++;
        uint8_t sp = 0; stack[sp++] = root; seen[root] = true;
        while (sp) {
            uint8_t v = stack[--sp];
            for (uint8_t e = 0; e < s->n_edges; e++) {
                uint8_t w = 0xFF;
                if (s->edge[e].a == v) w = s->edge[e].b;
                else if (s->edge[e].b == v) w = s->edge[e].a;
                if (w != 0xFF && !seen[w]) { seen[w] = true; stack[sp++] = w; }
            }
        }
    }
    return comps;
}

int32_t sig_entry(const sigil_t *s) {
    if (!s || s->n_nodes == 0) return -1;
    for (uint8_t i = 0; i < s->n_nodes; i++)
        if (sig_degree(s, i) == 1u) return (int32_t)i;   /* a loose end */
    return 0;                                            /* closed circuit */
}

/* Dependency waves. A node fires once every neighbour that feeds it has
 * fired; everything in a wave fires together. No clock is consulted — the
 * shape alone decides what may run at the same time. Disconnected parts of
 * the circuit are seeded as their own roots, because a separate component
 * has no dependency on the entry and must not be left unscheduled. */
bool sig_schedule(const sigil_t *s, int32_t entry, sig_schedule_t *out) {
    if (!s || !out || s->n_nodes == 0) return false;
    int32_t e0 = (entry >= 0 && (uint8_t)entry < s->n_nodes) ? entry : sig_entry(s);
    if (e0 < 0) return false;

    bool seen[SIG_MAX_NODES];
    for (uint8_t i = 0; i < s->n_nodes; i++) seen[i] = false;

    out->n_waves = 0; out->n_scheduled = 0; out->width = 0;
    out->wave_start[0] = 0;

    uint8_t frontier[SIG_MAX_NODES], next[SIG_MAX_NODES];
    uint8_t nf = 0;
    frontier[nf++] = (uint8_t)e0; seen[e0] = true;

    for (;;) {
        while (nf) {
            if (out->n_waves >= SIG_MAX_WAVES) return false;
            for (uint8_t i = 0; i < nf; i++) out->order[out->n_scheduled++] = frontier[i];
            if (nf > out->width) out->width = nf;
            out->n_waves++;
            out->wave_start[out->n_waves] = out->n_scheduled;

            uint8_t nn = 0;
            for (uint8_t i = 0; i < nf; i++) {
                uint8_t v = frontier[i];
                for (uint8_t e = 0; e < s->n_edges; e++) {
                    uint8_t w = 0xFF;
                    if (s->edge[e].a == v) w = s->edge[e].b;
                    else if (s->edge[e].b == v) w = s->edge[e].a;
                    if (w != 0xFF && !seen[w]) { seen[w] = true; next[nn++] = w; }
                }
            }
            for (uint8_t i = 0; i < nn; i++) frontier[i] = next[i];
            nf = nn;
        }
        /* seed any component the entry could not reach */
        uint8_t root = 0xFF;
        for (uint8_t i = 0; i < s->n_nodes; i++) if (!seen[i]) { root = i; break; }
        if (root == 0xFF) break;
        seen[root] = true; frontier[0] = root; nf = 1;
    }
    return true;
}
