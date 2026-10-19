/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_answer_tag.h — the 64-state tag (lpres_hex.h) on one model answer.
 *
 *   T1 WHAT IT ATTESTS. The tag says how the computation went, never whether
 *      the reply is true: model output stays advisory. Base TRUE means the
 *      forward pass finished cleanly and produced tokens.
 *   T2 FORBIDDEN. Any logit on the Q16 rails (saturated arithmetic) or a
 *      failed forward pass makes the answer forbidden (base NEITHER): the
 *      action is ROLLBACK, so the host withholds the reply and records it in
 *      the quarantine log. Reasons: ZXV_TAG_R_SATURATED, ZXV_TAG_R_FORWARD.
 *   T3 POLARITY. From the confidence trend: the mean margin of the second
 *      half of the reply against the first half. Growing by more than an
 *      eighth: negentropic (converging); shrinking by more than an eighth:
 *      entropic (the model is losing confidence, the action becomes WAIT);
 *      otherwise, or with fewer than two tokens, neutral.
 *   T4 PHASE. A clean reply is MANIFEST, reached one Gray step at a time; a
 *      forbidden one stops at EMERGING; no tokens stays POTENTIAL.
 */
#ifndef ZXV_ANSWER_TAG_H
#define ZXV_ANSWER_TAG_H

#include <stdint.h>
#include "lpres_hex.h"
#include "zxv_model_host.h"

enum { ZXV_TAG_R_NONE = 0, ZXV_TAG_R_SATURATED = 1, ZXV_TAG_R_FORWARD = 2 };

/* The tag for one verdict; *reason (may be NULL) gets the T2 reason. */
lpx_t zxv_answer_tag(const zxv_zt_verdict_t *v, uint8_t *reason);

#endif /* ZXV_ANSWER_TAG_H */
