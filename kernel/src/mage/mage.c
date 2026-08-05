/* mage.c — The Mage's Hats. See mage.h.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "mage.h"
#include "../robin_debanks/sha256.h"

static void zero(uint8_t *d, uint32_t n) { for (uint32_t i = 0; i < n; i++) d[i] = 0; }
static void put64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * (7 - i)));
}
static void put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * (3 - i)));
}

void mage_init(mage_ctx_t *m) {
    if (!m) return;
    for (uint32_t i = 0; i < sizeof(*m); i++) ((uint8_t *)m)[i] = 0;
}

void mage_set_verifier(mage_ctx_t *m, mage_verify_fn fn) { if (m) m->verify = fn; }

uint64_t mage_asset_add(mage_ctx_t *m, uint64_t id, bool owned) {
    if (!m || id == 0) return 0;
    mage_asset_t *a = mage_asset_get(m, id);
    if (a) { a->owned = a->owned || owned; return id; }
    if (m->asset_count >= MAGE_MAX_ASSETS) return 0;
    a = &m->assets[m->asset_count++];
    a->id = id; a->in_use = true; a->owned = owned;
    a->have_baseline = false;
    return id;
}

mage_asset_t *mage_asset_get(mage_ctx_t *m, uint64_t id) {
    if (!m) return 0;
    for (uint32_t i = 0; i < m->asset_count; i++)
        if (m->assets[i].in_use && m->assets[i].id == id) return &m->assets[i];
    return 0;
}

/* ===================== policy matrix ===================== */

bool mage_cap_is_gated(mage_cap_t cap) {
    switch (cap) {
    case MAGE_CAP_SCAN:
    case MAGE_CAP_EXPLOIT:
    case MAGE_CAP_C2:
    case MAGE_CAP_LATERAL:
    case MAGE_CAP_EXFIL:
        return true;
    default:
        return false;
    }
}

#define B(cap) MAGE_ROE_BIT(cap)
#define OPEN_DEFENSE (B(MAGE_CAP_RECON_PASSIVE)|B(MAGE_CAP_HARDEN)|B(MAGE_CAP_MONITOR)| \
                      B(MAGE_CAP_DETECT)|B(MAGE_CAP_RESPOND)|B(MAGE_CAP_FORENSIC)|      \
                      B(MAGE_CAP_AUDIT)|B(MAGE_CAP_EDUCATE))
#define OFFENSE (B(MAGE_CAP_SCAN)|B(MAGE_CAP_EXPLOIT)|B(MAGE_CAP_C2)| \
                 B(MAGE_CAP_LATERAL)|B(MAGE_CAP_EXFIL))

mage_roe_t mage_hat_grants(mage_hat_t hat) {
    switch (hat) {
    case MAGE_HAT_WHITE:  return OPEN_DEFENSE | OFFENSE;      /* the full kit    */
    case MAGE_HAT_PURPLE: return OPEN_DEFENSE | OFFENSE;      /* red + blue      */
    case MAGE_HAT_GREEN:  return OPEN_DEFENSE | OFFENSE;      /* all, sandboxed  */
    case MAGE_HAT_BLACK:  return B(MAGE_CAP_RECON_PASSIVE) | B(MAGE_CAP_EDUCATE) | OFFENSE;
    case MAGE_HAT_RED:    return B(MAGE_CAP_RECON_PASSIVE) | B(MAGE_CAP_EDUCATE) |
                                 B(MAGE_CAP_FORENSIC) | OFFENSE;
    case MAGE_HAT_ORANGE: return B(MAGE_CAP_RECON_PASSIVE) | B(MAGE_CAP_HARDEN) |
                                 B(MAGE_CAP_AUDIT) | B(MAGE_CAP_FORENSIC) |
                                 B(MAGE_CAP_EDUCATE) | OFFENSE;
    case MAGE_HAT_BLUE:   return OPEN_DEFENSE;
    case MAGE_HAT_YELLOW: return B(MAGE_CAP_RECON_PASSIVE) | B(MAGE_CAP_HARDEN) |
                                 B(MAGE_CAP_AUDIT) | B(MAGE_CAP_FORENSIC) |
                                 B(MAGE_CAP_EDUCATE);
    case MAGE_HAT_CLEAR:  return B(MAGE_CAP_RECON_PASSIVE) | B(MAGE_CAP_MONITOR) |
                                 B(MAGE_CAP_AUDIT) | B(MAGE_CAP_EDUCATE);
    case MAGE_HAT_GREY:   return B(MAGE_CAP_RECON_PASSIVE) | B(MAGE_CAP_AUDIT) |
                                 B(MAGE_CAP_EDUCATE);
    default: return 0;
    }
}

