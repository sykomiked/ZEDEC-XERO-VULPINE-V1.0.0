/* chiglet.c — ISF-weighted mixture of small experts. See chiglet.h. */
#include "chiglet.h"

void chg_init(chiglet_t *c, uint32_t caps) {
    if (!c) return;
    c->model.loaded = false;
    c->model.K = c->model.D = c->model.L = 0;
    c->model.epoch = 0;
    c->caps = caps;
    c->inferences = 0;
    c->uncertain_count = 0;
}

chg_status_t chg_load_model(chiglet_t *c, const chg_model_t *m) {
    if (!c || !m) return CHG_ERR_ARG;
    if (m->K == 0 || m->K > CHG_MAX_EXPERTS) return CHG_ERR_ARG;
    if (m->D == 0 || m->D > CHG_DIM)         return CHG_ERR_ARG;
    if (m->L == 0 || m->L > CHG_MAX_LABELS)  return CHG_ERR_ARG;
    /* rollback protection: a model may never be older than the loaded one */
    if (c->model.loaded && m->epoch < c->model.epoch) return CHG_ERR_ARG;

    c->model = *m;
    c->model.loaded = true;
    return CHG_OK;
}

/* dot product in Q32.32 */
static surplus_real_t dot(const surplus_real_t *a, const surplus_real_t *b,
                          uint32_t dim) {
    surplus_real_t s = SR_ZERO;
    for (uint32_t i = 0; i < dim; i++) s = SR_ADD(s, SR_MUL(a[i], b[i]));
    return s;
}

/* ISF interaction parameter, computed WITHOUT normalization.
 *
 * u = 1 - (a.b)^2 / (|a|^2 |b|^2)
 *
 * This form is scale-invariant, so no inverse square root is needed. That
 * matters: an rsqrt here carried ~1e-4 relative error, which is four
 * orders of magnitude larger than the decision margin it was supposed to
 * protect. Computing u directly makes the gate exact to Q32.32 rounding. */
surplus_real_t chg_interaction(const surplus_real_t *a, const surplus_real_t *b,
                               uint32_t dim) {
    if (!a || !b || dim == 0) return SR_ZERO;
    surplus_real_t ab = dot(a, b, dim);
    surplus_real_t na = dot(a, a, dim);
    surplus_real_t nb = dot(b, b, dim);

    /* A zero-magnitude vector carries no direction; treat it as fully
     * redundant (u = 0) rather than dividing by zero. */
    if (na <= SR_ZERO || nb <= SR_ZERO) return SR_ZERO;

    surplus_real_t num = SR_MUL(ab, ab);
    surplus_real_t den = SR_MUL(na, nb);
    if (den <= SR_ZERO) return SR_ZERO;

    surplus_real_t cos2 = SR_DIV(num, den);          /* in [0,1] */
    if (cos2 > SR_ONE) cos2 = SR_ONE;                /* clamp rounding */
    if (cos2 < SR_ZERO) cos2 = SR_ZERO;
    return SR_SUB(SR_ONE, cos2);
}

/* Effective number of independent experts.
 *
 * STEP 1 — COLLAPSE DUPLICATES. Any pair whose interaction is below
 * CHG_U_MERGE names the same direction; they are unioned into one group.
 * Each group contributes ONE representative. This is the safeguard that
 * makes the gate robust to duplication: R is computed over DISTINCT
 * directions, so cloning an expert adds nothing.
 *
 * STEP 2 — participation ratio over the representatives:
 *      R = n^2 / SUM_ij cos2_ij      where cos2 = 1 - u
 * R = 1 when all representatives are collinear, R = n when mutually
 * orthogonal. (Without step 1 this quantity is NOT monotone under
 * duplication — see the counterexample in the header.) */
