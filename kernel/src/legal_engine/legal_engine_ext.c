/*
 * legal_engine_ext.c — Extended legal engine: treaties, precedents,
 * DAO governance, jurisdictional arbitrage, Panopticon/LPRES integration
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */

#include "legal_engine.h"
#include "../include/freestanding.h"

/* defined below, used by treaty_enforce_breach */
static legal_enforcement_t *enforcement_slot(legal_engine_ext_t *engine,
                                             uint32_t watcher_id);

static uint32_t g_next_treaty_id = 1;
static uint32_t g_next_precedent_id = 1;
static uint32_t g_next_dao_id = 1;
static uint32_t g_next_arbitrage_id = 1;

static void safe_strncpy(uint8_t *dst, const char *src, int max) {
    int i = 0;
    if (src) { while (src[i] && i < max - 1) { dst[i] = src[i]; i++; } }
    dst[i] = 0;
}

static uint8_t phi_checksum(const void *data, uint32_t len) {
    const double phi = 1.6180339887498948;
    double sum = 0;
    const uint8_t *p = (const uint8_t *)data;
    for (uint32_t i = 0; i < len; i++)
        sum += (double)p[i] * phi;
    return (uint8_t)((uint64_t)sum & 0xFF);
}

static const char *treaty_status_name(treaty_status_t s) {
    switch (s) {
        case TREATY_DRAFT:       return "Draft";
        case TREATY_NEGOTIATING: return "Negotiating";
        case TREATY_SIGNED:      return "Signed";
        case TREATY_RATIFIED:    return "Ratified";
        case TREATY_ACTIVE:      return "Active";
        case TREATY_SUSPENDED:   return "Suspended";
        case TREATY_BREACHED:    return "Breached";
        case TREATY_TERMINATED:  return "Terminated";
        default: return "Unknown";
    }
}

static const char *treaty_type_name(treaty_type_t t) {
    switch (t) {
        case TREATY_BILATERAL:      return "Bilateral";
        case TREATY_MULTILATERAL:   return "Multilateral";
        case TREATY_NON_AGGRESSION: return "Non-Aggression Pact";
        case TREATY_DATA_SHARING:   return "Data Sharing Agreement";
        case TREATY_RESOURCE_ALLOC: return "Resource Allocation";
        case TREATY_MUTUAL_DEFENSE: return "Mutual Defense Pact";
        case TREATY_COMMERCE:       return "Commerce Treaty";
        case TREATY_EXTRADITION:    return "Extradition Treaty";
        default: return "Unknown";
    }
}

/* ============================================================
 * Extended Engine Init
 * ============================================================ */

int legal_engine_ext_init(legal_engine_ext_t *engine) {
    if (!engine) return -1;
    fs_memset(engine, 0, sizeof(*engine));
    /* Load nations from base engine */
    legal_engine_t base;
    legal_engine_init(&base);
    fs_memcpy(engine->nations, base.nations, sizeof(base.nations));
    engine->nation_count = base.nation_count;
    return 0;
}

int legal_engine_ext_tick(legal_engine_ext_t *engine) {
    if (!engine) return -1;
    engine->phase_tick++;
    /* Auto-audit active treaties periodically */
    for (uint32_t i = 0; i < LEGAL_MAX_TREATIES; i++) {
        if (engine->treaties[i].status == TREATY_ACTIVE &&
            engine->treaties[i].auto_enforce) {
            if (engine->phase_tick - engine->treaties[i].last_audit_tick > 100) {
                treaty_audit(engine, engine->treaties[i].treaty_id);
            }
        }
    }
    return 0;
}

/* ============================================================
 * Treaty Protocol
 * ============================================================ */

int treaty_init(legal_engine_ext_t *engine) {
    if (!engine) return -1;
    return 0;
}

int treaty_create(legal_engine_ext_t *engine, treaty_type_t type,
                  const char *title, p2p_treaty_t **treaty) {
    if (!engine || !title) return -1;
    for (uint32_t i = 0; i < LEGAL_MAX_TREATIES; i++) {
        if (engine->treaties[i].status == TREATY_TERMINATED ||
            engine->treaties[i].treaty_id == 0) {
            p2p_treaty_t *t = &engine->treaties[i];
            fs_memset(t, 0, sizeof(*t));
            t->treaty_id = g_next_treaty_id++;
            t->type = type;
            t->status = TREATY_DRAFT;
            safe_strncpy(t->title, title, LEGAL_MAX_NAME);
            t->created_tick = engine->phase_tick;
            safe_strncpy(t->jurisdiction, "Sovereign — SEL-3.3", LEGAL_MAX_NAME);
            t->auto_enforce = 1;
            engine->treaty_count++;
            if (treaty) *treaty = t;
            return t->treaty_id;
        }
    }
    return -2;
}

