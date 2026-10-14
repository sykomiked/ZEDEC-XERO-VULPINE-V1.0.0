/* ascw_golden_internal.h — table layouts shared by ascw_golden.c and the
 * generated ascw_golden_tables.c. See ascw_golden.h.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZXV_ASCW_GOLDEN_INTERNAL_H
#define ZXV_ASCW_GOLDEN_INTERNAL_H

#include "ascw_golden.h"

typedef struct {
    const char *script; /* ISO 15924 */
    int32_t face;
} ascw_script_face_t;

typedef struct {
    const char *tag; /* exact CLDR tag */
    int32_t face;
} ascw_locale_face_t;

typedef struct {
    uint32_t lo, hi;
    int32_t face;
} ascw_range_t;

extern const ascw_face_t ASCW_FACES[];
extern const uint32_t ASCW_FACE_N;
extern const ascw_script_face_t ASCW_SCRIPTS[];
extern const uint32_t ASCW_SCRIPT_N;
extern const ascw_locale_face_t ASCW_LOCALES[];
extern const uint32_t ASCW_LOCALE_N;
extern const ascw_range_t ASCW_RANGES[];
extern const uint32_t ASCW_RANGE_N;

#endif /* ZXV_ASCW_GOLDEN_INTERNAL_H */