surplus_real_t chg_effective_experts(const surplus_real_t ev[][CHG_DIM],
                                     uint32_t k, uint32_t dim,
                                     uint32_t *distinct_out) {
    if (distinct_out) *distinct_out = 0;
    if (!ev || k == 0 || dim == 0) return SR_ZERO;
    if (k > CHG_MAX_EXPERTS) k = CHG_MAX_EXPERTS;

    /* union-find over "same direction" */
    uint32_t group[CHG_MAX_EXPERTS];
    for (uint32_t i = 0; i < k; i++) group[i] = i;
    for (uint32_t i = 0; i < k; i++) {
        for (uint32_t j = i + 1; j < k; j++) {
            surplus_real_t u = chg_interaction(ev[i], ev[j], dim);
            if (u < CHG_U_MERGE) {
                /* same direction: merge j's group into i's */
                uint32_t gi = group[i], gj = group[j];
                if (gi != gj) {
                    uint32_t lo = (gi < gj) ? gi : gj;
                    uint32_t hi = (gi < gj) ? gj : gi;
                    for (uint32_t t = 0; t < k; t++)
                        if (group[t] == hi) group[t] = lo;
                }
            }
        }
    }

    /* one representative per group */
    uint32_t rep[CHG_MAX_EXPERTS], n = 0;
    for (uint32_t i = 0; i < k; i++) {
        if (group[i] == i) rep[n++] = i;
    }
    if (distinct_out) *distinct_out = n;
    if (n == 0) return SR_ZERO;
    if (n == 1) return SR_ONE;                 /* one direction => R = 1 */

    /* participation ratio over representatives */
    surplus_real_t sum = SR_ZERO;
    for (uint32_t a = 0; a < n; a++) {
        for (uint32_t b = 0; b < n; b++) {
            surplus_real_t u = (a == b) ? SR_ZERO
                                        : chg_interaction(ev[rep[a]], ev[rep[b]], dim);
            surplus_real_t cos2 = SR_SUB(SR_ONE, u);   /* a==b => 1 */
            sum = SR_ADD(sum, cos2);
        }
    }
    if (sum <= SR_ZERO) return SR_ONE;
    surplus_real_t n2 = SR_MUL(SR_FROM_INT((int64_t)n), SR_FROM_INT((int64_t)n));
    surplus_real_t R = SR_DIV(n2, sum);
    if (R < SR_ONE) R = SR_ONE;                        /* R >= 1 always */
    if (R > SR_FROM_INT((int64_t)n)) R = SR_FROM_INT((int64_t)n);
    return R;
}

