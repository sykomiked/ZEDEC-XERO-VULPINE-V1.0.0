/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_ramfb.c — host test for the QEMU ramfb scanout driver (ramfb.c).
 *
 * THE FAKE DEVICE. ramfb.c talks to fw_cfg through two MMIO stores at
 * RAMFB_FWCFG_BASE+0x10/+0x14 (the DMA address, big-endian, low half last)
 * and then spins on the descriptor's control word until "QEMU" clears it.
 * To give those stores real side effects on the host, the test maps a page at
 * RAMFB_FWCFG_BASE with PROT_NONE. Every access faults; the SIGSEGV handler
 * opens the page and single-steps the faulting instruction (x86 TF), and the
 * SIGTRAP handler then hands the stored value to a small fw_cfg DMA model and
 * closes the page again. The model decodes the descriptor exactly as QEMU
 * would (big-endian control/length/address), serves the file directory, and
 * captures what the driver writes to etc/ramfb. Faults can be injected (ERROR
 * bit, no completion) per transfer.
 *
 * Covers: argument refusal with no device traffic; the directory read
 * (selector 0x19, SELECT|READ, 16 KiB); the name match (exact, not prefix);
 * the big-endian selector; the 28-byte etc/ramfb record (be64 address, 'XR24'
 * fourcc, flags 0, width, height, stride=width*4) written with SELECT|WRITE;
 * the register order (high half then low half of the descriptor address);
 * every error code (-1/-2/-3); ramfb_is_live() tracking each outcome; and the
 * directory walk's bound against a lying entry count.
 *
 * Linux/x86-64 host only (fault decoding via REG_ERR, single-step via TF).
 */
#define _GNU_SOURCE
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <ucontext.h>
#include "ramfb.h"

#if !defined(__x86_64__) || !defined(__linux__)
#    error "test_ramfb.c: the fault-trapping MMIO model needs Linux on x86-64"
#endif

static int failures = 0;
static int checks = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s (line %d)\n", m, __LINE__);                                          \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

static uint32_t be32(uint32_t x)
{
    return __builtin_bswap32(x);
}
static uint64_t be64(uint64_t x)
{
    return __builtin_bswap64(x);
}

/* ===== the fw_cfg DMA model ===== */
#define PAGE     4096u
#define DIR_SIZE 16384u
#define MAX_XFER 8

typedef struct {
    uint32_t control, length; /* decoded (host order) */
    uint64_t address, desc;
    uint8_t data[64]; /* bytes the driver handed us on a WRITE */
} xfer_t;

/* DEV_LATE: complete ~1 ms after the kick, from a timer, so the driver must
 * really poll. DEV_LATE_ERR: report ERROR at ~1 ms and clear the word at ~20
 * ms; a driver that ignores ERROR and keeps waiting would then see "done". */
typedef enum { DEV_OK = 0, DEV_ERROR, DEV_HANG, DEV_LATE, DEV_LATE_ERR } dev_reply_t;

static struct {
    uint8_t dir[DIR_SIZE]; /* the file directory image the device serves */
    uint32_t dir_len;
    dev_reply_t reply[MAX_XFER]; /* per-transfer behaviour */
    xfer_t x[MAX_XFER];
    int nx;
    uint32_t hi; /* last value stored at +0x10 (raw, big-endian) */
    struct {
        uint32_t off, val;
    } trace[32];
    int ntrace;
    int stray; /* stores anywhere but +0x10 / +0x14, and any loads */
} D;

static uint8_t *const g_page = (uint8_t *) (uintptr_t) RAMFB_FWCFG_BASE;
static int g_pending = -1; /* page offset of an in-flight store */

static void dev_reset(void)
{
    memset(&D, 0, sizeof D);
}