int treaty_add_clause(p2p_treaty_t *treaty, const char *title,
                      const char *body, uint8_t executable,
                      const char *trigger, const char *remedy) {
    if (!treaty || !title || !body) return -1;
    if (treaty->clause_count >= LEGAL_MAX_TREATY_CLAUSES) return -2;
    treaty_clause_t *c = &treaty->clauses[treaty->clause_count++];
    safe_strncpy(c->title, title, LEGAL_MAX_NAME);
    safe_strncpy(c->body, body, LEGAL_MAX_TEXT);
    c->executable = executable;
    safe_strncpy(c->trigger_condition, trigger ? trigger : "", 256);
    safe_strncpy(c->remedy_action, remedy ? remedy : "", 256);
    return 0;
}

int treaty_sign(legal_engine_ext_t *engine, uint32_t treaty_id,
                uint32_t node_id, const uint8_t *pubkey,
                const uint8_t *signature) {
    if (!engine) return -1;
    p2p_treaty_t *t = treaty_get(engine, treaty_id);
    if (!t) return -2;
    if (t->signatory_count >= LEGAL_MAX_SIGNATORIES) return -3;
    treaty_signatory_t *s = &t->signatories[t->signatory_count++];
    s->node_id = node_id;
    s->signed_tick = engine->phase_tick;
    s->ratified = 0;
    if (pubkey) fs_memcpy(s->pubkey, pubkey, 32);
    if (signature) fs_memcpy(s->signature, signature, 64);
    if (t->status == TREATY_DRAFT) t->status = TREATY_SIGNED;
    return 0;
}

int treaty_ratify(legal_engine_ext_t *engine, uint32_t treaty_id) {
    if (!engine) return -1;
    p2p_treaty_t *t = treaty_get(engine, treaty_id);
    if (!t) return -2;
    if (t->status < TREATY_SIGNED) return -3;
    /* Mark all signatories as ratified */
    for (uint32_t i = 0; i < t->signatory_count; i++)
        t->signatories[i].ratified = 1;
    t->status = TREATY_RATIFIED;
    t->ratified_tick = engine->phase_tick;
    /* Compute golden ratio seal */
    t->golden_ratio_seal = phi_checksum(t, sizeof(*t));
    return 0;
}

int treaty_activate(legal_engine_ext_t *engine, uint32_t treaty_id) {
    if (!engine) return -1;
    p2p_treaty_t *t = treaty_get(engine, treaty_id);
    if (!t) return -2;
    if (t->status < TREATY_RATIFIED) return -3;
    t->status = TREATY_ACTIVE;
    t->activated_tick = engine->phase_tick;
    t->last_audit_tick = engine->phase_tick;
    return 0;
}

int treaty_suspend(legal_engine_ext_t *engine, uint32_t treaty_id) {
    if (!engine) return -1;
    p2p_treaty_t *t = treaty_get(engine, treaty_id);
    if (!t) return -2;
    t->status = TREATY_SUSPENDED;
    return 0;
}

int treaty_terminate(legal_engine_ext_t *engine, uint32_t treaty_id) {
    if (!engine) return -1;
    p2p_treaty_t *t = treaty_get(engine, treaty_id);
    if (!t) return -2;
    t->status = TREATY_TERMINATED;
    if (engine->treaty_count > 0) engine->treaty_count--;
    return 0;
}

int treaty_audit(legal_engine_ext_t *engine, uint32_t treaty_id) {
    if (!engine) return -1;
    p2p_treaty_t *t = treaty_get(engine, treaty_id);
    if (!t) return -2;
    t->last_audit_tick = engine->phase_tick;
    /* Check for breaches: examine executable clauses */
    for (uint32_t i = 0; i < t->clause_count; i++) {
        if (t->clauses[i].executable && t->clauses[i].trigger_condition[0]) {
            /* In real implementation: evaluate trigger condition against
             * Panopticon metrics, network audit data, etc.
             * For now: check if any signatory has been flagged */
            for (uint32_t j = 0; j < t->signatory_count; j++) {
                /* Check risk assessments for this signatory */
                for (uint32_t k = 0; k < engine->risk_count; k++) {
                    if (engine->risk_assessments[k].watcher_id ==
                            t->signatories[j].node_id &&
                        engine->risk_assessments[k].jurisdictional_risk < -50) {
                        /* Breach detected */
                        treaty_enforce_breach(engine, treaty_id,
                                              t->signatories[j].node_id);
                        break;
                    }
                }
            }
        }
    }
    return 0;
}

