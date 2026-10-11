/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zf_internal.h — shared between the z-ffi runtime and generator. */
#ifndef ZF_INTERNAL_H
#define ZF_INTERNAL_H

#include "zffi.h"

#if defined(__GNUC__) || defined(__clang__)
#define ZF_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define ZF_PRINTF(a, b)
#endif

void zf_set_errorf(const char *fmt, ...) ZF_PRINTF(1, 2);

#endif /* ZF_INTERNAL_H */
