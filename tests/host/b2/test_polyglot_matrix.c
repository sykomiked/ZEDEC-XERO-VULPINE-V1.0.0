/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_polyglot_matrix.c: unit tests for kernel/src/sdk_bridge/polyglot_matrix.c.
 * The orbital_compat registry and lowering are replaced by fakes (a
 * registration bitmask and a scripted oc_lower), so the matrix is tested on
 * its own. Covers the tier table and names, pm_init's language discovery,
 * explicit registration bounds, route creation (fields, ids, capacity),
 * pm_send (route lookup, policy veto on invalid rationals, message fields,
 * counters, capacity), pm_deliver (attestation, skipping, compaction),
 * pm_pipeline (lowering errors, cross-tier policy, route/send failures),
 * pm_self_audit (coverage boundary, inactive routes and languages) and
 * pm_get_stats. */
#include <stdio.h>
#include <string.h>
#include "polyglot_matrix.h"

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

/* ---- orbital_compat fakes ------------------------------------------------- */
static uint32_t reg_mask; /* bit n: language n is registered */
static int lower_calls;
static oc_lang_t lower_lang;
static const void *lower_src;
static uint32_t lower_len;
static int32_t lower_rc;
static oc_ir_t lower_ir;

bool oc_lang_registered(oc_lang_t lang)
{
    return (uint32_t) lang < 32 && (reg_mask >> (uint32_t) lang & 1u);
}

int32_t oc_lower(oc_lang_t from, const void *src, uint32_t len, oc_ir_t *out)
{
    lower_calls++;
    lower_lang = from;
    lower_src = src;
    lower_len = len;
    if (lower_rc == OC_OK) *out = lower_ir;
    return lower_rc;
}

static polyglot_matrix_t M;

#define ALL15 0x7FFFu

static rat_t rat(int64_t n, int64_t d)
{
    rat_t r = {n, d, true};
    return r;
}

static void ir2(oc_ir_t *ir, int64_t a, int64_t b)
{
    memset(ir, 0, sizeof *ir);
    ir->num_fields = 2;
    ir->fields[0].type = OC_TYPE_RATIONAL;
    ir->fields[0].num = rat(a, 1);
    ir->fields[1].type = OC_TYPE_RATIONAL;
    ir->fields[1].num = rat(b, 3);
}

static bool all_zero(const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++)
        if (b[i]) return false;
    return true;
}

static void fresh(uint32_t mask)
{
    reg_mask = mask;
    memset(&M, 0xA5, sizeof M);
    pm_init(&M);
}

static void test_tiers(void)
{
    static const pm_tier_t want[15] = {
        PM_TIER_LEGACY,  PM_TIER_LEGACY,  PM_TIER_CORE,    PM_TIER_LOGIC,   PM_TIER_CORE,
        PM_TIER_SAFETY,  PM_TIER_SAFETY,  PM_TIER_SCRIPT,  PM_TIER_SCRIPT,  PM_TIER_TELECOM,
        PM_TIER_TELECOM, PM_TIER_TELECOM, PM_TIER_TELECOM, PM_TIER_TELECOM, PM_TIER_TELECOM};
    for (int i = 0; i < 15; i++) CHECK(pm_lang_tier((oc_lang_t) i) == want[i]);
    CHECK(pm_lang_tier(OC_LANG_MAX) == PM_TIER_CORE);
    CHECK(pm_lang_tier((oc_lang_t) 200) == PM_TIER_CORE);

    CHECK(!strcmp(pm_tier_name(PM_TIER_CORE), "membrane"));
    CHECK(!strcmp(pm_tier_name(PM_TIER_SAFETY), "membrane-safety"));
    CHECK(!strcmp(pm_tier_name(PM_TIER_LEGACY), "organs"));
    CHECK(!strcmp(pm_tier_name(PM_TIER_LOGIC), "nervous-system"));
    CHECK(!strcmp(pm_tier_name(PM_TIER_SCRIPT), "interface"));
    CHECK(!strcmp(pm_tier_name(PM_TIER_TELECOM), "membrane-signaling"));
    CHECK(!strcmp(pm_tier_name((pm_tier_t) 6), "?"));
    CHECK(!strcmp(pm_tier_name((pm_tier_t) 99), "?"));
}

