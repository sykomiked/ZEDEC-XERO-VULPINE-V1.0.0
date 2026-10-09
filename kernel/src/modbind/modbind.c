/* modbind.c — construction rules. See modbind.h for what defect this prevents.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "modbind.h"
#include "zxv_decl.h"
#include "m5_types.h"

static mb_module_t g_mods[MB_MAX_MODULES];
static uint32_t    g_n;

/* THE AMPLITUDE SCALE IS trit_t's, NOT A COPY OF IT.
 * modbind.h spells the five drive levels as plain integers so that zxv_decl.h --
 * and therefore all 91 declaring translation units -- need not include
 * m5_types.h and its double-returning inlines. That is only safe if the two
 * numberings cannot drift, so they are pinned here, in the one translation unit
 * that sees both. Renumbering trit_t now stops the build instead of silently
 * rescaling every capability in the system. */
_Static_assert((int)TRIT_FALSE        == (int)MB_AMP_NONE, "MB_AMP_NONE != TRIT_FALSE");
_Static_assert((int)TRIT_TRUE         == (int)MB_AMP_FULL, "MB_AMP_FULL != TRIT_TRUE");
_Static_assert((int)TRIT_GLUT_PLUS    == (int)MB_AMP_HIGH, "MB_AMP_HIGH != TRIT_GLUT_PLUS");
_Static_assert((int)TRIT_GLUT_MINUS   == (int)MB_AMP_LOW,  "MB_AMP_LOW  != TRIT_GLUT_MINUS");
_Static_assert((int)TRIT_GLUT_NEUTRAL == (int)MB_AMP_HALF, "MB_AMP_HALF != TRIT_GLUT_NEUTRAL");

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

/* Registration COPIES (see modbind_register), so the constant declaration a
 * module carries in .rodata is NOT the entry whose `ready` the fixpoint writes.
 * This is the bridge between the two. */
const mb_module_t *modbind_find(const char *name) {
    if (!name) return (const mb_module_t *)0;
    for (uint32_t i = 0; i < g_n; i++)
        if (name_eq(g_mods[i].name, name)) return &g_mods[i];
    return (const mb_module_t *)0;
}