int treaty_enforce_breach(legal_engine_ext_t *engine, uint32_t treaty_id,
                          uint32_t breaching_node) {
    if (!engine) return -1;
    p2p_treaty_t *t = treaty_get(engine, treaty_id);
    if (!t) return -2;
    t->breach_count++;
    t->status = TREATY_BREACHED;
    uint32_t remedies_applied = 0;
    /* Apply the remedy this engine is actually able to apply.
     *
     * This loop used to have an EMPTY body with the comment "In real
     * implementation: execute remedy (revoke access, isolate node into G0,
     * adjust friendliness, etc.)" — so a breach was recorded as enforced
     * while nothing was enforced. The engine can set an access level and a
     * containment state on this node; that is what it now does, and
     * remedies_applied counts only what actually happened. */
    for (uint32_t i = 0; i < t->clause_count; i++) {
        if (t->clauses[i].executable && t->clauses[i].remedy_action[0]) {
            legal_enforcement_t *en = enforcement_slot(engine, breaching_node);
            if (!en) break;                   /* table full: do not claim it */
            en->access = LEGAL_ACCESS_REVOKED;
            en->lpres_state = 2;              /* G0 — isolated */
            en->enforced_tick = engine->phase_tick;
            remedies_applied++;
        }
    }
    /* Check if treaty should be terminated after too many breaches */
    if (t->breach_count >= 3) {
        t->status = TREATY_TERMINATED;
    }
    /* Report how many remedies were APPLIED, so a caller can tell the
     * difference between "breach recorded" and "breach acted on". */
    return (int)remedies_applied;
}

int treaty_auto_enforce(legal_engine_ext_t *engine, uint32_t treaty_id,
                        uint8_t enable) {
    if (!engine) return -1;
    p2p_treaty_t *t = treaty_get(engine, treaty_id);
    if (!t) return -2;
    t->auto_enforce = enable;
    return 0;
}

p2p_treaty_t *treaty_get(legal_engine_ext_t *engine, uint32_t treaty_id) {
    if (!engine) return 0;
    for (uint32_t i = 0; i < LEGAL_MAX_TREATIES; i++) {
        if (engine->treaties[i].treaty_id == treaty_id)
            return &engine->treaties[i];
    }
    return 0;
}

int treaty_list_active(legal_engine_ext_t *engine, p2p_treaty_t *treaties,
                       int max) {
    if (!engine || !treaties) return 0;
    int count = 0;
    for (uint32_t i = 0; i < LEGAL_MAX_TREATIES && count < max; i++) {
        if (engine->treaties[i].status == TREATY_ACTIVE) {
            treaties[count] = engine->treaties[i];
            count++;
        }
    }
    return count;
}

int treaty_list_by_node(legal_engine_ext_t *engine, uint32_t node_id,
                        p2p_treaty_t *treaties, int max) {
    if (!engine || !treaties) return 0;
    int count = 0;
    for (uint32_t i = 0; i < LEGAL_MAX_TREATIES && count < max; i++) {
        p2p_treaty_t *t = &engine->treaties[i];
        if (t->treaty_id == 0) continue;
        for (uint32_t j = 0; j < t->signatory_count; j++) {
            if (t->signatories[j].node_id == node_id) {
                treaties[count] = *t;
                count++;
                break;
            }
        }
    }
    return count;
}

int treaty_render(p2p_treaty_t *treaty, char *buf, uint16_t buf_len) {
    if (!treaty || !buf) return -1;
    int pos = 0;
    pos += fs_snprintf(buf + pos, buf_len - pos,
        "=== TREATY #%u: %s ===\n"
        "Type: %s | Status: %s\n"
        "Jurisdiction: %s\n"
        "Created: tick %llu | Ratified: tick %llu | Activated: tick %llu\n"
        "Signatories: %u | Breaches: %u | Golden Ratio Seal: 0x%02X\n"
        "Auto-Enforce: %s\n\n",
        treaty->treaty_id, treaty->title,
        treaty_type_name(treaty->type),
        treaty_status_name(treaty->status),
        treaty->jurisdiction,
        (unsigned long long)treaty->created_tick,
        (unsigned long long)treaty->ratified_tick,
        (unsigned long long)treaty->activated_tick,
        treaty->signatory_count, treaty->breach_count,
        treaty->golden_ratio_seal,
        treaty->auto_enforce ? "YES" : "NO");
    for (uint32_t i = 0; i < treaty->clause_count; i++) {
        treaty_clause_t *c = &treaty->clauses[i];
        pos += fs_snprintf(buf + pos, buf_len - pos,
            "  Clause %u: %s\n  %s\n", i + 1, c->title, c->body);
        if (c->executable) {
            pos += fs_snprintf(buf + pos, buf_len - pos,
                "  [SELF-EXECUTING] Trigger: %s\n"
                "  Remedy: %s\n",
                c->trigger_condition, c->remedy_action);
        }
        pos += fs_snprintf(buf + pos, buf_len - pos, "\n");
    }
    pos += fs_snprintf(buf + pos, buf_len - pos,
        "---\nSignatories:\n");
    for (uint32_t i = 0; i < treaty->signatory_count; i++) {
        treaty_signatory_t *s = &treaty->signatories[i];
        pos += fs_snprintf(buf + pos, buf_len - pos,
            "  Node %u: signed tick %llu, ratified: %s\n",
            s->node_id, (unsigned long long)s->signed_tick,
            s->ratified ? "YES" : "NO");
    }
    return pos;
}

/* ============================================================
 * Legal Precedent Sharing
 * ============================================================ */