void mage_contain(mage_ctx_t *m, mage_hat_t hat, bool on) {
    if (m && hat > 0 && hat < MAGE_HAT__COUNT) m->contained[hat] = on;
}

/* ===================== audit chain ===================== */

static void audit_hash(const mage_audit_entry_t *e, uint8_t out[MAGE_HASH_LEN]) {
    uint8_t hdr[8 + 1 + 1 + 1 + 8 + 8];
    uint32_t at = 0;
    put64(hdr + at, e->seq); at += 8;
    hdr[at++] = e->hat; hdr[at++] = e->cap; hdr[at++] = e->decision;
    put64(hdr + at, e->target); at += 8;
    put64(hdr + at, e->tick);  at += 8;
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, hdr, at);
    sha256_update(&c, e->prev, MAGE_HASH_LEN);
    sha256_final(&c, out);
}

static void audit_append(mage_ctx_t *m, mage_hat_t hat, mage_cap_t cap,
                         mage_decision_t dec, uint64_t target, uint64_t tick) {
    uint32_t retained = m->audit_count < MAGE_MAX_AUDIT ? m->audit_count : MAGE_MAX_AUDIT;
    mage_audit_entry_t e;
    zero((uint8_t *)&e, sizeof e);
    e.seq = m->audit_count;               /* monotonic, never reused          */
    e.hat = (uint8_t)hat; e.cap = (uint8_t)cap;
    e.decision = (uint8_t)dec; e.target = target; e.tick = tick;
    if (retained > 0) {
        uint32_t last = (m->audit_head + MAGE_MAX_AUDIT - 1u) % MAGE_MAX_AUDIT;
        for (uint32_t i = 0; i < MAGE_HASH_LEN; i++) e.prev[i] = m->audit[last].hash[i];
    } /* else prev stays zero (genesis) */
    audit_hash(&e, e.hash);
    m->audit[m->audit_head] = e;
    m->audit_head = (m->audit_head + 1u) % MAGE_MAX_AUDIT;
    m->audit_count++;
}

uint32_t mage_audit_len(const mage_ctx_t *m) {
    if (!m) return 0;
    return m->audit_count < MAGE_MAX_AUDIT ? m->audit_count : MAGE_MAX_AUDIT;
}

bool mage_audit_verify(const mage_ctx_t *m) {
    if (!m) return false;
    uint32_t retained = mage_audit_len(m);
    if (retained == 0) return true;
    uint32_t start = m->audit_count <= MAGE_MAX_AUDIT ? 0 : m->audit_head;
    uint8_t prevhash[MAGE_HASH_LEN];
    bool have_prev = false;
    for (uint32_t i = 0; i < retained; i++) {
        const mage_audit_entry_t *e = &m->audit[(start + i) % MAGE_MAX_AUDIT];
        uint8_t h[MAGE_HASH_LEN];
        audit_hash(e, h);
        for (uint32_t k = 0; k < MAGE_HASH_LEN; k++)
            if (h[k] != e->hash[k]) return false;      /* fields tampered      */
        if (have_prev)
            for (uint32_t k = 0; k < MAGE_HASH_LEN; k++)
                if (e->prev[k] != prevhash[k]) return false;  /* link broken   */
        for (uint32_t k = 0; k < MAGE_HASH_LEN; k++) prevhash[k] = e->hash[k];
        have_prev = true;
    }
    return true;
}

