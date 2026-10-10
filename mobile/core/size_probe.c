/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* size_probe.c — links every zxv_* entry point so build_mobile_core.sh can
 * measure what the core adds to an app after dead-code elimination. It is
 * also a smoke test: run it on an arm64 device (or qemu-aarch64) and it
 * creates a one-device mesh and routes a prompt. */
#include "zxv_mobile.h"

static uint64_t s = 88172645463325252ull;
static void rnd(void *ctx, uint8_t *out, uint32_t n)
{
    (void) ctx;
    for (uint32_t i = 0; i < n; i++) {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        out[i] = (uint8_t) s;
    }
}
static int snd(void *ctx, const uint8_t to[ZXV_ID_BYTES], const uint8_t *f, uint32_t n)
{
    (void) ctx, (void) to, (void) f, (void) n;
    return 0;
}

int main(void)
{
    zxv_host_t h = {0, rnd, snd, 0, 0, 0};
    uint8_t seed[64], id[ZXV_ID_BYTES], tgt[ZXV_ID_BYTES];
    char qr[256], code[11], name[32], val[65];
    uint32_t req = 0;
    rnd(0, seed, 64);
    if (zxv_start(&h, seed, "Phone", ZXV_ROLE_STANDALONE, ZXV_FLAG_HELD, 2) != 0) return 1;
    if (zxv_create_mesh(1000) != 0) return 2;
    if (zxv_set_caps(8192, 400, 80, 0, ZXV_NET_UNMETERED, 0, 1000) != 0) return 3;
    if (zxv_route(1, 1, 1000, tgt) != ZXV_ROUTE_LOCAL) return 4;
    zxv_invite(ZXV_ROLE_HOME, 0, 1000, qr, sizeof qr, code);
    zxv_set_setting(1, "dark", 1000);
    zxv_get_setting(1, val, sizeof val);
    zxv_device(0, id, name);
    zxv_self_id(id);
    zxv_tick(2000);
    zxv_prompt(tgt, "hi", 2000, &req);
    zxv_connect(id, 2000);
    zxv_receive(seed, 0, 2000);
    zxv_join_code("0000000000", 2000);
    zxv_join_qr_text("AAAA", 2000);
    zxv_confirm_sas(id, 0, 2000);
    zxv_rename(id, "Phone 2", 2000);
    zxv_money_answer(0, 2000);
    zxv_revoke(id, 2000);
    (void) zxv_local_model(0);
    (void) zxv_device_count();
    (void) zxv_assure_fee(10000);
    (void) zxv_api_version();
    zxv_stop();
    return 0;
}
