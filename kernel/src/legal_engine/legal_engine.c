/*
 * legal_engine.c — ASCW Diplomatic Legal Engine for ZEDEC pqOS
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#include "legal_engine.h"
#include "../include/freestanding.h"

static void safe_strncpy(uint8_t *dst, const char *src, int max) {
    int i = 0;
    if (src) { while (src[i] && i < max - 1) { dst[i] = src[i]; i++; } }
    dst[i] = 0;
}

static int safe_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

int legal_engine_init(legal_engine_t *engine) {
    if (!engine) return -1;
    fs_memset(engine, 0, sizeof(*engine));
    legal_engine_load_nations(engine);
    return 0;
}

int legal_engine_tick(legal_engine_t *engine) {
    if (!engine) return -1;
    engine->phase_tick++;
    return 0;
}

int legal_engine_load_nations(legal_engine_t *engine) {
    if (!engine) return -1;
    /* Load a representative subset of the 55 AU nations */
    static const struct {
        const char *code, *name, *lang, *lang2, *colonial, *independence;
        const char *resources, *conflicts, *currency, *rec, *colonial_root;
        const char *foreign_exploit, *strengths; uint8_t cfa;
    } data[] = {
        {"DZ","Algeria","ar","fr","France (1830-1962)","5 July 1962",
         "Oil, natural gas, phosphates","Berber identity; political repression",
         "Algerian Dinar","UMA","French colonization created extractive oil/gas economy",
         "France, China, EU","Largest country in Africa; major oil/gas reserves",0},
        {"AO","Angola","pt","","Portugal (1575-1975)","11 November 1975",
         "Oil, diamonds, iron ore","Civil war legacy; Cabinda separatism",
         "Angolan Kwanza","SADC","Portuguese colonialism created extractive oil/diamond economy",
         "China, US, France","Major oil producer; diamond wealth",0},
        {"BJ","Benin","fr","","France (1892-1960)","1 August 1960",
         "Cotton, limestone, timber","Economic vulnerability; regional inequality",
         "CFA Franc (XOF)","ECOWAS","French colonialism created cotton export economy; CFA franc dependency",
         "China, EU","Stable democracy; cotton production",1},
        {"BW","Botswana","en","","Britain (1885-1966)","30 September 1966",
         "Diamonds, copper, nickel","San marginalization; diamond dependency",
         "Botswana Pula","SADC","British colonialism created diamond-dependent economy",
         "De Beers, China","Africa's most stable democracy; diamond wealth",0},
        {"BF","Burkina Faso","fr","","France (1896-1960)","5 August 1960",
         "Gold, cotton, manganese","Sahel insurgency; military coups",
         "CFA Franc (XOF)","AES","French colonialism created cotton/gold extractive economy",
         "France, Russia, China","Gold production; revolutionary tradition (Sankara)",1},
        {"CM","Cameroon","fr","en","Germany/France/Britain (1884-1960)","1 January 1960",
         "Oil, timber, bauxite","Anglophone crisis; Boko Haram",
         "Central African CFA Franc","ECCAS","German then Franco-British colonialism; Anglophone marginalization",
         "France, China, US","Bilingual heritage; oil wealth; cobalt deposits",1},
        {"CD","DR Congo","fr","","Belgium (1885-1960)","30 June 1960",
         "Cobalt (70% global), copper, coltan","Eastern armed conflict; Rwanda proxy",
         "Congolese Franc","SADC/ECCAS","Belgian colonialism created extreme extractive economy",
         "US, Rwanda, China","70% global cobalt; massive copper; hydroelectric",0},
        {"EG","Egypt","ar","en","Britain (1882-1952)","23 July 1952",
         "Natural gas, oil, Suez Canal","Sinai insurgency; Nile waters (GERD)",
         "Egyptian Pound","CEN-SAD","British colonialism created Suez Canal extraction economy",
         "US, IMF, China","Suez Canal; natural gas; Arab League HQ",0},
        {"ET","Ethiopia","en","ar","Italy (partial 1936-1941)","Ancient sovereignty",
         "Gold, coffee, geothermal","Tigray aftermath; ethnic federalism",
         "Ethiopian Birr","IGAD","Italian occupation; ethnic federalism divisions",
         "China, UAE, US","Second-largest population; coffee origin; BRICS+ member",0},
        {"GH","Ghana","en","","Britain (1844-1957)","6 March 1957",
         "Gold, oil, cocoa","Northern conflicts; galamsey; election tensions",
         "Ghanaian Cedi","ECOWAS","British colonialism created gold/cocoa extractive economy",
         "China, Newmont, IMF","Major gold producer; oil/gas; stable democracy",0},
        {"KE","Kenya","en","sw","Britain (1895-1963)","12 December 1963",
         "Tea, coffee, geothermal, tourism","Election violence; al-Shabaab",
         "Kenyan Shilling","EAC, IGAD","British colonialism created tea/coffee plantation economy",
         "China, US, UK","East Africa's financial hub; M-Pesa; Silicon Savannah",0},
        {"LY","Libya","ar","en","Italy (1911-1951)","24 December 1951",
         "Oil (largest in Africa)","Civil war; government division",
         "Libyan Dinar","UMA","Italian colonialism created extractive oil economy",
         "Russia, Turkey, UAE","Largest oil reserves in Africa",0},
        {"ML","Mali","fr","","France (1892-1960)","22 September 1960",
         "Gold (3rd largest African), uranium","Sahel insurgency; coups",
         "CFA Franc (XOF)","AES","French colonialism created extractive gold/cotton economy",
         "France, Russia, China","Gold production; Timbuktu heritage; AES leadership",1},
        {"MA","Morocco","ar","fr","France/Spain (1912-1956)","2 March 1956",
         "Phosphates (70% world), tourism","Western Sahara dispute; Rif protests",
         "Moroccan Dirham","UMA","French/Spanish colonialism created phosphate/agricultural economy",
         "France, EU, China","70% world phosphates; tourism; Noor solar",0},
        {"NG","Nigeria","en","","Britain (1861-1960)","1 October 1960",
         "Oil (largest African), natural gas","Boko Haram; banditry; IPOB",
         "Nigerian Naira","ECOWAS","British colonialism created extractive oil economy; 1914 amalgamation",
         "Shell, Chevron, China","Largest economy/population; largest oil; Nollywood",0},
        {"NE","Niger","fr","","France (1900-1960)","3 August 1960",
         "Uranium (5th largest), oil","Military coup; Sahel insurgency",
         "CFA Franc (XOF)","AES","French colonialism created extractive uranium economy",
         "France (expelled), Russia","Uranium (40% of French nuclear); AES leadership",1},
        {"RW","Rwanda","en","fr","Germany/Belgium (1899-1962)","1 July 1962",
         "Tin, coltan, methane (Lake Kivu)","Genocide legacy; DRC proxy",
         "Rwandan Franc","EAC, ECCAS","Belgian colonialism engineered Hutu-Tutsi division",
         "China, DRC mineral proxy","Excellent governance; tech sector; gorilla tourism",0},
        {"ZA","South Africa","en","","Netherlands/Britain (1652-1994)","27 April 1994",
         "Gold, platinum (80% global), diamonds","Inequality; land redistribution",
         "South African Rand","SADC","Dutch/British colonialism created racial hierarchy; apartheid",
         "UK, China, US","Most industrialized; 80% platinum; BRICS/G20",0},
        {"SD","Sudan","ar","en","Britain/Anglo-Egyptian (1899-1956)","1 January 1956",
         "Oil, gold, gum arabic (80% world)","Civil war (SAF vs RSF); Darfur legacy",
         "Sudanese Pound","IGAD","British colonialism created North-South divide",
         "Russia, UAE, Egypt","Gold; oil; gum arabic (80% world)",0},
        {"TZ","Tanzania","en","sw","Germany/Britain (1885-1961)","9 December 1961",
         "Gold, tanzanite, natural gas","Zanzibar union tensions; political dominance",
         "Tanzanian Shilling","SADC, EAC","German/British colonialism created extractive sisal/cotton economy",
         "China, Barrick","Gold; tanzanite (unique); tourism; EAC+SADC hub",0},
        {"TN","Tunisia","ar","fr","France (1881-1956)","20 March 1956",
         "Phosphates, olive oil, tourism","Post-Arab Spring instability; economic crisis",
         "Tunisian Dinar","UMA","French colonialism created phosphate/olive oil economy",
         "EU, IMF, China","Phosphates; most educated Arab population; Arab Spring birthplace",0},
        {"UG","Uganda","en","sw","Britain (1894-1962)","9 October 1962",
         "Oil (Lake Albert), gold, coffee","Political repression; LRA legacy",
         "Ugandan Shilling","EAC, IGAD","British colonialism created extractive coffee/cotton economy",
         "China, TotalEnergies","Oil (EACOP); coffee; Nile hydropower; gorillas",0},
        {"ZM","Zambia","en","","Britain (1899-1964)","24 October 1964",
         "Copper (major global), cobalt","Political polarization; debt crisis",
         "Zambian Kwacha","SADC","British colonialism created extractive copper economy",
         "China, First Quantum","Major copper; cobalt; Victoria Falls",0},
        {"ZW","Zimbabwe","en","","Britain (1890-1980)","18 April 1980",
         "Platinum (2nd largest), gold, lithium","Political polarization; sanctions",
         "Zimbabwe Gold (ZiG)","SADC","British colonialism created white settler economy; Land Apportionment Act",
         "UK, US (ZDERA), China","2nd largest platinum; lithium; sanctions bypass test case",0},
    };
    int count = sizeof(data) / sizeof(data[0]);
    for (int i = 0; i < count && i < LEGAL_MAX_NATIONS; i++) {
        legal_nation_t *n = &engine->nations[i];
        fs_memset(n, 0, sizeof(*n));
        safe_strncpy(n->code, data[i].code, 4);
        safe_strncpy(n->name, data[i].name, LEGAL_MAX_NAME);
        n->lang = legal_lang_from_code(data[i].lang);
        n->lang2 = legal_lang_from_code(data[i].lang2);
        safe_strncpy(n->colonial, data[i].colonial, LEGAL_MAX_TEXT);
        safe_strncpy(n->independence, data[i].independence, 64);
        safe_strncpy(n->resources, data[i].resources, LEGAL_MAX_TEXT);
        safe_strncpy(n->conflicts, data[i].conflicts, LEGAL_MAX_TEXT);
        safe_strncpy(n->currency, data[i].currency, 64);
        safe_strncpy(n->rec, data[i].rec, 32);
        safe_strncpy(n->colonial_root, data[i].colonial_root, LEGAL_MAX_TEXT);
        safe_strncpy(n->foreign_exploit, data[i].foreign_exploit, LEGAL_MAX_TEXT);
        safe_strncpy(n->strengths, data[i].strengths, LEGAL_MAX_TEXT);
        n->cfa = data[i].cfa;
    }
    engine->nation_count = count;
    return count;
}

