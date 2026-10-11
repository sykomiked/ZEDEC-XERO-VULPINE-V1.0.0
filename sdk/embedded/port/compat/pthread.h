/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* compat/pthread.h — the subset of <pthread.h> libzxv uses, mapped onto the
 * port layer. Found before the system header because the embedded build
 * puts -I port/compat first. Only for libzxv/kernel objects. */
#ifndef ZXP_COMPAT_PTHREAD_H
#define ZXP_COMPAT_PTHREAD_H

#include "zxp.h"

#undef pthread_mutex_t
#undef pthread_cond_t
#undef PTHREAD_MUTEX_INITIALIZER
#define pthread_mutex_t           zxp_mutex_t
#define pthread_cond_t            zxp_cond_t
#define PTHREAD_MUTEX_INITIALIZER ZXP_MUTEX_INITIALIZER

#define pthread_mutex_init(m, attr)  ((void) (attr), zxp_mutex_init(m))
#define pthread_mutex_destroy(m)     zxp_mutex_destroy(m)
#define pthread_mutex_lock(m)        zxp_mutex_lock(m)
#define pthread_mutex_unlock(m)      zxp_mutex_unlock(m)
#define pthread_cond_init(c, attr)   ((void) (attr), zxp_cond_init(c))
#define pthread_cond_destroy(c)      zxp_cond_destroy(c)
#define pthread_cond_wait(c, m)      zxp_cond_wait((c), (m))
#define pthread_cond_broadcast(c)    zxp_cond_broadcast(c)

#endif /* ZXP_COMPAT_PTHREAD_H */
