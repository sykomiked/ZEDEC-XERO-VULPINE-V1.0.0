/* modbind.c — construction rules. See modbind.h for what defect this prevents.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 */
#include "modbind.h"
#include "m5_types.h"

static mb_module_t g_mods[MB_MAX_MODULES];
static uint32_t    g_n;

/* A form is BLOCKED when its representation is not sound enough to carry a
 * boundary. MB_FORM_TRIT was blocked while trit_t had two encodings for one
 * state; it is now gated on canonicalisation being demonstrably sound. */
static bool form_is_blocked(mb_form_t f) {
    if (f != MB_FORM_TRIT) return false;
    /* UNBLOCKED once canonicalisation is SOUND, not once someone says it is.
     *
     * TRIT_GLUT remains a deprecated alias for TRIT_GLUT_NEUTRAL (owner
     * decision: 59 call sites keep working). That is safe at a boundary only if
     * every value is canonicalised on the way in, so the boundary carries the
     * five-element canonical image rather than the six-element enum.
     *
     * trit_canon_is_sound() checks exactly that at runtime: the alias collapses,
     * the map is idempotent, every result is emittable, and the image has
     * exactly TRIT_CANONICAL_COUNT = 5 members. If any of that regresses -- say
     * a seventh enumerator is added without a canonical rule -- the form blocks
     * itself again automatically. Evidence, not a promise. */
    return !trit_canon_is_sound();
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
        case MB_FORM_TRIT:     return "trit/canonical-5";
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


/* ===== READINESS: the fixpoint that replaces the clock =====================
 *
 * THIS LOOP IS A HUYGENS PROPAGATION, and naming it that is not decoration --
 * it is what makes partial bring-up legible.
 *
 * Huygens' Principle (1670): every point a wavefront reaches becomes a SOURCE of
 * the next wavefront. The wave is continuous; the interactions are quantised.
 * That is precisely this loop. Every module that becomes MB_READY becomes a
 * provider, and therefore a source from which the next pass can advance. One
 * `while (changed)` iteration is one wavefront advance. There is no clock, and
 * there was never a need for one: the wave does not tick, it propagates.
 *
 * Three consequences that follow from the physics and are not obvious from the
 * code, drawn from the verified extraction in PROVENANCE/QAT_PHYSICS_EXTRACTION.md:
 *
 *   1. UNREACHED IS NOT FAILED. A wavefront reaches what it reaches. A module
 *      the wave never arrives at is MB_HELD -- S0, the unresolved remainder --
 *      not an error and not a skip. The barrier model had no way to say this,
 *      which is why it needed to treat "not yet" as a fault.
 *   2. POTENTIAL BECOMES ACTUAL AT THE INTERACTION. QAT's absorption/emission
 *      cycle converts potential energy into "the energy of what is actually
 *      happening". MB_HELD -> MB_READY is that conversion: the capability exists
 *      as potential until the requirement is met, and the meeting is the event.
 *   3. THE BOUNDARY IS THE ACTIVE SITE. On the sphere model the 2D surface --
 *      not the interior -- is where charge lives and where everything occurs.
 *      Here the requires/provides EDGE is where readiness is decided; the module
 *      interior does nothing until its boundary condition is satisfied.
 *
 * The wavefront also explains why two cores may resolve in different orders and
 * agree: a wave has no preferred traversal, only a front. `oseq` records the
 * happens-before that results.
 * ===========================================================================
 * modbind_resolve does not walk a list in order. It repeatedly promotes every
 * module whose requirements are already provided, until a pass changes nothing.
 * That is a least-fixpoint computation, and it has the properties the barrier
 * model could not offer:
 *   - order-independent: registration order cannot change the outcome
 *   - core-independent: two cores may promote in different orders, same result
 *   - partial: whatever cannot be satisfied stays MB_HELD (S0), which is an
 *     honest state rather than a failure or a silent skip
 * There is no tick, no level and no barrier anywhere in it. */

static bool cap_eq(const mb_cap_t *a, const mb_cap_t *b) {
    for (uint32_t i = 0; i < MB_CAP_NAME_LEN; i++) {
        if (a->name[i] != b->name[i]) return false;
        if (a->name[i] == 0) return true;
    }
    return true;
}

/* Is `cap` provided by some module that is already MB_READY? */
static bool cap_available(const mb_cap_t *cap) {
    for (uint32_t i = 0; i < g_n; i++) {
        if (g_mods[i].ready != MB_READY) continue;
        for (uint8_t j = 0; j < g_mods[i].n_provides; j++)
            if (cap_eq(&g_mods[i].provides[j], cap)) return true;
    }
    return false;
}

uint32_t modbind_resolve(void) {
    /* start everything HELD (S0) -- nothing is ready until shown to be */
    for (uint32_t i = 0; i < g_n; i++)
        if (g_mods[i].ready != MB_WITHDRAWN) g_mods[i].ready = MB_HELD;

    bool changed = true;
    while (changed) {
        changed = false;
        for (uint32_t i = 0; i < g_n; i++) {
            mb_module_t *m = &g_mods[i];
            if (m->ready != MB_HELD) continue;
            bool all = true;
            for (uint8_t j = 0; j < m->n_requires && all; j++)
                if (!cap_available(&m->requires[j])) all = false;
            if (all) { m->ready = MB_READY; changed = true; }
        }
    }
    uint32_t n = 0;
    for (uint32_t i = 0; i < g_n; i++) if (g_mods[i].ready == MB_READY) n++;
    return n;
}

/* S- : withdrawal is the REVERSE EDGE of provision, not a separate mechanism.
 * Retract the capability, then re-run the fixpoint -- everything that
 * transitively required it falls back to MB_HELD on its own. This is why the
 * empty S- column in THIRTEEN_LAYERS.md closes: teardown was impossible under
 * barriers because barriers only run forwards. */
uint32_t modbind_withdraw(const char *capability) {
    if (!capability) return 0;
    mb_cap_t want; 
    for (uint32_t i = 0; i < MB_CAP_NAME_LEN; i++) want.name[i] = 0;
    for (uint32_t i = 0; i < MB_CAP_NAME_LEN && capability[i]; i++)
        want.name[i] = capability[i];
    want.contract = 0;

    uint32_t before = 0;
    for (uint32_t i = 0; i < g_n; i++) if (g_mods[i].ready == MB_READY) before++;

    for (uint32_t i = 0; i < g_n; i++)
        for (uint8_t j = 0; j < g_mods[i].n_provides; j++)
            if (cap_eq(&g_mods[i].provides[j], &want))
                g_mods[i].ready = MB_WITHDRAWN;

    uint32_t after = modbind_resolve();
    return (before > after) ? (before - after) : 0;
}


/* ===== COMPOSITION ========================================================= */

static const mb_module_t *find_mod(const char *nm) {
    for (uint32_t i = 0; i < g_n; i++)
        if (name_eq(g_mods[i].name, nm)) return &g_mods[i];
    return (const mb_module_t *)0;
}

static bool cap_in(const mb_cap_t *set, uint8_t n, const mb_cap_t *c) {
    for (uint8_t i = 0; i < n; i++) if (cap_eq(&set[i], c)) return true;
    return false;
}

bool modbind_compose(const char *name,
                     const char *const *part_names, uint32_t n_parts,
                     mb_module_t *out) {
    if (!name || !part_names || !out || n_parts == 0) return false;

    mb_module_t c;
    for (uint32_t i = 0; i < MB_NAME_LEN; i++) c.name[i] = 0;
    for (uint32_t i = 0; i < MB_NAME_LEN && name[i]; i++) c.name[i] = name[i];
    c.n_emits = c.n_ingests = c.n_provides = c.n_requires = 0;
    c.ready = MB_HELD;
    c.registered = false;
    c.xform.pack = (int (*)(const void *, uint8_t *, uint32_t))0;
    c.xform.unpack = (int (*)(const uint8_t *, uint32_t, void *))0;

    /* pass 1: gather every provide, and reject a contract disagreement between
     * two parts. Alternative provision INSIDE one composite is only coherent if
     * the alternatives agree -- otherwise the composite's own behaviour would
     * depend on which part resolved first. */
    for (uint32_t p = 0; p < n_parts; p++) {
        const mb_module_t *m = find_mod(part_names[p]);
        if (!m) return false;
        for (uint8_t j = 0; j < m->n_provides; j++) {
            for (uint8_t k = 0; k < c.n_provides; k++)
                if (cap_eq(&c.provides[k], &m->provides[j]) &&
                    c.provides[k].contract != m->provides[j].contract)
                    return false;                      /* contract clash */
            if (cap_in(c.provides, c.n_provides, &m->provides[j])) continue;
            if (c.n_provides >= MB_MAX_CAPS) return false;
            c.provides[c.n_provides++] = m->provides[j];
        }
    }

    /* pass 2: a requirement satisfied INSIDE the composite is internal and does
     * not surface. Only what no part supplies becomes the composite's own
     * requirement. This subtraction is the whole of encapsulation. */
    for (uint32_t p = 0; p < n_parts; p++) {
        const mb_module_t *m = find_mod(part_names[p]);
        for (uint8_t j = 0; j < m->n_requires; j++) {
            if (cap_in(c.provides, c.n_provides, &m->requires[j])) continue;
            if (cap_in(c.requires, c.n_requires, &m->requires[j])) continue;
            if (c.n_requires >= MB_MAX_CAPS) return false;  /* refuse, never truncate */
            c.requires[c.n_requires++] = m->requires[j];
        }
    }

    /* pass 3: data ports surface the same way -- an emitted form some part
     * ingests is internal traffic and is not the composite's business. */
    for (uint32_t p = 0; p < n_parts; p++) {
        const mb_module_t *m = find_mod(part_names[p]);
        for (uint8_t j = 0; j < m->n_emits; j++) {
            bool internal = false;
            for (uint32_t q = 0; q < n_parts && !internal; q++) {
                const mb_module_t *o = find_mod(part_names[q]);
                for (uint8_t k = 0; k < o->n_ingests && !internal; k++)
                    if (o->ingests[k].form == m->emits[j].form &&
                        o->ingests[k].version == m->emits[j].version)
                        internal = true;
            }
            if (internal || c.n_emits >= MB_MAX_PORTS) continue;
            c.emits[c.n_emits++] = m->emits[j];
            if (!c.xform.pack) c.xform.pack = m->xform.pack;
        }
    }
    *out = c;
    return true;
}

uint32_t modbind_verify_graph(mb_err_t *first_err, const char **first_name) {
    uint32_t bad = 0;
    if (first_err)  *first_err = MB_OK;
    if (first_name) *first_name = (const char *)0;
    #define NOTE(e, nm) do { if (bad == 0) { if (first_err) *first_err = (e); \
                             if (first_name) *first_name = (nm); } bad++; } while (0)

    /* 1. every requirement must be provided by SOMEBODY (ready or not) */
    for (uint32_t i = 0; i < g_n; i++) {
        for (uint8_t j = 0; j < g_mods[i].n_requires; j++) {
            bool found = false;
            for (uint32_t k = 0; k < g_n && !found; k++)
                for (uint8_t l = 0; l < g_mods[k].n_provides && !found; l++)
                    if (cap_eq(&g_mods[k].provides[l], &g_mods[i].requires[j]))
                        found = true;
            if (!found) NOTE(MB_ERR_UNPROVIDED, g_mods[i].name);
        }
    }

    /* 2. ALTERNATIVE PROVISION IS LEGAL; DISAGREEMENT IS NOT. Two providers of
     *    one capability are interchangeable only at the same contract version.
     *    Differing versions are a build failure rather than a runtime
     *    coin-toss whose outcome depends on which module registered first. */
    for (uint32_t i = 0; i < g_n; i++)
        for (uint8_t j = 0; j < g_mods[i].n_provides; j++)
            for (uint32_t k = i + 1; k < g_n; k++)
                for (uint8_t l = 0; l < g_mods[k].n_provides; l++)
                    if (cap_eq(&g_mods[i].provides[j], &g_mods[k].provides[l]) &&
                        g_mods[i].provides[j].contract !=
                        g_mods[k].provides[l].contract)
                        NOTE(MB_ERR_CONTRACT, g_mods[k].name);

    /* 3. A cycle never reaches the fixpoint. Detected as: requirements are all
     *    provided by SOMETHING, yet the module never becomes ready. That
     *    distinguishes a cycle from a genuinely missing provider -- they need
     *    different fixes, so they get different errors. */
    modbind_resolve();
    for (uint32_t i = 0; i < g_n; i++) {
        if (g_mods[i].ready != MB_HELD || g_mods[i].n_requires == 0) continue;
        bool all_declared = true;
        for (uint8_t j = 0; j < g_mods[i].n_requires && all_declared; j++) {
            bool found = false;
            for (uint32_t k = 0; k < g_n && !found; k++)
                for (uint8_t l = 0; l < g_mods[k].n_provides && !found; l++)
                    if (cap_eq(&g_mods[k].provides[l], &g_mods[i].requires[j]))
                        found = true;
            if (!found) all_declared = false;
        }
        if (all_declared) NOTE(MB_ERR_CYCLE, g_mods[i].name);
    }
    #undef NOTE
    return bad;
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
        /* Canonicalisation is sound, so the trit boundary is now LEGAL. */
        if (!trit_canon_is_sound()) bad++;
        if (modbind_check(&g_mods[0]) != MB_OK) bad++;
        if (!modbind_can_bind(&g_mods[0], &g_mods[1], MB_FORM_TRIT)) bad++;
        /* and the canonicaliser itself must behave */
        if (trit_canon(TRIT_GLUT) != TRIT_GLUT_NEUTRAL) bad++;
        if (trit_is_canonical(TRIT_GLUT)) bad++;
        if (!trit_is_canonical(TRIT_GLUT_NEUTRAL)) bad++;
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
