/* test_immigration.c — Immigration Enforcement tests
 *
 * Tests visa application, signature verification, undocumented daemon
 * rejection, violation tracking, deportation, transit visa expiry,
 * and M5 coverage.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "immigration.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../robin_debanks/crypto_verify.h"

/* Compute a valid HMAC-SHA256 immigration signature for test daemons */

/* TEST VERIFIER — see the note in test_count_house.c.
 * immig_verify_sig performs real Ed25519 verification against the embedded
 * IMMIGRATION public key; its private key is held offline and is deliberately
 * not compiled in, so a host test cannot mint a valid signature. The API
 * exposes an injectable verify_sig hook for exactly this; we install one that
 * checks the same HMAC construction the test signs with, exercising the
 * visa/coverage logic without weakening the production Ed25519 path.
 * (This test signed HMAC and called the default verifier; when the
 * implementation was upgraded HMAC -> Ed25519 the test was left behind and
 * failed silently, because it was never wired into verify-all.) */
static bool test_verify_hmac(const immig_daemon_t *d)
{
    if (!d) return false;
    uint8_t msg[21 + 32 + 21];
    uint32_t pos = 0;
    for (uint32_t i = 0; i < 21; i++) msg[pos++] = d->content_hash[i];
    for (uint32_t i = 0; i < 32; i++) msg[pos++] = d->pubkey[i];
    for (uint32_t i = 0; i < 21; i++) msg[pos++] = d->daemon_id.bytes[i];
    uint8_t expect[32];
    crypto_hmac_sha256(msg, pos, CRYPTO_AUTHORITY_KEY_IMMIGRATION, expect);
    for (uint32_t i = 0; i < 32; i++)
        if (expect[i] != d->signature[i]) return false;
    return true;
}

static void compute_immig_sig(const uint8_t *content_hash, const uint8_t *pubkey,
                              const uint8_t *daemon_id_bytes, uint8_t sig[64])
{
    uint8_t msg[21 + 32 + 21]; /* content_hash + pubkey + daemon_id */
    uint32_t pos = 0;
    for (uint32_t i = 0; i < 21; i++) msg[pos++] = content_hash[i];
    for (uint32_t i = 0; i < 32; i++) msg[pos++] = pubkey[i];
    for (uint32_t i = 0; i < 21; i++) msg[pos++] = daemon_id_bytes[i];
    uint8_t hmac_out[32];
    crypto_hmac_sha256(msg, pos, CRYPTO_AUTHORITY_KEY_IMMIGRATION, hmac_out);
    for (uint32_t i = 0; i < 32; i++) sig[i] = hmac_out[i];
    for (uint32_t i = 32; i < 64; i++) sig[i] = 0;
}
static int feq(double a, double b, double eps)
{
    double diff = fabs(a - b);
    double scale = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    if (scale < 1.0) scale = 1.0;
    return diff <= eps * scale;
}

static word168_t make_id(uint8_t seed)
{
    word168_t w;
    for (int i = 0; i < WORD168_OCTETS; i++) w.bytes[i] = (uint8_t) (seed + i);
    return w;
}

static uint8_t zero_sig[IMMIG_SIG_LEN];
static uint8_t pubkey[IMMIG_PUBKEY_LEN];
static uint8_t hash[IMMIG_CONTENT_HASH_LEN];

