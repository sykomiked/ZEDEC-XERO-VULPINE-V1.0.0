/* modbind.c — construction rules. See modbind.h for what defect this prevents.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 */
#include "modbind.h"
#include "m5_types.h"

static mb_module_t g_mods[MB_MAX_MODULES];
static uint32_t    g_n;

/* A form is BLOCKED when its representation is not yet sound enough to carry a
 * boundary. Today that is exactly MB_FORM_TRIT: trit_t declares six enumerators
 * for five distinct states (TRIT_GLUT = 2 aliases TRIT_GLUT_NEUTRAL = 5), so
 * marshalling cannot be a bijection — pack(2) and pack(5) must denote one state,
 * and no unpack can then recover which was sent. Rather than let a
 * non-round-trippable boundary exist quietly, we refuse it and say why.
 *
 * This is checked against the ENUM ITSELF, so the day trit_t is repaired the
 * block lifts automatically and cannot be forgotten. */
static bool form_is_blocked(mb_form_t f) {
    if (f != MB_FORM_TRIT) return false;
    /* TRIT_GLUT and TRIT_GLUT_NEUTRAL are two DIFFERENT encodings documented as
     * the SAME state ("Legacy alias for GLUT_NEUTRAL"). While that holds, pack()
     * is not injective and no unpack() can recover which encoding was sent, so
     * the boundary cannot round-trip. Collapsing the alias — making the two
     * equal, or removing TRIT_GLUT — unblocks this automatically. */
    return (int)TRIT_GLUT != (int)TRIT_GLUT_NEUTRAL;
}

static bool name_eq(const char *a, const char *b) {
    for (uint32_t i = 0; i < MB_NAME_LEN; i++) {
        if (a[i] != b[i]) return false;
        if (a[i] == 0) return true;
    }
    return true;
}

void modbind_reset(void) {
    g_n = 0;
    for (uint32_t i = 0; i < MB_MAX_MODULES; i++) g_mods[i].registered = false;
}

uint32_t modbind_count(void) { return g_n; }

const mb_module_t *modbind_get(uint32_t i) {
    return (i < g_n) ? &g_mods[i] : (const mb_module_t *)0;
}

bool modbind_register(const mb_module_t *m) {
    if (!m || g_n >= MB_MAX_MODULES) return false;
    if (m->name[0] == 0) return false;
    if (m->n_emits > MB_MAX_PORTS || m->n_ingests > MB_MAX_PORTS) return false;
    for (uint32_t i = 0; i < g_n; i++)
        if (name_eq(g_mods[i].name, m->name)) return false;   /* duplicate */
    /* A declared port of MB_FORM_NONE is a filled-in struct that says nothing;
     * treat it as a malformed declaration rather than silently ignoring it. */
    for (uint8_t i = 0; i < m->n_emits; i++)
        if (m->emits[i].form == MB_FORM_NONE ||
            m->emits[i].form >= MB_FORM__COUNT) return false;
    for (uint8_t i = 0; i < m->n_ingests; i++)
        if (m->ingests[i].form == MB_FORM_NONE ||
            m->ingests[i].form >= MB_FORM__COUNT) return false;
    g_mods[g_n] = *m;
    g_mods[g_n].registered = true;
    g_n++;
    return true;
}

/* Does ANY registered module other than `self` ingest this exact (form,version)? */
static bool someone_ingests(const mb_module_t *self, mb_port_t p) {
    for (uint32_t i = 0; i < g_n; i++) {
        const mb_module_t *o = &g_mods[i];
        if (o == self) continue;
        for (uint8_t j = 0; j < o->n_ingests; j++)
            if (o->ingests[j].form == p.form && o->ingests[j].version == p.version)
                return true;
    }
    return false;
}

static bool someone_emits(const mb_module_t *self, mb_port_t p) {
    for (uint32_t i = 0; i < g_n; i++) {
        const mb_module_t *o = &g_mods[i];
        if (o == self) continue;
        for (uint8_t j = 0; j < o->n_emits; j++)
            if (o->emits[j].form == p.form && o->emits[j].version == p.version)
                return true;
    }
    return false;
}

/* Version mismatch is reported SEPARATELY from orphan/starved, because the two
 * demand different fixes: an orphan needs a consumer written, a version
 * mismatch needs one side migrated. Collapsing them would send the reader after
 * the wrong problem. */
static bool form_present_other_version(const mb_module_t *self, mb_port_t p,
                                       bool as_ingest) {
    for (uint32_t i = 0; i < g_n; i++) {
        const mb_module_t *o = &g_mods[i];
        if (o == self) continue;
        uint8_t n = as_ingest ? o->n_ingests : o->n_emits;
        const mb_port_t *ports = as_ingest ? o->ingests : o->emits;
        for (uint8_t j = 0; j < n; j++)
            if (ports[j].form == p.form && ports[j].version != p.version)
                return true;
    }
    return false;
}