static void test_init_register(void)
{
    pm_init(NULL);
    /* COBOL, C, Sutra, Rust, WASM, Telecom registered; 15 too (out of range) */
    uint32_t mask = 1u << OC_LANG_COBOL | 1u << OC_LANG_C | 1u << OC_LANG_SUTRA |
                    1u << OC_LANG_RUST | 1u << OC_LANG_WASM | 1u << OC_LANG_TELECOM | 1u << 15;
    fresh(mask);
    CHECK(M.initialized);
    CHECK(M.min_coverage == 1.8);
    CHECK(M.matrix_attestation == LPRES_STATE_NEITHER);
    CHECK(M.phase_tick == 0 && M.next_route_id == 1 && M.next_message_id == 1);
    CHECK(M.num_routes == 0 && M.num_pending == 0);
    CHECK(all_zero(&M.stats, sizeof M.stats));
    CHECK(all_zero(M.routes, sizeof M.routes));
    CHECK(all_zero(M.pending, sizeof M.pending));
    for (int i = 0; i < PM_MAX_LANG_SLOTS; i++) {
        bool on = (mask >> i) & 1u;
        CHECK(M.languages[i].bound == on && M.languages[i].active == on);
        CHECK(M.languages[i].messages_sent == 0 && M.languages[i].messages_received == 0);
        if (on)
            CHECK(M.languages[i].lang == (oc_lang_t) i &&
                  M.languages[i].tier == pm_lang_tier((oc_lang_t) i));
        else
            CHECK(M.languages[i].lang == 0 && M.languages[i].tier == 0);
    }

    /* slot 0 unregistered: every byte of it is zeroed */
    fresh(ALL15 & ~1u);
    CHECK(all_zero(&M.languages[0], sizeof M.languages[0]));
    CHECK(M.languages[1].bound);
    fresh(mask);

    /* explicit registration */
    CHECK(pm_register_language(NULL, OC_LANG_C) == -1);
    M.initialized = false;
    CHECK(pm_register_language(&M, OC_LANG_C) == -1);
    M.initialized = true;
    CHECK(pm_register_language(&M, (oc_lang_t) 15) == -1); /* past the slots */
    CHECK(all_zero(M.routes, sizeof M.routes));
    CHECK(pm_register_language(&M, OC_LANG_SS7) == -1); /* not registered */
    CHECK(!M.languages[OC_LANG_SS7].bound);
    reg_mask |= 1u << OC_LANG_SS7 | 1u << OC_LANG_ZIG;
    CHECK(pm_register_language(&M, OC_LANG_SS7) == 0);
    CHECK(M.languages[OC_LANG_SS7].lang == OC_LANG_SS7 &&
          M.languages[OC_LANG_SS7].tier == PM_TIER_TELECOM && M.languages[OC_LANG_SS7].bound &&
          M.languages[OC_LANG_SS7].active);
    CHECK(pm_register_language(&M, OC_LANG_ZIG) == 0);
    CHECK(M.languages[OC_LANG_ZIG].lang == OC_LANG_ZIG &&
          M.languages[OC_LANG_ZIG].tier == PM_TIER_SAFETY);
    /* re-activates a slot */
    M.languages[OC_LANG_C].active = false;
    CHECK(pm_register_language(&M, OC_LANG_C) == 0 && M.languages[OC_LANG_C].active);
    /* the last slot */
    CHECK(pm_register_language(&M, OC_LANG_TELECOM) == 0);
    CHECK(M.languages[OC_LANG_TELECOM].tier == PM_TIER_TELECOM);
}

