/* zab.h — ZAB: ZXV Artifact Bytecode, and the capability VERIFIER
 *
 * THE HOLE THIS CLOSES
 * --------------------
 * Tri-Space requirement 2 says "S- MUST NOT hold more capabilities than S+".
 * Until now that was checked against `tri_member_t.capability_set` — a uint32_t
 * the AUTHOR DECLARES. Nothing derived it from the artifact. So a hostile or
 * careless S- could simply declare `capability_set = 0` and carry any power it
 * liked: the rule passed, the seal covered the lie, and the whole triad
 * guarantee rested on a self-report.
 *
 * This module derives the capability set from the artifact's own instructions,
 * so the rules are enforced against what the code CAN DO rather than what it
 * SAYS it does.
 *
 * WHY CAPABILITY IS IMPLIED BY THE OPCODE, NOT GRANTED BY ONE
 * -----------------------------------------------------------
 * The obvious design is a GRANT instruction that names the capabilities a
 * program wants. That is the SAME self-report one level down: an author who
 * omits the GRANT while still using an effectful opcode is back to lying.
 *
 * So in ZAB there is no GRANT. Every effectful opcode IMPLIES the capability it
 * needs (see zab_op_capability), and the derived set is the union over every
 * instruction present. To carry a capability you must contain an instruction
 * that uses it, and containing that instruction IS carrying it. The only way to
 * misreport is a bug in the opcode->capability table, which is one small,
 * auditable, exhaustively tested array rather than a promise per artifact.
 *
 * INHERITED FROM ZCA (kernel/src/cards/zca.h) — the same safety spine
 * -------------------------------------------------------------------
 *  1. TOTAL. No jumps, no calls, no loops. A program is a straight list of at
 *     most ZAB_MAX_INS instructions. Verification always terminates; there is
 *     no halting question to get wrong.
 *  2. NO ADDRESSES. No opcode names a memory location.
 *  3. BOUNDED. Operands are masked; unknown opcodes are REJECTED, never
 *     skipped — an unrecognised instruction could imply any capability at all,
 *     so fail-closed is the only safe reading.
 *  4. SEALED. A checksum over the body; a truncated or garbled artifact is
 *     rejected rather than partially analysed.
 *
 * DATA IS NOT CODE (and carries no capability)
 * --------------------------------------------
 * An artifact that is not a ZAB program is data. Data can do nothing, so its
 * derived capability set is EMPTY — and declaring any capability on a data
 * artifact is itself a misdeclaration. This keeps the rule total: every
 * artifact has a derived set, so there is no "unanalysable, therefore trusted"
 * category.
 *
 * Freestanding: integer only, no libc, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV capability-verifier slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_ZAB_H
#define ZXV_ZAB_H

#include <stdint.h>
#include <stdbool.h>

#define ZAB_MAGIC      0x5A414231u   /* 'ZAB1' */
#define ZAB_VERSION    1u
#define ZAB_MAX_INS    256u          /* straight list; bounded by construction */

/* ---- capabilities -------------------------------------------------------
 * A capability is the authority to affect one class of thing. The PRODUCTION
 * mask is the subset that changes the world outside the process — it is what
 * requirement 3 ("S0 must hold NO production-effect capability") tests. */
#define ZAB_CAP_NONE         0x00000000u
#define ZAB_CAP_OBSERVE      0x00000001u  /* read own state; audit; no effect  */
#define ZAB_CAP_READ_STATE   0x00000002u  /* read shared/system state          */
#define ZAB_CAP_WRITE_STATE  0x00000004u  /* mutate shared/system state    (P) */
#define ZAB_CAP_FS_READ      0x00000008u  /* read the filesystem               */
#define ZAB_CAP_FS_WRITE     0x00000010u  /* write the filesystem          (P) */
#define ZAB_CAP_LEDGER       0x00000020u  /* post to the triple ledger     (P) */
#define ZAB_CAP_NET          0x00000040u  /* send on the network           (P) */
#define ZAB_CAP_SPAWN        0x00000080u  /* create a process/cell         (P) */
#define ZAB_CAP_EMIT         0x00000100u  /* emit an event other code sees (P) */

/* (P) above: production effect — acts on the world beyond this artifact. */
#define ZAB_CAP_PRODUCTION  (ZAB_CAP_WRITE_STATE | ZAB_CAP_FS_WRITE | \
                             ZAB_CAP_LEDGER | ZAB_CAP_NET | \
                             ZAB_CAP_SPAWN | ZAB_CAP_EMIT)
