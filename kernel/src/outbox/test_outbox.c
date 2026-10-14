/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_outbox.c — offline grants and the outbox on a real pay_ledger, with
 * both keys sealed through hwkey (outbox.h G1-G3, E1-E4, S1-S4, D1-D5,
 * C1-C3): out-of-order and repeated settlement, a device restored from a
 * backup signing the same number twice, forged totals, failed writes, the
 * close and expiry refunds, and a seeded random run that checks after
 * every step that no money is created and the escrow is exact. */
#include <stdio.h>
#include <string.h>
#include "outbox.h"
#include "../hwkey/hwkey.h"
#include "../mlkem/keccak.h"

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

static uint64_t rng_n;
static int rng(void *ctx, uint8_t *out, uint32_t len)
{
    (void) ctx;
    uint8_t b[9];
    memcpy(b, &rng_n, 8);
    b[8] = 'O';
    rng_n++;
    shake256(b, sizeof b, out, len);
    return 0;
}

/* one machine's chip (software stand-in) and a sealed key on it */
typedef struct {
    hwk_soft_t chip;
    hwk_provider_t p;
    hwk_sealed_t key;
    int fail;
} signer_t;

static void signer_make(signer_t *s, uint8_t machine, const char *label)
{
    memset(s, 0, sizeof *s);
    memset(s->chip.secret, machine, 32);
    s->chip.rng = rng;
    hwk_soft_provider(&s->p, &s->chip);
    if (hwk_keygen(&s->p, (const uint8_t *) label, (uint32_t) strlen(label), &s->key) != HWK_OK)
        printf("keygen failed\n");
}

static int sign_cb(void *ctx, const uint8_t *msg, uint32_t len, const uint8_t *c, uint32_t cl,
                   uint8_t sig[OBX_SIG_BYTES])
{
    signer_t *s = ctx;
    if (s->fail) return 1;
    return hwk_sign(&s->p, &s->key, msg, len, c, cl, sig) == HWK_OK ? 0 : 1;
}

/* outbox.h E1's encoding, written out independently: re-sign an edited entry */
static void resign(obx_entry_t *e, signer_t *s)
{
    static const uint8_t ctx[13] = "ZXV-OBX-ENTRY";
    uint8_t b[96];
    uint32_t o = 0;
    memcpy(b, e->grant_id, 32);
    o = 32;
    for (int i = 0; i < 4; i++) b[o++] = (uint8_t) (e->seq >> (8 * i));
    for (int i = 0; i < 8; i++) b[o++] = (uint8_t) (e->amount >> (8 * i));
    for (int i = 0; i < 8; i++) b[o++] = (uint8_t) (e->cumulative >> (8 * i));
    for (int i = 0; i < 4; i++) b[o++] = (uint8_t) (e->payee >> (8 * i));
    for (int i = 0; i < 8; i++) b[o++] = (uint8_t) (e->tick >> (8 * i));
    memcpy(b + o, e->prev, 32);
    sign_cb(s, b, sizeof b, ctx, sizeof ctx, e->sig);
}

/* write-ahead store: keeps the last persisted copy, can be told to fail */
static int persist_fail, persist_calls;
static obx_device_t disk;
static int persist_cb(void *ctx, const obx_device_t *d)
{
    (void) ctx;
    persist_calls++;
    if (persist_fail) return 1;
    memcpy(&disk, d, sizeof *d);
    return 0;
}

static pay_ledger_t L;
static obx_ledger_t LG;
static uint16_t usd;
static uint32_t iss, holder_acct, a_acct, b_acct;
static signer_t ledger_key, dev1, dev2, dev3, dev4;
static uint64_t nreq;
enum { ISSUER = 1, HOLDER = 2, PAYEE_A = 3, PAYEE_B = 4, PAYEE_LATE = 5, ESCROW = 900 };
#define SUPPLY 1000000ull

static pay_posting_req_t *mk(void)
{
    static pay_posting_req_t r;
    uint8_t in[9], d[32];
    memset(&r, 0, sizeof r);
    nreq++;
    memcpy(in, &nreq, 8);
    in[8] = 'T';
    shake256(in, sizeof in, d, 32);
    memcpy(r.idem_key, d, 32);
    pay_uetr_from_random(d + 8, r.uetr);
    snprintf(r.e2e, sizeof r.e2e, "OBT-%llu", (unsigned long long) nreq);
    return &r;
}

static uint64_t bal(uint32_t a)
{
    return pay_ledger_account(&L, a)->debit;
}

static uint32_t open_acct(uint32_t owner)
{
    uint32_t a = 0;
    pay_ledger_open(&L, owner, usd, PAY_CAP_FINANCIAL, 0, &a);
    return a;
}

/* holders' money + escrow == what was issued (no money created) */
static uint64_t circulating(void)
{
    uint64_t t = 0;
    for (uint32_t a = 0; a < L.n_accounts; a++)
        if (L.acct[a].asset == usd && a != iss) t += L.acct[a].debit;
    return t;
}

static void setup(void)
{
    pay_receipt_t rc;
    uint8_t seed[32];
    memset(seed, 0x5e, 32);
    pay_ledger_init(&L, NULL);
    nreq = 0;
    pay_ledger_add_fiat(&L, "USD", 840, 2, &usd);
    pay_ledger_open(&L, ISSUER, usd, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &iss);
    holder_acct = open_acct(HOLDER);
    a_acct = open_acct(PAYEE_A);
    b_acct = open_acct(PAYEE_B);
    pay_ledger_issue(&L, mk(), iss, holder_acct, SUPPLY, &rc);
    CHECK(obx_ledger_init(&LG, &L, usd, ESCROW, seed, ledger_key.key.pk, sign_cb, &ledger_key,
                          100) == OBX_OK,
          "ledger outbox set up");
}

static int audit(void)
{
    return obx_ledger_check(&LG) && pay_ledger_check(&L) && circulating() == SUPPLY;
}

static obx_grant_t G;
static obx_device_t D, saved;
static obx_entry_t E[8], X;
static obx_result_t R;