int main(void)
{
    memset(zero_sig, 0, IMMIG_SIG_LEN);
    memset(pubkey, 0x42, IMMIG_PUBKEY_LEN);
    memset(hash, 0x77, IMMIG_CONTENT_HASH_LEN);

    /* ===== init ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        immigration_t im;
        immig_init(&im, 1, &ph);
        im.verify_sig = test_verify_hmac;
        assert(im.device_id == 1);
        assert(im.num_daemons == 0);
        assert(im.porter == &ph);
    }

    /* ===== resident visa granted ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        immigration_t im;
        immig_init(&im, 1, &ph);
        im.verify_sig = test_verify_hmac;

        word168_t id = make_id(1);
        uint8_t sig1[IMMIG_SIG_LEN];
        compute_immig_sig(hash, pubkey, id.bytes, sig1);
        int32_t did = immig_apply_visa(&im, "kernel_logger", IMMIG_VISA_RESIDENT, &id, pubkey, hash,
                                       sig1, 0, 0);
        assert(did > 0);
        assert(im.num_daemons == 1);
        assert(im.total_granted == 1);
        assert(im.resident_count == 1);

        immig_daemon_t *d = immig_get_daemon(&im, (uint32_t) did);
        assert(d->state == IMMIG_DAEMON_GRANTED);
        assert(d->sig_verified == true); /* HMAC verified */
        assert(d->visa == IMMIG_VISA_RESIDENT);
    }

    /* ===== undocumented daemon rejected ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        immigration_t im;
        immig_init(&im, 1, &ph);
        im.verify_sig = test_verify_hmac;

        word168_t id = make_id(2);
        int32_t did =
            immig_apply_visa(&im, "malware", IMMIG_VISA_WORKER, &id, pubkey, hash, zero_sig, 0, 0);
        assert(did == -2);
        assert(im.total_rejected == 1);

        immig_daemon_t *d = immig_get_daemon(&im, 1);
        assert(d->state == IMMIG_DAEMON_REJECTED);
        assert(d->sig_verified == false);
    }

    /* ===== worker visa granted ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        immigration_t im;
        immig_init(&im, 1, &ph);
        im.verify_sig = test_verify_hmac;

        word168_t id = make_id(3);
        uint8_t sig3[IMMIG_SIG_LEN];
        compute_immig_sig(hash, pubkey, id.bytes, sig3);
        int32_t did = immig_apply_visa(&im, "mesh_app", IMMIG_VISA_WORKER, &id, pubkey, hash, sig3,
                                       0x01, 100);
        assert(did > 0);
        assert(im.worker_count == 1);

        immig_daemon_t *d = immig_get_daemon(&im, (uint32_t) did);
        assert(d->state == IMMIG_DAEMON_GRANTED);
        assert(d->network_ports == 0x01);
    }

    /* ===== transit visa granted ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        immigration_t im;
        immig_init(&im, 1, &ph);
        im.verify_sig = test_verify_hmac;

        word168_t id = make_id(4);
        uint8_t sig4[IMMIG_SIG_LEN];
        compute_immig_sig(hash, pubkey, id.bytes, sig4);
        int32_t did = immig_apply_visa(&im, "temp_script", IMMIG_VISA_TRANSIT, &id, pubkey, hash,
                                       sig4, 0, 200);
        assert(did > 0);
        assert(im.transit_count == 1);
    }

    /* ===== violations → deportation ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        immigration_t im;
        immig_init(&im, 1, &ph);
        im.verify_sig = test_verify_hmac;

        word168_t id = make_id(5);
        uint8_t sig5[IMMIG_SIG_LEN];
        compute_immig_sig(hash, pubkey, id.bytes, sig5);
        int32_t did =
            immig_apply_visa(&im, "bad_actor", IMMIG_VISA_WORKER, &id, pubkey, hash, sig5, 0, 0);
        assert(did > 0);

        assert(immig_record_violation(&im, (uint32_t) did) == 1);
        assert(immig_record_violation(&im, (uint32_t) did) == 2);
        assert(immig_record_violation(&im, (uint32_t) did) == 3);

        immig_daemon_t *d = immig_get_daemon(&im, (uint32_t) did);
        assert(d->state == IMMIG_DAEMON_DEPORTED);
        assert(d->violations == IMMIG_MAX_VIOLATIONS);
        assert(im.total_deported == 1);
    }

    /* ===== manual deportation ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        immigration_t im;
        immig_init(&im, 1, &ph);
        im.verify_sig = test_verify_hmac;

        word168_t id = make_id(6);
        uint8_t sig6[IMMIG_SIG_LEN];
        compute_immig_sig(hash, pubkey, id.bytes, sig6);
        int32_t did =
            immig_apply_visa(&im, "rogue", IMMIG_VISA_RESIDENT, &id, pubkey, hash, sig6, 0, 0);
        assert(did > 0);
        assert(im.resident_count == 1);

        assert(immig_deport(&im, (uint32_t) did) == 0);
        immig_daemon_t *d = immig_get_daemon(&im, (uint32_t) did);
        assert(d->state == IMMIG_DAEMON_DEPORTED);
        assert(im.resident_count == 0);
    }

    /* ===== transit visa expiry ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        immigration_t im;
        immig_init(&im, 1, &ph);
        im.verify_sig = test_verify_hmac;

        word168_t id = make_id(7);
        uint8_t sig7[IMMIG_SIG_LEN];
        compute_immig_sig(hash, pubkey, id.bytes, sig7);
        int32_t did =
            immig_apply_visa(&im, "temp", IMMIG_VISA_TRANSIT, &id, pubkey, hash, sig7, 0, 0);
        assert(did > 0);
        assert(im.transit_count == 1);

        /* Touch to keep alive */
        assert(immig_touch(&im, (uint32_t) did, 500) == 0);

        /* Not yet expired (touched at 500, now 600, timeout 1000) */
        assert(immig_check_expired(&im, 600, 1000) == 0);

        /* Expired (last active 500, now 1600, timeout 1000) */
        assert(immig_check_expired(&im, 1600, 1000) == 1);

        immig_daemon_t *d = immig_get_daemon(&im, (uint32_t) did);
        assert(d->state == IMMIG_DAEMON_EXPIRED);
        assert(im.transit_count == 0);
        assert(im.total_expired == 1);
    }

    /* ===== capacity limit ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        immigration_t im;
        immig_init(&im, 1, &ph);
        im.verify_sig = test_verify_hmac;

        for (uint32_t i = 0; i < IMMIG_MAX_DAEMONS; i++) {
            word168_t id = make_id((uint8_t) (i + 100));
            char name[32];
            uint32_t n = i;
            int pos = 0;
            if (n == 0) {
                name[pos++] = '0';
            } else {
                char tmp[16];
                int tp = 0;
                while (n > 0) {
                    tmp[tp++] = '0' + (n % 10);
                    n /= 10;
                }
                while (tp > 0) name[pos++] = tmp[--tp];
            }
            name[pos] = '\0';
            uint8_t sigc[IMMIG_SIG_LEN];
            compute_immig_sig(hash, pubkey, id.bytes, sigc);
            int32_t did =
                immig_apply_visa(&im, name, IMMIG_VISA_WORKER, &id, pubkey, hash, sigc, 0, 0);
            assert(did > 0);
        }

        word168_t id = make_id(255);
        uint8_t sig255[IMMIG_SIG_LEN];
        compute_immig_sig(hash, pubkey, id.bytes, sig255);
        int32_t did =
            immig_apply_visa(&im, "overflow", IMMIG_VISA_WORKER, &id, pubkey, hash, sig255, 0, 0);
        assert(did == -1);
    }

    /* ===== coverage computation ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        immigration_t im;
        immig_init(&im, 1, &ph);
        im.verify_sig = test_verify_hmac;

        /* Grant 2, reject 1 */
        word168_t id1 = make_id(10);
        word168_t id2 = make_id(11);
        word168_t id3 = make_id(12);
        uint8_t sig10[IMMIG_SIG_LEN];
        compute_immig_sig(hash, pubkey, id1.bytes, sig10);
        uint8_t sig11[IMMIG_SIG_LEN];
        compute_immig_sig(hash, pubkey, id2.bytes, sig11);
        immig_apply_visa(&im, "d1", IMMIG_VISA_WORKER, &id1, pubkey, hash, sig10, 0, 0);
        immig_apply_visa(&im, "d2", IMMIG_VISA_WORKER, &id2, pubkey, hash, sig11, 0, 0);
        immig_apply_visa(&im, "bad", IMMIG_VISA_WORKER, &id3, pubkey, hash, zero_sig, 0, 0);

        immig_update_coverage(&im);
        /* r = 2 granted / (2 granted + 1 rejected) = 2/3 */
        assert(feq(im.m5.r, 2.0 / 3.0, 1e-9));
        /* ell = 2 granted / 3 total = 2/3 */
        assert(feq(im.m5.ell, 2.0 / 3.0, 1e-9));
    }

    printf("All Immigration Enforcement tests passed\n");
    return 0;
}
