/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_ext.h — method groups that plug into zxvd's control socket.
 *
 * Kernel engines that run inside zxvd (netting, ISO 20022, cards, passkeys,
 * the economy modules) each live in their own zxvd_<group>.c and register a
 * table of methods here, so zxvd_rpc.c stays the one transport and the one
 * JSON reader and each group can be tested and mutation-scored on its own.
 *
 * GROUPS
 *   X1  zxvd_rpc_add_group registers a group before the socket opens. A group
 *       has a feature name (the switch it sits behind), a regulated flag,
 *       and its methods. Method names must be new: a name the core, an
 *       extension table (zxvd_rpc.h X1, for methods with no switch) or an
 *       earlier group already serves is refused, so a group can never
 *       shadow ledger.* or another group. At most ZXVD_EXT_MAX_GROUPS.
 *       zxvd_rpc.h X2-X7 (zxvd_result, zxvd_fail, zxvd_param_*, the books)
 *       apply to group methods too.
 *   X2  rpc.discover lists every registered method, on or off, so the
 *       methods zxvd serves do not depend on its switches and the spec
 *       check (check_zxvd_spec.py) sees one fixed list.
 *
 * FEATURE SWITCHES (features.get / features.set; zxvd --enable LIST)
 *   X3  Every feature starts OFF. zxvd --enable netting,cards (or "all")
 *       turns features on at start; features.set turns one on or off while
 *       zxvd runs. X7: with a books file (zxvd_rpc.h F1), each change made
 *       by features.set is one "feat" record, written before it applies and
 *       replayed at start, so a restart keeps the owner's choice; without a
 *       file it lasts for this run only. --enable is never saved, and a
 *       saved "off" never turns off a feature --enable named. A record for
 *       a feature this build lacks is skipped; a malformed one stops zxvd
 *       from starting, like any damaged record.
 *   X4  A method of a feature that is off answers ZXVD_E_OFF and does
 *       nothing, unless its table entry is marked `always` (status and
 *       read-only descriptions that move no money).
 *   X5  A regulated feature moves or records money, credit, securities or
 *       identity data that law regulates somewhere. Turning it on is the
 *       owner's choice for a test or a licensed deployment; features.get
 *       says so and zxvd never claims a licence, a membership or compliance.
 *   X6  Turning a feature off keeps its engine's state; turning it back on
 *       in the same run continues from there.
 *
 * READING PARAMETERS (all fail the call with -32602 and a reason)
 *   P1  zxvd_p_str/_i64/_u32/_amount/_bool follow zxvd_rpc.h M3: integers
 *       only, 64-bit amounts also as decimal strings, never rounded.
 *   P2  zxvd_p_hex reads a hex string into bytes (even length, at most cap).
 *   P3  Arrays of objects: zxvd_p_len, then zxvd_p_enter/zxvd_p_leave around
 *       the element's own zxvd_p_* reads; arrays of integers: zxvd_p_i64_at.
 */
#ifndef ZXVD_EXT_H
#define ZXVD_EXT_H

#include "zxvd_rpc.h"

#define ZXVD_EXT_MAX_GROUPS   16u
#define ZXVD_EXT_MAX_FEATURES 16u
#define ZXVD_E_OFF            (-32012) /* X4: the method's feature is off */

typedef struct {
    const char *name;
    void (*fn)(zxvd_call_t *c);
    bool always; /* X4: answers while the feature is off */
} zxvd_group_method_t;

typedef struct {
    const char *feature; /* the switch, e.g. "netting" (lower case, 1..23 bytes) */
    bool regulated;      /* X5 */
    const char *note;    /* one line for features.get */
    const zxvd_group_method_t *methods;
    uint32_t n_methods;
    void (*close)(void); /* zxvd_rpc_close; may be NULL */
} zxvd_group_t;

/* X1: 0, or -1 (table full, bad feature name, a clashing method name). The
 * group and its methods must outlive zxvd (static tables). */
int zxvd_rpc_add_group(const zxvd_group_t *g);
/* X3: turn on a comma-separated list of features ("all" = every one). 0, or
 * -1 when a name is unknown (the known ones in the list are still set). */
int zxvd_rpc_enable(const char *list);
bool zxvd_rpc_enabled(const char *feature);
/* Forget every group and switch (zxvd_rpc_close and the unit tests). */
void zxvd_rpc_groups_reset(void);

/* the call: its result buffer is zxvd_result, its error zxvd_fail (zxvd_rpc.h X2) */
zxvd_buf_t *zxvd_res(zxvd_call_t *c); /* = zxvd_result */
bool zxvd_failed(const zxvd_call_t *c);

/* parameters (P1-P3); each returns false after failing the call */
bool zxvd_p_has(zxvd_call_t *c, const char *key);
bool zxvd_p_str(zxvd_call_t *c, const char *key, char *out, uint32_t cap, bool required);
bool zxvd_p_i64(zxvd_call_t *c, const char *key, int64_t *v);
bool zxvd_p_u32(zxvd_call_t *c, const char *key, uint32_t *v);
bool zxvd_p_amount(zxvd_call_t *c, const char *key, int64_t *v);
bool zxvd_p_bool(zxvd_call_t *c, const char *key, bool def, bool *v);
bool zxvd_p_hex(zxvd_call_t *c, const char *key, uint8_t *out, uint32_t cap, uint32_t *n,
                bool required);
/* elements of an array: -1 when absent (no failure), else the count; a
 * value that is not an array fails the call (returns -2) */
int32_t zxvd_p_len(zxvd_call_t *c, const char *key);
bool zxvd_p_enter(zxvd_call_t *c, const char *key, uint32_t idx, int32_t *saved);
void zxvd_p_leave(zxvd_call_t *c, int32_t saved);
bool zxvd_p_i64_at(zxvd_call_t *c, const char *key, uint32_t idx, int64_t *v);

/* a hex string of n bytes, quoted, into b */
void zxvd_buf_hex(zxvd_buf_t *b, const uint8_t *p, size_t n);

#endif