const legal_nation_t *legal_engine_get_nation(legal_engine_t *engine,
                                              const char *code) {
    if (!engine || !code) return 0;
    for (uint32_t i = 0; i < engine->nation_count; i++) {
        if (safe_strcmp((const char *)engine->nations[i].code, code) == 0)
            return &engine->nations[i];
    }
    return 0;
}

int legal_engine_list_nations(legal_engine_t *engine,
                              legal_nation_t *nations, int max) {
    if (!engine || !nations) return 0;
    int count = (int)engine->nation_count < max ? (int)engine->nation_count : max;
    for (int i = 0; i < count; i++)
        nations[i] = engine->nations[i];
    return count;
}

int legal_gen_treaty(legal_engine_t *engine, legal_lang_t lang,
                     legal_document_t *doc) {
    if (!engine || !doc) return -1;
    fs_memset(doc, 0, sizeof(*doc));
    doc->type = LEGAL_DOC_TREATY;
    doc->lang = lang;
    doc->timestamp = engine->phase_tick;
    safe_strncpy(doc->title, "ZEDEC pqOS Sovereign Treaty Framework", LEGAL_MAX_NAME);
    safe_strncpy(doc->author, "H.M. Michael-Laurence: Curzi (c)", LEGAL_MAX_NAME);
    safe_strncpy(doc->jurisdiction, "Sovereign — SEL-3.3", LEGAL_MAX_NAME);
    int c = 0;
    safe_strncpy(doc->clauses[c].title, "Article I: Sovereign Declaration", LEGAL_MAX_NAME);
    safe_strncpy(doc->clauses[c].body,
        "This treaty establishes the sovereign digital infrastructure framework "
        "under the Streisand Engine License (SEL-3.3). All participating nodes "
        "recognize the irrevocable, interdimensional authority of the signatory.", LEGAL_MAX_TEXT);
    doc->clauses[c].level = 1; c++;
    safe_strncpy(doc->clauses[c].title, "Article II: Mutual Recognition", LEGAL_MAX_NAME);
    safe_strncpy(doc->clauses[c].body,
        "Parties shall mutually recognize the digital sovereignty of all "
        "connected nodes. No party shall impose external surveillance, "
        "censorship, or data extraction upon another party without explicit "
        "cryptographic consent.", LEGAL_MAX_TEXT);
    doc->clauses[c].level = 1; c++;
    safe_strncpy(doc->clauses[c].title, "Article III: Golden Ratio Integrity", LEGAL_MAX_NAME);
    safe_strncpy(doc->clauses[c].body,
        "All treaty documents, communications, and transactions shall be "
        "verified using the golden ratio (phi) checksum standard. Documents "
        "failing phi-verification are null and void.", LEGAL_MAX_TEXT);
    doc->clauses[c].level = 1; c++;
    safe_strncpy(doc->clauses[c].title, "Article IV: Paraconsistent Dispute Resolution", LEGAL_MAX_NAME);
    safe_strncpy(doc->clauses[c].body,
        "Disputes shall be resolved through the 5-state paraconsistent logic "
        "core (LPRES). Contradictory states are isolated, not escalated. "
        "Speculative resolutions (G+) are pursued cooperatively.", LEGAL_MAX_TEXT);
    doc->clauses[c].level = 1; c++;
    safe_strncpy(doc->clauses[c].title, "Article V: Boot Legger P2P Communications", LEGAL_MAX_NAME);
    safe_strncpy(doc->clauses[c].body,
        "All inter-node communications shall utilize direct-stream peer "
        "synchronization via the Boot Legger P2P transport layer. "
        "Garlic routing (PungentClove) wraps all traffic for metadata "
        "obfuscation. No centralized servers are required.", LEGAL_MAX_TEXT);
    doc->clauses[c].level = 1; c++;
    doc->clause_count = c;
    return 0;
}

