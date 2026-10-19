/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_hwkey_host.c — the machine's own chip, end to end: seal an account
 * key, store and reload it, sign, show that another device key cannot open
 * it, then run an offline grant through the outbox with that key signing
 * and settle it on a ledger. Where the chip keeps monotonic counters (a
 * TPM), a second grant is guarded by one (outbox.h R1-R4) and a restored
 * backup of the outbox is shown to be refused.
 *
 *   test_hwkey_host [--require-hw] [--soft-only] [--foreign] STATE_DIR
 *
 * A sealed key left in STATE_DIR by an earlier run must open again, or,
 * with --foreign (the directory came from another machine), must not.
 * Prints which provider was used. --require-hw fails when there is no chip
 * (run on a real Mac, a Windows PC with a TPM, or Linux with swtpm). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hwkey_host.h"
#include "../../../src/outbox/outbox.h"
#include "../../../src/mlkem/keccak.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
        }                                                                                          \
    } while (0)

/* CHECK, and give back the outcome */
#define CHECK_OK(c, msg)                                                                           \
    ((c) ? (pass++, 1) : (fail++, printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg), 0))

static hwk_host_t H;
static hwk_sealed_t K, K2, KL;
static uint8_t blob[HWK_ENCODED_MAX];
static uint8_t sig[HWK_SIG_BYTES];

typedef struct {
    const hwk_provider_t *p;
    const hwk_sealed_t *k;
} signer_t;

static int sign_cb(void *ctx, const uint8_t *msg, uint32_t len, const uint8_t *c, uint32_t cl,
                   uint8_t out[OBX_SIG_BYTES])
{
    signer_t *s = ctx;
    return hwk_sign(s->p, s->k, msg, len, c, cl, out) == HWK_OK ? 0 : 1;
}

static obx_device_t dev_disk;
static char dev_path[1024];
static int persist_cb(void *ctx, const obx_device_t *d)
{
    (void) ctx;
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", dev_path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return 1;
    int ok = fwrite(d, sizeof *d, 1, f) == 1;
    ok = (fflush(f) == 0) && ok;
    ok = (fclose(f) == 0) && ok;
#ifdef _WIN32
    remove(dev_path); /* Windows rename does not replace (test only) */
#endif
    if (!ok || rename(tmp, dev_path) != 0) return 1;
    memcpy(&dev_disk, d, sizeof *d);
    return 0;
}

/* outbox.h R1: the chip's counter for a grant */
static int ctr_read(void *ctx, const uint8_t id[32], uint64_t *v)
{
    return hwk_host_ctr(ctx, HWK_CTR_READ, id, v) == HWK_OK ? 0 : 1;
}
static int ctr_bump(void *ctx, const uint8_t id[32], uint64_t *v)
{
    return hwk_host_ctr(ctx, HWK_CTR_BUMP, id, v) == HWK_OK ? 0 : 1;
}

static int read_state(obx_device_t *d)
{
    FILE *f = fopen(dev_path, "rb");
    int ok = f && fread(d, sizeof *d, 1, f) == 1;
    if (f) fclose(f);
    return ok;
}

static pay_ledger_t L;
static obx_ledger_t LG;
static obx_device_t D, D2, D3, OLD;
static obx_entry_t E[3];
static uint64_t nreq;

static pay_posting_req_t *mk(void)
{
    static pay_posting_req_t r;
    uint8_t in[9], d[32];
    memset(&r, 0, sizeof r);
    nreq++;
    memcpy(in, &nreq, 8);
    in[8] = 'H';
    shake256(in, sizeof in, d, 32);
    memcpy(r.idem_key, d, 32);
    pay_uetr_from_random(d + 8, r.uetr);
    snprintf(r.e2e, sizeof r.e2e, "HK-%llu", (unsigned long long) nreq);
    return &r;
}

