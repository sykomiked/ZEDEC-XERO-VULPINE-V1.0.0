/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gatehouse_rules.h — Gatehouse's rules in C, the reference its Eve, KRON,
 * C++ (WebAssembly) and Trebago blocks are checked against
 * (blocks/gatehouse). Pure integer functions, no state: the model
 * (gatehouse_model.c) and the language test (tests/gatehouse_zxapp.inc) both
 * include this, so the languages are tested against the very C the model
 * runs.
 *
 *   GR1 THE CAPABILITIES are zxapp.c's one table (R3), in its order; a
 *       capability's index is its bit in a mask. The language test checks
 *       this copy against zxapp_caps(), so the two cannot drift.
 *   GR2 A FLAG is 0 (no) or 1 (yes). Anything else is no flag: the rules
 *       below refuse it (never read it as yes), and the languages hold it.
 *   GR3 A USE IS ALLOWED when the app asked for the capability (a `needs`
 *       line of its manifest), the owner granted it, and the owner has not
 *       revoked it: asked AND granted AND not revoked, all three flags.
 *   GR4 HOLDERS of a capability: the count of apps whose flag says they may
 *       use it, one flag per app; refused when a flag is no flag. */
#ifndef GATEHOUSE_RULES_H
#define GATEHOUSE_RULES_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define GATEHOUSE_CAPS    19u /* zxapp.c R3 */
#define GATEHOUSE_REFUSED (-1)

/* GR1: zxapp.c CAPS, the same names in the same order */
static const char *const GATEHOUSE_CAP_NAME[GATEHOUSE_CAPS] = {
    "net",      "peers",  "location", "fs.read", "fs.write", "ledger.read", "ledger.post",
    "camera",   "mic",    "print",    "scan",    "backup",   "journal",     "screen.time",
    "sys.read", "search", "swarm",    "ai.ask",  "notify"};

/* GR1: a capability's index, or -1 when zxapp.c has no such name */
static inline int32_t gatehouse_cap_index(const char *name)
{
    if (!name) return -1;
    for (uint32_t i = 0; i < GATEHOUSE_CAPS; i++)
        if (!strcmp(GATEHOUSE_CAP_NAME[i], name)) return (int32_t) i;
    return -1;
}

/* GR2 */
static inline bool gatehouse_flag(int64_t f)
{
    return f == 0 || f == 1;
}

/* GR3 as a truth: false for a flag that is no flag */
static inline bool gatehouse_use_ok(int64_t asked, int64_t granted, int64_t revoked)
{
    return asked == 1 && granted == 1 && revoked == 0;
}

/* GR3 as a number to count with: 1 allowed, 0 not, GATEHOUSE_REFUSED for a
 * flag that is no flag */
static inline int64_t gatehouse_use_allowed(int64_t asked, int64_t granted, int64_t revoked)
{
    if (!gatehouse_flag(asked) || !gatehouse_flag(granted) || !gatehouse_flag(revoked))
        return GATEHOUSE_REFUSED;
    return gatehouse_use_ok(asked, granted, revoked) ? 1 : 0;
}

/* GR4 */
static inline int64_t gatehouse_holders(const int64_t *held, uint32_t n)
{
    int64_t c = 0;
    if (!held && n) return GATEHOUSE_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (!gatehouse_flag(held[i])) return GATEHOUSE_REFUSED;
        c += held[i];
    }
    return c;
}

#endif
