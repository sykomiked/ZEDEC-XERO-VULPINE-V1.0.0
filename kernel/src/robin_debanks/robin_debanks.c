/* robin_debanks.c — Robin DeBanks Vault implementation
 *
 * See robin_debanks.h for design rationale. Uses real AES-256-GCM
 * (aes256_gcm.h) for authenticated encryption and SHA-256 (sha256.h,
 * truncated to 168 bits) for the content-identity hash, replacing the
 * prior XOR/rolling-hash placeholders. Both primitives are validated
 * against NIST/FIPS test vectors (see crypto_validate.c).
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "robin_debanks.h"
#include "aes256_gcm.h"
#include "sha256.h"
#include <string.h>

static void robin_hash168(const uint8_t *data, uint32_t len, uint8_t out[ROBIN_HASH_LEN]) {
    uint8_t digest[SHA256_DIGEST_LEN];
    sha256(data, len, digest);
    memcpy(out, digest, ROBIN_HASH_LEN); /* truncate 256-bit digest to 168 bits */
}

/* Derive the next unique GCM nonce for this vault: device_id (32 bits)
 * || monotonic counter (64 bits) = 96 bits, matching GCM_IV_LEN. A
 * fresh (key, nonce) pair per encryption is a hard GCM requirement --
 * the prior placeholder derived the nonce from plaintext content,
 * which repeats (and breaks GCM's security guarantees) whenever the
 * same plaintext is stored twice. */
static void robin_next_nonce(robin_vault_t *rv, uint8_t nonce[ROBIN_NONCE_LEN]) {
    uint32_t did = rv->device_id;
    uint64_t ctr = rv->nonce_counter++;
    nonce[0] = (uint8_t)(did >> 24); nonce[1] = (uint8_t)(did >> 16);
    nonce[2] = (uint8_t)(did >> 8);  nonce[3] = (uint8_t)(did);
    for (int i = 0; i < 8; i++) nonce[4 + i] = (uint8_t)(ctr >> (8 * (7 - i)));
}

void robin_init(robin_vault_t *rv, uint32_t device_id,
                 const uint8_t master_key[ROBIN_KEY_LEN],
                 porter_house_t *porter) {
    if (!rv) return;
    memset(rv, 0, sizeof(*rv));
    rv->device_id = device_id;
    rv->porter = porter;
    rv->num_entries = 0;
    rv->next_id = 1;
    if (master_key) memcpy(rv->master_key, master_key, ROBIN_KEY_LEN);
    rv->m5.omega = device_id;
    rv->m5.chi = device_id;
    rv->m5.phi = SR_ZERO;
    robin_update_coverage(rv);
}

int32_t robin_store(robin_vault_t *rv, const char *name,
                     robin_entry_type_t type,
                     const uint8_t *plaintext, uint32_t plaintext_len,
                     bool time_locked, uint64_t current_cycle) {
    if (!rv || !name || !plaintext) return -1;
    if (plaintext_len > ROBIN_MAX_DATA_LEN) return -1;

    uint32_t slot = ROBIN_MAX_ENTRIES;
    for (uint32_t i = 0; i < ROBIN_MAX_ENTRIES; i++) {
        if (!rv->entries[i].active) {
            slot = i;
            break;
        }
    }
    if (slot >= ROBIN_MAX_ENTRIES) return -1;

    robin_entry_t *e = &rv->entries[slot];
    memset(e, 0, sizeof(*e));
    e->id = rv->next_id++;
    e->active = true;
    e->type = type;
    e->time_locked = time_locked;

    uint32_t j;
    for (j = 0; j + 1 < ROBIN_MAX_NAME_LEN && name[j]; j++) e->name[j] = name[j];
    e->name[j] = '\0';

    /* Fresh nonce (never reused for this key) and authenticated encryption */
    robin_next_nonce(rv, e->nonce);
    aes256_gcm_encrypt(rv->master_key, e->nonce, NULL, 0,
                       plaintext, plaintext_len, e->ciphertext, e->tag);
    e->ciphertext_len = plaintext_len;

    /* Content-identity hash of plaintext (168-bit, CRIT-168 convention) */
    robin_hash168(plaintext, plaintext_len, e->integrity_hash);

    if (time_locked) {
        e->state = ROBIN_ENTRY_LOCKED;
        e->lock_requested_cycle = current_cycle;
        e->unlock_at_cycle = current_cycle + ROBIN_UNLOCK_DELAY;
    } else {
        e->state = ROBIN_ENTRY_STORED;
    }

    rv->num_entries++;
    rv->total_stored++;
    robin_update_coverage(rv);
    return (int32_t)e->id;
}

int32_t robin_request_unlock(robin_vault_t *rv, uint32_t entry_id,
                              uint64_t current_cycle) {
    if (!rv) return -1;
    robin_entry_t *e = robin_get_entry(rv, entry_id);
    if (!e) return -1;
    if (!e->time_locked) return -2;
    if (e->state != ROBIN_ENTRY_LOCKED) return -2;
    /* Request already made at store time; just update cycle if needed */
    if (current_cycle > e->lock_requested_cycle) {
        e->lock_requested_cycle = current_cycle;
        e->unlock_at_cycle = current_cycle + ROBIN_UNLOCK_DELAY;
    }
    return 0;
}

bool robin_unlock_ready(robin_vault_t *rv, uint32_t entry_id, uint64_t current_cycle) {
    if (!rv) return false;
    robin_entry_t *e = robin_get_entry(rv, entry_id);
    if (!e) return false;
    if (e->state == ROBIN_ENTRY_STORED) return true;
    /* Already past its delay once: a time-locked entry stays readable. */
    if (e->state == ROBIN_ENTRY_UNLOCKED) return true;
    if (e->state != ROBIN_ENTRY_LOCKED) return false;
    return current_cycle >= e->unlock_at_cycle;
}

