/* crash.c — crash dumps that survive a reboot. See crash.h.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "crash.h"
#include "../robin_debanks/sha256.h"

/* ---- on-disk layout (little-endian) */
#define OFF_MAGIC   0
#define OFF_VERSION 4
#define OFF_ARCH    8
#define OFF_REASON  12
#define OFF_CODE    16
#define OFF_PC      24
#define OFF_SP      32
#define OFF_FAULT   40
#define OFF_UPTIME  48
#define OFF_BOOT    56
#define OFF_NREGS   60
#define OFF_NFRAMES 64
#define OFF_LOGLEN  68
#define OFF_REGS    72
#define OFF_FRAMES  (OFF_REGS + 8 * CRASH_MAX_REGS)
#define OFF_MSG     (OFF_FRAMES + 8 * CRASH_MAX_FRAMES)
#define OFF_LOG     (OFF_MSG + CRASH_MSG_MAX)
#define OFF_SEAL    (CRASH_RECORD_BYTES - 32u)

_Static_assert(OFF_LOG + CRASH_LOG_MAX <= OFF_SEAL, "crash record fits its sectors");

static void zero(void *p, uint32_t n)
{
    uint8_t *d = (uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

/* ---- console tail */

static char g_log[CRASH_LOG_MAX];
static uint32_t g_log_head;  /* next write position */
static uint32_t g_log_total; /* bytes ever written, saturating */

void crash_log_puts(const char *s)
{
    if (!s) return;
    for (; *s; s++) {
        g_log[g_log_head] = *s;
        g_log_head = (g_log_head + 1) % CRASH_LOG_MAX;
        if (g_log_total < CRASH_LOG_MAX) g_log_total++;
    }
}

void crash_log_reset(void)
{
    g_log_head = 0;
    g_log_total = 0;
}

uint32_t crash_log_tail(char *out, uint32_t cap)
{
    uint32_t n = g_log_total < cap ? g_log_total : cap;
    uint32_t start = (g_log_head + CRASH_LOG_MAX - n) % CRASH_LOG_MAX;
    for (uint32_t i = 0; i < n; i++) out[i] = g_log[(start + i) % CRASH_LOG_MAX];
    return n;
}

/* ---- the crash path */

static volatile uint32_t g_in_crash;
static uint32_t g_boot_count;

void crash_set_boot_count(uint32_t n)
{
    g_boot_count = n;
}

bool crash_begin(void)
{
    if (g_in_crash) return false;
    g_in_crash = 1;
    return true;
}

void crash_record_fill(crash_record_t *r, crash_arch_t arch, crash_reason_t reason, uint64_t code,
                       uint64_t pc, uint64_t sp, uint64_t fault_addr, const uint64_t *regs,
                       uint32_t nregs, const char *msg)
{
    zero(r, sizeof *r);
    r->arch = arch;
    r->reason = reason;
    r->code = code;
    r->pc = pc;
    r->sp = sp;
    r->fault_addr = fault_addr;
    r->boot_count = g_boot_count;
    if (regs) {
        r->nregs = nregs < CRASH_MAX_REGS ? nregs : CRASH_MAX_REGS;
        for (uint32_t i = 0; i < r->nregs; i++) r->regs[i] = regs[i];
    }
    if (msg)
        for (uint32_t i = 0; i + 1 < CRASH_MSG_MAX && msg[i]; i++) r->msg[i] = msg[i];
    r->log_len = crash_log_tail(r->log, CRASH_LOG_MAX);
}

uint32_t crash_backtrace(crash_record_t *r, uint64_t fp, uint64_t lo, uint64_t hi)
{
    uint32_t n = 0;
    while (n < CRASH_MAX_FRAMES && hi >= 16 && fp >= lo && fp <= hi - 16 && (fp & 7) == 0) {
        const volatile uint64_t *f = (const volatile uint64_t *) (uintptr_t) fp;
        uint64_t next = f[0], ret = f[1];
        if (ret == 0) break;
        r->frames[n++] = ret;
        if (next <= fp) break; /* stacks grow down, so callers sit higher */
        fp = next;
    }
    r->nframes = n;
    return n;
}

/* ---- encoding */

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

static uint64_t get64(const uint8_t *p)
{
    return (uint64_t) get32(p) | ((uint64_t) get32(p + 4) << 32);
}

void crash_encode(const crash_record_t *r, uint8_t out[CRASH_RECORD_BYTES])
{
    zero(out, CRASH_RECORD_BYTES);
    put32(out + OFF_MAGIC, CRASH_MAGIC);
    put32(out + OFF_VERSION, CRASH_VERSION);
    put32(out + OFF_ARCH, r->arch);
    put32(out + OFF_REASON, r->reason);
    put64(out + OFF_CODE, r->code);
    put64(out + OFF_PC, r->pc);
    put64(out + OFF_SP, r->sp);
    put64(out + OFF_FAULT, r->fault_addr);
    put64(out + OFF_UPTIME, r->uptime);
    put32(out + OFF_BOOT, r->boot_count);
    uint32_t nregs = r->nregs < CRASH_MAX_REGS ? r->nregs : CRASH_MAX_REGS;
    uint32_t nframes = r->nframes < CRASH_MAX_FRAMES ? r->nframes : CRASH_MAX_FRAMES;
    uint32_t log_len = r->log_len < CRASH_LOG_MAX ? r->log_len : CRASH_LOG_MAX;
    put32(out + OFF_NREGS, nregs);
    put32(out + OFF_NFRAMES, nframes);
    put32(out + OFF_LOGLEN, log_len);
    for (uint32_t i = 0; i < nregs; i++) put64(out + OFF_REGS + 8 * i, r->regs[i]);
    for (uint32_t i = 0; i < nframes; i++) put64(out + OFF_FRAMES + 8 * i, r->frames[i]);
    for (uint32_t i = 0; i + 1 < CRASH_MSG_MAX && r->msg[i]; i++) out[OFF_MSG + i] = r->msg[i];
    for (uint32_t i = 0; i < log_len; i++) out[OFF_LOG + i] = (uint8_t) r->log[i];
    sha256(out, OFF_SEAL, out + OFF_SEAL);
}

static bool all_zero(const uint8_t *p, uint32_t n)
{
    uint8_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc |= p[i];
    return acc == 0;
}

crash_status_t crash_decode(const uint8_t in[CRASH_RECORD_BYTES], crash_record_t *r)
{
    if (get32(in + OFF_MAGIC) != CRASH_MAGIC) return CRASH_EMPTY;
    uint8_t seal[32];
    sha256(in, OFF_SEAL, seal);
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) diff |= seal[i] ^ in[OFF_SEAL + i];
    if (diff) return CRASH_CORRUPT;
    if (get32(in + OFF_VERSION) != CRASH_VERSION) return CRASH_VERSION_UNKNOWN;

    zero(r, sizeof *r);
    r->arch = get32(in + OFF_ARCH);
    r->reason = get32(in + OFF_REASON);
    r->code = get64(in + OFF_CODE);
    r->pc = get64(in + OFF_PC);
    r->sp = get64(in + OFF_SP);
    r->fault_addr = get64(in + OFF_FAULT);
    r->uptime = get64(in + OFF_UPTIME);
    r->boot_count = get32(in + OFF_BOOT);
    r->nregs = get32(in + OFF_NREGS);
    r->nframes = get32(in + OFF_NFRAMES);
    r->log_len = get32(in + OFF_LOGLEN);
    if (r->nregs > CRASH_MAX_REGS || r->nframes > CRASH_MAX_FRAMES || r->log_len > CRASH_LOG_MAX)
        return CRASH_CORRUPT; /* sealed but impossible: written by something else */
    for (uint32_t i = 0; i < r->nregs; i++) r->regs[i] = get64(in + OFF_REGS + 8 * i);
    for (uint32_t i = 0; i < r->nframes; i++) r->frames[i] = get64(in + OFF_FRAMES + 8 * i);
    for (uint32_t i = 0; i + 1 < CRASH_MSG_MAX; i++) r->msg[i] = (char) in[OFF_MSG + i];
    for (uint32_t i = 0; i < r->log_len; i++) r->log[i] = (char) in[OFF_LOG + i];
    return CRASH_OK;
}

/* ---- the reserved area */

static uint8_t g_buf[CRASH_RECORD_BYTES]; /* static: the crash path may have little stack */

bool crash_area_attach(crash_area_t *a, block_device_t *dev, uint32_t base)
{
    a->attached = false;
    a->dev = dev;
    a->base = base;
    if (!dev || !dev->present || !dev->read_sector || !dev->write_sector) return false;
    if (base > dev->total_sectors || dev->total_sectors - base < CRASH_AREA_SECTORS) return false;
    a->attached = true;
    return true;
}

crash_status_t crash_area_save(crash_area_t *a, const crash_record_t *r)
{
    if (!a || !a->attached) return CRASH_IO;
    crash_encode(r, g_buf);
    /* Write the sealed tail first and the header sector last, so a write cut
     * short leaves either the old record or one whose seal fails. */
    for (uint32_t s = CRASH_RECORD_BYTES / BLOCKDEV_SECTOR_SIZE; s-- > 0;)
        if (a->dev->write_sector(a->dev, a->base + 1 + s, g_buf + s * BLOCKDEV_SECTOR_SIZE) != 0)
            return CRASH_IO;
    return CRASH_OK;
}

crash_status_t crash_area_load(crash_area_t *a, crash_record_t *r)
{
    if (!a || !a->attached) return CRASH_IO;
    for (uint32_t s = 0; s < CRASH_RECORD_BYTES / BLOCKDEV_SECTOR_SIZE; s++)
        if (a->dev->read_sector(a->dev, a->base + 1 + s, g_buf + s * BLOCKDEV_SECTOR_SIZE) != 0)
            return CRASH_IO;
    if (all_zero(g_buf, CRASH_RECORD_BYTES)) return CRASH_EMPTY;
    return crash_decode(g_buf, r);
}

crash_status_t crash_area_clear(crash_area_t *a)
{
    if (!a || !a->attached) return CRASH_IO;
    zero(g_buf, BLOCKDEV_SECTOR_SIZE);
    return a->dev->write_sector(a->dev, a->base + 1, g_buf) == 0 ? CRASH_OK : CRASH_IO;
}

crash_status_t crash_area_read_ctl(crash_area_t *a, uint8_t sector[BLOCKDEV_SECTOR_SIZE])
{
    if (!a || !a->attached) return CRASH_IO;
    return a->dev->read_sector(a->dev, a->base, sector) == 0 ? CRASH_OK : CRASH_IO;
}

crash_status_t crash_area_write_ctl(crash_area_t *a, const uint8_t sector[BLOCKDEV_SECTOR_SIZE])
{
    if (!a || !a->attached) return CRASH_IO;
    return a->dev->write_sector(a->dev, a->base, sector) == 0 ? CRASH_OK : CRASH_IO;
}

const char *crash_reason_str(uint32_t reason)
{
    switch (reason) {
    case CRASH_REASON_EXCEPTION:
        return "unhandled exception";
    case CRASH_REASON_PANIC:
        return "panic";
    case CRASH_REASON_WATCHDOG:
        return "watchdog";
    case CRASH_REASON_ASSERT:
        return "internal check failed";
    case CRASH_REASON_OOM:
        return "out of memory";
    default:
        return "unknown";
    }
}

const char *crash_arch_str(uint32_t arch)
{
    static const char *const names[] = {"unknown", "x86_64", "arm64", "riscv64",
                                        "riscv32", "arm32",  "hosted"};
    return arch < sizeof names / sizeof *names ? names[arch] : "unknown";
}