static void test_grant(void)
{
    setup();
    CHECK(obx_ledger_grant(&LG, HOLDER, dev1.key.pk, 300, 8, 10, 1000, &G) == OBX_OK, "grant");
    CHECK(bal(holder_acct) == SUPPLY - 300 && bal(LG.escrow_acct) == 300,
          "G1: the limit moves to escrow before anything is spent");
    CHECK(audit(), "C3 after the grant");
    obx_grant_t g2;
    CHECK(obx_ledger_grant(&LG, HOLDER, dev2.key.pk, SUPPLY, 8, 10, 1000, &g2) == OBX_ERR_LEDGER &&
              audit(),
          "G1: a grant larger than the balance is refused, nothing moves");
    CHECK(obx_ledger_grant(&LG, PAYEE_LATE, dev2.key.pk, 1, 8, 10, 1000, &g2) == OBX_ERR_LEDGER,
          "holder without an account refused");
    CHECK(obx_ledger_grant(&LG, HOLDER, dev2.key.pk, 0, 8, 10, 1000, &g2) == OBX_ERR_ARG &&
              obx_ledger_grant(&LG, HOLDER, dev2.key.pk, 5, 0, 10, 1000, &g2) == OBX_ERR_ARG &&
              obx_ledger_grant(&LG, HOLDER, dev2.key.pk, 5, OBX_MAX_ENTRIES + 1, 10, 1000, &g2) ==
                  OBX_ERR_ARG &&
              obx_ledger_grant(&LG, HOLDER, dev2.key.pk, 5, 8, 10, 10, &g2) == OBX_ERR_ARG &&
              obx_ledger_grant(&LG, ESCROW, dev2.key.pk, 5, 8, 10, 100, &g2) == OBX_ERR_ARG,
          "zero limit, zero or too many entries, past expiry, escrow as holder refused");

    CHECK(obx_device_init(&D, &G, dev1.key.pk, dev1.key.pk) == OBX_ERR_SIG,
          "device refuses a grant not signed by its ledger");
    CHECK(obx_device_init(&D, &G, ledger_key.key.pk, dev2.key.pk) == OBX_ERR_SIG,
          "device refuses a grant naming another device");
    obx_grant_t bad = G;
    bad.limit++;
    CHECK(obx_device_init(&D, &bad, ledger_key.key.pk, dev1.key.pk) == OBX_ERR_SIG,
          "an edited grant fails the ledger signature");
    CHECK(obx_device_init(&D, &G, ledger_key.key.pk, dev1.key.pk) == OBX_OK, "device init");
}

static void test_spend_and_settle(void)
{
    persist_fail = 0;
    CHECK(obx_device_spend(&D, PAYEE_A, 100, 1, sign_cb, &dev1, persist_cb, 0, &E[0]) == OBX_OK,
          "spend 1");
    CHECK(obx_device_spend(&D, PAYEE_B, 50, 2, sign_cb, &dev1, persist_cb, 0, &E[1]) == OBX_OK,
          "spend 2");
    CHECK(E[1].seq == 2 && E[1].cumulative == 150 && D.cumulative == 150, "running total");
    memcpy(&saved, &D, sizeof D); /* the "backup" used in test_rollback */
    CHECK(obx_device_spend(&D, PAYEE_A, 100, 3, sign_cb, &dev1, persist_cb, 0, &E[2]) == OBX_OK,
          "spend 3");
    CHECK(obx_device_spend(&D, PAYEE_A, 51, 4, sign_cb, &dev1, persist_cb, 0, &X) ==
                  OBX_ERR_LIMIT &&
              D.n == 3,
          "E2: 251 of 300 refused, nothing queued");

    /* E3: a failed write hands nothing out and leaves the state as it was */
    persist_fail = 1;
    memset(&X, 0xee, sizeof X);
    CHECK(obx_device_spend(&D, PAYEE_A, 10, 4, sign_cb, &dev1, persist_cb, 0, &X) ==
                  OBX_ERR_PERSIST &&
              D.n == 3 && D.next_seq == 4 && D.cumulative == 250 && X.seq == 0xeeeeeeeeu,
          "E3: persist failure -> no entry returned, state unchanged");
    persist_fail = 0;
    CHECK(!memcmp(&disk, &D, sizeof D), "disk holds exactly the device state");
    dev1.fail = 1;
    CHECK(obx_device_spend(&D, PAYEE_A, 10, 4, sign_cb, &dev1, persist_cb, 0, &X) ==
                  OBX_ERR_SIGNER &&
              D.n == 3,
          "signer failure -> nothing queued");
    dev1.fail = 0;

    /* E4: what a payee checks offline */
    for (int i = 0; i < 3; i++)
        CHECK(obx_verify_offline(ledger_key.key.pk, &G, dev1.key.pk, &E[i]) == OBX_OK,
              "payee verifies offline");
    X = E[0];
    X.amount = 99;
    CHECK(obx_verify_offline(ledger_key.key.pk, &G, dev1.key.pk, &X) == OBX_ERR_SIG,
          "an edited amount fails offline");
    CHECK(obx_verify_offline(ledger_key.key.pk, &G, dev2.key.pk, &E[0]) == OBX_ERR_SIG,
          "wrong device key fails offline");
    CHECK(obx_verify_offline(dev1.key.pk, &G, dev1.key.pk, &E[0]) == OBX_ERR_SIG,
          "wrong ledger key fails offline");

    /* S1: payee of entry 3 reconnects first */
    CHECK(obx_ledger_submit(&LG, &E[2], 20, &R) == OBX_OK && R == OBX_R_ACCEPTED &&
              bal(a_acct) == 100 && audit(),
          "entry 3 settles before 1 and 2");
    CHECK(obx_ledger_submit(&LG, &E[0], 21, &R) == OBX_OK && R == OBX_R_ACCEPTED &&
              bal(a_acct) == 200,
          "entry 1");
    CHECK(obx_ledger_submit(&LG, &E[1], 22, &R) == OBX_OK && R == OBX_R_ACCEPTED &&
              bal(b_acct) == 50 && audit(),
          "entry 2, links checked against both neighbours");
    CHECK(bal(LG.escrow_acct) == 50, "50 of 300 left in escrow");

    /* S3: the device reconnects and submits everything again */
    for (int i = 0; i < 3; i++) {
        CHECK(obx_ledger_submit(&LG, &E[i], 30, &R) == OBX_OK && R == OBX_R_DUPLICATE,
              "S3: resubmission is DUPLICATE");
        CHECK(obx_device_ack(&D, E[i].seq, R, persist_cb, 0) == OBX_OK, "ack");
    }
    CHECK(bal(a_acct) == 200 && bal(b_acct) == 50 && audit(), "duplicates move nothing");
    CHECK(obx_device_next_pending(&D, 0) == -1, "outbox empty after acks");
}

static void test_rollback_conflict(void)
{
    /* the device is restored from the backup taken after entry 2 and spends
     * again: same number 3, different payee */
    CHECK(obx_device_spend(&saved, PAYEE_B, 100, 3, sign_cb, &dev1, persist_cb, 0, &X) == OBX_OK &&
              X.seq == 3,
          "a restored device re-signs number 3");
    CHECK(obx_verify_offline(ledger_key.key.pk, &G, dev1.key.pk, &X) == OBX_OK,
          "it looks valid offline (the honest limit)");
    uint64_t b_before = bal(b_acct);
    CHECK(obx_ledger_submit(&LG, &X, 31, &R) == OBX_OK && R == OBX_R_CONFLICT &&
              bal(b_acct) == b_before && audit(),
          "D2: the second number 3 is a CONFLICT and is not paid");
    CHECK(obx_ledger_flagged(&LG, G.device_id), "the device is flagged");
    obx_grant_t g2;
    CHECK(obx_ledger_grant(&LG, HOLDER, dev1.key.pk, 10, 4, 40, 400, &g2) == OBX_ERR_FLAGGED,
          "G3: a flagged device gets no new grant");
    CHECK(obx_device_ack(&saved, 3, R, persist_cb, 0) == OBX_OK && saved.state[2] == OBX_E_REJECTED,
          "the restored device records the rejection");
}