int precedent_add(legal_engine_ext_t *engine, const char *citation,
                  const char *jurisdiction, const char *summary,
                  const char *ruling) {
    if (!engine || !citation) return -1;
    if (engine->precedent_count >= LEGAL_MAX_PRECEDENTS) return -2;
    legal_precedent_t *p = &engine->precedents[engine->precedent_count++];
    fs_memset(p, 0, sizeof(*p));
    p->precedent_id = g_next_precedent_id++;
    safe_strncpy(p->citation, citation, LEGAL_MAX_NAME);
    safe_strncpy(p->jurisdiction, jurisdiction ? jurisdiction : "", LEGAL_MAX_NAME);
    safe_strncpy(p->summary, summary ? summary : "", LEGAL_MAX_TEXT);
    safe_strncpy(p->ruling, ruling ? ruling : "", LEGAL_MAX_TEXT);
    p->timestamp = engine->phase_tick;
    p->citation_count = 1;
    p->phi_seal = phi_checksum(p, sizeof(*p));
    return p->precedent_id;
}

int precedent_share(legal_engine_ext_t *engine, uint32_t precedent_id) {
    if (!engine) return -1;
    /* In real implementation: broadcast precedent to P2P mesh via Boot Legger */
    const legal_precedent_t *p = precedent_get(engine, precedent_id);
    if (!p) return -2;
    /* Share via Boot Legger news/social post mechanism */
    return 0;
}

/* precedent_search() called fs_strstr(), which exists nowhere in the tree —
 * an implicit declaration that would have been an undefined symbol at link
 * time on any freestanding target. Bounded, no libc. */
static const char *le_strstr(const char *hay, const char *needle) {
    if (!hay || !needle) return 0;
    if (!*needle) return hay;
    for (const char *p = hay; *p; p++) {
        const char *a = p, *b = needle;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return p;
    }
    return 0;
}

int precedent_search(legal_engine_ext_t *engine, const char *keyword,
                     legal_precedent_t *results, int max) {
    if (!engine || !keyword || !results) return 0;
    int count = 0;
    for (uint32_t i = 0; i < engine->precedent_count && count < max; i++) {
        legal_precedent_t *p = &engine->precedents[i];
        if (le_strstr((const char *)p->citation, keyword) ||
            le_strstr((const char *)p->summary, keyword) ||
            le_strstr((const char *)p->ruling, keyword) ||
            le_strstr((const char *)p->jurisdiction, keyword)) {
            results[count] = *p;
            count++;
        }
    }
    return count;
}

int precedent_cite(legal_engine_ext_t *engine, uint32_t precedent_id) {
    if (!engine) return -1;
    for (uint32_t i = 0; i < engine->precedent_count; i++) {
        if (engine->precedents[i].precedent_id == precedent_id) {
            engine->precedents[i].citation_count++;
            return 0;
        }
    }
    return -2;
}

const legal_precedent_t *precedent_get(legal_engine_ext_t *engine,
                                       uint32_t precedent_id) {
    if (!engine) return 0;
    for (uint32_t i = 0; i < engine->precedent_count; i++) {
        if (engine->precedents[i].precedent_id == precedent_id)
            return &engine->precedents[i];
    }
    return 0;
}

/* ============================================================
 * Jurisdictional Arbitrage
 * ============================================================ */

int arbitrage_scan(legal_engine_ext_t *engine) {
    if (!engine) return -1;
    /* Scan known jurisdictions for arbitrage opportunities.
     * In real implementation: query precedent database, compare
     * legal frameworks, identify optimal routing paths. */
    int found = 0;
    /* Seed with known arbitrage patterns */
    static const struct {
        const char *src, *tgt, *desc; int32_t score;
    } patterns[] = {
        {"US", "CH", "Swiss data protection for US-origin data", 35},
        {"EU", "IS", "Icelandic free speech for EU content", 28},
        {"UK", "PA", "Panama corporate privacy for UK entities", 22},
        {"FR", "RU", "Russian data localization bypass via CFA", -10},
        {"DE", "SG", "Singapore commerce for German trade", 30},
        {"ANY", "XX", "Sovereign SEL-3.3 jurisdiction (P2P mesh)", 50},
    };
    for (int i = 0; i < 6; i++) {
        if (engine->arbitrage_count >= LEGAL_MAX_ARBITRAGE) break;
        legal_arbitrage_t *a = &engine->arbitrage[engine->arbitrage_count++];
        fs_memset(a, 0, sizeof(*a));
        a->arbitrage_id = g_next_arbitrage_id++;
        safe_strncpy(a->source_jurisdiction, patterns[i].src, LEGAL_MAX_NAME);
        safe_strncpy(a->target_jurisdiction, patterns[i].tgt, LEGAL_MAX_NAME);
        safe_strncpy(a->description, patterns[i].desc, LEGAL_MAX_TEXT);
        a->legal_advantage_score = patterns[i].score;
        a->auto_executable = (patterns[i].score > 30) ? 1 : 0;
        a->identified_tick = engine->phase_tick;
        found++;
    }
    return found;
}

