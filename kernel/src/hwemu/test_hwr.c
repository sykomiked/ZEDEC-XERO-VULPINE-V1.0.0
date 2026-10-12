/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_hwr.c — host tests for signed reconfiguration images (hwr.h R1-R7),
 * with real ML-DSA-87 and SLH-DSA-SHAKE-256s signatures, a simulated
 * two-slot region whose "bitstream" is a 256-byte lookup table, the audit
 * log (hwa.h) and contract packages. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hwr.h"
#include "hwr_pq.h"
#include "../robin_debanks/sha256.h"
#include "../pqsec/pq_matrix_algs.h"

static int fails, checks;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
        }                                                                                          \
    } while (0)

/* ---- the simulated region ---- */
typedef struct {
    uint8_t slot[2][256];
    uint32_t len[2];
    int active;  /* -1: blank */
    int corrupt; /* 1: flip a bit on load (bad flash) */
    int refuse;  /* 1: activate fails */
    uint32_t loads;
} sim_t;

static int sim_load(void *d, uint32_t s, const uint8_t *p, uint32_t n)
{
    sim_t *x = d;
    if (s > 1 || n > 256) return -1;
    memcpy(x->slot[s], p, n);
    x->len[s] = n;
    if (x->corrupt) x->slot[s][0] ^= 1;
    x->loads++;
    return 0;
}
static int sim_readback(void *d, uint32_t s, uint8_t dg[32])
{
    sim_t *x = d;
    sha256(x->slot[s], x->len[s], dg);
    return 0;
}
static int sim_activate(void *d, uint32_t s)
{
    sim_t *x = d;
    if (x->refuse) return -1;
    x->active = (int) s;
    return 0;
}
static const hwr_ops_t SIM_OPS = {sim_load, sim_readback, sim_activate};

/* The RECONF provider: whatever table is active, applied byte by byte. */
static int sim_call(void *inst, hwm_t *m, uint32_t op, const uint8_t *in, uint32_t n, uint8_t *out,
                    uint32_t cap, uint32_t *out_len)
{
    (void) m;
    (void) op;
    sim_t *x = inst;
    if (x->active < 0 || n > cap) return -1;
    for (uint32_t i = 0; i < n; i++) out[i] = x->slot[x->active][in[i]];
    *out_len = n;
    return 0;
}
/* The software emulator of "inc": out = in + 1. */
static int soft_inc(void *inst, hwm_t *m, uint32_t op, const uint8_t *in, uint32_t n, uint8_t *out,
                    uint32_t cap, uint32_t *out_len)
{
    (void) inst;
    (void) m;
    (void) op;
    if (n > cap) return -1;
    for (uint32_t i = 0; i < n; i++) out[i] = (uint8_t) (in[i] + 1);
    *out_len = n;
    return 0;
}

/* ---- events (R7) ---- */
static uint8_t evs[64];
static uint32_t nev;
static void on_event(void *ctx, uint32_t region, uint32_t ev, uint32_t reason, uint32_t version)
{
    (void) ctx;
    (void) reason;
    (void) version;
    if (region == 9 && nev < 64) evs[nev++] = (uint8_t) ev;
}
static const hwa_rec_t *last(const hwa_log_t *l)
{
    return hwa_get(l, l->n - 1);
}

/* ---- image building ---- */
static uint8_t pk[2592], sk[4896], pk2[2592], sk2[4896];
#define TCLASS 0x00D0F00Du

static uint32_t make(uint8_t *img, const uint8_t lut[256], uint32_t fn_id, uint32_t version,
                     uint32_t svn, const uint8_t *signer, const uint8_t tdig[32])
{
    hwr_image_t im;
    memset(&im, 0, sizeof im);
    im.alg = HWR_ALG_MLDSA87;
    im.kind = HWR_KIND_PARTIAL;
    im.target_class = TCLASS;
    im.fn_id = fn_id;
    im.version = version;
    im.svn = svn;
    im.payload_len = 256;
    if (tdig) memcpy(im.target_digest, tdig, 32);
    sha256(lut, 256, im.payload_sha256);
    hwr_build_header(&im, img);
    memcpy(img + HWR_HEADER, lut, 256);
    hwr_mldsa87_sign(signer, img, NULL, img + HWR_HEADER + 256);
    return HWR_HEADER + 256 + HWR_MLDSA87_SIG;
}

