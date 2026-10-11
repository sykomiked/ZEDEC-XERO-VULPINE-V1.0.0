/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_situation_model.c — host test for situation_model.c.
 *
 * situation_model.c is a COMBINER: every domain calls the EDP risk operator,
 * the ISF surplus f(u) and the predictive model, then derives readiness,
 * vulnerability, resilience, escalation, the threat level and the
 * immediate-action flag from them and from one domain-specific input. Those
 * engines have their own tests, so here they are replaced by recording stubs
 * with simple exact outputs (all values are binary fractions, so every
 * comparison below is ==, no epsilon). That isolates what THIS file decides:
 *   - which inputs reach which engine (geopolitical overrides phi),
 *   - each domain's formulas and clamps, at and either side of each boundary,
 *   - the threat ladder at 0.1/0.3/0.5/0.7/0.9 (>=, so the boundary is in),
 *   - dispatch for all 15 domains plus an out-of-range one,
 *   - cross-domain contagion (sum over pairs i<j, divided by count^2),
 *   - situation_assess_all aggregates, the 15-domain cap and empty input,
 *   - the name tables and their "Unknown" fallbacks, and the bring-up.
 * The module is #included (as test_virtio_gpu.c does) so the static bring-up
 * is callable. surplus_real_t is double under TEST_HOST. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "situation_model.c"

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

/* ---- recording stubs for the engines ------------------------------------ */
static bool g_breach;          /* edp_compute_risk().coverage_breached */
static double g_last_risk_phi; /* phi seen by edp_compute_risk */
static double g_last_nl_phi;   /* phi seen by predictive_nonlinear_risk */
static double g_last_pc_phi;   /* phi seen by predictive_count */
static double g_last_cov_phi;  /* phi seen by edp_coverage_product */
static const predictive_config_t *g_last_cfg;
static int g_nl_calls;

risk_result_t edp_compute_risk(const m5_coords_t *c, surplus_real_t exposure, surplus_real_t psi)
{
    risk_result_t r;
    memset(&r, 0, sizeof r);
    r.magnitude = exposure * 3.0;
    r.real_part = psi;
    r.phase = c->phi;
    r.coverage_breached = g_breach;
    g_last_risk_phi = c->phi;
    return r;
}

surplus_real_t surplus_f(surplus_real_t u, uint32_t N)
{
    return u * 2.0 + (double) N / 4.0;
}

surplus_real_t predictive_nonlinear_risk(const m5_coords_t *c, surplus_real_t u,
                                         surplus_real_t exposure, const predictive_config_t *cfg)
{
    (void) u;
    g_last_nl_phi = c->phi;
    g_last_cfg = cfg;
    g_nl_calls++;
    return exposure; /* composite risk == exposure: the test steers threat by it */
}

prediction_t predictive_count(const m5_coords_t *c, surplus_real_t u, surplus_real_t exposure,
                              const predictive_config_t *cfg)
{
    prediction_t p;
    memset(&p, 0, sizeof p);
    p.predicted_count = u + exposure;
    p.confidence = c->phi;
    p.upper_bound = (double) cfg->N;
    g_last_pc_phi = c->phi;
    return p;
}

surplus_real_t edp_coverage_product(const m5_coords_t *c)
{
    g_last_cov_phi = c->phi;
    return c->r * c->ell;
}

bool edp_coverage_satisfied(const m5_coords_t *c)
{
    return c->r * c->ell >= 1.75;
}

fib_signed_t edp_nc_rating(surplus_real_t coverage, surplus_real_t externality)
{
    fib_signed_t f;
    f.magnitude = (int32_t) (coverage * 100.0);
    f.sign = 1;
    f.level = 7;
    f.phase = externality;
    return f;
}

/* ---- helpers ------------------------------------------------------------ */
static predictive_config_t CFG;

