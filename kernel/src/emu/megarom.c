/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* megarom.c — the console cartridge registry + boot selector. See megarom.h. */
#include "megarom.h"

static megarom_t g_roms[MEGAROM_MAX];
static int       g_count   = 0;
static int       g_current = -1;

int megarom_register(const megarom_t *m){
    if (!m || !m->name || g_count >= MEGAROM_MAX) return -1;
    g_roms[g_count] = *m;
    return g_count++;
}

int megarom_count(void){ return g_count; }

const megarom_t *megarom_get(int i){
    if (i < 0 || i >= g_count) return 0;
    return &g_roms[i];
}

int megarom_boot(int i){
    if (i < 0 || i >= g_count) return -1;
    g_current = i;
    if (g_roms[i].boot) g_roms[i].boot();   /* activate this UI layer */
    return 0;
}

int megarom_current(void){ return g_current; }

/* ---- self-check ---- */
static int s_booted_flag;
static void s_demo_boot(void){ s_booted_flag = 1; }

int megarom_selfcheck(void){
    /* NB: uses the real registry, so it appends after any earlier registrations
     * (e.g. the desktop as MegaROM #0). It verifies the mechanism, not slot 0. */
    int base = megarom_count();
    megarom_t a = { "SELFTEST-A", "console registry probe", MR_KIND_TOOL, 0 };
    megarom_t b = { "SELFTEST-B", "boot-dispatch probe",    MR_KIND_TOOL, s_demo_boot };
    int ia = megarom_register(&a);
    int ib = megarom_register(&b);
    if (ia != base || ib != base + 1) return 0;                 /* registration order */
    if (megarom_count() < base + 2) return 0;                   /* count grew          */
    const megarom_t *ga = megarom_get(ia);
    if (!ga || ga->name[0] != 'S') return 0;                    /* read-back           */
    s_booted_flag = 0;
    if (megarom_boot(ib) != 0) return 0;                        /* boot dispatch        */
    if (!s_booted_flag) return 0;                               /* the hook actually ran */
    if (megarom_current() != ib) return 0;                     /* current tracks boot   */
    return 1;
}