int legal_gen_diplomatic(legal_engine_t *engine, const char *nation_code,
                         legal_lang_t lang, legal_document_t *doc) {
    if (!engine || !doc) return -1;
    const legal_nation_t *n = legal_engine_get_nation(engine, nation_code);
    fs_memset(doc, 0, sizeof(*doc));
    doc->type = LEGAL_DOC_DIPLOMATIC;
    doc->lang = lang;
    doc->timestamp = engine->phase_tick;
    if (n) {
        safe_strncpy(doc->title, (const char *)n->name, LEGAL_MAX_NAME);
    } else {
        safe_strncpy(doc->title, "Diplomatic Package", LEGAL_MAX_NAME);
    }
    int c = 0;
    if (n) {
        safe_strncpy(doc->clauses[c].title, "Executive Summary", LEGAL_MAX_NAME);
        fs_snprintf((char *)doc->clauses[c].body, LEGAL_MAX_TEXT,
            "Nation: %s | Currency: %s | Resources: %s | CFA: %s",
            n->name, n->currency, n->resources, n->cfa ? "Yes" : "No");
        doc->clauses[c].level = 1; c++;
        safe_strncpy(doc->clauses[c].title, "Colonial History", LEGAL_MAX_NAME);
        safe_strncpy(doc->clauses[c].body, (const char *)n->colonial, LEGAL_MAX_TEXT);
        doc->clauses[c].level = 1; c++;
        safe_strncpy(doc->clauses[c].title, "Colonial Root Cause", LEGAL_MAX_NAME);
        safe_strncpy(doc->clauses[c].body, (const char *)n->colonial_root, LEGAL_MAX_TEXT);
        doc->clauses[c].level = 1; c++;
        safe_strncpy(doc->clauses[c].title, "Foreign Exploitation", LEGAL_MAX_NAME);
        safe_strncpy(doc->clauses[c].body, (const char *)n->foreign_exploit, LEGAL_MAX_TEXT);
        doc->clauses[c].level = 1; c++;
        safe_strncpy(doc->clauses[c].title, "National Strengths", LEGAL_MAX_NAME);
        safe_strncpy(doc->clauses[c].body, (const char *)n->strengths, LEGAL_MAX_TEXT);
        doc->clauses[c].level = 1; c++;
    }
    doc->clause_count = c;
    return 0;
}

