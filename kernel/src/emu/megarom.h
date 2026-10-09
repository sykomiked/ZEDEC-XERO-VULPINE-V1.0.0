/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* megarom.h — the CONSOLE model: the system's interface resembles a game
 * console, and MegaROMs ARE the bootable UI. A MegaROM is a self-contained UI
 * layer you "boot" like a cartridge; the console front-end lists the registered
 * MegaROMs and boots one as the active interface. The 13-space lattice desktop
 * is MegaROM #0; games, tools, and future UI layers register alongside it.
 *
 * This is the registry + selector — the console's cartridge slot. The actual
 * rendering of a given MegaROM lives in its own module (e.g. zxv_shell for the
 * desktop); a MegaROM's `boot` hook makes it the active layer. */
#ifndef ZXV_MEGAROM_H
#define ZXV_MEGAROM_H

#include <stdint.h>

#define MEGAROM_MAX 16

typedef enum {
    MR_KIND_DESKTOP = 0,   /* a full desktop/shell UI layer      */
    MR_KIND_GAME    = 1,   /* an emulated / native game universe */
    MR_KIND_TOOL    = 2,   /* a focused tool surface             */
    MR_KIND_SYSTEM  = 3,   /* a system/boot layer                */
} megarom_kind_t;

typedef struct megarom {
    const char    *name;        /* short cartridge name                    */
    const char    *tagline;     /* one line shown on the console selector  */
    megarom_kind_t kind;
    void         (*boot)(void); /* activate this UI layer (may be NULL)    */
} megarom_t;

/* Register a bootable MegaROM. Returns its slot index, or -1 if the slots are
 * full or the descriptor is invalid. */
int  megarom_register(const megarom_t *m);

/* How many MegaROMs are registered, and read-only access to one. */
int  megarom_count(void);
const megarom_t *megarom_get(int i);

/* Boot MegaROM `i` as the active interface (calls its boot hook if any).
 * Returns 0 on success, -1 on a bad index. */
int  megarom_boot(int i);

/* The currently-booted MegaROM index, or -1 if none has been booted. */
int  megarom_current(void);

/* On-target self-check: register + list + boot dispatch. Returns 1 on pass. */
int  megarom_selfcheck(void);

#endif /* ZXV_MEGAROM_H */
