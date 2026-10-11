/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_hwe.c — host tests for the standard engines (hwe.h): every
 * contract installs with a proven SOFT provider, answers known values
 * from independent sources, and hardware providers take over on cost. */
#include <stdio.h>
#include <string.h>
#include "hwe.h"

static int fails, checks;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
        }                                                                                          \
    } while (0)

static int hex_eq(const uint8_t *b, const char *hex)
{
    for (size_t i = 0; hex[2 * i]; i++) {
        unsigned v;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1 || b[i] != v) return 0;
    }
    return 1;
}

/* A "hardware" sha256 that is the SOFT one, and a broken one. */
static int hw_sha(void *inst, hwm_t *m, uint32_t op, const uint8_t *in, uint32_t n, uint8_t *out,
                  uint32_t cap, uint32_t *len)
{
    int rc = hwe_soft_call((void *) (uintptr_t) HWE_FN_SHA256, m, op, in, n, out, cap, len);
    if (rc == 0 && inst) out[31] ^= 1;
    return rc;
}

int main(void)
{
    static hwm_t m;
    hwm_init(&m);
    uint32_t cpu, inst, f, pv;
    hwm_resource_add(&m, 1, &cpu);
    CHECK(hwe_install(&m, cpu, 0x40, &inst) == HWM_E_ARG);
    CHECK(hwe_install(&m, cpu, HWE_ALL, &inst) == HWM_OK && inst == hwe_available());
    /* Installing again adds nothing. */
    uint32_t nfn = m.nfn, npv = m.npv;
    CHECK(hwe_install(&m, cpu, HWE_ALL, &inst) == HWM_OK && inst == 0 && m.nfn == nfn &&
          m.npv == npv);
    for (uint32_t i = 0; i < m.npv; i++) {
        CHECK(m.pv[i].attest == LPRES_STATE_TRUE && m.pv[i].kind == HWM_KIND_SOFT);
        CHECK(m.pv[i].proven_contract == HWE_CONTRACT_V1);
    }
    CHECK(strcmp(hwe_name(HWE_FN_KECCAK), "hash.keccak") == 0 && hwe_fn_id(HWE_GCD) == HWE_FN_GCD);
    CHECK(hwe_fn_id(3) == 0 && strcmp(hwe_name(1), "unknown") == 0);

    uint8_t out[512], in[256];
    uint32_t n = 0;
    /* Known answers from outside this tree (FIPS 180-4, FIPS 202, RFC 4231). */
    hwm_func_find(&m, HWE_FN_SHA256, &f);
    CHECK(hwm_call(&m, f, 0, (const uint8_t *) "abc", 3, out, sizeof out, &n) == HWM_OK && n == 32);
    CHECK(hex_eq(out, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    hwm_func_find(&m, HWE_FN_KECCAK, &f);
    CHECK(hwm_call(&m, f, 0, (const uint8_t *) "", 0, out, sizeof out, &n) == HWM_OK);
    CHECK(hex_eq(out, "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a"));
    in[0] = 0;
    in[1] = 2; /* 512 bytes */
    CHECK(hwm_call(&m, f, 3, in, 2, out, sizeof out, &n) == HWM_OK && n == 512);
    in[1] = 3; /* 768: refused */
    CHECK(hwm_call(&m, f, 3, in, 2, out, sizeof out, &n) == HWM_E_PROVIDER);
    hwm_func_find(&m, HWE_FN_MAC, &f);
    in[0] = 4;
    in[1] = 0;
    memcpy(in + 2, "Jefe", 4);
    memcpy(in + 6, "what do ya want for nothing?", 28);
    CHECK(hwm_call(&m, f, 0, in, 34, out, sizeof out, &n) == HWM_OK && n == 32);
    CHECK(hex_eq(out, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
    /* The MAC is key-bearing: no memo, never remote. */
    CHECK(m.fn[f].flags & HWM_F_KEY_BEARING);
    static hwm_memo_entry_t memo[4];
    CHECK(hwm_memo_attach(&m, f, memo, 4) == HWM_E_POLICY);
    uint32_t net, prem;
    hwm_resource_add(&m, 2, &net);
    CHECK(hwm_provider_add(&m, f, net, HWM_KIND_REMOTE, 1, hwe_soft_call,
                           (void *) (uintptr_t) HWE_FN_MAC, &prem) == HWM_OK);
    CHECK(hwm_attest(&m, prem, 0) == LPRES_STATE_TRUE);
    CHECK(hwm_bind(&m, f, &pv) == HWM_OK && m.pv[pv].kind == HWM_KIND_SOFT);
    hwm_func_find(&m, HWE_FN_GCD, &f);
    memset(in, 0, 32);
    in[0] = 12;
    in[8] = 18;
    in[16] = 0;
    in[24] = 7;
    CHECK(hwm_call(&m, f, 0, in, 32, out, sizeof out, &n) == HWM_OK && n == 16 && out[0] == 6 &&
          out[8] == 7);
    CHECK(hwm_call(&m, f, 0, in, 31, out, sizeof out, &n) == HWM_E_PROVIDER);
    /* logic64: TRUE AND x = x, FALSE OR x = x, NOT swaps TRUE and FALSE. */
    hwm_func_find(&m, HWE_FN_LOGIC64, &f);
    memset(in, 0x55, 16);      /* every lane TRUE (1) */
    memset(in + 16, 0x1B, 16); /* lanes 3,2,1,0 = BOTH, FALSE, TRUE, NEITHER */
    CHECK(hwm_call(&m, f, 0, in, 32, out, sizeof out, &n) == HWM_OK && n == 16 && out[0] == 0x1B);
    CHECK(hwm_call(&m, f, 2, in, 16, out, sizeof out, &n) == HWM_OK && out[0] == 0xAA);
#ifndef HWE_NO_TENSOR
    CHECK(hwm_func_find(&m, HWE_FN_TENSOR, &f) == HWM_OK);
    {
        /* One block, scale 1.0 (f16 0x3C00), w = 1..32, a = 1, shift 0:
         * sum 528 as Q16 -> 528 * 65536 >> 16 ... = 528 in Q16 units of a. */
        uint8_t t[4 + 34 + 64 + 1];
        memset(t, 0, sizeof t);
        t[0] = 1;
        t[4] = 0x00;
        t[5] = 0x3C;
        for (int i = 0; i < 32; i++) t[6 + i] = (uint8_t) (i + 1);
        for (int i = 0; i < 32; i++) t[38 + 2 * i] = 1;
        CHECK(hwm_call(&m, f, 0, t, sizeof t, out, sizeof out, &n) == HWM_OK && n == 4);
        int32_t r = (int32_t) ((uint32_t) out[0] | ((uint32_t) out[1] << 8) |
                               ((uint32_t) out[2] << 16) | ((uint32_t) out[3] << 24));
        CHECK(r == 528); /* activations are a * 2^(ash-16): 528 * 2^-16 in Q16 is 528 */
    }
#endif
    /* A HARD sha256 that is right takes over on cost; a wrong one never serves. */
    uint32_t acc, pgood, pbad;
    hwm_resource_add(&m, 3, &acc);
    hwm_func_find(&m, HWE_FN_SHA256, &f);
    hwm_provider_add(&m, f, acc, HWM_KIND_HARD, 1, hw_sha, (void *) 1, &pbad);
    hwm_provider_add(&m, f, acc, HWM_KIND_HARD, 2, hw_sha, 0, &pgood);
    CHECK(hwm_attest(&m, pbad, 0) == LPRES_STATE_FALSE);
    CHECK(hwm_attest(&m, pgood, 0) == LPRES_STATE_TRUE);
    CHECK(hwm_bind(&m, f, &pv) == HWM_OK && pv == pgood);
    /* The accelerator faults: the SOFT engine answers the same bytes. */
    hwm_resource_set(&m, acc, HWM_RES_FAULTED);
    CHECK(hwm_call(&m, f, 0, (const uint8_t *) "abc", 3, out, sizeof out, &n) == HWM_OK);
    CHECK(hwm_bind(&m, f, &pv) == HWM_OK && m.pv[pv].kind == HWM_KIND_SOFT);
    CHECK(hex_eq(out, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