int legal_gen_risk_assessment(legal_engine_t *engine, const char *nation_code,
                              legal_lang_t lang, legal_document_t *doc) {
    if (!engine || !doc) return -1;
    const legal_nation_t *n = legal_engine_get_nation(engine, nation_code);
    fs_memset(doc, 0, sizeof(*doc));
    doc->type = LEGAL_DOC_RISK_ASSESS;
    doc->lang = lang;
    doc->timestamp = engine->phase_tick;
    safe_strncpy(doc->title, "Risk Assessment", LEGAL_MAX_NAME);
    int c = 0;
    if (n) {
        safe_strncpy(doc->clauses[c].title, "Current Challenges", LEGAL_MAX_NAME);
        safe_strncpy(doc->clauses[c].body, (const char *)n->conflicts, LEGAL_MAX_TEXT);
        doc->clauses[c].level = 1; c++;
        safe_strncpy(doc->clauses[c].title, "Rebuttal", LEGAL_MAX_NAME);
        fs_snprintf((char *)doc->clauses[c].body, LEGAL_MAX_TEXT,
            "The ZEDEC pqOS framework addresses these challenges through "
            "sovereign digital infrastructure, golden ratio verification, "
            "and paraconsistent governance. %s retains full control.",
            n->name);
        doc->clauses[c].level = 2; c++;
    }
    doc->clause_count = c;
    return 0;
}