/* ===================== engagements ===================== */

static mage_engagement_t *engage_alloc(mage_ctx_t *m) {
    for (uint32_t i = 0; i < MAGE_MAX_ENGAGEMENTS; i++)
        if (!m->engagements[i].in_use) {
            m->engagements[i].in_use = true;
            if (i + 1 > m->engagement_count) m->engagement_count = i + 1;
            return &m->engagements[i];
        }
    return 0;
}

static void engage_fill(mage_engagement_t *e, uint32_t id,
                        const uint64_t *scope, uint32_t scope_len,
                        mage_roe_t roe, uint64_t from, uint64_t until) {
    e->id = id; e->roe = roe; e->from_tick = from; e->until_tick = until;
    e->scope_len = scope_len > MAGE_MAX_SCOPE ? MAGE_MAX_SCOPE : scope_len;
    for (uint32_t i = 0; i < e->scope_len; i++) e->scope[i] = scope[i];
}

uint32_t mage_engage_self(mage_ctx_t *m,
                          const uint64_t *scope, uint32_t scope_len,
                          mage_roe_t roe, uint64_t from, uint64_t until) {
    if (!m || !scope || scope_len == 0 || scope_len > MAGE_MAX_SCOPE) return 0;
    if (until < from) return 0;
    /* Self-authorization is ONLY valid over assets we own. This is the "test my
     * own architecture" path and it cannot be turned against something we do
     * not control. */
    for (uint32_t i = 0; i < scope_len; i++) {
        mage_asset_t *a = mage_asset_get(m, scope[i]);
        if (!a || !a->owned) return 0;
    }
    mage_engagement_t *e = engage_alloc(m);
    if (!e) return 0;
    uint32_t id = (uint32_t)(e - m->engagements) + 1u;
    engage_fill(e, id, scope, scope_len, roe, from, until);
    e->authorized = true;                 /* owner authorized their own assets */
    zero(e->authorizer, 32);
    return id;
}

/* Canonical message an external authorizer signs. */
static uint32_t engage_message(uint8_t *out, uint32_t cap,
                               const uint64_t *scope, uint32_t scope_len,
                               mage_roe_t roe, uint64_t from, uint64_t until) {
    uint32_t need = 8 + 8 + 4 + 4 + scope_len * 8u;
    if (cap < need) return 0;
    uint32_t at = 0;
    put64(out + at, from); at += 8;
    put64(out + at, until); at += 8;
    put32(out + at, roe); at += 4;
    put32(out + at, scope_len); at += 4;
    for (uint32_t i = 0; i < scope_len; i++) { put64(out + at, scope[i]); at += 8; }
    return at;
}

uint32_t mage_engage_signed(mage_ctx_t *m,
                            const uint64_t *scope, uint32_t scope_len,
                            mage_roe_t roe, uint64_t from, uint64_t until,
                            const uint8_t authorizer[32], const uint8_t sig[64]) {
    if (!m || !scope || scope_len == 0 || scope_len > MAGE_MAX_SCOPE) return 0;
    if (until < from || !authorizer || !sig) return 0;
    if (!m->verify) return 0;             /* no verifier -> cannot trust a sig  */

    uint8_t msg[8 + 8 + 4 + 4 + MAGE_MAX_SCOPE * 8u];
    uint32_t n = engage_message(msg, sizeof msg, scope, scope_len, roe, from, until);
    if (n == 0) return 0;
    if (!m->verify(msg, n, sig, authorizer)) return 0;   /* forgery refused     */

    mage_engagement_t *e = engage_alloc(m);
    if (!e) return 0;
    uint32_t id = (uint32_t)(e - m->engagements) + 1u;
    engage_fill(e, id, scope, scope_len, roe, from, until);
    e->authorized = true;
    for (uint32_t i = 0; i < 32; i++) e->authorizer[i] = authorizer[i];
    return id;
}

static bool scope_has(const mage_engagement_t *e, uint64_t target) {
    for (uint32_t i = 0; i < e->scope_len; i++) if (e->scope[i] == target) return true;
    return false;
}

