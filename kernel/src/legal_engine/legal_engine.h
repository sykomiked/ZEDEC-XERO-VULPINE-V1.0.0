/*
 * legal_engine.h — ASCW Diplomatic Legal Engine for ZEDEC pqOS
 *
 * Port of the ASCW diplomatic legal code generator into a native
 * C module. Provides legal compliance architecture, treaty templates,
 * user agreement generation, and multi-language legal document support.
 *
 * Integrates with Boot Legger P2P for server user agreements.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 * 36N9 Genetics, LLC — Irrevocable, Interdimensional
 */

#ifndef LEGAL_ENGINE_H
#define LEGAL_ENGINE_H

#include "m5_types.h"

#define LEGAL_MAX_CLAUSES      128
#define LEGAL_MAX_LANGS        6
#define LEGAL_MAX_NATIONS      55
#define LEGAL_MAX_TEXT         4096
#define LEGAL_MAX_NAME         128
#define LEGAL_MAX_TEMPLATE     8192
#define LEGAL_LANG_LEN         3

/* Supported languages */
typedef enum {
    LEGAL_LANG_EN = 0,
    LEGAL_LANG_FR = 1,
    LEGAL_LANG_AR = 2,
    LEGAL_LANG_PT = 3,
    LEGAL_LANG_SW = 4,
    LEGAL_LANG_ES = 5,
} legal_lang_t;

/* Document types */
typedef enum {
    LEGAL_DOC_TREATY        = 0,
    LEGAL_DOC_USER_AGREEMENT = 1,
    LEGAL_DOC_PRIVACY_POLICY = 2,
    LEGAL_DOC_TERMS_OF_USE   = 3,
    LEGAL_DOC_SOVEREIGN_DECL = 4,
    LEGAL_DOC_DIPLOMATIC     = 5,
    LEGAL_DOC_IMPLEMENTATION = 6,
    LEGAL_DOC_RISK_ASSESS    = 7,
    LEGAL_DOC_COST_BENEFIT   = 8,
} legal_doc_type_t;

/* Legal clause */
typedef struct {
    uint8_t  title[LEGAL_MAX_NAME];
    uint8_t  body[LEGAL_MAX_TEXT];
    int      level;  /* 0=top, 1=section, 2=subsection */
} legal_clause_t;

/* Legal document */
typedef struct {
    legal_doc_type_t type;
    legal_lang_t lang;
    uint8_t title[LEGAL_MAX_NAME];
    legal_clause_t clauses[LEGAL_MAX_CLAUSES];
    uint32_t clause_count;
    uint8_t  author[LEGAL_MAX_NAME];
    uint8_t  jurisdiction[LEGAL_MAX_NAME];
    uint64_t timestamp;
} legal_document_t;

/* Nation entry (from ASCW nations database) */
typedef struct {
    uint8_t code[4];
    uint8_t name[LEGAL_MAX_NAME];
    legal_lang_t lang;
    legal_lang_t lang2;
    uint8_t colonial[LEGAL_MAX_TEXT];
    uint8_t independence[64];
    uint8_t resources[LEGAL_MAX_TEXT];
    uint8_t conflicts[LEGAL_MAX_TEXT];
    uint8_t currency[64];
    uint8_t rec[32];
    uint8_t colonial_root[LEGAL_MAX_TEXT];
    uint8_t foreign_exploit[LEGAL_MAX_TEXT];
    uint8_t strengths[LEGAL_MAX_TEXT];
    uint8_t cfa;  /* 1 if CFA franc */
} legal_nation_t;

/* User agreement template (for Carracho P2P servers) */
typedef struct {
    uint8_t  server_name[LEGAL_MAX_NAME];
    uint8_t  operator[LEGAL_MAX_NAME];
    legal_lang_t lang;
    uint8_t  access_rules[LEGAL_MAX_TEXT];
    uint8_t  data_policy[LEGAL_MAX_TEXT];
    uint8_t  prohibited_uses[LEGAL_MAX_TEXT];
    uint8_t  liability[LEGAL_MAX_TEXT];
    uint8_t  dispute_resolution[LEGAL_MAX_TEXT];
    uint8_t  termination[LEGAL_MAX_TEXT];
    uint8_t  custom_clauses[LEGAL_MAX_TEMPLATE];
    uint8_t  sovereign_jurisdiction[LEGAL_MAX_NAME];
    uint64_t created_tick;
    uint8_t  golden_ratio_seal;  /* phi-based checksum */
} legal_user_agreement_t;

