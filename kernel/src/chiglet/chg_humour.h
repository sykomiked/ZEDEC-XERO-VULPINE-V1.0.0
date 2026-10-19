/* chg_humour.h — the Chiglet's sense of humour: deterministic, bounded,
 * integer-seeded quips tied to inference outcomes. See chg_humour.c.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * A machine that decides things should be allowed to be light about it.
 * The quip engine maps an inference OUTCOME (state + reason + margin) to a
 * short, deterministic line — the same outcome always gives the same quip,
 * so the humour is reproducible like everything else in the kernel. The
 * pick is a pure function of (outcome, tick, phase): no RNG, no wall clock,
 * no float. The quips are modest and self-deprecating rather than clever at
 * the user's expense; the Chiglet never jokes about money, identity or
 * safety states.
 */
#ifndef ZXV_CHG_HUMOUR_H
#define ZXV_CHG_HUMOUR_H

#include <stdint.h>
#include <stdbool.h>

#include "chiglet.h"      /* chg_result_t — the outcome the quip maps       */

/* ---- bounds ---------------------------------------------------------------- */

#define CHG_QUIP_LEN 64u

/* The quip for an inference result. `tick` and `phase` select among the
 * variants for an outcome deterministically. Returns a static string (never
 * NULL). Money, identity and safety outcomes always return a neutral line. */
const char *chg_quip(const chg_result_t *r, uint64_t tick, uint32_t phase);

/* The quip for a plain state (no full result), for the shell's idle line. */
const char *chg_quip_state(chg_state_t s, uint64_t tick);

#endif /* ZXV_CHG_HUMOUR_H */