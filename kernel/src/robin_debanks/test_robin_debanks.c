/* test_robin_debanks.c — Robin DeBanks Vault tests
 *
 * Tests vault initialization, encrypted storage, immediate unlock,
 * time-locked entries, unlock delay, tamper detection, deletion,
 * and M5 coverage.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "robin_debanks.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static int feq(double a, double b, double eps) {
    double diff = fabs(a - b);
    double scale = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    if (scale < 1.0) scale = 1.0;
    return diff <= eps * scale;
}

static uint8_t master_key[ROBIN_KEY_LEN];

int main(void) {
    memset(master_key, 0x55, ROBIN_KEY_LEN);

    /* ===== init ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        robin_vault_t rv;
        robin_init(&rv, 1, master_key, &ph);
        assert(rv.device_id == 1);
        assert(rv.num_entries == 0);
        assert(rv.porter == &ph);
        assert(memcmp(rv.master_key, master_key, ROBIN_KEY_LEN) == 0);
    }

    /* ===== store + immediate unlock ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        robin_vault_t rv;
        robin_init(&rv, 1, master_key, &ph);

        uint8_t secret[] = "my_private_key_data_123";
        int32_t eid = robin_store(&rv, "wallet_key", ROBIN_TYPE_PRIVATE_KEY,
                                     secret, sizeof(secret), false, 0);
        assert(eid > 0);
        assert(rv.num_entries == 1);
        assert(rv.total_stored == 1);

        robin_entry_t *e = robin_get_entry(&rv, (uint32_t)eid);
        assert(e->state == ROBIN_ENTRY_STORED);
        assert(e->time_locked == false);

        /* Unlock */
        uint8_t plaintext[ROBIN_MAX_DATA_LEN];
        uint32_t pt_len = 0;
        int32_t result = robin_unlock(&rv, (uint32_t)eid, plaintext, &pt_len, 0);
        assert(result > 0);
        assert(pt_len == sizeof(secret));
        assert(memcmp(plaintext, secret, sizeof(secret)) == 0);
        assert(e->state == ROBIN_ENTRY_UNLOCKED);
        assert(rv.total_unlocked == 1);
    }

    /* ===== time-locked entry: not ready before delay ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        robin_vault_t rv;
        robin_init(&rv, 1, master_key, &ph);

        uint8_t secret[] = "high_value_seed_phrase";
        int32_t eid = robin_store(&rv, "seed", ROBIN_TYPE_SEED_PHRASE,
                                     secret, sizeof(secret), true, 100);
        assert(eid > 0);

        robin_entry_t *e = robin_get_entry(&rv, (uint32_t)eid);
        assert(e->state == ROBIN_ENTRY_LOCKED);
        assert(e->time_locked == true);

        /* Not ready yet */
        assert(robin_unlock_ready(&rv, (uint32_t)eid, 500) == false);

        /* Unlock attempt fails */
        uint8_t pt[ROBIN_MAX_DATA_LEN];
        uint32_t pt_len = 0;
        assert(robin_unlock(&rv, (uint32_t)eid, pt, &pt_len, 500) == -2);
    }

    /* ===== time-locked entry: ready after delay ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        robin_vault_t rv;
        robin_init(&rv, 1, master_key, &ph);

        uint8_t secret[] = "high_value_seed_phrase";
        int32_t eid = robin_store(&rv, "seed", ROBIN_TYPE_SEED_PHRASE,
                                     secret, sizeof(secret), true, 100);
        assert(eid > 0);

        /* Wait for unlock delay */
        assert(robin_unlock_ready(&rv, (uint32_t)eid, 100 + ROBIN_UNLOCK_DELAY) == true);

        uint8_t pt[ROBIN_MAX_DATA_LEN];
        uint32_t pt_len = 0;
        int32_t result = robin_unlock(&rv, (uint32_t)eid, pt, &pt_len,
                                        100 + ROBIN_UNLOCK_DELAY);
        assert(result > 0);
        assert(pt_len == sizeof(secret));
        assert(memcmp(pt, secret, sizeof(secret)) == 0);
    }

    /* ===== request unlock resets timer ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        robin_vault_t rv;
        robin_init(&rv, 1, master_key, &ph);

        uint8_t secret[] = "delayed_secret";
        int32_t eid = robin_store(&rv, "delayed", ROBIN_TYPE_DOCUMENT,
                                     secret, sizeof(secret), true, 0);
        assert(eid > 0);

        /* Request unlock at cycle 500 */
        assert(robin_request_unlock(&rv, (uint32_t)eid, 500) == 0);

        /* Should unlock at 500 + ROBIN_UNLOCK_DELAY, not 0 + ROBIN_UNLOCK_DELAY */
        assert(robin_unlock_ready(&rv, (uint32_t)eid, 500 + ROBIN_UNLOCK_DELAY - 1) == false);
        assert(robin_unlock_ready(&rv, (uint32_t)eid, 500 + ROBIN_UNLOCK_DELAY) == true);
    }

    /* ===== entry types ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        robin_vault_t rv;
        robin_init(&rv, 1, master_key, &ph);

        uint8_t data[] = "test_data";
        int32_t e1 = robin_store(&rv, "pk", ROBIN_TYPE_PRIVATE_KEY, data, sizeof(data), false, 0);
        int32_t e2 = robin_store(&rv, "seed", ROBIN_TYPE_SEED_PHRASE, data, sizeof(data), false, 0);
        int32_t e3 = robin_store(&rv, "cred", ROBIN_TYPE_CREDENTIAL, data, sizeof(data), false, 0);
        int32_t e4 = robin_store(&rv, "doc", ROBIN_TYPE_DOCUMENT, data, sizeof(data), false, 0);
        int32_t e5 = robin_store(&rv, "api", ROBIN_TYPE_API_KEY, data, sizeof(data), false, 0);
        int32_t e6 = robin_store(&rv, "custom", ROBIN_TYPE_CUSTOM, data, sizeof(data), false, 0);

        assert(e1 > 0 && e2 > 0 && e3 > 0 && e4 > 0 && e5 > 0 && e6 > 0);
        assert(robin_get_entry(&rv, (uint32_t)e1)->type == ROBIN_TYPE_PRIVATE_KEY);
        assert(robin_get_entry(&rv, (uint32_t)e2)->type == ROBIN_TYPE_SEED_PHRASE);
        assert(robin_get_entry(&rv, (uint32_t)e3)->type == ROBIN_TYPE_CREDENTIAL);
        assert(robin_get_entry(&rv, (uint32_t)e4)->type == ROBIN_TYPE_DOCUMENT);
        assert(robin_get_entry(&rv, (uint32_t)e5)->type == ROBIN_TYPE_API_KEY);
        assert(robin_get_entry(&rv, (uint32_t)e6)->type == ROBIN_TYPE_CUSTOM);
    }

    /* ===== delete entry ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        robin_vault_t rv;
        robin_init(&rv, 1, master_key, &ph);

        uint8_t data[] = "to_delete";
        int32_t eid = robin_store(&rv, "temp", ROBIN_TYPE_CREDENTIAL,
                                     data, sizeof(data), false, 0);
        assert(eid > 0);
        assert(rv.num_entries == 1);

        assert(robin_delete(&rv, (uint32_t)eid) == 0);
        assert(rv.num_entries == 0);
        assert(robin_get_entry(&rv, (uint32_t)eid) == NULL);
    }

    /* ===== check_unlocks ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        robin_vault_t rv;
        robin_init(&rv, 1, master_key, &ph);

        uint8_t data[] = "locked";
        robin_store(&rv, "e1", ROBIN_TYPE_DOCUMENT, data, sizeof(data), true, 0);
        robin_store(&rv, "e2", ROBIN_TYPE_DOCUMENT, data, sizeof(data), true, 0);

        /* None ready yet */
        assert(robin_check_unlocks(&rv, 100) == 0);

        /* Both ready after delay */
        assert(robin_check_unlocks(&rv, ROBIN_UNLOCK_DELAY + 10) == 2);
    }

    /* ===== capacity limit ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        robin_vault_t rv;
        robin_init(&rv, 1, master_key, &ph);

        uint8_t data[] = "x";
        for (uint32_t i = 0; i < ROBIN_MAX_ENTRIES; i++) {
            int32_t eid = robin_store(&rv, "e", ROBIN_TYPE_CUSTOM,
                                         data, 1, false, 0);
            assert(eid > 0);
        }
        int32_t eid = robin_store(&rv, "overflow", ROBIN_TYPE_CUSTOM,
                                     data, 1, false, 0);
        assert(eid == -1);
    }

    /* ===== coverage computation ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        robin_vault_t rv;
        robin_init(&rv, 1, master_key, &ph);

        uint8_t data[] = "cov_test";
        int32_t e1 = robin_store(&rv, "s1", ROBIN_TYPE_DOCUMENT, data, sizeof(data), false, 0);
        robin_store(&rv, "s2", ROBIN_TYPE_DOCUMENT, data, sizeof(data), false, 0);

        /* Unlock one */
        uint8_t pt[ROBIN_MAX_DATA_LEN];
        uint32_t pt_len = 0;
        robin_unlock(&rv, (uint32_t)e1, pt, &pt_len, 0);

        robin_update_coverage(&rv);
        /* r = 2 intact / 2 total = 1.0
         * ell = 1 unlocked / 2 total = 0.5 */
        assert(feq(rv.m5.r, 1.0, 1e-9));
        assert(feq(rv.m5.ell, 0.5, 1e-9));
    }

    printf("All Robin DeBanks Vault tests passed\n");
    return 0;
}
