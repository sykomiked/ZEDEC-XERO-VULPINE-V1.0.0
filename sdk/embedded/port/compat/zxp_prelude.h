/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxp_prelude.h — force-included (-include zxp_prelude.h) into every libzxv
 * and kernel object of the EMBEDDED build only, together with
 * -I port/compat (whose pthread.h shadows the system one). It routes the
 * libc/OS facilities libzxv uses to the port layer WITHOUT editing libzxv:
 *
 *   malloc calloc realloc free strdup  -> zxp_* (counted, limited, hookable)
 *   pthread mutex / cond               -> zxp_mutex_* / zxp_cond_* (compat/pthread.h)
 *   getentropy (zxv_random)            -> zxp_getentropy (fail closed);
 *                                         the build defines ZXV_RANDOM_GETENTROPY
 *   _Thread_local                      -> nothing, when ZXP_NO_TLS is set
 *                                         (single-owner mode, see zxp.h)
 *
 * The cleaner way is an OS-hook header inside libzxv itself; that is
 * proposed in ../../PROPOSED_LIBZXV_CHANGES.md and not done here. */
#ifndef ZXP_PRELUDE_H
#define ZXP_PRELUDE_H

/* Pull in the system headers that declare the names we rename, first,
 * so their declarations keep the real names (include guards stop a second
 * pass after the macros below exist). */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#if defined(__unix__) || defined(__APPLE__)
#    include <sys/types.h>
#    include <unistd.h>
#    if defined(__linux__)
#        include <sys/random.h>
#    endif
#endif

#include "zxp.h"

#undef malloc
#undef calloc
#undef realloc
#undef free
#undef strdup
#define malloc  zxp_malloc
#define calloc  zxp_calloc
#define realloc zxp_realloc
#define free    zxp_free
#define strdup  zxp_strdup

#undef getentropy
#define getentropy zxp_getentropy

#ifdef ZXP_NO_TLS
/* Single-owner mode: process-wide error text and listener marker. Legal
 * only because zxp_mutex_lock enforces the owner task (zxp.h). */
#    define _Thread_local
#endif

#endif /* ZXP_PRELUDE_H */