int legal_gen_implementation(legal_engine_t *engine, const char *nation_code,
                             legal_lang_t lang, legal_document_t *doc) {
    if (!engine || !doc) return -1;
    (void)nation_code;   /* the generated text is jurisdiction-neutral */
    fs_memset(doc, 0, sizeof(*doc));
    doc->type = LEGAL_DOC_IMPLEMENTATION;
    doc->lang = lang;
    doc->timestamp = engine->phase_tick;
    safe_strncpy(doc->title, "Implementation Plan", LEGAL_MAX_NAME);
    int c = 0;
    const char *phases[] = {
        "Phase 1: Infrastructure deployment and node initialization",
        "Phase 2: P2P mesh network establishment and Carracho protocol activation",
        "Phase 3: Sovereign currency integration and VINO bridge deployment",
        "Phase 4: Full governance transition and treaty activation"
    };
    for (int i = 0; i < 4; i++) {
        safe_strncpy(doc->clauses[c].title, phases[i], LEGAL_MAX_NAME);
        safe_strncpy(doc->clauses[c].body,
            "Deploy ZEDEC pqOS nodes with Carracho-style P2P transport, "
            "garlic routing, and golden ratio verification. Establish "
            "sovereign communications grid.", LEGAL_MAX_TEXT);
        doc->clauses[c].level = 1; c++;
    }
    doc->clause_count = c;
    return 0;
}