static void dir_add(const char *name, uint16_t select, uint32_t size)
{
    uint32_t n = be32(*(uint32_t *) (void *) D.dir);
    uint8_t *e = D.dir + 4 + n * 64u;
    uint32_t bsize = be32(size);
    uint16_t bsel = (uint16_t) ((select >> 8) | (select << 8));
    memcpy(e, &bsize, 4);
    memcpy(e + 4, &bsel, 2);
    memset(e + 6, 0, 2);
    memset(e + 8, 0, 56);
    memcpy(e + 8, name, strnlen(name, 56));
    n++;
    n = be32(n);
    memcpy(D.dir, &n, 4);
    D.dir_len = 4 + be32(n) * 64u;
}

static void dir_set_count(uint32_t count)
{
    uint32_t b = be32(count);
    memcpy(D.dir, &b, 4);
}

/* ---- late completion from SIGALRM ---- */
static volatile uint32_t *g_late_ctl;
static volatile int g_late_ticks, g_late_err;

static void on_alarm(int sig)
{
    (void) sig;
    if (!g_late_ctl) return;
    g_late_ticks++;
    if (g_late_err && g_late_ticks == 1) {
        *g_late_ctl = be32(0x01u); /* ERROR first ... */
    } else if (!g_late_err || g_late_ticks >= 20) {
        *g_late_ctl = 0; /* ... then (or directly) done */
        g_late_ctl = NULL;
    }
}

static void late_arm(uint64_t desc_addr, bool err)
{
    struct itimerval it = {{0, 1000}, {0, 1000}};
    g_late_ticks = 0;
    g_late_err = err;
    g_late_ctl = (volatile uint32_t *) (uintptr_t) desc_addr;
    setitimer(ITIMER_REAL, &it, NULL);
}

static void late_disarm(void)
{
    struct itimerval it = {{0, 0}, {0, 0}};
    setitimer(ITIMER_REAL, &it, NULL);
    g_late_ctl = NULL;
}

static void dma_kick(uint64_t desc_addr)
{
    volatile uint8_t *d = (volatile uint8_t *) (uintptr_t) desc_addr;
    uint32_t c, l;
    uint64_t a;
    memcpy(&c, (const void *) d, 4);
    memcpy(&l, (const void *) (d + 4), 4);
    memcpy(&a, (const void *) (d + 8), 8);
    int k = D.nx < MAX_XFER ? D.nx : MAX_XFER - 1;
    xfer_t *x = &D.x[k];
    x->control = be32(c);
    x->length = be32(l);
    x->address = be64(a);
    x->desc = desc_addr;
    dev_reply_t r = D.reply[k];
    D.nx++;
    if (r == DEV_HANG) return; /* never completes */
    if (x->control & 0x02u) {  /* READ: serve the directory */
        uint32_t n = x->length < DIR_SIZE ? x->length : DIR_SIZE;
        memcpy((void *) (uintptr_t) x->address, D.dir, n);
    }
    if (x->control & 0x10u) { /* WRITE: capture */
        uint32_t n = x->length < sizeof x->data ? x->length : (uint32_t) sizeof x->data;
        memcpy(x->data, (const void *) (uintptr_t) x->address, n);
    }
    if (r == DEV_LATE || r == DEV_LATE_ERR) {
        late_arm(desc_addr, r == DEV_LATE_ERR);
        return;
    }
    uint32_t done = r == DEV_ERROR ? be32(0x01u) : 0u;
    memcpy((void *) d, &done, 4);
}

static void dev_store(uint32_t off, uint32_t val)
{
    if (D.ntrace < 32) {
        D.trace[D.ntrace].off = off;
        D.trace[D.ntrace].val = val;
        D.ntrace++;
    }
    if (off == 0x10u) {
        D.hi = val;
    } else if (off == 0x14u) {
        dma_kick(((uint64_t) be32(D.hi) << 32) | be32(val));
    } else {
        D.stray++;
    }
}

