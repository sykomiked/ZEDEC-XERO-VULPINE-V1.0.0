/* immigration.c — Immigration Enforcement implementation
 *
 * See immigration.h for design rationale and crypto integration
 * instructions. Uses HMAC-SHA256 verification signature verification
 * (same pattern as Count House / Community Chest / AI Layer).
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * License: SEL-3.3 (kernel component)
 */
#include "immigration.h"
#include <string.h>
#include "../robin_debanks/ed25519_verify.h"

static bool immig_verify_sig(const immig_daemon_t *d) {
    if (!d) return false;
    /* Ed25519 asymmetric verification: verifies the signature against
     * the kernel's embedded PUBLIC key. The corresponding private key
     * is held offline/HSM-backed and never compiled into the kernel.
     * This replaces the previous HMAC-SHA256 symmetric scheme. */
    uint8_t msg[IMMIG_CONTENT_HASH_LEN + IMMIG_PUBKEY_LEN + 21];
    uint32_t pos = 0;
    for (uint32_t i = 0; i < IMMIG_CONTENT_HASH_LEN; i++) msg[pos++] = d->content_hash[i];
    for (uint32_t i = 0; i < IMMIG_PUBKEY_LEN; i++) msg[pos++] = d->pubkey[i];
    for (uint32_t i = 0; i < 21; i++) msg[pos++] = d->daemon_id.bytes[i];
    return ed25519_verify(msg, pos, d->signature, ED25519_PUBKEY_IMMIGRATION);
}

void immig_init(immigration_t *im, uint32_t device_id, porter_house_t *porter) {
    if (!im) return;
    memset(im, 0, sizeof(*im));
    im->device_id = device_id;
    im->porter = porter;
    im->num_daemons = 0;
    im->next_id = 1;
    im->m5.omega = device_id;
    im->m5.chi = device_id;
    im->m5.phi = SR_ZERO;
    immig_update_coverage(im);
}

int32_t immig_apply_visa(immigration_t *im, const char *name,
                          immig_visa_t visa,
                          const word168_t *daemon_id,
                          const uint8_t pubkey[IMMIG_PUBKEY_LEN],
                          const uint8_t content_hash[IMMIG_CONTENT_HASH_LEN],
                          const uint8_t signature[IMMIG_SIG_LEN],
                          uint32_t network_ports,
                          uint64_t current_cycle) {
    if (!im || !name || !daemon_id) return -1;

    uint32_t slot = IMMIG_MAX_DAEMONS;
    for (uint32_t i = 0; i < IMMIG_MAX_DAEMONS; i++) {
        if (!im->daemons[i].active) {
            slot = i;
            break;
        }
    }
    if (slot >= IMMIG_MAX_DAEMONS) return -1;

    immig_daemon_t *d = &im->daemons[slot];
    memset(d, 0, sizeof(*d));
    d->id = im->next_id++;
    d->active = true;
    d->visa = visa;
    d->state = IMMIG_DAEMON_PENDING;
    d->daemon_id = *daemon_id;
    d->network_ports = network_ports;
    d->created_cycle = current_cycle;
    d->last_active_cycle = current_cycle;
    d->violations = 0;

    uint32_t j;
    for (j = 0; j + 1 < IMMIG_MAX_NAME_LEN && name[j]; j++) d->name[j] = name[j];
    d->name[j] = '\0';

    if (pubkey) memcpy(d->pubkey, pubkey, IMMIG_PUBKEY_LEN);
    if (content_hash) memcpy(d->content_hash, content_hash, IMMIG_CONTENT_HASH_LEN);
    if (signature) memcpy(d->signature, signature, IMMIG_SIG_LEN);

    /* Verify signature */
    d->sig_verified = im->verify_sig ? im->verify_sig(d) : immig_verify_sig(d);
    if (!d->sig_verified) {
        d->state = IMMIG_DAEMON_REJECTED;
        im->num_daemons++;
        im->total_rejected++;
        immig_update_coverage(im);
        return -2;
    }

    /* Network-facing daemons must pass Porter House admission.
     * If Porter House denies, the visa is REJECTED — fail-closed.
     * No network capability is granted without Porter House approval. */
    if (im->porter && d->network_ports != 0) {
        if (!porter_house_admit(im->porter, 8800, daemon_id, 500)) {
            d->state = IMMIG_DAEMON_REJECTED;
            d->active = false;
            im->num_daemons++;
            im->total_rejected++;
            immig_update_coverage(im);
            return -3;  /* Porter House denial — visa rejected */
        }
    }

    d->state = IMMIG_DAEMON_GRANTED;
    im->num_daemons++;
    im->total_granted++;

    switch (visa) {
        case IMMIG_VISA_RESIDENT: im->resident_count++; break;
        case IMMIG_VISA_WORKER:   im->worker_count++; break;
        case IMMIG_VISA_TRANSIT:  im->transit_count++; break;
        default: break;
    }

    immig_update_coverage(im);
    return (int32_t)d->id;
}

