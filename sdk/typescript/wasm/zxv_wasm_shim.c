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
 *  - zxvjs_hw_provider_add / zxvjs_hw_region_create / zxvjs_hw_region_events
 *    (ABI 1.4): the same with C trampolines that forward hardware
 *    implementations and region devices to the JS imports zxv.hw_*; the
 *    `user` pointer is a uint32 id the JS layer maps to its callbacks.
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

/* ---- hardware implementations and devices (ABI 1.4) ---- */
__attribute__((import_module("zxv"), import_name("hw_call"))) int32_t
zxvjs_hw_call(uint32_t user, uint32_t op, const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap,
              size_t *out_len);
__attribute__((import_module("zxv"), import_name("hw_load"))) int32_t
zxvjs_hw_load(uint32_t user, uint32_t slot, const uint8_t *payload, size_t len);
__attribute__((import_module("zxv"), import_name("hw_readback"))) int32_t
zxvjs_hw_readback(uint32_t user, uint32_t slot, uint8_t *digest);
__attribute__((import_module("zxv"), import_name("hw_activate"))) int32_t
zxvjs_hw_activate(uint32_t user, uint32_t slot);
__attribute__((import_module("zxv"), import_name("hw_event"))) void
zxvjs_hw_event(uint32_t user, uint32_t region_id, int32_t event, uint32_t reason, uint32_t version);

static int32_t forward_hw_call(void *user, uint32_t op, const uint8_t *in, size_t in_len, uint8_t *out,
                               size_t out_cap, size_t *out_len)
{
    return zxvjs_hw_call((uint32_t) (uintptr_t) user, op, in, in_len, out, out_cap, out_len);
}
static int32_t forward_hw_load(void *user, uint32_t slot, const uint8_t *payload, size_t len)
{
    return zxvjs_hw_load((uint32_t) (uintptr_t) user, slot, payload, len);
}
static int32_t forward_hw_readback(void *user, uint32_t slot, uint8_t digest[32])
{
    return zxvjs_hw_readback((uint32_t) (uintptr_t) user, slot, digest);
}
static int32_t forward_hw_activate(void *user, uint32_t slot)
{
    return zxvjs_hw_activate((uint32_t) (uintptr_t) user, slot);
}
static void forward_hw_event(void *user, uint32_t region_id, int32_t event, uint32_t reason, uint32_t version)
{
    zxvjs_hw_event((uint32_t) (uintptr_t) user, region_id, event, reason, version);
}

EXPORT(zxvjs_hw_provider_add)
int32_t zxvjs_hw_provider_add(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id, uint32_t res_id,
                              int32_t kind, uint32_t cost, uint32_t user, uint32_t *out_provider)
{
    return zxv_hw_provider_add(cap, mx, fn_id, res_id, kind, cost, forward_hw_call,
                               (void *) (uintptr_t) user, out_provider);
}

EXPORT(zxvjs_hw_region_create)
int32_t zxvjs_hw_region_create(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t provider,
                               uint32_t region_id, uint32_t target_class, const uint8_t *target_digest,
                               uint32_t user, uint32_t log_cap, zxv_hw_region **out)
{
    return zxv_hw_region_create_cap(cap, mx, provider, region_id, target_class, target_digest,
                                    forward_hw_load, forward_hw_readback, forward_hw_activate,
                                    (void *) (uintptr_t) user, log_cap, out);
}

EXPORT(zxvjs_hw_region_events)
int32_t zxvjs_hw_region_events(const zxv_cap *cap, zxv_hw_region *rg, uint32_t user)
{
    return zxv_hw_region_events(cap, rg, forward_hw_event, (void *) (uintptr_t) user);
}