static void test_bad_entries(void)
{
    setup();
    CHECK(obx_ledger_grant(&LG, HOLDER, dev2.key.pk, 200, 4, 10, 1000, &G) == OBX_OK, "grant 2");
    CHECK(obx_device_init(&D, &G, ledger_key.key.pk, dev2.key.pk) == OBX_OK, "init");
    CHECK(obx_device_spend(&D, PAYEE_LATE, 40, 1, sign_cb, &dev2, persist_cb, 0, &E[0]) == OBX_OK,
          "spend to a payee with no account yet");
    CHECK(obx_ledger_submit(&LG, &E[0], 20, &R) == OBX_OK && R == OBX_R_NO_PAYEE && audit(),
          "S4: NO_PAYEE, nothing moves");
    CHECK(obx_device_ack(&D, 1, R, persist_cb, 0) == OBX_OK && obx_device_next_pending(&D, 0) == 0,
          "NO_PAYEE stays pending on the device");
    uint32_t late = open_acct(PAYEE_LATE);
    CHECK(obx_ledger_submit(&LG, &E[0], 21, &R) == OBX_OK && R == OBX_R_ACCEPTED &&
              bal(late) == 40 && audit(),
          "S4: the same entry settles once the payee has an account");

    X = E[0];
    X.sig[100] ^= 1;
    CHECK(obx_ledger_submit(&LG, &X, 22, &R) == OBX_OK && R == OBX_R_BAD_SIG, "bad signature");
    X = E[0];
    X.grant_id[0] ^= 1;
    CHECK(obx_ledger_submit(&LG, &X, 22, &R) == OBX_OK && R == OBX_R_UNKNOWN_GRANT,
          "unknown grant");

    /* a device that lies to itself about its limit or entry cap */
    obx_device_t liar;
    memcpy(&liar, &D, sizeof D);
    liar.grant.limit = 1000;
    CHECK(obx_device_spend(&liar, PAYEE_A, 500, 2, sign_cb, &dev2, persist_cb, 0, &X) == OBX_OK,
          "liar signs 540 cumulative against a 200 grant");
    CHECK(obx_verify_offline(ledger_key.key.pk, &G, dev2.key.pk, &X) == OBX_ERR_LIMIT,
          "E4: the payee catches it offline");
    uint64_t esc = bal(LG.escrow_acct);
    CHECK(obx_ledger_submit(&LG, &X, 23, &R) == OBX_OK && R == OBX_R_OVER_LIMIT &&
              bal(LG.escrow_acct) == esc && audit(),
          "D1/D4: OVER_LIMIT, nothing paid");
    CHECK(obx_ledger_flagged(&LG, G.device_id), "and flagged");

    memcpy(&liar, &D, sizeof D);
    liar.grant.max_entries = 10;
    liar.next_seq = 9;
    CHECK(obx_device_spend(&liar, PAYEE_A, 1, 2, sign_cb, &dev2, persist_cb, 0, &X) == OBX_OK,
          "liar signs number 9 of 4");
    CHECK(obx_ledger_submit(&LG, &X, 24, &R) == OBX_OK && R == OBX_R_BAD_SEQ, "BAD_SEQ");

    /* D3: number 2 whose running total skips money */
    memcpy(&liar, &D, sizeof D);
    liar.cumulative = 100; /* claims entry 1 was 100, it was 40 */
    CHECK(obx_device_spend(&liar, PAYEE_A, 10, 2, sign_cb, &dev2, persist_cb, 0, &X) == OBX_OK,
          "liar signs a running total that does not follow entry 1");
    CHECK(obx_ledger_submit(&LG, &X, 25, &R) == OBX_OK && R == OBX_R_CONFLICT && audit(),
          "D3: CONFLICT with the accepted neighbour");
    memcpy(&liar, &D, sizeof D);
    memset(liar.prev, 7, 32);
    CHECK(obx_device_spend(&liar, PAYEE_A, 10, 2, sign_cb, &dev2, persist_cb, 0, &X) == OBX_OK &&
              obx_ledger_submit(&LG, &X, 26, &R) == OBX_OK && R == OBX_R_CONFLICT,
          "D3: a broken previous-hash link is a CONFLICT");

    /* D4: a running total smaller than the amount, signed by the real key */
    obx_device_t l2;
    memcpy(&l2, &D, sizeof D);
    CHECK(obx_device_spend(&l2, PAYEE_A, 5, 2, sign_cb, &dev2, persist_cb, 0, &X) == OBX_OK,
          "spend 2 on a copy");
    X.cumulative = 4;
    resign(&X, &dev2);
    CHECK(obx_ledger_submit(&LG, &X, 27, &R) == OBX_OK && R == OBX_R_BAD_ENTRY && audit(),
          "D4: total below its own amount is BAD_ENTRY");
    X.cumulative = 45;
    resign(&X, &dev2);
    CHECK(obx_ledger_submit(&LG, &X, 28, &R) == OBX_OK && R == OBX_R_ACCEPTED && audit(),
          "re-signed with the true total it settles (the encoding matches the spec)");
    memcpy(&l2, &D, sizeof D);
    l2.cumulative = UINT64_MAX - 2;
    l2.grant.limit = UINT64_MAX;
    CHECK(obx_device_spend(&l2, PAYEE_A, 5, 2, sign_cb, &dev2, persist_cb, 0, &X) == OBX_ERR_LIMIT,
          "the device refuses a total that would overflow");

    CHECK(obx_ledger_submit(NULL, &E[0], 1, &R) == OBX_ERR_ARG &&
              obx_ledger_submit(&LG, NULL, 1, &R) == OBX_ERR_ARG &&
              obx_ledger_submit(&LG, &E[0], 1, NULL) == OBX_ERR_ARG,
          "NULL refused");
    CHECK(obx_device_spend(&D, PAYEE_A, 0, 1, sign_cb, &dev2, persist_cb, 0, &X) == OBX_ERR_ARG,
          "zero amount refused");
    CHECK(obx_device_ack(&D, 0, OBX_R_ACCEPTED, persist_cb, 0) == OBX_ERR_ARG &&
              obx_device_ack(&D, 9, OBX_R_ACCEPTED, persist_cb, 0) == OBX_ERR_ARG,
          "ack out of range refused");
}