/* ===================== the decision ===================== */

mage_decision_t mage_authorize(mage_ctx_t *m, mage_hat_t hat, mage_cap_t cap,
                               uint64_t target, uint64_t now_tick) {
    if (!m || hat <= 0 || hat >= MAGE_HAT__COUNT || cap < 0 || cap >= MAGE_CAP__COUNT)
        return MAGE_DENY;

    mage_decision_t dec;
    mage_roe_t grants = mage_hat_grants(hat);

    if (!(grants & MAGE_ROE_BIT(cap))) {
        dec = MAGE_DENY;                          /* this hat never does this   */
    } else if (hat == MAGE_HAT_GREEN) {
        dec = MAGE_SANDBOX_ONLY;                  /* learner: simulated only    */
    } else if (!mage_cap_is_gated(cap)) {
        dec = MAGE_ALLOW;                         /* open defensive/audit cap   */
    } else if (m->contained[hat]) {
        dec = MAGE_CONTAINED;                     /* offensive while contained  */
    } else {
        /* GATED: needs an active, authorized, in-scope, in-window engagement. */
        bool saw_expired = false, saw_out_of_scope = false;
        dec = MAGE_NEEDS_ENGAGEMENT;
        for (uint32_t i = 0; i < MAGE_MAX_ENGAGEMENTS; i++) {
            const mage_engagement_t *e = &m->engagements[i];
            if (!e->in_use || !e->authorized) continue;
            bool roe_ok = (e->roe & MAGE_ROE_BIT(cap)) != 0;
            if (!roe_ok) continue;
            bool scope_ok = scope_has(e, target);
            bool window_ok = now_tick >= e->from_tick && now_tick <= e->until_tick;
            if (scope_ok && window_ok) { dec = MAGE_ALLOW; break; }
            if (scope_ok && !window_ok) saw_expired = true;
            if (!scope_ok) saw_out_of_scope = true;
        }
        if (dec != MAGE_ALLOW) {
            if (saw_expired)            dec = MAGE_EXPIRED;
            else if (saw_out_of_scope)  dec = MAGE_OUT_OF_SCOPE;
            else                        dec = MAGE_NEEDS_ENGAGEMENT;
        }
    }

    audit_append(m, hat, cap, dec, target, now_tick);
    return dec;
}

/* ===================== defensive tools ===================== */

bool mage_baseline_set(mage_ctx_t *m, uint64_t asset, const uint8_t *content, uint32_t len) {
    mage_asset_t *a = mage_asset_get(m, asset);
    if (!a || !content) return false;
    sha256(content, len, a->baseline);
    a->have_baseline = true;
    return true;
}

bool mage_integrity_check(mage_ctx_t *m, uint64_t asset, const uint8_t *content, uint32_t len) {
    mage_asset_t *a = mage_asset_get(m, asset);
    if (!a || !a->have_baseline || !content) return false;
    uint8_t h[MAGE_HASH_LEN];
    sha256(content, len, h);
    for (uint32_t i = 0; i < MAGE_HASH_LEN; i++) if (h[i] != a->baseline[i]) return false;
    return true;
}

void mage_rate_init(mage_rate_t *r) { if (r) { r->n = 0; r->sum = 0; for (int i=0;i<16;i++) r->win[i]=0; } }

bool mage_rate_observe(mage_rate_t *r, uint32_t sample, uint32_t k_times_4) {
    if (!r) return false;
    /* Need a little history before calling anything anomalous. */
    bool anomalous = false;
    if (r->n >= 4) {
        uint32_t mean = r->sum / r->n;
        /* mean absolute deviation, integer */
        uint32_t mad = 0, cnt = r->n < 16 ? r->n : 16;
        for (uint32_t i = 0; i < cnt; i++) {
            uint32_t v = r->win[i];
            mad += (v > mean) ? (v - mean) : (mean - v);
        }
        mad = cnt ? mad / cnt : 0;
        if (mad == 0) mad = 1;               /* avoid a zero threshold          */
        /* flag if sample exceeds mean + (k/4)*MAD */
        uint32_t thresh = mean + (k_times_4 * mad) / 4u;
        if (sample > thresh) anomalous = true;
    }
    /* update rolling window (last 16) */
    uint32_t idx = r->n % 16u;
    if (r->n >= 16) r->sum -= r->win[idx];
    r->win[idx] = sample;
    r->sum += sample;
    r->n++;
    return anomalous;
}