static situation_input_t base_input(situation_domain_t d)
{
    situation_input_t in;
    memset(&in, 0, sizeof in);
    in.domain = d;
    in.coords.r = 1.5;
    in.coords.ell = 0.5; /* coverage product 0.75 */
    in.coords.phi = 0.25;
    in.exposure = 0.4; /* composite 0.4 -> MODERATE */
    in.interaction_u = 0.125;
    in.psi_amplitude_sq = 0.5;
    in.force_ratio = 0.75;
    in.supply_coverage = 0.75;
    in.diplomatic_phase = 1.5;
    in.environmental_stress = 0.25;
    in.contagion_rate = 2.0;
    in.cyber_exposure = 0.25;
    in.resource_depletion = 0.25;
    in.orbital_congestion = 0.25;
    in.social_cohesion = 0.75;
    return in;
}

/* the engine-derived fields every domain fills the same way */
static int common_ok(const situation_result_t *r, const situation_input_t *in, double phi)
{
    double surplus = in->interaction_u * 2.0 + (double) CFG.N / 4.0;
    return r->domain == in->domain && r->risk.magnitude == in->exposure * 3.0 &&
           r->risk.real_part == in->psi_amplitude_sq && r->risk.phase == phi &&
           r->surplus_value == surplus && r->composite_risk == in->exposure &&
           r->prediction.predicted_count == in->interaction_u + in->exposure &&
           r->prediction.confidence == phi && r->prediction.upper_bound == (double) CFG.N &&
           r->action_rating.magnitude == (int32_t) (in->coords.r * in->coords.ell * 100.0) &&
           r->action_rating.level == 7 && r->action_rating.phase == phi;
}

static double S(const situation_input_t *in)
{
    return in->interaction_u * 2.0 + (double) CFG.N / 4.0; /* 0.25 + 2 = 2.25 */
}

/* ---- tests -------------------------------------------------------------- */
static void test_threat_level(void)
{
    const double edge[5] = {0.1, 0.3, 0.5, 0.7, 0.9};
    for (int i = 0; i < 5; i++) {
        CHECK(situation_threat_level(edge[i]) == (threat_level_t) (i + 1),
              "boundary value is in the higher level");
        CHECK(situation_threat_level(nextafter(edge[i], 0.0)) == (threat_level_t) i,
              "just below the boundary stays in the lower level");
    }
    CHECK(situation_threat_level(0.0) == THREAT_NONE, "0 -> NONE");
    CHECK(situation_threat_level(-5.0) == THREAT_NONE, "negative -> NONE");
    CHECK(situation_threat_level(0.2) == THREAT_LOW, "0.2 -> LOW");
    CHECK(situation_threat_level(0.4) == THREAT_MODERATE, "0.4 -> MODERATE");
    CHECK(situation_threat_level(0.6) == THREAT_HIGH, "0.6 -> HIGH");
    CHECK(situation_threat_level(0.8) == THREAT_CRITICAL, "0.8 -> CRITICAL");
    CHECK(situation_threat_level(1e9) == THREAT_EXISTENTIAL, "huge -> EXISTENTIAL");
}

static void test_names(void)
{
    static const char *dn[15] = {"Military", "Economic",    "Logistics",    "Geopolitical",
                                 "Climate",  "Pandemic",    "Cyber",        "Energy",
                                 "Space",    "Social",      "Agricultural", "Maritime",
                                 "Nuclear",  "Information", "Humanitarian"};
    for (int i = 0; i < 15; i++)
        CHECK(strcmp(situation_domain_name((situation_domain_t) i), dn[i]) == 0, "domain name");
    CHECK(strcmp(situation_domain_name(SIT_DOMAIN_MAX), "Unknown") == 0, "domain 15 Unknown");
    CHECK(strcmp(situation_domain_name((situation_domain_t) 99), "Unknown") == 0,
          "domain 99 Unknown");
    static const char *tn[6] = {"None", "Low", "Moderate", "High", "Critical", "Existential"};
    for (int i = 0; i < 6; i++)
        CHECK(strcmp(situation_threat_name((threat_level_t) i), tn[i]) == 0, "threat name");
    CHECK(strcmp(situation_threat_name((threat_level_t) 6), "Unknown") == 0, "threat 6 Unknown");
}