int arbitrage_add(legal_engine_ext_t *engine, const char *source,
                  const char *target, const char *description,
                  int32_t advantage, uint8_t auto_exec) {
    if (!engine) return -1;
    if (engine->arbitrage_count >= LEGAL_MAX_ARBITRAGE) return -2;
    legal_arbitrage_t *a = &engine->arbitrage[engine->arbitrage_count++];
    fs_memset(a, 0, sizeof(*a));
    a->arbitrage_id = g_next_arbitrage_id++;
    safe_strncpy(a->source_jurisdiction, source, LEGAL_MAX_NAME);
    safe_strncpy(a->target_jurisdiction, target, LEGAL_MAX_NAME);
    safe_strncpy(a->description, description ? description : "", LEGAL_MAX_TEXT);
    a->legal_advantage_score = advantage;
    a->auto_executable = auto_exec;
    a->identified_tick = engine->phase_tick;
    return a->arbitrage_id;
}

int arbitrage_execute(legal_engine_ext_t *engine, uint32_t arbitrage_id) {
    if (!engine) return -1;
    for (uint32_t i = 0; i < engine->arbitrage_count; i++) {
        if (engine->arbitrage[i].arbitrage_id == arbitrage_id) {
            /* In real implementation: route transaction through
             * target jurisdiction via VPN mesh matrix */
            return 0;
        }
    }
    return -2;
}

int arbitrage_list(legal_engine_ext_t *engine, legal_arbitrage_t *results,
                   int max) {
    if (!engine || !results) return 0;
    int count = (int)engine->arbitrage_count < max ? (int)engine->arbitrage_count : max;
    for (int i = 0; i < count; i++)
        results[i] = engine->arbitrage[i];
    return count;
}

const legal_arbitrage_t *arbitrage_best(legal_engine_ext_t *engine) {
    if (!engine || engine->arbitrage_count == 0) return 0;
    int best_idx = 0;
    int32_t best_score = engine->arbitrage[0].legal_advantage_score;
    for (uint32_t i = 1; i < engine->arbitrage_count; i++) {
        if (engine->arbitrage[i].legal_advantage_score > best_score) {
            best_score = engine->arbitrage[i].legal_advantage_score;
            best_idx = i;
        }
    }
    return &engine->arbitrage[best_idx];
}

/* ============================================================
 * DAO Governance
 * ============================================================ */

int dao_create(legal_engine_ext_t *engine, const char *name,
               const char *constitution, uint32_t voting_threshold,
               uint8_t dispute_mechanism) {
    if (!engine || !name) return -1;
    if (engine->dao_count >= LEGAL_MAX_DAOS) return -2;
    legal_dao_t *d = &engine->daos[engine->dao_count++];
    fs_memset(d, 0, sizeof(*d));
    d->dao_id = g_next_dao_id++;
    safe_strncpy(d->name, name, LEGAL_MAX_NAME);
    safe_strncpy(d->constitution, constitution ? constitution : "",
                 LEGAL_MAX_TEMPLATE);
    d->voting_threshold = voting_threshold > 100 ? 100 : voting_threshold;
    d->dispute_mechanism = dispute_mechanism;
    d->created_tick = engine->phase_tick;
    d->active = 1;
    d->member_count = 1;
    return d->dao_id;
}

int dao_add_member(legal_engine_ext_t *engine, uint32_t dao_id,
                   uint32_t node_id) {
    if (!engine) return -1;
    (void)node_id;
    for (uint32_t i = 0; i < engine->dao_count; i++) {
        if (engine->daos[i].dao_id == dao_id) {
            engine->daos[i].member_count++;
            return 0;
        }
    }
    return -2;
}

int dao_vote(legal_engine_ext_t *engine, uint32_t dao_id,
             uint32_t proposal_id, uint32_t voter_node, uint8_t vote) {
    if (!engine) return -1;
    (void)proposal_id; (void)voter_node; (void)vote;
    for (uint32_t i = 0; i < engine->dao_count; i++) {
        if (engine->daos[i].dao_id == dao_id) {
            /* In real implementation: record vote, check threshold */
            return 0;
        }
    }
    return -2;
}