int main(void)
{
    uint8_t seed[32] = {1}, seed2[32] = {2};
    pqm_mldsa87_keygen(seed, pk, sk);
    pqm_mldsa87_keygen(seed2, pk2, sk2);

    static uint8_t inc[256], dec[256], wrong[256], xor5[256];
    for (int i = 0; i < 256; i++) {
        inc[i] = (uint8_t) (i + 1);
        dec[i] = (uint8_t) (i - 1);
        wrong[i] = (uint8_t) (i + 1);
        xor5[i] = (uint8_t) (i ^ 5);
    }
    wrong[200] = 0; /* a signed image with one wrong gate */

    /* Matrix: "inc" (soft emulator + the region) and "xor5" (region only). */
    static hwm_t m;
    hwm_init(&m);
    uint32_t finc, fxor, rcpu, rfab, psoft, preg;
    hwm_func_add(&m, 0x200, "inc", &finc);
    hwm_func_add(&m, 0x201, "xor5", &fxor);
    hwm_resource_add(&m, 1, &rcpu);
    hwm_resource_add(&m, 2, &rfab);
    static uint8_t vin[256], vinc[256], vxor[256];
    for (int i = 0; i < 256; i++) {
        vin[i] = (uint8_t) i;
        vinc[i] = (uint8_t) (i + 1);
        vxor[i] = (uint8_t) (i ^ 5);
    }
    hwm_vector_add(&m, finc, 0, vin, 256, vinc, 256);
    hwm_vector_add(&m, fxor, 0, vin, 256, vxor, 256);
    static sim_t sim = {.active = -1};
    hwm_provider_add(&m, finc, rcpu, HWM_KIND_SOFT, 50, soft_inc, 0, &psoft);
    hwm_provider_add(&m, finc, rfab, HWM_KIND_FABRIC, 1, sim_call, &sim, &preg);
    CHECK(hwm_attest(&m, psoft, 0) == LPRES_STATE_TRUE);
    CHECK(hwm_attest(&m, preg, 0) == LPRES_STATE_FALSE); /* blank region */

    static hwa_rec_t qrec[20];
    hwa_log_t q;
    hwa_init(&q, qrec, 20);
    static hwr_region_t r;
    uint8_t tdig[32];
    memset(tdig, 0xAB, 32);
    CHECK(hwr_region_init(&r, 9, TCLASS, tdig, HWR_ALG_MLDSA87, hwr_pq_verify, 0, &SIM_OPS, &sim,
                          &q) == HWR_OK);
    hwr_set_event(&r, on_event, 0);
    CHECK(hwr_trust_key(&r, pk) == HWR_OK);
    CHECK(hwr_link(&r, &m, rfab, psoft) == HWR_E_ARG); /* not a FABRIC/MICRO provider */
    CHECK(hwr_link(&r, &m, rfab, preg) == HWR_OK);

    static uint8_t img[HWR_HEADER + 256 + HWR_MLDSA87_SIG];
    uint32_t n;
    uint32_t pv;

    /* R1: an untrusted signer is refused and logged; nothing loads. */
    n = make(img, inc, 0x200, 1, 1, sk2, NULL);
    CHECK(hwr_stage(&r, img, n) == HWR_E_VERIFY && q.n == 1 && sim.loads == 0);
    CHECK(q.rec[0].reason == HWR_Q_SIGNATURE && lpx_forbidden(q.rec[0].tag));
    CHECK(q.rec[0].event == HWA_EV_REJECTED && nev == 1 && evs[0] == HWA_EV_REJECTED);
    /* Format: one byte short, or a tampered payload. */
    n = make(img, inc, 0x200, 1, 1, sk, NULL);
    CHECK(hwr_stage(&r, img, n - 1) == HWR_E_FORMAT);
    img[HWR_HEADER + 3] ^= 1;
    CHECK(hwr_stage(&r, img, n) == HWR_E_FORMAT);
    /* R2: another part of the class, or an unknown function. */
    uint8_t other[32];
    memset(other, 0xCD, 32);
    n = make(img, inc, 0x200, 1, 1, sk, other);
    CHECK(hwr_stage(&r, img, n) == HWR_E_TARGET);
    n = make(img, inc, 0x999, 1, 1, sk, NULL);
    CHECK(hwr_stage(&r, img, n) == HWR_E_FUNCTION);

    /* The happy path: stage, trial (proved by vectors), commit. */
    n = make(img, inc, 0x200, 1, 1, sk, tdig);
    CHECK(hwr_stage(&r, img, n) == HWR_OK && r.state == HWR_ST_STAGED);
    CHECK(hwr_commit(&r) == HWR_E_STATE);
    CHECK(hwr_trial(&r, 3) == HWR_OK && r.state == HWR_ST_PROBATION);
    CHECK(m.pv[preg].attest == LPRES_STATE_TRUE);
    CHECK(hwm_bind(&m, finc, &pv) == HWM_OK && pv == preg); /* the hardware now serves */
    CHECK(hwr_commit(&r) == HWR_OK && r.state == HWR_ST_ACTIVE && r.floor == 1);
    /* R7: staged, tested and activated are each recorded, not forbidden. */
    CHECK(q.n == 4 && q.rec[1].event == HWA_EV_STAGED && q.rec[2].event == HWA_EV_TESTED &&
          q.rec[3].event == HWA_EV_ACTIVATED && q.rec[3].version == 1 &&
          !lpx_forbidden(q.rec[3].tag) && hwa_forbidden(&q) == 1);
    CHECK(nev == 4 && evs[3] == HWA_EV_ACTIVATED);

    /* R3: replaying the same version, or a lower svn, is refused and logged. */
    CHECK(hwr_stage(&r, img, n) == HWR_E_ROLLBACK && q.n == 5);
    n = make(img, inc, 0x200, 5, 0, sk, NULL);
    CHECK(hwr_stage(&r, img, n) == HWR_E_ROLLBACK && q.n == 6);
    CHECK(last(&q)->event == HWA_EV_REJECTED && last(&q)->reason == HWR_Q_ROLLBACK);

    /* R5/R6: a validly signed image with a wrong gate fails conformance,
     * rolls back to v1, is quarantined and blocked. */
    n = make(img, wrong, 0x200, 2, 1, sk, NULL);
    CHECK(hwr_stage(&r, img, n) == HWR_OK);
    CHECK(hwr_trial(&r, 3) == HWR_E_CONFORMANCE && r.state == HWR_ST_ACTIVE);
    CHECK(q.n == 9 && q.rec[7].event == HWA_EV_ROLLED_BACK &&
          q.rec[8].event == HWA_EV_QUARANTINED && q.rec[8].reason == HWR_Q_CONFORMANCE &&
          hwa_verify(&q));
    CHECK(m.pv[preg].attest == LPRES_STATE_TRUE && sim.active == (int) r.active);
    CHECK(hwm_bind(&m, finc, &pv) == HWM_OK && pv == preg);
    n = make(img, wrong, 0x200, 3, 1, sk, NULL); /* same bytes, new version */
    CHECK(hwr_stage(&r, img, n) == HWR_E_BLOCKED);

    /* R4: the flash corrupts what it stores: read-back catches it. */
    sim.corrupt = 1;
    n = make(img, dec, 0x200, 3, 1, sk, NULL);
    CHECK(hwr_stage(&r, img, n) == HWR_E_READBACK && last(&q)->reason == HWR_Q_READBACK &&
          last(&q)->event == HWA_EV_QUARANTINED);
    sim.corrupt = 0;

    /* The region becomes a different accelerator: v5 is an xor5 table.
     * Left uncommitted, its probation expires and it rolls back. */
    n = make(img, xor5, 0x201, 5, 2, sk, NULL);
    CHECK(hwr_stage(&r, img, n) == HWR_OK);
    CHECK(hwr_trial(&r, 2) == HWR_OK && m.pv[preg].fn == fxor);
    /* inc now comes from the emulator again, xor5 from the region. */
    CHECK(hwm_bind(&m, finc, &pv) == HWM_OK && pv == psoft);
    CHECK(hwm_bind(&m, fxor, &pv) == HWM_OK && pv == preg);
    CHECK(hwr_tick(&r) == HWR_OK);
    CHECK(hwr_tick(&r) == HWR_E_CONFORMANCE && r.state == HWR_ST_ACTIVE);
    CHECK(last(&q)->reason == HWR_Q_EXPIRED);
    CHECK(m.pv[preg].fn == finc && m.pv[preg].attest == LPRES_STATE_TRUE);

    /* Activation refused by the device: rolled back and quarantined. */
    sim.refuse = 1;
    n = make(img, xor5, 0x201, 6, 2, sk, NULL);
    CHECK(hwr_stage(&r, img, n) == HWR_OK);
    CHECK(hwr_trial(&r, 2) == HWR_E_ACTIVATE);
    /* With the device refusing even the fallback, the region drops to S0
     * and the emulator keeps "inc" running. */
    CHECK(r.state == HWR_ST_EMPTY && m.pv[preg].attest == LPRES_STATE_NEITHER);
    CHECK(hwm_bind(&m, finc, &pv) == HWM_OK && pv == psoft);
    sim.refuse = 0;

    /* An operator rollback of a staged image is recorded too. */
    n = make(img, inc, 0x200, 7, 2, sk, NULL);
    CHECK(hwr_stage(&r, img, n) == HWR_OK && hwr_rollback(&r) == HWR_OK && r.state == HWR_ST_EMPTY);
    CHECK(q.n == 19 && last(&q)->event == HWA_EV_ROLLED_BACK && last(&q)->version == 7);
    CHECK(nev == q.n);
    for (uint32_t i = 0; i < q.n; i++) CHECK(evs[i] == q.rec[i].event);

    /* A full audit log halts the region (fail closed). */
    n = make(img, inc, 0x200, 9, 3, sk2, NULL);
    CHECK(hwr_stage(&r, img, n) == HWR_E_VERIFY && q.n == 20);
    CHECK(hwr_stage(&r, img, n) == HWR_E_HALTED && r.state == HWR_ST_HALTED);
    CHECK(hwr_stage(&r, img, n) == HWR_E_HALTED);
    CHECK(hwa_verify(&q));
    qrec[5].version ^= 1; /* tampering breaks the chain */
    CHECK(!hwa_verify(&q));
    qrec[5].version ^= 1;
    /* VECTORS packages are never loaded into a region. */
    {
        static hwr_region_t r3;
        static hwa_rec_t rec3[4];
        hwa_log_t l3;
        hwa_init(&l3, rec3, 4);
        hwr_region_init(&r3, 3, TCLASS, tdig, HWR_ALG_MLDSA87, hwr_pq_verify, 0, &SIM_OPS, &sim,
                        &l3);
        hwr_trust_key(&r3, pk);
        n = make(img, inc, 0x200, 1, 1, sk, NULL);
        img[8] = HWR_KIND_VECTORS;
        CHECK(hwr_stage(&r3, img, n) == HWR_E_KIND);
    }

    /* R7: an activation that cannot be recorded is rolled back, not made. */
    {
        static hwm_t m2;
        hwm_init(&m2);
        uint32_t f, rc_, rf, ps, pr;
        hwm_func_add(&m2, 0x200, "inc", &f);
        hwm_resource_add(&m2, 1, &rc_);
        hwm_resource_add(&m2, 2, &rf);
        hwm_vector_add(&m2, f, 0, vin, 256, vinc, 256);
        hwm_provider_add(&m2, f, rc_, HWM_KIND_SOFT, 50, soft_inc, 0, &ps);
        hwm_provider_add(&m2, f, rf, HWM_KIND_MICRO, 1, sim_call, &sim, &pr);
        hwm_attest(&m2, ps, 0);
        static hwr_region_t r2;
        static hwa_rec_t rec2[2];
        hwa_log_t l2;
        hwa_init(&l2, rec2, 2);
        sim.active = -1;
        CHECK(hwr_region_init(&r2, 4, TCLASS, tdig, HWR_ALG_MLDSA87, hwr_pq_verify, 0, &SIM_OPS,
                              &sim, &l2) == HWR_OK);
        hwr_trust_key(&r2, pk);
        CHECK(hwr_link(&r2, &m2, rf, pr) == HWR_OK); /* MICRO is reconfigurable */
        n = make(img, inc, 0x200, 1, 1, sk, tdig);
        CHECK(hwr_stage(&r2, img, n) == HWR_OK && hwr_trial(&r2, 2) == HWR_OK);
        CHECK(hwm_bind(&m2, f, &pv) == HWM_OK && pv == pr);
        CHECK(hwr_commit(&r2) == HWR_E_HALTED && r2.state == HWR_ST_HALTED && r2.n_committed == 0);
        CHECK(m2.pv[pr].attest == LPRES_STATE_NEITHER); /* back to S0 */
        CHECK(hwm_bind(&m2, f, &pv) == HWM_OK && pv == ps);
    }

    /* Contract packages: verify, parse, install. */
    {
        static uint8_t pkg[HWR_HEADER + 64 + HWR_MLDSA87_SIG];
        uint8_t pay[64];
        uint32_t o = 0;
        /* two vectors: op 0, "ab" -> "bc"; op 0, "z" -> "{" */
        const uint8_t v[] = {0,   0, 0, 0, 2, 0, 0, 0, 'a', 'b', 2, 0, 0, 0, 'b',
                             'c', 0, 0, 0, 0, 1, 0, 0, 0,   'z', 1, 0, 0, 0, '{'};
        memcpy(pay, v, sizeof v);
        o = sizeof v;
        hwr_image_t im;
        memset(&im, 0, sizeof im);
        im.alg = HWR_ALG_MLDSA87;
        im.kind = HWR_KIND_VECTORS;
        im.fn_id = 0x200;
        im.version = 2;
        im.svn = 2;
        im.payload_len = o;
        sha256(pay, o, im.payload_sha256);
        hwr_build_header(&im, pkg);
        memcpy(pkg + HWR_HEADER, pay, o);
        hwr_mldsa87_sign(sk, pkg, NULL, pkg + HWR_HEADER + o);
        uint32_t plen = HWR_HEADER + o + HWR_MLDSA87_SIG;
        const uint8_t *keys[2] = {pk2, pk};
        hwr_image_t got;
        CHECK(hwr_package_verify(pkg, plen, HWR_ALG_MLDSA87, hwr_pq_verify, 0, keys, 2, &got) ==
              HWR_OK);
        CHECK(got.version == 2 && got.fn_id == 0x200 && got.payload_len == o);
        CHECK(hwr_package_verify(pkg, plen, HWR_ALG_MLDSA87, hwr_pq_verify, 0, keys, 1, &got) ==
              HWR_E_VERIFY);
        hwr_image_t other_im;
        CHECK(hwr_package_verify(img, n, HWR_ALG_MLDSA87, hwr_pq_verify, 0, keys, 2, &other_im) ==
              HWR_E_KIND); /* a bitstream is not a contract */
        CHECK(hwr_package_verify(pkg, plen, HWR_ALG_MLDSA87, hwr_pq_verify, 0, keys, 2, &got) ==
              HWR_OK);
        uint32_t off = 0, op, il, ol, cnt = 0;
        const uint8_t *vi, *vo;
        int rc;
        while ((rc = hwr_vectors_next(got.payload, got.payload_len, &off, &op, &vi, &il, &vo,
                                      &ol)) == HWR_OK)
            cnt++;
        CHECK(rc == 1 && cnt == 2);
        off = 0;
        CHECK(hwr_vectors_next(got.payload, got.payload_len - 1, &off, &op, &vi, &il, &vo, &ol) ==
              HWR_OK);
        CHECK(hwr_vectors_next(got.payload, got.payload_len - 1, &off, &op, &vi, &il, &vo, &ol) ==
              HWR_E_FORMAT);
    }

    /* R1 dual suite: ML-DSA-87 and SLH-DSA-SHAKE-256s, both required. */
    {
        static uint8_t dpk[HWR_DUAL_PK], dsk_m[4896], dsk_s[128];
        uint8_t sseed[96];
        memset(sseed, 7, sizeof sseed);
        pqm_mldsa87_keygen(seed, dpk, dsk_m);
        pqm_slh256s_keygen(sseed, dpk + HWR_MLDSA87_PK, dsk_s);
        static uint8_t dimg[HWR_HEADER + 256 + HWR_DUAL_SIG];
        hwr_image_t im;
        memset(&im, 0, sizeof im);
        im.alg = HWR_ALG_DUAL;
        im.kind = HWR_KIND_PARTIAL;
        im.target_class = TCLASS;
        im.fn_id = 0x200;
        im.version = 1;
        im.svn = 1;
        im.payload_len = 256;
        sha256(inc, 256, im.payload_sha256);
        hwr_build_header(&im, dimg);
        memcpy(dimg + HWR_HEADER, inc, 256);
        hwr_dual_sign(dsk_m, dsk_s, dimg, NULL, dimg + HWR_HEADER + 256);
        uint32_t dn = (uint32_t) sizeof dimg;
        static hwr_region_t rd;
        static hwa_rec_t recd[8];
        hwa_log_t ld;
        hwa_init(&ld, recd, 8);
        CHECK(hwr_region_init(&rd, 5, TCLASS, tdig, HWR_ALG_DUAL, hwr_pq_verify, 0, &SIM_OPS, &sim,
                              &ld) == HWR_OK);
        hwr_trust_key(&rd, dpk);
        /* break only the SLH-DSA half, then only the ML-DSA half */
        dimg[HWR_HEADER + 256 + HWR_MLDSA87_SIG + 100] ^= 1;
        CHECK(hwr_stage(&rd, dimg, dn) == HWR_E_VERIFY);
        dimg[HWR_HEADER + 256 + HWR_MLDSA87_SIG + 100] ^= 1;
        dimg[HWR_HEADER + 256 + 100] ^= 1;
        CHECK(hwr_stage(&rd, dimg, dn) == HWR_E_VERIFY);
        dimg[HWR_HEADER + 256 + 100] ^= 1;
        /* an ML-DSA-only image is not accepted by a dual region */
        n = make(img, inc, 0x200, 1, 1, sk, NULL);
        CHECK(hwr_stage(&rd, img, n) == HWR_E_FORMAT);
        CHECK(hwr_stage(&rd, dimg, dn) == HWR_OK && hwr_trial(&rd, 1) == HWR_OK &&
              hwr_commit(&rd) == HWR_OK && rd.state == HWR_ST_ACTIVE);
        CHECK(ld.n == 5 && hwa_forbidden(&ld) == 2 && hwa_verify(&ld));
    }

    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