static void test_close_and_expiry(void)
{
    setup();
    CHECK(obx_ledger_grant(&LG, HOLDER, dev3.key.pk, 500, 8, 10, 1000, &G) == OBX_OK, "grant 3");
    CHECK(obx_device_init(&D, &G, ledger_key.key.pk, dev3.key.pk) == OBX_OK, "init");
    CHECK(obx_device_spend(&D, PAYEE_A, 70, 1, sign_cb, &dev3, persist_cb, 0, &E[0]) == OBX_OK &&
              obx_device_spend(&D, PAYEE_B, 30, 2, sign_cb, &dev3, persist_cb, 0, &E[1]) == OBX_OK,
          "two spends");
    memcpy(&saved, &D, sizeof D);
    CHECK(obx_ledger_submit(&LG, &E[0], 20, &R) == OBX_OK && R == OBX_R_ACCEPTED, "entry 1");
    obx_close_t c;
    CHECK(obx_device_close(&D, sign_cb, &dev3, persist_cb, 0, &c) == OBX_OK && c.last_seq == 2 &&
              c.cumulative == 100,
          "device closes at 2 entries, 100");
    CHECK(obx_device_spend(&D, PAYEE_A, 1, 3, sign_cb, &dev3, persist_cb, 0, &X) == OBX_ERR_STATE,
          "no spending after close");
    uint64_t h0 = bal(holder_acct);
    obx_close_t bad = c;
    bad.cumulative = 10;
    CHECK(obx_ledger_close(&LG, &bad, 21) == OBX_ERR_SIG, "an edited close fails its signature");
    CHECK(obx_ledger_close(&LG, &c, 21) == OBX_OK && bal(holder_acct) == h0 + 400 &&
              bal(LG.escrow_acct) == 30 && audit(),
          "C1: 400 unspent returns at once, 30 stays for entry 2");
    CHECK(obx_ledger_close(&LG, &c, 22) == OBX_OK && bal(holder_acct) == h0 + 400,
          "closing twice changes nothing");
    /* the restored device signs a number 3 after its own close */
    CHECK(obx_device_spend(&saved, PAYEE_A, 5, 3, sign_cb, &dev3, persist_cb, 0, &X) == OBX_OK,
          "restored device signs number 3");
    CHECK(obx_ledger_submit(&LG, &X, 23, &R) == OBX_OK && R == OBX_R_CONFLICT && audit(),
          "D5: an entry after the close is a CONFLICT");
    CHECK(obx_ledger_submit(&LG, &E[1], 24, &R) == OBX_OK && R == OBX_R_ACCEPTED &&
              bal(b_acct) == 30 && bal(LG.escrow_acct) == 0 && audit(),
          "entry 2 still settles after the close");
    CHECK(LG.slot[0].finished, "grant finished once everything it covers is paid");
    CHECK(obx_ledger_submit(&LG, &E[1], 25, &R) == OBX_OK && R == OBX_R_DUPLICATE,
          "a finished grant still answers DUPLICATE");

    /* C2: expiry with no close */
    obx_grant_t g4;
    CHECK(obx_ledger_grant(&LG, HOLDER, dev4.key.pk, 90, 4, 30, 300, &g4) == OBX_OK, "grant 4");
    obx_device_t d4;
    CHECK(obx_device_init(&d4, &g4, ledger_key.key.pk, dev4.key.pk) == OBX_OK, "init 4");
    CHECK(obx_device_spend(&d4, PAYEE_A, 20, 31, sign_cb, &dev4, persist_cb, 0, &E[2]) == OBX_OK &&
              obx_device_spend(&d4, PAYEE_B, 25, 32, sign_cb, &dev4, persist_cb, 0, &E[3]) ==
                  OBX_OK,
          "two spends on grant 4");
    CHECK(obx_ledger_submit(&LG, &E[2], 350, &R) == OBX_OK && R == OBX_R_ACCEPTED,
          "inside the grace window (expiry 300, grace 100)");
    CHECK(obx_ledger_expire(&LG, 399) == 0 && bal(LG.escrow_acct) == 70, "not yet");
    uint64_t h1 = bal(holder_acct);
    CHECK(obx_ledger_expire(&LG, 400) == 1 && bal(holder_acct) == h1 + 70 &&
              bal(LG.escrow_acct) == 0 && audit(),
          "C2: at expiry + grace the rest returns to the holder");
    CHECK(obx_ledger_submit(&LG, &E[3], 401, &R) == OBX_OK && R == OBX_R_EXPIRED && audit(),
          "late entry: EXPIRED, nothing moves");
    CHECK(obx_ledger_expire(&LG, 10000) == 0, "expiring again does nothing");
}

static void test_slots(void)
{
    setup();
    signer_t many;
    signer_make(&many, 0x77, "slots");
    obx_grant_t g;
    for (uint32_t i = 0; i < OBX_MAX_GRANTS; i++)
        CHECK(obx_ledger_grant(&LG, HOLDER, many.key.pk, 1, 1, 10, 20 + i, &g) == OBX_OK, "fill");
    CHECK(obx_ledger_grant(&LG, HOLDER, many.key.pk, 1, 1, 10, 50, &g) == OBX_ERR_FULL,
          "no free slot");
    CHECK(obx_ledger_expire(&LG, 120) == 1 && audit(), "the first grant expires");
    CHECK(obx_ledger_grant(&LG, HOLDER, many.key.pk, 1, 1, 130, 500, &g) == OBX_OK && audit(),
          "its slot is reused");
}

/* seeded random run: three devices, shuffled and repeated submissions */
static void test_random(void)
{
    setup();
    signer_t *devs[3] = {&dev1, &dev2, &dev3};
    static obx_device_t dv[3];
    static obx_entry_t pool[3 * 12];
    obx_grant_t g[3];
    uint32_t np = 0;
    uint8_t r[4];
    for (int d = 0; d < 3; d++) {
        CHECK(obx_ledger_grant(&LG, HOLDER, devs[d]->key.pk, 1000, 12, 10, 100000, &g[d]) == OBX_OK,
              "grant");
        CHECK(obx_device_init(&dv[d], &g[d], ledger_key.key.pk, devs[d]->key.pk) == OBX_OK, "init");
        for (int k = 0; k < 12; k++) {
            rng(0, r, 4);
            uint64_t amt = 1 + r[0] % 120;
            obx_entry_t e;
            obx_status_t st = obx_device_spend(&dv[d], PAYEE_A + (r[1] & 1), amt, k, sign_cb,
                                               devs[d], persist_cb, 0, &e);
            if (st == OBX_OK)
                pool[np++] = e;
            else
                CHECK(st == OBX_ERR_LIMIT, "only the limit stops a spend");
        }
    }
    uint64_t paid = 0, a0 = bal(a_acct), b0 = bal(b_acct);
    int ok = 1;
    for (int step = 0; step < 300; step++) {
        rng(0, r, 4);
        const obx_entry_t *e = &pool[(r[0] | (r[1] << 8)) % np];
        obx_result_t res;
        if (obx_ledger_submit(&LG, e, 50 + step, &res) != OBX_OK) ok = 0;
        if (res == OBX_R_ACCEPTED)
            paid += e->amount;
        else if (res != OBX_R_DUPLICATE)
            ok = 0;
        if (!audit()) ok = 0;
    }
    for (uint32_t i = 0; i < np; i++) {
        obx_result_t res;
        obx_ledger_submit(&LG, &pool[i], 400, &res);
        if (res == OBX_R_ACCEPTED) paid += pool[i].amount;
    }
    uint64_t want = dv[0].cumulative + dv[1].cumulative + dv[2].cumulative;
    CHECK(ok, "every random submission is ACCEPTED or DUPLICATE and every audit holds");
    CHECK(paid == want && bal(a_acct) + bal(b_acct) - a0 - b0 == want,
          "each entry paid exactly once, whatever the order");
    CHECK(bal(LG.escrow_acct) == 3000 - want && audit(), "escrow holds exactly the unspent part");
    CHECK(LG.conflicts == 0, "no conflicts among honest devices");
}

/* ---- outbox.h encodings written out independently: grant and close ---- */
static void le(uint8_t *b, uint32_t *o, uint64_t v, int n)
{
    for (int i = 0; i < n; i++) b[(*o)++] = (uint8_t) (v >> (8 * i));
}