bool immig_verify_daemon(immigration_t *im, uint32_t daemon_id) {
    if (!im) return false;
    immig_daemon_t *d = immig_get_daemon(im, daemon_id);
    if (!d) return false;
    d->sig_verified = im->verify_sig ? im->verify_sig(d) : immig_verify_sig(d);
    if (d->sig_verified && d->state == IMMIG_DAEMON_REJECTED) {
        d->state = IMMIG_DAEMON_GRANTED;
        im->total_rejected--;
        im->total_granted++;
    }
    return d->sig_verified;
}

int32_t immig_record_violation(immigration_t *im, uint32_t daemon_id) {
    if (!im) return -1;
    immig_daemon_t *d = immig_get_daemon(im, daemon_id);
    if (!d || d->state != IMMIG_DAEMON_GRANTED) return -1;

    d->violations++;
    if (d->violations >= IMMIG_MAX_VIOLATIONS) {
        immig_deport(im, daemon_id);
    }
    immig_update_coverage(im);
    return (int32_t)d->violations;
}

int32_t immig_deport(immigration_t *im, uint32_t daemon_id) {
    if (!im) return -1;
    immig_daemon_t *d = immig_get_daemon(im, daemon_id);
    if (!d) return -1;
    d->state = IMMIG_DAEMON_DEPORTED;
    im->total_deported++;

    switch (d->visa) {
        case IMMIG_VISA_RESIDENT: if (im->resident_count > 0) im->resident_count--; break;
        case IMMIG_VISA_WORKER:   if (im->worker_count > 0) im->worker_count--; break;
        case IMMIG_VISA_TRANSIT:  if (im->transit_count > 0) im->transit_count--; break;
        default: break;
    }

    immig_update_coverage(im);
    return 0;
}

uint32_t immig_check_expired(immigration_t *im, uint64_t current_cycle,
                              uint64_t transit_timeout) {
    if (!im) return 0;
    uint32_t expired = 0;

    for (uint32_t i = 0; i < IMMIG_MAX_DAEMONS; i++) {
        immig_daemon_t *d = &im->daemons[i];
        if (!d->active) continue;
        if (d->state != IMMIG_DAEMON_GRANTED) continue;
        if (d->visa != IMMIG_VISA_TRANSIT) continue;

        if (current_cycle - d->last_active_cycle >= transit_timeout) {
            d->state = IMMIG_DAEMON_EXPIRED;
            im->total_expired++;
            if (im->transit_count > 0) im->transit_count--;
            expired++;
        }
    }

    if (expired > 0) immig_update_coverage(im);
    return expired;
}

int32_t immig_touch(immigration_t *im, uint32_t daemon_id, uint64_t current_cycle) {
    if (!im) return -1;
    immig_daemon_t *d = immig_get_daemon(im, daemon_id);
    if (!d || d->state != IMMIG_DAEMON_GRANTED) return -1;
    d->last_active_cycle = current_cycle;
    return 0;
}

immig_daemon_t *immig_get_daemon(immigration_t *im, uint32_t daemon_id) {
    if (!im) return NULL;
    for (uint32_t i = 0; i < IMMIG_MAX_DAEMONS; i++) {
        if (im->daemons[i].active && im->daemons[i].id == daemon_id) {
            return &im->daemons[i];
        }
    }
    return NULL;
}

surplus_real_t immig_update_coverage(immigration_t *im) {
    if (!im) return SR_ZERO;

    /* r: compliance rate = granted / (granted + rejected + deported) */
    uint32_t total_resolved = im->total_granted + im->total_rejected + im->total_deported;
    im->m5.r = (total_resolved == 0) ? SR_ONE
        : SR_DIV(SR_FROM_INT((int64_t)im->total_granted), SR_FROM_INT((int64_t)total_resolved));

    /* ell: active daemon rate = granted / total daemons */
    uint32_t granted = 0;
    uint32_t total = 0;
    for (uint32_t i = 0; i < IMMIG_MAX_DAEMONS; i++) {
        if (!im->daemons[i].active) continue;
        total++;
        if (im->daemons[i].state == IMMIG_DAEMON_GRANTED) granted++;
    }
    im->m5.ell = (total == 0) ? SR_ZERO
        : SR_DIV(SR_FROM_INT((int64_t)granted), SR_FROM_INT((int64_t)total));

    surplus_real_t product = SR_MUL(im->m5.r, im->m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(COVERAGE_FLOOR_NUM), SR_FROM_INT(COVERAGE_FLOOR_DEN));
    im->coverage_ratio = SR_DIV(product, floor);

    return im->coverage_ratio;
}