static void test_routes(void)
{
    fresh(ALL15 & ~(1u << OC_LANG_PULSE));
    CHECK(pm_create_route(NULL, OC_LANG_C, OC_LANG_COBOL, false) == -1);
    M.initialized = false;
    CHECK(pm_create_route(&M, OC_LANG_C, OC_LANG_COBOL, false) == -1);
    M.initialized = true;
    CHECK(pm_create_route(&M, OC_LANG_PULSE, OC_LANG_COBOL, false) == -1);
    CHECK(pm_create_route(&M, OC_LANG_COBOL, OC_LANG_PULSE, false) == -1);
    CHECK(M.num_routes == 0 && M.next_route_id == 1);

    memset(&M.routes[0], 0x5C, sizeof M.routes[0]);
    CHECK(pm_create_route(&M, OC_LANG_FORTRAN, OC_LANG_SUTRA, true) == 1);
    pm_route_t *r = &M.routes[0];
    CHECK(r->route_id == 1 && r->source_lang == OC_LANG_FORTRAN && r->target_lang == OC_LANG_SUTRA);
    CHECK(r->source_tier == PM_TIER_LEGACY && r->target_tier == PM_TIER_LOGIC);
    CHECK(r->route_attestation == LPRES_STATE_NEITHER && r->coverage_ratio == 2.0);
    CHECK(r->messages_passed == 0 && r->messages_failed == 0);
    CHECK(r->requires_policy_check && r->active);
    CHECK(M.num_routes == 1 && M.next_route_id == 2);
    CHECK(pm_create_route(&M, OC_LANG_PYTHON, OC_LANG_PYTHON, false) == 2);
    CHECK(!M.routes[1].requires_policy_check && M.routes[1].source_tier == PM_TIER_SCRIPT);
    CHECK(M.num_routes == 2);

    /* capacity: exactly PM_MAX_ROUTES */
    for (uint32_t i = 2; i < PM_MAX_ROUTES; i++)
        CHECK(pm_create_route(&M, OC_LANG_C, OC_LANG_RUST, false) == (int32_t) i + 1);
    CHECK(M.num_routes == PM_MAX_ROUTES);
    CHECK(pm_create_route(&M, OC_LANG_C, OC_LANG_RUST, false) == -1);
    CHECK(M.num_routes == PM_MAX_ROUTES && M.next_route_id == PM_MAX_ROUTES + 1);
}