int legal_gen_user_agreement(legal_engine_t *engine,
                             legal_user_agreement_t *ua,
                             const char *server_name,
                             const char *operator_name,
                             legal_lang_t lang) {
    if (!ua) return -1;
    fs_memset(ua, 0, sizeof(*ua));
    safe_strncpy(ua->server_name, server_name ? server_name : "ZEDEC P2P Server",
                 LEGAL_MAX_NAME);
    safe_strncpy(ua->operator, operator_name ? operator_name : "Anonymous",
                 LEGAL_MAX_NAME);
    ua->lang = lang;
    ua->created_tick = engine ? engine->phase_tick : 0;
    safe_strncpy(ua->access_rules,
        "1. Access is granted to all peers with valid cryptographic identity.\n"
        "2. Guests may browse public files and join public chat channels.\n"
        "3. Users may download files, chat, and participate in news threads.\n"
        "4. Contributors may upload files and create news threads.\n"
        "5. Admins have full control over server configuration.\n"
        "6. The operator reserves the right to revoke access at any time.",
        LEGAL_MAX_TEXT);
    safe_strncpy(ua->data_policy,
        "1. This server does not log IP addresses or browsing history.\n"
        "2. All communications are encrypted via PLNP and garlic routing.\n"
        "3. File transfers are content-addressed via smap (SHA-256).\n"
        "4. No data is shared with third parties.\n"
        "5. User identity is protected by Ed25519 cryptographic keys.",
        LEGAL_MAX_TEXT);
    safe_strncpy(ua->prohibited_uses,
        "1. No distribution of malware or malicious code.\n"
        "2. No spam or unsolicited mass messaging.\n"
        "3. No attempts to compromise server security or other peers.\n"
        "4. No distribution of content that violates sovereign law.\n"
        "5. No scraping or automated mass downloading without permission.",
        LEGAL_MAX_TEXT);
    safe_strncpy(ua->liability,
        "The server operator provides this service 'as is' without warranty. "
        "The operator is not liable for data loss, service interruption, or "
        "content uploaded by users. Users assume all risk for downloaded files. "
        "Golden ratio (phi) verification is provided for integrity checking.",
        LEGAL_MAX_TEXT);
    safe_strncpy(ua->dispute_resolution,
        "Disputes are resolved through the 5-state paraconsistent logic core "
        "(LPRES). Contradictory states are isolated (G0) and resolved through "
        "speculative negotiation (G+). No external arbitration is required. "
        "The sovereign jurisdiction of this server is specified below.",
        LEGAL_MAX_TEXT);
    safe_strncpy(ua->termination,
        "Access may be terminated by: (1) user disconnect, (2) operator "
        "revocation, (3) security violation, (4) inactivity exceeding 30 "
        "phase cycles. Upon termination, all session keys are destroyed. "
        "File transfers in progress are gracefully paused.",
        LEGAL_MAX_TEXT);
    safe_strncpy(ua->sovereign_jurisdiction,
        "Sovereign — SEL-3.3 Streisand Engine License", LEGAL_MAX_NAME);
    return legal_seal_agreement(ua);
}

int legal_gen_default_agreement(legal_engine_t *engine,
                                legal_user_agreement_t *ua,
                                const char *server_name,
                                legal_lang_t lang) {
    return legal_gen_user_agreement(engine, ua, server_name,
                                    "Server Operator", lang);
}

/* legal_customize_agreement() called le_strstr(), which does not exist
 * anywhere in the tree — an implicit declaration that would be an undefined
 * symbol at link time on a freestanding target. Bounded, no libc. */
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

