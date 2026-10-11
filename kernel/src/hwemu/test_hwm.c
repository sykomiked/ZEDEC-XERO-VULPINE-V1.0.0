/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_hwm.c — host tests for the hardware emulation matrix (hwm.h M1-M9). */
#include <stdio.h>
#include <string.h>
#include "hwm.h"

static int fails, checks;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
        }                                                                                          \
    } while (0)

/* Function "add8": out[i] = in[i] + k. Providers differ only in how. */
typedef struct {
    uint8_t k;
    int broken; /* 1: wrong on byte 0x7f; 2: return an error */
} addk_t;

static int addk(void *inst, hwm_t *m, uint32_t op, const uint8_t *in, uint32_t n, uint8_t *out,
                uint32_t cap, uint32_t *out_len)
{
    (void) m;
    (void) op;
    addk_t *a = inst;
    if (a->broken == 2) return -42;
    if (n > cap) return -1;
    for (uint32_t i = 0; i < n; i++)
        out[i] = (uint8_t) (in[i] + a->k + (a->broken == 1 && in[i] == 0x7f));
    *out_len = n;
    return 0;
}

/* Function "add16" emulated by calling "add8" twice (a PEER/SOFT chain). */
static uint32_t g_add8;
static int add_twice(void *inst, hwm_t *m, uint32_t op, const uint8_t *in, uint32_t n, uint8_t *out,
                     uint32_t cap, uint32_t *out_len)
{
    uint32_t dep = inst ? *(uint32_t *) inst : g_add8;
    uint8_t tmp[64];
    uint32_t l1 = 0, l2 = 0;
    int rc = hwm_call(m, dep, op, in, n, tmp, sizeof tmp, &l1);
    if (rc) return rc;
    rc = hwm_call(m, dep, op, tmp, l1, out, cap, &l2);
    if (rc) return rc;
    *out_len = l2;
    return 0;
}