static void test_military(void)
{
    situation_input_t in = base_input(SIT_DOMAIN_MILITARY);
    g_breach = true;
    situation_result_t r = situation_assess(&in, &CFG);
    CHECK(common_ok(&r, &in, 0.25), "military: engine fields");
    CHECK(r.coverage_breached == true, "military: breach comes from the risk operator");
    CHECK(r.operational_readiness == 0.75 && r.vulnerability_index == 0.25,
          "military: readiness = force ratio, vulnerability = 1 - it");
    CHECK(r.resilience_score == 0.75 * S(&in), "military: resilience = ratio * surplus");
    CHECK(r.escalation_probability == S(&in) * 0.25, "military: escalation = surplus * vuln");
    CHECK(r.threat_level == THREAT_MODERATE && !r.immediate_action_required,
          "military: MODERATE, no immediate action");
    in.force_ratio = 1.5; /* overmatched: vulnerability clamps at 0 */
    in.exposure = 0.5;    /* HIGH */
    g_breach = false;
    r = situation_military(&in, &CFG);
    CHECK(r.vulnerability_index == 0.0 && r.escalation_probability == 0.0 &&
              r.operational_readiness == 1.5,
          "military: vulnerability clamps at 0, readiness not clamped");
    CHECK(r.threat_level == THREAT_HIGH && r.immediate_action_required && !r.coverage_breached,
          "military: HIGH demands immediate action");
    in.force_ratio = 1.0;
    r = situation_military(&in, &CFG);
    CHECK(r.vulnerability_index == 0.0, "military: ratio 1 -> vulnerability 0");
}

static void test_economic(void)
{
    situation_input_t in = base_input(SIT_DOMAIN_ECONOMIC);
    in.coords.r = 0.9;
    in.coords.ell = 1.0; /* coverage 0.9 -> readiness 0.9 / 1.8 = 0.5 */
    in.exposure = 0.75;
    g_breach = false;
    situation_result_t r = situation_assess(&in, &CFG);
    CHECK(common_ok(&r, &in, 0.25), "economic: engine fields");
    CHECK(r.operational_readiness == 0.9 / 1.8 && r.vulnerability_index == 1.0 - 0.9 / 1.8,
          "economic: readiness = coverage / 1.8");
    CHECK(r.resilience_score == S(&in) * (0.9 / 1.8), "economic: resilience");
    CHECK(r.escalation_probability == 0.75 * 0.5, "economic: escalation = composite * psi");
    CHECK(r.threat_level == THREAT_CRITICAL, "economic: 0.75 -> CRITICAL");
    CHECK(!r.immediate_action_required, "economic: action only on coverage breach");
    g_breach = true;
    in.coords.r = 3.6; /* coverage 3.6 -> 2.0, clamps to 1 */
    r = situation_economic(&in, &CFG);
    CHECK(r.operational_readiness == 1.0 && r.vulnerability_index == 0.0,
          "economic: readiness clamps at 1");
    CHECK(r.immediate_action_required && r.coverage_breached, "economic: breach -> action");
    in.coords.r = 1.8; /* exactly the floor: 1.8 / 1.8 */
    r = situation_economic(&in, &CFG);
    CHECK(r.operational_readiness == 1.8 / 1.8 && r.vulnerability_index == 1.0 - 1.8 / 1.8,
          "economic: at the floor");
}

static void test_logistics(void)
{
    situation_input_t in = base_input(SIT_DOMAIN_LOGISTICS);
    g_breach = false;
    situation_result_t r = situation_assess(&in, &CFG);
    CHECK(common_ok(&r, &in, 0.25), "logistics: engine fields");
    CHECK(r.operational_readiness == 0.75 && r.vulnerability_index == 0.25,
          "logistics: readiness = supply coverage");
    CHECK(r.resilience_score == S(&in) * 0.75 && r.escalation_probability == 0.0,
          "logistics: resilience, zero escalation");
    CHECK(!r.immediate_action_required, "logistics: 0.75 supply is fine");
    in.supply_coverage = 0.5;
    r = situation_logistics(&in, &CFG);
    CHECK(!r.immediate_action_required, "logistics: exactly 0.5 is not a shortfall");
    in.supply_coverage = nextafter(0.5, 0.0);
    r = situation_logistics(&in, &CFG);
    CHECK(r.immediate_action_required, "logistics: just under 0.5 is a shortfall");
    in.supply_coverage = 2.0;
    r = situation_logistics(&in, &CFG);
    CHECK(r.operational_readiness == 1.0 && r.vulnerability_index == 0.0,
          "logistics: readiness clamps at 1");
    CHECK(r.resilience_score == S(&in) * 2.0, "logistics: resilience uses the UNclamped supply");
    in.supply_coverage = 1.0;
    r = situation_logistics(&in, &CFG);
    CHECK(r.operational_readiness == 1.0 && r.vulnerability_index == 0.0, "logistics: at 1");
}

