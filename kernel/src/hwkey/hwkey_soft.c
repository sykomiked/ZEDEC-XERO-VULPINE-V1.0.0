/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwkey_soft.c — the no-chip fallback provider (hwkey.h). The "device key"
 * is a 32-byte secret the host keeps in a file, so it is reported as
 * HWK_KIND_SOFTWARE and refused wherever hardware is required. It follows
 * the same wrap/unwrap shape as the chips: E is 32 random bytes and
 * Z = SHAKE256(domain || secret || handle || E). Freestanding. */
#include "hwkey.h"
#include "../mlkem/keccak.h"

static const uint8_t ID_DOM[12] = {'Z', 'X', 'V', '-', 'H', 'W', 'K', 'S', '-', 'I', 'D', ' '};
static const uint8_t Z_DOM[12] = {'Z', 'X', 'V', '-', 'H', 'W', 'K', 'S', '-', 'Z', ' ', ' '};

#define SOFT_HANDLE 16u
#define SOFT_EPH    32u

static int soft_create(void *ctx, const uint8_t *label, uint32_t label_len, uint8_t *handle,
                       uint32_t *handle_len)
{
    hwk_soft_t *s = (hwk_soft_t *) ctx;
    if (!s || *handle_len < SOFT_HANDLE || label_len > HWK_LABEL_MAX) return -1;
    uint8_t buf[sizeof ID_DOM + 32 + HWK_LABEL_MAX];
    uint32_t o = 0;
    for (unsigned i = 0; i < sizeof ID_DOM; i++) buf[o++] = ID_DOM[i];
    for (unsigned i = 0; i < 32; i++) buf[o++] = s->secret[i];
    for (uint32_t i = 0; i < label_len; i++) buf[o++] = label[i];
    shake256(buf, o, handle, SOFT_HANDLE); /* a key id, not a secret */
    hwk_wipe(buf, sizeof buf);
    *handle_len = SOFT_HANDLE;
    return 0;
}

static int soft_z(const hwk_soft_t *s, const uint8_t *handle, uint32_t handle_len,
                  const uint8_t *eph, uint8_t z[HWK_SECRET_BYTES])
{
    if (handle_len != SOFT_HANDLE) return -1;
    uint8_t buf[sizeof Z_DOM + 32 + SOFT_HANDLE + SOFT_EPH];
    uint32_t o = 0;
    for (unsigned i = 0; i < sizeof Z_DOM; i++) buf[o++] = Z_DOM[i];
    for (unsigned i = 0; i < 32; i++) buf[o++] = s->secret[i];
    for (unsigned i = 0; i < SOFT_HANDLE; i++) buf[o++] = handle[i];
    for (unsigned i = 0; i < SOFT_EPH; i++) buf[o++] = eph[i];
    shake256(buf, o, z, HWK_SECRET_BYTES);
    hwk_wipe(buf, sizeof buf);
    return 0;
}

static int soft_random(void *ctx, uint8_t *out, uint32_t len)
{
    hwk_soft_t *s = (hwk_soft_t *) ctx;
    if (!s || !s->rng) return -1;
    return s->rng(s->rng_ctx, out, len) ? -1 : 0;
}

static int soft_wrap(void *ctx, const uint8_t *handle, uint32_t handle_len, uint8_t *eph,
                     uint32_t *eph_len, uint8_t secret[HWK_SECRET_BYTES])
{
    hwk_soft_t *s = (hwk_soft_t *) ctx;
    if (!s || *eph_len < SOFT_EPH) return -1;
    if (soft_random(ctx, eph, SOFT_EPH)) return -1;
    *eph_len = SOFT_EPH;
    return soft_z(s, handle, handle_len, eph, secret);
}

static int soft_unwrap(void *ctx, const uint8_t *handle, uint32_t handle_len, const uint8_t *eph,
                       uint32_t eph_len, uint8_t secret[HWK_SECRET_BYTES])
{
    hwk_soft_t *s = (hwk_soft_t *) ctx;
    if (!s || eph_len != SOFT_EPH) return -1;
    return soft_z(s, handle, handle_len, eph, secret);
}

void hwk_soft_provider(hwk_provider_t *p, hwk_soft_t *s)
{
    p->kind = HWK_KIND_SOFTWARE;
    p->name = "software";
    p->ctx = s;
    p->create = soft_create;
    p->wrap = soft_wrap;
    p->unwrap = soft_unwrap;
    p->random = soft_random;
}
