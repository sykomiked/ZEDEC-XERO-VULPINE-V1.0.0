/* chiglet.h — ZXV native AI runtime: ISF-weighted mixture of small experts
 *
 * WHAT IT IS, PLAINLY
 * -------------------
 * NOT an LLM. NOT a neural network in any useful sense. It is a bounded
 * mixture of tiny linear experts whose combination weights come from the
 * Interaction Surplus Framework: an expert is weighted by how ORTHOGONAL
 * its evidence is to every other expert's, so redundant experts are
 * discounted and genuinely independent evidence dominates. ISF's g(u)
 * replaces softmax.
 *
 * THE CLAIM
 * ---------
 *   The Chiglet decides by how much genuinely INDEPENDENT evidence it
 *   has, not by how loudly its predictors agree.
 *
 * Eight copies of one expert collapse to effective count R = 1 and the
 * runtime returns an explicit UNCERTAIN, where a softmax gate would
 * report eight-fold confidence. That inversion is the whole product.
 *
 * THE THEOREM, STATED CORRECTLY (this cost us a design round)
 * ----------------------------------------------------------
 * R is the participation ratio of the evidence Gram spectrum:
 *
 *      R = K'^2 / SUM_ij (e_i . e_j)^2
 *
 *   * R = 1  under total collinearity  (all evidence identical)
 *   * R = K' under mutual orthogonality (all evidence independent)
 *   * R is **NOT monotone under duplication in general.**
 *
 * That last line is not a caveat, it is a security-critical fact. An
 * earlier version of this design claimed "cloning an expert cannot
 * manufacture confidence". That is FALSE. Counterexample, verified and
 * kept as a regression test in test_chiglet.c:
 *
 *      K'=4, e1 orthogonal to e2=e3=e4 (three collinear):  R = 4/2.5 = 1.600
 *      clone e1 -> K'=5:                                   R = 5/2.6 = 1.923
 *
 * R *increased* by 20%. Because `R >= R_min` is the gate that permits a
 * DECIDED verdict, an adversary who can register experts could duplicate
 * the most independent one to flip UNCERTAIN into DECIDED — precisely the
 * attack this subsystem exists to prevent.
 *
 * THE FIX (implemented, not merely noted): before computing R, evidence
 * directions are COLLAPSED INTO GROUPS. Any pair with u_ij below
 * CHG_U_MERGE is treated as the same direction, and each group
 * contributes exactly one member to K'. R therefore gates on DISTINCT
 * directions, which duplication cannot inflate.
 *
 * NUMERICS
 * --------
 * u_ij is scale-invariant, so it is computed from the UNNORMALIZED
 * vectors:  u_ij = 1 - (a.b)^2 / (|a|^2 |b|^2). No square root appears
 * anywhere in the gate. This was the other correction from review: an
 * inverse-sqrt in the gate carried ~1e-4 relative error, four orders of
 * magnitude worse than the decision margin it had to protect. Removing it
 * makes the gate exact up to Q32.32 rounding.
 *
 * BOUNDS: everything is statically sized. No allocation, no libc, no
 * floating point, no clock. Inference is a fixed number of operations.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV Chiglet slice)
 * License: SEL-3.3
 */
#ifndef ZXV_CHIGLET_H
#define ZXV_CHIGLET_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"

#define CHG_MAX_EXPERTS   8u
#define CHG_DIM           8u      /* evidence dimension */
#define CHG_MAX_LABELS    8u
#define CHG_NAME_LEN     16u

/* Two experts whose evidence differs by less than this are the SAME
 * direction for gating purposes. This is the duplication safeguard. */
#define CHG_U_MERGE      SR_FROM_FLOAT(0.02)

typedef enum {
    CHG_OK            =  0,
    CHG_ERR_ARG       = -1,
    CHG_ERR_NO_MODEL  = -2,
    CHG_ERR_DENIED    = -3    /* capability check refused the action */
} chg_status_t;

typedef enum {
    CHG_UNAVAILABLE = 0,      /* no model loaded — the runtime does nothing */
    CHG_DECIDED     = 1,
    CHG_UNCERTAIN   = 2       /* first-class "I don't know" */
} chg_state_t;

typedef enum {
    CHG_REASON_NONE = 0,
    CHG_REASON_NO_MODEL,
    CHG_REASON_NO_EVIDENCE,
    CHG_REASON_REDUNDANT,     /* R < R_min: evidence not independent enough */
    CHG_REASON_LOW_MARGIN     /* top-2 scores too close to separate */
} chg_reason_t;

/* A model: K experts x D evidence dims, L labelled prototypes. */
typedef struct {
    uint32_t K, D, L;
    uint32_t epoch;                                   /* rollback counter */
    surplus_real_t proto[CHG_MAX_LABELS][CHG_DIM];    /* label prototypes */
    surplus_real_t R_min;                             /* min effective experts */
    surplus_real_t margin_min;                        /* min top1-top2 gap */
    char label_name[CHG_MAX_LABELS][CHG_NAME_LEN];
    bool loaded;
} chg_model_t;

typedef struct {
    chg_state_t  state;
    chg_reason_t reason;
    uint32_t     label;          /* meaningful only when state == DECIDED */

    uint32_t k_valid;            /* experts that produced evidence */
    uint32_t k_distinct;         /* DISTINCT directions after merging */
    surplus_real_t R;            /* ISF effective independent experts */
    surplus_real_t S;            /* ensemble surplus in nats: S = ln R */
    surplus_real_t u_bar;        /* mean pairwise independence */
    surplus_real_t margin;       /* top1 - top2 */

    surplus_real_t w[CHG_MAX_EXPERTS];   /* ISF gate weights */
    surplus_real_t score[CHG_MAX_LABELS];
} chg_result_t;

typedef struct {
    chg_model_t model;
    uint32_t    caps;            /* capability bitmask granted to the runtime */
    uint64_t    inferences;
    uint64_t    uncertain_count;
} chiglet_t;

/* capability bits — the runtime may do nothing it was not granted */
#define CHG_CAP_INFER    (1u << 0)
#define CHG_CAP_ADVISE   (1u << 1)

void chg_init(chiglet_t *c, uint32_t caps);

/* Load a model. `epoch` must not go backwards (rollback protection).
 * In the full system the bytes arrive via zsp_verify(); this entry point
 * takes an already-authenticated model so the core stays independent of
 * any particular signature scheme. */
chg_status_t chg_load_model(chiglet_t *c, const chg_model_t *m);

/* ISF interaction parameter between two evidence vectors, computed from
 * UNNORMALIZED input: u = 1 - (a.b)^2 / (|a|^2 |b|^2), in [0,1].
 * 0 = parallel/redundant, 1 = orthogonal/complementary. */
surplus_real_t chg_interaction(const surplus_real_t *a, const surplus_real_t *b,
                               uint32_t dim);

/* Effective number of INDEPENDENT experts, after collapsing near-duplicate
 * directions. This is the quantity the DECIDED gate depends on, and the
 * merge step is what makes it robust to duplication. */
surplus_real_t chg_effective_experts(const surplus_real_t ev[][CHG_DIM],
                                     uint32_t k, uint32_t dim,
                                     uint32_t *distinct_out);

/* Run inference over k evidence vectors. Requires CHG_CAP_INFER. */
chg_status_t chg_infer(chiglet_t *c, const surplus_real_t ev[][CHG_DIM],
                       uint32_t k, chg_result_t *out);

const char *chg_state_name(chg_state_t s);
const char *chg_reason_name(chg_reason_t r);

#endif /* ZXV_CHIGLET_H */