chg_status_t chg_infer(chiglet_t *c, const surplus_real_t ev[][CHG_DIM],
                       uint32_t k, chg_result_t *out) {
    if (!c || !out) return CHG_ERR_ARG;

    /* capability gate: the runtime does nothing it was not granted */
    if (!(c->caps & CHG_CAP_INFER)) return CHG_ERR_DENIED;

    /* zero the result so no stale field is ever read as meaningful */
    out->state = CHG_UNAVAILABLE;
    out->reason = CHG_REASON_NONE;
    out->label = 0;
    out->k_valid = 0; out->k_distinct = 0;
    out->R = SR_ZERO; out->S = SR_ZERO; out->u_bar = SR_ZERO;
    out->margin = SR_ZERO;
    for (uint32_t i = 0; i < CHG_MAX_EXPERTS; i++) out->w[i] = SR_ZERO;
    for (uint32_t i = 0; i < CHG_MAX_LABELS; i++) out->score[i] = SR_ZERO;

    if (!c->model.loaded) { out->reason = CHG_REASON_NO_MODEL; return CHG_OK; }
    if (!ev || k == 0)    { out->reason = CHG_REASON_NO_EVIDENCE; return CHG_OK; }
    if (k > CHG_MAX_EXPERTS) k = CHG_MAX_EXPERTS;

    const uint32_t dim = c->model.D;
    c->inferences++;

    /* count experts that carry any evidence at all */
    uint32_t valid = 0;
    for (uint32_t i = 0; i < k; i++)
        if (dot(ev[i], ev[i], dim) > SR_ZERO) valid++;
    out->k_valid = valid;
    if (valid == 0) {
        out->state = CHG_UNCERTAIN;
        out->reason = CHG_REASON_NO_EVIDENCE;
        c->uncertain_count++;
        return CHG_OK;
    }

    /* mean pairwise independence, for reporting */
    if (k > 1) {
        surplus_real_t acc = SR_ZERO; uint32_t pairs = 0;
        for (uint32_t i = 0; i < k; i++)
            for (uint32_t j = i + 1; j < k; j++) {
                acc = SR_ADD(acc, chg_interaction(ev[i], ev[j], dim));
                pairs++;
            }
        if (pairs) out->u_bar = SR_DIV(acc, SR_FROM_INT((int64_t)pairs));
    }

    /* THE GATE: effective independent experts, duplication-robust */
    uint32_t distinct = 0;
    out->R = chg_effective_experts(ev, k, dim, &distinct);
    out->k_distinct = distinct;
    /* ensemble surplus in nats — ISF's f() evaluated at the effective count */
    out->S = (out->R > SR_ZERO) ? SR_LN(out->R) : SR_ZERO;

    /* ISF weights: an expert's weight is g(u) against the rest, so evidence
     * that duplicates its peers is discounted. */
    for (uint32_t i = 0; i < k; i++) {
        if (dot(ev[i], ev[i], dim) <= SR_ZERO) { out->w[i] = SR_ZERO; continue; }
        surplus_real_t acc = SR_ZERO; uint32_t pairs = 0;
        for (uint32_t j = 0; j < k; j++) {
            if (i == j) continue;
            acc = SR_ADD(acc, chg_interaction(ev[i], ev[j], dim));
            pairs++;
        }
        surplus_real_t u_i = pairs ? SR_DIV(acc, SR_FROM_INT((int64_t)pairs)) : SR_ONE;
        /* g(u) = 1 + (N-1)u — the ISF effective count (Paper A, Thm 2.1) */
        uint32_t N = distinct ? distinct : 1;
        out->w[i] = SR_ADD(SR_ONE,
                           SR_MUL(SR_FROM_INT((int64_t)(N - 1)), u_i));
    }

    /* mixture vector, then score each label prototype */
    surplus_real_t mix[CHG_DIM];
    for (uint32_t d = 0; d < dim; d++) mix[d] = SR_ZERO;
    surplus_real_t wsum = SR_ZERO;
    for (uint32_t i = 0; i < k; i++) wsum = SR_ADD(wsum, out->w[i]);
    if (wsum <= SR_ZERO) {
        out->state = CHG_UNCERTAIN;
        out->reason = CHG_REASON_NO_EVIDENCE;
        c->uncertain_count++;
        return CHG_OK;
    }
    for (uint32_t i = 0; i < k; i++) {
        if (out->w[i] <= SR_ZERO) continue;
        surplus_real_t wn = SR_DIV(out->w[i], wsum);
        for (uint32_t d = 0; d < dim; d++)
            mix[d] = SR_ADD(mix[d], SR_MUL(wn, ev[i][d]));
    }
    for (uint32_t l = 0; l < c->model.L; l++)
        out->score[l] = dot(mix, c->model.proto[l], dim);

    /* argmax and runner-up */
    uint32_t top = 0, second = 0;
    for (uint32_t l = 1; l < c->model.L; l++)
        if (out->score[l] > out->score[top]) top = l;
    bool have_second = false;
    for (uint32_t l = 0; l < c->model.L; l++) {
        if (l == top) continue;
        if (!have_second || out->score[l] > out->score[second]) {
            second = l; have_second = true;
        }
    }
    out->margin = have_second ? SR_SUB(out->score[top], out->score[second])
                              : out->score[top];

    /* ---- the two abstention rules ---- */
    if (out->R < c->model.R_min) {
        out->state = CHG_UNCERTAIN;
        out->reason = CHG_REASON_REDUNDANT;   /* evidence is not independent */
        c->uncertain_count++;
        return CHG_OK;
    }
    if (out->margin < c->model.margin_min) {
        out->state = CHG_UNCERTAIN;
        out->reason = CHG_REASON_LOW_MARGIN;  /* cannot separate the top two */
        c->uncertain_count++;
        return CHG_OK;
    }

    out->state = CHG_DECIDED;
    out->reason = CHG_REASON_NONE;
    out->label = top;
    return CHG_OK;
}

const char *chg_state_name(chg_state_t s) {
    switch (s) {
        case CHG_UNAVAILABLE: return "UNAVAILABLE";
        case CHG_DECIDED:     return "DECIDED";
        case CHG_UNCERTAIN:   return "UNCERTAIN";
        default:              return "?";
    }
}
const char *chg_reason_name(chg_reason_t r) {
    switch (r) {
        case CHG_REASON_NONE:        return "-";
        case CHG_REASON_NO_MODEL:    return "no model loaded";
        case CHG_REASON_NO_EVIDENCE: return "no evidence";
        case CHG_REASON_REDUNDANT:   return "evidence not independent enough";
        case CHG_REASON_LOW_MARGIN:  return "top two scores too close";
        default:                     return "?";
    }
}

/* ---- DECLARATION -----------------------------------------------------------

 * The native AI runtime. concord.o (chg_interaction, chg_effective_experts)
 * and holodeck/swarm.o (chg_effective_experts) both reach for it -- which is
 * why the capability had to exist before social_spaces and viewing could
 * declare honestly.
 */
#include "zxv_decl.h"
ZXV_DECLARE(chiglet,
    ZXV_PROVIDES(chiglet_ready),
    ZXV_REQUIRES_NONE,
    ZXV_NO_BRINGUP);