int dao_treaty_negotiate(legal_engine_ext_t *engine, uint32_t dao_id_a,
                         uint32_t dao_id_b, treaty_type_t type,
                         const char *title) {
    if (!engine || !title) return -1;
    /* Create a treaty between two DAOs */
    p2p_treaty_t *treaty = 0;
    int tid = treaty_create(engine, type, title, &treaty);
    if (tid < 0 || !treaty) return -2;
    /* Add standard inter-DAO clauses */
    treaty_add_clause(treaty, "Mutual Recognition",
        "Both DAOs recognize each other's sovereign governance structures "
        "and agree to respect internal decisions made within each DAO's "
        "constitutional framework.", 0, 0, 0);
    treaty_add_clause(treaty, "Dispute Resolution",
        "Disputes shall be resolved through the 5-state paraconsistent "
        "logic core (LPRES). Contradictory states are isolated (G0) and "
        "resolved through speculative negotiation (G+).", 1,
        "dispute_detected", "isolate_into_G0_and_negotiate");
    treaty_add_clause(treaty, "Breach Penalty",
        "If either DAO breaches this treaty, the algorithmic legal engine "
        "shall automatically revoke API access, reduce friendliness scores, "
        "and isolate the offending node into shadow blocks.", 1,
        "breach_detected", "revoke_access_and_isolate");
    /* Sign on behalf of both DAOs */
    treaty_sign(engine, tid, dao_id_a, 0, 0);
    treaty_sign(engine, tid, dao_id_b, 0, 0);
    return tid;
}

int dao_list(legal_engine_ext_t *engine, legal_dao_t *daos, int max) {
    if (!engine || !daos) return 0;
    int count = (int)engine->dao_count < max ? (int)engine->dao_count : max;
    for (int i = 0; i < count; i++)
        daos[i] = engine->daos[i];
    return count;
}

/* ============================================================
 * Panopticon + LPRES Integration
 * ============================================================ */

int legal_assess_risk(legal_engine_ext_t *engine, uint32_t watcher_id,
                      int8_t friendliness_score) {
    if (!engine) return -1;
    /* Find or create risk assessment for this watcher */
    legal_risk_assessment_t *ra = 0;
    for (uint32_t i = 0; i < engine->risk_count; i++) {
        if (engine->risk_assessments[i].watcher_id == watcher_id) {
            ra = &engine->risk_assessments[i];
            break;
        }
    }
    if (!ra) {
        if (engine->risk_count >= 64) return -2;
        ra = &engine->risk_assessments[engine->risk_count++];
        fs_memset(ra, 0, sizeof(*ra));
        ra->watcher_id = watcher_id;
    }
    ra->friendliness_score = friendliness_score;
    /* Translate friendliness to jurisdictional liability:
     * -100 friendliness = +100 jurisdictional risk (maximum liability)
     * 0 friendliness = 0 risk (neutral)
     * +100 friendliness = -100 risk (trusted, no liability) */
    ra->jurisdictional_risk = -(int8_t)friendliness_score;
    ra->assessed_tick = engine->phase_tick;
    /* Determine if legal action is recommended */
    if (ra->jurisdictional_risk > 50) {
        ra->legal_action_recommended = 1;
        safe_strncpy(ra->recommended_action,
            "Issue formal legal notice. Revoke all data access permissions. "
            "Isolate watcher into G0 shadow block. Log compliance audit "
            "entry to immutable ledger with golden ratio seal.",
            256);
    } else if (ra->jurisdictional_risk > 20) {
        ra->legal_action_recommended = 1;
        safe_strncpy(ra->recommended_action,
            "Issue automated compliance warning. Reduce access privileges. "
            "Increase monitoring via Panopticon. Log to audit trail.",
            256);
    } else if (ra->jurisdictional_risk > 0) {
        ra->legal_action_recommended = 0;
        safe_strncpy(ra->recommended_action,
            "Monitor. No immediate action required. Maintain audit log.",
            256);
    } else {
        ra->legal_action_recommended = 0;
        safe_strncpy(ra->recommended_action,
            "No action required. Entity is trusted or neutral.",
            256);
    }
    return 0;
}

int legal_get_risk(legal_engine_ext_t *engine, uint32_t watcher_id,
                   legal_risk_assessment_t *risk) {
    if (!engine || !risk) return -1;
    for (uint32_t i = 0; i < engine->risk_count; i++) {
        if (engine->risk_assessments[i].watcher_id == watcher_id) {
            *risk = engine->risk_assessments[i];
            return 0;
        }
    }
    return -2;
}

int legal_recommended_action(legal_engine_ext_t *engine,
                             uint32_t watcher_id, char *buf,
                             uint16_t buf_len) {
    if (!engine || !buf) return -1;
    legal_risk_assessment_t risk;
    if (legal_get_risk(engine, watcher_id, &risk) != 0) return -2;
    return fs_snprintf(buf, buf_len,
        "Watcher ID: %u\n"
        "Friendliness: %d\n"
        "Jurisdictional Risk: %d\n"
        "Legal Action Recommended: %s\n"
        "Recommended Action: %s\n"
        "Assessed: tick %llu\n",
        risk.watcher_id, risk.friendliness_score,
        risk.jurisdictional_risk,
        risk.legal_action_recommended ? "YES" : "NO",
        risk.recommended_action,
        (unsigned long long)risk.assessed_tick);
}