int32_t robin_unlock(robin_vault_t *rv, uint32_t entry_id,
                      uint8_t *plaintext_out, uint32_t *plaintext_len_out,
                      uint64_t current_cycle) {
    if (!rv) return -1;
    robin_entry_t *e = robin_get_entry(rv, entry_id);
    if (!e) return -1;

    if (e->state == ROBIN_ENTRY_LOCKED_OUT) return -4;
    if (e->state == ROBIN_ENTRY_TAMPERED) return -3;
    if (e->time_locked && !robin_unlock_ready(rv, entry_id, current_cycle)) return -2;

    /* Decrypt + verify GCM authentication tag in one step -- this is
     * the primary tamper check (stronger than a bare hash comparison:
     * it authenticates the actual ciphertext bytes, not just a digest
     * of the recovered plaintext). */
    uint8_t plaintext[ROBIN_MAX_DATA_LEN];
    int authentic = aes256_gcm_decrypt(rv->master_key, e->nonce, NULL, 0,
                                        e->ciphertext, e->ciphertext_len,
                                        e->tag, plaintext);
    if (!authentic) {
        e->state = ROBIN_ENTRY_TAMPERED;
        rv->total_tampered++;
        robin_update_coverage(rv);
        return -3;
    }

    /* Secondary check: content-identity hash (168-bit SHA-256) */
    uint8_t check_hash[ROBIN_HASH_LEN];
    robin_hash168(plaintext, e->ciphertext_len, check_hash);
    if (memcmp(check_hash, e->integrity_hash, ROBIN_HASH_LEN) != 0) {
        e->state = ROBIN_ENTRY_TAMPERED;
        rv->total_tampered++;
        robin_update_coverage(rv);
        return -3;
    }

    /* Copy plaintext out */
    if (plaintext_out && plaintext_len_out) {
        uint32_t copy_len = e->ciphertext_len;
        if (copy_len > ROBIN_MAX_DATA_LEN) copy_len = ROBIN_MAX_DATA_LEN;
        memcpy(plaintext_out, plaintext, copy_len);
        *plaintext_len_out = copy_len;
    }
    /* Do not leave the secret behind in this stack frame. */
    for (uint32_t i = 0; i < ROBIN_MAX_DATA_LEN; i++) ((volatile uint8_t *) plaintext)[i] = 0;

    e->state = ROBIN_ENTRY_UNLOCKED;
    e->failed_attempts = 0;
    rv->total_unlocked++;
    robin_update_coverage(rv);
    return (int32_t)e->ciphertext_len;
}

int32_t robin_delete(robin_vault_t *rv, uint32_t entry_id) {
    if (!rv) return -1;
    robin_entry_t *e = robin_get_entry(rv, entry_id);
    if (!e) return -1;
    /* Secure wipe */
    memset(e, 0, sizeof(*e));
    e->state = ROBIN_ENTRY_UNUSED;
    e->active = false;
    if (rv->num_entries > 0) rv->num_entries--;
    robin_update_coverage(rv);
    return 0;
}

uint32_t robin_check_unlocks(robin_vault_t *rv, uint64_t current_cycle) {
    if (!rv) return 0;
    uint32_t ready = 0;
    for (uint32_t i = 0; i < ROBIN_MAX_ENTRIES; i++) {
        robin_entry_t *e = &rv->entries[i];
        if (!e->active) continue;
        if (e->state == ROBIN_ENTRY_LOCKED && e->time_locked) {
            if (current_cycle >= e->unlock_at_cycle) {
                ready++;
            }
        }
    }
    return ready;
}

robin_entry_t *robin_get_entry(robin_vault_t *rv, uint32_t entry_id) {
    if (!rv) return NULL;
    for (uint32_t i = 0; i < ROBIN_MAX_ENTRIES; i++) {
        if (rv->entries[i].active && rv->entries[i].id == entry_id) {
            return &rv->entries[i];
        }
    }
    return NULL;
}

surplus_real_t robin_update_coverage(robin_vault_t *rv) {
    if (!rv) return SR_ZERO;

    /* r: integrity rate = (stored + unlocked) / total entries */
    uint32_t intact = 0;
    uint32_t total = 0;
    for (uint32_t i = 0; i < ROBIN_MAX_ENTRIES; i++) {
        if (!rv->entries[i].active) continue;
        total++;
        if (rv->entries[i].state == ROBIN_ENTRY_STORED ||
            rv->entries[i].state == ROBIN_ENTRY_UNLOCKED ||
            rv->entries[i].state == ROBIN_ENTRY_LOCKED) {
            intact++;
        }
    }
    rv->m5.r = (total == 0) ? SR_ONE
        : SR_DIV(SR_FROM_INT((int64_t)intact), SR_FROM_INT((int64_t)total));

    /* ell: unlock rate = unlocked / total entries */
    uint32_t unlocked = 0;
    for (uint32_t i = 0; i < ROBIN_MAX_ENTRIES; i++) {
        if (!rv->entries[i].active) continue;
        if (rv->entries[i].state == ROBIN_ENTRY_UNLOCKED) unlocked++;
    }
    rv->m5.ell = (total == 0) ? SR_ZERO
        : SR_DIV(SR_FROM_INT((int64_t)unlocked), SR_FROM_INT((int64_t)total));

    surplus_real_t product = SR_MUL(rv->m5.r, rv->m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(COVERAGE_FLOOR_NUM), SR_FROM_INT(COVERAGE_FLOOR_DEN));
    rv->coverage_ratio = SR_DIV(product, floor);

    return rv->coverage_ratio;
}