/* Legal engine state */
typedef struct {
    legal_nation_t nations[LEGAL_MAX_NATIONS];
    uint32_t nation_count;
    uint64_t phase_tick;
} legal_engine_t;

/* ============================================================
 * Core API
 * ============================================================ */

int legal_engine_init(legal_engine_t *engine);
int legal_engine_tick(legal_engine_t *engine);

/* Nation database */
int legal_engine_load_nations(legal_engine_t *engine);
const legal_nation_t *legal_engine_get_nation(legal_engine_t *engine,
                                              const char *code);
int legal_engine_list_nations(legal_engine_t *engine,
                              legal_nation_t *nations, int max);

/* Document generation */
int legal_gen_treaty(legal_engine_t *engine, legal_lang_t lang,
                     legal_document_t *doc);
int legal_gen_diplomatic(legal_engine_t *engine, const char *nation_code,
                         legal_lang_t lang, legal_document_t *doc);
int legal_gen_risk_assessment(legal_engine_t *engine, const char *nation_code,
                              legal_lang_t lang, legal_document_t *doc);
int legal_gen_implementation(legal_engine_t *engine, const char *nation_code,
                             legal_lang_t lang, legal_document_t *doc);

/* User agreement generation (for Carracho P2P) */
int legal_gen_user_agreement(legal_engine_t *engine,
                             legal_user_agreement_t *ua,
                             const char *server_name,
                             const char *operator_name,
                             legal_lang_t lang);
int legal_gen_default_agreement(legal_engine_t *engine,
                                legal_user_agreement_t *ua,
                                const char *server_name,
                                legal_lang_t lang);
int legal_customize_agreement(legal_user_agreement_t *ua,
                              const char *field,
                              const char *value);
int legal_validate_agreement(legal_user_agreement_t *ua);
int legal_seal_agreement(legal_user_agreement_t *ua);

/* Render document to text */
int legal_render_document(legal_document_t *doc, char *buf, uint16_t buf_len);
int legal_render_agreement(legal_user_agreement_t *ua, char *buf,
                           uint16_t buf_len);

/* Language utilities */
const char *legal_lang_name(legal_lang_t lang);
const char *legal_lang_code(legal_lang_t lang);
legal_lang_t legal_lang_from_code(const char *code);

/* Document type utilities */
const char *legal_doc_type_name(legal_doc_type_t type);

/* ============================================================
 * Peer-to-Peer Treaty Protocol
 * ============================================================ */

#define LEGAL_MAX_TREATIES       64
#define LEGAL_MAX_TREATY_CLAUSES 32
#define LEGAL_MAX_SIGNATORIES    16
#define LEGAL_MAX_PRECEDENTS     128
#define LEGAL_MAX_DAOS           32
#define LEGAL_MAX_ARBITRAGE      64

/* Treaty status */
typedef enum {
    TREATY_DRAFT       = 0,
    TREATY_NEGOTIATING = 1,
    TREATY_SIGNED      = 2,
    TREATY_RATIFIED    = 3,
    TREATY_ACTIVE      = 4,
    TREATY_SUSPENDED   = 5,
    TREATY_BREACHED    = 6,
    TREATY_TERMINATED  = 7,
} treaty_status_t;

/* Treaty types */
typedef enum {
    TREATY_BILATERAL      = 0,
    TREATY_MULTILATERAL   = 1,
    TREATY_NON_AGGRESSION = 2,
    TREATY_DATA_SHARING   = 3,
    TREATY_RESOURCE_ALLOC = 4,
    TREATY_MUTUAL_DEFENSE = 5,
    TREATY_COMMERCE       = 6,
    TREATY_EXTRADITION    = 7,
} treaty_type_t;

/* Treaty clause — self-executing smart contract */
typedef struct {
    uint8_t  title[LEGAL_MAX_NAME];
    uint8_t  body[LEGAL_MAX_TEXT];
    uint8_t  executable;          /* 1 = self-executing smart contract */
    uint8_t  trigger_condition[256]; /* condition that triggers execution */
    uint8_t  remedy_action[256];  /* action to take when triggered */
} treaty_clause_t;

