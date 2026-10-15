/* crash.h — crash dumps that survive a reboot
 *
 * When the kernel hits a fault it cannot handle, the serial console used to be
 * the only witness: on a machine nobody was watching, the cause was lost. This
 * module captures a crash record (fault registers, the last 2 KiB of console
 * output, a reason and a message), seals it with SHA-256, and writes it to a
 * small reserved area on disk. The next boot reads it back, reports it, and
 * clears it.
 *
 * The crash path must work in a broken kernel, so it:
 *  - allocates nothing and uses only static storage and the caller's stack;
 *  - runs once: a fault inside the crash path is not captured again
 *    (crash_begin() returns false), so it cannot recurse;
 *  - writes raw sectors through a polled block device, never the filesystem.
 *
 * The reserved area (crash_area_t) is CRASH_AREA_SECTORS sectors at a base
 * the caller chooses outside any filesystem. Sector 0 holds the boot-control
 * state (bootctl.h); sectors 1-8 hold the crash record.
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_CRASH_H
#define ZXV_CRASH_H

#include <stdint.h>
#include <stdbool.h>
#include "../../include/blockdev.h"

#define CRASH_MAGIC        0x5A584344u /* 'ZXCD' */
#define CRASH_VERSION      1u
#define CRASH_RECORD_BYTES 4096u /* 8 sectors */
#define CRASH_MAX_REGS     32u
#define CRASH_MAX_FRAMES   16u
#define CRASH_MSG_MAX      128u
#define CRASH_LOG_MAX      2048u
#define CRASH_AREA_SECTORS 16u /* 1 boot-control + 8 record + 7 spare */

typedef enum {
    CRASH_ARCH_UNKNOWN = 0,
    CRASH_ARCH_X86_64,
    CRASH_ARCH_ARM64,
    CRASH_ARCH_RISCV64,
    CRASH_ARCH_RISCV32,
    CRASH_ARCH_ARM32,
    CRASH_ARCH_HOSTED
} crash_arch_t;

typedef enum {
    CRASH_REASON_UNKNOWN = 0,
    CRASH_REASON_EXCEPTION, /* an unhandled CPU exception; code = ESR / vector */
    CRASH_REASON_PANIC,     /* the kernel gave up on purpose; code = caller's */
    CRASH_REASON_WATCHDOG,  /* a watchdog fired */
    CRASH_REASON_ASSERT,    /* an internal check failed */
    CRASH_REASON_OOM        /* out of memory with no way forward */
} crash_reason_t;

typedef enum {
    CRASH_OK = 0,
    CRASH_EMPTY,   /* no record (blank or cleared) */
    CRASH_CORRUPT, /* a record whose seal does not match */
    CRASH_VERSION_UNKNOWN,
    CRASH_IO /* the device refused a read or write */
} crash_status_t;

typedef struct {
    uint32_t arch, reason;
    uint64_t code;       /* ESR_EL1, scause, vector number, panic code */
    uint64_t pc, sp;     /* where it happened */
    uint64_t fault_addr; /* FAR_EL1 / CR2 / stval, or 0 */
    uint64_t uptime;     /* caller's tick count at the crash */
    uint32_t boot_count; /* which boot this was (bootctl) */
    uint32_t nregs, nframes, log_len;
    uint64_t regs[CRASH_MAX_REGS];
    uint64_t frames[CRASH_MAX_FRAMES]; /* return addresses, innermost first */
    char msg[CRASH_MSG_MAX];           /* NUL-terminated */
    char log[CRASH_LOG_MAX];           /* the console tail; not NUL-terminated */
} crash_record_t;

/* ---- console tail: the arch console tees everything it prints here */
void crash_log_puts(const char *s);
void crash_log_reset(void);
/* Copy the last min(cap, CRASH_LOG_MAX) bytes printed, oldest first. */
uint32_t crash_log_tail(char *out, uint32_t cap);

/* ---- the crash path */
/* Which boot this is, recorded in every crash record filled afterwards. */
void crash_set_boot_count(uint32_t n);
/* True the first time; false if a crash is already being handled. */
bool crash_begin(void);
/* Fill `r` with the given fault, the boot count and the console tail.
 * `regs` may be 0. The caller sets r->uptime and may add a backtrace. */
void crash_record_fill(crash_record_t *r, crash_arch_t arch, crash_reason_t reason, uint64_t code,
                       uint64_t pc, uint64_t sp, uint64_t fault_addr, const uint64_t *regs,
                       uint32_t nregs, const char *msg);
/* Walk a frame-pointer chain (AArch64 / x86-64 layout: [fp] = previous fp,
 * [fp + 8] = return address) between stack bounds lo and hi. Each frame must
 * be above the last, 8-byte aligned and inside the bounds, so a corrupt stack
 * ends the walk instead of faulting. Returns the number of frames stored. */
uint32_t crash_backtrace(crash_record_t *r, uint64_t fp, uint64_t lo, uint64_t hi);

/* ---- the sealed on-disk form */
void crash_encode(const crash_record_t *r, uint8_t out[CRASH_RECORD_BYTES]);
crash_status_t crash_decode(const uint8_t in[CRASH_RECORD_BYTES], crash_record_t *r);

/* ---- the reserved area */
typedef struct {
    block_device_t *dev;
    uint32_t base;
    bool attached;
} crash_area_t;

/* Attach only if [base, base + CRASH_AREA_SECTORS) fits on the device. */
bool crash_area_attach(crash_area_t *a, block_device_t *dev, uint32_t base);
crash_status_t crash_area_save(crash_area_t *a, const crash_record_t *r);
crash_status_t crash_area_load(crash_area_t *a, crash_record_t *r);
crash_status_t crash_area_clear(crash_area_t *a);
/* Sector 0 of the area, for bootctl. */
crash_status_t crash_area_read_ctl(crash_area_t *a, uint8_t sector[BLOCKDEV_SECTOR_SIZE]);
crash_status_t crash_area_write_ctl(crash_area_t *a, const uint8_t sector[BLOCKDEV_SECTOR_SIZE]);

const char *crash_reason_str(uint32_t reason);
const char *crash_arch_str(uint32_t arch);

#endif /* ZXV_CRASH_H */
