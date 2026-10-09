/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_residual_tap.c — the one harmonic file that includes tensor/zt.h. See
 * zt_residual_tap.h. */
#include <stddef.h>
#include "../tensor/zt.h"
#include "zt_residual_tap.h"

_Static_assert(sizeof(zt_residual_tap_t) == sizeof(zt_shell_tap_t), "tap layout");
_Static_assert(offsetof(zt_residual_tap_t, acc) == offsetof(zt_shell_tap_t, acc), "tap layout");
_Static_assert(sizeof(((zt_residual_tap_t *) 0)->acc) == sizeof(((zt_shell_tap_t *) 0)->acc),
               "tap layout");

void zt_residual_tap_attach(zt_residual_tap_t *tap)
{
    /* Same layout; the engine only touches tap->acc[s], int32_t objects. */
    zt_set_shell_tap((zt_shell_tap_t *) (void *) tap);
}
