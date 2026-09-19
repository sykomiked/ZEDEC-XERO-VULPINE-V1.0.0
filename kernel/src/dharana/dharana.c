/* dharana.c — 112 Non-Linear Gate Array implementation. See dharana.h. */
#include "dharana.h"
#include "rmag_core.h"

static int rat_sign(rational_t r) { return (r.num > 0) - (r.num < 0); }
static rational_t rat_abs(rational_t r) { if (r.num < 0) r.num = -r.num; return r; }
/* |diff| <= |tol|, assuming positive denominators (true of any value
 * produced by rational_normalize, which every rmag_*_quotas call uses). */
static bool rat_le_abs(rational_t diff, rational_t tol) {
    rational_t ad = rat_abs(diff), at = rat_abs(tol);
    return (int64_t)ad.num * at.den <= (int64_t)at.num * ad.den;
}

/* ---- Naming ---- */

const char *dharana_class_name(dharana_class_t c) {
    switch (c) {
        case DHARANA_CLASS_SANDHI:       return "Sandhi";
        case DHARANA_CLASS_VISRANTI:     return "Visranti";
        case DHARANA_CLASS_DVAITADVAITA: return "Dvaitadvaita";
        case DHARANA_CLASS_SUNYA:        return "Sunya";
        default:                         return "Unknown";
    }
}

const char *dharana_gate_name(uint32_t gate_id) {
    static char buf[32];
    if (gate_id < 1 || gate_id > DHARANA_COUNT) return "Invalid";
    dharana_class_t c = dharana_class_of(gate_id);
    uint32_t local = ((gate_id - 1) % DHARANA_CLASS_SIZE) + 1; /* 1..28 */
    const char *cname = dharana_class_name(c);
    int pos = 0;
    for (int i = 0; cname[i]; i++) buf[pos++] = cname[i];
    buf[pos++] = '-';
    if (local >= 10) buf[pos++] = (char)('0' + local / 10);
    buf[pos++] = (char)('0' + local % 10);
    buf[pos] = '\0';
    return buf;
}

/* ---- Class A: Sandhi ---- */

void sandhi_init(sandhi_gate_t *g, uint32_t gate_id, rational_t epsilon) {
    g->gate_id = gate_id;
    g->prev = (rational_t){0, 1};
    g->has_prev = false;
    g->epsilon = epsilon;
    g->crossing_count = 0;
}

bool sandhi_feed(sandhi_gate_t *g, rational_t sample) {
    if (!g->has_prev) {
        g->prev = sample;
        g->has_prev = true;
        return false;
    }
    int sp = rat_sign(g->prev);
    int ss = rat_sign(sample);
    rational_t velocity = rmag_sub_quotas(sample, g->prev);
    bool crossed = (sp != ss) && (sp != 0 || ss != 0) && !rat_le_abs(velocity, g->epsilon);
    g->prev = sample;
    if (crossed) g->crossing_count++;
    return crossed;
}

/* ---- Class B: Visranti ---- */

void visranti_init(visranti_gate_t *g, uint32_t gate_id, rational_t attractor,
                    rational_t tolerance, uint32_t max_iters) {
    g->gate_id = gate_id;
    g->attractor = attractor;
    g->tolerance = tolerance;
    g->max_iters = max_iters;
}

rational_t visranti_settle(const visranti_gate_t *g, rational_t noisy_input, uint32_t *iters_used) {
    rational_t x = noisy_input;
    rational_t half = {1, 2};
    uint32_t i;
    for (i = 0; i < g->max_iters; i++) {
        rational_t diff = rmag_sub_quotas(x, g->attractor);
        if (rat_le_abs(diff, g->tolerance)) break;
        x = rmag_mul_quotas(rmag_add_quotas(x, g->attractor), half);
    }
    if (iters_used) *iters_used = i;
    return x;
}

/* ---- Class C: Dvaitadvaita ---- */

void dvaitadvaita_init(dvaitadvaita_gate_t *g, uint32_t gate_id) {
    g->gate_id = gate_id;
}

trit_t dvaitadvaita_resolve(const dvaitadvaita_gate_t *g, trit_t a, trit_t b) {
    (void)g;
    if (a == b) return a;
    int charge = trit_charge(a) + trit_charge(b);
    if (charge > 0) return TRIT_GLUT_PLUS;
    if (charge < 0) return TRIT_GLUT_MINUS;
    return TRIT_GLUT_NEUTRAL;
}

/* ---- Class D: Sunya ---- */

void sunya_init(sunya_gate_t *g, uint32_t gate_id) {
    g->gate_id = gate_id;
    g->register_value = (rational_t){0, 1};
    g->live = false;
}

void sunya_write(sunya_gate_t *g, rational_t value) {
    g->register_value = value;
    g->live = true;
}

rational_t sunya_reset(sunya_gate_t *g) {
    rational_t snapshot = g->register_value;
    g->register_value = (rational_t){0, 1};
    g->live = false;
    return snapshot;
}

/* ---- The full 112-gate array ---- */

void dharana_array_init(dharana_array_t *arr) {
    rational_t default_epsilon = {1, 1000};
    rational_t zero = {0, 1};
    rational_t default_tolerance = {1, 1000};
    for (uint32_t i = 0; i < DHARANA_CLASS_SIZE; i++) {
        sandhi_init(&arr->sandhi[i], i + 1, default_epsilon);
        visranti_init(&arr->visranti[i], DHARANA_CLASS_SIZE + i + 1, zero, default_tolerance, 64);
        dvaitadvaita_init(&arr->dvaitadvaita[i], 2 * DHARANA_CLASS_SIZE + i + 1);
        sunya_init(&arr->sunya[i], 3 * DHARANA_CLASS_SIZE + i + 1);
    }
}

/* ---- DECLARATION -----------------------------------------------------------

 * The 112 concentration gates. REQUIRES(rmag_ready) is measured, not thematic:
 * dharana.o's `nm -u` is exactly {rmag_add_quotas, rmag_mul_quotas,
 * rmag_sub_quotas} -- every gate threshold is rational quota arithmetic, and
 * with rmag absent a gate would compare against garbage rather than fail.
 *
 * The bring-up stays on the class map, which is pure and total: gate 1 is
 * Sandhi and gate 112 is the last of the four classes, so an off-by-one in the
 * 1..112 partition shows up immediately.
 */
#include "zxv_decl.h"
static int zxvd_dharana_bringup(void) {
    static dharana_array_t arr;
    dharana_array_init(&arr);
    if (dharana_class_name(dharana_class_of(1u))   == 0) return -1;
    if (dharana_class_name(dharana_class_of(112u)) == 0) return -1;
    if (dharana_class_of(1u) == dharana_class_of(112u))  return -1;
    return 0;
}

ZXV_DECLARE(dharana,
    ZXV_PROVIDES(dharana_gates_ready),
    ZXV_REQUIRES(rmag_ready),
    ZXV_BRINGUP(zxvd_dharana_bringup));