bool modbind_register(const mb_module_t *m) {
    if (!m || g_n >= MB_MAX_MODULES) return false;
    if (m->name[0] == 0) return false;
    if (m->n_emits > MB_MAX_PORTS || m->n_ingests > MB_MAX_PORTS) return false;
    /* The capability counts were NOT bounded here, only the port counts. A
     * declaration claiming n_provides = 6 was accepted and then read past the
     * end of a 4-element array by resolve/verify_graph -- a silent out-of-bounds
     * read in the one component whose job is to catch declaration errors. */
    if (m->n_provides > MB_MAX_CAPS || m->n_requires > MB_MAX_CAPS) return false;
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
    /* ---- RESOLVE THE INHERITED PHASE, ONCE, HERE ---------------------------
     * A capability declared MB_PHASE_INHERIT rides its module's phase. Doing it
     * at registration rather than at the declaration site is what makes the
     * derivation possible at all: ZXV_PROVIDES/ZXV_REQUIRES are function-like
     * macros sitting in ZXV_DECLARE's ARGUMENT LIST, so they are fully expanded
     * before ZXV_DECLARE's body is ever substituted -- the module name cannot
     * reach inside them, and no amount of token pasting changes that (verified,
     * not assumed). ZXV_DECLARE can only stamp the phase on the MODULE, which
     * is where it belongs anyway.
     *
     * This is the single registration path -- modbind_compose's composite comes
     * through here too -- so there is exactly one place the sentinel dies.
     * An explicit ZXV_CAP_PHASED phase is 0..12 and is left alone: the escape
     * hatch stays an escape hatch. */
    for (uint8_t i = 0; i < g_mods[g_n].n_provides; i++)
        if (g_mods[g_n].provides[i].phase == MB_PHASE_INHERIT)
            g_mods[g_n].provides[i].phase =
                (uint8_t)(g_mods[g_n].phase % MB_PHASES);
    for (uint8_t i = 0; i < g_mods[g_n].n_requires; i++)
        if (g_mods[g_n].requires[i].phase == MB_PHASE_INHERIT)
            g_mods[g_n].requires[i].phase =
                (uint8_t)(g_mods[g_n].phase % MB_PHASES);
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
        /* The three graph errors had no names here, so modbind_verify_graph
         * could not print its own diagnosis -- it would have reported the one
         * class of failure it exists to find as "?". */
        case MB_ERR_UNPROVIDED: return "UNPROVIDED: REQUIRES a capability nothing PROVIDES";
        case MB_ERR_CYCLE:      return "CYCLE: requires-graph closes on itself; never resolves";
        case MB_ERR_CONTRACT:   return "CONTRACT: two providers of one capability disagree";
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
    /* n_provides / n_requires / ready were NEVER set here. These fixtures are
     * automatics, so all three carried whatever was on the stack, and
     * modbind_register copied that garbage straight into the registry --
     * where modbind_resolve and modbind_verify_graph index provides[] and
     * requires[] by exactly those counts. Nothing noticed, because the checks
     * this selfcheck runs (ORPHAN/STARVED/VERSION/NO_XFORM) only ever look at
     * the PORTS. The new MB_MAX_CAPS bound in modbind_register is what made it
     * visible: it started refusing the fixtures outright. Setting the counts is
     * the fix; the bound is what turned a latent out-of-bounds read into a
     * refusal. */
    m.n_provides = 0; m.n_requires = 0;
    /* `phase` is the module's own l13 seat and these fixtures are AUTOMATICS,
     * so leaving it unset would put a stack byte on the module and
     * modbind_register would stamp that byte onto every MB_PHASE_INHERIT cap.
     * These fixtures declare no capabilities at all (n_provides = n_requires =
     * 0), so nothing would read it today -- which is exactly the shape of the
     * bug the comment above records: fields nobody read until the resolver
     * started asking the physics. Declared, not inherited. */
    m.phase = 0;
    m.ready = MB_HELD;
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

/* ---- COUPLING IS A POWER QUESTION, NOT A NAME QUESTION --------------------
 * This predicate used to be cap_eq alone: a byte-for-byte comparison of two
 * capability names. That is a HARDCODED EDGE -- rigid, binary, permanent, and
 * blind to everything the electrical model measures. Two modules that spell a
 * capability the same way were coupled always and unconditionally, whatever
 * their contract, their drive level or their phase.
 *
 * mb_real_power asks the whole question at once, and each factor is a real
 * refusal rather than a decoration:
 *     VOLTAGE  cap_eq -- different capabilities are not a circuit at all
 *     CONTRACT a disagreement is an OPEN circuit, not a lossy one
 *     CURRENT  the five-level trit amplitude
 *     pf(dφ)   the INTERFERENCE fringe (1+cos)/2 over the 13 phases
 *     CARRIER  no line, no circuit
 * Non-zero real power is coupling. Zero is not a failure -- it is a boundary
 * that is present and doing no work, and the requirer stays MB_HELD.
 *
 * Sign is deliberately ignored (`!= 0`, not `> 0`): coupling STRENGTH (the phase
 * relationship, now the interference magnitude, always non-negative) and POLARITY
 * (which half-cycle delivered it, carried by the trit charge) are separate axes.
 * A negative-charge binding still couples; the sign only says which way it flows.
 * Antiphase is now the WEAKEST binding (14 permille), not an inverted-full one. */
static bool cap_available(const mb_cap_t *cap) {
    for (uint32_t i = 0; i < g_n; i++) {
        if (g_mods[i].ready != MB_READY) continue;
        for (uint8_t j = 0; j < g_mods[i].n_provides; j++)
            if (mb_real_power(&g_mods[i].provides[j], cap) != 0) return true;
    }
    return false;
}

/* Does ANY module, ready or not, NAME this capability? The old question. */
static bool cap_named_anywhere(const mb_cap_t *cap) {
    for (uint32_t i = 0; i < g_n; i++)
        for (uint8_t j = 0; j < g_mods[i].n_provides; j++)
            if (cap_eq(&g_mods[i].provides[j], cap)) return true;
    return false;
}

/* Does ANY module, ready or not, actually COUPLE with it? The new question. The
 * gap between these two answers is precisely the held-out-of-phase state. */
static bool cap_coupled_anywhere(const mb_cap_t *cap) {
    for (uint32_t i = 0; i < g_n; i++)
        for (uint8_t j = 0; j < g_mods[i].n_provides; j++)
            if (mb_real_power(&g_mods[i].provides[j], cap) != 0) return true;
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

/* ===== WHY A MODULE IS HELD: A FIXPOINT, NOT A LOCAL TEST ==================
 * modbind_verify_graph used to derive the answer by elimination: if nothing
 * provides the name it is UNPROVIDED, otherwise it must be a CYCLE. Under power
 * coupling that elimination is wrong, because a third explanation now exists --
 * the provider is right there and is simply not in phase.
 *
 * And the explanation PROPAGATES. Consider A -> B -> C, where A provides what B
 * requires but out of phase. B is held-out-of-phase. C's own requirement (B's
 * capability) is named AND coupled -- B's provide is perfectly in phase with
 * C -- so a purely local test would call C a CYCLE. There is no cycle anywhere
 * in that graph. C is held because its provider is, and its reason is B's.
 *
 * So the classification is computed as a fixpoint over the requires-graph, with
 * a strict ordering: a ROOT CAUSE outranks an INHERITED one, and CYCLE is only
 * what survives when nothing else explains the hold. The enum is numbered in
 * that order (UNPROVIDED 1 < PHASE 2 < CYCLE 3) and a module's reason only ever
 * moves DOWNWARD, which is what makes the loop terminate: the value is bounded
 * below and strictly decreasing per change, so at most 3*g_n changes can occur.
 *
 * Static, bounded, no allocation -- the same constraints as the rest of the
 * file. The array is written by modbind_hold_reason's driver and read by
 * modbind_verify_graph and the boot gate. */
static uint8_t g_hold[MB_MAX_MODULES];

static void hold_classify_all(void) {
    /* Everything held-with-requirements starts at the weakest explanation and
     * can only be improved on. A ready or requirement-free module has no hold
     * to explain. */
    for (uint32_t i = 0; i < g_n; i++)
        g_hold[i] = (uint8_t)((g_mods[i].ready == MB_HELD && g_mods[i].n_requires > 0)
                              ? MB_HOLD_CYCLE : MB_HOLD_NONE);

    bool changed = true;
    while (changed) {
        changed = false;
        for (uint32_t i = 0; i < g_n; i++) {
            if (g_hold[i] == (uint8_t)MB_HOLD_NONE) continue;
            uint8_t best = g_hold[i];
            for (uint8_t j = 0; j < g_mods[i].n_requires; j++) {
                const mb_cap_t *r = &g_mods[i].requires[j];
                /* ROOT CAUSE 1: nobody names it. */
                if (!cap_named_anywhere(r)) { best = (uint8_t)MB_HOLD_UNPROVIDED; break; }
                /* ROOT CAUSE 2: somebody names it and nobody drives it. */
                if (!cap_coupled_anywhere(r)) {
                    if ((uint8_t)MB_HOLD_PHASE < best) best = (uint8_t)MB_HOLD_PHASE;
                    continue;
                }
                /* This requirement IS coupled by somebody. If any of those
                 * providers is READY the requirement is met and explains
                 * nothing; otherwise this module is downstream of whatever is
                 * holding them, and inherits the strongest reason among them. */
                bool met = false;
                uint8_t inherited = (uint8_t)MB_HOLD_CYCLE;
                for (uint32_t k = 0; k < g_n && !met; k++) {
                    for (uint8_t l = 0; l < g_mods[k].n_provides; l++) {
                        if (mb_real_power(&g_mods[k].provides[l], r) == 0) continue;
                        if (g_mods[k].ready == MB_READY) { met = true; break; }
                        /* ROOT CAUSE 3: the provider was RETRACTED. S− is a
                         * state, not a fault, and a dependant of a withdrawn
                         * provider is not in a cycle -- it is waiting for a
                         * restoration that may never come, which is a different
                         * fact needing a different fix. */
                        if (g_mods[k].ready == MB_WITHDRAWN) {
                            if ((uint8_t)MB_HOLD_WITHDRAWN < inherited)
                                inherited = (uint8_t)MB_HOLD_WITHDRAWN;
                            continue;
                        }
                        if (g_hold[k] != (uint8_t)MB_HOLD_NONE &&
                            g_hold[k] < inherited) inherited = g_hold[k];
                    }
                }
                if (!met && inherited < best) best = inherited;
            }
            if (best < g_hold[i]) { g_hold[i] = best; changed = true; }
        }
    }
}

mb_hold_t modbind_hold_reason(const mb_module_t *m) {
    if (!m) return MB_HOLD_NONE;
    hold_classify_all();
    for (uint32_t i = 0; i < g_n; i++)
        if (&g_mods[i] == m || name_eq(g_mods[i].name, m->name))
            return (mb_hold_t)g_hold[i];
    return MB_HOLD_NONE;
}

const char *mb_hold_name(mb_hold_t h) {
    switch (h) {
        case MB_HOLD_NONE:       return "not held";
        case MB_HOLD_UNPROVIDED: return "UNPROVIDED: nothing declares that capability";
        case MB_HOLD_PHASE:      return "OUT OF PHASE: a provider exists, delivers no real power";
        case MB_HOLD_WITHDRAWN:  return "WITHDRAWN: its provider is in S-";
        case MB_HOLD_CYCLE:      return "CYCLE: requirements close on themselves";
        default:                 return "?";
    }
}

/* S- : withdrawal is the REVERSE EDGE of provision, not a separate mechanism.
 * Retract the capability, then re-run the fixpoint -- everything that
 * transitively required it falls back to MB_HELD on its own. This is why the
 * empty S- column in THIRTEEN_LAYERS.md closes: teardown was impossible under
 * barriers because barriers only run forwards. */
/* The capability being named, as a fully-initialised mb_cap_t. Every field is
 * written: `want` used to be an automatic with only name and contract set, so
 * phase/amplitude/alternating carried whatever was on the stack. That was
 * harmless while cap_eq looked only at the name, and it is exactly the class of
 * latent defect that the switch to power coupling turns live -- so it is closed
 * here rather than left to be discovered by a resolver that reads them. */
static mb_cap_t cap_by_name(const char *capability) {
    mb_cap_t want;
    for (uint32_t i = 0; i < MB_CAP_NAME_LEN; i++) want.name[i] = 0;
    for (uint32_t i = 0; i < MB_CAP_NAME_LEN && capability[i]; i++)
        want.name[i] = capability[i];
    want.contract = 0;
    want.phase = 0;
    want.amplitude = (uint8_t)MB_AMP_FULL;
    want.alternating = 0;
    return want;
}

uint32_t modbind_withdraw(const char *capability) {
    if (!capability) return 0;
    mb_cap_t want = cap_by_name(capability);

    uint32_t before = 0;
    for (uint32_t i = 0; i < g_n; i++) if (g_mods[i].ready == MB_READY) before++;

    for (uint32_t i = 0; i < g_n; i++)
        for (uint8_t j = 0; j < g_mods[i].n_provides; j++)
            if (cap_eq(&g_mods[i].provides[j], &want))
                g_mods[i].ready = MB_WITHDRAWN;

    uint32_t after = modbind_resolve();
    return (before > after) ? (before - after) : 0;
}

/* S− -> S0: the reverse of the reverse edge. modbind_resolve refuses to reset an
 * MB_WITHDRAWN module -- that is what makes a withdrawal survive a re-resolve --
 * so something has to put the provider back into the fixpoint, or S− is a
 * one-way door and the column can never be exercised twice.
 *
 * This restores only what was withdrawn FOR THIS CAPABILITY: a module parked in
 * S− is returned to MB_HELD, never straight to MB_READY. Whether it comes back
 * up is the fixpoint's decision, not this function's -- promoting it directly
 * would be asserting a readiness nothing re-derived. */
uint32_t modbind_restore(const char *capability) {
    if (!capability) return 0;
    mb_cap_t want = cap_by_name(capability);

    uint32_t before = 0;
    for (uint32_t i = 0; i < g_n; i++) if (g_mods[i].ready == MB_READY) before++;

    for (uint32_t i = 0; i < g_n; i++) {
        if (g_mods[i].ready != MB_WITHDRAWN) continue;
        for (uint8_t j = 0; j < g_mods[i].n_provides; j++)
            if (cap_eq(&g_mods[i].provides[j], &want)) {
                g_mods[i].ready = MB_HELD;
                break;
            }
    }

    uint32_t after = modbind_resolve();
    return (after > before) ? (after - before) : 0;
}



/* ===== THE ELECTRICAL MODEL ===============================================
 * cos over the 13 phases, as permille, integer-only. 13 phases means the phase
 * difference is (a-b) mod 13, and cos(2*pi*k/13) is tabulated rather than
 * computed -- freestanding, no libm, and exact enough that a power factor is a
 * decision rather than an estimate. */
static const int16_t COS13_PERMILLE[13] = {
    1000,  885,  568,  121, -355, -749, -971,   /* k = 0..6  */
    -971, -749, -355,  121,  568,  885           /* k = 7..12 */
};

uint32_t mb_power_factor(uint8_t phase_a, uint8_t phase_b) {
    uint32_t k = (uint32_t)((phase_a >= phase_b)
                            ? (phase_a - phase_b) : (phase_b - phase_a)) % 13u;
    int32_t c = COS13_PERMILLE[k];
    /* POWER FACTOR IS OPTICAL INTERFERENCE, not the AC transformer. Two beams
     * that share a carrier interfere with intensity I = I0 * cos^2(dphase/2) =
     * I0 * (1 + cos dphase)/2 -- the Young's-slits / Michelson fringe. So the
     * factor is the fringe (1000 + cos)/2 in permille: constructive and full
     * (1000) IN PHASE at k=0, falling MONOTONICALLY to its floor of 14 at
     * ANTIPHASE (k=6). Antiphase is now the WEAKEST coupling, not a strong one
     * with inverted polarity -- polarity is a separate axis carried by the trit
     * sign in mb_real_power, never folded into this magnitude.
     *
     * DERIVED, NOT A SECOND TABLE: computed straight from COS13_PERMILLE so the
     * two cannot drift; modbind_selfcheck pins every value against this formula.
     * NEVER ZERO: 13 is odd, so cos never reaches exactly -1000; the smallest
     * (1000 + c) is 1000 + (-971) = 29, and 29/2 = 14 > 0. Integer division
     * truncates toward zero, deterministically and identically on all five
     * arches -- no float, no libgcc. (1000 + c) is always positive here so the
     * cast is well-defined. */
    return (uint32_t)((1000 + c) / 2);
}

int32_t mb_real_power(const mb_cap_t *provided, const mb_cap_t *required) {
    if (!provided || !required) return 0;
    /* NO CARRIER, NO CIRCUIT -- and this is the header's own claim, not a new
     * policy: "the carrier only makes the circuit exist", "Absence is the
     * information: no carrier means no circuit, which is why silence on the line
     * is diagnostic rather than merely quiet."
     *
     * The physical argument is the one that decides it. PHASE IS MEASURED
     * RELATIVE TO THE CARRIER -- that is precisely how two modules agree on a
     * phase without agreeing what time it is. With no carrier there is no
     * reference, so `provided->phase - required->phase` is a difference between
     * two numbers that denote nothing, and a power computed from it would be an
     * invented measurement. Returning 0 is the honest answer.
     *
     * IT IS ALSO THE USEFUL ONE, because it is LOUD: with the line dead every
     * module carrying a requirement stays MB_HELD and says so by name. A carrier
     * that made no difference would not be a diagnostic, it would be an
     * ornament.
     *
     * THE COST IS THAT BOOT ORDER BECOMES LOAD-BEARING: mb_carrier_up() must run
     * before any modbind_resolve(). That is a real obligation and it is PROVEN
     * rather than asserted -- gate_withdraw_negtest() in zxv_decl_gate.c drops
     * the carrier on the live graph and shows the whole system falling to S0,
     * then raises it and shows it come back. */
    if (!mb_carrier()) return 0;
    /* VOLTAGE: potential exists only where the two name the same capability.
     * Different capabilities are not a circuit at all. */
    if (!cap_eq(provided, required)) return 0;
    /* A contract mismatch is an OPEN circuit, not a lossy one -- there is no
     * partial credit for two modules that disagree about what they mean. */
    if (provided->contract != required->contract) return 0;

    /* CURRENT: the five-level trit amplitude, canonicalised first so the
     * deprecated GLUT alias cannot present as a sixth level. */
    trit_t t = trit_canon((trit_t)provided->amplitude);
    int32_t i_permille;
    switch (t) {
        case TRIT_TRUE:           i_permille =  1000; break;
        case TRIT_GLUT_PLUS:      i_permille =   750; break;
        case TRIT_GLUT_NEUTRAL:   i_permille =   500; break;
        case TRIT_GLUT_MINUS:     i_permille =   250; break;
        case TRIT_FALSE:          i_permille =     0; break;
        default:                  i_permille =     0; break;
    }
    /* P = V * I * pf(dphase), where pf is the INTERFERENCE fringe (1000+cos)/2,
     * not raw cos. V is unity here (the potential either exists or it does not,
     * established above), so real power is amplitude scaled by the fringe factor:
     * full in phase, weakest (14 permille) antiphase, never zero from phase
     * alone. */
    uint32_t pf = mb_power_factor(provided->phase, required->phase);
    int32_t p = (int32_t)(((int64_t)i_permille * (int64_t)pf) / 1000);
    /* POLARITY: the trit sign says which half-cycle delivered it. */
    return (trit_charge(t) < 0) ? -p : p;
}

/* ===== THE PULSE: a carrier, not a clock ==================================
 * The system has no clock and it does have a PULSE -- the distinction is the
 * same one a dial tone makes. A dial tone counts nothing and orders nothing. It
 * says only: THE LINE IS LIVE. You do not read a time off it; you hear that the
 * circuit exists, and its absence is the signal that something is wrong.
 *
 * That is what a carrier does under AC. It is the continuous wave everything
 * else modulates onto -- it does not sequence the traffic, it makes traffic
 * possible. Phase is measured RELATIVE to it, which is exactly why a phase can
 * be meaningful without any global time existing: two modules do not need to
 * agree what time it is, only to share a reference tone.
 *
 * So the pulse is NOT a tick and must never become one. If anything starts
 * counting pulses to decide ordering, the clock has been reinvented and the
 * event-space model is lost. Ordering comes from oseq's happens-before;
 * readiness from the Huygens fixpoint; the pulse only carries.
 *
 * Absence is the information: no carrier means no circuit, which is why silence
 * on the line is diagnostic rather than merely quiet. */
static uint32_t g_carrier_live;   /* not a counter -- a presence flag */

void mb_carrier_up(void)   { g_carrier_live = 1u; }
void mb_carrier_down(void) { g_carrier_live = 0u; }
bool mb_carrier(void)      { return g_carrier_live != 0u; }

/* ===== COMPOSITION ========================================================= */

#define find_mod modbind_find

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
    /* A COMPOSITE HAS NO DERIVED PHASE OF ITS OWN, and inventing one would be
     * the hardcoding this whole mechanism exists to remove. It does not need
     * one: every cap it carries is COPIED from a part that was already
     * registered, so those caps already hold concrete resolved phases and
     * modbind_register's INHERIT fixup finds nothing to do. The surviving
     * boundary keeps the phase of whichever part it came from -- which is the
     * honest answer, because the internal edge cancelled and the external one
     * is still the part's edge. Set explicitly because `c` is an automatic. */
    c.phase = 0;
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

    /* 3. THREE WAYS TO BE HELD, AND ONLY ONE OF THEM IS A DEFECT.
     *
     *    This check used to reason by elimination -- "every requirement is
     *    named by somebody, and yet the fixpoint never arrived, therefore a
     *    cycle" -- which was sound only while coupling WAS naming. It is not any
     *    more. A provider can be present, named, contract-compatible and simply
     *    OUT OF PHASE, delivering no real power. The graph is then perfectly
     *    acyclic and the requirer is still held.
     *
     *    Calling that a cycle would be the worst possible failure of this whole
     *    model: the gate would turn ordinary fluid behaviour into a build
     *    failure, and the pressure would be to hardcode the edge back. So the
     *    reason is CLASSIFIED (modbind_hold_reason's fixpoint, above) and only
     *    MB_HOLD_CYCLE is counted as a problem:
     *
     *      MB_HOLD_UNPROVIDED  already counted once by check 1 -- counting it
     *                          again here would double-report one defect
     *      MB_HOLD_PHASE       PRESENT BUT NOT COUPLED. Not an error, not a
     *                          skip. S0 is an honest state; the gate names the
     *                          module and the boot proceeds.
     *      MB_HOLD_CYCLE       the requirements really do close on themselves,
     *                          and no traversal will ever resolve them.
     */
    modbind_resolve();
    hold_classify_all();
    for (uint32_t i = 0; i < g_n; i++) {
        if (g_mods[i].ready != MB_HELD || g_mods[i].n_requires == 0) continue;
        if (g_hold[i] == (uint8_t)MB_HOLD_CYCLE) NOTE(MB_ERR_CYCLE, g_mods[i].name);
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

    /* ===== THE ELECTRICAL MODEL, ACTUALLY EXERCISED =======================
     * mb_real_power, mb_power_factor and the carrier were WRITTEN AND NOT RUN:
     * measured with nm against kernel_arm64.elf, all four symbols were absent,
     * with zero callers anywhere in the tree. The law was on the page and the
     * resolver was still coupling by a byte-for-byte name compare. A law nothing
     * executes is a comment.
     *
     * These cases run it on real registered modules through the real fixpoint,
     * and every one of them is a claim that can FAIL rather than a claim that is
     * true by construction. */
    {
        modbind_reset();
        const bool carrier_was = mb_carrier();
        mb_carrier_up();

        mb_module_t p = mk("pwr_prov", MB_FORM_BINARY, MB_FORM_NONE, 1, 0, true);
        mb_module_t c = mk("pwr_req",  MB_FORM_NONE, MB_FORM_BINARY, 0, 1, true);
        mb_cap_t cap = cap_by_name("pwr");         /* phase 0, full drive */
        cap.contract = 1;
        p.n_provides = 1; p.provides[0] = cap;
        c.n_requires = 1; c.requires[0] = cap;
        if (!modbind_register(&p)) bad++;
        if (!modbind_register(&c)) bad++;

        /* IN PHASE AND FULLY DRIVEN: cos(0) = 1, so all of the current is real
         * power and the requirer couples. This is the case that must reproduce
         * the old name-matching behaviour EXACTLY -- every capability in the
         * shipped system is declared at phase 0 and full drive. */
        if (mb_power_factor(0, 0) != 1000u) bad++;
        if (mb_real_power(&g_mods[0].provides[0], &g_mods[1].requires[0]) != 1000) bad++;
        if (modbind_resolve() != 2u) bad++;
        if (modbind_verify_graph((mb_err_t *)0, (const char **)0) != 0u) bad++;
        if (modbind_hold_reason(&g_mods[1]) != MB_HOLD_NONE) bad++;

        /* PHASE ATTENUATES BUT DOES NOT OPEN. The interference fringe
         * (1000 + cos(2*pi*k/13))/2 falls MONOTONICALLY from 1000 in phase to its
         * floor of 14 at ANTIPHASE (k=6), and is never zero -- 13 is odd, so cos
         * never reaches -1000, so (1000+cos) never reaches 0. Recorded here as a
         * MEASUREMENT, not a wish: phase alone cannot presently isolate two
         * modules, and adding a "close enough to zero" threshold to make it
         * would put a tolerance in a capability decision, which modbind.h
         * already refuses on the grounds that a tolerance in a permission check
         * is a vulnerability rather than an approximation. What DOES open the
         * circuit is below.
         *
         * k=3 -> (1000+121)/2 = 560; k=6 (antiphase) -> (1000-971)/2 = 14, the
         * new floor. Every value is DERIVED from COS13_PERMILLE, checked below. */
        if (mb_power_factor(0, 3)  !=  560u) bad++;
        if (mb_power_factor(3, 0)  !=  560u) bad++;   /* symmetric */
        if (mb_power_factor(0, 6)  !=   14u) bad++;   /* antiphase = the floor */
        if (mb_power_factor(0, 13) != 1000u) bad++;   /* wraps at 13 */
        {
            mb_cap_t off = cap; off.phase = 3;
            if (mb_real_power(&g_mods[0].provides[0], &off) != 560) bad++;
        }

        /* PIN THE LAW TO ITS SOURCE: the factor is DERIVED from COS13_PERMILLE,
         * not a second table, and it has three properties the interference model
         * requires -- reproduce the derivation, symmetry, monotone-down 0..6, and
         * a strictly non-zero floor. If any drift, the invariance proof is void. */
        {
            uint32_t prev = 1001u;
            for (uint32_t kk = 0; kk < 13u; kk++) {
                uint32_t want = (uint32_t)((1000 + (int32_t)COS13_PERMILLE[kk]) / 2);
                if (mb_power_factor((uint8_t)kk, 0u) != want) bad++;   /* derived   */
                if (mb_power_factor(0u, (uint8_t)kk) != want) bad++;   /* symmetric */
                if (want == 0u) bad++;                                 /* never 0   */
                if (kk <= 6u) { if (want > prev) bad++; prev = want; } /* monotone  */
            }
            if (mb_power_factor(0, 6) != 14u) bad++;                   /* floor     */
        }

        /* ZERO DRIVE IS AN OPEN CIRCUIT, AND THAT IS THE THIRD STATE.
         * The provider is present, named, contract-compatible -- and delivers
         * nothing. The requirer must go MB_HELD, verify_graph must NOT call that
         * a cycle and must NOT call it unprovided, and the graph must still
         * report ZERO problems. Getting this wrong turns normal fluid behaviour
         * into a build failure, so it is tested rather than believed. */
        g_mods[0].provides[0].amplitude = (uint8_t)MB_AMP_NONE;
        if (mb_real_power(&g_mods[0].provides[0], &g_mods[1].requires[0]) != 0) bad++;
        if (modbind_resolve() != 1u) bad++;                      /* only pwr_prov */
        if (g_mods[1].ready != MB_HELD) bad++;
        if (modbind_verify_graph((mb_err_t *)0, (const char **)0) != 0u) bad++;
        if (modbind_hold_reason(&g_mods[1]) != MB_HOLD_PHASE) bad++;
        g_mods[0].provides[0].amplitude = (uint8_t)MB_AMP_FULL;

        /* THE FIVE-LEVEL DRIVE IS FIVE LEVELS, and the deprecated GLUT alias
         * canonicalises rather than presenting as a sixth. */
        {
            mb_cap_t d = cap;
            d.amplitude = (uint8_t)MB_AMP_HIGH;
            if (mb_real_power(&d, &g_mods[1].requires[0]) !=  750) bad++;
            d.amplitude = (uint8_t)MB_AMP_HALF;
            if (mb_real_power(&d, &g_mods[1].requires[0]) !=  500) bad++;
            d.amplitude = (uint8_t)MB_AMP_LOW;
            /* GLUT_MINUS carries negative charge: same magnitude, opposite
             * polarity. Non-zero either way -- it couples. */
            if (mb_real_power(&d, &g_mods[1].requires[0]) != -250) bad++;
            d.amplitude = (uint8_t)TRIT_GLUT;    /* the alias */
            if (mb_real_power(&d, &g_mods[1].requires[0]) !=  500) bad++;
        }

        /* A CONTRACT DISAGREEMENT IS AN OPEN CIRCUIT, NOT A LOSSY ONE. */
        {
            mb_cap_t v2 = cap; v2.contract = 2;
            if (mb_real_power(&v2, &g_mods[1].requires[0]) != 0) bad++;
        }

        /* NO CARRIER, NO CIRCUIT. Absence is the diagnostic: with the line dead
         * nothing that requires anything can couple, and the requirer falls to
         * S0 while the provider (which requires nothing) stays up. This is the
         * proof that raising the carrier at boot is load-bearing. */
        mb_carrier_down();
        if (mb_carrier()) bad++;
        if (mb_real_power(&g_mods[0].provides[0], &g_mods[1].requires[0]) != 0) bad++;
        if (modbind_resolve() != 1u) bad++;
        if (g_mods[1].ready != MB_HELD) bad++;
        mb_carrier_up();
        if (!mb_carrier()) bad++;
        if (modbind_resolve() != 2u) bad++;          /* and it comes straight back */

        /* S−: WITHDRAWAL AND RESTORATION, on registered modules through the
         * fixpoint. Withdrawing the provider must un-ready the requirer; the
         * provider itself is parked in S− and does not silently re-promote. */
        if (modbind_withdraw("pwr") != 2u) bad++;    /* provider + its dependant */
        if (g_mods[0].ready != MB_WITHDRAWN) bad++;
        if (g_mods[1].ready != MB_HELD) bad++;
        if (modbind_hold_reason(&g_mods[1]) != MB_HOLD_WITHDRAWN) bad++;
        if (modbind_restore("pwr") != 2u) bad++;
        if (g_mods[0].ready != MB_READY) bad++;
        if (g_mods[1].ready != MB_READY) bad++;

        /* Leave the carrier exactly as it was found: a selfcheck that changes
         * the machine it measured is not a measurement. */
        if (carrier_was) mb_carrier_up(); else mb_carrier_down();
    }

    modbind_reset();
    return bad;
}

/* ===== THE DECLARATION WALK ================================================
 * The linker collected one pointer per ZXV_DECLARE into .rodata.zxv_decl. That
 * is the entire integration surface: no list, no table, no registration call
 * anywhere in the arch main naming a module.
 *
 * The markers are WEAK on purpose. A target whose linker script has not yet
 * placed the section (or a host test that links only this file) resolves both
 * to 0, walks nothing, and links -- while the records themselves still land
 * harmlessly inside .rodata via that script's existing `*(.rodata.*)` wildcard.
 * The failure mode is "walked nothing", which the caller reports, rather than
 * "did not link" or "placed an orphan over .text".
 *
 * ORDER IS NOT LOAD-BEARING HERE. The order the linker happens to emit these
 * pointers in is an accident of the link line. It cannot affect the outcome,
 * because readiness is not decided by this loop -- it is decided afterwards by
 * modbind_resolve's fixpoint, which is order-independent by construction. */
extern const zxv_decl_t *const __zxv_decl_start[] __attribute__((weak));
extern const zxv_decl_t *const __zxv_decl_end[]   __attribute__((weak));

uint32_t zxv_decl_count(void) {
    const zxv_decl_t *const *s = __zxv_decl_start;
    const zxv_decl_t *const *e = __zxv_decl_end;
    if (!s || !e || e <= s) return 0;
    return (uint32_t)(e - s);
}

uint32_t zxv_decl_register_all(uint32_t *n_refused) {
    const zxv_decl_t *const *s = __zxv_decl_start;
    uint32_t n = zxv_decl_count(), ok = 0, bad = 0;
    for (uint32_t i = 0; i < n; i++) {
        const zxv_decl_t *d = s[i];
        /* A null slot would mean the section carried padding rather than
         * pointers -- a stride error, not a module error. Count it as refused
         * so it can never be mistaken for a clean walk. */
        if (!d || !d->mod) { bad++; continue; }
        if (modbind_register(d->mod)) ok++; else bad++;
    }
    if (n_refused) *n_refused = bad;
    return ok;
}

/* WHY THE FAILURE NAMES ARE RECORDED AND NOT JUST COUNTED.
 * `bring-up: up=86 failed=1` is not actionable -- it is the same silence this
 * whole mechanism exists to remove, one level down. The gate already learned
 * this lesson once: modbind_verify_graph returns a count, and gate_report_all()
 * in the arch main exists solely to turn that count into "which module, which
 * capability". A failed bring-up needs exactly the same treatment, and without
 * it the only way to find the offender is to bisect by rebuilding.
 * Bounded, static, no allocation: the first ZXV_DECL_MAX_FAILED names are kept
 * and any beyond that are counted but unnamed, which the caller reports. */
static const char *g_failed_names[ZXV_DECL_MAX_FAILED];
static uint32_t    g_failed_n;

/* The modules whose bring-up RAN and returned MB_BRINGUP_HELD. Same bounded,
 * static, allocation-free shape as g_failed_names: a held count on its own is not
 * actionable, and the gate turns these into "which module, and why". */
static const char *g_held_names[ZXV_DECL_MAX_FAILED];
static uint32_t    g_held_n;

const char *zxv_decl_failed_name(uint32_t i) {
    return (i < g_failed_n) ? g_failed_names[i] : (const char *)0;
}

const char *zxv_decl_held_name(uint32_t i) {
    return (i < g_held_n) ? g_held_names[i] : (const char *)0;
}

uint32_t zxv_decl_bringup_ready(uint32_t *n_failed, uint32_t *n_held,
                                uint32_t *n_declared_only) {
    const zxv_decl_t *const *s = __zxv_decl_start;
    uint32_t n = zxv_decl_count(), up = 0, failed = 0, held = 0, declared_only = 0;
    g_failed_n = 0;
    g_held_n   = 0;
    for (uint32_t i = 0; i < n; i++) {
        const zxv_decl_t *d = s[i];
        if (!d || !d->mod) continue;
        /* The registry entry, not the declaration: `ready` lives on the copy. */
        const mb_module_t *reg = modbind_find(d->mod->name);
        if (!reg) {
            failed++;
            if (g_failed_n < ZXV_DECL_MAX_FAILED)
                g_failed_names[g_failed_n++] = d->mod->name;
            continue;
        }
        /* S0 is not a failure and not a skip-in-silence. It is HELD, and the
         * caller prints it by name. Nothing is run for it -- that is the point:
         * its preconditions have not happened yet. */
        if (reg->ready != MB_READY) { held++; continue; }
        /* DECLARING IS NOT BRINGING UP, so it must not be COUNTED as bringing
         * up. A module with no bring-up function ran nothing; folding it into
         * `up` made the banner "every ready module brought itself up" true by
         * construction for every such module -- a report that cannot fail is
         * the same self-certifying defect this gate exists to remove. It is
         * counted and reported on its own line instead. */
        if (!d->bringup) { declared_only++; continue; }
        /* THREE outcomes, not two. HELD != FAILED: a bring-up that ran, found its
         * requirements met, and honestly reports the hardware/precondition it
         * fronts is ABSENT (MB_BRINGUP_HELD) is S0, not a fault. It is counted in
         * `held` -- the same bucket as the fixpoint's S0 -- named for the gate,
         * and kept OUT of `up` and OUT of `failed`, so it raises no [FAIL]. Only a
         * genuine non-zero (a real error) still fails. */
        {
            int rc = d->bringup();
            if (rc == 0) {
                up++;
            } else if (rc == MB_BRINGUP_HELD) {
                held++;
                if (g_held_n < ZXV_DECL_MAX_FAILED)
                    g_held_names[g_held_n++] = d->mod->name;
            } else {
                failed++;
                if (g_failed_n < ZXV_DECL_MAX_FAILED)
                    g_failed_names[g_failed_n++] = d->mod->name;
            }
        }
    }
    if (n_failed)        *n_failed        = failed;
    if (n_held)          *n_held          = held;
    if (n_declared_only) *n_declared_only = declared_only;
    return up;
}

/* ---- NEGATIVE-TEST FIXTURES ----------------------------------------------
 * A gate that has never failed is not known to work. These two fixtures make
 * the gate fail on demand, from a real declaration going through the real walk
 * -- not from a hand-built struct that bypasses it.
 *
 * Build with CFLAGS_EXTRA=-DZXV_DECL_NEGTEST=1 (unprovided), =2 (cycle) or
 * =3 (both). OFF in every normal build, and deliberately re-runnable: deleting
 * the fixture would mean the next person has to write it again from scratch to
 * find out whether the gate still bites. */
#if defined(ZXV_DECL_NEGTEST) && ((ZXV_DECL_NEGTEST) & 1)
/* REQUIRES something no module anywhere PROVIDES -> MB_ERR_UNPROVIDED. */
ZXV_DECLARE(negtest_unprovided,
    ZXV_PROVIDES_NONE,
    ZXV_REQUIRES(negtest_absent_cap),
    ZXV_NO_BRINGUP);
#endif
#if defined(ZXV_DECL_NEGTEST) && ((ZXV_DECL_NEGTEST) & 2)
/* Each provides what the other requires. Every requirement IS provided, so
 * check 1 stays silent and only the fixpoint can tell: neither ever leaves
 * MB_HELD -> MB_ERR_CYCLE. That is exactly the discrimination verify_graph
 * exists to make, and it is why a missing provider and a cycle are separate
 * errors -- they need different fixes. */
ZXV_DECLARE(negtest_cycle_a,
    ZXV_PROVIDES(negtest_cap_a),
    ZXV_REQUIRES(negtest_cap_b),
    ZXV_NO_BRINGUP);
ZXV_DECLARE(negtest_cycle_b,
    ZXV_PROVIDES(negtest_cap_b),
    ZXV_REQUIRES(negtest_cap_a),
    ZXV_NO_BRINGUP);
#endif