static void test_geopolitical(void)
{
    situation_input_t in = base_input(SIT_DOMAIN_GEOPOLITICAL);
    in.exposure = 0.6;
    g_breach = true;
    situation_result_t r = situation_assess(&in, &CFG);
    /* the diplomatic phase REPLACES phi for every engine call */
    CHECK(common_ok(&r, &in, 1.5), "geopolitical: engine fields under phi = diplomatic phase");
    CHECK(g_last_risk_phi == 1.5 && g_last_nl_phi == 1.5 && g_last_pc_phi == 1.5 &&
              g_last_cov_phi == 1.5,
          "geopolitical: every engine saw the overridden phi");
    CHECK(r.coverage_breached, "geopolitical: breach from the risk operator");
    CHECK(r.operational_readiness == 1.0 - 1.5 / 3.0 && r.vulnerability_index == 1.5 / 3.0,
          "geopolitical: readiness 1 - phase/3, vulnerability phase/3");
    CHECK(r.resilience_score == S(&in), "geopolitical: resilience = surplus");
    CHECK(r.escalation_probability == 0.6 * 0.5, "geopolitical: escalation = composite * psi");
    CHECK(r.threat_level == THREAT_HIGH && r.immediate_action_required,
          "geopolitical: HIGH -> action");
    in.diplomatic_phase = 6.0;
    in.exposure = 0.45;
    r = situation_geopolitical(&in, &CFG);
    CHECK(r.operational_readiness == 0.0 && r.vulnerability_index == 1.0,
          "geopolitical: both clamp at phase 6");
    CHECK(r.threat_level == THREAT_MODERATE && !r.immediate_action_required,
          "geopolitical: MODERATE -> no action");
    in.diplomatic_phase = 3.0;
    r = situation_geopolitical(&in, &CFG);
    CHECK(r.operational_readiness == 0.0 && r.vulnerability_index == 1.0,
          "geopolitical: at phase 3 exactly");
    CHECK(in.coords.phi == 0.25, "geopolitical: caller's coords not modified");
}

/* climate, cyber, energy and space share one shape: readiness = 1 - x
 * (floored at 0), vulnerability = x, escalation = x * psi, action when x > t */
typedef situation_result_t (*dom_fn)(const situation_input_t *, const predictive_config_t *);
static void shape_1mx(situation_domain_t d, dom_fn fn, double *field, situation_input_t *in,
                      double thr, const char *name)
{
    char msg[96];
    g_breach = (d % 2) == 0;
    *field = 0.25;
    situation_result_t r = situation_assess(in, &CFG);
    snprintf(msg, sizeof msg, "%s: engine fields and breach", name);
    CHECK(common_ok(&r, in, 0.25) && r.coverage_breached == g_breach, msg);
    snprintf(msg, sizeof msg, "%s: readiness 1-x, vulnerability x", name);
    CHECK(r.operational_readiness == 0.75 && r.vulnerability_index == 0.25, msg);
    snprintf(msg, sizeof msg, "%s: resilience, escalation", name);
    CHECK(r.resilience_score == S(in) * 0.75 && r.escalation_probability == 0.25 * 0.5, msg);
    snprintf(msg, sizeof msg, "%s: no action at 0.25", name);
    CHECK(!r.immediate_action_required && r.threat_level == THREAT_MODERATE, msg);
    *field = thr;
    r = fn(in, &CFG);
    snprintf(msg, sizeof msg, "%s: no action exactly at the threshold", name);
    CHECK(!r.immediate_action_required, msg);
    *field = nextafter(thr, 1.0);
    r = fn(in, &CFG);
    snprintf(msg, sizeof msg, "%s: action just above the threshold", name);
    CHECK(r.immediate_action_required, msg);
    *field = 1.5;
    r = fn(in, &CFG);
    snprintf(msg, sizeof msg, "%s: readiness floors at 0", name);
    CHECK(r.operational_readiness == 0.0 && r.vulnerability_index == 1.5 &&
              r.resilience_score == 0.0 && r.escalation_probability == 0.75,
          msg);
    *field = 1.0;
    r = fn(in, &CFG);
    snprintf(msg, sizeof msg, "%s: readiness 0 at x = 1", name);
    CHECK(r.operational_readiness == 0.0, msg);
}