/* Treaty signatory */
typedef struct {
    uint32_t node_id;
    uint8_t  pubkey[32];
    uint8_t  signature[64];
    uint64_t signed_tick;
    uint8_t  ratified;
} treaty_signatory_t;

/* Peer-to-peer treaty */
typedef struct {
    uint32_t treaty_id;
    treaty_type_t type;
    treaty_status_t status;
    uint8_t  title[LEGAL_MAX_NAME];
    treaty_clause_t clauses[LEGAL_MAX_TREATY_CLAUSES];
    uint32_t clause_count;
    treaty_signatory_t signatories[LEGAL_MAX_SIGNATORIES];
    uint32_t signatory_count;
    uint64_t created_tick;
    uint64_t ratified_tick;
    uint64_t activated_tick;
    uint8_t  golden_ratio_seal;
    uint8_t  jurisdiction[LEGAL_MAX_NAME];
    /* Enforcement state */
    uint8_t  auto_enforce;       /* 1 = automatic enforcement enabled */
    uint32_t breach_count;
    uint64_t last_audit_tick;
} p2p_treaty_t;

/* Legal precedent entry */
typedef struct {
    uint32_t precedent_id;
    uint8_t  citation[LEGAL_MAX_NAME];
    uint8_t  jurisdiction[LEGAL_MAX_NAME];
    uint8_t  summary[LEGAL_MAX_TEXT];
    uint8_t  ruling[LEGAL_MAX_TEXT];
    uint64_t timestamp;
    uint8_t  phi_seal;           /* golden ratio checksum */
    uint32_t citation_count;     /* how many nodes cite this */
} legal_precedent_t;

/* DAO governance module */
typedef struct {
    uint32_t dao_id;
    uint8_t  name[LEGAL_MAX_NAME];
    uint8_t  constitution[LEGAL_MAX_TEMPLATE];
    uint32_t member_count;
    uint32_t voting_threshold;   /* percentage for approval (0-100) */
    uint8_t  dispute_mechanism;  /* 0=LPRES, 1=arbitration, 2=vote */
    uint64_t created_tick;
    uint8_t  active;
} legal_dao_t;

/* Jurisdictional arbitrage opportunity */
typedef struct {
    uint32_t arbitrage_id;
    uint8_t  source_jurisdiction[LEGAL_MAX_NAME];
    uint8_t  target_jurisdiction[LEGAL_MAX_NAME];
    uint8_t  description[LEGAL_MAX_TEXT];
    int32_t  legal_advantage_score; /* benefit score of routing through this */
    uint8_t  auto_executable;
    uint64_t identified_tick;
} legal_arbitrage_t;

/* Legal risk assessment (Panopticon integration) */
typedef struct {
    uint32_t watcher_id;
    int8_t   friendliness_score;   /* from Panopticon */
    int8_t   jurisdictional_risk;  /* legal liability index */
    uint8_t  legal_action_recommended;
    uint8_t  recommended_action[256];
    uint64_t assessed_tick;
} legal_risk_assessment_t;

/* ============================================================
 * Enforcement record
 *
 * WHY THIS TYPE EXISTS
 * --------------------
 * legal_auto_respond() used to emit a formal notice asserting, in the PAST
 * TENSE, that six enforcement actions "have been executed": access revoked,
 * the node isolated into a G0 shadow block, an entry written to an immutable
 * ledger, a phi seal applied, a notice broadcast to the P2P mesh, and
 * friendliness adjusted across all connected nodes. None of it happened. The
 * function's entire body was string formatting. An operator forwarding that
 * document to a counterparty would have been transmitting a false statement
 * of fact.
 *
 * The fix is not to soften the wording. It is to make the engine ACTUALLY DO
 * what it is capable of doing, record it here, and then report exactly that —
 * and to list everything it cannot do as a RECOMMENDATION requiring an
 * operator, clearly separated from what was carried out.
 *
 * What the engine can genuinely do, in process, is set an access level and an
 * LPRES containment state and stamp when it did so. That is what this record
 * holds, and it is queryable via legal_enforcement_get() so a caller can
 * verify the notice against the state rather than trusting the prose.
 * ============================================================ */