/* ---- the trap: fault -> open page -> single-step -> SIGTRAP -> model ---- */
static void on_segv(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = ucv;
    uintptr_t a = (uintptr_t) si->si_addr;
    (void) sig;
    if (a < (uintptr_t) g_page || a >= (uintptr_t) g_page + PAGE) {
        signal(SIGSEGV, SIG_DFL); /* a real crash: let it happen */
        return;
    }
    mprotect(g_page, PAGE, PROT_READ | PROT_WRITE);
    if (uc->uc_mcontext.gregs[REG_ERR] & 2) {
        g_pending = (int) (a - (uintptr_t) g_page);
    } else {
        D.stray++; /* ramfb never reads fw_cfg registers */
        g_pending = -1;
    }
    uc->uc_mcontext.gregs[REG_EFL] |= 0x100; /* TF: trap after one instruction */
}

static void on_trap(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = ucv;
    (void) sig;
    (void) si;
    uc->uc_mcontext.gregs[REG_EFL] &= ~0x100;
    if (g_pending >= 0) {
        uint32_t v, off = (uint32_t) g_pending;
        memcpy(&v, g_page + off, 4);
        g_pending = -1;
        dev_store(off, v);
    }
    mprotect(g_page, PAGE, PROT_NONE);
}

static void trap_install(void)
{
    void *p =
        mmap(g_page, PAGE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != g_page) {
        printf("[FAIL] cannot map the fake fw_cfg page at 0x%lx\n",
               (unsigned long) RAMFB_FWCFG_BASE);
        exit(2);
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sa.sa_sigaction = on_segv;
    sigaction(SIGSEGV, &sa, NULL);
    sa.sa_sigaction = on_trap;
    sigaction(SIGTRAP, &sa, NULL);
    signal(SIGALRM, on_alarm);
}

/* ===== helpers over the recorded traffic ===== */
static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}
static uint64_t rd_be64(const uint8_t *p)
{
    return ((uint64_t) rd_be32(p) << 32) | rd_be32(p + 4);
}

/* a fake framebuffer address; the device never dereferences it */
#define FB_ADDR ((volatile uint32_t *) (uintptr_t) 0x0000123456789AB0ull)

static void std_dir(uint16_t ramfb_sel)
{
    dir_add("bootorder", 0x0020, 0);
    dir_add("etc/ramfbx", 0x0021, 28);   /* longer: must not match */
    dir_add("etc/ramf", 0x0022, 28);     /* prefix: must not match */
    dir_add("Xtc/ramfb", 0x0024, 28);    /* first byte differs: must not match */
    dir_add("etc/ramfb", ramfb_sel, 28); /* the real one */
    dir_add("etc/ramfb", 0x0030, 28);    /* a later duplicate is never reached */
}

static void test_refusals(void)
{
    dev_reset();
    std_dir(0x0123);
    CHECK(ramfb_is_live() == false, "initially not live");
    CHECK(ramfb_init(NULL, 800, 600) == -1, "NULL fb -> -1");
    CHECK(ramfb_init(FB_ADDR, 0, 600) == -1, "width 0 -> -1");
    CHECK(ramfb_init(FB_ADDR, 800, 0) == -1, "height 0 -> -1");
    CHECK(D.ntrace == 0 && D.nx == 0 && D.stray == 0, "refusals touch no fw_cfg register");
    CHECK(ramfb_is_live() == false, "refusals leave it not live");
}