int legal_customize_agreement(legal_user_agreement_t *ua,
                              const char *field, const char *value) {
    if (!ua || !field || !value) return -1;
    if (le_strstr(field, "access") || le_strstr(field, "rule"))
        safe_strncpy(ua->access_rules, value, LEGAL_MAX_TEXT);
    else if (le_strstr(field, "data") || le_strstr(field, "privacy"))
        safe_strncpy(ua->data_policy, value, LEGAL_MAX_TEXT);
    else if (le_strstr(field, "prohibit"))
        safe_strncpy(ua->prohibited_uses, value, LEGAL_MAX_TEXT);
    else if (le_strstr(field, "liability"))
        safe_strncpy(ua->liability, value, LEGAL_MAX_TEXT);
    else if (le_strstr(field, "dispute"))
        safe_strncpy(ua->dispute_resolution, value, LEGAL_MAX_TEXT);
    else if (le_strstr(field, "terminat"))
        safe_strncpy(ua->termination, value, LEGAL_MAX_TEXT);
    else if (le_strstr(field, "jurisdict"))
        safe_strncpy(ua->sovereign_jurisdiction, value, LEGAL_MAX_NAME);
    else if (le_strstr(field, "custom"))
        safe_strncpy(ua->custom_clauses, value, LEGAL_MAX_TEMPLATE);
    else if (le_strstr(field, "operator"))
        safe_strncpy(ua->operator, value, LEGAL_MAX_NAME);
    else
        return -2;
    return legal_seal_agreement(ua);
}

int legal_validate_agreement(legal_user_agreement_t *ua) {
    if (!ua) return -1;
    if (ua->server_name[0] == 0) return -2;
    if (ua->access_rules[0] == 0) return -3;
    if (ua->data_policy[0] == 0) return -4;
    if (ua->prohibited_uses[0] == 0) return -5;
    if (ua->liability[0] == 0) return -6;
    if (ua->dispute_resolution[0] == 0) return -7;
    if (ua->termination[0] == 0) return -8;
    return 0;
}

int legal_seal_agreement(legal_user_agreement_t *ua) {
    if (!ua) return -1;
    /* Golden ratio seal: compute phi-based checksum */
    const double phi = 1.6180339887498948;
    double sum = 0;
    const uint8_t *p = (const uint8_t *)ua;
    for (uint32_t i = 0; i < sizeof(*ua); i++)
        sum += (double)p[i] * phi;
    ua->golden_ratio_seal = (uint8_t)((uint64_t)sum & 0xFF);
    return 0;
}

int legal_render_document(legal_document_t *doc, char *buf, uint16_t buf_len) {
    if (!doc || !buf) return -1;
    int pos = 0;
    pos += fs_snprintf(buf + pos, buf_len - pos,
        "=== %s ===\n"
        "Type: %s | Language: %s | Author: %s\n"
        "Jurisdiction: %s | Timestamp: tick %llu\n\n",
        doc->title, legal_doc_type_name(doc->type),
        legal_lang_name(doc->lang), doc->author,
        doc->jurisdiction, (unsigned long long)doc->timestamp);
    for (uint32_t i = 0; i < doc->clause_count; i++) {
        legal_clause_t *c = &doc->clauses[i];
        const char *prefix = c->level == 0 ? "#" : (c->level == 1 ? "##" : "###");
        pos += fs_snprintf(buf + pos, buf_len - pos,
            "%s %s\n%s\n\n", prefix, c->title, c->body);
    }
    return pos;
}

