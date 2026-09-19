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
 * A CAPABILITY NAMES A VERB. A SCOPE NAMES THE OBJECT.
 * ----------------------------------------------------
 * The version of this file that had only `zab_host_t` handlers bound the whole
 * table wholesale — `h.fs_write = (granted & ZAB_CAP_FS_WRITE) ? real : NULL`.
 * So ZAB_CAP_FS_WRITE meant "write THE FILESYSTEM", not "write THIS FILE". No
 * artifact could acquire a capability it was not granted (that edge held), but
 * any artifact legitimately holding one could reach every object the host could
 * reach. Least privilege failed INSIDE the grant rather than at its edge.
 *
 * A grant is therefore now a pair: the verb mask (`granted`) AND a table of
 * EXTENTS the host binds into `zab_host_t.scopes`. Each instruction SELECTS an
 * extent with its `a` operand, and the effect happens only if all four hold:
 *   (a) the instruction's capability is in the caller's grant,
 *   (b) the host actually supplies a handler for it,
 *   (c) the selected extent exists, is well-formed, and CARRIES that verb,
 *   (d) the instruction's `b` operand lies inside that extent.
 *
 * WHY `a` IS THE SELECTOR AND THE TABLE BELONGS TO THE HOST
 * ---------------------------------------------------------
 * ZAB rule 2 is "NO ADDRESSES: no opcode names a memory location", and an
 * instruction is four fixed bytes with nowhere to put a path. An object must
 * therefore be named INDIRECTLY, through a table the program cannot write —
 * exactly as a file descriptor is an index into a table the kernel owns. The
 * program chooses AMONG what it was offered; it can never invent an object,
 * because inventing one would mean writing an entry, and only the host writes
 * entries. This needs no new opcode, no format version bump and no change to
 * capability derivation: `a` was previously ignored by every handler in the
 * tree, and the seal already covers it, so a program cannot be re-aimed at a
 * different extent without breaking verification.
 *
 * FAIL CLOSED, ON EVERY AXIS
 * --------------------------
 * No table, an empty table, a table longer than ZAB_MAX_SCOPES, a slot past the
 * end, an entry with an unrecognised kind, an unterminated or empty name, a
 * zero or wrapping extent, undefined capability bits, a verb the extent does
 * not carry, or an offset past the extent: every one of these DENIES with
 * ZABX_ERR_SCOPE. There is no path on which an unrecognised scope means
 * "everything" — a zero-initialised host grants nothing at all, which is why
 * the two in-tree callers (zxvfs_tri_execute, vena_execute_contract) DERIVE the
 * extent they bind rather than leaving the field blank.
 *
 * Note the deliberate asymmetry with zab_op_capability(): an unknown OPCODE
 * returns ZAB_CAP_ALL (it could do anything, so refuse it), and an unknown
 * SCOPE KIND returns ZAB_CAP_NONE (it can carry nothing, so refuse it). Both
 * defaults point the same way — toward refusal.
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

/* ---- SCOPE: the OBJECT half of a capability -----------------------------
 *
 * NAME LENGTH. 32 is ZXVFS_NAME_LEN — the widest name any addressable object
 * in this tree has. It is not a round number chosen for looks: a scope name
 * SHORTER than the filesystem's would have to truncate, and two distinct
 * objects sharing a prefix would become one extent. A capability system whose
 * names collide is worse than none, so the field holds a full name or the
 * binding is refused.
 *
 * SLOT COUNT. The selector is a uint8_t operand, so 256 slots are expressible,
 * but a host that binds hundreds of extents into one run has stopped
 * least-privileging anything. 32 is the bound; a host declaring more is a bug
 * and the whole run is refused rather than the surplus quietly ignored. */
#define ZAB_SCOPE_NAME_LEN 32u
#define ZAB_MAX_SCOPES     32u

/* What `name` denotes. The kind is not decoration: zab_scope_kind_caps() maps
 * it to the verbs that kind can EVER carry, so a host that fat-fingers
 * ZAB_CAP_FS_WRITE onto a network peer is refused rather than obeyed. */
typedef enum {
    ZAB_SCOPE_NONE     = 0,   /* unset — carries nothing, always denies      */
    ZAB_SCOPE_FILE     = 1,   /* a file: name is a filesystem name           */
    ZAB_SCOPE_STATE    = 2,   /* a shared-state region                       */
    ZAB_SCOPE_LEDGER   = 3,   /* a ledger / account                          */
    ZAB_SCOPE_PEER     = 4,   /* a network peer                              */
    ZAB_SCOPE_EVENT    = 5,   /* an event channel                            */
    ZAB_SCOPE_PROCESS  = 6,   /* a process/cell nursery                      */
    ZAB_SCOPE_CONTRACT = 7,   /* a contract acting on its own runtime        */
    ZAB_SCOPE_TRIAD    = 8,   /* a stored triad acting on itself             */
    ZAB_SCOPE__MAX
} zab_scope_kind_t;

/* One extent. Plain scalars and a fixed name: an entry lives happily in
 * .rodata, needs no allocator, and can be built on a caller's stack.
 *
 * `base`/`span` are NOT memory — ZAB rule 2 forbids an opcode naming an
 * address, and nothing here changes that. They are an abstract extent inside
 * the named object (a byte range, a record range, a slot range) whose units
 * only the host and the artifact's author need to agree on. The VM's job is
 * narrower and total: the instruction's `b` operand must fall inside it. */