typedef enum {
    LEGAL_ACCESS_FULL = 0,
    LEGAL_ACCESS_GUEST,      /* reduced privileges                        */
    LEGAL_ACCESS_REVOKED     /* no data access                            */
} legal_access_level_t;

typedef struct {
    uint32_t watcher_id;
    uint8_t  in_use;
    legal_access_level_t access;   /* what was actually applied           */
    uint8_t  lpres_state;          /* containment state actually set      */
    uint8_t  notice_issued;        /* a formal notice was generated       */
    uint64_t enforced_tick;        /* when                                */
} legal_enforcement_t;

/* Extended legal engine state with treaty/precedent/DAO/arbitrage */
typedef struct {
    legal_nation_t nations[LEGAL_MAX_NATIONS];
    uint32_t nation_count;
    uint64_t phase_tick;
    /* Treaty registry */
    p2p_treaty_t treaties[LEGAL_MAX_TREATIES];
    uint32_t treaty_count;
    /* Legal precedent database */
    legal_precedent_t precedents[LEGAL_MAX_PRECEDENTS];
    uint32_t precedent_count;
    /* DAO registry */
    legal_dao_t daos[LEGAL_MAX_DAOS];
    uint32_t dao_count;
    /* Arbitrage opportunities */
    legal_arbitrage_t arbitrage[LEGAL_MAX_ARBITRAGE];
    uint32_t arbitrage_count;
    /* Active risk assessments */
    legal_risk_assessment_t risk_assessments[64];
    uint32_t risk_count;
    /* Enforcement actually applied — see legal_enforcement_t above */
    legal_enforcement_t enforcements[64];
    uint32_t enforcement_count;
} legal_engine_ext_t;

/* ============================================================
 * Treaty Protocol API
 * ============================================================ */

/* Treaty lifecycle */
int treaty_init(legal_engine_ext_t *engine);
int treaty_create(legal_engine_ext_t *engine, treaty_type_t type,
                  const char *title, p2p_treaty_t **treaty);
int treaty_add_clause(p2p_treaty_t *treaty, const char *title,
                      const char *body, uint8_t executable,
                      const char *trigger, const char *remedy);
int treaty_sign(legal_engine_ext_t *engine, uint32_t treaty_id,
                uint32_t node_id, const uint8_t *pubkey,
                const uint8_t *signature);
int treaty_ratify(legal_engine_ext_t *engine, uint32_t treaty_id);
int treaty_activate(legal_engine_ext_t *engine, uint32_t treaty_id);
int treaty_suspend(legal_engine_ext_t *engine, uint32_t treaty_id);
int treaty_terminate(legal_engine_ext_t *engine, uint32_t treaty_id);

/* Treaty enforcement */
int treaty_audit(legal_engine_ext_t *engine, uint32_t treaty_id);
int treaty_enforce_breach(legal_engine_ext_t *engine, uint32_t treaty_id,
                          uint32_t breaching_node);
int treaty_auto_enforce(legal_engine_ext_t *engine, uint32_t treaty_id,
                        uint8_t enable);

/* Treaty queries */
p2p_treaty_t *treaty_get(legal_engine_ext_t *engine, uint32_t treaty_id);
int treaty_list_active(legal_engine_ext_t *engine, p2p_treaty_t *treaties,
                       int max);
int treaty_list_by_node(legal_engine_ext_t *engine, uint32_t node_id,
                        p2p_treaty_t *treaties, int max);

/* Treaty rendering */
int treaty_render(p2p_treaty_t *treaty, char *buf, uint16_t buf_len);

/* ============================================================
 * Legal Precedent Sharing API
 * ============================================================ */

int precedent_add(legal_engine_ext_t *engine, const char *citation,
                  const char *jurisdiction, const char *summary,
                  const char *ruling);
int precedent_share(legal_engine_ext_t *engine, uint32_t precedent_id);
int precedent_search(legal_engine_ext_t *engine, const char *keyword,
                     legal_precedent_t *results, int max);
int precedent_cite(legal_engine_ext_t *engine, uint32_t precedent_id);
const legal_precedent_t *precedent_get(legal_engine_ext_t *engine,
                                       uint32_t precedent_id);

/* ============================================================
 * Jurisdictional Arbitrage API
 * ============================================================ */

