/* syscall.c — the ZXV ABI table. See syscall.h.
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV ABI slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "syscall.h"

/* The one table, expanded. Because every row comes from ZXV_SYSCALL_TABLE, the
 * name, capability and arity for a given number cannot disagree with each other
 * -- there is only one place any of them is written. */
static const zxv_syscall_info_t TABLE[] = {
#define ZXV_SC_ROW(n, name, cap, arity, doc) \
    { (n), #name, (cap), (arity), (doc), ((n) == 0) },
    ZXV_SYSCALL_TABLE(ZXV_SC_ROW)
#undef ZXV_SC_ROW
};
#define NSYS (sizeof(TABLE) / sizeof(TABLE[0]))

uint32_t zxv_syscall_count(void) { return (uint32_t)NSYS; }

bool zxv_syscall_info(uint32_t nr, zxv_syscall_info_t *out) {
    if (nr >= NSYS) return false;
    /* The table is dense and ascending (asserted by the selfcheck), so the
     * number IS the index. No search, and no chance of a row moving. */
    if (out) *out = TABLE[nr];
    return true;
}

int zxv_syscall_permit(uint32_t nr, uint32_t caps) {
    if (nr >= NSYS) return ZXV_ENOSYS;
    const zxv_syscall_info_t *s = &TABLE[nr];
    if (s->reserved) return ZXV_ENOSYS;      /* a hole is not a syscall */
    /* Required bits must ALL be held. Note the direction: we test what the
     * call demands against what the caller has, never the reverse -- a caller
     * holding extra capabilities is fine, a call needing one it lacks is not. */
    if ((s->capability & ~caps) != 0) return ZXV_EPERM;
    return ZXV_OK;
}

bool zxv_abi_compatible(uint32_t want) {
    uint32_t want_major = want >> 16, want_minor = want & 0xFFFFu;
    if (want_major != ZXV_ABI_MAJOR) return false;
    /* A caller built against a NEWER minor expects calls this kernel may not
     * have, so it is refused rather than half-served. */
    return want_minor <= ZXV_ABI_MINOR;
}

uint32_t zxv_syscall_selfcheck(void) {
    uint32_t bad = 0;
    const uint32_t legal = ZSC_CAP_WRITE | ZSC_CAP_PROC | ZSC_CAP_EXEC |
                           ZSC_CAP_IPC | ZSC_CAP_FS;
    for (uint32_t i = 0; i < NSYS; i++) {
        const zxv_syscall_info_t *s = &TABLE[i];
        if (s->nr != i) bad++;                       /* dense + ascending */
        if (!s->name || !s->name[0]) bad++;
        if (!s->doc || !s->doc[0]) bad++;
        if ((s->capability & ~legal) != 0) bad++;    /* no invented bits  */
        if (s->arity > 4) bad++;
        if (s->reserved && i != 0) bad++;            /* holes are declared */
    }
    return bad;
}