static void test_shared_shape(void)
{
    situation_input_t in = base_input(SIT_DOMAIN_CLIMATE);
    shape_1mx(SIT_DOMAIN_CLIMATE, situation_climate, &in.environmental_stress, &in, 0.7, "climate");
    in = base_input(SIT_DOMAIN_CYBER);
    shape_1mx(SIT_DOMAIN_CYBER, situation_cyber, &in.cyber_exposure, &in, 0.7, "cyber");
    in = base_input(SIT_DOMAIN_ENERGY);
    shape_1mx(SIT_DOMAIN_ENERGY, situation_energy, &in.resource_depletion, &in, 0.7, "energy");
    in = base_input(SIT_DOMAIN_SPACE);
    shape_1mx(SIT_DOMAIN_SPACE, situation_space, &in.orbital_congestion, &in, 0.8, "space");
}

static void test_pandemic(void)
{
    situation_input_t in = base_input(SIT_DOMAIN_PANDEMIC);
    g_breach = true;
    situation_result_t r = situation_assess(&in, &CFG);
    CHECK(common_ok(&r, &in, 0.25) && r.coverage_breached, "pandemic: engine fields");
    CHECK(r.operational_readiness == 0.5 && r.vulnerability_index == 1.0,
          "pandemic: R0 2 -> readiness 1/2, vulnerability 1");
    CHECK(r.resilience_score == S(&in) * 0.5 && r.escalation_probability == 2.0 * 0.5,
          "pandemic: resilience, escalation = R0 * psi");
    CHECK(r.immediate_action_required, "pandemic: R0 2 > 1.5");
    in.contagion_rate = 1.5;
    r = situation_pandemic(&in, &CFG);
    CHECK(!r.immediate_action_required, "pandemic: exactly 1.5 is not above");
    CHECK(r.vulnerability_index == 0.5 && r.operational_readiness == 1.0 / 1.5,
          "pandemic: R0 1.5 -> vulnerability 0.5, readiness 2/3");
    in.contagion_rate = nextafter(1.5, 2.0);
    r = situation_pandemic(&in, &CFG);
    CHECK(r.immediate_action_required, "pandemic: just above 1.5");
    in.contagion_rate = 0.5;
    r = situation_pandemic(&in, &CFG);
    CHECK(r.operational_readiness == 1.0 && r.vulnerability_index == 0.0 &&
              !r.immediate_action_required,
          "pandemic: R0 < 1 clamps readiness to 1 and vulnerability to 0");
    in.contagion_rate = 1.0;
    r = situation_pandemic(&in, &CFG);
    CHECK(r.operational_readiness == 1.0 && r.vulnerability_index == 0.0, "pandemic: R0 = 1");
}

static void test_social(void)
{
    situation_input_t in = base_input(SIT_DOMAIN_SOCIAL);
    g_breach = false;
    situation_result_t r = situation_assess(&in, &CFG);
    CHECK(common_ok(&r, &in, 0.25) && !r.coverage_breached, "social: engine fields");
    CHECK(r.operational_readiness == 0.75 && r.vulnerability_index == 0.25,
          "social: readiness = cohesion");
    CHECK(r.resilience_score == S(&in) * 0.75 && r.escalation_probability == 0.25 * 0.5,
          "social: resilience, escalation = vuln * psi");
    CHECK(!r.immediate_action_required, "social: cohesion 0.75 is fine");
    in.social_cohesion = 0.3;
    r = situation_social(&in, &CFG);
    CHECK(!r.immediate_action_required, "social: exactly 0.3 is not below");
    in.social_cohesion = nextafter(0.3, 0.0);
    r = situation_social(&in, &CFG);
    CHECK(r.immediate_action_required, "social: just below 0.3");
    in.social_cohesion = 1.5;
    r = situation_social(&in, &CFG);
    CHECK(r.vulnerability_index == 0.0 && r.escalation_probability == 0.0 &&
              r.operational_readiness == 1.5,
          "social: vulnerability clamps at 0");
    in.social_cohesion = 1.0;
    r = situation_social(&in, &CFG);
    CHECK(r.vulnerability_index == 0.0, "social: cohesion 1 -> vulnerability 0");
}