static void resign_grant(obx_grant_t *g, signer_t *s)
{
    static const uint8_t ctx[13] = "ZXV-OBX-GRANT";
    uint8_t b[130];
    uint32_t o = 0;
    memcpy(b, g->id, 32);
    memcpy(b + 32, g->ledger_id, 32);
    memcpy(b + 64, g->device_id, 32);
    o = 96;
    le(b, &o, g->holder, 4);
    le(b, &o, g->asset, 2);
    le(b, &o, g->limit, 8);
    le(b, &o, g->max_entries, 4);
    le(b, &o, g->issued_tick, 8);
    le(b, &o, g->expires_tick, 8);
    sign_cb(s, b, sizeof b, ctx, sizeof ctx, g->sig);
}

static void resign_close(obx_close_t *c, signer_t *s)
{
    static const uint8_t ctx[13] = "ZXV-OBX-CLOSE";
    uint8_t b[76];
    uint32_t o = 32;
    memcpy(b, c->grant_id, 32);
    le(b, &o, c->last_seq, 4);
    le(b, &o, c->cumulative, 8);
    memcpy(b + o, c->last_hash, 32);
    sign_cb(s, b, sizeof b, ctx, sizeof ctx, c->sig);
}

static int all_zero(const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++)
        if (b[i]) return 0;
    return 1;
}

static obx_entry_t T; /* template entry for the offline checks */
static obx_result_t offline(void)
{
    resign(&T, &dev1);
    return (obx_result_t) obx_verify_offline(ledger_key.key.pk, &G, dev1.key.pk, &T);
}

static void test_ids_and_offline_rules(void)
{
    setup();
    CHECK(pay_ledger_account(&L, LG.escrow_acct)->flags == 0, "escrow is a plain account");
    CHECK(obx_ledger_grant(&LG, HOLDER, dev1.key.pk, 100, 8, 10, 1000, &G) == OBX_OK, "grant 0");
    obx_grant_t g1;
    CHECK(obx_ledger_grant(&LG, HOLDER, dev2.key.pk, 100, 8, 10, 1000, &g1) == OBX_OK, "grant 1");
    uint8_t in[41], want[32];
    memset(in, 0x5e, 32);
    in[32] = 'G';
    memset(in + 33, 0, 8);
    shake256(in, sizeof in, want, 32);
    CHECK(!memcmp(G.id, want, 32), "grant id = SHAKE256(seed || 'G' || n), n = 0");
    in[33] = 1;
    shake256(in, sizeof in, want, 32);
    CHECK(!memcmp(g1.id, want, 32), "and n = 1 for the next");
    obx_key_id(ledger_key.key.pk, want);
    CHECK(!memcmp(G.ledger_id, want, 32), "ledger id = hash of its key");
    obx_key_id(dev1.key.pk, want);
    CHECK(!memcmp(G.device_id, want, 32), "device id = hash of its key");
    resign_grant(&G, &ledger_key);
    CHECK(obx_device_init(&D, &G, ledger_key.key.pk, dev1.key.pk) == OBX_OK,
          "a grant re-signed from the written-out encoding is valid");

    obx_grant_t bad = G;
    bad.ledger_id[0] ^= 1;
    resign_grant(&bad, &ledger_key);
    CHECK(obx_device_init(&D, &bad, ledger_key.key.pk, dev1.key.pk) == OBX_ERR_SIG,
          "a grant naming another ledger is refused even with a valid signature");
    bad = G;
    bad.max_entries = 0;
    resign_grant(&bad, &ledger_key);
    CHECK(obx_device_init(&D, &bad, ledger_key.key.pk, dev1.key.pk) == OBX_ERR_ARG, "0 entries");
    bad.max_entries = OBX_MAX_ENTRIES + 1;
    resign_grant(&bad, &ledger_key);
    CHECK(obx_device_init(&D, &bad, ledger_key.key.pk, dev1.key.pk) == OBX_ERR_ARG, "65 entries");
    bad.max_entries = OBX_MAX_ENTRIES;
    resign_grant(&bad, &ledger_key);
    CHECK(obx_device_init(&D, &bad, ledger_key.key.pk, dev1.key.pk) == OBX_OK, "64 entries");
    bad.limit = 0;
    resign_grant(&bad, &ledger_key);
    CHECK(obx_device_init(&D, &bad, ledger_key.key.pk, dev1.key.pk) == OBX_ERR_ARG, "zero limit");
    CHECK(obx_device_init(NULL, &G, ledger_key.key.pk, dev1.key.pk) == OBX_ERR_ARG &&
              obx_device_init(&D, NULL, ledger_key.key.pk, dev1.key.pk) == OBX_ERR_ARG &&
              obx_device_init(&D, &G, NULL, dev1.key.pk) == OBX_ERR_ARG &&
              obx_device_init(&D, &G, ledger_key.key.pk, NULL) == OBX_ERR_ARG,
          "device_init NULLs");

    /* E4, rule by rule, every entry validly signed by the device */
    memset(&T, 0, sizeof T);
    memcpy(T.grant_id, G.id, 32);
    memcpy(T.prev, G.id, 32);
    T.seq = 1;
    T.amount = T.cumulative = 1;
    CHECK(offline() == (obx_result_t) OBX_OK, "first entry, amount 1");
    T.cumulative = 2;
    CHECK(offline() == (obx_result_t) OBX_ERR_LIMIT, "first entry total must equal its amount");
    T.cumulative = 1;
    T.prev[0] ^= 1;
    CHECK(offline() == (obx_result_t) OBX_ERR_LIMIT, "first entry must link to the grant");
    T.seq = 8;
    T.cumulative = 100;
    CHECK(offline() == (obx_result_t) OBX_OK, "last number, total at the limit");
    T.cumulative = 101;
    CHECK(offline() == (obx_result_t) OBX_ERR_LIMIT, "total over the limit");
    T.cumulative = 100;
    T.seq = 9;
    CHECK(offline() == (obx_result_t) OBX_ERR_LIMIT, "number over the cap");
    T.seq = 0;
    CHECK(offline() == (obx_result_t) OBX_ERR_LIMIT, "number 0");
    T.seq = 2;
    T.amount = 0;
    CHECK(offline() == (obx_result_t) OBX_ERR_LIMIT, "zero amount");
    T.amount = 101;
    CHECK(offline() == (obx_result_t) OBX_ERR_LIMIT, "amount over its own total");
    T.amount = 100;
    CHECK(offline() == (obx_result_t) OBX_OK, "amount equal to its total");
    memcpy(T.grant_id, g1.id, 32);
    CHECK(offline() == (obx_result_t) OBX_ERR_STATE, "entry for another grant");
    memcpy(T.grant_id, G.id, 32);
    resign(&T, &dev2);
    CHECK(obx_verify_offline(ledger_key.key.pk, &G, dev2.key.pk, &T) == OBX_ERR_SIG,
          "another device's valid signature is refused (grant names dev1)");
    CHECK(obx_verify_offline(NULL, &G, dev1.key.pk, &T) == OBX_ERR_ARG &&
              obx_verify_offline(ledger_key.key.pk, NULL, dev1.key.pk, &T) == OBX_ERR_ARG &&
              obx_verify_offline(ledger_key.key.pk, &G, NULL, &T) == OBX_ERR_ARG &&
              obx_verify_offline(ledger_key.key.pk, &G, dev1.key.pk, NULL) == OBX_ERR_ARG,
          "verify_offline NULLs");
}

