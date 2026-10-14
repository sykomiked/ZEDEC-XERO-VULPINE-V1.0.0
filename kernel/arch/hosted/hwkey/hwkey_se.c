/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwkey_se.c — the Apple Secure Enclave as an hwkey provider. The enclave
 * work is in hwkey_se.swift (CryptoKit has no C interface); this file
 * adapts it to hwk_provider_t. The label does not select an enclave key:
 * every hwk_keygen creates a new enclave key whose encrypted blob is the
 * handle stored in the sealed key. macOS only; link with swiftc. */
#ifdef __APPLE__
#    include "hwkey_host.h"
#    include <stdio.h>
#    include <stdlib.h>

int32_t zxv_se_available(void);
int32_t zxv_se_create(uint8_t *out, uint32_t *out_len);
int32_t zxv_se_wrap(const uint8_t *handle, uint32_t handle_len, uint8_t *eph, uint32_t *eph_len,
                    uint8_t *z);
int32_t zxv_se_unwrap(const uint8_t *handle, uint32_t handle_len, const uint8_t *eph,
                      uint32_t eph_len, uint8_t *z);

static int rc_of(int32_t r)
{
    return r == 0 ? 0 : r == -3 ? HWK_ERR_UNAVAILABLE : -1;
}

static int se_create(void *ctx, const uint8_t *label, uint32_t label_len, uint8_t *handle,
                     uint32_t *handle_len)
{
    (void) ctx;
    (void) label;
    (void) label_len;
    return rc_of(zxv_se_create(handle, handle_len));
}

static int se_wrap(void *ctx, const uint8_t *handle, uint32_t handle_len, uint8_t *eph,
                   uint32_t *eph_len, uint8_t secret[HWK_SECRET_BYTES])
{
    (void) ctx;
    return rc_of(zxv_se_wrap(handle, handle_len, eph, eph_len, secret));
}

static int se_unwrap(void *ctx, const uint8_t *handle, uint32_t handle_len, const uint8_t *eph,
                     uint32_t eph_len, uint8_t secret[HWK_SECRET_BYTES])
{
    (void) ctx;
    return rc_of(zxv_se_unwrap(handle, handle_len, eph, eph_len, secret));
}

hwk_status_t hwk_se_open(hwk_provider_t *p, void **impl, char *why, size_t why_cap)
{
    if (zxv_se_available() != 1) {
        snprintf(why, why_cap, "no Secure Enclave on this Mac (or not reachable from a VM)");
        return HWK_ERR_UNAVAILABLE;
    }
    p->kind = HWK_KIND_SECURE_ENCLAVE;
    p->name = "secure-enclave";
    p->ctx = 0;
    p->create = se_create;
    p->wrap = se_wrap;
    p->unwrap = se_unwrap;
    p->random = hwk_host_random;
    *impl = 0;
    return HWK_OK;
}

void hwk_se_close(void *impl)
{
    (void) impl;
}
#else
typedef int hwkey_se_unused_t; /* ISO C forbids an empty translation unit */
#endif