static void test_success(void)
{
    dev_reset();
    std_dir(0x0123);
    int rc = ramfb_init(FB_ADDR, 800, 600);
    CHECK(rc == 0, "init with etc/ramfb present -> 0");
    CHECK(ramfb_is_live() == true, "live after success");
    CHECK(D.nx == 2, "exactly two DMA transfers (dir read, cfg write)");
    CHECK(D.stray == 0, "no load and no store outside +0x10/+0x14");
    CHECK(D.ntrace == 4, "four register stores (2 per transfer)");
    CHECK(D.trace[0].off == 0x10u && D.trace[1].off == 0x14u && D.trace[2].off == 0x10u &&
              D.trace[3].off == 0x14u,
          "each kick stores the high half (+0x10) then the low half (+0x14)");

    /* transfer 0: directory read */
    xfer_t *x = &D.x[0];
    CHECK(x->control == 0x0019000Au, "dir: control = (0x19<<16)|SELECT|READ");
    CHECK(x->length == 16384u, "dir: length = 16 KiB buffer");
    CHECK(x->address != 0 && (x->address & 15u) == 0, "dir: buffer address non-zero, 16-aligned");
    CHECK((x->desc & 15u) == 0, "descriptor is 16-byte aligned");
    CHECK(D.trace[0].val == be32((uint32_t) (x->desc >> 32)) &&
              D.trace[1].val == be32((uint32_t) x->desc),
          "dir: descriptor address written big-endian, high then low");

    /* transfer 1: etc/ramfb write */
    x = &D.x[1];
    CHECK(x->control == 0x01230018u, "cfg: control = (sel 0x0123<<16)|SELECT|WRITE");
    CHECK(x->length == 28u, "cfg: length = sizeof(struct ramfb_cfg) = 28");
    CHECK(x->desc == D.x[0].desc, "cfg: same descriptor reused");
    CHECK(x->address != 0 && (x->address & 15u) == 0, "cfg: record address 16-aligned");
    CHECK(rd_be64(x->data + 0) == (uint64_t) (uintptr_t) FB_ADDR, "cfg: addr = be64(fb)");
    CHECK(rd_be32(x->data + 8) == 0x34325258u, "cfg: fourcc = be32('XR24')");
    CHECK(rd_be32(x->data + 12) == 0u, "cfg: flags = 0");
    CHECK(rd_be32(x->data + 16) == 800u, "cfg: width = be32(800)");
    CHECK(rd_be32(x->data + 20) == 600u, "cfg: height = be32(600)");
    CHECK(rd_be32(x->data + 24) == 3200u, "cfg: stride = be32(800*4)");
    CHECK(D.trace[3].val == be32((uint32_t) x->desc), "cfg: low half triggers the transfer");

    /* a second, different geometry and selector: nothing is cached */
    dev_reset();
    dir_add("etc/ramfb", 0x0A0B, 28);
    rc = ramfb_init((volatile uint32_t *) (uintptr_t) 0xFEDCBA9840000001ull, 1, 2);
    CHECK(rc == 0 && ramfb_is_live(), "1x2 at an odd 64-bit address -> 0, live");
    CHECK(D.x[1].control == 0x0A0B0018u, "selector 0x0A0B byte-swapped back correctly");
    CHECK(rd_be64(D.x[1].data) == 0xFEDCBA9840000001ull, "addr: all 64 bits, both halves");
    CHECK(rd_be32(D.x[1].data + 16) == 1u && rd_be32(D.x[1].data + 20) == 2u &&
              rd_be32(D.x[1].data + 24) == 4u,
          "1x2: width 1, height 2, stride 4");
}

static void test_missing(void)
{
    /* first make it live, then show a failing init drops the live flag */
    dev_reset();
    std_dir(0x0123);
    CHECK(ramfb_init(FB_ADDR, 64, 64) == 0 && ramfb_is_live(), "precondition: live");
    dev_reset();
    dir_add("bootorder", 0x0020, 0);
    dir_add("etc/ramfbx", 0x0021, 28);
    dir_add("etc/ramf", 0x0022, 28);
    dir_add("etc/RAMFB", 0x0023, 28);
    CHECK(ramfb_init(FB_ADDR, 64, 64) == -2, "no etc/ramfb -> -2");
    CHECK(D.nx == 1 && D.x[0].control == 0x0019000Au, "only the directory was read");
    CHECK(ramfb_is_live() == false, "failed init clears live");

    /* an empty directory */
    dev_reset();
    dir_set_count(0);
    CHECK(ramfb_init(FB_ADDR, 64, 64) == -2 && D.nx == 1, "empty directory -> -2");

    /* count says 2 but the match is the third entry: not searched */
    dev_reset();
    std_dir(0x0123);
    dir_set_count(4);
    CHECK(ramfb_init(FB_ADDR, 64, 64) == -2, "entries beyond the count are not searched");
    dev_reset();
    std_dir(0x0123);
    dir_set_count(5);
    CHECK(ramfb_init(FB_ADDR, 64, 64) == 0, "count 5 reaches the match at index 4");
}