static void test_send_deliver(void)
{
    fresh(ALL15);
    oc_ir_t p;
    ir2(&p, 7, 2);
    CHECK(pm_send(&M, 1, &p) == -1); /* no routes */
    int32_t r1 = pm_create_route(&M, OC_LANG_COBOL, OC_LANG_C, false);
    int32_t r2 = pm_create_route(&M, OC_LANG_FORTRAN, OC_LANG_SUTRA, true);
    CHECK(r1 == 1 && r2 == 2);
    CHECK(pm_send(NULL, 1, &p) == -1);
    CHECK(pm_send(&M, 1, NULL) == -1);
    M.initialized = false;
    CHECK(pm_send(&M, 1, &p) == -1);
    M.initialized = true;
    CHECK(pm_send(&M, 3, &p) == -1);
    CHECK(pm_send(&M, 0, &p) == -1);
    /* a stale slot past num_routes is not searched */
    M.routes[2].route_id = 99;
    M.routes[2].active = true;
    CHECK(pm_send(&M, 99, &p) == -1);
    memset(&M.routes[2], 0, sizeof M.routes[2]);
    CHECK(M.num_pending == 0 && M.stats.total_messages == 0);

    /* first message, on route 2 (found past route 1) */
    M.phase_tick = 41;
    memset(&M.pending[0], 0x3E, sizeof M.pending[0]);
    CHECK(pm_send(&M, 2, &p) == 1);
    pm_message_t *m = &M.pending[0];
    CHECK(m->message_id == 1 && m->route_id == 2);
    CHECK(m->source_lang == OC_LANG_FORTRAN && m->target_lang == OC_LANG_SUTRA);
    CHECK(memcmp(&m->payload, &p, sizeof p) == 0);
    CHECK(m->phase_tick == 41 && m->delivery_attestation == LPRES_STATE_NEITHER);
    CHECK(!m->completed && !m->failed);
    CHECK(all_zero(&m->result, sizeof m->result));
    CHECK(M.num_pending == 1 && M.next_message_id == 2);
    CHECK(M.stats.total_messages == 1 && M.stats.total_policy_checks == 1 &&
          M.stats.total_policy_vetoes == 0);
    CHECK(M.routes[1].messages_passed == 1 && M.routes[0].messages_passed == 0);
    CHECK(M.languages[OC_LANG_FORTRAN].messages_sent == 1 &&
          M.languages[OC_LANG_SUTRA].messages_received == 1);
    CHECK(M.languages[OC_LANG_FORTRAN].messages_received == 0 &&
          M.languages[OC_LANG_SUTRA].messages_sent == 0);

    /* policy veto: invalid rational, or a zero denominator */
    oc_ir_t bad = p;
    bad.fields[1].num.valid = false;
    CHECK(pm_send(&M, 2, &bad) == -1);
    bad = p;
    bad.fields[0].num.den = 0;
    CHECK(pm_send(&M, 2, &bad) == -1);
    CHECK(M.stats.total_policy_checks == 3 && M.stats.total_policy_vetoes == 2);
    CHECK(M.routes[1].messages_failed == 2 && M.routes[1].messages_passed == 1);
    CHECK(M.num_pending == 1 && M.stats.total_messages == 1 && M.next_message_id == 2);
    /* text fields and fields past num_fields are not inspected */
    bad = p;
    bad.fields[1].type = OC_TYPE_TEXT;
    bad.fields[1].num.valid = false;
    bad.fields[2].type = OC_TYPE_RATIONAL;
    bad.fields[2].num.valid = false;
    CHECK(pm_send(&M, 2, &bad) == 2);
    /* a route without the policy gate takes anything, unchecked */
    bad = p;
    bad.fields[0].num.valid = false;
    CHECK(pm_send(&M, 1, &bad) == 3);
    CHECK(M.stats.total_policy_checks == 4 && M.stats.total_policy_vetoes == 2);
    CHECK(M.routes[0].messages_passed == 1 && M.routes[0].messages_failed == 0);
    CHECK(M.languages[OC_LANG_COBOL].messages_sent == 1 &&
          M.languages[OC_LANG_C].messages_received == 1);
    CHECK(M.num_pending == 3 && M.stats.total_messages == 3);

    /* inactive routes are not found; a later route with the same id is */
    M.routes[0].active = false;
    CHECK(pm_send(&M, 1, &p) == -1);
    M.routes[1].route_id = 1;
    CHECK(pm_send(&M, 1, &p) == 4);
    CHECK(M.pending[3].source_lang == OC_LANG_FORTRAN);
    M.routes[1].route_id = 2;
    M.routes[0].active = true;

    /* deliver: NULL / uninitialised */
    CHECK(pm_deliver(NULL) == 0);
    M.initialized = false;
    CHECK(pm_deliver(&M) == 0);
    CHECK(M.phase_tick == 41 && M.num_pending == 4);
    M.initialized = true;

    /* one already failed and one already completed are skipped and dropped */
    M.pending[1].failed = true;
    M.pending[2].completed = true;
    CHECK(pm_deliver(&M) == 2);
    CHECK(M.phase_tick == 42 && M.num_pending == 0);
    CHECK(M.pending[0].completed && M.pending[0].delivery_attestation == LPRES_STATE_TRUE);
    CHECK(M.pending[3].completed && M.pending[3].delivery_attestation == LPRES_STATE_TRUE);
    CHECK(!M.pending[1].completed && M.pending[1].delivery_attestation == LPRES_STATE_NEITHER);
    CHECK(M.pending[2].delivery_attestation == LPRES_STATE_NEITHER);
    CHECK(pm_deliver(&M) == 0 && M.phase_tick == 43);

    /* the next message stamps the advanced tick */
    CHECK(pm_send(&M, 1, &p) == 5);
    CHECK(M.pending[0].phase_tick == 43 && M.pending[0].message_id == 5);
    CHECK(M.num_pending == 1);

    /* pending capacity: exactly PM_MAX_PENDING */
    for (uint32_t i = 1; i < PM_MAX_PENDING; i++) CHECK(pm_send(&M, 1, &p) == (int32_t) (5 + i));
    CHECK(M.num_pending == PM_MAX_PENDING);
    CHECK(pm_send(&M, 1, &p) == -1);
    CHECK(pm_send(&M, 2, &p) == -1);
    CHECK(M.stats.total_policy_checks == 5);
    CHECK(pm_deliver(&M) == PM_MAX_PENDING);
    CHECK(M.num_pending == 0);
}

