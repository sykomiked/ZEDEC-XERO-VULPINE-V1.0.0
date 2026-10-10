/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_residual_tap.h — where the tensor engine's per-shell residuals enter the
 * residual carrier.
 *
 * kernel/src/tensor/zt.h (T15) provides
 *     typedef struct { int32_t acc[10]; } zt_shell_tap_t;
 *     void zt_set_shell_tap(zt_shell_tap_t *tap);
 * While a tap is set, zt_holo_encode adds floor(delta / 256) to acc[s] for
 * every place of shell s, saturating. zt_residual_tap_t has exactly that
 * layout (checked by _Static_assert in zt_residual_tap.c).
 *
 * Why a second name: when this was written, zt.h's T14 truth enumerators
 * were also called ZT_TRUTH_* (with other values), so the two headers could
 * not share a translation unit. zt.h now names them ZT_COIL_*; the wrapper
 * stays so that users of the carrier need not include the tensor engine.
 * This header includes neither.
 */
#ifndef ZT_RESIDUAL_TAP_H
#define ZT_RESIDUAL_TAP_H

#include <stdint.h>
#include <stdbool.h>

#define ZT_RESIDUAL_SHELLS 10u

typedef struct {
    int32_t acc[ZT_RESIDUAL_SHELLS]; /* already divided by 256 by the engine */
} zt_residual_tap_t;

/* Point the tensor engine's shell tap at `tap` (NULL detaches). Link
 * zt_residual_tap.c and the tensor engine (zt_holo.c) to use it. */
void zt_residual_tap_attach(zt_residual_tap_t *tap);

#endif /* ZT_RESIDUAL_TAP_H */
