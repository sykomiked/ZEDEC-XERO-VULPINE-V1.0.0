/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_crash.c — crash records, the reserved disk area and boot modes.
 *
 * A memory-backed block device stands in for the disk. It can refuse writes
 * after a set count, which is how a write cut short by power loss is
 * modelled. */
#include <stdio.h>
#include <string.h>
#include "crash.h"
#include "bootctl.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] " __VA_ARGS__);                                                         \
            printf("\n");                                                                          \
            failures++;                                                                            \
        } else {                                                                                   \
            printf("[PASS] " __VA_ARGS__);                                                         \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

#define DISK_SECTORS 64u
static uint8_t g_disk[DISK_SECTORS][BLOCKDEV_SECTOR_SIZE];
static int g_writes_left = -1; /* -1: unlimited */
static int g_fail_reads;

static int mem_read(block_device_t *d, uint32_t lba, uint8_t *buf)
{
    (void) d;
    if (g_fail_reads || lba >= DISK_SECTORS) return -1;
    memcpy(buf, g_disk[lba], BLOCKDEV_SECTOR_SIZE);
    return 0;
}

static int mem_write(block_device_t *d, uint32_t lba, const uint8_t *buf)
{
    (void) d;
    if (lba >= DISK_SECTORS || g_writes_left == 0) return -1;
    if (g_writes_left > 0) g_writes_left--;
    memcpy(g_disk[lba], buf, BLOCKDEV_SECTOR_SIZE);
    return 0;
}

static block_device_t g_dev = {true, DISK_SECTORS, "mem", 0, mem_read, mem_write};

static void sample(crash_record_t *r)
{
    uint64_t regs[31];
    for (int i = 0; i < 31; i++) regs[i] = 0x1000u * (uint64_t) i + 7;
    crash_record_fill(r, CRASH_ARCH_ARM64, CRASH_REASON_EXCEPTION, 0x96000045, 0xFFFF000040081234,
                      0xFFFF000040200000, 0x7A0000CCCC, regs, 31, "data abort at EL1");
    r->uptime = 123456;
    r->boot_count = 42;
}

static void test_log(void)
{
    char out[CRASH_LOG_MAX];
    crash_log_reset();
    CHECK(crash_log_tail(out, sizeof out) == 0, "empty log");
    crash_log_puts("[BOOT] hello\n");
    uint32_t n = crash_log_tail(out, sizeof out);
    CHECK(n == 13 && !memcmp(out, "[BOOT] hello\n", 13), "short log copied whole");
    for (int i = 0; i < 1000; i++) crash_log_puts("0123456789");
    crash_log_puts("THE END");
    n = crash_log_tail(out, sizeof out);
    CHECK(n == CRASH_LOG_MAX && !memcmp(out + n - 7, "THE END", 7),
          "a long log keeps the last 2 KiB, newest last");
    n = crash_log_tail(out, 5);
    CHECK(n == 5 && !memcmp(out, "E END", 5), "a short tail is the newest bytes");
    crash_log_puts(0);
    CHECK(crash_log_tail(out, 3) == 3, "null string ignored");
}

static void test_record(void)
{
    crash_record_t r, back;
    crash_log_reset();
    crash_log_puts("[FAULT] Synchronous exception\n");
    sample(&r);
    CHECK(r.nregs == 31 && r.regs[30] == 0x1E007, "registers copied");
    CHECK(!strcmp(r.msg, "data abort at EL1"), "message copied");
    CHECK(r.log_len == 30 && !memcmp(r.log, "[FAULT]", 7), "console tail captured");

    static uint8_t buf[CRASH_RECORD_BYTES];
    crash_encode(&r, buf);
    CHECK(crash_decode(buf, &back) == CRASH_OK, "sealed record decodes");
    CHECK(back.code == r.code && back.pc == r.pc && back.sp == r.sp &&
              back.fault_addr == r.fault_addr && back.uptime == 123456 && back.boot_count == 42 &&
              back.arch == CRASH_ARCH_ARM64 && back.reason == CRASH_REASON_EXCEPTION,
          "every field survives");
    CHECK(back.nregs == 31 && !memcmp(back.regs, r.regs, sizeof r.regs) &&
              !strcmp(back.msg, r.msg) && back.log_len == r.log_len &&
              !memcmp(back.log, r.log, r.log_len),
          "registers, message and log survive");

    int caught = 0;
    for (uint32_t i = 4; i < CRASH_RECORD_BYTES; i += 37) {
        buf[i] ^= 0x01;
        caught += crash_decode(buf, &back) == CRASH_CORRUPT;
        buf[i] ^= 0x01;
    }
    CHECK(caught == (int) ((CRASH_RECORD_BYTES - 4 + 36) / 37), "every flipped bit is caught (%d)",
          caught);
    buf[0] ^= 1;
    CHECK(crash_decode(buf, &back) == CRASH_EMPTY, "no magic reads as empty");
    buf[0] ^= 1;

    /* oversized counts are clamped on encode */
    r.nregs = 999;
    r.log_len = 99999;
    crash_encode(&r, buf);
    CHECK(crash_decode(buf, &back) == CRASH_OK && back.nregs == CRASH_MAX_REGS &&
              back.log_len == CRASH_LOG_MAX,
          "oversized counts clamped");

    /* a long message is cut and terminated */
    char longmsg[300];
    memset(longmsg, 'x', sizeof longmsg - 1);
    longmsg[299] = 0;
    crash_record_fill(&r, CRASH_ARCH_X86_64, CRASH_REASON_PANIC, 1, 0, 0, 0, 0, 0, longmsg);
    CHECK(strlen(r.msg) == CRASH_MSG_MAX - 1 && r.nregs == 0, "long message cut; no registers");
    CHECK(!strcmp(crash_reason_str(CRASH_REASON_PANIC), "panic") &&
              !strcmp(crash_arch_str(CRASH_ARCH_ARM64), "arm64") &&
              !strcmp(crash_arch_str(99), "unknown"),
          "names");
}

static void test_backtrace(void)
{
    /* A fake stack: three frames chained upward, then a zero return. */
    static uint64_t stack[64];
    memset(stack, 0, sizeof stack);
    uint64_t base = (uint64_t) (uintptr_t) stack;
    stack[8] = base + 20 * 8;
    stack[9] = 0x1111;
    stack[20] = base + 40 * 8;
    stack[21] = 0x2222;
    stack[40] = base + 50 * 8;
    stack[41] = 0x3333;
    stack[50] = 0;
    stack[51] = 0;
    crash_record_t r;
    memset(&r, 0, sizeof r);
    uint32_t n = crash_backtrace(&r, base + 8 * 8, base, base + sizeof stack);
    CHECK(n == 3 && r.frames[0] == 0x1111 && r.frames[2] == 0x3333, "three frames walked");

    stack[20] = base + 4 * 8; /* points back down: a loop or corruption */
    n = crash_backtrace(&r, base + 8 * 8, base, base + sizeof stack);
    CHECK(n == 2, "a frame pointing downward ends the walk (%u)", n);
    stack[20] = base + 1000 * 8; /* outside the stack */
    n = crash_backtrace(&r, base + 8 * 8, base, base + sizeof stack);
    CHECK(n == 2, "a frame outside the stack ends the walk");
    stack[20] = base + 40 * 8 + 4; /* misaligned */
    n = crash_backtrace(&r, base + 8 * 8, base, base + sizeof stack);
    CHECK(n == 2, "a misaligned frame ends the walk");
    CHECK(crash_backtrace(&r, 0, base, base + sizeof stack) == 0, "fp 0 gives no frames");
    CHECK(crash_backtrace(&r, base, base, 8) == 0, "tiny bounds give no frames");

    /* a long chain stops at the limit */
    for (int i = 0; i < 30; i++) {
        stack[2 * i] = base + (uint64_t) (2 * i + 2) * 8;
        stack[2 * i + 1] = 0x100 + (uint64_t) i;
    }
    n = crash_backtrace(&r, base, base, base + sizeof stack);
    CHECK(n == CRASH_MAX_FRAMES, "walk capped at %u frames", CRASH_MAX_FRAMES);
}

static void test_area(void)
{
    crash_area_t a;
    crash_record_t r, back;
    memset(g_disk, 0, sizeof g_disk);
    CHECK(!crash_area_attach(&a, &g_dev, DISK_SECTORS - CRASH_AREA_SECTORS + 1),
          "an area past the end of the disk is refused");
    CHECK(!crash_area_attach(&a, 0, 0), "no device refused");
    CHECK(crash_area_save(&a, &r) == CRASH_IO, "an unattached area writes nothing");
    CHECK(crash_area_attach(&a, &g_dev, 40), "area attached at sector 40");
    CHECK(crash_area_load(&a, &back) == CRASH_EMPTY, "blank area: no crash");

    crash_log_reset();
    crash_log_puts("last words\n");
    sample(&r);
    CHECK(crash_area_save(&a, &r) == CRASH_OK, "record saved");
    int outside = 0;
    for (uint32_t s = 0; s < DISK_SECTORS; s++)
        if (s < 41 || s > 48)
            for (uint32_t i = 0; i < BLOCKDEV_SECTOR_SIZE; i++) outside |= g_disk[s][i];
    CHECK(!outside, "nothing outside sectors 41-48 was touched");
    CHECK(crash_area_load(&a, &back) == CRASH_OK && back.pc == r.pc && back.log_len == 11,
          "record read back on the next boot");
    CHECK(crash_area_clear(&a) == CRASH_OK && crash_area_load(&a, &back) == CRASH_EMPTY,
          "cleared after reporting");

    /* power lost partway through a save: the header goes last, so a
     * partial write is either no record or a record that fails its seal */
    for (int cut = 0; cut < 8; cut++) {
        crash_area_clear(&a);
        g_writes_left = cut;
        crash_area_save(&a, &r);
        g_writes_left = -1;
        crash_status_t st = crash_area_load(&a, &back);
        CHECK(st == CRASH_EMPTY, "save cut after %d sectors leaves no record (%d)", cut, st);
    }
    /* overwriting an old record and losing power: old header, new tail */
    crash_area_save(&a, &r);
    r.pc ^= 0xFF;
    g_writes_left = 3;
    crash_area_save(&a, &r);
    g_writes_left = -1;
    CHECK(crash_area_load(&a, &back) == CRASH_CORRUPT, "a torn overwrite fails its seal");
    g_fail_reads = 1;
    CHECK(crash_area_load(&a, &back) == CRASH_IO, "a read error is reported");
    g_fail_reads = 0;
}

static void test_bootctl(void)
{
    bootctl_t b, back;
    bootctl_init(&b);
    CHECK(bootctl_begin(&b, false, false) == BOOT_MODE_NORMAL && b.boot_count == 1,
          "first boot is normal");
    bootctl_mark_good(&b);
    CHECK(bootctl_begin(&b, false, false) == BOOT_MODE_NORMAL, "a confirmed boot stays normal");

    /* boots that never reach the shell */
    boot_mode_t m2 = bootctl_begin(&b, false, false);
    boot_mode_t m3 = bootctl_begin(&b, false, false);
    boot_mode_t m4 = bootctl_begin(&b, false, false);
    boot_mode_t m5 = bootctl_begin(&b, false, false);
    CHECK(m2 == BOOT_MODE_NORMAL && m3 == BOOT_MODE_SAFE && m4 == BOOT_MODE_SAFE &&
              m5 == BOOT_MODE_RECOVERY,
          "two failed boots give safe mode, four give recovery");
    bootctl_mark_good(&b);
    CHECK(bootctl_begin(&b, false, false) == BOOT_MODE_NORMAL, "confirming returns to normal");
    bootctl_mark_good(&b);

    CHECK(bootctl_begin(&b, true, false) == BOOT_MODE_SAFE && b.crashes == 1,
          "a crash in the last boot gives safe mode once");
    bootctl_mark_good(&b);
    CHECK(bootctl_begin(&b, false, true) == BOOT_MODE_SAFE, "the owner can hold safe mode");
    bootctl_mark_good(&b);
    bootctl_request(&b, BOOT_MODE_RECOVERY);
    CHECK(bootctl_begin(&b, false, false) == BOOT_MODE_RECOVERY && b.requested == 0,
          "a request applies to the next boot");
    bootctl_mark_good(&b);
    CHECK(bootctl_begin(&b, false, false) == BOOT_MODE_NORMAL, "and only that boot");
    bootctl_request(&b, (boot_mode_t) 7);
    CHECK(b.requested == 0, "a nonsense request is ignored");

    uint8_t s[BLOCKDEV_SECTOR_SIZE];
    bootctl_encode(&b, s);
    CHECK(bootctl_decode(s, &back) && back.boot_count == b.boot_count &&
              back.unconfirmed == b.unconfirmed && back.crashes == 1,
          "state round-trips");
    s[9] ^= 1;
    CHECK(!bootctl_decode(s, &back) && back.boot_count == 0, "damaged state reads as fresh");
    memset(s, 0, sizeof s);
    CHECK(!bootctl_decode(s, &back), "blank state reads as fresh");

    crash_area_t a;
    memset(g_disk, 0, sizeof g_disk);
    crash_area_attach(&a, &g_dev, 40);
    CHECK(!bootctl_load(&a, &back) && back.boot_count == 0, "blank disk: fresh state");
    CHECK(bootctl_save(&a, &b) && bootctl_load(&a, &back) && back.boot_count == b.boot_count,
          "state saved in sector 40 and read back");
    CHECK(!strcmp(boot_mode_str(BOOT_MODE_RECOVERY), "recovery"), "names");
}

static void test_reentry(void)
{
    CHECK(crash_begin(), "first crash proceeds");
    CHECK(!crash_begin(), "a fault inside the crash path does not recurse");
}

int main(void)
{
    test_log();
    test_record();
    test_backtrace();
    test_area();
    test_bootctl();
    test_reentry();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
