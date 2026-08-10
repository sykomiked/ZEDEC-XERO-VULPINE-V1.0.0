/* modbind.h — construction rules: a module declares what it speaks, or it does
 * not link. The structural fix for composition debt.
 *
 * THE DEFECT THIS EXISTS TO PREVENT
 * --------------------------------
 * Measured, not hypothesised: 55 files — 22,416 lines, 20% of kernel/src —
 * compile, pass their own host tests, and are linked into no kernel image on any
 * architecture. Several carry headers advertising capabilities the running
 * system does not have. The TLS subsystem ships as hkdf.c alone: key derivation
 * with no key exchange, no cipher, no record layer.
 *
 * None of that was caught, because nothing in the build could notice it. A
 * module that nothing calls is indistinguishable from a module that is merely
 * quiet.
 *
 * The previous attempt at a fix was `composed_bringup()` — a function in the
 * arch main that calls each subsystem in turn. That is EXTERNAL GLUE, and it is
 * the wrong shape: it wires the modules that were remembered, silently omits the
 * ones that were not, and prevents no regression. Adding a module and forgetting
 * to glue it is still invisible.
 *
 * THE FIX: THE CONNECTION IS A PROPERTY OF THE MODULE, NOT OF THE GLUE
 * -------------------------------------------------------------------
 * Every module declares, in its own translation unit, what canonical form it
 * EMITS and what it INGESTS. The kernel provides only the bus; the modules carry
 * their own translation. Then `modbind_verify()` answers a question the build
 * could not previously ask:
 *
 *     is every registered module reachable, and is every declared binding
 *     satisfied by a peer that actually implements the other half?
 *
 * A module that emits a form nothing ingests is an ORPHAN and is reported as a
 * failure. A module that ingests a form nothing emits is STARVED. A module that
 * declares a binding whose peer does not implement it is a BROKEN CONTRACT. All
 * three are silent today and all three are loud after this.
 *
 * WHY THE FORMS ARE AN ENUM AND NOT A STRING
 * ------------------------------------------
 * A string would let two modules agree by spelling and disagree in layout.
 * The form is a closed enum plus a version, so "we both speak MB_FORM_TRISPACE
 * v1" is a checkable claim rather than a shared belief.
 *
 * WHAT IS DELIBERATELY *NOT* HERE
 * -------------------------------
 * No continuous/real-valued form. A boundary that carries a capability decision
 * must be DECIDABLE, and continuous state forces a tolerance; a tolerance in a
 * permission check is a vulnerability, not an approximation. MB_FORM_POLY is
 * therefore over the finite field F_q (the existing ML-KEM ring
 * R_q = Z_q[X]/(X^n+1), q = 3329, n = 256), where equality is exact.
 *
 * MB_FORM_TRIT is declared but MUST NOT be used until `trit_t` is repaired:
 * TRIT_GLUT = 2 aliases TRIT_GLUT_NEUTRAL = 5, so two encodings denote one
 * state and marshalling cannot be a bijection. `modbind_verify` refuses any
 * module declaring MB_FORM_TRIT while that alias stands, rather than letting a
 * non-round-trippable boundary exist quietly.
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV composition slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_MODBIND_H
#define ZXV_MODBIND_H

#include <stdint.h>
#include <stdbool.h>

#define MB_MAX_MODULES  128u
#define MB_NAME_LEN      24u
#define MB_MAX_PORTS      4u   /* emits/ingests declared per module */

/* The canonical forms that may cross a module boundary. Closed set: adding one
 * is a deliberate act, and every form needs a declared marshaller. */
typedef enum {
    MB_FORM_NONE     = 0,
    MB_FORM_BINARY   = 1,  /* registers/bytes — the classic ABI payload      */
    MB_FORM_TRISPACE = 2,  /* S+ / S- / S0 triad members                     */
    MB_FORM_POLY     = 3,  /* F_q coefficient vector (R_q, q=3329, n=256)    */
    MB_FORM_TRIT     = 4,  /* multi-valued logic state — BLOCKED, see header */
    MB_FORM_PHASE    = 5,  /* phase-tick aligned event payload               */
    MB_FORM__COUNT
} mb_form_t;

/* Why a module failed verification. */
typedef enum {
    MB_OK              = 0,
    MB_ERR_ORPHAN      = 1,  /* emits a form nothing ingests            */
    MB_ERR_STARVED     = 2,  /* ingests a form nothing emits            */
    MB_ERR_NO_PORTS    = 3,  /* declares neither — cannot be in a system */
    MB_ERR_BLOCKED     = 4,  /* uses a form that is not yet sound        */
    MB_ERR_VERSION     = 5,  /* form matches, version does not           */
    MB_ERR_NO_XFORM    = 6,  /* declares a form but supplies no marshaller */
    MB_ERR_UNPROVIDED  = 7,  /* REQUIRES something nothing PROVIDES        */
    MB_ERR_CYCLE       = 8,  /* requires-graph has a cycle: never boots    */
    MB_ERR_CONTRACT    = 9,  /* two providers disagree on contract version */
} mb_err_t;