/* Fill the stack below us with non-zero bytes, so a field the module forgets
 * to initialise does not read as zero by luck. */
static __attribute__((noinline)) void dirty_stack(void)
{
    volatile char junk[16384];
    for (size_t i = 0; i < sizeof junk; i++) junk[i] = 'X';
}

static void test_default_domains(void)
{
    for (int d = SIT_DOMAIN_AGRICULTURAL; d <= SIT_DOMAIN_MAX + 1; d++) {
        situation_input_t in = base_input((situation_domain_t) d);
        in.exposure = 0.95; /* would be EXISTENTIAL if it were rated */
        g_breach = true;
        in.coords.r = 3.5;
        in.coords.ell = 0.5; /* 1.75: satisfied */
        dirty_stack();
        situation_result_t r = situation_assess(&in, &CFG);
        CHECK(common_ok(&r, &in, 0.25), "default domain: engine fields");
        CHECK(r.threat_level == THREAT_NONE && r.operational_readiness == 1.0 &&
                  r.vulnerability_index == 0.0 && r.resilience_score == 1.0 &&
                  r.escalation_probability == 0.0 && !r.immediate_action_required,
              "default domain: unrated, neutral outputs");
        CHECK(!r.coverage_breached, "default domain: breach from coverage test, not operator");
        CHECK(r.assessment_summary[0] == 0, "default domain: empty summary");
        in.coords.r = nextafter(3.5, 0.0);
        r = situation_assess(&in, &CFG);
        CHECK(r.coverage_breached, "default domain: coverage below 1.75 is a breach");
    }
}

static void test_dispatch(void)
{
    /* every rated domain dispatches to its own function: compare whole results */
    static const dom_fn fns[10] = {situation_military,     situation_economic, situation_logistics,
                                   situation_geopolitical, situation_climate,  situation_pandemic,
                                   situation_cyber,        situation_energy,   situation_space,
                                   situation_social};
    for (int d = 0; d < 10; d++) {
        situation_input_t in = base_input((situation_domain_t) d);
        situation_result_t a = situation_assess(&in, &CFG), b = fns[d](&in, &CFG);
        CHECK(a.domain == (situation_domain_t) d &&
                  a.operational_readiness == b.operational_readiness &&
                  a.vulnerability_index == b.vulnerability_index &&
                  a.escalation_probability == b.escalation_probability &&
                  a.resilience_score == b.resilience_score &&
                  a.immediate_action_required == b.immediate_action_required,
              "dispatch reaches the matching domain function");
    }
    g_last_cfg = NULL;
    situation_input_t in = base_input(SIT_DOMAIN_CYBER);
    (void) situation_assess(&in, &CFG);
    CHECK(g_last_cfg == &CFG, "config is passed through");
}

static void test_contagion(void)
{
    static situation_result_t d[4];
    memset(d, 0, sizeof d);
    d[0].composite_risk = 0.5;
    d[1].composite_risk = 0.25;
    d[2].composite_risk = 2.0;
    d[3].composite_risk = 64.0; /* beyond count: must not be read */
    CHECK(situation_cross_domain_contagion(d, 0) == 0.0, "count 0 -> 0");
    CHECK(situation_cross_domain_contagion(d, 1) == 0.0, "count 1 -> no pairs -> 0");
    CHECK(situation_cross_domain_contagion(d, 2) == (0.5 * 0.25) / 4.0, "two: ab / 4");
    double want = ((0.5 * 0.25 + 0.5 * 2.0) + 0.25 * 2.0) / 9.0;
    CHECK(situation_cross_domain_contagion(d, 3) == want, "three: (ab+ac+bc) / 9");
}