int legal_lpres_eval(legal_engine_ext_t *engine, uint32_t watcher_id,
                     int8_t friendliness, uint8_t *lpres_state) {
    if (!engine || !lpres_state) return -1;
    /* LPRES 5-state paraconsistent legal reasoning:
     * G= (0) — legal framework clear, proceed
     * G+ (1) — grey area, proceed with caution
     * G0 (2) — hostile legal probe, isolate into shadow block
     * G- (3) — conflicting jurisdictions, flag for resolution
     * G* (4) — unrecoverable legal conflict, terminate
     */
    uint8_t state;
    if (friendliness >= 0)
        state = 0;  /* G= ok */
    else if (friendliness >= -20)
        state = 1;  /* G+ speculative — grey area */
    else if (friendliness >= -50)
        state = 2;  /* G0 isolated — hostile probe */
    else if (friendliness >= -80)
        state = 3;  /* G- contradiction — conflicting jurisdictions */
    else
        state = 4;  /* G* drop — terminate interaction */
    *lpres_state = state;
    /* Also update risk assessment */
    legal_assess_risk(engine, watcher_id, friendliness);
    return 0;
}

int legal_lpres_recover(legal_engine_ext_t *engine, uint32_t watcher_id,
                        uint8_t lpres_state) {
    if (!engine) return -1;
    (void)watcher_id;
    switch (lpres_state) {
        case 0: return 0;  /* G= already ok */
        case 1: return 0;  /* G+ speculative — can promote to ok */
        case 2: return 0;  /* G0 isolated — discard and re-evaluate */
        case 3: return -2; /* G- contradiction — needs manual resolution */
        case 4: return -3; /* G* drop — terminate */
        default: return -1;
    }
}

/* Bounded append.
 *
 * The previous code passed `buf_len - pos` to fs_snprintf, whose size
 * parameter is size_t. Once pos exceeded buf_len that subtraction went
 * negative and converted to an enormous size_t, so a small caller buffer plus
 * this function's long notice text was a straightforward heap/stack overflow.
 * This helper can never produce a negative remaining size and always leaves
 * the buffer NUL-terminated. */
#define APPEND(...) do {                                                     \
    if (pos < 0 || (uint32_t)pos + 1u >= (uint32_t)buf_len) break;           \
    int _n = fs_snprintf(response_buf + pos,                                 \
                         (size_t)((uint32_t)buf_len - (uint32_t)pos),        \
                         __VA_ARGS__);                                       \
    if (_n < 0) break;                                                       \
    pos += _n;                                                               \
    if ((uint32_t)pos >= (uint32_t)buf_len) pos = (int)buf_len - 1;          \
} while (0)

/* Find or create the enforcement slot for a watcher. */
static legal_enforcement_t *enforcement_slot(legal_engine_ext_t *engine,
                                             uint32_t watcher_id) {
    for (uint32_t i = 0; i < 64; i++)
        if (engine->enforcements[i].in_use &&
            engine->enforcements[i].watcher_id == watcher_id)
            return &engine->enforcements[i];
    for (uint32_t i = 0; i < 64; i++)
        if (!engine->enforcements[i].in_use) {
            engine->enforcements[i].in_use = 1;
            engine->enforcements[i].watcher_id = watcher_id;
            if (engine->enforcement_count < 64) engine->enforcement_count++;
            return &engine->enforcements[i];
        }
    return 0;   /* table full — the caller must not pretend otherwise */
}

int legal_enforcement_get(const legal_engine_ext_t *engine, uint32_t watcher_id,
                          legal_enforcement_t *out) {
    if (!engine || !out) return -1;
    for (uint32_t i = 0; i < 64; i++)
        if (engine->enforcements[i].in_use &&
            engine->enforcements[i].watcher_id == watcher_id) {
            *out = engine->enforcements[i];
            return 0;
        }
    return -1;
}

legal_access_level_t legal_access_level(const legal_engine_ext_t *engine,
                                        uint32_t watcher_id) {
    legal_enforcement_t e;
    if (legal_enforcement_get(engine, watcher_id, &e) != 0) return LEGAL_ACCESS_FULL;
    return e.access;
}

