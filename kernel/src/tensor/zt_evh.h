/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zt_evh.h — the paraconsistent evidence head (roadmap 6.6b P2.1, 5.1 M.2).
 *
 * Beside the normal output, two linear probes on a hidden state give two
 * separate degrees of evidence, each in [0, 1]:
 *     mu      evidence FOR what the model is saying,
 *     lambda  evidence AGAINST it.
 * They are not forced to sum to 1, so the pair can say "both" (strong
 * evidence each way: a contradiction) or "neither" (no evidence at all),
 * which a softmax cannot. The pair maps onto lpres's four values with the
 * para-analyser of annotated logic (Da Costa, Abe):
 *     certainty     Gc  = mu - lambda       (-1 .. 1)
 *     contradiction Gct = mu + lambda - 1   (-1 .. 1)
 *     |Gc| >  |Gct|:  TRUE when Gc > 0, else FALSE
 *     |Gc| <= |Gct|:  BOTH when Gct > 0, else NEITHER
 * The four regions are the triangles the unit square's diagonals cut; the
 * centre (no information) is NEITHER and a tie goes to the contradiction
 * axis, so the head never claims TRUE or FALSE on a tie. Base codes are
 * lpres_hex's (LPX_B_FALSE 0, NEITHER 1, BOTH 2, TRUE 3); paradox.h's
 * pdx_classify_answer turns an answer's summary into a paradox class and
 * an action.
 *
 *   E1 THE HEAD.  mu = sigmoid(w_mu . h / 2^sh_mu + b_mu), the same for
 *       lambda; int16 weights, the dot product exact in 64 bits, Q16 out.
 *       h is the final hidden state (zt_model_hidden) or a layer's
 *       residual stream (zt_model_set_tap); n is the model's n_embd.
 *   E2 FITTING (M.2's calibration probe).  zt_evh_fit is logistic
 *       regression by stochastic gradient descent in fixed point (Q24
 *       weights, one pass over the samples in order per epoch, learning
 *       rate 2^-lr_shift), then a rounding to int16 with the largest shift
 *       that fits. mu and lambda are fitted separately, from separate
 *       labels: a sample labelled for and against is BOTH, neither is
 *       NEITHER. zt_evh_score gives the 4x4 confusion on held-out data.
 *   E3 PER ANSWER AND PER SPAN.  An accumulator (zt_evh_acc_t) takes each
 *       generated token's (mu, lambda); its summary is the mean pair, its
 *       four-valued base, per-base token counts, the evidence counts
 *       (tokens with mu >= 1/2, with lambda >= 1/2) now and at the last
 *       power-of-two token count (so the trend covers about the second
 *       half), and whether the engine saturated anywhere. Reset it to
 *       start a span.
 *   E4 ONE ANSWER EVERYWHERE.  Integers only, no allocation, no libc; the
 *       fit and the head give the same bits on every machine.
 *
 * HONEST LIMITS. A linear probe finds what is linearly readable in one
 * hidden state; whether that is "truth" depends entirely on the labelled
 * data it was fitted on, and its accuracy is measured on held-out prompts,
 * never promised. Plain SGD (no regularisation, no shuffling: shuffle the
 * samples first). It lowers confident wrong answers only where the labels
 * taught it to; it does not end hallucinations. The LoRA alternative is
 * P2.3.
 */
#ifndef ZT_EVH_H
#define ZT_EVH_H

#include <stdbool.h>
#include <stdint.h>
#include "zt.h"

enum { ZT_EVH_FALSE = 0, ZT_EVH_NEITHER = 1, ZT_EVH_BOTH = 2, ZT_EVH_TRUE = 3 };
enum { ZT_EVH_EARG = -60, ZT_EVH_ESPACE = -61 };

typedef struct {
    uint32_t n;                 /* input width */
    const int16_t *w_mu, *w_la; /* n weights each */
    uint32_t sh_mu, sh_la;      /* weight = w / 2^sh, 0..40 */
    zt_fx b_mu, b_la;           /* Q16 biases */
} zt_evh_t;

typedef struct {
    zt_fx z_mu, z_la;  /* Q16 logits before the sigmoid (saturated to 32 bits) */
    zt_fx mu, la;      /* Q16, 0 .. ZT_ONE */
    zt_fx cert, contr; /* Q16, -ZT_ONE .. ZT_ONE */
    uint8_t base;      /* ZT_EVH_* */
} zt_evh_out_t;

/* Q16 logistic: 1 / (1 + e^-z), 0 .. ZT_ONE, monotone, and
 * sigmoid(-z) == ZT_ONE - sigmoid(z). */
zt_fx zt_sigmoid(zt_fx z);
/* The para-analyser; cert and contr may be null. */
uint8_t zt_evh_base(zt_fx mu, zt_fx la, zt_fx *cert, zt_fx *contr);
/* The head on one hidden state. */
int32_t zt_evh_apply(const zt_evh_t *e, const zt_fx *h, uint32_t n, zt_evh_out_t *o);

/* ---- fitting ---- */
typedef struct {
    uint32_t epochs;   /* passes over the samples (>= 1) */
    uint32_t lr_shift; /* learning rate 2^-lr_shift (0..40) */
} zt_evh_fit_params_t;

uint64_t zt_evh_fit_work_bytes(uint32_t n);
/* H: ns samples of n Q16 values, row after row. y_mu[i], y_la[i]: 0 or 1.
 * Writes w_mu and w_la (n each) and fills *out to point at them. */
int32_t zt_evh_fit(const zt_fx *H, uint32_t ns, uint32_t n, const uint8_t *y_mu,
                   const uint8_t *y_la, const zt_evh_fit_params_t *p, int16_t *w_mu, int16_t *w_la,
                   zt_evh_t *out, void *work, uint64_t work_bytes);
/* conf[truth][predicted], bases ZT_EVH_*; truth from the labels. Returns
 * the number predicted correctly, or < 0. */
int32_t zt_evh_score(const zt_evh_t *e, const zt_fx *H, uint32_t ns, const uint8_t *y_mu,
                     const uint8_t *y_la, uint32_t conf[4][4]);

/* ---- per answer and per span ---- */
typedef struct {
    uint32_t n;
    uint64_t sum_mu, sum_la;
    uint32_t cnt[4];     /* tokens per base */
    uint32_t pos, neg;   /* tokens with mu >= 1/2, with lambda >= 1/2 */
    uint32_t pos0, neg0; /* the same at the last power-of-two count */
    uint32_t sat;        /* largest saturation count seen */
} zt_evh_acc_t;

typedef struct {
    uint8_t base; /* of the mean pair (NEITHER with no tokens) */
    zt_fx mu, la; /* means */
    zt_fx cert, contr;
    uint32_t n, cnt[4];
    uint32_t pos0, neg0, pos1, neg1; /* evidence counts then and now, for lpx_polarity */
    bool saturated;                  /* the engine clipped a value: treat the evidence as stale */
} zt_evh_sum_t;

void zt_evh_acc_reset(zt_evh_acc_t *a);
void zt_evh_acc_add(zt_evh_acc_t *a, const zt_evh_out_t *o, uint32_t n_sat);
void zt_evh_summary(const zt_evh_acc_t *a, zt_evh_sum_t *s);

/* zt_model_generate's on_hidden hook, hctx a zt_evh_hook_t: applies the
 * head to each generated token and adds it to acc. An input width that
 * does not match the head is ignored (nothing added) and counted. */
typedef struct {
    const zt_evh_t *head;
    zt_evh_acc_t *acc;
    uint32_t skipped;
} zt_evh_hook_t;
void zt_evh_hook(void *hctx, int32_t id, const zt_fx *h, uint32_t n, uint32_t n_sat);

#endif /* ZT_EVH_H */