int main(void)
{
    static hwm_t m;
    hwm_init(&m);
    uint32_t f8, f16, rcpu, racc, rfab;
    CHECK(hwm_func_add(&m, 0x100, "add8", &f8) == HWM_OK);
    CHECK(hwm_func_add(&m, 0x101, "add16", &f16) == HWM_OK);
    CHECK(hwm_func_add(&m, 0x100, "dup", 0) == HWM_E_DUPLICATE);
    CHECK(hwm_resource_add(&m, 1, &rcpu) == HWM_OK);
    CHECK(hwm_resource_add(&m, 2, &racc) == HWM_OK);
    CHECK(hwm_resource_add(&m, 3, &rfab) == HWM_OK);
    g_add8 = f8;

    static const uint8_t in1[] = {0, 1, 0x7f, 0xff}, ex8[] = {8, 9, 0x87, 7};
    static const uint8_t ex16[] = {16, 17, 0x8f, 15};
    CHECK(hwm_vector_add(&m, f8, 0, in1, 4, ex8, 4) == HWM_OK);
    CHECK(hwm_vector_add(&m, f16, 0, in1, 4, ex16, 4) == HWM_OK);

    addk_t fixed = {8, 0}, soft = {8, 0}, bad = {8, 1};
    uint32_t pfix, psoft, pbad, p16;
    CHECK(hwm_provider_add(&m, f8, racc, HWM_KIND_HARD, 1, addk, &fixed, &pfix) == HWM_OK);
    CHECK(hwm_provider_add(&m, f8, rcpu, HWM_KIND_SOFT, 10, addk, &soft, &psoft) == HWM_OK);
    CHECK(hwm_provider_add(&m, f8, rfab, HWM_KIND_FABRIC, 1, addk, &bad, &pbad) == HWM_OK);
    CHECK(hwm_provider_add(&m, f16, rcpu, HWM_KIND_SOFT, 20, add_twice, 0, &p16) == HWM_OK);
    CHECK(hwm_provider_dep(&m, p16, f8) == HWM_OK);
    CHECK(hwm_provider_dep(&m, p16, f16) == HWM_E_DEP);

    /* M2: nothing serves before it is proven. */
    uint8_t out[64], scr[64];
    uint32_t n = 0;
    CHECK(hwm_call(&m, f8, 0, in1, 4, out, sizeof out, &n) == HWM_E_NOPROVIDER);
    uint32_t failed;
    CHECK(hwm_attest(&m, pfix, &failed) == LPRES_STATE_TRUE && failed == HWM_NONE);
    CHECK(hwm_attest(&m, psoft, 0) == LPRES_STATE_TRUE);
    CHECK(hwm_attest(&m, pbad, &failed) == LPRES_STATE_FALSE && failed == 0);

    /* M3: FIXED beats RECONF at equal cost, and the broken one never binds. */
    uint32_t pv;
    CHECK(hwm_bind(&m, f8, &pv) == HWM_OK && pv == pfix);
    CHECK(hwm_call(&m, f8, 0, in1, 4, out, sizeof out, &n) == HWM_OK && n == 4 &&
          !memcmp(out, ex8, 4));

    /* M5: chained provider, proven through its dependency. */
    CHECK(hwm_attest(&m, p16, 0) == LPRES_STATE_TRUE);
    CHECK(hwm_call(&m, f16, 0, in1, 4, out, sizeof out, &n) == HWM_OK && !memcmp(out, ex16, 4));

    /* M4: the accelerator faults; add8 fails over to the emulator. */
    CHECK(hwm_resource_set(&m, racc, HWM_RES_FAULTED) == HWM_OK);
    CHECK(hwm_call(&m, f8, 0, in1, 4, out, sizeof out, &n) == HWM_OK && !memcmp(out, ex8, 4));
    CHECK(m.fn[f8].bound == psoft && m.fn[f8].failovers == 1);
    /* The CPU goes offline: add8 and add16 both refuse rather than guess. */
    CHECK(hwm_resource_set(&m, rcpu, HWM_RES_OFFLINE) == HWM_OK);
    CHECK(hwm_call(&m, f8, 0, in1, 4, out, sizeof out, &n) == HWM_E_NOPROVIDER);
    CHECK(hwm_select(&m, f16) == HWM_NONE);
    CHECK(hwm_resource_set(&m, rcpu, HWM_RES_OK) == HWM_OK);
    CHECK(hwm_resource_set(&m, racc, HWM_RES_OK) == HWM_OK);
    CHECK(hwm_bind(&m, f8, &pv) == HWM_OK && pv == pfix);

    /* M6: the shadow agrees, then a provider silently goes bad. */
    bool checked;
    CHECK(hwm_shadow(&m, f8, 0, in1, 4, out, scr, sizeof out, &n, &checked) == HWM_OK && checked);
    soft.broken = 1;
    CHECK(hwm_shadow(&m, f8, 0, in1, 4, out, scr, sizeof out, &n, &checked) == HWM_E_DISPUTE);
    CHECK(m.pv[pfix].attest == LPRES_STATE_BOTH && m.pv[psoft].attest == LPRES_STATE_BOTH);
    CHECK(hwm_call(&m, f8, 0, in1, 4, out, sizeof out, &n) == HWM_E_NOPROVIDER);
    /* Re-attestation settles it: the fixed unit was right. */
    CHECK(hwm_attest(&m, pfix, 0) == LPRES_STATE_TRUE);
    CHECK(hwm_attest(&m, psoft, 0) == LPRES_STATE_FALSE);
    CHECK(hwm_bind(&m, f8, &pv) == HWM_OK && pv == pfix);

    /* M5: undeclared dependency calls are refused. */
    uint32_t f2;
    CHECK(hwm_func_add(&m, 0x102, "sneaky", &f2) == HWM_OK);
    static uint32_t dep_f16;
    dep_f16 = f16;
    static const uint8_t ex2[] = {32, 33, 0x9f, 31};
    CHECK(hwm_vector_add(&m, f2, 0, in1, 4, ex2, 4) == HWM_OK);
    uint32_t p2;
    CHECK(hwm_provider_add(&m, f2, rcpu, HWM_KIND_SOFT, 5, add_twice, &dep_f16, &p2) == HWM_OK);
    CHECK(hwm_attest(&m, p2, 0) == LPRES_STATE_FALSE); /* called f16 without declaring it */
    CHECK(hwm_provider_dep(&m, p2, f16) == HWM_OK);
    CHECK(hwm_attest(&m, p2, 0) == LPRES_STATE_TRUE);

    /* M5: mutual emulation cells, no circular binding. A emulates B and
     * B emulates A; with no native provider for either, neither binds. */
    static hwm_t c;
    hwm_init(&c);
    uint32_t fa, fb, r;
    hwm_func_add(&c, 1, "A", &fa);
    hwm_func_add(&c, 2, "B", &fb);
    hwm_resource_add(&c, 1, &r);
    static uint32_t da, db;
    da = fb;
    db = fa;
    uint32_t pa, pb;
    hwm_provider_add(&c, fa, r, HWM_KIND_PEER, 1, add_twice, &da, &pa);
    hwm_provider_add(&c, fb, r, HWM_KIND_PEER, 1, add_twice, &db, &pb);
    hwm_provider_dep(&c, pa, fb);
    hwm_provider_dep(&c, pb, fa);
    hwm_provider_set_attest(&c, pa, LPRES_STATE_TRUE);
    hwm_provider_set_attest(&c, pb, LPRES_STATE_TRUE);
    CHECK(hwm_select(&c, fa) == HWM_NONE && hwm_select(&c, fb) == HWM_NONE);
    /* Give B real hardware: A now binds through B, and B binds natively. */
    addk_t nb = {1, 0};
    uint32_t pbn;
    hwm_provider_add(&c, fb, r, HWM_KIND_HARD, 5, addk, &nb, &pbn);
    hwm_provider_set_attest(&c, pbn, LPRES_STATE_TRUE);
    CHECK(hwm_select(&c, fa) == pa);
    CHECK(hwm_select(&c, fb) == pbn || hwm_select(&c, fb) == pb);
    CHECK(hwm_call(&c, fa, 0, in1, 2, out, sizeof out, &n) == HWM_OK && out[0] == 2 && out[1] == 3);

    /* A new vector resets proofs. */
    static const uint8_t in3[] = {5}, ex3[] = {13};
    CHECK(hwm_vector_add(&m, f8, 0, in3, 1, ex3, 1) == HWM_OK);
    CHECK(m.pv[pfix].attest == LPRES_STATE_NEITHER);
    /* Provider error surfaces as HWM_E_PROVIDER. */
    fixed.broken = 2;
    CHECK(hwm_attest(&m, pfix, 0) == LPRES_STATE_FALSE);
    CHECK(hwm_provider_retarget(&m, pbad, f16) == HWM_OK &&
          m.pv[pbad].attest == LPRES_STATE_NEITHER);

    /* M7: memo skips the provider on a repeat, and flushes on rebinding. */
    {
        static hwm_t q;
        static hwm_memo_entry_t memo[8];
        hwm_init(&q);
        uint32_t fq, rq, pq1, pq2;
        hwm_func_add(&q, 7, "add8", &fq);
        hwm_resource_add(&q, 1, &rq);
        hwm_vector_add(&q, fq, 0, in1, 4, ex8, 4);
        addk_t a1 = {8, 0}, a2 = {8, 0};
        hwm_provider_add(&q, fq, rq, HWM_KIND_HARD, 1, addk, &a1, &pq1);
        hwm_provider_add(&q, fq, rq, HWM_KIND_SOFT, 9, addk, &a2, &pq2);
        hwm_attest(&q, pq1, 0);
        hwm_attest(&q, pq2, 0);
        CHECK(hwm_memo_attach(&q, fq, memo, 6) == HWM_E_ARG);
        CHECK(hwm_memo_attach(&q, fq, memo, 8) == HWM_OK);
        uint32_t before = q.pv[pq1].calls;
        CHECK(hwm_call(&q, fq, 0, in1, 4, out, sizeof out, &n) == HWM_OK && !memcmp(out, ex8, 4));
        CHECK(hwm_call(&q, fq, 0, in1, 4, out, sizeof out, &n) == HWM_OK && !memcmp(out, ex8, 4));
        CHECK(q.pv[pq1].calls == before + 1 && q.fn[fq].memo_hits == 1);
        /* Different op: a miss, never a wrong hit. */
        CHECK(hwm_call(&q, fq, 1, in1, 4, out, sizeof out, &n) == HWM_OK &&
              q.fn[fq].memo_hits == 1);
        /* pq1 is disabled: the binding moves and the memo is flushed. */
        hwm_provider_enable(&q, pq1, false);
        CHECK(hwm_call(&q, fq, 0, in1, 4, out, sizeof out, &n) == HWM_OK);
        CHECK(q.fn[fq].memo_hits == 1 && q.pv[pq2].calls == 2);
        CHECK(q.pv[pq2].cost_spent == 18);
    }

    /* M8 placement policy and M9 contracts. */
    {
        static hwm_t p;
        static hwm_memo_entry_t memo[4];
        hwm_init(&p);
        uint32_t fk, fd, rl, rr, pl, pr, ph, pd_local, pd_remote, pkey;
        hwm_func_add(&p, 1, "mac", &fk);  /* key-bearing */
        hwm_func_add(&p, 2, "add8", &fd); /* what the mac's provider leans on */
        hwm_resource_add(&p, 1, &rl);
        hwm_resource_add(&p, 2, &rr);
        hwm_vector_add(&p, fd, 0, in1, 4, ex8, 4);
        static const uint8_t ex16[4] = {16, 17, 0x8f, 0x0f};
        hwm_vector_add(&p, fk, 0, in1, 4, ex16, 4);
        addk_t a8 = {8, 0}, a16 = {16, 0};
        /* add8: a cheap REMOTE peer and a dearer local SOFT one */
        hwm_provider_add(&p, fd, rr, HWM_KIND_REMOTE, 1, addk, &a8, &pd_remote);
        hwm_provider_add(&p, fd, rl, HWM_KIND_SOFT, 5, addk, &a8, &pd_local);
        /* mac: REMOTE (cheapest), a HARD unit, and a SOFT chain on add8 */
        hwm_provider_add(&p, fk, rr, HWM_KIND_REMOTE, 1, addk, &a16, &pr);
        hwm_provider_add(&p, fk, rl, HWM_KIND_HARD, 3, addk, &a16, &ph);
        static uint32_t dep_fd;
        dep_fd = fd;
        hwm_provider_add(&p, fk, rl, HWM_KIND_SOFT, 2, add_twice, &dep_fd, &pl);
        hwm_provider_dep(&p, pl, fd);
        for (uint32_t i = 0; i < p.npv; i++) CHECK(hwm_attest(&p, i, 0) == LPRES_STATE_TRUE);
        uint32_t pv;
        /* no policy: lowest cost wins, REMOTE included */
        CHECK(hwm_bind(&p, fk, &pv) == HWM_OK && pv == pr);
        CHECK(hwm_bind(&p, fd, &pv) == HWM_OK && pv == pd_remote);
        /* key-bearing: never REMOTE, and its SOFT chain's add8 runs locally */
        CHECK(hwm_policy_set(&p, fk, HWM_F_KEY_BEARING, 0, 0) == HWM_OK);
        CHECK(hwm_bind(&p, fk, &pv) == HWM_OK && pv == pl);
        uint64_t remote_before = p.pv[pd_remote].calls;
        CHECK(hwm_call(&p, fk, 0, in1, 4, out, sizeof out, &n) == HWM_OK && !memcmp(out, ex16, 4));
        CHECK(p.pv[pd_remote].calls == remote_before && p.pv[pd_local].calls >= 2);
        /* ...while add8 called on its own may still go remote */
        CHECK(hwm_bind(&p, fd, &pv) == HWM_OK && pv == pd_remote);
        /* with no local add8 the chain cannot serve: HARD takes over */
        hwm_provider_enable(&p, pd_local, false);
        CHECK(hwm_bind(&p, fk, &pv) == HWM_OK && pv == ph);
        hwm_provider_enable(&p, pd_local, true);
        /* key-bearing is sticky and refuses a memo */
        CHECK(hwm_policy_set(&p, fk, 0, 0, 0) == HWM_E_POLICY);
        CHECK(hwm_policy_set(&p, fk, 0x80, 0, 0) == HWM_E_ARG);
        CHECK(hwm_memo_attach(&p, fk, memo, 4) == HWM_E_POLICY);
        /* setting key-bearing on a memoized function wipes the memo */
        CHECK(hwm_memo_attach(&p, fd, memo, 4) == HWM_OK);
        CHECK(hwm_call(&p, fd, 0, in1, 4, out, sizeof out, &n) == HWM_OK &&
              memo[0].used + memo[1].used + memo[2].used + memo[3].used == 1);
        CHECK(hwm_policy_set(&p, fd, HWM_F_KEY_BEARING, 0, 0) == HWM_OK && p.fn[fd].memo == 0);
        {
            uint8_t z = 0;
            for (uint32_t i = 0; i < (uint32_t) sizeof memo; i++) z |= ((uint8_t *) memo)[i];
            CHECK(z == 0);
        }
        /* latency ceiling and power budget; undeclared traits never qualify */
        hwm_provider_traits(&p, ph, 100, 2000, 12);
        hwm_provider_traits(&p, pl, 5000, 500, 3);
        CHECK(hwm_policy_set(&p, fk, HWM_F_KEY_BEARING, 1000, 0) == HWM_OK);
        CHECK(hwm_bind(&p, fk, &pv) == HWM_OK && pv == ph);
        CHECK(hwm_policy_set(&p, fk, HWM_F_KEY_BEARING, 0, 1000) == HWM_OK);
        CHECK(hwm_bind(&p, fk, &pv) == HWM_OK && pv == pl);
        CHECK(hwm_policy_set(&p, fk, HWM_F_KEY_BEARING, 1000, 1000) == HWM_OK);
        CHECK(hwm_bind(&p, fk, &pv) == HWM_E_NOPROVIDER);
        CHECK(hwm_call(&p, fk, 0, in1, 4, out, sizeof out, &n) == HWM_E_NOPROVIDER);
        /* discovery flags */
        CHECK(hwm_provider_flags(&p, pr) & HWM_PF_POLICY);
        CHECK(hwm_provider_flags(&p, ph) & HWM_PF_POLICY); /* power */
        hwm_resource_set(&p, rl, HWM_RES_FAULTED);
        CHECK(hwm_provider_flags(&p, ph) & HWM_PF_FAULT);
        hwm_resource_set(&p, rl, HWM_RES_OK);
        CHECK(hwm_power_class(0) == 0 && hwm_power_class(10) == 1 && hwm_power_class(11) == 2 &&
              hwm_power_class(1000) == 3 && hwm_power_class(100000) == 5 &&
              hwm_power_class(4000000) == 6);
        CHECK(hwm_provider_traits(&p, ph, 1, 1, 16) == HWM_E_ARG);
        /* LOCAL_ONLY alone excludes REMOTE too (not key-bearing) */
        hwm_func_add(&p, 3, "add8b", &pkey);
        hwm_vector_add(&p, pkey, 0, in1, 4, ex8, 4);
        uint32_t r1, r2;
        hwm_provider_add(&p, pkey, rr, HWM_KIND_REMOTE, 1, addk, &a8, &r1);
        hwm_provider_add(&p, pkey, rl, HWM_KIND_SOFT, 9, addk, &a8, &r2);
        hwm_attest(&p, r1, 0);
        hwm_attest(&p, r2, 0);
        CHECK(hwm_bind(&p, pkey, &pv) == HWM_OK && pv == r1);
        CHECK(hwm_policy_set(&p, pkey, HWM_F_LOCAL_ONLY, 0, 0) == HWM_OK);
        CHECK(hwm_bind(&p, pkey, &pv) == HWM_OK && pv == r2);
        CHECK(hwm_policy_set(&p, pkey, 0, 0, 0) == HWM_OK); /* local-only is not sticky */
        CHECK(hwm_bind(&p, pkey, &pv) == HWM_OK && pv == r1);
        /* M9: a new contract makes everyone prove again; forward only */
        CHECK(p.pv[r1].proven_contract == 0);
        CHECK(hwm_contract_begin(&p, pkey, 0) == HWM_E_ARG);
        CHECK(hwm_contract_begin(&p, pkey, 2) == HWM_OK && p.fn[pkey].nvec == 0);
        CHECK(hwm_bind(&p, pkey, &pv) == HWM_E_NOPROVIDER);
        CHECK(hwm_provider_flags(&p, r1) & HWM_PF_UNPROVEN);
        CHECK(hwm_contract_begin(&p, pkey, 2) == HWM_E_ARG);
        static const uint8_t ex9[4] = {9, 10, 0x88, 8};
        hwm_vector_add(&p, pkey, 0, in1, 4, ex9, 4); /* contract 2: add 9 */
        CHECK(hwm_attest(&p, r1, 0) == LPRES_STATE_FALSE);
        CHECK(hwm_provider_flags(&p, r1) & HWM_PF_FAILED);
        a8.k = 9;
        CHECK(hwm_attest(&p, r2, 0) == LPRES_STATE_TRUE && p.pv[r2].proven_contract == 2);
        CHECK(hwm_bind(&p, pkey, &pv) == HWM_OK && pv == r2);
        CHECK(hwm_provider_flags(&p, r2) & HWM_PF_SERVING);
        CHECK(strcmp(hwm_kind_name(HWM_KIND_REMOTE), "remote") == 0 &&
              hwm_kind_reconfigurable(HWM_KIND_MICRO) && !hwm_kind_reconfigurable(HWM_KIND_PEER));
    }

    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
