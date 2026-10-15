/* bootctl.h — boot modes: normal, safe and recovery
 *
 * A kernel that crashes during boot will crash again on the next boot, and
 * the one after. bootctl counts boots that started but were never confirmed
 * good, and steps down to a smaller system when they pile up:
 *
 *   NORMAL    everything starts.
 *   SAFE      only what is needed to reach a shell and storage: no user
 *             apps, no optional subsystems. Chosen after BOOTCTL_SAFE_AFTER
 *             unconfirmed boots in a row, after a crash in the previous boot,
 *             or when the owner asks for it (a key held at boot, a command).
 *   RECOVERY  the safe system plus repair tools (crash report, A/B rollback,
 *             boot-mode reset). Chosen after BOOTCTL_RECOVERY_AFTER
 *             unconfirmed boots, or when asked for.
 *
 * A boot is confirmed with bootctl_mark_good() once the shell is up. An owner
 * request (bootctl_request) applies to the next boot only.
 *
 * The state lives in sector 0 of the crash area (crash.h), sealed with
 * SHA-256; a blank or damaged sector reads as a fresh device.
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_BOOTCTL_H
#define ZXV_BOOTCTL_H

#include <stdint.h>
#include <stdbool.h>
#include "crash.h"

#define BOOTCTL_MAGIC          0x5A584243u /* 'ZXBC' */
#define BOOTCTL_VERSION        1u
#define BOOTCTL_SAFE_AFTER     2u
#define BOOTCTL_RECOVERY_AFTER 4u

typedef enum { BOOT_MODE_NORMAL = 0, BOOT_MODE_SAFE = 1, BOOT_MODE_RECOVERY = 2 } boot_mode_t;

typedef struct {
    uint32_t boot_count;  /* boots ever started */
    uint32_t unconfirmed; /* boots started since the last confirmed one */
    uint32_t requested;   /* one-shot mode for the next boot, or 0 */
    uint32_t last_mode;   /* the mode of the current (or last) boot */
    uint32_t crashes;     /* crash records ever reported */
} bootctl_t;

void bootctl_init(bootctl_t *b);
/* Decide this boot's mode and count the boot. crashed_last_boot: a crash
 * record was found. owner_safe: the owner asked for safe mode right now (a
 * key held at boot, a command line flag). */
boot_mode_t bootctl_begin(bootctl_t *b, bool crashed_last_boot, bool owner_safe);
void bootctl_mark_good(bootctl_t *b);
/* Ask for `mode` on the next boot only. */
void bootctl_request(bootctl_t *b, boot_mode_t mode);

void bootctl_encode(const bootctl_t *b, uint8_t sector[BLOCKDEV_SECTOR_SIZE]);
/* False (and a fresh state in `b`) for a blank or damaged sector. */
bool bootctl_decode(const uint8_t sector[BLOCKDEV_SECTOR_SIZE], bootctl_t *b);

/* Read and write the state in a crash area. A load that finds nothing
 * usable gives a fresh state and returns false. */
bool bootctl_load(crash_area_t *a, bootctl_t *b);
bool bootctl_save(crash_area_t *a, const bootctl_t *b);

const char *boot_mode_str(uint32_t mode);

#endif /* ZXV_BOOTCTL_H */