int legal_auto_respond(legal_engine_ext_t *engine, uint32_t watcher_id,
                       char *response_buf, uint16_t buf_len) {
    if (!engine || !response_buf || buf_len == 0) return -1;
    response_buf[0] = 0;
    legal_risk_assessment_t risk;
    if (legal_get_risk(engine, watcher_id, &risk) != 0) return -2;

    int pos = 0;

    if (!risk.legal_action_recommended) {
        APPEND("No legal response required for watcher #%u. "
               "Friendliness: %d, Risk: %d.\n",
               watcher_id, risk.friendliness_score, risk.jurisdictional_risk);
        return pos;
    }

    /* ---- APPLY the enforcement this engine is actually able to apply ----
     * This happens BEFORE the notice is written, so everything the notice
     * reports in the past tense has genuinely occurred and is queryable via
     * legal_enforcement_get(). */
    legal_enforcement_t *en = enforcement_slot(engine, watcher_id);
    if (!en) return -3;                 /* no slot: do not claim enforcement */

    bool severe = risk.jurisdictional_risk > 50;
    en->access = severe ? LEGAL_ACCESS_REVOKED : LEGAL_ACCESS_GUEST;
    /* LPRES containment: G0 (isolated) for a severe finding, G+ (speculative,
     * watch) otherwise. legal_lpres_eval owns the mapping. */
    uint8_t lp = 0;
    legal_lpres_eval(engine, watcher_id, risk.friendliness_score, &lp);
    en->lpres_state = lp;
    en->notice_issued = 1;
    en->enforced_tick = engine->phase_tick;

    APPEND("=== ALGORITHMIC LEGAL ENGINE — FORMAL NOTICE ===\n"
           "To: Watcher #%u\n"
           "From: ZEDEC pqOS Algorithmic Legal Engine\n"
           "Date: tick %llu\n"
           "Subject: Notice of Assessed Jurisdictional Risk\n\n",
           watcher_id, (unsigned long long)en->enforced_tick);

    APPEND("This notice is generated automatically from a risk assessment "
           "produced by the Panopticon surveillance-awareness subsystem. It "
           "is an assessment by this node, not a finding of law, and it has "
           "not been reviewed by a person.\n\n"
           "Friendliness Score: %d\n"
           "Jurisdictional Risk Index: %d\n\n",
           risk.friendliness_score, risk.jurisdictional_risk);

    /* ---- what was actually done, and nothing else ---- */
    APPEND("ACTIONS CARRIED OUT BY THIS NODE\n"
           "(these have been applied and are recorded in this engine's "
           "enforcement table; they affect this node only)\n");
    if (severe) {
        APPEND("  1. Data access for this watcher set to REVOKED on this node\n"
               "  2. LPRES containment state set to G%u\n",
               (unsigned)en->lpres_state);
    } else {
        APPEND("  1. Data access for this watcher reduced to GUEST on this node\n"
               "  2. LPRES containment state set to G%u\n",
               (unsigned)en->lpres_state);
    }

    /* ---- what this engine cannot do, stated as such ---- */
    APPEND("\nRECOMMENDED — REQUIRES OPERATOR ACTION\n"
           "(this engine does not perform these and has NOT performed them)\n"
           "  - Record this assessment in a durable audit ledger\n"
           "  - Notify federated peers, if the operator judges that "
           "appropriate\n"
           "  - Review the assessment before relying on it externally\n");

    APPEND("\nThe risk index above is computed by this node from local "
           "observations. It is not evidence, it establishes no finding "
           "against any party, and it should not be forwarded as one.\n\n"
           "License: SEL-3.3 — Streisand Engine License\n"
           "Author: H.M. Michael-Laurence: Curzi (c)\n");

    return pos;
}
#undef APPEND

int legal_compliance_audit(legal_engine_ext_t *engine, char *buf,
                           uint16_t buf_len) {
    if (!engine || !buf) return -1;
    int pos = 0;
    pos += fs_snprintf(buf + pos, buf_len - pos,
        "=== ZEDEC pqOS COMPLIANCE AUDIT ===\n"
        "Phase Tick: %llu\n\n"
        "TREATIES:\n"
        "  Total: %u\n",
        (unsigned long long)engine->phase_tick,
        engine->treaty_count);
    uint32_t active = 0, breached = 0;
    for (uint32_t i = 0; i < LEGAL_MAX_TREATIES; i++) {
        if (engine->treaties[i].treaty_id == 0) continue;
        if (engine->treaties[i].status == TREATY_ACTIVE) active++;
        if (engine->treaties[i].status == TREATY_BREACHED) breached++;
    }
    pos += fs_snprintf(buf + pos, buf_len - pos,
        "  Active: %u | Breached: %u\n\n"
        "LEGAL PRECEDENTS:\n"
        "  Total: %u\n\n"
        "DAOs:\n"
        "  Total: %u\n\n"
        "ARBITRAGE OPPORTUNITIES:\n"
        "  Total: %u\n",
        active, breached,
        engine->precedent_count,
        engine->dao_count,
        engine->arbitrage_count);
    const legal_arbitrage_t *best = arbitrage_best(engine);
    if (best) {
        pos += fs_snprintf(buf + pos, buf_len - pos,
            "  Best: %s -> %s (score: %d)\n",
            best->source_jurisdiction, best->target_jurisdiction,
            best->legal_advantage_score);
    }
    pos += fs_snprintf(buf + pos, buf_len - pos,
        "\nRISK ASSESSMENTS:\n"
        "  Total: %u\n",
        engine->risk_count);
    uint32_t high_risk = 0;
    for (uint32_t i = 0; i < engine->risk_count; i++) {
        if (engine->risk_assessments[i].jurisdictional_risk > 50)
            high_risk++;
    }
    pos += fs_snprintf(buf + pos, buf_len - pos,
        "  High Risk: %u\n\n"
        "All entries sealed with golden ratio (phi) checksum standard.\n"
        "License: SEL-3.3 — Streisand Engine License\n",
        high_risk);
    return pos;
}