int arbitrage_scan(legal_engine_ext_t *engine);
int arbitrage_add(legal_engine_ext_t *engine, const char *source,
                  const char *target, const char *description,
                  int32_t advantage, uint8_t auto_exec);
int arbitrage_execute(legal_engine_ext_t *engine, uint32_t arbitrage_id);
int arbitrage_list(legal_engine_ext_t *engine, legal_arbitrage_t *results,
                   int max);
const legal_arbitrage_t *arbitrage_best(legal_engine_ext_t *engine);

/* ============================================================
 * DAO Governance API
 * ============================================================ */

int dao_create(legal_engine_ext_t *engine, const char *name,
               const char *constitution, uint32_t voting_threshold,
               uint8_t dispute_mechanism);
int dao_add_member(legal_engine_ext_t *engine, uint32_t dao_id,
                   uint32_t node_id);
int dao_vote(legal_engine_ext_t *engine, uint32_t dao_id,
             uint32_t proposal_id, uint32_t voter_node, uint8_t vote);
int dao_treaty_negotiate(legal_engine_ext_t *engine, uint32_t dao_id_a,
                         uint32_t dao_id_b, treaty_type_t type,
                         const char *title);
int dao_list(legal_engine_ext_t *engine, legal_dao_t *daos, int max);

/* ============================================================
 * Panopticon + LPRES Integration API
 * ============================================================ */

/* Translate Panopticon friendliness score into legal liability index */
int legal_assess_risk(legal_engine_ext_t *engine, uint32_t watcher_id,
                      int8_t friendliness_score);
int legal_get_risk(legal_engine_ext_t *engine, uint32_t watcher_id,
                   legal_risk_assessment_t *risk);
int legal_recommended_action(legal_engine_ext_t *engine,
                             uint32_t watcher_id, char *buf,
                             uint16_t buf_len);

/* LPRES paraconsistent legal reasoning:
 * G= (ok)         — legal framework clear, proceed
 * G+ (speculative) — grey area, proceed with caution
 * G0 (isolated)   — hostile legal probe, isolate into shadow block
 * G- (contradict)  — conflicting jurisdictions, flag for resolution
 * G* (drop)        — unrecoverable legal conflict, terminate interaction
 */
int legal_lpres_eval(legal_engine_ext_t *engine, uint32_t watcher_id,
                     int8_t friendliness, uint8_t *lpres_state);
int legal_lpres_recover(legal_engine_ext_t *engine, uint32_t watcher_id,
                        uint8_t lpres_state);

/* Autonomous legal response.
 *
 * Applies the enforcement the engine is actually capable of — an access level
 * and an LPRES containment state — records it in engine->enforcements, and
 * writes a formal notice that reports THAT, and only that, as carried out.
 * Anything requiring a component the engine does not drive (ledger, mesh
 * broadcast, cross-node reputation) appears under a separate RECOMMENDED
 * heading marked as requiring an operator.
 *
 * Returns the number of characters written, or negative on error. The notice
 * is always NUL-terminated and never exceeds buf_len.
 *
 * Verify the notice against the state with legal_enforcement_get(): if the
 * document says access was revoked, that call will show it. */
int legal_auto_respond(legal_engine_ext_t *engine, uint32_t watcher_id,
                       char *response_buf, uint16_t buf_len);

/* Read back what enforcement was actually applied to a watcher.
 * Returns 0 and fills *out if a record exists, -1 if none. */
int legal_enforcement_get(const legal_engine_ext_t *engine, uint32_t watcher_id,
                          legal_enforcement_t *out);

/* The access level currently in force for a watcher. Defaults to
 * LEGAL_ACCESS_FULL when no enforcement has been applied. */
legal_access_level_t legal_access_level(const legal_engine_ext_t *engine,
                                        uint32_t watcher_id);

/* Compliance audit — check all active treaties and agreements */
int legal_compliance_audit(legal_engine_ext_t *engine, char *buf,
                           uint16_t buf_len);

/* ============================================================
 * Extended Engine Initialization
 * ============================================================ */

int legal_engine_ext_init(legal_engine_ext_t *engine);
int legal_engine_ext_tick(legal_engine_ext_t *engine);

#endif /* LEGAL_ENGINE_H */