static void test_bounds(void)
{
    /* a lying count: the walk stops at the last entry that fits the 16 KiB buffer
     * (index 254: 4 + 255*64 = 16324 <= 16384; index 255 would end at 16388) */
    dev_reset();
    for (int i = 0; i < 255; i++) dir_add(i == 254 ? "etc/ramfb" : "filler", 0x0100, 1);
    dir_set_count(0xFFFFFFFFu);
    CHECK(ramfb_init(FB_ADDR, 8, 8) == 0, "match at the last entry that fits (index 254)");
    CHECK(D.x[1].control == 0x01000018u, "index 254's selector used");

    dev_reset();
    for (int i = 0; i < 255; i++) dir_add("filler", 0x0100, 1);
    /* bytes 16324..16383 hold a partial 256th entry whose name would match */
    memcpy(D.dir + 4 + 255 * 64 + 8, "etc/ramfb", 10);
    dir_set_count(0xFFFFFFFFu);
    CHECK(ramfb_init(FB_ADDR, 8, 8) == -2, "a partial entry past the buffer is never read");
    CHECK(D.nx == 1, "no cfg write after a bounded miss");

    /* a 56-byte name with no terminator compares over all 56 bytes */
    dev_reset();
    char longname[57];
    memset(longname, 'q', 56);
    longname[56] = 0;
    dir_add(longname, 0x0040, 1);
    dir_add("etc/ramfb", 0x0041, 28);
    CHECK(ramfb_init(FB_ADDR, 8, 8) == 0 && D.x[1].control == 0x00410018u,
          "an unterminated 56-byte name is skipped, the real one found");
}

static void test_dma_faults(void)
{
    dev_reset();
    std_dir(0x0123);
    D.reply[0] = DEV_ERROR;
    CHECK(ramfb_init(FB_ADDR, 64, 64) == -2, "directory DMA ERROR -> -2");
    CHECK(D.nx == 1 && !ramfb_is_live(), "no cfg write after a failed dir read; not live");

    dev_reset();
    std_dir(0x0123);
    D.reply[1] = DEV_ERROR;
    CHECK(ramfb_init(FB_ADDR, 64, 64) == -3, "cfg DMA ERROR -> -3");
    CHECK(D.nx == 2 && !ramfb_is_live(), "cfg error: not live");

    dev_reset();
    std_dir(0x0123);
    D.reply[1] = DEV_HANG;
    CHECK(ramfb_init(FB_ADDR, 64, 64) == -3, "cfg DMA never completes -> bounded spin, -3");
    CHECK(!ramfb_is_live(), "hang: not live");

    /* completion arrives later, from a timer: the driver must keep polling */
    dev_reset();
    std_dir(0x0123);
    D.reply[0] = DEV_LATE;
    D.reply[1] = DEV_LATE;
    CHECK(ramfb_init(FB_ADDR, 64, 64) == 0 && ramfb_is_live(), "late completions are waited for");
    late_disarm();

    /* ERROR then cleared: the driver must stop at the ERROR, not wait it out */
    dev_reset();
    std_dir(0x0123);
    D.reply[1] = DEV_LATE_ERR;
    CHECK(ramfb_init(FB_ADDR, 64, 64) == -3, "a late ERROR fails the transfer at once");
    CHECK(!ramfb_is_live(), "late error: not live");
    late_disarm();

    /* then a healthy device works again */
    dev_reset();
    std_dir(0x0123);
    CHECK(ramfb_init(FB_ADDR, 64, 64) == 0 && ramfb_is_live(), "recovers after faults");
}

int main(void)
{
    trap_install();
    test_refusals();
    test_success();
    test_missing();
    test_bounds();
    test_dma_faults();
    printf("test_ramfb: %d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