static void test_pipeline(void)
{
    fresh(ALL15 & ~(1u << OC_LANG_MF));
    oc_ir_t out;
    uint8_t src[4] = {1, 2, 3, 4};
    lower_calls = 0;
    lower_rc = OC_OK;
    ir2(&lower_ir, 5, 4);
    CHECK(pm_pipeline(NULL, OC_LANG_C, src, 4, OC_LANG_C, OC_LANG_C, &out) == -1);
    CHECK(pm_pipeline(&M, OC_LANG_C, NULL, 4, OC_LANG_C, OC_LANG_C, &out) == -1);
    CHECK(pm_pipeline(&M, OC_LANG_C, src, 4, OC_LANG_C, OC_LANG_C, NULL) == -1);
    M.initialized = false;
    CHECK(pm_pipeline(&M, OC_LANG_C, src, 4, OC_LANG_C, OC_LANG_C, &out) == -1);
    M.initialized = true;
    CHECK(lower_calls == 0);

    /* lowering fails: its code is returned, nothing routed */
    lower_rc = OC_ERR_CONV;
    CHECK(pm_pipeline(&M, OC_LANG_COBOL, src, 3, OC_LANG_SUTRA, OC_LANG_C, &out) == OC_ERR_CONV);
    CHECK(lower_calls == 1 && lower_lang == OC_LANG_COBOL && lower_src == src && lower_len == 3);
    CHECK(M.num_routes == 0 && M.stats.total_cross_tier == 0);
    lower_rc = OC_OK;

    /* cross-tier: counted, policy-gated route, delivered */
    memset(&out, 0, sizeof out);
    CHECK(pm_pipeline(&M, OC_LANG_FORTRAN, src, 4, OC_LANG_COBOL, OC_LANG_SUTRA, &out) == 0);
    CHECK(M.stats.total_cross_tier == 0); /* Fortran and COBOL share a tier */
    CHECK(M.num_routes == 1 && !M.routes[0].requires_policy_check);
    CHECK(memcmp(&out, &lower_ir, sizeof out) == 0);
    CHECK(M.num_pending == 0 && M.phase_tick == 1 && M.stats.total_messages == 1);
    CHECK(M.routes[0].source_lang == OC_LANG_FORTRAN && M.routes[0].target_lang == OC_LANG_COBOL);

    memset(&out, 0, sizeof out);
    CHECK(pm_pipeline(&M, OC_LANG_COBOL, src, 4, OC_LANG_SUTRA, OC_LANG_C, &out) == 0);
    CHECK(M.stats.total_cross_tier == 1 && M.stats.total_policy_checks == 1);
    CHECK(M.num_routes == 2 && M.routes[1].requires_policy_check);
    CHECK(memcmp(&out, &lower_ir, sizeof out) == 0);
    CHECK(M.phase_tick == 2 && M.num_pending == 0);
    CHECK(M.languages[OC_LANG_SUTRA].messages_received == 1);

    /* route creation fails (target unregistered): its -1 returned */
    memset(&out, 0x11, sizeof out);
    CHECK(pm_pipeline(&M, OC_LANG_C, src, 4, OC_LANG_MF, OC_LANG_C, &out) == -1);
    CHECK(M.stats.total_cross_tier == 2 && M.num_routes == 2 && M.phase_tick == 2);
    CHECK(((uint8_t *) &out)[0] == 0x11);

    /* send vetoed by policy: -1, nothing delivered, output untouched */
    lower_ir.fields[1].num.valid = false;
    CHECK(pm_pipeline(&M, OC_LANG_C, src, 4, OC_LANG_SUTRA, OC_LANG_C, &out) == -1);
    CHECK(M.stats.total_policy_vetoes == 1 && M.num_routes == 3 && M.phase_tick == 2);
    CHECK(((uint8_t *) &out)[0] == 0x11);
    /* same tier with the bad payload goes through unchecked */
    CHECK(pm_pipeline(&M, OC_LANG_C, src, 4, OC_LANG_ASSEMBLY, OC_LANG_C, &out) == 0);
    CHECK(M.stats.total_cross_tier == 3 && M.phase_tick == 3);
    CHECK(!out.fields[1].num.valid);

    /* route creation fails on a full table: nothing is sent, even to a
     * route whose id reads as -1 */
    lower_ir.fields[1].num.valid = true;
    fresh(ALL15);
    for (uint32_t i = 0; i < PM_MAX_ROUTES; i++) pm_create_route(&M, OC_LANG_C, OC_LANG_C, false);
    M.routes[5].route_id = 0xFFFFFFFFu;
    CHECK(pm_pipeline(&M, OC_LANG_C, src, 4, OC_LANG_C, OC_LANG_C, &out) == -1);
    CHECK(M.stats.total_messages == 0 && M.phase_tick == 0);

    /* id 0 (counters wrapped) is a valid route and message id */
    fresh(ALL15);
    M.next_route_id = 0;
    M.next_message_id = 0;
    memset(&out, 0, sizeof out);
    CHECK(pm_pipeline(&M, OC_LANG_C, src, 4, OC_LANG_C, OC_LANG_C, &out) == 0);
    CHECK(M.routes[0].route_id == 0 && M.stats.total_messages == 1 && M.phase_tick == 1);
    CHECK(M.pending[0].message_id == 0 && M.pending[0].completed);
    CHECK(memcmp(&out, &lower_ir, sizeof out) == 0);
}