static void test_assess_all(void)
{
    static comprehensive_assessment_t out;
    static situation_input_t in[16];
    g_breach = false;

    in[0] = base_input(SIT_DOMAIN_MILITARY);
    in[0].exposure = 0.2; /* LOW */
    in[1] = base_input(SIT_DOMAIN_CYBER);
    in[1].exposure = 0.75; /* CRITICAL */
    in[2] = base_input(SIT_DOMAIN_SOCIAL);
    in[2].exposure = 0.5; /* HIGH */
    in[2].interaction_u = 0.5;
    memset(&out, 0x5A, sizeof out);
    situation_assess_all(&out, in, 3, &CFG);
    double s0 = S(&in[0]), s2 = S(&in[2]);
    CHECK(out.domains[0].domain == SIT_DOMAIN_MILITARY &&
              out.domains[1].domain == SIT_DOMAIN_CYBER &&
              out.domains[2].domain == SIT_DOMAIN_SOCIAL,
          "assess_all: results in input order");
    CHECK(out.aggregate_risk == ((0.0 + 0.2) + 0.75 + 0.5) / 3.0, "assess_all: mean risk");
    CHECK(out.aggregate_readiness == ((0.0 + 0.75) + 0.75 + 0.75) / 3.0,
          "assess_all: mean readiness");
    CHECK(out.system_surplus == ((0.0 + s0) + s0) + s2, "assess_all: surplus is a SUM");
    CHECK(out.system_sustainability == out.system_surplus / 3.0,
          "assess_all: sustainability = surplus / n");
    CHECK(out.max_threat == THREAT_CRITICAL, "assess_all: max threat");
    CHECK(out.critical_domains == 2, "assess_all: HIGH and CRITICAL counted, LOW not");
    double want = ((0.2 * 0.75 + 0.2 * 0.5) + 0.75 * 0.5) / 9.0;
    CHECK(out.systemic_contagion == want, "assess_all: contagion over assessed domains");

    /* exactly HIGH counts; MODERATE does not */
    in[0].exposure = 0.5;
    in[1].exposure = nextafter(0.5, 0.0);
    situation_assess_all(&out, in, 2, &CFG);
    CHECK(out.critical_domains == 1 && out.max_threat == THREAT_HIGH,
          "assess_all: HIGH boundary counted once");
    /* max threat keeps the FIRST maximum, later lower ones do not lower it */
    in[0].exposure = 0.95;
    in[1].exposure = 0.0;
    situation_assess_all(&out, in, 2, &CFG);
    CHECK(out.max_threat == THREAT_EXISTENTIAL && out.critical_domains == 1,
          "assess_all: EXISTENTIAL first, NONE second");

    /* empty: everything zero */
    memset(&out, 0x5A, sizeof out);
    situation_assess_all(&out, in, 0, &CFG);
    CHECK(out.aggregate_risk == 0.0 && out.aggregate_readiness == 0.0 &&
              out.system_surplus == 0.0 && out.system_sustainability == 0.0 &&
              out.max_threat == THREAT_NONE && out.critical_domains == 0 &&
              out.systemic_contagion == 0.0,
          "assess_all: empty input is all zero");

    /* more than 15 inputs: only the first 15 are assessed */
    for (int i = 0; i < 16; i++) {
        in[i] = base_input((situation_domain_t) (i % 10));
        in[i].exposure = 0.0;
    }
    in[15].exposure = 0.95;
    g_nl_calls = 0;
    situation_assess_all(&out, in, 16, &CFG);
    CHECK(g_nl_calls == 15, "assess_all: capped at 15 domains");
    CHECK(out.max_threat == THREAT_NONE && out.aggregate_risk == 0.0,
          "assess_all: the 16th input is ignored");
    in[14].exposure = 0.3;
    situation_assess_all(&out, in, 15, &CFG);
    CHECK(out.max_threat == THREAT_MODERATE && out.aggregate_risk == 0.3 / 15.0,
          "assess_all: the 15th input counts");
}

int main(void)
{
    CFG.N = 8;
    test_threat_level();
    test_names();
    test_military();
    test_economic();
    test_logistics();
    test_geopolitical();
    test_shared_shape();
    test_pandemic();
    test_social();
    test_default_domains();
    test_dispatch();
    test_contagion();
    test_assess_all();
    CHECK(zxvd_situation_bringup() == 0, "bring-up passes");
    printf("test_situation_model: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