static void test_device_edges(void)
{
    setup();
    CHECK(obx_ledger_grant(&LG, HOLDER, dev1.key.pk, 10, 3, 10, 1000, &G) == OBX_OK, "grant");
    CHECK(obx_device_init(&D, &G, ledger_key.key.pk, dev1.key.pk) == OBX_OK, "init");
    dev1.fail = 1;
    CHECK(obx_device_spend(&D, PAYEE_A, 5, 1, sign_cb, &dev1, persist_cb, 0, &X) ==
                  OBX_ERR_SIGNER &&
              all_zero(&D.e[0], sizeof D.e[0]),
          "a failed signature leaves no half-built entry");
    dev1.fail = 0;
    CHECK(obx_device_spend(&D, PAYEE_A, 5, 1, sign_cb, &dev1, persist_cb, 0, &E[0]) == OBX_OK &&
              obx_device_spend(&D, PAYEE_B, 4, 2, sign_cb, &dev1, persist_cb, 0, &E[1]) == OBX_OK &&
              obx_device_spend(&D, PAYEE_A, 1, 3, sign_cb, &dev1, persist_cb, 0, &E[2]) == OBX_OK,
          "three entries, the last one reaching the limit exactly");
    CHECK(D.cumulative == 10 && E[2].cumulative == 10, "total at the limit");
    CHECK(obx_device_spend(&D, PAYEE_A, 1, 4, sign_cb, &dev1, persist_cb, 0, &X) == OBX_ERR_LIMIT,
          "a fourth entry is over the cap");
    CHECK(obx_device_spend(NULL, PAYEE_A, 1, 4, sign_cb, &dev1, persist_cb, 0, &X) == OBX_ERR_ARG &&
              obx_device_spend(&D, PAYEE_A, 1, 4, NULL, &dev1, persist_cb, 0, &X) == OBX_ERR_ARG &&
              obx_device_spend(&D, PAYEE_A, 1, 4, sign_cb, &dev1, NULL, 0, &X) == OBX_ERR_ARG &&
              obx_device_spend(&D, PAYEE_A, 1, 4, sign_cb, &dev1, persist_cb, 0, NULL) ==
                  OBX_ERR_ARG,
          "spend NULLs");

    /* acks */
    CHECK(obx_device_next_pending(NULL, 0) == -1, "next_pending(NULL)");
    CHECK(obx_device_next_pending(&D, 1) == 1 && obx_device_next_pending(&D, 3) == -1,
          "next_pending starts where asked");
    CHECK(obx_device_ack(&D, 1, OBX_R_ACCEPTED, persist_cb, 0) == OBX_OK &&
              D.state[0] == OBX_E_SETTLED && D.result[0] == OBX_R_ACCEPTED,
          "ACCEPTED settles");
    CHECK(obx_device_ack(&D, 2, OBX_R_NO_PAYEE, persist_cb, 0) == OBX_OK &&
              D.state[1] == OBX_E_PENDING && D.result[1] == OBX_R_NO_PAYEE,
          "NO_PAYEE stays pending");
    CHECK(obx_device_ack(&D, 2, OBX_R_LEDGER, persist_cb, 0) == OBX_OK &&
              D.state[1] == OBX_E_PENDING,
          "LEDGER stays pending");
    CHECK(obx_device_ack(&D, 2, OBX_R_DUPLICATE, persist_cb, 0) == OBX_OK &&
              D.state[1] == OBX_E_SETTLED,
          "DUPLICATE settles");
    persist_fail = 1;
    CHECK(obx_device_ack(&D, 3, OBX_R_CONFLICT, persist_cb, 0) == OBX_ERR_PERSIST &&
              D.state[2] == OBX_E_PENDING && D.result[2] == 0,
          "a failed write leaves the ack unrecorded");
    persist_fail = 0;
    CHECK(obx_device_ack(&D, 3, OBX_R_CONFLICT, persist_cb, 0) == OBX_OK &&
              D.state[2] == OBX_E_REJECTED,
          "CONFLICT rejects");
    CHECK(obx_device_ack(NULL, 1, OBX_R_ACCEPTED, persist_cb, 0) == OBX_ERR_ARG &&
              obx_device_ack(&D, 1, OBX_R_ACCEPTED, NULL, 0) == OBX_ERR_ARG,
          "ack NULLs");

    /* close */
    obx_close_t c, c2;
    dev1.fail = 1;
    CHECK(obx_device_close(&D, sign_cb, &dev1, persist_cb, 0, &c) == OBX_ERR_SIGNER && !D.closed &&
              all_zero(&D.close, sizeof D.close),
          "close: signer failure leaves nothing");
    dev1.fail = 0;
    persist_fail = 1;
    CHECK(obx_device_close(&D, sign_cb, &dev1, persist_cb, 0, &c) == OBX_ERR_PERSIST && !D.closed &&
              all_zero(&D.close, sizeof D.close),
          "close: write failure leaves nothing");
    persist_fail = 0;
    CHECK(obx_device_close(&D, sign_cb, &dev1, persist_cb, 0, &c) == OBX_OK && D.closed, "close");
    dev1.fail = 1;
    CHECK(obx_device_close(&D, sign_cb, &dev1, persist_cb, 0, &c2) == OBX_OK &&
              !memcmp(&c, &c2, sizeof c),
          "closing again returns the same statement without signing");
    dev1.fail = 0;
    CHECK(obx_device_close(NULL, sign_cb, &dev1, persist_cb, 0, &c2) == OBX_ERR_ARG &&
              obx_device_close(&D, NULL, &dev1, persist_cb, 0, &c2) == OBX_ERR_ARG &&
              obx_device_close(&D, sign_cb, &dev1, NULL, 0, &c2) == OBX_ERR_ARG &&
              obx_device_close(&D, sign_cb, &dev1, persist_cb, 0, NULL) == OBX_ERR_ARG,
          "close NULLs");
}