typedef struct {
    char     name[ZAB_SCOPE_NAME_LEN];  /* the OBJECT; NUL-terminated        */
    uint32_t caps;                      /* verbs permitted ON THIS OBJECT    */
    uint32_t base;                      /* extent start, host-defined units  */
    uint32_t span;                      /* extent length; 0 denies           */
    uint8_t  kind;                      /* zab_scope_kind_t                  */
    uint8_t  reserved[3];               /* MUST be zero (else the entry is
                                         * from a future this VM cannot read
                                         * and is refused)                   */
} zab_scope_t;

/* The host effect interface. Every handler returns 0 on success, non-zero to
 * fail the instruction (and therefore the run). A NULL handler means the host
 * does not provide that effect at all.
 *
 * The handler signature is UNCHANGED, and deliberately so: a handler already
 * receives `a`, which IS the slot the VM resolved and bounds-checked against
 * the host's own table. A host that binds several objects indexes its table
 * with `a` to learn which one; a host that binds one ignores it exactly as
 * before. Adding a scoped handler vector would have doubled the interface to
 * deliver information the old signature already carries. */
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
    /* ---- the extents this host binds. APPENDED, so every existing host that
     * zeroes the struct and assigns handlers by name still compiles — and
     * lands on n_scopes == 0, which grants NOTHING. Backward compatible at the
     * source level, fail-closed at the security level. ---- */
    const zab_scope_t *scopes;
    uint16_t           n_scopes;
    uint16_t           reserved;        /* MUST be zero                      */
} zab_host_t;

typedef enum {
    ZABX_OK = 0,
    ZABX_ERR_VERIFY      = -1,  /* the bytes do not verify as a ZAB program  */
    ZABX_ERR_CAP_DENIED  = -2,  /* instruction needs a capability not granted*/
    ZABX_ERR_NO_HOST     = -3,  /* granted, but the host provides no handler */
    ZABX_ERR_HOST_FAILED = -4,  /* the host rejected the effect              */
    ZABX_ERR_BUDGET      = -5,  /* step bound exceeded (should be impossible)*/
    ZABX_ERR_ARGS        = -6,
    ZABX_ERR_SCOPE       = -7   /* the verb is granted, but not on THIS object*/
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
    /* ---- appended with scoping: an audit trail that records only the VERB
     * cannot answer "what did it touch?", which is the whole question this
     * change exists to make answerable. ---- */
    uint8_t  fault_slot;            /* scope slot the faulting instruction chose */
    uint8_t  log_slot[ZAB_MAX_LOG]; /* the OBJECT each logged effect touched */
} zab_exec_t;

/* The verbs a scope kind can EVER carry. Total over uint8_t: any value that is
 * not a defined kind returns ZAB_CAP_NONE, so an unrecognised scope carries
 * nothing and every effect against it is refused. */
uint32_t zab_scope_kind_caps(uint8_t kind);

/* Is this entry something the VM can reason about at all? Checks the kind, the
 * name's termination and non-emptiness, the reserved bytes, the capability
 * bits against ZAB_CAP_ALL, and that base+span neither is empty nor wraps.
 * A malformed entry is refused, never repaired. */
bool zab_scope_valid(const zab_scope_t *sc);

/* May `need` (a capability mask) act on this extent at offset `off`?
 * The permitted set is the entry's own caps INTERSECTED with what its kind can
 * carry, so both the host's intent and the kind's ceiling must agree. */
bool zab_scope_permits(const zab_scope_t *sc, uint32_t need, uint16_t off);

/* Resolve a slot against a host's table. NULL for no table, an over-long
 * table, or a slot past the end — all of which deny. */
const zab_scope_t *zab_scope_resolve(const zab_host_t *host, uint8_t slot);

/* Fill `sc` with a well-formed extent. Returns false — leaving `sc` zeroed and
 * therefore denying — if the name does not fit with its NUL, if the kind
 * cannot carry `caps`, or if the extent is empty or wraps. Callers building a
 * scope from runtime data (a filesystem name, a contract name) MUST check the
 * return: a truncated name would name the wrong object. */
bool zab_scope_set(zab_scope_t *sc, const char *name, uint8_t kind,
                   uint32_t caps, uint32_t base, uint32_t span);

/* Execute `prog` with `granted` capabilities against `host`.
 * Re-verifies the program first. Stops at the first denied or failing
 * instruction; `out` always describes what ran before that point, so a partial
 * run is auditable rather than opaque. `out` may be NULL.
 *
 * The signature is unchanged. The extents travel in `host->scopes` because the
 * host IS the statement of what a program may touch — "the writable surface of
 * a program is precisely what its caller chose to expose" — and because that
 * is what let this become fail-closed without breaking a single call site. */
zab_exec_result_t zab_execute(const uint8_t *prog, uint32_t len,
                              uint32_t granted, const zab_host_t *host,
                              zab_exec_t *out);

const char *zab_exec_result_name(zab_exec_result_t r);

#endif /* ZXV_ZAB_EXEC_H */
