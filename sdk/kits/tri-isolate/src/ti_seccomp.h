/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ti_seccomp.h — the worker's optional seccomp filter (see ti_seccomp.c). */
#ifndef TI_SECCOMP_H
#define TI_SECCOMP_H

#include <stdint.h>

/* 1 if this build has the filter (Linux x86-64 / arm64), else 0. */
int32_t ti_seccomp_available(void);
/* Install the filter in the calling process: ZXV_OK, ZXV_E_NOTIMPL (not
 * in this build, or the kernel has no seccomp), ZXV_E_STATE. Irreversible. */
int32_t ti_seccomp_install(void);

#endif /* TI_SECCOMP_H */
