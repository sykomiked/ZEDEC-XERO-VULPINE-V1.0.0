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
} mb_err_t;

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

typedef struct {
    char       name[MB_NAME_LEN];
    mb_port_t  emits[MB_MAX_PORTS];
    uint8_t    n_emits;
    mb_port_t  ingests[MB_MAX_PORTS];
    uint8_t    n_ingests;
    module_transform_t xform;
    bool       registered;
} mb_module_t;

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