static void test_ledger_edges(void)
{
    setup();
    obx_grant_t g;
    CHECK(obx_ledger_init(NULL, &L, usd, ESCROW, LG.seed, ledger_key.key.pk, sign_cb, 0, 1) ==
                  OBX_ERR_ARG &&
              obx_ledger_init(&LG, NULL, usd, ESCROW, LG.seed, ledger_key.key.pk, sign_cb, 0, 1) ==
                  OBX_ERR_ARG,
          "ledger_init NULLs");
    setup();
    CHECK(obx_ledger_grant(NULL, HOLDER, dev1.key.pk, 1, 1, 1, 2, &g) == OBX_ERR_ARG &&
              obx_ledger_grant(&LG, HOLDER, NULL, 1, 1, 1, 2, &g) == OBX_ERR_ARG &&
              obx_ledger_grant(&LG, HOLDER, dev1.key.pk, 1, 1, 1, 2, NULL) == OBX_ERR_ARG,
          "grant NULLs");
    CHECK(obx_ledger_grant(&LG, HOLDER, dev1.key.pk, 50, OBX_MAX_ENTRIES, 10, 300, &G) == OBX_OK,
          "a 64-entry grant");
    ledger_key.fail = 1;
    CHECK(obx_ledger_grant(&LG, HOLDER, dev2.key.pk, 50, 4, 10, 300, &g) == OBX_ERR_SIGNER &&
              audit() && bal(LG.escrow_acct) == 50,
          "ledger signer failure: nothing moves");
    ledger_key.fail = 0;
    CHECK(obx_device_init(&D, &G, ledger_key.key.pk, dev1.key.pk) == OBX_OK, "init");
    for (int i = 0; i < 3; i++)
        CHECK(obx_device_spend(&D, PAYEE_A + (i & 1), (uint64_t) i + 1, i, sign_cb, &dev1,
                               persist_cb, 0, &E[i]) == OBX_OK,
              "spend");
    /* the later neighbour is checked too */
    CHECK(obx_ledger_submit(&LG, &E[2], 20, &R) == OBX_OK && R == OBX_R_ACCEPTED, "3 first");
    X = E[1];
    X.tick = 999;
    resign(&X, &dev1);
    CHECK(obx_ledger_submit(&LG, &X, 21, &R) == OBX_OK && R == OBX_R_CONFLICT,
          "D3: a number 2 that entry 3 does not link to");
    X = E[1];
    X.amount = 1;
    X.cumulative = 2;
    resign(&X, &dev1);
    CHECK(obx_ledger_submit(&LG, &X, 21, &R) == OBX_OK && R == OBX_R_CONFLICT,
          "D3: a number 2 whose total does not lead to entry 3's");
    CHECK(obx_ledger_submit(&LG, &E[0], 22, &R) == OBX_OK && R == OBX_R_ACCEPTED &&
              obx_ledger_submit(&LG, &E[1], 22, &R) == OBX_OK && R == OBX_R_ACCEPTED && audit(),
          "the real 1 and 2 (amount 1 is a valid amount)");

    /* expiry boundary: expires 300, grace 100 */
    obx_entry_t late;
    CHECK(obx_device_spend(&D, PAYEE_A, 4, 30, sign_cb, &dev1, persist_cb, 0, &late) == OBX_OK &&
              obx_device_spend(&D, PAYEE_A, 5, 31, sign_cb, &dev1, persist_cb, 0, &X) == OBX_OK,
          "two more");
    CHECK(obx_ledger_submit(&LG, &late, 399, &R) == OBX_OK && R == OBX_R_ACCEPTED,
          "accepted one tick before expiry + grace");
    CHECK(obx_ledger_submit(&LG, &X, 400, &R) == OBX_OK && R == OBX_R_EXPIRED,
          "EXPIRED at expiry + grace exactly");

    /* a fully spent, never closed grant audits clean */
    obx_grant_t g2;
    obx_device_t d2;
    CHECK(obx_ledger_grant(&LG, HOLDER, dev2.key.pk, 7, 2, 10, 300, &g2) == OBX_OK &&
              obx_device_init(&d2, &g2, ledger_key.key.pk, dev2.key.pk) == OBX_OK &&
              obx_device_spend(&d2, PAYEE_B, 7, 1, sign_cb, &dev2, persist_cb, 0, &X) == OBX_OK &&
              obx_ledger_submit(&LG, &X, 50, &R) == OBX_OK && R == OBX_R_ACCEPTED,
          "grant spent to the limit");
    CHECK(obx_ledger_check(&LG) && audit(), "C3 holds with a live grant at settled == covered");
    CHECK(!obx_ledger_check(NULL), "check(NULL)");
    uint32_t keep = LG.escrow_acct;
    LG.escrow_acct = PAY_MAX_ACCOUNTS + 3;
    CHECK(!obx_ledger_check(&LG), "check fails without an escrow account");
    LG.escrow_acct = keep;
    CHECK(obx_ledger_expire(NULL, 1) == 0, "expire(NULL)");
    CHECK(obx_ledger_expire(&LG, 100000) == 2 && obx_ledger_expire(&LG, 100001) == 0 && audit(),
          "expire counts only grants it finishes");
    CHECK(!obx_ledger_flagged(NULL, G.device_id) && !obx_ledger_flagged(&LG, NULL),
          "flagged NULLs");
}

