/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxp_baremetal.h — bare-metal (super-loop) port extras. */
#ifndef ZXP_BAREMETAL_H
#define ZXP_BAREMETAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The board's monotonic microsecond clock (e.g. a 64-bit extension of a
 * hardware timer). Without one, zxp_monotonic_us() is 0. */
void zxp_baremetal_set_clock(uint64_t (*monotonic_us)(void));

#ifdef __cplusplus
}
#endif

#endif /* ZXP_BAREMETAL_H */