int main(int argc, char **argv)
{
    unsigned flags = 0;
    int foreign = 0;
    const char *dir = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--require-hw"))
            flags |= HWK_HOST_REQUIRE_HW;
        else if (!strcmp(argv[i], "--soft-only"))
            flags |= HWK_HOST_SOFT_ONLY;
        else if (!strcmp(argv[i], "--foreign"))
            foreign = 1;
        else
            dir = argv[i];
    }
    if (!dir) {
        fprintf(stderr, "usage: %s [--require-hw] [--soft-only] [--foreign] STATE_DIR\n", argv[0]);
        return 2;
    }
    char soft[1024], keyf[1024];
    snprintf(soft, sizeof soft, "%s/hwkey-soft.secret", dir);
    snprintf(keyf, sizeof keyf, "%s/account.zhk", dir);
    snprintf(dev_path, sizeof dev_path, "%s/outbox.state", dir);

    hwk_status_t st = hwk_host_open(&H, soft, flags);
    if (st != HWK_OK) {
        printf("no provider (%d): %s\n", st, H.why);
        return 1;
    }
    const char *kinds[] = {"none", "TPM 2.0", "Secure Enclave", "software"};
    printf("provider: %s (%s)%s%s\n", H.p.name, kinds[H.p.kind],
           H.why[0] ? "; chip not used: " : "", H.why);

    /* a key sealed by an earlier run (another process) still opens */
    FILE *f = fopen(keyf, "rb");
    if (f) {
        uint32_t m0 = (uint32_t) fread(blob, 1, sizeof blob, f);
        fclose(f);
        hwk_status_t o = hwk_decode(blob, m0, &K2);
        if (o == HWK_OK) o = hwk_check(&H.p, &K2);
        if (foreign) {
            CHECK(o == HWK_ERR_AUTH || o == HWK_ERR_HW,
                  "a key sealed on another machine's chip does not open here");
            printf("a key sealed on another chip was refused (%d), as it must be\n", o);
        } else {
            CHECK(o == HWK_OK, "a key sealed by an earlier run opens");
            printf("reopened the key sealed by the previous run\n");
        }
    }

    /* seal, store, reload, sign */
    CHECK(hwk_keygen(&H.p, (const uint8_t *) "zxv-account", 11, &K) == HWK_OK, "keygen");
    uint32_t n = hwk_encode(&K, blob, sizeof blob);
    f = fopen(keyf, "wb");
    CHECK(f && fwrite(blob, 1, n, f) == n && fclose(f) == 0, "sealed key written");
    memset(blob, 0, sizeof blob);
    f = fopen(keyf, "rb");
    uint32_t m = f ? (uint32_t) fread(blob, 1, sizeof blob, f) : 0;
    if (f) fclose(f);
    CHECK(m == n && hwk_decode(blob, m, &K2) == HWK_OK, "sealed key read back");
    static const uint8_t msg[] = "ZXV hardware key end to end";
    CHECK(hwk_sign(&H.p, &K2, msg, sizeof msg, 0, 0, sig) == HWK_OK &&
              pqm_mldsa87_verify(K2.pk, msg, sizeof msg, 0, 0, sig),
          "the reloaded key signs and the signature verifies");
    if (H.p.kind != HWK_KIND_SOFTWARE)
        CHECK(hwk_require_hardware(&K2) == HWK_OK, "the key is chip-sealed");

    /* a key sealed to another device key (another label) cannot be opened
     * with this one's handle: swap the handles */
    CHECK(hwk_keygen(&H.p, (const uint8_t *) "zxv-other", 9, &KL) == HWK_OK, "second key");
    memcpy(&K, &K2, sizeof K);
    memcpy(K.handle, KL.handle, KL.handle_len);
    K.handle_len = KL.handle_len;
    CHECK(hwk_check(&H.p, &K) == HWK_ERR_AUTH || hwk_check(&H.p, &K) == HWK_ERR_HW,
          "another device key cannot open the seal");

    /* offline grant -> outbox (this key signs) -> settlement */
    uint16_t usd;
    uint32_t iss, hold, payee;
    pay_receipt_t rc;
    pay_ledger_init(&L, NULL);
    pay_ledger_add_fiat(&L, "USD", 840, 2, &usd);
    pay_ledger_open(&L, 1, usd, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &iss);
    pay_ledger_open(&L, 2, usd, PAY_CAP_FINANCIAL, 0, &hold);
    pay_ledger_open(&L, 3, usd, PAY_CAP_FINANCIAL, 0, &payee);
    pay_ledger_issue(&L, mk(), iss, hold, 10000, &rc);
    signer_t ledger_signer = {&H.p, &KL}, dev_signer = {&H.p, &K2};
    uint8_t seed[32];
    hwk_host_random(0, seed, 32);
    obx_grant_t g;
    obx_result_t r;
    CHECK(obx_ledger_init(&LG, &L, usd, 900, seed, KL.pk, sign_cb, &ledger_signer, 10) == OBX_OK,
          "ledger");
    CHECK(obx_ledger_grant(&LG, 2, K2.pk, 2500, 4, 1, 100, &g) == OBX_OK, "grant 25.00");
    CHECK(obx_device_init(&D, &g, KL.pk, K2.pk) == OBX_OK, "outbox on the grant");
    CHECK(obx_device_spend(&D, 3, 1250, 2, sign_cb, &dev_signer, persist_cb, 0, &E[0]) == OBX_OK &&
              obx_device_spend(&D, 3, 700, 3, sign_cb, &dev_signer, persist_cb, 0, &E[1]) == OBX_OK,
          "two offline payments signed by the sealed key");
    CHECK(obx_device_spend(&D, 3, 600, 4, sign_cb, &dev_signer, persist_cb, 0, &E[2]) ==
              OBX_ERR_LIMIT,
          "the third would exceed the grant");
    f = fopen(dev_path, "rb");
    CHECK(f && fread(&D2, sizeof D2, 1, f) == 1, "outbox state read back from disk");
    if (f) fclose(f);
    CHECK(!memcmp(&D2, &D, sizeof D), "disk matches memory (write-ahead)");
    CHECK(obx_verify_offline(KL.pk, &g, K2.pk, &E[1]) == OBX_OK, "payee check offline");
    for (int i = 0; i < 2; i++) {
        int32_t at = obx_device_next_pending(&D2, 0);
        CHECK(at == i && obx_ledger_submit(&LG, &D2.e[at], 5, &r) == OBX_OK &&
                  r == OBX_R_ACCEPTED && obx_device_ack(&D2, D2.e[at].seq, r, persist_cb, 0) == 0,
              "reconnect: queued entry settles");
    }
    CHECK(obx_ledger_submit(&LG, &E[0], 6, &r) == OBX_OK && r == OBX_R_DUPLICATE,
          "the payee submitting again is a duplicate");
    CHECK(L.acct[payee].debit == 1950 && L.acct[hold].debit == 7500 && obx_ledger_check(&LG) &&
              pay_ledger_check(&L),
          "balances: payee 19.50, holder 75.00, escrow 5.50");

    /* R1-R4 with the chip's own counter */
    obx_grant_t g2;
    uint64_t v = 0, v2 = 0;
    CHECK(obx_ledger_grant(&LG, 2, K2.pk, 1000, 4, 2, 100, &g2) == OBX_OK, "second grant");
    hwk_status_t cs = hwk_host_ctr(&H, HWK_CTR_CREATE, g2.id, &v);
