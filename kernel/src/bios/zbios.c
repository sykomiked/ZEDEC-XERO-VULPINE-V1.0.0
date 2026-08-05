/* zbios.c — ZXV staged measured boot. See zbios.h. */
#include "zbios.h"
#include "../robin_debanks/sha256.h"

static void zmemcpy(uint8_t *d, const uint8_t *s, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}
static void zmemset(uint8_t *d, uint8_t v, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) d[i] = v;
}

void zb_init(zbios_t *b, const uint8_t *anchor_key, uint32_t anchor_len) {
    if (!b) return;
    for (uint32_t k = 0; k < ZB_KEYS; k++) {
        b->stage[k].name[0] = '\0';
        zmemset(b->stage[k].digest, 0, ZB_DIGEST_LEN);
        b->stage[k].version = 0;
        b->stage[k].executed = false;
        b->stage[k].verified = false;
        b->min_version[k] = 0;
    }
    /* M[0] = SHA256(anchor_key) — the root of the chain. Everything that
     * follows commits to this value, so a different anchor produces a
     * completely different attestation. A missing anchor is NOT a softer
     * variant of that — an all-zero root would happily seal a full
     * attestation that attests to nothing. Halt instead: every subsequent
     * zb_stage/zb_seal then reports ZB_ERR_HALTED. */
    b->next_key = 0;
    b->halted = false;
    b->fault = ZB_OK;
    b->stages_run = 0;
    b->sealed = false;

    if (anchor_key && anchor_len) {
        sha256(anchor_key, anchor_len, b->chain);
    } else {
        zmemset(b->chain, 0, ZB_DIGEST_LEN);
        b->halted = true;                   /* set LAST — nothing may clear it */
        b->fault  = ZB_ERR_ARG;
    }
}

void zb_set_min_version(zbios_t *b, zb_key_t key, uint32_t min_version) {
    if (!b || (uint32_t)key >= ZB_KEYS) return;
    b->min_version[key] = min_version;
}

zb_result_t zb_stage(zbios_t *b, zb_key_t key, const char *name,
                     const uint8_t digest[ZB_DIGEST_LEN],
                     uint32_t version, bool verified) {
    if (!b || !digest) return ZB_ERR_ARG;
    if (b->halted) return ZB_ERR_HALTED;      /* fail-closed: no recovery */
    if ((uint32_t)key >= ZB_KEYS) { b->halted = true; b->fault = ZB_ERR_ARG; return ZB_ERR_ARG; }

    /* ORDER: keys must run 0..9 with no skips and no repeats. Enforcing
     * this is what makes the chain's final value meaningful — otherwise a
     * stage could be replayed or bypassed. */
    if ((uint32_t)key != b->next_key) {
        b->halted = true; b->fault = ZB_ERR_ORDER;
        return ZB_ERR_ORDER;
    }

    /* SIGNATURE: the caller performs the Ed25519 check (keeping this
     * module independent of any particular signature scheme) and reports
     * it here. A false result halts the chain. */
    if (!verified) {
        b->halted = true; b->fault = ZB_ERR_SIGNATURE;
        return ZB_ERR_SIGNATURE;
    }

    /* ROLLBACK: a stage may never be older than the recorded floor. This
     * is what stops an attacker re-installing a signed-but-vulnerable
     * earlier image. */
    if (version < b->min_version[key]) {
        b->halted = true; b->fault = ZB_ERR_ROLLBACK;
        return ZB_ERR_ROLLBACK;
    }

    /* EXTEND: M[k+1] = SHA256( M[k] || digest || key_index )
     * Including the key index binds each measurement to its position, so
     * two stages cannot be transposed without changing the result. */
    uint8_t buf[ZB_DIGEST_LEN + ZB_DIGEST_LEN + 4];
    zmemcpy(buf, b->chain, ZB_DIGEST_LEN);
    zmemcpy(buf + ZB_DIGEST_LEN, digest, ZB_DIGEST_LEN);
    uint32_t ki = (uint32_t)key;
    buf[2*ZB_DIGEST_LEN + 0] = (uint8_t)(ki);
    buf[2*ZB_DIGEST_LEN + 1] = (uint8_t)(ki >> 8);
    buf[2*ZB_DIGEST_LEN + 2] = (uint8_t)(ki >> 16);
    buf[2*ZB_DIGEST_LEN + 3] = (uint8_t)(ki >> 24);
    sha256(buf, sizeof(buf), b->chain);

    zb_stage_t *s = &b->stage[key];
    uint32_t i = 0;
    if (name) while (i < ZB_NAME_LEN - 1 && name[i]) { s->name[i] = name[i]; i++; }
    s->name[i] = '\0';
    zmemcpy(s->digest, digest, ZB_DIGEST_LEN);
    s->version = version;
    s->executed = true;
    s->verified = true;

    b->next_key++;
    b->stages_run++;
    return ZB_OK;
}

zb_result_t zb_seal(zbios_t *b, uint8_t out[ZB_DIGEST_LEN]) {
    if (!b || !out) return ZB_ERR_ARG;
    if (b->halted) return ZB_ERR_HALTED;
    if (b->next_key != ZB_KEYS) {          /* not every key has run */
        b->halted = true; b->fault = ZB_ERR_ORDER;
        return ZB_ERR_ORDER;
    }
    zmemcpy(out, b->chain, ZB_DIGEST_LEN);
    b->sealed = true;
    return ZB_OK;
}

zb_triad_t zb_triad_of(zb_key_t key) {
    if (key <= ZB_K2_MEASURE) return ZB_TRIAD_SILICON;
    if (key <= ZB_K5_PAYLOAD) return ZB_TRIAD_STORAGE;
    if (key <= ZB_K8_HANDOFF) return ZB_TRIAD_SOVEREIGNTY;
    return ZB_TRIAD_SEAL;
}

const char *zb_key_name(zb_key_t key) {
    switch (key) {
        case ZB_K0_ANCHOR:  return "K0 anchor (root of trust)";
        case ZB_K1_SILICON: return "K1 silicon init";
        case ZB_K2_MEASURE: return "K2 measurement root";
        case ZB_K3_MEDIUM:  return "K3 boot medium";
        case ZB_K4_SLOT:    return "K4 slot select + rollback";
        case ZB_K5_PAYLOAD: return "K5 payload verify";
        case ZB_K6_FABRIC:  return "K6 cell fabric admission";
        case ZB_K7_POLICY:  return "K7 capability policy";
        case ZB_K8_HANDOFF: return "K8 runtime handoff";
        case ZB_K9_SEAL:    return "K9 attestation seal";
        default:            return "unknown key";
    }
}

const char *zb_triad_name(zb_triad_t t) {
    switch (t) {
        case ZB_TRIAD_SILICON:     return "I. SILICON";
        case ZB_TRIAD_STORAGE:     return "II. STORAGE";
        case ZB_TRIAD_SOVEREIGNTY: return "III. SOVEREIGNTY";
        default:                   return "SEAL";
    }
}

const char *zb_strerror(zb_result_t r) {
    switch (r) {
        case ZB_OK:            return "ok";
        case ZB_ERR_ORDER:     return "stage out of order / chain incomplete";
        case ZB_ERR_SIGNATURE: return "stage signature did not verify";
        case ZB_ERR_ROLLBACK:  return "stage version below the rollback floor";
        case ZB_ERR_HALTED:    return "chain halted (fail-closed)";
        default:               return "bad argument";
    }
}