mb_err_t modbind_check(const mb_module_t *m) {
    if (!m) return MB_ERR_NO_PORTS;

    /* A module that neither emits nor ingests cannot participate in a system.
     * This is the check that catches the 55-orphan class directly: a freestanding
     * module with a passing test suite and no declared boundary now fails. */
    if (m->n_emits == 0 && m->n_ingests == 0) return MB_ERR_NO_PORTS;

    for (uint8_t i = 0; i < m->n_emits; i++) {
        if (form_is_blocked(m->emits[i].form))   return MB_ERR_BLOCKED;
        if (!m->xform.pack)                      return MB_ERR_NO_XFORM;
        if (!someone_ingests(m, m->emits[i])) {
            if (form_present_other_version(m, m->emits[i], true))
                return MB_ERR_VERSION;
            return MB_ERR_ORPHAN;
        }
    }
    for (uint8_t i = 0; i < m->n_ingests; i++) {
        if (form_is_blocked(m->ingests[i].form)) return MB_ERR_BLOCKED;
        if (!m->xform.unpack)                    return MB_ERR_NO_XFORM;
        if (!someone_emits(m, m->ingests[i])) {
            if (form_present_other_version(m, m->ingests[i], false))
                return MB_ERR_VERSION;
            return MB_ERR_STARVED;
        }
    }
    return MB_OK;
}

uint32_t modbind_verify(mb_err_t *first_err, const char **first_name) {
    uint32_t bad = 0;
    if (first_err)  *first_err = MB_OK;
    if (first_name) *first_name = (const char *)0;
    for (uint32_t i = 0; i < g_n; i++) {
        mb_err_t e = modbind_check(&g_mods[i]);
        if (e != MB_OK) {
            if (bad == 0) {
                if (first_err)  *first_err = e;
                if (first_name) *first_name = g_mods[i].name;
            }
            bad++;
        }
    }
    return bad;
}

bool modbind_can_bind(const mb_module_t *from, const mb_module_t *to,
                      mb_form_t form) {
    if (!from || !to || form == MB_FORM_NONE || form >= MB_FORM__COUNT) return false;
    if (form_is_blocked(form)) return false;
    if (!from->xform.pack || !to->xform.unpack) return false;
    for (uint8_t i = 0; i < from->n_emits; i++) {
        if (from->emits[i].form != form) continue;
        for (uint8_t j = 0; j < to->n_ingests; j++)
            if (to->ingests[j].form == form &&
                to->ingests[j].version == from->emits[i].version)
                return true;
    }
    return false;
}

const char *mb_form_name(mb_form_t f) {
    switch (f) {
        case MB_FORM_NONE:     return "none";
        case MB_FORM_BINARY:   return "binary";
        case MB_FORM_TRISPACE: return "trispace";
        case MB_FORM_POLY:     return "poly/F_q";
        case MB_FORM_TRIT:     return "trit(BLOCKED)";
        case MB_FORM_PHASE:    return "phase";
        default:               return "?";
    }
}

const char *mb_err_name(mb_err_t e) {
    switch (e) {
        case MB_OK:           return "ok";
        case MB_ERR_ORPHAN:   return "ORPHAN: emits a form nothing ingests";
        case MB_ERR_STARVED:  return "STARVED: ingests a form nothing emits";
        case MB_ERR_NO_PORTS: return "NO PORTS: declares no boundary at all";
        case MB_ERR_BLOCKED:  return "BLOCKED: form not yet sound (see trit_t)";
        case MB_ERR_VERSION:  return "VERSION: form matches, version does not";
        case MB_ERR_NO_XFORM: return "NO XFORM: declares a form, supplies no marshaller";
        default:              return "?";
    }
}

/* ---- self-verification --------------------------------------------------- */

static int stub_pack(const void *l, uint8_t *o, uint32_t m)
{ (void)l; (void)o; (void)m; return 0; }
static int stub_unpack(const uint8_t *i, uint32_t l, void *o)
{ (void)i; (void)l; (void)o; return 0; }

static mb_module_t mk(const char *nm, mb_form_t emit, mb_form_t ing,
                      uint16_t ev, uint16_t iv, bool xf) {
    mb_module_t m;
    for (uint32_t i = 0; i < MB_NAME_LEN; i++) m.name[i] = 0;
    for (uint32_t i = 0; i < MB_NAME_LEN && nm[i]; i++) m.name[i] = nm[i];
    m.n_emits = 0; m.n_ingests = 0;
    if (emit != MB_FORM_NONE) { m.emits[0].form = emit; m.emits[0].version = ev; m.n_emits = 1; }
    if (ing  != MB_FORM_NONE) { m.ingests[0].form = ing; m.ingests[0].version = iv; m.n_ingests = 1; }
    m.xform.pack   = xf ? stub_pack   : (int (*)(const void *, uint8_t *, uint32_t))0;
    m.xform.unpack = xf ? stub_unpack : (int (*)(const uint8_t *, uint32_t, void *))0;
    m.registered = false;
    return m;
}

