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
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_MODBIND_H
#define ZXV_MODBIND_H

#include <stdint.h>
#include <stdbool.h>

/* 192, not 128. The registry is the whole graph, and the census that motivated
 * this work counts 164 candidate declarers -- at 128 the 165th module would be
 * REFUSED, and modbind_register refuses by returning plain false with no
 * diagnostic, so the gate below would then certify a graph missing the very
 * modules that overflowed. The ceiling has to sit above the population before
 * declarations start accumulating, not after. */
#define MB_MAX_MODULES  192u
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
 * Returns how many modules reached MB_READY.
 *
 * A REQUIREMENT IS SATISFIED WHEN REAL POWER FLOWS, NOT WHEN A NAME MATCHES.
 * The predicate is mb_real_power(provided, required) != 0, which folds in the
 * name (VOLTAGE: no shared capability, no potential), the contract (a mismatch
 * is an OPEN circuit), the amplitude (CURRENT) and the phase difference (POWER
 * FACTOR). Two modules that name the same capability a quarter-cycle apart are
 * CONNECTED AND DOING NO WORK, and the requirer stays MB_HELD -- S0, present but
 * not coupled. That is not an error, not a skip and not a missing provider; it
 * is the ordinary behaviour of a nonlinear system, and modbind_hold_reason
 * below is how a caller tells the three apart. */
uint32_t modbind_resolve(void);

/* Withdraw a capability (S−). Every module transitively requiring it returns to
 * MB_HELD — teardown is the reverse edge of bring-up, not a separate mechanism.
 * Returns how many modules were un-readied. */
uint32_t modbind_withdraw(const char *capability);

/* Un-withdraw: the modules parked in MB_WITHDRAWN by modbind_withdraw for this
 * capability return to the fixpoint, and everything that fell to MB_HELD comes
 * back on its own. Returns how many modules regained MB_READY.
 *
 * WHY THIS HAS TO EXIST. modbind_resolve deliberately does NOT reset an
 * MB_WITHDRAWN module (that is what makes withdrawal stick across a re-resolve),
 * so without a reverse there is no way out of S− and a withdrawal is permanent.
 * A teardown that cannot be undone is not a state, it is a demolition -- and it
 * makes the S− column untestable, because a negative test must leave the system
 * as it found it. */
uint32_t modbind_restore(const char *capability);

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
 * POWER FACTOR IS THE POINT. Real power is V*I*pf(dphase) where pf is the OPTICAL
 * INTERFERENCE fringe (1 + cos dphase)/2, not raw cos. Two modules perfectly in
 * phase deliver all of it (constructive); two in ANTIPHASE deliver almost none
 * (destructive, 14 permille) while still drawing current. That is the precise
 * description of a boundary that is CONNECTED AND DOING (almost) NO WORK -- which
 * is what 1,674 discarded symbols and every "linked but never exercised" module
 * actually are. They are not disconnected. They are reactive.
 *
 * And it makes phi a control rather than an ornament: phi is the most irrational
 * ratio (continued fraction all ones, Hurwitz worst case), so a phi phase offset
 * NEVER aligns at any harmonic -- it sits near antiphase, the weakest coupling
 * the lattice allows. Matched phase couples strongly; phi damps toward the floor.
 * (The floor is 14 permille, never 0: over 13 discrete phases cos never reaches
 * -1000, so phase alone attenuates but never fully isolates -- see modbind.c.) */
typedef struct {
    char     name[MB_CAP_NAME_LEN];   /* e.g. "mm_ready"                     */
    uint16_t contract;                /* providers must agree                */
    uint8_t  phase;                   /* l13 phase (0..12) this rides on     */
    uint8_t  amplitude;               /* trit_t: the five-level drive level   */
    uint8_t  alternating;             /* 1 = both sides swap roles per phase  */
} mb_cap_t;

/* ---- THE FIVE DRIVE LEVELS, NAMED AT THE DECLARATION SITE -----------------
 * `amplitude` is a trit_t, and the numbers below are trit_t's own enumerators
 * from kernel/include/m5_types.h -- NOT a parallel scale invented here. They are
 * repeated as plain integers for one reason: zxv_decl.h includes THIS header and
 * is itself included by 91 declaring translation units, and m5_types.h carries
 * `double`-returning inlines (trit_to_ell). Pulling scalar FP into every module
 * that merely wants to name a capability is a cost with no benefit, so the two
 * are kept in step by _Static_assert in modbind.c instead of by an include --
 * if anyone renumbers trit_t, the build stops rather than silently rescaling
 * every capability in the system.
 *
 * The permille figures are mb_real_power's own current table, quoted so the
 * choice of default is checkable at the point of use rather than inferred:
 *   MB_AMP_FULL  TRIT_TRUE          1000  full drive -- the DEFAULT for ZXV_CAP
 *   MB_AMP_HIGH  TRIT_GLUT_PLUS      750  constructive superposition, +charge
 *   MB_AMP_HALF  TRIT_GLUT_NEUTRAL   500  balanced superposition,  no charge
 *   MB_AMP_LOW   TRIT_GLUT_MINUS     250  destructive superposition, -charge
 *   MB_AMP_NONE  TRIT_FALSE            0  no drive: DECLARED AND DEAD
 *
 * MB_AMP_NONE WAS THE OLD DEFAULT, AND IT WAS A TRAP. ZXV_CAP expanded to
 * amplitude 0, which canonicalises to TRIT_FALSE, which mb_real_power maps to
 * i_permille = 0 -- so every capability in the system would have delivered ZERO
 * REAL POWER the instant the resolver started asking the physics instead of the
 * name. A capability that is declared is, by the act of declaring it, driven:
 * the module is asserting it supplies this. Silence is spelled by not declaring,
 * or by MB_AMP_NONE on purpose. */