#define ZAB_CAP_ALL         (ZAB_CAP_OBSERVE | ZAB_CAP_READ_STATE | \
                             ZAB_CAP_FS_READ | ZAB_CAP_PRODUCTION)

/* ---- instruction set ----------------------------------------------------
 * Each opcode is bound to exactly one capability by zab_op_capability(). Adding
 * an opcode without adding its capability is caught by test_zab.c, which walks
 * the whole opcode range and requires every one to be classified. */
typedef enum {
    ZAB_OP_NOP        = 0,   /* nothing                                       */
    ZAB_OP_OBSERVE    = 1,   /* inspect own state / assert an invariant       */
    ZAB_OP_READ       = 2,   /* read shared state                             */
    ZAB_OP_WRITE      = 3,   /* mutate shared state                       (P) */
    ZAB_OP_FS_READ    = 4,   /* read a file                                   */
    ZAB_OP_FS_WRITE   = 5,   /* write a file                              (P) */
    ZAB_OP_POST       = 6,   /* post a ledger entry                       (P) */
    ZAB_OP_SEND       = 7,   /* send on the network                       (P) */
    ZAB_OP_SPAWN      = 8,   /* create a process/cell                     (P) */
    ZAB_OP_EMIT       = 9,   /* emit an event                             (P) */
    ZAB_OP_END        = 10,  /* terminator                                    */
    ZAB_OP__MAX
} zab_op_t;

/* One instruction: 4 bytes, fixed. No operand can name an address. */
typedef struct {
    uint8_t  op;
    uint8_t  a;
    uint16_t b;
} zab_ins_t;

/* Program header, then `count` instructions, then the seal. */
typedef struct {
    uint32_t magic;      /* ZAB_MAGIC   */
    uint16_t version;    /* ZAB_VERSION */
    uint16_t count;      /* instruction count, <= ZAB_MAX_INS */
    uint32_t seal;       /* checksum over the header (seal=0) + instructions */
} zab_header_t;

/* Verification verdicts. Anything other than ZAB_OK means the artifact was
 * NOT analysed and must not be trusted — never "assume no capabilities". */
typedef enum {
    ZAB_OK = 0,
    ZAB_ERR_NOT_PROGRAM  = -1,  /* no ZAB magic: it is data, not code        */
    ZAB_ERR_TRUNCATED    = -2,  /* header/body shorter than declared         */
    ZAB_ERR_VERSION      = -3,
    ZAB_ERR_TOO_LONG     = -4,  /* count > ZAB_MAX_INS                       */
    ZAB_ERR_BAD_OPCODE   = -5,  /* unknown opcode — fail closed              */
    ZAB_ERR_SEAL         = -6,  /* checksum mismatch: garbled or tampered    */
    ZAB_ERR_NO_END       = -7   /* program does not terminate with END       */
} zab_result_t;

/* The capability a single opcode implies. Total over uint8_t: any value that
 * is not a defined opcode returns ZAB_CAP_ALL, so an unknown instruction can
 * never look harmless. (Callers still reject it; this is defence in depth.) */
uint32_t zab_op_capability(uint8_t op);

/* Derive the capability set a ZAB program actually carries: the union over
 * every instruction. `out_caps` is set only on ZAB_OK. */
zab_result_t zab_derive_capabilities(const uint8_t *buf, uint32_t len,
                                     uint32_t *out_caps);

/* Derive the capability set of ANY artifact, program or not:
 *   a valid ZAB program -> the union of what its instructions imply
 *   plain data          -> ZAB_CAP_NONE (data cannot act)
 *   a MALFORMED program -> failure; *out_caps untouched, caller must refuse
 * Returns true only when a set could be established. */
bool zab_artifact_capabilities(const uint8_t *buf, uint32_t len,
                               uint32_t *out_caps, zab_result_t *why);

/* Compute the seal for a program body (used by builders and by tests). */
uint32_t zab_compute_seal(const zab_header_t *h, const zab_ins_t *ins,
                          uint16_t count);

/* Human-readable names — for diagnostics, never for control flow. */
const char *zab_op_name(uint8_t op);
const char *zab_result_name(zab_result_t r);

#endif /* ZXV_ZAB_H */
