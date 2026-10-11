/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_wasm_shim.c — the small host layer that the @zedec/zxv WebAssembly
 * build adds to libzxv. It is compiled with the C library sources into
 * wasm/zxv.wasm and owned by this SDK (sdk/libzxv is not modified).
 *
 *  - arc4random_buf over WASI random_get (the JS host maps it to
 *    crypto.getRandomValues), so zxv_random works in wasm32.
 *  - Single-threaded pthread stubs. The wasm build is SINGLE-THREADED: a
 *    mutex can never be contended, so lock/unlock only track state, and
 *    pthread_create fails (the tensor engine then runs every matrix on the
 *    calling thread, which gives bit-identical results by design).
 *    wasi-libc ships equivalent stubs; ours are weak so either may win.
 *  - zxvjs_generate: zxv_generate with a C token callback that forwards
 *    each token to the JS import zxv.on_token (function pointers cannot be
 *    created from JS).
 */
#define _DEFAULT_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "zxv_api.h"

#define EXPORT(name) __attribute__((export_name(#name), visibility("default")))

/* Weak: wasi-libc builds that already provide arc4random_buf (seeded from
 * random_get) win; older ones without it use this one. */
__attribute__((weak)) void arc4random_buf(void *buf, size_t len)
{
    uint8_t *p = buf;
    while (len) {
        size_t n = len > 256 ? 256 : len; /* getentropy's per-call limit */
        if (getentropy(p, n) != 0) abort();
        p += n;
        len -= n;
    }
}

/* ---- single-threaded pthread stubs ---- */
__attribute__((weak)) int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a)
{
    (void) a;
    memset(m, 0, sizeof *m);
    return 0;
}
__attribute__((weak)) int pthread_mutex_destroy(pthread_mutex_t *m)
{
    (void) m;
    return 0;
}
__attribute__((weak)) int pthread_mutex_lock(pthread_mutex_t *m)
{
    (void) m;
    return 0;
}
__attribute__((weak)) int pthread_mutex_unlock(pthread_mutex_t *m)
{
    (void) m;
    return 0;
}

/* ---- generation with a JS token callback ---- */
__attribute__((import_module("zxv"), import_name("on_token"))) int32_t
zxvjs_on_token(uint32_t user, int32_t token, const uint8_t *piece, size_t len);

static int32_t forward_token(void *user, int32_t token, const uint8_t *piece, size_t len)
{
    return zxvjs_on_token((uint32_t) (uintptr_t) user, token, piece, len);
}

EXPORT(zxvjs_generate)
int32_t zxvjs_generate(const zxv_cap *cap, zxv_session *session, const uint8_t *prompt,
                       size_t prompt_len, uint32_t max_tokens, int32_t temperature_q16,
                       uint32_t top_k, int32_t top_p_q16, int32_t repeat_penalty_q16,
                       uint32_t repeat_last_n, uint64_t seed, uint32_t user,
                       uint32_t *out_n_prompt, uint32_t *out_n_generated, int32_t *out_stop)
{
    return zxv_generate(cap, session, prompt, prompt_len, max_tokens, temperature_q16, top_k,
                        top_p_q16, repeat_penalty_q16, repeat_last_n, seed, forward_token,
                        (void *) (uintptr_t) user, out_n_prompt, out_n_generated, out_stop);
}
