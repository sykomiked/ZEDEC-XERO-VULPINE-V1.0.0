/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxp_zephyr.h — Zephyr port extras. */
#ifndef ZXP_ZEPHYR_H
#define ZXP_ZEPHYR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* sys_csrand_get as a zxp_entropy_fn (CONFIG_ZXV_ENTROPY_CSPRNG, which
 * needs a real entropy driver behind the zephyr,entropy chosen node). */
int zxp_zephyr_entropy_csrand(void *user, uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ZXP_ZEPHYR_H */