static void test_close_edges(void)
{
    setup();
    obx_close_t c;
    CHECK(obx_ledger_close(NULL, &c, 1) == OBX_ERR_ARG &&
              obx_ledger_close(&LG, NULL, 1) == OBX_ERR_ARG,
          "close NULLs");
    CHECK(obx_ledger_grant(&LG, HOLDER, dev3.key.pk, 60, 4, 10, 1000, &G) == OBX_OK, "grant");
    CHECK(obx_device_init(&D, &G, ledger_key.key.pk, dev3.key.pk) == OBX_OK, "init");
    CHECK(obx_device_spend(&D, PAYEE_A, 10, 1, sign_cb, &dev3, persist_cb, 0, &E[0]) == OBX_OK &&
              obx_device_spend(&D, PAYEE_B, 20, 2, sign_cb, &dev3, persist_cb, 0, &E[1]) == OBX_OK,
          "two entries");
    memset(&c, 0, sizeof c);
    memcpy(c.grant_id, G.id, 32);
    c.grant_id[0] ^= 1;
    resign_close(&c, &dev3);
    CHECK(obx_ledger_close(&LG, &c, 20) == OBX_ERR_STATE, "close for an unknown grant");
    memcpy(c.grant_id, G.id, 32);
    c.last_seq = 5;
    c.cumulative = 30;
    resign_close(&c, &dev3);
    CHECK(obx_ledger_close(&LG, &c, 20) == OBX_ERR_LIMIT, "close past the entry cap");
    c.last_seq = 2;
    c.cumulative = 61;
    resign_close(&c, &dev3);
    CHECK(obx_ledger_close(&LG, &c, 20) == OBX_ERR_LIMIT, "close past the limit");
    c.last_seq = 0;
    c.cumulative = 5;
    resign_close(&c, &dev3);
    CHECK(obx_ledger_close(&LG, &c, 20) == OBX_ERR_LIMIT, "nothing spent but a total");
    c.last_seq = 2;
    c.cumulative = 0;
    resign_close(&c, &dev3);
    CHECK(obx_ledger_close(&LG, &c, 20) == OBX_ERR_LIMIT, "entries but no total");
    CHECK(obx_ledger_submit(&LG, &E[1], 21, &R) == OBX_OK && R == OBX_R_ACCEPTED, "entry 2 paid");
    c.last_seq = 1;
    c.cumulative = 15;
    resign_close(&c, &dev3);
    CHECK(obx_ledger_close(&LG, &c, 22) == OBX_ERR_LIMIT, "a total below what was paid");
    c.cumulative = 30;
    resign_close(&c, &dev3);
    CHECK(obx_ledger_close(&LG, &c, 22) == OBX_ERR_STATE, "a close before an entry already paid");
    c.last_seq = 2;
    c.cumulative = 31;
    obx_entry_hash(&E[1], c.last_hash);
    resign_close(&c, &dev3);
    CHECK(obx_ledger_close(&LG, &c, 22) == OBX_ERR_STATE, "total disagrees with the paid entry");
    c.cumulative = 30;
    c.last_hash[0] ^= 1;
    resign_close(&c, &dev3);
    CHECK(obx_ledger_close(&LG, &c, 22) == OBX_ERR_STATE, "hash disagrees with the paid entry");
    CHECK(obx_ledger_flagged(&LG, G.device_id), "each lie flags the device");

    /* a forged number 2 after the real close names the real hash */
    obx_close_t real;
    CHECK(obx_device_close(&D, sign_cb, &dev3, persist_cb, 0, &real) == OBX_OK &&
              obx_ledger_close(&LG, &real, 23) == OBX_OK,
          "the real close");
    X = E[1];
    CHECK(obx_ledger_submit(&LG, &X, 24, &R) == OBX_OK && R == OBX_R_DUPLICATE, "2 duplicate");
    CHECK(obx_ledger_submit(&LG, &E[0], 24, &R) == OBX_OK && R == OBX_R_ACCEPTED && audit(),
          "entry 1 settles after the close");
    CHECK(obx_ledger_close(&LG, &real, 25) == OBX_ERR_STATE, "finished grant: close refused");

    /* close naming the last number, before it arrives: a forgery is refused */
    obx_grant_t g;
    obx_device_t d;
    CHECK(obx_ledger_grant(&LG, HOLDER, dev4.key.pk, 40, 4, 30, 1000, &g) == OBX_OK &&
              obx_device_init(&d, &g, ledger_key.key.pk, dev4.key.pk) == OBX_OK &&
              obx_device_spend(&d, PAYEE_A, 15, 1, sign_cb, &dev4, persist_cb, 0, &E[3]) ==
                  OBX_OK &&
              obx_device_spend(&d, PAYEE_B, 5, 2, sign_cb, &dev4, persist_cb, 0, &E[4]) == OBX_OK &&
              obx_device_close(&d, sign_cb, &dev4, persist_cb, 0, &real) == OBX_OK &&
              obx_ledger_close(&LG, &real, 31) == OBX_OK,
          "grant 4 closed at 2 entries, 20");
    X = E[4];
    X.payee = PAYEE_A;
    resign(&X, &dev4);
    CHECK(obx_ledger_submit(&LG, &X, 32, &R) == OBX_OK && R == OBX_R_CONFLICT,
          "a different number 2 than the close named");
    CHECK(obx_ledger_submit(&LG, &E[3], 33, &R) == OBX_OK && R == OBX_R_ACCEPTED &&
              obx_ledger_submit(&LG, &E[4], 33, &R) == OBX_OK && R == OBX_R_ACCEPTED && audit(),
          "the real entries settle");

    /* a close at the entry cap and exactly the limit is honest */
    obx_grant_t g6;
    obx_device_t d6;
    CHECK(
        obx_ledger_grant(&LG, HOLDER, dev1.key.pk, 9, 2, 50, 1000, &g6) == OBX_OK &&
            obx_device_init(&d6, &g6, ledger_key.key.pk, dev1.key.pk) == OBX_OK &&
            obx_device_spend(&d6, PAYEE_A, 4, 1, sign_cb, &dev1, persist_cb, 0, &E[5]) == OBX_OK &&
            obx_device_spend(&d6, PAYEE_A, 5, 2, sign_cb, &dev1, persist_cb, 0, &E[6]) == OBX_OK &&
            obx_device_close(&d6, sign_cb, &dev1, persist_cb, 0, &real) == OBX_OK &&
            real.last_seq == 2 && real.cumulative == 9 &&
            obx_ledger_close(&LG, &real, 51) == OBX_OK && audit(),
        "close at the cap and the limit");

    /* nothing spent: the whole limit comes back and the grant is finished */
    obx_grant_t g5;
    obx_device_t d5;
    uint64_t h0 = bal(holder_acct);
    CHECK(obx_ledger_grant(&LG, HOLDER, dev2.key.pk, 33, 4, 40, 1000, &g5) == OBX_OK &&
              obx_device_init(&d5, &g5, ledger_key.key.pk, dev2.key.pk) == OBX_OK &&
              obx_device_close(&d5, sign_cb, &dev2, persist_cb, 0, &real) == OBX_OK &&
              real.last_seq == 0 && real.cumulative == 0 &&
              obx_ledger_close(&LG, &real, 41) == OBX_OK,
          "empty close");
    CHECK(bal(holder_acct) == h0 && audit(), "all 33 returned");
    int fin = 0;
    for (unsigned i = 0; i < OBX_MAX_GRANTS; i++)
        if (LG.slot[i].used && !memcmp(LG.slot[i].grant.id, g5.id, 32)) fin = LG.slot[i].finished;
    CHECK(fin, "and the grant is finished at once");
}

/* the flagged list: no duplicates, and the newest kept when it is full */
static void test_flag_list(void)
{
    setup();
    static signer_t d[OBX_MAX_FLAGGED + 1];
    char lab[16];
    for (unsigned i = 0; i <= OBX_MAX_FLAGGED; i++) {
        snprintf(lab, sizeof lab, "fl-%u", i);
        signer_make(&d[i], (uint8_t) (0x90 + i), lab);
        obx_grant_t g;
        obx_close_t c;
        uint64_t now = 1000 * (i + 1);
        if (obx_ledger_grant(&LG, HOLDER, d[i].key.pk, 1, 1, now, now + 1, &g) != OBX_OK) {
            CHECK(0, "grant");
            continue;
        }
        memset(&c, 0, sizeof c);
        memcpy(c.grant_id, g.id, 32);
        c.last_seq = 1;
        c.cumulative = 2;
        resign_close(&c, &d[i]);
        CHECK(obx_ledger_close(&LG, &c, now) == OBX_ERR_LIMIT, "lying close");
        if (i == 0) {
            CHECK(obx_ledger_close(&LG, &c, now) == OBX_ERR_LIMIT && LG.n_flagged == 1 &&
                      LG.conflicts == 2,
                  "flagged once, counted twice");
        }
        obx_ledger_expire(&LG, now + 500);
    }
    uint8_t id[32];
    obx_key_id(d[0].key.pk, id);
    CHECK(!obx_ledger_flagged(&LG, id), "the oldest drops out when the list is full");
    obx_key_id(d[1].key.pk, id);
    CHECK(obx_ledger_flagged(&LG, id), "the second oldest stays");
    obx_key_id(d[OBX_MAX_FLAGGED].key.pk, id);
    CHECK(obx_ledger_flagged(&LG, id) && LG.n_flagged == OBX_MAX_FLAGGED, "the newest is kept");
    CHECK(audit(), "audit");
}

int main(void)
{
    signer_make(&ledger_key, 0x01, "ledger");
    signer_make(&dev1, 0x02, "till-1");
    signer_make(&dev2, 0x03, "till-2");
    signer_make(&dev3, 0x04, "till-3");
    signer_make(&dev4, 0x05, "till-4");
    test_grant();
    test_spend_and_settle();
    test_rollback_conflict();
    test_bad_entries();
    test_close_and_expiry();
    test_slots();
    test_random();
    test_ids_and_offline_rules();
    test_device_edges();
    test_ledger_edges();
    test_close_edges();
    test_flag_list();
    printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
