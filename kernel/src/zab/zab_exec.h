/* zab_exec.h — the ZAB virtual machine: execute a tri-space artifact.
 *
 * WHY A VM AND NOT JUST A VERIFIER
 * --------------------------------
 * zab.c derives what a program CAN do. That is a static claim about an
 * artifact. This file is where the claim meets the world: it runs the program
 * and REFUSES, per instruction, anything the caller did not grant. Derivation
 * without enforcement would just be a nicer comment.
 *
 * So the invariant is: an effect happens only if BOTH
 *   (a) the instruction's capability is in the caller's grant, and
 *   (b) the host actually supplies a handler for it.
 * A capability that is granted but unimplemented is unavailable, not silently
 * skipped — "granted" is permission, never a promise that something exists.
 *
 * NO AMBIENT AUTHORITY
 * --------------------
 * The VM has no way to reach the kernel, the filesystem or the network by
 * itself. Every effect goes through a `zab_host_t` the caller supplies, exactly
 * as a ZCA card can only touch the one `card_effect_t` handed to it. The
 * writable surface of a program is precisely what its caller chose to expose.
 *
 * TERMINATION IS STRUCTURAL
 * -------------------------
 * ZAB has no jumps, calls or loops, so a program is a straight list executed
 * once and always halts. There is no gas metering here because there is no
 * halting question to price. (A step budget exists only as a belt-and-braces
 * bound in case the instruction set ever grows control flow — if that happens,
 * this comment is the thing to revisit first.)
 *
 * RE-VERIFICATION IS NOT OPTIONAL
 * -------------------------------
 * Execution re-runs the full verifier on the bytes it is about to execute,
 * rather than trusting a capability set derived earlier. The gap between "we
 * checked it" and "we ran it" is where TOCTOU bugs live, and the check is cheap.
 *
 * ROLLBACK, AND ITS HONEST LIMIT
 * ------------------------------
 * The payoff of binding an undo to an action is that rollback is always
 * available: run S+, and if it fails, run S-. But S- is the inverse of the
 * COMPLETE effect of S+. Applying it after a PARTIAL failure assumes S- is safe
 * to apply to a partially-applied state — a property the author must ensure,
 * and precisely why TRI_INV_COMPENSATING exists as a kind distinct from
 * TRI_INV_EXACT. The executed-effect log is reported so a caller that needs
 * finer recovery can drive it itself. Rollback is refused outright for kinds
 * that have no undo (OBSERVATIONAL, CONSTRAINING).
 *
 * Freestanding: integer only, no libc, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV artifact-VM slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_ZAB_EXEC_H
#define ZXV_ZAB_EXEC_H

#include <stdint.h>
#include <stdbool.h>
#include "zab.h"

#define ZAB_MAX_LOG   64u    /* effects recorded per run (audit trail)        */
#define ZAB_MAX_STEPS 1024u  /* belt-and-braces bound; ZAB cannot loop today  */

/* The host effect interface. Every handler returns 0 on success, non-zero to
 * fail the instruction (and therefore the run). A NULL handler means the host
 * does not provide that effect at all. */
typedef struct {
    int (*observe)     (void *ctx, uint8_t a, uint16_t b);
    int (*read_state)  (void *ctx, uint8_t a, uint16_t b);
    int (*write_state) (void *ctx, uint8_t a, uint16_t b);
    int (*fs_read)     (void *ctx, uint8_t a, uint16_t b);
    int (*fs_write)    (void *ctx, uint8_t a, uint16_t b);
    int (*ledger_post) (void *ctx, uint8_t a, uint16_t b);
    int (*net_send)    (void *ctx, uint8_t a, uint16_t b);
    int (*spawn)       (void *ctx, uint8_t a, uint16_t b);
    int (*emit)        (void *ctx, uint8_t a, uint16_t b);
    void *ctx;
} zab_host_t;

typedef enum {
    ZABX_OK = 0,
    ZABX_ERR_VERIFY      = -1,  /* the bytes do not verify as a ZAB program  */
    ZABX_ERR_CAP_DENIED  = -2,  /* instruction needs a capability not granted*/
    ZABX_ERR_NO_HOST     = -3,  /* granted, but the host provides no handler */
    ZABX_ERR_HOST_FAILED = -4,  /* the host rejected the effect              */
    ZABX_ERR_BUDGET      = -5,  /* step bound exceeded (should be impossible)*/
    ZABX_ERR_ARGS        = -6
} zab_exec_result_t;

/* What happened, whether or not the run succeeded. */
typedef struct {
    zab_exec_result_t result;
    uint32_t steps;                 /* instructions executed                 */
    uint32_t effects;               /* effectful instructions executed       */
    uint32_t caps_used;             /* union of capabilities actually used   */
    uint32_t fault_pc;              /* instruction index where it stopped    */
    uint8_t  fault_op;              /* opcode at that index                  */
    uint16_t log_count;             /* entries in `log_op` (may saturate)    */
    uint8_t  log_op[ZAB_MAX_LOG];   /* the effects that ACTUALLY happened    */
} zab_exec_t;

/* Execute `prog` with `granted` capabilities against `host`.
 * Re-verifies the program first. Stops at the first denied or failing
 * instruction; `out` always describes what ran before that point, so a partial
 * run is auditable rather than opaque. `out` may be NULL. */
zab_exec_result_t zab_execute(const uint8_t *prog, uint32_t len,
                              uint32_t granted, const zab_host_t *host,
                              zab_exec_t *out);

const char *zab_exec_result_name(zab_exec_result_t r);

#endif /* ZXV_ZAB_EXEC_H */
