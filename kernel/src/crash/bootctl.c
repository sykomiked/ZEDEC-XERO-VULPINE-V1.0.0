/* bootctl.c — boot modes: normal, safe and recovery. See bootctl.h.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bootctl.h"
#include "../robin_debanks/sha256.h"

#define SEAL_OFF (BLOCKDEV_SECTOR_SIZE - 32u)

void bootctl_init(bootctl_t *b)
{
    b->boot_count = 0;
    b->unconfirmed = 0;
    b->requested = 0;
    b->last_mode = BOOT_MODE_NORMAL;
    b->crashes = 0;
}

static uint32_t max_mode(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
}

boot_mode_t bootctl_begin(bootctl_t *b, bool crashed_last_boot, bool owner_safe)
{
    uint32_t mode = BOOT_MODE_NORMAL;
    if (b->unconfirmed >= BOOTCTL_RECOVERY_AFTER)
        mode = BOOT_MODE_RECOVERY;
    else if (b->unconfirmed >= BOOTCTL_SAFE_AFTER || crashed_last_boot)
        mode = BOOT_MODE_SAFE;
    if (b->requested == BOOT_MODE_SAFE || b->requested == BOOT_MODE_RECOVERY)
        mode = max_mode(mode, b->requested);
    if (owner_safe) mode = max_mode(mode, BOOT_MODE_SAFE);

    b->requested = 0;
    if (crashed_last_boot && b->crashes < 0xFFFFFFFFu) b->crashes++;
    if (b->boot_count < 0xFFFFFFFFu) b->boot_count++;
    if (b->unconfirmed < 0xFFFFFFFFu) b->unconfirmed++;
    b->last_mode = mode;
    return (boot_mode_t) mode;
}

void bootctl_mark_good(bootctl_t *b)
{
    b->unconfirmed = 0;
}

void bootctl_request(bootctl_t *b, boot_mode_t mode)
{
    b->requested = mode <= BOOT_MODE_RECOVERY ? (uint32_t) mode : 0;
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

void bootctl_encode(const bootctl_t *b, uint8_t sector[BLOCKDEV_SECTOR_SIZE])
{
    for (uint32_t i = 0; i < BLOCKDEV_SECTOR_SIZE; i++) sector[i] = 0;
    put32(sector + 0, BOOTCTL_MAGIC);
    put32(sector + 4, BOOTCTL_VERSION);
    put32(sector + 8, b->boot_count);
    put32(sector + 12, b->unconfirmed);
    put32(sector + 16, b->requested);
    put32(sector + 20, b->last_mode);
    put32(sector + 24, b->crashes);
    sha256(sector, SEAL_OFF, sector + SEAL_OFF);
}

bool bootctl_decode(const uint8_t sector[BLOCKDEV_SECTOR_SIZE], bootctl_t *b)
{
    bootctl_init(b);
    if (get32(sector) != BOOTCTL_MAGIC || get32(sector + 4) != BOOTCTL_VERSION) return false;
    uint8_t seal[32], diff = 0;
    sha256(sector, SEAL_OFF, seal);
    for (int i = 0; i < 32; i++) diff |= seal[i] ^ sector[SEAL_OFF + i];
    if (diff) return false;
    uint32_t requested = get32(sector + 16), last = get32(sector + 20);
    if (requested > BOOT_MODE_RECOVERY || last > BOOT_MODE_RECOVERY) return false;
    b->boot_count = get32(sector + 8);
    b->unconfirmed = get32(sector + 12);
    b->requested = requested;
    b->last_mode = last;
    b->crashes = get32(sector + 24);
    return true;
}

bool bootctl_load(crash_area_t *a, bootctl_t *b)
{
    uint8_t s[BLOCKDEV_SECTOR_SIZE];
    if (crash_area_read_ctl(a, s) != CRASH_OK) {
        bootctl_init(b);
        return false;
    }
    return bootctl_decode(s, b);
}

bool bootctl_save(crash_area_t *a, const bootctl_t *b)
{
    uint8_t s[BLOCKDEV_SECTOR_SIZE];
    bootctl_encode(b, s);
    return crash_area_write_ctl(a, s) == CRASH_OK;
}

const char *boot_mode_str(uint32_t mode)
{
    switch (mode) {
    case BOOT_MODE_NORMAL:
        return "normal";
    case BOOT_MODE_SAFE:
        return "safe";
    case BOOT_MODE_RECOVERY:
        return "recovery";
    default:
        return "unknown";
    }
}