static void test_audit_stats(void)
{
    fresh(ALL15);
    CHECK(pm_self_audit(NULL) == -1);
    M.initialized = false;
    CHECK(pm_self_audit(&M) == -1);
    CHECK(M.matrix_attestation == LPRES_STATE_NEITHER);
    M.initialized = true;

    CHECK(pm_self_audit(&M) == 0); /* no routes */
    CHECK(M.matrix_attestation == LPRES_STATE_TRUE);
    pm_create_route(&M, OC_LANG_C, OC_LANG_RUST, false);
    pm_create_route(&M, OC_LANG_C, OC_LANG_ZIG, false);
    pm_create_route(&M, OC_LANG_C, OC_LANG_WASM, false);
    M.routes[3].active = true; /* stale slot past num_routes: ignored */
    M.routes[3].coverage_ratio = 0.5;
    CHECK(pm_self_audit(&M) == 0);
    CHECK(M.routes[3].route_attestation == LPRES_STATE_NEITHER);
    CHECK(M.routes[0].route_attestation == LPRES_STATE_TRUE &&
          M.routes[1].route_attestation == LPRES_STATE_TRUE &&
          M.routes[2].route_attestation == LPRES_STATE_TRUE);
    /* exactly the minimum passes; just below fails that route only */
    M.routes[0].coverage_ratio = 1.8;
    M.routes[1].coverage_ratio = 1.79;
    CHECK(pm_self_audit(&M) == -1);
    CHECK(M.matrix_attestation == LPRES_STATE_FALSE);
    CHECK(M.routes[0].route_attestation == LPRES_STATE_TRUE);
    CHECK(M.routes[1].route_attestation == LPRES_STATE_FALSE);
    CHECK(M.routes[2].route_attestation == LPRES_STATE_TRUE);
    /* a failing route first does not stop the scan */
    M.routes[2].route_attestation = LPRES_STATE_NEITHER;
    CHECK(pm_self_audit(&M) == -1 && M.routes[2].route_attestation == LPRES_STATE_TRUE);
    /* inactive routes are skipped, attestation untouched */
    M.routes[1].active = false;
    M.routes[1].route_attestation = LPRES_STATE_BOTH;
    CHECK(pm_self_audit(&M) == 0);
    CHECK(M.matrix_attestation == LPRES_STATE_TRUE);
    CHECK(M.routes[1].route_attestation == LPRES_STATE_BOTH);
    /* raising the minimum fails the 1.8 route */
    M.min_coverage = 1.81;
    CHECK(pm_self_audit(&M) == -1 && M.routes[0].route_attestation == LPRES_STATE_FALSE);
    M.min_coverage = 1.8;

    /* a bound language that went inactive fails; an unbound one does not */
    M.languages[OC_LANG_TELECOM].active = false;
    CHECK(pm_self_audit(&M) == -1 && M.matrix_attestation == LPRES_STATE_FALSE);
    M.languages[OC_LANG_TELECOM].bound = false;
    CHECK(pm_self_audit(&M) == 0 && M.matrix_attestation == LPRES_STATE_TRUE);
    M.languages[OC_LANG_COBOL].active = false;
    CHECK(pm_self_audit(&M) == -1);
    M.languages[OC_LANG_COBOL].active = true;

    /* stats */
    M.stats.total_messages = 1;
    M.stats.total_cross_tier = 2;
    M.stats.total_policy_checks = 3;
    M.stats.total_policy_vetoes = 4;
    M.stats.total_failures = 5;
    struct {
        uint64_t a, b, c, d, e;
    } st;
    _Static_assert(sizeof st == sizeof M.stats, "stats layout");
    memset(&st, 0xEE, sizeof st);
    pm_get_stats(&M, &st);
    CHECK(st.a == 1 && st.b == 2 && st.c == 3 && st.d == 4 && st.e == 5);
    memset(&st, 0xEE, sizeof st);
    pm_get_stats(NULL, &st);
    CHECK(st.a == 0xEEEEEEEEEEEEEEEEull);
    pm_get_stats(&M, NULL);
}

int main(void)
{
    test_tiers();
    test_init_register();
    test_routes();
    test_send_deliver();
    test_pipeline();
    test_audit_stats();
    if (fails) {
        printf("%d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("%d checks passed\n", checks);
    return 0;
}