/* ===================== adversary emulation ===================== */

mage_cap_t mage_technique_cap(mage_technique_t tech) {
    switch (tech) {
    case MAGE_TECH_PORT_PROBE:    return MAGE_CAP_SCAN;
    case MAGE_TECH_AUTH_GUESS:    return MAGE_CAP_EXPLOIT;
    case MAGE_TECH_TAMPER:        return MAGE_CAP_EXPLOIT;
    case MAGE_TECH_REPLAY:        return MAGE_CAP_EXPLOIT;
    case MAGE_TECH_INJECT:        return MAGE_CAP_EXPLOIT;
    case MAGE_TECH_EXFIL_CHANNEL: return MAGE_CAP_EXFIL;
    default:                      return MAGE_CAP_SCAN;
    }
}

/* Defenses are stored in a small static registry keyed by technique. Kept
 * outside mage_ctx_t so the context stays a plain data struct; a real system
 * would hang these off the ctx, but a file-static table keeps the ABI simple
 * and the tests honest. */
typedef struct { mage_defense_fn fn; void *ctx; bool used; } mage_def_slot_t;
static mage_def_slot_t g_defenses[MAGE_TECH__COUNT][MAGE_MAX_DEFENSES];

bool mage_defense_register(mage_ctx_t *m, mage_technique_t tech,
                           mage_defense_fn fn, void *ctx) {
    (void)m;
    if (tech < 0 || tech >= MAGE_TECH__COUNT || !fn) return false;
    for (uint32_t i = 0; i < MAGE_MAX_DEFENSES; i++)
        if (!g_defenses[tech][i].used) {
            g_defenses[tech][i].fn = fn;
            g_defenses[tech][i].ctx = ctx;
            g_defenses[tech][i].used = true;
            return true;
        }
    return false;
}

mage_finding_t mage_emulate(mage_ctx_t *m, mage_hat_t hat,
                            mage_technique_t tech, uint64_t target,
                            uint64_t now_tick) {
    if (!m || tech < 0 || tech >= MAGE_TECH__COUNT) return MAGE_FINDING_NOT_APPLICABLE;

    /* The emulator cannot be a way around the gate: the underlying capability
     * must be authorized against this target, or the technique does not run
     * and the refusal is audited. */
    mage_cap_t cap = mage_technique_cap(tech);
    mage_decision_t dec = mage_authorize(m, hat, cap, target, now_tick);
    if (dec != MAGE_ALLOW && dec != MAGE_SANDBOX_ONLY)
        return MAGE_FINDING_NOT_APPLICABLE;

    /* Run every registered defense for this technique. If ANY catches it, the
     * finding is CAUGHT (the defender wins). If none does, it is a GAP — a
     * weakness the operator now knows about. The emulator delivers no payload;
     * it only asks the defenses whether they would have noticed. */
    bool any_defense = false, caught = false;
    for (uint32_t i = 0; i < MAGE_MAX_DEFENSES; i++) {
        if (!g_defenses[tech][i].used) continue;
        any_defense = true;
        if (g_defenses[tech][i].fn(tech, target, g_defenses[tech][i].ctx)) {
            caught = true;
            break;
        }
    }
    if (!any_defense) return MAGE_FINDING_GAP;   /* no defense registered at all */
    return caught ? MAGE_FINDING_CAUGHT : MAGE_FINDING_GAP;
}

/* Test/reset hook for the file-static defense registry. */
void mage_defenses_reset(void) {
    for (int t = 0; t < MAGE_TECH__COUNT; t++)
        for (uint32_t i = 0; i < MAGE_MAX_DEFENSES; i++)
            g_defenses[t][i].used = false;
}