uint32_t modbind_selfcheck(void) {
    uint32_t bad = 0;
    modbind_reset();

    /* A producer and a matching consumer: composable. */
    mb_module_t prod = mk("producer", MB_FORM_TRISPACE, MB_FORM_NONE, 1, 0, true);
    mb_module_t cons = mk("consumer", MB_FORM_NONE, MB_FORM_TRISPACE, 0, 1, true);
    if (!modbind_register(&prod)) bad++;
    if (!modbind_register(&cons)) bad++;
    if (modbind_verify((mb_err_t *)0, (const char **)0) != 0) bad++;
    if (!modbind_can_bind(&g_mods[0], &g_mods[1], MB_FORM_TRISPACE)) bad++;

    /* THE 55-ORPHAN CASE. A module that emits a form nobody ingests must FAIL.
     * This is the whole point: today such a module is silent. */
    {
        modbind_reset();
        mb_module_t lonely = mk("lonely", MB_FORM_POLY, MB_FORM_NONE, 1, 0, true);
        if (!modbind_register(&lonely)) bad++;
        if (modbind_check(&g_mods[0]) != MB_ERR_ORPHAN) bad++;
    }
    /* The mirror case: ingests something nobody produces. */
    {
        modbind_reset();
        mb_module_t hungry = mk("hungry", MB_FORM_NONE, MB_FORM_POLY, 0, 1, true);
        if (!modbind_register(&hungry)) bad++;
        if (modbind_check(&g_mods[0]) != MB_ERR_STARVED) bad++;
    }
    /* A module with no declared boundary at all — the freestanding-but-untethered
     * shape that produced the 20% orphan rate. */
    {
        modbind_reset();
        mb_module_t island = mk("island", MB_FORM_NONE, MB_FORM_NONE, 0, 0, true);
        if (!modbind_register(&island)) bad++;
        if (modbind_check(&g_mods[0]) != MB_ERR_NO_PORTS) bad++;
    }
    /* Version skew must be distinguished from orphaning. */
    {
        modbind_reset();
        mb_module_t a = mk("v1emit", MB_FORM_BINARY, MB_FORM_NONE, 1, 0, true);
        mb_module_t b = mk("v2ingest", MB_FORM_NONE, MB_FORM_BINARY, 0, 2, true);
        modbind_register(&a); modbind_register(&b);
        if (modbind_check(&g_mods[0]) != MB_ERR_VERSION) bad++;
        if (modbind_can_bind(&g_mods[0], &g_mods[1], MB_FORM_BINARY)) bad++;
    }
    /* Declaring a form without supplying a marshaller is a broken contract. */
    {
        modbind_reset();
        mb_module_t p = mk("noxform", MB_FORM_TRISPACE, MB_FORM_NONE, 1, 0, false);
        mb_module_t c = mk("sink", MB_FORM_NONE, MB_FORM_TRISPACE, 0, 1, true);
        modbind_register(&p); modbind_register(&c);
        if (modbind_check(&g_mods[0]) != MB_ERR_NO_XFORM) bad++;
    }
    /* MB_FORM_TRIT must be refused while trit_t carries the GLUT alias. */
    {
        modbind_reset();
        mb_module_t t = mk("trituser", MB_FORM_TRIT, MB_FORM_NONE, 1, 0, true);
        mb_module_t s = mk("tritsink", MB_FORM_NONE, MB_FORM_TRIT, 0, 1, true);
        modbind_register(&t); modbind_register(&s);
        if (modbind_check(&g_mods[0]) != MB_ERR_BLOCKED) bad++;
        if (modbind_can_bind(&g_mods[0], &g_mods[1], MB_FORM_TRIT)) bad++;
    }
    /* Registry hygiene: duplicates and malformed declarations refused. */
    {
        modbind_reset();
        mb_module_t a = mk("dup", MB_FORM_BINARY, MB_FORM_NONE, 1, 0, true);
        if (!modbind_register(&a)) bad++;
        if (modbind_register(&a))  bad++;          /* duplicate name */
        mb_module_t noname = mk("", MB_FORM_BINARY, MB_FORM_NONE, 1, 0, true);
        if (modbind_register(&noname)) bad++;
    }
    modbind_reset();
    return bad;
}
