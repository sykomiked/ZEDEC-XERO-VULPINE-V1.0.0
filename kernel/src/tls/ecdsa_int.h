/* ecdsa_int.h — the curve arithmetic ecdsa.c shares with ecdsa_sign.c.
 * Not an API: points are projective, coordinates in Montgomery form.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZXV_TLS_ECDSA_INT_H
#define ZXV_TLS_ECDSA_INT_H

#include "ecdsa.h"
#include "bn.h"

#define EC_LIMBS 12u /* P-384 */

typedef struct {
    uint32_t len; /* field bytes */
    const uint8_t *p, *b, *n, *gx, *gy;
} curve_t;

typedef struct {
    uint32_t x[EC_LIMBS], y[EC_LIMBS], z[EC_LIMBS];
} pt_t;

/* the curve's constants, its field context f and b in Montgomery form */
bool ec_setup(ecdsa_curve_t curve, curve_t *cv, bn_mont_t *f, uint32_t *bm);
/* complete addition (Renes-Costello-Batina, a = -3); r may alias p or q */
void ec_pt_add(const bn_mont_t *f, const uint32_t *bm, pt_t *r, const pt_t *p, const pt_t *q);
/* an affine point, checked to be on the curve */
bool ec_pt_load(const bn_mont_t *f, const uint32_t *bm, const curve_t *cv, pt_t *r,
                const uint8_t *x_be, const uint8_t *y_be);

#endif
