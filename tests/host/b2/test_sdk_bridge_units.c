/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_sdk_bridge_units.c: unit tests for kernel/src/sdk_bridge/sdk_bridge.c.
 * Covers sb_init (every bridge field, every byte zeroed), the full built-in
 * capability table (every field of every descriptor), sb_attest's LPRES
 * conjunction, grant/revoke/has (coverage and attestation boundaries, HSM
 * sandbox, full context, duplicate grants, callbacks), self-audit/self-heal
 * (coverage formula, per-capability minimums, counters, callbacks), coverage
 * enforcement, stats copy, callback registration and the name tables.
 * Superset of kernel/src/sdk_bridge/test_sdk_bridge.c. */
#include <stdio.h>
#include <string.h>
#include "sdk_bridge.h"

static int fails = 0;
static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            printf("  FAIL line %d: %s\n", __LINE__, #cond);                                       \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static sdk_bridge_t B;
static sb_context_t C;

/* expected built-in table, in registration order */
typedef struct {
    sb_capability_t id;
    const char *name;
    const char *desc;
    double cov;
    bool hsm, zk;
    uint8_t form;
    uint64_t bal;
} exp_cap_t;

static const exp_cap_t EXP[] = {
    {SB_CAP_CAPITAL_TRANSFER, "capital.transfer", "Transfer capital between accounts", 1.8, false,
     false, 5, 1000},
    {SB_CAP_DERIVATIVES_TRADE, "derivatives.trade", "Trade derivatives contracts", 2.0, true, true,
     5, 10000},
    {SB_CAP_ASSURANCE_CREATE, "assurance.create", "Create assurance contracts", 2.0, false, true, 5,
     5000},
    {SB_CAP_TREATY_TOKENIZE, "treaty.tokenize", "Tokenize conservation easements", 2.5, true, true,
     3, 100000},
    {SB_CAP_NETWORK_MESH, "network.mesh", "Create/join mesh networks", 1.8, false, false, 6, 1000},
    {SB_CAP_STORAGE_PERSIST, "storage.persist", "Persistent storage access", 1.8, false, false, 6,
     10000},
    {SB_CAP_MEDIA_STREAM, "media.stream", "Real-time media streaming", 2.0, false, false, 8, 10000},
    {SB_CAP_GOVERNANCE_VOTE, "governance.vote", "Participate in governance", 2.0, false, true, 4,
     10000},
    {SB_CAP_IDENTITY_CREDENTIAL, "identity.credential", "Issue/verify credentials", 1.8, false,
     false, 1, 1000},
    {SB_CAP_COMPUTE_SCHEDULE, "compute.schedule", "Schedule compute tasks", 1.8, false, false, 2,
     1000},
    {SB_CAP_CRYPTO_SIGN, "crypto.sign", "Cryptographic signing", 2.0, true, false, 7, 1000},
    {SB_CAP_CRYPTO_ENCRYPT, "crypto.encrypt", "Cryptographic encryption", 2.0, true, false, 7, 1000},
    {SB_CAP_CRYPTO_ZK_PROVE, "crypto.zk_prove", "Zero-knowledge proof generation", 2.5, true, true,
     7, 10000},
    {SB_CAP_CRYPTO_KEM, "crypto.kem", "Key encapsulation mechanism", 2.5, true, true, 7, 10000},
    {SB_CAP_HSM_OPERATION, "hsm.operation", "Hardware security module operation", 2.5, true, true,
     7, 10000},
    {SB_CAP_TELECOM_SIGNAL, "telecom.signal", "Telecom signal processing", 1.8, false, false, 1,
     1000},
    {SB_CAP_TELECOM_GLARE_DETECT, "telecom.glare_detect", "Telecom glare detection (LPRES)", 2.0,
     false, true, 1, 1000},
    {SB_CAP_AI_INFERENCE, "ai.inference", "AI model inference", 2.0, false, true, 8, 10000},
    {SB_CAP_AI_TRAINING, "ai.training", "AI model training", 2.5, true, true, 8, 100000},
    {SB_CAP_QUANTUM_OPERATION, "quantum.operation", "Quantum device operation", 3.0, true, true, 7,
     100000},
    {SB_CAP_HOLOGRAPHIC_RENDER, "holographic.render", "Holographic interference rendering", 2.0,
     false, false, 8, 10000},
    {SB_CAP_TRIPARTITE_FILE, "tripartite.file", "Tripartite file operations", 1.8, false, false, 8,
     10000},
    {SB_CAP_CIVILIZATIONAL_APP, "civilizational.app", "Civilizational stack application", 1.8,
     false, false, 0, 0},
};
#define N_EXP (sizeof EXP / sizeof EXP[0])

/* callback recorders */
static int n_grant, n_revoke, n_fail, n_heal;
static sb_context_t *last_ctx;
static sb_capability_t last_cap;
static lpres_state_t last_state;
static uint32_t last_op;

static void on_grant(sb_context_t *c, sb_capability_t cap, lpres_state_t s)
{
    n_grant++;
    last_ctx = c;
    last_cap = cap;
    last_state = s;
}
static void on_revoke(sb_context_t *c, sb_capability_t cap, lpres_state_t s)
{
    n_revoke++;
    last_ctx = c;
    last_cap = cap;
    last_state = s;
}
static void on_fail(sb_context_t *c, uint32_t op, lpres_state_t s)
{
    n_fail++;
    last_ctx = c;
    last_op = op;
    last_state = s;
}
static void on_heal(sb_context_t *c, uint32_t op, lpres_state_t s)
{
    n_heal++;
    last_ctx = c;
    last_op = op;
    last_state = s;
}
static void on_drop(sb_context_t *c, surplus_real_t r, lpres_state_t s)
{
    (void) c;
    (void) r;
    (void) s;
}

static void reset_cb(void)
{
    n_grant = n_revoke = n_fail = n_heal = 0;
    last_ctx = NULL;
    last_cap = (sb_capability_t) 0;
    last_state = (lpres_state_t) 77;
    last_op = 0;
}

static void fresh_bridge(void)
{
    memset(&B, 0xFF, sizeof B);
    sb_init(&B, NULL, NULL, NULL);
}

static void fresh_ctx(lpres_state_t att, bool sandboxed)
{
    memset(&C, 0, sizeof C);
    C.coverage_ratio = SR_FROM_FLOAT(10.0);
    C.process_attestation = att;
    C.sandboxed = sandboxed;
}

static sb_capability_desc_t *desc(sb_capability_t c)
{
    for (uint32_t i = 0; i < B.num_capabilities; i++)
        if (B.capabilities[i].id == c) return &B.capabilities[i];
    return NULL;
}

static void set_m5(uint64_t omega, double r, double ell, double phi, int32_t chi)
{
    C.m5.omega = omega;
    C.m5.r = r;
    C.m5.ell = ell;
    C.m5.phi = phi;
    C.m5.chi = chi;
}

static void test_init(void)
{
    crypto_wallet_system_t *cw = (crypto_wallet_system_t *) (uintptr_t) 0x1000;
    mesh_net_t *mn = (mesh_net_t *) (uintptr_t) 0x2000;
    financial_fabric_t *ff = (financial_fabric_t *) (uintptr_t) 0x3000;
    memset(&B, 0xFF, sizeof B);
    sb_init(&B, cw, mn, ff);
    CHECK(B.current_context == NULL);
    CHECK(B.crypto_wallet == cw);
    CHECK(B.mesh == mn);
    CHECK(B.financial == ff);
    CHECK(B.min_global_coverage == 1.8);
    CHECK(B.global_attestation == LPRES_STATE_NEITHER);
    CHECK(B.global_safety_gate == false);
    CHECK(B.initialized == true);
    CHECK(B.stats.total_api_calls == 0 && B.stats.total_audits == 0 &&
          B.stats.total_healings == 0 && B.stats.total_translations == 0 &&
          B.stats.total_capability_grants == 0 && B.stats.total_capability_revokes == 0);
    CHECK(B.num_capabilities == N_EXP);
    CHECK(B.num_capabilities == (uint32_t) SB_CAP_MAX - 1);
    /* slots past the table are zeroed by the init */
    int zero = 1;
    for (uint32_t i = N_EXP; i < SB_MAX_CAPABILITIES; i++) {
        const uint8_t *p = (const uint8_t *) &B.capabilities[i];
        for (size_t k = 0; k < sizeof B.capabilities[i]; k++) zero &= p[k] == 0;
    }
    CHECK(zero);

    /* every descriptor field */
    for (uint32_t i = 0; i < N_EXP; i++) {
        const sb_capability_desc_t *d = &B.capabilities[i];
        const exp_cap_t *e = &EXP[i];
        int ok = d->id == e->id && !strcmp(d->name, e->name) && !strcmp(d->description, e->desc) &&
                 d->min_coverage == e->cov && d->min_attestation == LPRES_STATE_TRUE &&
                 d->requires_hsm == e->hsm && d->requires_zk_proof == e->zk &&
                 d->capital_form_required == e->form && d->min_balance == e->bal &&
                 d->policy_id == 0;
        if (!ok) printf("  descriptor %u (%s) mismatch\n", i, e->name);
        CHECK(ok);
        CHECK(!strcmp(sb_capability_name(d->id), d->name));
    }
    int uniq = 1;
    for (uint32_t i = 0; i < B.num_capabilities; i++)
        for (uint32_t j = i + 1; j < B.num_capabilities; j++)
            uniq &= B.capabilities[i].id != B.capabilities[j].id;
    CHECK(uniq);
    CHECK(SB_CAP_TELECOM_GLARE_DETECT != SB_CAP_AI_INFERENCE &&
          SB_CAP_AI_TRAINING != SB_CAP_QUANTUM_OPERATION);

    /* NULL bridge is a no-op; registering on NULL too */
    sb_init(NULL, cw, mn, ff);
    sb_register_builtin_capabilities(NULL);
    /* registering again appends a second copy */
    sb_register_builtin_capabilities(&B);
    CHECK(B.num_capabilities == 2 * N_EXP);
    CHECK(B.capabilities[N_EXP].id == SB_CAP_CAPITAL_TRANSFER);
    CHECK(B.capabilities[2 * N_EXP - 1].id == SB_CAP_CIVILIZATIONAL_APP);
}

static void test_context(void)
{
    fresh_bridge();
    word168_t id;
    memset(&id, 0x5A, sizeof id);
    /* the context pool is not wired: creation always yields NULL */
    CHECK(sb_create_context(NULL, &id) == NULL);
    CHECK(sb_create_context(&B, NULL) == NULL);
    CHECK(sb_create_context(&B, &id) == NULL);

    fresh_ctx(LPRES_STATE_TRUE, false);
    C.active = true;
    C.initialized = true;
    sb_destroy_context(NULL, &C);
    CHECK(C.active && C.initialized);
    sb_destroy_context(&B, NULL);
    sb_destroy_context(&B, &C);
    CHECK(!C.active && !C.initialized);
}

static void test_attest(void)
{
    fresh_bridge();
    fresh_ctx(LPRES_STATE_TRUE, false);
    CHECK(sb_attest(NULL, &C, 1, NULL, 0) == LPRES_STATE_NEITHER);
    CHECK(sb_attest(&B, NULL, 1, NULL, 0) == LPRES_STATE_NEITHER);
    CHECK(C.process_attestation == LPRES_STATE_TRUE);

    /* all TRUE: result >= 0 (0 is success), coverage at the minimum */
    B.global_attestation = LPRES_STATE_TRUE;
    C.coverage_ratio = B.min_global_coverage;
    C.global_attestation = LPRES_STATE_NEITHER;
    CHECK(sb_attest(&B, &C, 7, NULL, 0) == LPRES_STATE_TRUE);
    CHECK(C.process_attestation == LPRES_STATE_TRUE);
    CHECK(C.global_attestation == LPRES_STATE_TRUE);
    CHECK(B.global_attestation == LPRES_STATE_TRUE);

    /* negative result -> FALSE everywhere */
    CHECK(sb_attest(&B, &C, 7, NULL, -1) == LPRES_STATE_FALSE);
    CHECK(C.process_attestation == LPRES_STATE_FALSE);
    CHECK(C.global_attestation == LPRES_STATE_FALSE);
    CHECK(B.global_attestation == LPRES_STATE_FALSE);

    /* coverage just under the minimum -> FALSE */
    B.global_attestation = LPRES_STATE_TRUE;
    C.process_attestation = LPRES_STATE_TRUE;
    C.coverage_ratio = B.min_global_coverage - 0.001;
    CHECK(sb_attest(&B, &C, 7, NULL, 5) == LPRES_STATE_FALSE);

    /* context BOTH, rest TRUE -> BOTH; bridge absorbs it */
    B.global_attestation = LPRES_STATE_TRUE;
    C.process_attestation = LPRES_STATE_BOTH;
    C.coverage_ratio = 5.0;
    CHECK(sb_attest(&B, &C, 7, NULL, 1) == LPRES_STATE_BOTH);
    CHECK(C.global_attestation == LPRES_STATE_BOTH);
    CHECK(B.global_attestation == LPRES_STATE_BOTH);

    /* bridge NEITHER (fresh), context TRUE -> NEITHER */
    fresh_bridge();
    C.process_attestation = LPRES_STATE_TRUE;
    C.coverage_ratio = 5.0;
    CHECK(sb_attest(&B, &C, 7, NULL, 1) == LPRES_STATE_NEITHER);
    CHECK(C.process_attestation == LPRES_STATE_NEITHER);
    CHECK(B.global_attestation == LPRES_STATE_NEITHER);

    /* bridge BOTH, context TRUE: combined BOTH */
    B.global_attestation = LPRES_STATE_BOTH;
    C.process_attestation = LPRES_STATE_TRUE;
    CHECK(sb_attest(&B, &C, 7, NULL, 1) == LPRES_STATE_BOTH);
}

static void test_grant(void)
{
    fresh_bridge();
    reset_cb();
    fresh_ctx(LPRES_STATE_TRUE, false);
    sb_result_t r = sb_grant_capability(NULL, &C, SB_CAP_AI_INFERENCE);
    CHECK(r.code == -1 && r.attestation == LPRES_STATE_FALSE);
    r = sb_grant_capability(&B, NULL, SB_CAP_AI_INFERENCE);
    CHECK(r.code == -1 && r.attestation == LPRES_STATE_FALSE);

    sb_register_callback(&C, 3, (void *) on_grant);
    r = sb_grant_capability(&B, &C, SB_CAP_AI_INFERENCE);
    CHECK(r.code == 0 && r.attestation == LPRES_STATE_TRUE);
    CHECK(C.num_capabilities == 1 && C.capabilities[0] == SB_CAP_AI_INFERENCE);
    CHECK(sb_has_capability(&C, SB_CAP_AI_INFERENCE));
    CHECK(!sb_has_capability(&C, SB_CAP_TELECOM_GLARE_DETECT));
    CHECK(!sb_has_capability(&C, SB_CAP_AI_TRAINING));
    CHECK(n_grant == 1 && last_ctx == &C && last_cap == SB_CAP_AI_INFERENCE &&
          last_state == LPRES_STATE_TRUE);
    /* a second grant is a no-op success with no callback */
    r = sb_grant_capability(&B, &C, SB_CAP_AI_INFERENCE);
    CHECK(r.code == 0 && r.attestation == LPRES_STATE_TRUE);
    CHECK(C.num_capabilities == 1 && n_grant == 1);
    /* even an already-held cap is refused if it is unknown... and a held
     * cap is still reported held when the context has fallen below */
    C.coverage_ratio = 0.0;
    r = sb_grant_capability(&B, &C, SB_CAP_AI_INFERENCE);
    CHECK(r.code == 0 && C.num_capabilities == 1);

    /* HSM sandbox */
    fresh_ctx(LPRES_STATE_TRUE, false);
    CHECK(desc(SB_CAP_AI_TRAINING)->requires_hsm && !desc(SB_CAP_AI_INFERENCE)->requires_hsm);
    r = sb_grant_capability(&B, &C, SB_CAP_AI_TRAINING);
    CHECK(r.code == -1 && r.attestation == LPRES_STATE_FALSE && C.num_capabilities == 0);
    CHECK(sb_grant_capability(&B, &C, SB_CAP_QUANTUM_OPERATION).code == -1);
    fresh_ctx(LPRES_STATE_TRUE, true);
    CHECK(sb_grant_capability(&B, &C, SB_CAP_AI_TRAINING).code == 0);
    CHECK(sb_has_capability(&C, SB_CAP_AI_TRAINING));

    /* each capability against its own minimum coverage */
    int cov = 1;
    for (uint32_t i = 0; i < B.num_capabilities; i++) {
        const sb_capability_desc_t *d = &B.capabilities[i];
        fresh_ctx(LPRES_STATE_TRUE, d->requires_hsm);
        C.coverage_ratio = d->min_coverage - 0.01;
        sb_result_t rr = sb_grant_capability(&B, &C, d->id);
        cov &= rr.code == -1 && rr.attestation == LPRES_STATE_FALSE && C.num_capabilities == 0;
        C.coverage_ratio = d->min_coverage;
        cov &= sb_grant_capability(&B, &C, d->id).code == 0 && sb_has_capability(&C, d->id) &&
               C.num_capabilities == 1;
    }
    CHECK(cov);

    /* attestation: only TRUE passes a TRUE requirement */
    const lpres_state_t bad[3] = {LPRES_STATE_FALSE, LPRES_STATE_BOTH, LPRES_STATE_NEITHER};
    for (int k = 0; k < 3; k++) {
        fresh_ctx(bad[k], false);
        r = sb_grant_capability(&B, &C, SB_CAP_AI_INFERENCE);
        CHECK(r.code == -1 && r.attestation == LPRES_STATE_FALSE && C.num_capabilities == 0);
    }
    /* a descriptor asking for exactly BOTH accepts a BOTH process */
    desc(SB_CAP_NETWORK_MESH)->min_attestation = LPRES_STATE_BOTH;
    fresh_ctx(LPRES_STATE_BOTH, false);
    CHECK(sb_grant_capability(&B, &C, SB_CAP_NETWORK_MESH).code == 0);
    fresh_ctx(LPRES_STATE_FALSE, false);
    CHECK(sb_grant_capability(&B, &C, SB_CAP_NETWORK_MESH).code == -1);
    fresh_ctx(LPRES_STATE_TRUE, false);
    CHECK(sb_grant_capability(&B, &C, SB_CAP_NETWORK_MESH).code == 0);
    desc(SB_CAP_NETWORK_MESH)->min_attestation = LPRES_STATE_TRUE;

    /* unknown and unregistered ids */
    fresh_ctx(LPRES_STATE_TRUE, true);
    CHECK(sb_grant_capability(&B, &C, (sb_capability_t) 0).code == -1);
    CHECK(sb_grant_capability(&B, &C, SB_CAP_MAX).code == -1);
    CHECK(sb_grant_capability(&B, &C, (sb_capability_t) 99).code == -1);
    CHECK(C.num_capabilities == 0);
    /* first and last valid ids are found */
    CHECK(sb_grant_capability(&B, &C, (sb_capability_t) 1).code == 0);
    CHECK(sb_grant_capability(&B, &C, (sb_capability_t) (SB_CAP_MAX - 1)).code == 0);
    CHECK(C.num_capabilities == 2);
    /* the registry is only searched up to num_capabilities */
    B.num_capabilities = 4; /* capital, derivatives, assurance, treaty */
    fresh_ctx(LPRES_STATE_TRUE, true);
    CHECK(sb_grant_capability(&B, &C, SB_CAP_TREATY_TOKENIZE).code == 0);
    CHECK(sb_grant_capability(&B, &C, SB_CAP_NETWORK_MESH).code == -1);
    CHECK(C.num_capabilities == 1);
    B.num_capabilities = 0;
    CHECK(sb_grant_capability(&B, &C, SB_CAP_CAPITAL_TRANSFER).code == -1);
    /* ids 0 and SB_CAP_MAX are refused even if a descriptor claims them */
    fresh_bridge();
    B.capabilities[B.num_capabilities] = B.capabilities[0];
    B.capabilities[B.num_capabilities++].id = (sb_capability_t) 0;
    B.capabilities[B.num_capabilities] = B.capabilities[0];
    B.capabilities[B.num_capabilities++].id = SB_CAP_MAX;
    fresh_ctx(LPRES_STATE_TRUE, true);
    CHECK(sb_grant_capability(&B, &C, (sb_capability_t) 0).code == -1);
    CHECK(sb_grant_capability(&B, &C, SB_CAP_MAX).code == -1);
    CHECK(C.num_capabilities == 0);
    C.capabilities[0] = (sb_capability_t) 0;
    C.capabilities[1] = SB_CAP_MAX;
    for (int k = 0; k < 2; k++) {
        C.num_capabilities = 1;
        C.capabilities[0] = k ? SB_CAP_MAX : (sb_capability_t) 0;
        C.m5 = (m5_coords_t){.omega = 3, .r = 1.0, .ell = 1.0, .phi = 1.0, .chi = 1};
        CHECK(sb_self_audit_context(&B, &C).code == -1);
    }

    /* a full context: success reported, callback fired, nothing stored */
    fresh_bridge();
    reset_cb();
    fresh_ctx(LPRES_STATE_TRUE, true);
    sb_register_callback(&C, 3, (void *) on_grant);
    for (uint32_t i = 0; i < SB_MAX_CAPABILITIES; i++) C.capabilities[i] = SB_CAP_MEDIA_STREAM;
    C.num_capabilities = SB_MAX_CAPABILITIES - 1;
    CHECK(sb_grant_capability(&B, &C, SB_CAP_CRYPTO_KEM).code == 0);
    CHECK(C.num_capabilities == SB_MAX_CAPABILITIES);
    CHECK(C.capabilities[SB_MAX_CAPABILITIES - 1] == SB_CAP_CRYPTO_KEM);
    r = sb_grant_capability(&B, &C, SB_CAP_CRYPTO_SIGN);
    CHECK(r.code == 0 && r.attestation == LPRES_STATE_TRUE);
    CHECK(C.num_capabilities == SB_MAX_CAPABILITIES);
    CHECK(!sb_has_capability(&C, SB_CAP_CRYPTO_SIGN));
    CHECK(n_grant == 2 && last_cap == SB_CAP_CRYPTO_SIGN);
}

static void test_revoke(void)
{
    fresh_bridge();
    reset_cb();
    fresh_ctx(LPRES_STATE_TRUE, false);
    sb_result_t r = sb_revoke_capability(NULL, &C, SB_CAP_CAPITAL_TRANSFER);
    CHECK(r.code == -1 && r.attestation == LPRES_STATE_FALSE);
    r = sb_revoke_capability(&B, NULL, SB_CAP_CAPITAL_TRANSFER);
    CHECK(r.code == -1 && r.attestation == LPRES_STATE_FALSE);

    C.capabilities[0] = SB_CAP_CAPITAL_TRANSFER;
    C.capabilities[1] = SB_CAP_NETWORK_MESH;
    C.capabilities[2] = SB_CAP_MEDIA_STREAM;
    C.capabilities[3] = SB_CAP_AI_INFERENCE;
    C.num_capabilities = 4;
    /* no callback registered yet: revoke from the middle */
    r = sb_revoke_capability(&B, &C, SB_CAP_NETWORK_MESH);
    CHECK(r.code == 0 && r.attestation == LPRES_STATE_TRUE);
    CHECK(C.num_capabilities == 3);
    CHECK(C.capabilities[0] == SB_CAP_CAPITAL_TRANSFER && C.capabilities[1] == SB_CAP_MEDIA_STREAM &&
          C.capabilities[2] == SB_CAP_AI_INFERENCE);
    CHECK(!sb_has_capability(&C, SB_CAP_NETWORK_MESH));
    CHECK(n_revoke == 0);

    sb_register_callback(&C, 4, (void *) on_revoke);
    /* a cap not held: nothing removed, callback still fires */
    r = sb_revoke_capability(&B, &C, SB_CAP_QUANTUM_OPERATION);
    CHECK(r.code == 0 && r.attestation == LPRES_STATE_TRUE && C.num_capabilities == 3);
    CHECK(n_revoke == 1 && last_ctx == &C && last_cap == SB_CAP_QUANTUM_OPERATION &&
          last_state == LPRES_STATE_FALSE);
    /* the last element */
    r = sb_revoke_capability(&B, &C, SB_CAP_AI_INFERENCE);
    CHECK(C.num_capabilities == 2 && C.capabilities[1] == SB_CAP_MEDIA_STREAM);
    CHECK(C.capabilities[2] == SB_CAP_AI_INFERENCE); /* stale slot left in place */
    /* the first element; only one copy is removed */
    C.capabilities[2] = SB_CAP_CAPITAL_TRANSFER;
    C.num_capabilities = 3;
    r = sb_revoke_capability(&B, &C, SB_CAP_CAPITAL_TRANSFER);
    CHECK(C.num_capabilities == 2);
    CHECK(C.capabilities[0] == SB_CAP_MEDIA_STREAM && C.capabilities[1] == SB_CAP_CAPITAL_TRANSFER);
    CHECK(sb_has_capability(&C, SB_CAP_CAPITAL_TRANSFER));
    CHECK(n_revoke == 3);
    /* empty context */
    C.num_capabilities = 0;
    r = sb_revoke_capability(&B, &C, SB_CAP_MEDIA_STREAM);
    CHECK(r.code == 0 && C.num_capabilities == 0);

    CHECK(!sb_has_capability(NULL, SB_CAP_MEDIA_STREAM));
    CHECK(!sb_has_capability(&C, SB_CAP_MEDIA_STREAM));
    /* has only scans num_capabilities */
    C.capabilities[0] = SB_CAP_HSM_OPERATION;
    C.capabilities[1] = SB_CAP_CRYPTO_KEM;
    C.num_capabilities = 1;
    CHECK(sb_has_capability(&C, SB_CAP_HSM_OPERATION));
    CHECK(!sb_has_capability(&C, SB_CAP_CRYPTO_KEM));
}

static void test_audit_heal(void)
{
    fresh_bridge();
    reset_cb();
    fresh_ctx(LPRES_STATE_NEITHER, false);
    sb_result_t r = sb_self_audit_context(NULL, &C);
    CHECK(r.code == -1 && r.attestation == LPRES_STATE_FALSE);
    r = sb_self_audit_context(&B, NULL);
    CHECK(r.code == -1 && r.attestation == LPRES_STATE_FALSE);
    B.initialized = false;
    r = sb_self_audit_context(&B, &C);
    CHECK(r.code == -1 && r.attestation == LPRES_STATE_FALSE);
    CHECK(C.api_calls == 0 && B.stats.total_audits == 0);
    B.initialized = true;

    /* coverage = omega*r*ell / (phi*chi) = 6*1.5*2 / (2*3) = 3.0 */
    set_m5(6, 1.5, 2.0, 2.0, 3);
    C.coverage_ratio = 0.0;
    r = sb_self_audit_context(&B, &C);
    CHECK(C.coverage_ratio == 3.0);
    CHECK(r.code == 0 && r.attestation == LPRES_STATE_TRUE);
    CHECK(C.process_attestation == LPRES_STATE_TRUE);
    CHECK(C.api_calls == 1 && C.audit_passes == 1 && C.audit_failures == 0);
    CHECK(B.stats.total_audits == 1);

    /* a zero denominator yields 100 */
    set_m5(1, 1.0, 1.0, 0.0, 3);
    sb_update_context_coverage(&B, &C);
    CHECK(C.coverage_ratio == 100.0);
    set_m5(1, 1.0, 1.0, 2.0, 0);
    sb_update_context_coverage(&B, &C);
    CHECK(C.coverage_ratio == 100.0);
    /* update: exact formula, NULL-safe */
    set_m5(10, 3.0, 0.5, 4.0, 5); /* 15/20 = 0.75 */
    sb_update_context_coverage(&B, &C);
    CHECK(C.coverage_ratio == 0.75);
    C.coverage_ratio = 9.0;
    sb_update_context_coverage(NULL, &C);
    CHECK(C.coverage_ratio == 9.0);
    sb_update_context_coverage(&B, NULL);

    /* exactly at the global minimum (18/10 = 1.8) passes */
    set_m5(18, 1.0, 1.0, 10.0, 1);
    r = sb_self_audit_context(&B, &C);
    CHECK(C.coverage_ratio == 1.8);
    CHECK(r.code == 0 && r.attestation == LPRES_STATE_TRUE);

    /* below the minimum fails: BOTH, callback, counters (first without a
     * callback registered) */
    set_m5(10, 3.0, 0.5, 4.0, 5);
    r = sb_self_audit_context(&B, &C);
    CHECK(r.code == -1 && r.attestation == LPRES_STATE_BOTH && n_fail == 0);
    sb_register_callback(&C, 0, (void *) on_fail);
    r = sb_self_audit_context(&B, &C);
    CHECK(r.code == -1 && r.attestation == LPRES_STATE_BOTH);
    CHECK(C.process_attestation == LPRES_STATE_BOTH);
    CHECK(C.api_calls == 4 && C.audit_passes == 2 && C.audit_failures == 2);
    CHECK(B.stats.total_audits == 4);
    CHECK(n_fail == 1 && last_ctx == &C && last_op == 0xFFFFFFFFu &&
          last_state == LPRES_STATE_BOTH);

    /* a held capability above the context's coverage fails the audit */
    set_m5(6, 1.5, 2.0, 2.0, 3); /* 3.0 */
    C.capabilities[0] = SB_CAP_CAPITAL_TRANSFER; /* 1.8 */
    C.capabilities[1] = SB_CAP_TREATY_TOKENIZE;  /* 2.5 */
    C.num_capabilities = 2;
    r = sb_self_audit_context(&B, &C);
    CHECK(r.code == 0 && r.attestation == LPRES_STATE_TRUE);
    set_m5(12, 1.0, 1.0, 5.0, 1); /* 2.4 */
    r = sb_self_audit_context(&B, &C);
    CHECK(C.coverage_ratio == 2.4);
    CHECK(r.code == -1 && r.attestation == LPRES_STATE_BOTH && n_fail == 2);
    /* same coverage, only the 1.8 cap held -> passes */
    C.num_capabilities = 1;
    r = sb_self_audit_context(&B, &C);
    CHECK(r.code == 0 && n_fail == 2);
    /* the failing cap first, then a passing one: still fails */
    C.capabilities[0] = SB_CAP_TREATY_TOKENIZE;
    C.capabilities[1] = SB_CAP_CAPITAL_TRANSFER;
    C.num_capabilities = 2;
    CHECK(sb_self_audit_context(&B, &C).code == -1);
    /* exactly at the cap's own minimum (25/10 = 2.5) passes */
    set_m5(25, 1.0, 1.0, 10.0, 1);
    CHECK(sb_self_audit_context(&B, &C).code == 0);
    /* an unregistered id held by the context fails */
    C.capabilities[0] = (sb_capability_t) 99;
    C.num_capabilities = 1;
    CHECK(sb_self_audit_context(&B, &C).code == -1);
    CHECK(B.stats.total_audits == 10);

    /* self-heal */
    reset_cb();
    r = sb_self_heal_context(NULL, &C);
    CHECK(r.code == -1 && r.attestation == LPRES_STATE_FALSE);
    r = sb_self_heal_context(&B, NULL);
    CHECK(r.code == -1 && r.attestation == LPRES_STATE_FALSE);
    fresh_ctx(LPRES_STATE_BOTH, false);
    C.global_attestation = LPRES_STATE_NEITHER;
    r = sb_self_heal_context(&B, &C); /* no callback registered */
    CHECK(r.code == 0 && r.attestation == LPRES_STATE_TRUE);
    CHECK(C.process_attestation == LPRES_STATE_TRUE && C.global_attestation == LPRES_STATE_TRUE);
    CHECK(C.healings == 1 && B.stats.total_healings == 1 && n_heal == 0);
    sb_register_callback(&C, 1, (void *) on_heal);
    C.process_attestation = LPRES_STATE_FALSE;
    C.global_attestation = LPRES_STATE_FALSE;
    r = sb_self_heal_context(&B, &C);
    CHECK(r.code == 0 && r.attestation == LPRES_STATE_TRUE);
    CHECK(C.process_attestation == LPRES_STATE_TRUE && C.global_attestation == LPRES_STATE_TRUE);
    CHECK(C.healings == 2 && B.stats.total_healings == 2);
    CHECK(n_heal == 1 && last_ctx == &C && last_op == 0xFFFFFFFFu &&
          last_state == LPRES_STATE_TRUE);
    /* TRUE and NEITHER are not healed */
    C.process_attestation = LPRES_STATE_TRUE;
    C.global_attestation = LPRES_STATE_BOTH;
    r = sb_self_heal_context(&B, &C);
    CHECK(r.code == 0 && r.attestation == LPRES_STATE_TRUE);
    CHECK(C.global_attestation == LPRES_STATE_BOTH && C.healings == 2 && n_heal == 1);
    C.process_attestation = LPRES_STATE_NEITHER;
    r = sb_self_heal_context(&B, &C);
    CHECK(r.code == 0 && C.process_attestation == LPRES_STATE_NEITHER);
    CHECK(C.healings == 2 && B.stats.total_healings == 2 && n_heal == 1);
}

static void test_enforce_stats_cb(void)
{
    fresh_bridge();
    fresh_ctx(LPRES_STATE_TRUE, false);
    C.coverage_ratio = 2.0;
    CHECK(!sb_enforce_coverage(NULL, &C, 1.0));
    CHECK(!sb_enforce_coverage(&B, NULL, 1.0));
    CHECK(sb_enforce_coverage(&B, &C, 1.9));
    CHECK(sb_enforce_coverage(&B, &C, 2.0));
    CHECK(!sb_enforce_coverage(&B, &C, 2.0001));

    B.stats.total_api_calls = 11;
    B.stats.total_audits = 22;
    B.stats.total_healings = 33;
    B.stats.total_translations = 44;
    B.stats.total_capability_grants = 55;
    B.stats.total_capability_revokes = 66;
    struct {
        uint64_t a, b, c, d, e, f;
    } st;
    memset(&st, 0xEE, sizeof st);
    _Static_assert(sizeof st == sizeof B.stats, "stats layout");
    sb_get_stats(&B, &st);
    CHECK(st.a == 11 && st.b == 22 && st.c == 33 && st.d == 44 && st.e == 55 && st.f == 66);
    memset(&st, 0xEE, sizeof st);
    sb_get_stats(NULL, &st);
    CHECK(st.a == 0xEEEEEEEEEEEEEEEEull);
    sb_get_stats(&B, NULL);

    /* callback registration by type */
    memset(&C, 0, sizeof C);
    sb_register_callback(NULL, 0, (void *) on_fail);
    sb_register_callback(&C, 0, NULL);
    CHECK(C.callbacks.on_audit_fail == NULL);
    sb_register_callback(&C, 0, (void *) on_fail);
    sb_register_callback(&C, 1, (void *) on_heal);
    sb_register_callback(&C, 2, (void *) on_drop);
    sb_register_callback(&C, 3, (void *) on_grant);
    sb_register_callback(&C, 4, (void *) on_revoke);
    CHECK(C.callbacks.on_audit_fail == on_fail);
    CHECK(C.callbacks.on_heal == on_heal);
    CHECK(C.callbacks.on_coverage_drop == on_drop);
    CHECK(C.callbacks.on_capability_grant == on_grant);
    CHECK(C.callbacks.on_capability_revoke == on_revoke);
    sb_register_callback(&C, 5, (void *) on_heal);
    sb_register_callback(&C, 0xFFFFFFFFu, (void *) on_heal);
    CHECK(C.callbacks.on_audit_fail == on_fail && C.callbacks.on_capability_revoke == on_revoke &&
          C.callbacks.on_capability_grant == on_grant);
}

static void test_names(void)
{
    static const char *const names[] = {
        "UNKNOWN",          "capital.transfer",    "derivatives.trade", "assurance.create",
        "treaty.tokenize",  "network.mesh",        "storage.persist",   "media.stream",
        "governance.vote",  "identity.credential", "compute.schedule",  "crypto.sign",
        "crypto.encrypt",   "crypto.zk_prove",     "crypto.kem",        "hsm.operation",
        "telecom.signal",   "telecom.glare_detect", "ai.inference",     "ai.training",
        "quantum.operation", "holographic.render", "tripartite.file",   "civilizational.app"};
    for (uint32_t i = 0; i < SB_CAP_MAX; i++)
        CHECK(!strcmp(sb_capability_name((sb_capability_t) i), names[i]));
    CHECK(!strcmp(sb_capability_name(SB_CAP_MAX), "UNKNOWN"));
    CHECK(!strcmp(sb_capability_name((sb_capability_t) 1000), "UNKNOWN"));

    static const char *const codes[] = {"OK",          "ERR_CAPABILITY", "ERR_COVERAGE",
                                        "ERR_ATTESTATION", "ERR_HSM",    "ERR_ZK",
                                        "ERR_BALANCE", "ERR_POLICY",     "ERR_NOT_FOUND",
                                        "ERR_BUSY"};
    for (int i = 0; i < 10; i++) CHECK(!strcmp(sb_result_code_name(-i), codes[i]));
    CHECK(!strcmp(sb_result_code_name(1), "UNKNOWN"));
    CHECK(!strcmp(sb_result_code_name(-10), "UNKNOWN"));
    CHECK(!strcmp(sb_result_code_name(INT32_MIN), "UNKNOWN"));
    CHECK(!strcmp(sb_result_code_name(INT32_MAX), "UNKNOWN"));

    CHECK(!strcmp(sb_lpres_state_name(LPRES_STATE_NEITHER), "neither"));
    CHECK(!strcmp(sb_lpres_state_name(LPRES_STATE_TRUE), "true"));
    CHECK(!strcmp(sb_lpres_state_name(LPRES_STATE_FALSE), "false"));
    CHECK(!strcmp(sb_lpres_state_name(LPRES_STATE_BOTH), "both"));
}

int main(void)
{
    test_init();
    test_context();
    test_attest();
    test_grant();
    test_revoke();
    test_audit_heal();
    test_enforce_stats_cb();
    test_names();
    if (fails) {
        printf("%d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("%d checks passed\n", checks);
    return 0;
}