int legal_render_agreement(legal_user_agreement_t *ua, char *buf,
                           uint16_t buf_len) {
    if (!ua || !buf) return -1;
    int pos = 0;
    pos += fs_snprintf(buf + pos, buf_len - pos,
        "=== USER AGREEMENT ===\n"
        "Server: %s\n"
        "Operator: %s\n"
        "Language: %s\n"
        "Jurisdiction: %s\n"
        "Golden Ratio Seal: 0x%02X\n"
        "Created: tick %llu\n\n"
        "## ACCESS RULES\n%s\n\n"
        "## DATA & PRIVACY POLICY\n%s\n\n"
        "## PROHIBITED USES\n%s\n\n"
        "## LIABILITY\n%s\n\n"
        "## DISPUTE RESOLUTION\n%s\n\n"
        "## TERMINATION\n%s\n\n",
        ua->server_name, ua->operator,
        legal_lang_name(ua->lang),
        ua->sovereign_jurisdiction,
        ua->golden_ratio_seal,
        (unsigned long long)ua->created_tick,
        ua->access_rules, ua->data_policy,
        ua->prohibited_uses, ua->liability,
        ua->dispute_resolution, ua->termination);
    if (ua->custom_clauses[0]) {
        pos += fs_snprintf(buf + pos, buf_len - pos,
            "## CUSTOM CLAUSES\n%s\n\n", ua->custom_clauses);
    }
    pos += fs_snprintf(buf + pos, buf_len - pos,
                       "---\nThis agreement is sealed with the golden ratio (phi) checksum.\n"
                       "License: Apache-2.0\n"
                       "Author: H.M. Michael-Laurence: Curzi (c)\n");
    return pos;
}

const char *legal_lang_name(legal_lang_t lang) {
    switch (lang) {
        case LEGAL_LANG_EN: return "English";
        case LEGAL_LANG_FR: return "French";
        case LEGAL_LANG_AR: return "Arabic";
        case LEGAL_LANG_PT: return "Portuguese";
        case LEGAL_LANG_SW: return "Kiswahili";
        case LEGAL_LANG_ES: return "Spanish";
        default: return "Unknown";
    }
}

const char *legal_lang_code(legal_lang_t lang) {
    switch (lang) {
        case LEGAL_LANG_EN: return "en";
        case LEGAL_LANG_FR: return "fr";
        case LEGAL_LANG_AR: return "ar";
        case LEGAL_LANG_PT: return "pt";
        case LEGAL_LANG_SW: return "sw";
        case LEGAL_LANG_ES: return "es";
        default: return "en";
    }
}

legal_lang_t legal_lang_from_code(const char *code) {
    if (!code || !code[0]) return LEGAL_LANG_EN;
    if (code[0] == 'f') return LEGAL_LANG_FR;
    if (code[0] == 'a') return LEGAL_LANG_AR;
    if (code[0] == 'p') return LEGAL_LANG_PT;
    if (code[0] == 's' && code[1] == 'w') return LEGAL_LANG_SW;
    if (code[0] == 's') return LEGAL_LANG_ES;
    return LEGAL_LANG_EN;
}

const char *legal_doc_type_name(legal_doc_type_t type) {
    switch (type) {
        case LEGAL_DOC_TREATY:         return "Treaty";
        case LEGAL_DOC_USER_AGREEMENT: return "User Agreement";
        case LEGAL_DOC_PRIVACY_POLICY: return "Privacy Policy";
        case LEGAL_DOC_TERMS_OF_USE:   return "Terms of Use";
        case LEGAL_DOC_SOVEREIGN_DECL: return "Sovereign Declaration";
        case LEGAL_DOC_DIPLOMATIC:     return "Diplomatic Package";
        case LEGAL_DOC_IMPLEMENTATION: return "Implementation Plan";
        case LEGAL_DOC_RISK_ASSESS:    return "Risk Assessment";
        case LEGAL_DOC_COST_BENEFIT:   return "Cost-Benefit Analysis";
        default: return "Unknown";
    }
}

/* ---- DECLARATION -----------------------------------------------------------

 * REQUIRES_NONE is measured: legal_engine.o's only undefined symbol is memcpy,
 * which the build redirects to fs_memcpy. The nation tables are static data
 * compiled into this file.
 */
#include "zxv_decl.h"
static int zxvd_legal_engine_bringup(void) {
    static legal_engine_t eng;
    if (legal_engine_init(&eng) != 0) return -1;
    if (legal_lang_name(legal_lang_from_code("en")) == 0) return -1;
    return 0;
}

ZXV_DECLARE(legal_engine,
    ZXV_PROVIDES(legal_engine_ready),
    ZXV_REQUIRES_NONE,
    ZXV_BRINGUP(zxvd_legal_engine_bringup));
