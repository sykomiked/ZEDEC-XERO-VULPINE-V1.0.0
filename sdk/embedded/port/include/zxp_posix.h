/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxp_posix.h — POSIX host port extras. */
#ifndef ZXP_POSIX_H
#define ZXP_POSIX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The operating system's CSPRNG (getrandom on Linux, getentropy
 * elsewhere), as a zxp_entropy_fn. Opt-in, like every entropy source:
 *   zxp_set_entropy(zxp_posix_entropy, NULL); zxp_init(); */
int zxp_posix_entropy(void *user, uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ZXP_POSIX_H */