/* ---- readiness resolution ------------------------------------------------
 * No clock, no levels, no barriers. Repeatedly mark ready every module whose
 * requirements are all provided by an already-ready module, until nothing more
 * changes. Whatever remains HELD is either genuinely blocked or in a cycle —
 * and modbind_verify_graph distinguishes those, because they need different
 * fixes.
 *
 * Returns how many modules reached MB_READY. */
uint32_t modbind_resolve(void);

/* Withdraw a capability (S−). Every module transitively requiring it returns to
 * MB_HELD — teardown is the reverse edge of bring-up, not a separate mechanism.
 * Returns how many modules were un-readied. */
uint32_t modbind_withdraw(const char *capability);



/* Acyclic + fully-provided + contract-consistent. Returns problems found. */
uint32_t modbind_verify_graph(mb_err_t *first_err, const char **first_name);

/* A module's own translation block. It lives WITH the module, so the kernel
 * never needs to know how to convert anything — it only runs the bus. */
typedef struct {
    /* pack local representation -> canonical form. Returns bytes written, or
     * <0. NULL is legal only for a form the module never emits. */
    int (*pack)(const void *local, uint8_t *out, uint32_t max);
    /* unpack canonical form -> local. Returns 0, or <0. */
    int (*unpack)(const uint8_t *in, uint32_t len, void *local);
} module_transform_t;

typedef struct {
    mb_form_t form;
    uint16_t  version;
} mb_port_t;

/* ---- CAPABILITIES: the nonlinear half of this module -----------------------
 * emits/ingests describe DATA crossing a boundary. provides/requires describe
 * READINESS, and they are what removes the clock.
 *
 * A module does not declare a bring-up LEVEL. It declares what it PROVIDES and
 * what it REQUIRES, and it becomes ready when its requirements are satisfied --
 * on whichever core, in whatever order. The dependency graph IS the schedule;
 * any valid traversal is a valid boot, and two cores may traverse differently
 * and both be correct. `oseq` (already reachable) decides happens-before.
 *
 * TWO PROVIDERS OF ONE CAPABILITY IS NOT A CONFLICT. It is alternative
 * provision -- mm.c provides mm_ready with no requirements, arm64_mmu provides
 * the same capability but REQUIRES arm64_el1. A core without an MMU takes the
 * first and still boots. That is why nothing is deleted for being "redundant".
 *
 * The contract VERSION is what keeps that sound: two providers at the same
 * version are interchangeable; at different versions they are a build failure,
 * never a runtime coin-toss. This mirrors why MB_ERR_VERSION is reported
 * separately from MB_ERR_ORPHAN -- the two demand different fixes. */
#define MB_CAP_NAME_LEN 24u
#define MB_MAX_CAPS      4u

/* ---- THE ELECTRICAL MODEL: Ohm at a module boundary -----------------------
 * A boundary is not a wire with a cost. It is a circuit, and three quantities
 * describe it exactly as Ohm's law does:
 *
 *   VOLTAGE   the POTENTIAL across the boundary -- a difference exists only
 *             where something is required and something else provides it.
 *             No gap, no drive.
 *   CURRENT   the AMPLITUDE actually flowing. This is the TRIT, and it was
 *             already a five-level waveform: trit_to_ell gives 1.0 / 0.75 /
 *             0.5 / 0.25 / 0.0 and trit_to_charge gives +1 / 0 / -1. Rails,
 *             partials, and a zero crossing -- a multilevel inverter, built
 *             before anyone called it one.
 *   IMPEDANCE what opposes flow: the marshalling cost of pack/unpack. Under DC
 *             this would be plain resistance. It is not, because it is
 *             PHASE-DEPENDENT -- see below.
 *
 * POWER FACTOR IS THE POINT. Real power is V*I*cos(dphase), not V*I. Two modules
 * perfectly in phase deliver all of it; two a quarter-cycle apart deliver none
 * while still drawing current. That is the precise description of a boundary
 * that is CONNECTED AND DOING NO WORK -- which is what 1,674 discarded symbols
 * and every "linked but never exercised" module actually are. They are not
 * disconnected. They are reactive.
 *
 * And it makes phi a control rather than an ornament: phi is the most irrational
 * ratio (continued fraction all ones, Hurwitz worst case), so a phi phase offset
 * NEVER aligns at any harmonic -- a permanently zero power factor, deliberate
 * isolation. Matched phase couples; phi decouples forever. */
typedef struct {
    char     name[MB_CAP_NAME_LEN];   /* e.g. "mm_ready"                     */
    uint16_t contract;                /* providers must agree                */
    uint8_t  phase;                   /* l13 phase (0..12) this rides on     */
    uint8_t  amplitude;               /* trit_t: the five-level drive level   */
    uint8_t  alternating;             /* 1 = both sides swap roles per phase  */
} mb_cap_t;