#if !defined(__APPLE__) && !defined(_WIN32)
    if (H.p.kind == HWK_KIND_TPM) CHECK(cs == HWK_OK, "a TPM keeps a counter for the grant");
#endif
    if (cs == HWK_ERR_UNAVAILABLE) {
        printf("no chip counter on this machine: the outbox runs unguarded (outbox.h HONEST "
               "LIMITS)\n");
    } else if (CHECK_OK(cs == HWK_OK, "counter created")) {
        obx_counter_t ctr = {&H, ctr_read, ctr_bump};
        snprintf(dev_path, sizeof dev_path, "%s/outbox2.state", dir);
        obx_entry_t e1, e2, e3;
        CHECK(obx_device_init(&D3, &g2, KL.pk, K2.pk) == OBX_OK &&
                  obx_device_guard(&D3, &ctr, persist_cb, 0) == OBX_OK && D3.ctr == v,
              "R1: the outbox is bound to the chip counter");
        CHECK(obx_device_spend_guarded(&D3, &ctr, 3, 100, 7, sign_cb, &dev_signer, persist_cb, 0,
                                       &e1) == OBX_OK &&
                  read_state(&OLD) && OLD.ctr == v + 1,
              "R3: spend 1 counts the chip up; the state on disk is backed up");
        CHECK(obx_device_spend_guarded(&D3, &ctr, 3, 200, 8, sign_cb, &dev_signer, persist_cb, 0,
                                       &e2) == OBX_OK &&
                  hwk_host_ctr(&H, HWK_CTR_READ, g2.id, &v2) == HWK_OK && v2 == v + 2,
              "spend 2");
        CHECK(obx_device_spend_guarded(&OLD, &ctr, 3, 200, 9, sign_cb, &dev_signer, persist_cb, 0,
                                       &e3) == OBX_ERR_ROLLBACK,
              "R2: the restored backup cannot sign number 2 again");
        printf("a restored backup was refused by the chip counter (%llu on disk, %llu in the "
               "chip)\n",
               (unsigned long long) OLD.ctr, (unsigned long long) v2);
        CHECK(obx_ledger_submit(&LG, &e1, 9, &r) == OBX_OK && r == OBX_R_ACCEPTED &&
                  obx_ledger_submit(&LG, &e2, 9, &r) == OBX_OK && r == OBX_R_ACCEPTED &&
                  L.acct[payee].debit == 2250 && obx_ledger_check(&LG) && pay_ledger_check(&L),
              "the guarded entries settle");
        CHECK(hwk_host_ctr(&H, HWK_CTR_RELEASE, g2.id, 0) == HWK_OK &&
                  hwk_host_ctr(&H, HWK_CTR_READ, g2.id, &v2) != HWK_OK,
              "the counter is released with its grant");
    }

    hwk_host_close(&H);
    printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