#define MB_AMP_NONE  0u   /* == TRIT_FALSE         */
#define MB_AMP_FULL  1u   /* == TRIT_TRUE          */
#define MB_AMP_HIGH  3u   /* == TRIT_GLUT_PLUS     */
#define MB_AMP_LOW   4u   /* == TRIT_GLUT_MINUS    */
#define MB_AMP_HALF  5u   /* == TRIT_GLUT_NEUTRAL  */

/* The number of distinct phases the l13 lattice carries. The cos table in
 * modbind.c is exactly this long; a phase is taken mod this. */
#define MB_PHASES   13u

/* Real power delivered across a binding, in milliwatt-equivalents (integer, no
 * float in the kernel). Returns 0 for a binding that carries current but does no
 * work -- which is a DIFFERENT and more useful answer than "not connected". */
int32_t mb_real_power(const mb_cap_t *provided, const mb_cap_t *required);

/* Power factor as a permille, the interference fringe (1000 + cos dphase)/2.
 * 1000 = perfectly in phase (constructive, all real); it falls MONOTONICALLY to
 * its floor of 14 at antiphase (k=6, destructive). Never 0: 13 is odd, so phase
 * alone weakens a binding to 14 permille but never opens it. */
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

/* ---- PHASE IS A PROPERTY OF THE MODULE, NOT OF THE CAPABILITY -------------
 * MEASURED, not argued. Any phase derived from the CAPABILITY token cancels:
 * provider and requirer compute the same number from the same token, so dphase
 * is identically 0 on every edge in the graph and nothing differentiates. That
 * is structural for the whole family of capability-keyed derivations, and it is
 * why `phase` lives on the module and the capability merely RIDES it.
 *
 * A cap declared MB_PHASE_INHERIT is saying "I ride my module's phase", which
 * is what every ZXV_CAP does. modbind_register resolves it once, at
 * registration, so nothing downstream ever sees the sentinel: mb_real_power,
 * mb_power_factor, the gate and the fixpoint all read a concrete 0..12.
 *
 * The sentinel is OUTSIDE 0..MB_PHASES-1 on purpose. Using 0 to mean "unset"
 * would collide with L0, which is a real and heavily populated phase (substrate:
 * zphi, rat, sha256, rmag, and both MMU providers), and a sentinel that aliases
 * a legal value is the same defect as TRIT_GLUT aliasing TRIT_GLUT_NEUTRAL --
 * two encodings for one state, which this header already refuses elsewhere. */
#define MB_PHASE_INHERIT 0xFFu

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
    /* The module's own l13 phase (0..MB_PHASES-1). DERIVED, never typed: see
     * build_system/gen_phase_table.sh, which computes it from the module's DRC
     * layer and emits one #define per module for ZXV_DECLARE to paste. */
    uint8_t    phase;
    mb_ready_t ready;
    module_transform_t xform;
    bool       registered;
} mb_module_t;

/* ---- WHY A MODULE IS HELD -------------------------------------------------
 * Under name-matching there were only two ways to be stuck, and modbind_verify_
 * graph inferred one from the absence of the other: nothing provides the name
 * (UNPROVIDED), otherwise it must be a CYCLE. Coupling by POWER introduces a
 * THIRD, and it is the common one in a fluid system:
 *
 *     A PROVIDER EXISTS AND IS NOT IN PHASE.
 *
 * That is not a cycle -- the graph is perfectly acyclic -- and it is not an
 * unprovided requirement -- the capability is right there. It is a boundary that
 * carries current and does no work, exactly what the power-factor model exists
 * to describe. Reporting it as a cycle would turn NORMAL fluid behaviour into a
 * build failure, which is the precise failure mode a phase model must not have.
 *
 * THE REASON PROPAGATES. A module whose provider is itself held out of phase is
 * not in a cycle either -- it is downstream of one that is not coupled. So the
 * classification is a fixpoint over the requires-graph, not a local test, and
 * the ordering is deliberate: a root cause outranks an inherited one, and CYCLE
 * is what is left when no other explanation survives. */
typedef enum {
    MB_HOLD_NONE       = 0,  /* not held, or held with nothing required      */
    MB_HOLD_UNPROVIDED = 1,  /* a requirement no module names at all         */
    /* A provider NAMES it and delivers no real power. Every way mb_real_power
     * can return zero on a name that matches lands here, because they are one
     * physical statement -- the circuit is open: a contract disagreement, zero
     * drive (MB_AMP_NONE), or no carrier on the line. */
    MB_HOLD_PHASE      = 2,
    /* Its provider is in S− -- deliberately retracted. Also not a defect: it is
     * the teardown edge doing exactly what it is for, and it must not be
     * reported as a cycle either, or every negative test of S− would present as
     * a broken graph. */
    MB_HOLD_WITHDRAWN  = 3,
    MB_HOLD_CYCLE      = 4,  /* everything coupled, and still never resolves */
} mb_hold_t;

/* Why is this module HELD? Valid immediately after modbind_resolve() (or
 * modbind_verify_graph, which runs it). MB_HOLD_NONE for a ready module. */
mb_hold_t modbind_hold_reason(const mb_module_t *m);

/* Human-readable, for boot output. Never NULL. */
const char *mb_hold_name(mb_hold_t h);

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
/* The registered copy of a module, by name, or NULL. Registration COPIES, so
 * the declaration a module holds and the registry entry that carries its
 * readiness are two different objects -- this is how you get from one to the
 * other without the caller keeping an index. */
const mb_module_t *modbind_find(const char *name);

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