/* Real power delivered across a binding, in milliwatt-equivalents (integer, no
 * float in the kernel). Returns 0 for a binding that carries current but does no
 * work -- which is a DIFFERENT and more useful answer than "not connected". */
int32_t mb_real_power(const mb_cap_t *provided, const mb_cap_t *required);

/* Power factor as a permille (0..1000). 1000 = perfectly in phase, all real.
 * 0 = quadrature, purely reactive, connected and useless. */
uint32_t mb_power_factor(uint8_t phase_a, uint8_t phase_b);

/* ---- THE PULSE -------------------------------------------------------------
 * A carrier, like a dial tone: it counts nothing and orders nothing. It says
 * only that the line is live, and phase is measured relative to it -- which is
 * how two modules can agree on phase without agreeing what time it is.
 *
 * NEVER COUNT IT. The moment anything derives ordering from pulse tallies, the
 * clock is back and the event-space model is gone. Ordering is oseq's job;
 * readiness is the fixpoint's; the carrier only makes the circuit exist.
 * Its ABSENCE is the diagnostic. */
void mb_carrier_up(void);
void mb_carrier_down(void);
bool mb_carrier(void);

/* Readiness state — the tri-space faces, not a boolean.
 *   S+ available   S- withdrawable   S0 HELD (requirements unmet) */
typedef enum {
    MB_HELD = 0,      /* S0: requirements not yet satisfied — not a failure */
    MB_READY = 1,     /* S+: provided                                        */
    MB_WITHDRAWN = 2, /* S-: retracted; dependants return to MB_HELD         */
} mb_ready_t;

typedef struct {
    char       name[MB_NAME_LEN];
    mb_port_t  emits[MB_MAX_PORTS];
    uint8_t    n_emits;
    mb_port_t  ingests[MB_MAX_PORTS];
    uint8_t    n_ingests;
    mb_cap_t   provides[MB_MAX_CAPS];
    uint8_t    n_provides;
    mb_cap_t   requires[MB_MAX_CAPS];
    uint8_t    n_requires;
    mb_ready_t ready;
    module_transform_t xform;
    bool       registered;
} mb_module_t;

/* ---- COMPOSITION: matrices within matrices ---------------------------------
 * A composite is not a new kind of thing. It IS an mb_module_t, one level up,
 * and that is what makes the recursion terminate-free: composites compose,
 * because a composite is a module.
 *
 * The boundary is COMPUTED, not declared:
 *     composite.provides = union of the parts' provides
 *     composite.requires = union of the parts' requires MINUS that union
 * Internal edges cancel. A requirement one part needs and another part supplies
 * never appears on the outside -- it was satisfied within. What is left is
 * exactly the composite's contract with the world.
 *
 * This is the same closure `mixmat` proves for row-stochastic matrices: the
 * product of two is another, so nesting never leaves the class. Here the product
 * of two modules is another module. That closure is the theorem the whole
 * "matrices within matrices" architecture rests on -- without it, a composite
 * would be a different kind of object needing its own rules at every level.
 *
 * A USER CAN THEREFORE BUILD A MODULE WITHOUT WRITING CODE: name the parts, and
 * the boundary follows. What they get back is a first-class module that can be
 * a part of the next one.
 *
 * Returns false if the parts cannot compose -- a contract disagreement between
 * two parts providing one capability, or more boundary edges than MB_MAX_CAPS.
 * Refusing is correct: a composite whose boundary was silently truncated would
 * misreport what it needs. */
bool modbind_compose(const char *name,
                     const char *const *part_names, uint32_t n_parts,
                     mb_module_t *out);


/* ---- registry ------------------------------------------------------------ */
void     modbind_reset(void);
/* Register a module. Returns false if the table is full, the name is empty or
 * duplicated, or the declaration is self-inconsistent. */
bool     modbind_register(const mb_module_t *m);
uint32_t modbind_count(void);
const mb_module_t *modbind_get(uint32_t i);

/* ---- the construction check ---------------------------------------------
 * Walks every registered module and reports the FIRST problem per module.
 * Returns the number of modules that failed (0 = the system is composable).
 * `first_err` / `first_name`, when non-NULL, receive the leading failure so a
 * caller can print something actionable rather than just a count. */
uint32_t modbind_verify(mb_err_t *first_err, const char **first_name);

/* Per-module answer, for tests and diagnostics. */
mb_err_t modbind_check(const mb_module_t *m);

/* Is this pairing legal — does `to` ingest what `from` emits, at a matching
 * version? The single question the bus asks before moving a payload. */
bool modbind_can_bind(const mb_module_t *from, const mb_module_t *to,
                      mb_form_t form);

/* Human-readable, for boot output. Never NULL. */
const char *mb_form_name(mb_form_t f);
const char *mb_err_name(mb_err_t e);

/* Internal consistency of the rules themselves. Returns problems (0 = sound). */
uint32_t modbind_selfcheck(void);

#endif /* ZXV_MODBIND_H */
