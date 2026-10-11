/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_virtio_gpu.c — host test for the virtio-gpu 2D scanout driver.
 *
 * THE FAKE DEVICE. virtio_gpu.c drives virtio-mmio registers with plain
 * volatile loads/stores at s_base+off, and expects side effects: a status
 * read-back after FEATURES_OK, a queue that completes when QUEUE_NOTIFY is
 * written. To give those accesses real side effects on the host, each mmio
 * slot is a PROT_NONE page. Every access faults; the SIGSEGV handler decodes
 * read/write from the x86 page-fault error code, serves reads from a register
 * model, opens the page and single-steps the faulting instruction (TF); the
 * SIGTRAP handler hands a store to the model and closes the page again. The
 * model is a virtio-mmio v1 (legacy) / v2 (modern) virtio-gpu: it logs every
 * register access, derives the ring addresses exactly as the device would
 * (three 64-bit addresses on v2; QueuePFN * GuestPageSize plus the spec's
 * avail/used offsets on v1), walks the avail ring on QUEUE_NOTIFY, records
 * each command's bytes and descriptor flags, answers it (OK_NODATA,
 * OK_DISPLAY_INFO with a scripted pmodes[], or an injected error), and
 * publishes a used entry and an interrupt.
 *
 * The test #includes virtio_gpu.c so it can call the static bring-up and poke
 * the driver's state to falsify each self-check; every scenario runs in a
 * fork()ed child so each starts from the driver's pristine statics.
 *
 * Covers: every virtio_gpu_state_t outcome and its string; the slot scan
 * (modern preferred, legacy taken, unknown versions, holes); the exact modern
 * and legacy handshake register sequences; queue depth negotiation; the
 * control-queue wire format of all six commands; geometry refusal; flush
 * clipping and offsets; interrupt acknowledgement; idempotence; every
 * self-check; and the bring-up's three exits (SKIP / budget refusal / fault).
 *
 * Linux/x86-64 host only (fault decoding via REG_ERR, single-step via TF).
 * Build it twice: -no-pie (with ASan) puts the rings below 16 TiB so a legacy
 * 32-bit page frame number can reach them; a default PIE build re-runs every
 * modern path with non-zero high address halves (legacy is then skipped). The
 * unanswered-command scenario spins the driver's full 200M-poll bound (~3-6 s);
 * VGPU_SKIP_TIMEOUT=1 skips it.
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
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

#include "virtio_gpu.c" /* the unit under test, statics and all */

#if !defined(__x86_64__) || !defined(__linux__)
#    error "test_virtio_gpu.c: the fault-trapping MMIO model needs Linux on x86-64"
#endif

/* ===== checks, shared across fork()ed scenarios ===== */
static struct {
    int checks, failures;
} *g_cnt;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        g_cnt->checks++;                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s (line %d)\n", m, __LINE__);                                          \
            g_cnt->failures++;                                                                     \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

/* ===== console capture ===== */
static char g_log[16384];
static size_t g_loglen;
void uart_puts(const char *s)
{
    size_t n = strlen(s);
    if (g_loglen + n >= sizeof g_log) n = sizeof g_log - 1 - g_loglen;
    memcpy(g_log + g_loglen, s, n);
    g_loglen += n;
    g_log[g_loglen] = 0;
}
static bool logged(const char *s)
{
    return strstr(g_log, s) != NULL;
}
static void log_clear(void)
{
    g_loglen = 0;
    g_log[0] = 0;
}

/* ===== the virtio-mmio virtio-gpu model ===== */
#define NSLOT  6
#define PAGE   4096u
#define MAXACC 256
#define MAXCMD 32
#define T_ERR  0x1200u /* VIRTIO_GPU_RESP_ERR_UNSPEC */

typedef struct {
    uint32_t type, req_len, resp_len;
    uint16_t req_flags, resp_flags, head;
    uint64_t req_addr, resp_addr;
    uint8_t req[64];
} cmd_t;

typedef struct {
    uint32_t x, y, w, h, enabled;
} pmode_t;

typedef struct {
    uint32_t reg[PAGE / 4]; /* plain storage, by offset/4 */
    uint32_t drv_feat[2];
    bool refuse_features; /* status read-back hides FEATURES_OK */
    bool no_irq;          /* never raise INTERRUPT_STATUS */
    bool queue_stuck;     /* queue 0 reads as already in use, even after reset */
    bool late;            /* publish each used entry ~1 ms later, from a timer */
    uint32_t isr;
    uint16_t last_avail;
    /* scripted answers */
    uint32_t fail_type, fail_resp; /* answer command `fail_type` with `fail_resp` */
    uint32_t silent_type;          /* complete `silent_type` without writing a response */
    uint32_t drop_type;            /* never complete `drop_type` */
    pmode_t pmodes[16];
    cmd_t cmd[MAXCMD];
    int ncmd;
    int bad_chain; /* chains not shaped [req R] -> [resp W] */
} slot_t;

typedef struct {
    int slot;
    uint32_t off, val;
    bool write;
} acc_t;

static uint8_t *g_pages;     /* NSLOT PROT_NONE pages */
static slot_t g_slot[NSLOT]; /* their device models */
static uint32_t g_nslots;    /* what virtio_mmio_slot_count() reports */
static bool g_hole[NSLOT];   /* slot base reported as 0 */
static acc_t g_acc[MAXACC];
static int g_nacc;
static int g_pend_slot = -1;
static uint32_t g_pend_off;

uint32_t virtio_mmio_slot_count(void)
{
    return g_nslots;
}
uint64_t virtio_mmio_slot_base(uint32_t i)
{
    if (i >= NSLOT || g_hole[i]) return 0;
    return (uint64_t) (uintptr_t) (g_pages + (size_t) i * PAGE);
}

static void slot_setup(int i, uint32_t magic, uint32_t version, uint32_t id)
{
    memset(&g_slot[i], 0, sizeof g_slot[i]);
    g_slot[i].reg[VMMIO_MAGIC / 4] = magic;
    g_slot[i].reg[VMMIO_VERSION / 4] = version;
    g_slot[i].reg[VMMIO_DEVICE_ID / 4] = id;
    g_slot[i].reg[VMMIO_VENDOR_ID / 4] = 0x554D4551u; /* "QEMU" */
    g_slot[i].reg[VMMIO_QUEUE_NUM_MAX / 4] = 256;
    g_slot[i].pmodes[0].w = 64;
    g_slot[i].pmodes[0].h = 48;
    g_slot[i].pmodes[0].enabled = 1;
}

/* one GPU in slot `gpu` of `n`, the rest empty */
static slot_t *one_gpu(uint32_t n, int gpu, uint32_t version)
{
    g_nslots = n;
    for (int i = 0; i < NSLOT; i++) slot_setup(i, 0, 0, 0);
    slot_setup(gpu, VMAGIC, version, VDEV_GPU);
    return &g_slot[gpu];
}

static void acc_clear(void)
{
    g_nacc = 0;
}

static uint32_t dev_read(int s, uint32_t off)
{
    slot_t *d = &g_slot[s];
    switch (off) {
    case VMMIO_STATUS:
        return d->refuse_features ? (d->reg[off / 4] & ~(uint32_t) VS_FEATURES_OK)
                                  : d->reg[off / 4];
    case VMMIO_INTERRUPT_STATUS:
        return d->isr;
    case VMMIO_QUEUE_READY:
    case VMMIO_L_QUEUE_PFN:
        return d->queue_stuck ? 5u : d->reg[off / 4];
    default:
        return d->reg[off / 4];
    }
}

static void *gp(uint64_t a)
{
    return (void *) (uintptr_t) a;
}

/* ---- late completion: the used entry appears ~1 ms after the notify ---- */
static slot_t *volatile g_late_dev;
static vring_used_t *volatile g_late_used;

static void on_alarm(int sig)
{
    (void) sig;
    if (!g_late_used) return;
    g_late_used->idx++;
    if (!g_late_dev->no_irq) g_late_dev->isr |= 1u;
    g_late_used = NULL;
}

static void late_arm(slot_t *d, vring_used_t *used)
{
    struct itimerval it = {{0, 0}, {0, 1000}};
    g_late_dev = d;
    g_late_used = used;
    setitimer(ITIMER_REAL, &it, NULL);
}

static void dev_answer(slot_t *d, uint16_t num, vring_desc_t *desc, vring_used_t *used,
                       uint16_t head)
{
    cmd_t *c = &d->cmd[d->ncmd < MAXCMD ? d->ncmd : MAXCMD - 1];
    d->ncmd++;
    vring_desc_t *rq = &desc[head % num];
    vring_desc_t *rs = rq->flags & VRING_DESC_F_NEXT ? &desc[rq->next % num] : NULL;
    memset(c, 0, sizeof *c);
    c->head = head;
    c->req_addr = rq->addr;
    c->req_len = rq->len;
    c->req_flags = rq->flags;
    memcpy(c->req, gp(rq->addr), rq->len < 64 ? rq->len : 64);
    memcpy(&c->type, c->req, 4);
    uint32_t written = 0;
    if (!rs || (rq->flags & VRING_DESC_F_WRITE) || !(rs->flags & VRING_DESC_F_WRITE) ||
        (rs->flags & VRING_DESC_F_NEXT)) {
        d->bad_chain++;
    } else {
        c->resp_addr = rs->addr;
        c->resp_len = rs->len;
        c->resp_flags = rs->flags;
        uint8_t *r = gp(rs->addr);
        uint32_t t =
            c->type == VGPU_CMD_GET_DISPLAY_INFO ? VGPU_RESP_OK_DISPLAY_INFO : VGPU_RESP_OK_NODATA;
        if (d->fail_type && c->type == d->fail_type) t = d->fail_resp;
        if (d->drop_type && c->type == d->drop_type) return;
        if (d->silent_type && c->type == d->silent_type) t = 0;
        if (t) {
            memset(r, 0, rs->len);
            memcpy(r, &t, 4);
            written = 24;
        }
        if (t == VGPU_RESP_OK_DISPLAY_INFO && rs->len >= 24 + 16 * 24) {
            for (int i = 0; i < 16; i++) {
                uint32_t pm[6] = {d->pmodes[i].x, d->pmodes[i].y,       d->pmodes[i].w,
                                  d->pmodes[i].h, d->pmodes[i].enabled, 0};
                memcpy(r + 24 + i * 24, pm, sizeof pm);
            }
            written = 24 + 16 * 24;
        }
    }
    used->ring[used->idx % num].id = head;
    used->ring[used->idx % num].len = written;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (d->late) {
        late_arm(d, used);
        return;
    }
    used->idx++;
    if (!d->no_irq) d->isr |= 1u;
}

static void dev_notify(slot_t *d)
{
    uint16_t num = (uint16_t) d->reg[VMMIO_QUEUE_NUM / 4];
    vring_desc_t *desc;
    vring_avail_t *avail;
    vring_used_t *used;
    if (num == 0) return;
    if (d->reg[VMMIO_VERSION / 4] == 1) {
        /* legacy: the device DERIVES avail and used from one page frame number */
        uint64_t base =
            (uint64_t) d->reg[VMMIO_L_QUEUE_PFN / 4] * d->reg[VMMIO_L_GUEST_PAGE_SIZE / 4];
        uint64_t align = d->reg[VMMIO_L_QUEUE_ALIGN / 4];
        uint64_t a = base + 16u * num;
        uint64_t u = (a + 6u + 2u * num + align - 1) & ~(align - 1);
        desc = gp(base);
        avail = gp(a);
        used = gp(u);
    } else {
        if (!d->reg[VMMIO_QUEUE_READY / 4]) return;
        desc = gp(((uint64_t) d->reg[VMMIO_QUEUE_DESC_HIGH / 4] << 32) |
                  d->reg[VMMIO_QUEUE_DESC_LOW / 4]);
        avail = gp(((uint64_t) d->reg[VMMIO_QUEUE_DRIVER_HIGH / 4] << 32) |
                   d->reg[VMMIO_QUEUE_DRIVER_LOW / 4]);
        used = gp(((uint64_t) d->reg[VMMIO_QUEUE_DEVICE_HIGH / 4] << 32) |
                  d->reg[VMMIO_QUEUE_DEVICE_LOW / 4]);
    }
    while (d->last_avail != avail->idx) {
        uint16_t head = avail->ring[d->last_avail % num];
        d->last_avail++;
        dev_answer(d, num, desc, used, head);
    }
}

static void dev_write(int s, uint32_t off, uint32_t val)
{
    slot_t *d = &g_slot[s];
    d->reg[off / 4] = val;
    switch (off) {
    case VMMIO_STATUS:
        if (val == 0) { /* device reset: the queue and its progress are forgotten */
            d->reg[VMMIO_QUEUE_READY / 4] = 0;
            d->reg[VMMIO_L_QUEUE_PFN / 4] = 0;
            d->reg[VMMIO_QUEUE_NUM / 4] = 0;
            d->isr = 0;
            d->last_avail = 0;
        }
        break;
    case VMMIO_DRIVER_FEATURES:
        d->drv_feat[d->reg[VMMIO_DRIVER_FEATURES_SEL / 4] & 1u] = val;
        break;
    case VMMIO_INTERRUPT_ACK:
        d->isr &= ~val;
        break;
    case VMMIO_QUEUE_NOTIFY:
        if (val == 0) dev_notify(d);
        break;
    default:
        break;
    }
}

static void acc_log(int s, uint32_t off, uint32_t val, bool w)
{
    if (g_nacc < MAXACC) g_acc[g_nacc] = (acc_t){s, off, val, w};
    g_nacc++;
}

/* ---- the trap: fault -> model -> single-step -> SIGTRAP -> model ---- */
static void on_segv(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = ucv;
    uintptr_t a = (uintptr_t) si->si_addr;
    (void) sig;
    if (a < (uintptr_t) g_pages || a >= (uintptr_t) g_pages + NSLOT * PAGE) {
        signal(SIGSEGV, SIG_DFL); /* a real crash: let it happen */
        return;
    }
    int s = (int) ((a - (uintptr_t) g_pages) / PAGE);
    uint32_t off = (uint32_t) ((a - (uintptr_t) g_pages) % PAGE);
    mprotect(g_pages, NSLOT * PAGE, PROT_READ | PROT_WRITE);
    if (uc->uc_mcontext.gregs[REG_ERR] & 2) {
        g_pend_slot = s;
        g_pend_off = off;
    } else {
        uint32_t v = dev_read(s, off);
        memcpy(g_pages + (size_t) s * PAGE + off, &v, 4);
        acc_log(s, off, v, false);
        g_pend_slot = -1;
    }
    uc->uc_mcontext.gregs[REG_EFL] |= 0x100; /* TF: trap after one instruction */
}

static void on_trap(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = ucv;
    (void) sig;
    (void) si;
    uc->uc_mcontext.gregs[REG_EFL] &= ~0x100;
    if (g_pend_slot >= 0) {
        int s = g_pend_slot;
        uint32_t v;
        memcpy(&v, g_pages + (size_t) s * PAGE + g_pend_off, 4);
        g_pend_slot = -1;
        acc_log(s, g_pend_off, v, true);
        dev_write(s, g_pend_off, v);
    }
    mprotect(g_pages, NSLOT * PAGE, PROT_NONE);
}

static void trap_install(void)
{
    g_pages = mmap(NULL, NSLOT * PAGE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g_pages == MAP_FAILED) {
        printf("[FAIL] cannot map the fake mmio pages\n");
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

/* ---- access-log queries ---- */
static int n_writes(void)
{
    int n = 0;
    for (int i = 0; i < g_nacc && i < MAXACC; i++) n += g_acc[i].write;
    return n;
}

/* the writes, in order, equal `w` (pairs of off,val) on slot s */
static bool writes_are(int s, const uint32_t *w, int n)
{
    int k = 0;
    for (int i = 0; i < g_nacc && i < MAXACC; i++) {
        if (!g_acc[i].write) continue;
        if (k >= n || g_acc[i].slot != s || g_acc[i].off != w[2 * k] ||
            g_acc[i].val != w[2 * k + 1])
            return false;
        k++;
    }
    return k == n;
}

static bool was_read(int s, uint32_t off)
{
    for (int i = 0; i < g_nacc && i < MAXACC; i++)
        if (!g_acc[i].write && g_acc[i].slot == s && g_acc[i].off == off) return true;
    return false;
}

static bool was_written(int s, uint32_t off)
{
    for (int i = 0; i < g_nacc && i < MAXACC; i++)
        if (g_acc[i].write && g_acc[i].slot == s && g_acc[i].off == off) return true;
    return false;
}

static uint32_t last_write(int s, uint32_t off)
{
    uint32_t v = 0xFFFFFFFFu;
    for (int i = 0; i < g_nacc && i < MAXACC; i++)
        if (g_acc[i].write && g_acc[i].slot == s && g_acc[i].off == off) v = g_acc[i].val;
    return v;
}

static uint32_t u32at(const uint8_t *p, int off)
{
    uint32_t v;
    memcpy(&v, p + off, 4);
    return v;
}
static uint64_t u64at(const uint8_t *p, int off)
{
    uint64_t v;
    memcpy(&v, p + off, 8);
    return v;
}

static bool hdr_rest_zero(const cmd_t *c)
{
    for (int i = 4; i < 24; i++)
        if (c->req[i]) return false;
    return true;
}

/* ===== scenario runner: each in a fresh fork ===== */
static void run(const char *name, void (*fn)(void))
{
    fflush(stdout);
    pid_t p = fork();
    if (p == 0) {
        printf("--- %s\n", name);
        fn();
        fflush(stdout);
        _exit(0);
    }
    int st = 0;
    waitpid(p, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        g_cnt->checks++;
        g_cnt->failures++;
        printf("[FAIL] scenario '%s' crashed (status 0x%x)\n", name, st);
    }
}

/* ===== scenarios ===== */
static int g_param; /* scenario variant, set before run() (fork copies it) */
static uint32_t a_, b_, c_;

#define W2(o, v) (uint32_t)(o), (uint32_t) (v)

static void expect_strings(void)
{
    static const struct {
        virtio_gpu_state_t st;
        const char *s;
    } t[] = {
        {VGPU_STATE_UNPROBED, "not probed yet"},
        {VGPU_STATE_ABSENT, "no device id 16 on any mmio slot"},
        {VGPU_STATE_NO_TRANSPORT, "board profile declares no virtio-mmio window"},
        {VGPU_STATE_BAD_TRANSPORT, "present; unknown transport version"},
        {VGPU_STATE_FEATURES_FAILED, "present; device refused our feature set"},
        {VGPU_STATE_QUEUE_FAILED, "present; control queue would not come up"},
        {VGPU_STATE_NO_DISPLAY_INFO, "present; GET_DISPLAY_INFO refused"},
        {VGPU_STATE_NO_SCANOUT, "present; answered, but no enabled scanout"},
        {VGPU_STATE_LIVE, "live"},
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        s_state = t[i].st;
        CHECK(strcmp(virtio_gpu_state_str(), t[i].s) == 0, t[i].s);
        CHECK(virtio_gpu_state() == t[i].st, "state() returns the recorded state");
    }
    s_state = (virtio_gpu_state_t) 99;
    CHECK(strcmp(virtio_gpu_state_str(), "unknown") == 0, "out-of-range state -> \"unknown\"");
}

static void sc_pristine(void)
{
    g_nslots = 0;
    CHECK(virtio_gpu_state() == VGPU_STATE_UNPROBED, "pristine: UNPROBED");
    CHECK(strcmp(virtio_gpu_state_str(), "not probed yet") == 0, "pristine: string");
    a_ = 77, b_ = 88, c_ = 99;
    CHECK(!virtio_gpu_transport(&a_, &b_) && a_ == 77 && b_ == 88,
          "pristine: transport false, outputs untouched");
    CHECK(!virtio_gpu_display_size(&a_, &b_, &c_) && a_ == 77 && b_ == 88 && c_ == 99,
          "pristine: display_size false, outputs untouched");
    CHECK(!virtio_gpu_is_live(), "pristine: not live");
    static uint32_t fb[16];
    CHECK(virtio_gpu_set_scanout(fb, 4, 4) == -1, "pristine: set_scanout -1");
    CHECK(virtio_gpu_flush(0, 0, 1, 1) == -1, "pristine: flush -1");
    CHECK(virtio_gpu_flush_all() == -1, "pristine: flush_all -1");
    uint32_t n = 0;
    CHECK(virtio_gpu_selfcheck(&n) == 8 && n == 8, "pristine: selfcheck 8/8 wire+state checks");
    CHECK(virtio_gpu_selfcheck(NULL) == 8, "pristine: selfcheck accepts NULL n_out");
    CHECK(g_nacc == 0, "pristine: no mmio access");
    expect_strings();
}

static void sc_no_transport(void)
{
    g_nslots = 0;
    CHECK(!virtio_gpu_probe(), "no transport: probe false");
    CHECK(virtio_gpu_state() == VGPU_STATE_NO_TRANSPORT, "no transport: NO_TRANSPORT");
    CHECK(!virtio_gpu_transport(&a_, &b_), "no transport: transport false");
    CHECK(virtio_bus_driver_count() == 1, "probe registers the class driver");
    const virtio_driver_t *d = NULL;
    CHECK(virtio_classify_slot(VMAGIC, VDEV_GPU, 2, &d) == VCLS_DISPATCH && d &&
              d->device_id == 16u && d->init_slot == gpu_init_slot && d->poll == NULL &&
              strcmp(d->name, "virtio-gpu") == 0,
          "registered cell: id 16, \"virtio-gpu\", gpu_init_slot, no poll");
    CHECK(g_nacc == 0, "no transport: no mmio access");
    /* the bus may hand init_slot any slot: it must check the registers itself */
    g_nslots = 2;
    slot_setup(0, VMAGIC, 2, VDEV_BLOCK);
    slot_setup(1, 0x0BADu, 2, VDEV_GPU);
    CHECK(!d->init_slot(virtio_mmio_slot_base(0)) && s_base == 0 && s_version == 0,
          "init_slot: a block device is refused, base and version cleared");
    CHECK(!d->init_slot(virtio_mmio_slot_base(1)) && s_base == 0 && s_version == 0,
          "init_slot: bad magic is refused, base and version cleared");
    CHECK(n_writes() == 0 && virtio_gpu_state() == VGPU_STATE_NO_TRANSPORT,
          "init_slot: refusals write nothing and keep the state");
    g_nslots = 0;
    log_clear();
    CHECK(virtio_gpu_bringup() == 0, "no transport: bring-up returns 0 (not a fault)");
    CHECK(logged("  [SKIP] no virtio-gpu device"), "no transport: [SKIP] line");
    CHECK(virtio_bus_driver_count() == 1, "re-probe does not duplicate the driver");
}

static void sc_absent(void)
{
    g_nslots = 5;
    for (int i = 0; i < NSLOT; i++) slot_setup(i, 0, 0, 0);
    g_hole[0] = true;                        /* slot 0: no base */
    slot_setup(1, 0x12345678u, 2, VDEV_GPU); /* wrong magic     */
    slot_setup(2, VMAGIC, 2, VDEV_BLOCK);    /* another device  */
    slot_setup(3, 0, 2, VDEV_GPU);           /* no magic        */
    slot_setup(4, VMAGIC, 2, VDEV_GPU + 1);  /* id 17           */
    slot_setup(5, VMAGIC, 2, VDEV_GPU);      /* beyond count    */
    CHECK(!virtio_gpu_probe(), "absent: probe false");
    CHECK(virtio_gpu_state() == VGPU_STATE_ABSENT, "absent: ABSENT");
    CHECK(!virtio_gpu_transport(&a_, &b_), "absent: transport false");
    CHECK(n_writes() == 0, "absent: no register written");
    CHECK(was_read(1, VMMIO_MAGIC) && !was_read(1, VMMIO_DEVICE_ID),
          "absent: wrong magic stops at the magic");
    CHECK(was_read(2, VMMIO_DEVICE_ID) && !was_read(2, VMMIO_VERSION),
          "absent: wrong id stops at the id");
    CHECK(was_read(4, VMMIO_DEVICE_ID) && !was_read(5, VMMIO_MAGIC),
          "absent: scan covers exactly slot_count slots");
    log_clear();
    CHECK(virtio_gpu_bringup() == 0 && logged("[SKIP]"), "absent: bring-up SKIPs, returns 0");
    uint32_t n = 0;
    CHECK(virtio_gpu_selfcheck(&n) == 8 && n == 8, "absent: selfcheck 8/8");
}

static void sc_bad_transport(void)
{
    one_gpu(3, 1, (uint32_t) g_param);
    log_clear();
    CHECK(!virtio_gpu_probe(), "bad transport: probe false");
    CHECK(virtio_gpu_state() == VGPU_STATE_BAD_TRANSPORT, "bad transport: BAD_TRANSPORT");
    CHECK(virtio_gpu_transport(&a_, &b_) && a_ == 1 && b_ == (uint32_t) g_param,
          "bad transport: reports slot 1 and the version read");
    CHECK(n_writes() == 0, "bad transport: no register written");
    char want[160];
    snprintf(want, sizeof want, "device id 16 found at mmio slot 1, transport v%d (unknown)\n",
             g_param);
    CHECK(logged(want), "bad transport: scan line names slot, version, (unknown)");
    snprintf(want, sizeof want, "device PRESENT on slot 1 but transport version %d is neither",
             g_param);
    CHECK(logged(want), "bad transport: refusal names the version");
    CHECK(!virtio_gpu_is_live(), "bad transport: not live");
    uint32_t n = 0;
    CHECK(virtio_gpu_selfcheck(&n) == 8 && n == 8, "bad transport: selfcheck 8/8 (base reset)");
    log_clear();
    CHECK(virtio_gpu_bringup() == 1, "bad transport: bring-up is a fault (1)");
    CHECK(logged("DEVICE PRESENT BUT NOT USABLE: present; unknown transport version"),
          "bad transport: fault line carries the reason");
}

static void sc_features_refused(void)
{
    slot_t *d = one_gpu(2, 1, 2);
    d->refuse_features = true;
    log_clear();
    CHECK(!virtio_gpu_probe(), "features refused: probe false");
    CHECK(virtio_gpu_state() == VGPU_STATE_FEATURES_FAILED, "features refused: FEATURES_FAILED");
    static const uint32_t w[] = {
        W2(VMMIO_STATUS, 0),
        W2(VMMIO_STATUS, VS_ACK),
        W2(VMMIO_STATUS, VS_ACK | VS_DRIVER),
        W2(VMMIO_DRIVER_FEATURES_SEL, 0),
        W2(VMMIO_DRIVER_FEATURES, 0),
        W2(VMMIO_DRIVER_FEATURES_SEL, 1),
        W2(VMMIO_DRIVER_FEATURES, 1),
        W2(VMMIO_STATUS, VS_ACK | VS_DRIVER | VS_FEATURES_OK),
        W2(VMMIO_STATUS, VS_FAILED),
    };
    CHECK(writes_are(1, w, 9), "features refused: exact modern handshake then FAILED");
    CHECK(was_read(1, VMMIO_STATUS), "features refused: status read back");
    CHECK(d->drv_feat[0] == 0 && d->drv_feat[1] == 1u,
          "features: only VIRTIO_F_VERSION_1 (bit 32) offered");
    CHECK(logged("device PRESENT on slot 1 but REFUSED our feature set"),
          "features refused: says so");
    CHECK(virtio_gpu_transport(&a_, &b_) && a_ == 1 && b_ == 2, "features refused: slot 1 v2");
    uint32_t n = 0;
    CHECK(virtio_gpu_selfcheck(&n) == 8 && n == 8, "features refused: selfcheck 8/8");
    CHECK(virtio_gpu_bringup() == 1, "features refused: bring-up fault");
}

/* g_param: 0 modern READY busy, 1 modern NUM_MAX 0, 2 modern NUM_MAX 1,
 *          3 legacy PFN busy, 4 legacy NUM_MAX 0, 5 legacy NUM_MAX 1 */
static void sc_queue_failed(void)
{
    bool legacy = g_param >= 3;
    slot_t *d = one_gpu(1, 0, legacy ? 1 : 2);
    switch (g_param % 3) {
    case 0:
        d->queue_stuck = true;
        break;
    case 1:
        d->reg[VMMIO_QUEUE_NUM_MAX / 4] = 0;
        break;
    default:
        d->reg[VMMIO_QUEUE_NUM_MAX / 4] = 1;
        break;
    }
    log_clear();
    CHECK(!virtio_gpu_probe(), "queue failed: probe false");
    CHECK(virtio_gpu_state() == VGPU_STATE_QUEUE_FAILED, "queue failed: QUEUE_FAILED");
    CHECK(!was_written(0, VMMIO_QUEUE_NUM) && !was_written(0, VMMIO_QUEUE_READY) &&
              !was_written(0, VMMIO_L_QUEUE_PFN),
          "queue failed: queue never published");
    CHECK(last_write(0, VMMIO_STATUS) == VS_FAILED, "queue failed: status FAILED last");
    CHECK(last_write(0, VMMIO_QUEUE_SEL) == 0, "queue failed: queue 0 selected");
    CHECK(logged(legacy ? "control queue would not come up (transport v1)"
                        : "control queue would not come up (transport v2)"),
          "queue failed: says which transport");
    CHECK(logged("device PRESENT on slot 0 but"), "queue failed: slot 0 printed as \"0\"");
    CHECK(!virtio_gpu_is_live() && s_base == 0, "queue failed: not live, base dropped");
    if (g_param % 3 != 0)
        CHECK(was_read(0, VMMIO_QUEUE_NUM_MAX), "queue failed: NUM_MAX consulted");
    else
        CHECK(!was_read(0, VMMIO_QUEUE_NUM_MAX), "queue failed: busy queue refused before NUM_MAX");
    CHECK(virtio_gpu_bringup() == 1, "queue failed: bring-up fault");
}

/* g_param: response type returned for GET_DISPLAY_INFO */
static void sc_no_display_info(void)
{
    slot_t *d = one_gpu(2, 1, 2);
    d->fail_type = VGPU_CMD_GET_DISPLAY_INFO;
    d->fail_resp = (uint32_t) g_param;
    log_clear();
    CHECK(!virtio_gpu_probe(), "no display info: probe false");
    CHECK(virtio_gpu_state() == VGPU_STATE_NO_DISPLAY_INFO, "no display info: NO_DISPLAY_INFO");
    char want[96];
    snprintf(want, sizeof want, "GET_DISPLAY_INFO refused (resp type %d, wanted 4353)\n", g_param);
    CHECK(logged(want), "no display info: response type printed in decimal");
    CHECK(last_write(1, VMMIO_STATUS) == VS_FAILED, "no display info: status FAILED last");
    CHECK(!virtio_gpu_is_live() && s_base == 0, "no display info: not live, base dropped");
    CHECK(!virtio_gpu_display_size(&a_, &b_, &c_), "no display info: no size");
    uint32_t n = 0;
    CHECK(virtio_gpu_selfcheck(&n) == 8 && n == 8, "no display info: selfcheck 8/8");
    log_clear();
    CHECK(virtio_gpu_bringup() == 1 && logged("present; GET_DISPLAY_INFO refused"),
          "no display info: bring-up fault with reason");
}

/* g_param: 0 all disabled, 1 enabled-but-degenerate modes */
static void sc_no_scanout(void)
{
    slot_t *d = one_gpu(1, 0, 2);
    if (g_param == 0) {
        d->pmodes[0].enabled = 0;
    } else {
        d->pmodes[0].enabled = 0; /* disabled, real size */
        d->pmodes[1] = (pmode_t){0, 0, 0, 48, 1};
        d->pmodes[2] = (pmode_t){0, 0, 64, 0, 1};
    }
    log_clear();
    CHECK(!virtio_gpu_probe(), "no scanout: probe false");
    CHECK(virtio_gpu_state() == VGPU_STATE_NO_SCANOUT, "no scanout: NO_SCANOUT");
    CHECK(logged("reports no enabled scanout"), "no scanout: says so");
    CHECK(!virtio_gpu_display_size(NULL, NULL, NULL), "no scanout: no size");
    CHECK(last_write(0, VMMIO_STATUS) == VS_FAILED && !virtio_gpu_is_live(),
          "no scanout: FAILED, not live");
    CHECK(virtio_gpu_bringup() == 1, "no scanout: bring-up fault");
}

/* only the LAST of the 16 pmodes is enabled: still found */
static void sc_last_scanout(void)
{
    slot_t *d = one_gpu(1, 0, 2);
    d->pmodes[0].enabled = 0;
    d->pmodes[15] = (pmode_t){0, 0, 32, 16, 1};
    CHECK(virtio_gpu_probe(), "last scanout: probe true");
    CHECK(virtio_gpu_display_size(&a_, &b_, &c_) && a_ == 32 && b_ == 16 && c_ == 15,
          "last scanout: pmodes[15] adopted, 32x16");
}

/* bring up a modern GPU in slot 3 of 4 reporting 80x60 on scanout 5 */
static slot_t *modern_live(void)
{
    slot_t *d = one_gpu(4, 3, 2);
    d->pmodes[0].enabled = 0;
    d->pmodes[1] = (pmode_t){0, 0, 0, 60, 1};
    d->pmodes[2] = (pmode_t){0, 0, 80, 0, 1};
    d->pmodes[5] = (pmode_t){0, 0, 80, 60, 1};
    d->pmodes[6] = (pmode_t){0, 0, 100, 100, 1};
    log_clear();
    acc_clear();
    CHECK(virtio_gpu_probe(), "modern: probe true");
    return d;
}

static void sc_modern_live(void)
{
    slot_t *d = modern_live();
    uint64_t dsc = (uint64_t) (uintptr_t) s_desc, av = (uint64_t) (uintptr_t) &s_avail,
             us = (uint64_t) (uintptr_t) &s_used;
    const uint32_t w[] = {
        W2(VMMIO_STATUS, 0),
        W2(VMMIO_STATUS, VS_ACK),
        W2(VMMIO_STATUS, VS_ACK | VS_DRIVER),
        W2(VMMIO_DRIVER_FEATURES_SEL, 0),
        W2(VMMIO_DRIVER_FEATURES, 0),
        W2(VMMIO_DRIVER_FEATURES_SEL, 1),
        W2(VMMIO_DRIVER_FEATURES, 1),
        W2(VMMIO_STATUS, VS_ACK | VS_DRIVER | VS_FEATURES_OK),
        W2(VMMIO_QUEUE_SEL, 0),
        W2(VMMIO_QUEUE_NUM, 8),
        W2(VMMIO_QUEUE_DESC_LOW, (uint32_t) dsc),
        W2(VMMIO_QUEUE_DESC_HIGH, (uint32_t) (dsc >> 32)),
        W2(VMMIO_QUEUE_DRIVER_LOW, (uint32_t) av),
        W2(VMMIO_QUEUE_DRIVER_HIGH, (uint32_t) (av >> 32)),
        W2(VMMIO_QUEUE_DEVICE_LOW, (uint32_t) us),
        W2(VMMIO_QUEUE_DEVICE_HIGH, (uint32_t) (us >> 32)),
        W2(VMMIO_QUEUE_READY, 1),
        W2(VMMIO_STATUS, VS_ACK | VS_DRIVER | VS_FEATURES_OK | VS_DRIVER_OK),
        W2(VMMIO_QUEUE_NOTIFY, 0),
        W2(VMMIO_INTERRUPT_ACK, 1),
    };
    CHECK(writes_are(3, w, 20), "modern: exact register write sequence on slot 3 only");
    CHECK(was_read(3, VMMIO_STATUS) && was_read(3, VMMIO_QUEUE_READY) &&
              was_read(3, VMMIO_QUEUE_NUM_MAX) && was_read(3, VMMIO_INTERRUPT_STATUS),
          "modern: status, ready, num_max and isr read");
    CHECK(!was_read(3, VMMIO_L_QUEUE_PFN) && !was_read(3, VMMIO_DEVICE_FEATURES),
          "modern: no legacy register touched");
    CHECK(d->isr == 0, "modern: interrupt acknowledged");
    CHECK(virtio_gpu_state() == VGPU_STATE_LIVE && virtio_gpu_is_live(), "modern: LIVE");
    CHECK(virtio_gpu_transport(&a_, &b_) && a_ == 3 && b_ == 2, "modern: slot 3, v2");
    CHECK(virtio_gpu_transport(NULL, NULL), "modern: transport accepts NULLs");
    a_ = b_ = c_ = 0;
    CHECK(virtio_gpu_display_size(&a_, &b_, &c_) && a_ == 80 && b_ == 60 && c_ == 5,
          "modern: first ENABLED non-degenerate scanout (5) adopted, 80x60");
    CHECK(virtio_gpu_display_size(NULL, NULL, NULL), "modern: display_size accepts NULLs");
    CHECK(d->ncmd == 1 && d->bad_chain == 0, "modern: one well-formed command");
    cmd_t *c = &d->cmd[0];
    CHECK(c->type == 0x0100u && hdr_rest_zero(c), "GET_DISPLAY_INFO: type 0x100, header zero");
    CHECK(c->req_len == 24 && c->resp_len == 408, "GET_DISPLAY_INFO: 24-byte req, 408-byte resp");
    CHECK(c->req_flags == VRING_DESC_F_NEXT && c->resp_flags == VRING_DESC_F_WRITE,
          "GET_DISPLAY_INFO: [req R] -> [resp W] chain");
    CHECK(c->req_addr == (uint64_t) (uintptr_t) &s_req &&
              c->resp_addr == (uint64_t) (uintptr_t) &s_resp,
          "GET_DISPLAY_INFO: request/response scratch addresses");
    CHECK(logged("  [virtio-gpu] device id 16 found at mmio slot 3, transport v2 (modern)\n"),
          "modern: scan line");
    CHECK(logged("  [DRIVER ONLINE] virtio-gpu 2D — device reports 80x60 on scanout 5 (read "
                 "from GET_DISPLAY_INFO, not assumed); mmio slot 3, transport v2 modern\n"),
          "modern: online line with geometry, scanout, slot, version");
    uint32_t n = 0;
    CHECK(virtio_gpu_selfcheck(&n) == 14 && n == 14, "modern: selfcheck 14/14");

    /* idempotent, singleton */
    acc_clear();
    CHECK(virtio_gpu_probe() && g_nacc == 0, "modern: second probe is a no-op");
    CHECK(!gpu_init_slot(virtio_mmio_slot_base(3)) && g_nacc == 0 && s_base != 0,
          "modern: init_slot refuses a second device while live");

    /* set_scanout refusals */
    static uint32_t fb[80 * 60];
    CHECK(virtio_gpu_flush(0, 0, 1, 1) == -1, "flush before set_scanout -> -1");
    CHECK(virtio_gpu_set_scanout(fb, 81, 60) == -3, "set_scanout: wrong width -3");
    CHECK(virtio_gpu_set_scanout(fb, 80, 59) == -3, "set_scanout: wrong height -3");
    CHECK(virtio_gpu_set_scanout(NULL, 80, 60) == -2, "set_scanout: NULL fb -2");
    CHECK(virtio_gpu_set_scanout(fb, 0, 60) == -2, "set_scanout: w 0 -2");
    CHECK(virtio_gpu_set_scanout(fb, 80, 0) == -2, "set_scanout: h 0 -2");
    CHECK(d->ncmd == 1, "set_scanout refusals issue no command");

    s_have_info = false;
    CHECK(virtio_gpu_set_scanout(fb, 80, 60) == -1, "set_scanout: live but no info -> -1");
    s_have_info = true;
    acc_clear();
    CHECK(virtio_gpu_set_scanout(fb, 80, 60) == 0, "set_scanout: 80x60 -> 0");
    CHECK(d->ncmd == 4 && d->bad_chain == 0, "set_scanout: three commands");
    c = &d->cmd[1];
    CHECK(c->type == 0x0101u && hdr_rest_zero(c) && c->req_len == 40 && c->resp_len == 24,
          "CREATE_2D: type 0x101, 40-byte req, 24-byte resp");
    CHECK(u32at(c->req, 24) == 1 && u32at(c->req, 28) == 2 && u32at(c->req, 32) == 80 &&
              u32at(c->req, 36) == 60,
          "CREATE_2D: resource 1, format B8G8R8X8 (2), 80x60");
    c = &d->cmd[2];
    CHECK(c->type == 0x0106u && hdr_rest_zero(c) && c->req_len == 48 && c->resp_len == 24,
          "ATTACH_BACKING: type 0x106, 48-byte req");
    CHECK(u32at(c->req, 24) == 1 && u32at(c->req, 28) == 1, "ATTACH_BACKING: resource 1, 1 entry");
    CHECK(u64at(c->req, 32) == (uint64_t) (uintptr_t) fb && u32at(c->req, 40) == 19200 &&
              u32at(c->req, 44) == 0,
          "ATTACH_BACKING: entry = fb, 80*60*4 bytes, padding 0");
    c = &d->cmd[3];
    CHECK(c->type == 0x0103u && hdr_rest_zero(c) && c->req_len == 48 && c->resp_len == 24,
          "SET_SCANOUT: type 0x103, 48-byte req");
    CHECK(u32at(c->req, 24) == 0 && u32at(c->req, 28) == 0 && u32at(c->req, 32) == 80 &&
              u32at(c->req, 36) == 60 && u32at(c->req, 40) == 5 && u32at(c->req, 44) == 1,
          "SET_SCANOUT: rect 0,0,80,60, scanout 5, resource 1");
    int notifies = 0, acks = 0;
    for (int i = 0; i < g_nacc && i < MAXACC; i++) {
        notifies += g_acc[i].write && g_acc[i].off == VMMIO_QUEUE_NOTIFY && g_acc[i].val == 0;
        acks += g_acc[i].write && g_acc[i].off == VMMIO_INTERRUPT_ACK && g_acc[i].val == 1;
    }
    CHECK(notifies == 3 && acks == 3, "set_scanout: one notify (queue 0) and one ack per command");

    /* flush */
    CHECK(virtio_gpu_flush(0, 0, 0, 5) == -2 && virtio_gpu_flush(0, 0, 5, 0) == -2,
          "flush: empty rect -2");
    CHECK(virtio_gpu_flush(80, 0, 1, 1) == -2 && virtio_gpu_flush(0, 60, 1, 1) == -2,
          "flush: origin off the panel -2");
    CHECK(d->ncmd == 4, "flush refusals issue no command");
    CHECK(virtio_gpu_flush(3, 4, 10, 5) == 0 && d->ncmd == 6, "flush: (3,4) 10x5 -> 0");
    c = &d->cmd[4];
    CHECK(c->type == 0x0105u && hdr_rest_zero(c) && c->req_len == 56 && c->resp_len == 24,
          "TRANSFER_TO_HOST_2D: type 0x105, 56-byte req");
    CHECK(u32at(c->req, 24) == 3 && u32at(c->req, 28) == 4 && u32at(c->req, 32) == 10 &&
              u32at(c->req, 36) == 5,
          "TRANSFER: rect 3,4,10,5");
    CHECK(u64at(c->req, 40) == (uint64_t) ((4 * 80 + 3) * 4), "TRANSFER: offset (y*w+x)*4 = 1292");
    CHECK(u32at(c->req, 48) == 1 && u32at(c->req, 52) == 0, "TRANSFER: resource 1, padding 0");
    c = &d->cmd[5];
    CHECK(c->type == 0x0104u && hdr_rest_zero(c) && c->req_len == 48 && c->resp_len == 24,
          "RESOURCE_FLUSH: type 0x104, 48-byte req");
    CHECK(u32at(c->req, 24) == 3 && u32at(c->req, 28) == 4 && u32at(c->req, 32) == 10 &&
              u32at(c->req, 36) == 5 && u32at(c->req, 40) == 1 && u32at(c->req, 44) == 0,
          "RESOURCE_FLUSH: rect 3,4,10,5, resource 1, padding 0");

    CHECK(virtio_gpu_flush(75, 57, 10, 10) == 0, "flush: overhanging rect -> 0");
    c = &d->cmd[6];
    CHECK(u32at(c->req, 24) == 75 && u32at(c->req, 28) == 57 && u32at(c->req, 32) == 5 &&
              u32at(c->req, 36) == 3,
          "flush: clipped to the panel (5x3)");
    CHECK(u64at(c->req, 40) == (uint64_t) ((57 * 80 + 75) * 4), "flush: clipped offset");
    c = &d->cmd[7];
    CHECK(u32at(c->req, 32) == 5 && u32at(c->req, 36) == 3, "flush: clipped RESOURCE_FLUSH");
    CHECK(virtio_gpu_flush(79, 59, 1, 1) == 0 && u32at(d->cmd[8].req, 32) == 1 &&
              u32at(d->cmd[8].req, 36) == 1 && u64at(d->cmd[8].req, 40) == 4 * (59 * 80 + 79),
          "flush: last pixel");
    CHECK(virtio_gpu_flush_all() == 0, "flush_all -> 0");
    c = &d->cmd[10];
    CHECK(c->type == 0x0105u && u32at(c->req, 24) == 0 && u32at(c->req, 28) == 0 &&
              u32at(c->req, 32) == 80 && u32at(c->req, 36) == 60 && u64at(c->req, 40) == 0,
          "flush_all: whole panel from offset 0");
    CHECK(d->cmd[11].type == 0x0104u && u32at(d->cmd[11].req, 32) == 80 &&
              u32at(d->cmd[11].req, 36) == 60,
          "flush_all: RESOURCE_FLUSH whole panel");
    CHECK(d->bad_chain == 0, "every chain was [req R] -> [resp W]");
    n = 0;
    CHECK(virtio_gpu_selfcheck(&n) == 14 && n == 14, "scanout up: selfcheck 14/14");

    /* interrupt handling: no interrupt -> no ack; pending bits acked as read */
    d->no_irq = true;
    acc_clear();
    CHECK(virtio_gpu_flush(0, 0, 1, 1) == 0, "flush without interrupt still completes");
    CHECK(was_read(3, VMMIO_INTERRUPT_STATUS) && !was_written(3, VMMIO_INTERRUPT_ACK),
          "no interrupt pending: no ack written");
    d->no_irq = false;
    d->isr = 4;
    acc_clear();
    CHECK(virtio_gpu_flush(0, 0, 1, 1) == 0, "flush with extra isr bits pending");
    bool ack5 = false;
    for (int i = 0; i < g_nacc && i < MAXACC; i++)
        ack5 =
            ack5 || (g_acc[i].write && g_acc[i].off == VMMIO_INTERRUPT_ACK && g_acc[i].val == 5u);
    CHECK(ack5 && d->isr == 0, "pending isr bits acked exactly as read (5)");

    /* each command's failure maps to its own code */
    static const struct {
        uint32_t type;
        int rc;
        const char *msg;
    } f[] = {{VGPU_CMD_RESOURCE_CREATE_2D, -4, "RESOURCE_CREATE_2D failed"},
             {VGPU_CMD_RESOURCE_ATTACH_BACKING, -5, "RESOURCE_ATTACH_BACKING failed"},
             {VGPU_CMD_SET_SCANOUT, -6, "SET_SCANOUT failed"}};
    for (int i = 0; i < 3; i++) {
        d->fail_type = f[i].type;
        d->fail_resp = T_ERR;
        log_clear();
        int k = d->ncmd;
        CHECK(virtio_gpu_set_scanout(fb, 80, 60) == f[i].rc, f[i].msg);
        CHECK(logged(f[i].msg), "failure is logged");
        CHECK(d->ncmd == k + i + 1, "set_scanout stops at the failing command");
    }
    d->fail_type = VGPU_CMD_TRANSFER_TO_HOST_2D;
    int k = d->ncmd;
    CHECK(virtio_gpu_flush(0, 0, 1, 1) == -3 && d->ncmd == k + 1, "flush: TRANSFER error -3");
    d->fail_type = VGPU_CMD_RESOURCE_FLUSH;
    CHECK(virtio_gpu_flush(0, 0, 1, 1) == -4, "flush: RESOURCE_FLUSH error -4");
    d->fail_type = VGPU_CMD_RESOURCE_FLUSH;
    d->fail_resp = VGPU_RESP_OK_DISPLAY_INFO;
    CHECK(virtio_gpu_flush(0, 0, 1, 1) == -4, "flush: a wrong OK type is still an error");
    d->fail_type = 0;
    /* a completion with no response written reads as type 0, not a stale OK */
    CHECK(virtio_gpu_flush(0, 0, 1, 1) == 0, "precondition: last response was OK_NODATA");
    d->silent_type = VGPU_CMD_TRANSFER_TO_HOST_2D;
    CHECK(virtio_gpu_flush(0, 0, 1, 1) == -3, "an empty response is a failure (scratch zeroed)");
    d->silent_type = 0;
    CHECK(virtio_gpu_flush(0, 0, 1, 1) == 0, "and the queue still works afterwards");

    /* a device that answers ~1 ms after the notify is polled for, not given up on */
    d->late = true;
    k = d->ncmd;
    CHECK(virtio_gpu_flush(2, 2, 2, 2) == 0 && d->ncmd == k + 2, "late completions are waited for");
    d->late = false;
    CHECK(virtio_gpu_flush(0, 0, 1, 1) == 0, "queue in step after late completions");
}

/* g_param: QUEUE_NUM_MAX; check the negotiated depth and that commands flow */
static void sc_depth(void)
{
    bool legacy = g_param < 0;
    uint32_t max = (uint32_t) (legacy ? -g_param : g_param);
    slot_t *d = one_gpu(1, 0, legacy ? 1 : 2);
    d->reg[VMMIO_QUEUE_NUM_MAX / 4] = max;
    CHECK(virtio_gpu_probe(), "depth: probe true");
    uint32_t want = max < 8 ? max : 8;
    CHECK(last_write(0, VMMIO_QUEUE_NUM) == want, "depth: QUEUE_NUM = min(NUM_MAX, 8)");
    CHECK(s_vq.depth == want, "depth: vring depth matches");
    static uint32_t fb[64 * 48];
    CHECK(virtio_gpu_set_scanout(fb, 64, 48) == 0, "depth: set_scanout");
    bool ok = true;
    for (int i = 0; i < 20; i++) ok = ok && virtio_gpu_flush_all() == 0;
    CHECK(ok && d->ncmd == 44 && d->bad_chain == 0, "depth: 44 commands through the ring wrap");
}

static void sc_legacy_live(void)
{
    slot_t *d = one_gpu(2, 0, 1);
    log_clear();
    CHECK(virtio_gpu_probe(), "legacy: probe true");
    const uint32_t w[] = {
        W2(VMMIO_STATUS, 0),
        W2(VMMIO_STATUS, VS_ACK),
        W2(VMMIO_STATUS, VS_ACK | VS_DRIVER),
        W2(VMMIO_DEVICE_FEATURES_SEL, 0),
        W2(VMMIO_DRIVER_FEATURES_SEL, 0),
        W2(VMMIO_DRIVER_FEATURES, 0),
        W2(VMMIO_L_GUEST_PAGE_SIZE, 4096),
        W2(VMMIO_QUEUE_SEL, 0),
        W2(VMMIO_QUEUE_NUM, 8),
        W2(VMMIO_L_QUEUE_ALIGN, 4096),
        W2(VMMIO_L_QUEUE_PFN, (uint32_t) ((uintptr_t) s_ring >> 12)),
        W2(VMMIO_STATUS, VS_ACK | VS_DRIVER | VS_DRIVER_OK),
        W2(VMMIO_QUEUE_NOTIFY, 0),
        W2(VMMIO_INTERRUPT_ACK, 1),
    };
    CHECK(writes_are(0, w, 14), "legacy: exact register write sequence (PFN, no FEATURES_OK)");
    CHECK(was_read(0, VMMIO_DEVICE_FEATURES) && was_read(0, VMMIO_L_QUEUE_PFN) &&
              was_read(0, VMMIO_QUEUE_NUM_MAX),
          "legacy: features, PFN and NUM_MAX read");
    CHECK(!was_read(0, VMMIO_QUEUE_READY) && !was_read(0, VMMIO_STATUS),
          "legacy: no QueueReady, no FEATURES_OK read-back");
    CHECK((void *) s_vq.desc == (void *) s_ring && (void *) s_vq.avail == (void *) (s_ring + 128) &&
              (void *) s_vq.used == (void *) (s_ring + 4096),
          "legacy: rings at desc+0, avail+16*8, used aligned to 4096");
    CHECK(virtio_gpu_state() == VGPU_STATE_LIVE, "legacy: LIVE");
    CHECK(virtio_gpu_transport(&a_, &b_) && a_ == 0 && b_ == 1, "legacy: slot 0, v1");
    CHECK(logged("found at mmio slot 0, transport v1 (legacy)\n"), "legacy: scan line");
    CHECK(logged("64x48 on scanout 0 (read from GET_DISPLAY_INFO, not assumed); mmio slot 0, "
                 "transport v1 legacy\n"),
          "legacy: online line");
    uint32_t n = 0;
    CHECK(virtio_gpu_selfcheck(&n) == 14 && n == 14, "legacy: selfcheck 14/14");
    static uint32_t fb[64 * 48];
    CHECK(virtio_gpu_set_scanout(fb, 64, 48) == 0 && virtio_gpu_flush_all() == 0,
          "legacy: set_scanout + flush through the device-derived rings");
    CHECK(d->ncmd == 6 && d->bad_chain == 0, "legacy: six well-formed commands");
    vring_used_t *keep = s_vq.used;
    s_vq.used = (vring_used_t *) (void *) (s_ring + 2048);
    CHECK(virtio_gpu_selfcheck(&n) == 13 && n == 14, "legacy: misplaced used ring fails a check");
    s_vq.used = keep;
}

/* g_param selects a slot layout; checks which slot the scan takes */
static void sc_scan(void)
{
    g_nslots = 4;
    for (int i = 0; i < NSLOT; i++) slot_setup(i, 0, 0, 0);
    int want = 0;
    uint32_t wver = 0;
    switch (g_param) {
    case 0: /* legacy, other, modern, modern -> first modern, scan stops */
        slot_setup(0, VMAGIC, 1, VDEV_GPU);
        slot_setup(1, VMAGIC, 2, VDEV_BLOCK);
        slot_setup(2, VMAGIC, 2, VDEV_GPU);
        slot_setup(3, VMAGIC, 2, VDEV_GPU);
        want = 2, wver = 2;
        break;
    case 1: /* legacy, legacy -> first legacy, scan continues */
        slot_setup(0, VMAGIC, 1, VDEV_GPU);
        slot_setup(1, VMAGIC, 1, VDEV_GPU);
        want = 0, wver = 1;
        break;
    case 2: /* v3, modern -> modern */
        slot_setup(0, VMAGIC, 3, VDEV_GPU);
        slot_setup(1, VMAGIC, 2, VDEV_GPU);
        want = 1, wver = 2;
        break;
    case 3: /* legacy, v3 -> legacy */
        slot_setup(0, VMAGIC, 1, VDEV_GPU);
        slot_setup(1, VMAGIC, 3, VDEV_GPU);
        want = 0, wver = 1;
        break;
    default: /* v3, legacy -> v3 is taken first and refused */
        slot_setup(0, VMAGIC, 3, VDEV_GPU);
        slot_setup(1, VMAGIC, 1, VDEV_GPU);
        want = 0, wver = 3;
        break;
    }
    bool up = virtio_gpu_probe();
    CHECK(up == (wver != 3), "scan: probe outcome");
    CHECK(virtio_gpu_transport(&a_, &b_) && a_ == (uint32_t) want && b_ == wver,
          "scan: the expected slot and version");
    bool only = true;
    for (int i = 0; i < g_nacc && i < MAXACC; i++)
        if (g_acc[i].write && g_acc[i].slot != want) only = false;
    CHECK(only, "scan: only the chosen slot is written");
    if (g_param == 0) CHECK(!was_read(3, VMMIO_MAGIC), "scan: stops at the first modern slot");
    if (g_param == 1) CHECK(was_read(1, VMMIO_VERSION), "scan: keeps looking past a legacy slot");
}

static void sc_selfcheck_pokes(void)
{
    modern_live();
    static uint32_t fb[80 * 60];
    CHECK(virtio_gpu_set_scanout(fb, 80, 60) == 0, "pokes: scanout up");
    uint32_t n = 0;
#define POKE(var, val, want_ok, want_n, msg)                                                       \
    do {                                                                                           \
        __typeof__(var) keep_ = var;                                                               \
        var = val;                                                                                 \
        CHECK(virtio_gpu_selfcheck(&n) == (want_ok) && n == (want_n), msg);                        \
        var = keep_;                                                                               \
    } while (0)
    CHECK(virtio_gpu_selfcheck(&n) == 14 && n == 14, "pokes: baseline 14/14");
    POKE(s_w_wire, 81, 13, 14, "selfcheck: width differs from the wire witness");
    POKE(s_h_wire, 61, 13, 14, "selfcheck: height differs from the wire witness");
    POKE(s_wire_seen, false, 13, 14, "selfcheck: no wire witness");
    POKE(s_h, 0, 12, 14, "selfcheck: zero height (degenerate + witness)");
    POKE(s_w, 0, 12, 14, "selfcheck: zero width (degenerate + witness)");
    POKE(s_version, 3, 13, 14, "selfcheck: version neither 1 nor 2");
    POKE(s_version, 1, 13, 14, "selfcheck: legacy flag disagrees with version 1");
    POKE(s_legacy, true, 13, 14, "selfcheck: legacy flag on a v2 transport");
    POKE(s_state, VGPU_STATE_ABSENT, 12, 14, "selfcheck: live but state ABSENT");
    POKE(s_state, VGPU_STATE_NO_TRANSPORT, 12, 14, "selfcheck: live but state NO_TRANSPORT");
    POKE(s_state, VGPU_STATE_NO_SCANOUT, 13, 14, "selfcheck: live but state NO_SCANOUT");
    POKE(s_have_info, false, 13, 14, "selfcheck: live without display info");
    POKE(s_base, 0, 13, 14, "selfcheck: live without a base");
    POKE(s_live, false, 6, 8, "selfcheck: not live but base and LIVE state remain");
    POKE(s_scanout_up, false, 14, 14, "selfcheck: budget check vacuous without a scanout");
    /* the budget: exactly VGPU_MAX_W*VGPU_MAX_H passes, one row more fails */
    s_w = s_w_wire = VGPU_MAX_W;
    s_h = s_h_wire = VGPU_MAX_H;
    CHECK(virtio_gpu_selfcheck(&n) == 14, "selfcheck: mode at the budget passes");
    s_h = s_h_wire = VGPU_MAX_H + 1;
    CHECK(virtio_gpu_selfcheck(&n) == 13, "selfcheck: mode over the budget fails");
    s_scanout_up = false;
    CHECK(virtio_gpu_selfcheck(&n) == 14, "selfcheck: over budget is fine with no scanout");
}

static void sc_bringup_ok(void)
{
    slot_t *d = one_gpu(1, 0, 2);
    log_clear();
    CHECK(virtio_gpu_bringup() == 0, "bring-up: live device -> 0");
    CHECK(d->ncmd == 6 && d->bad_chain == 0,
          "bring-up: info, create, attach, scanout, xfer, flush");
    CHECK(d->cmd[1].type == 0x0101u && u32at(d->cmd[1].req, 32) == 64 &&
              u32at(d->cmd[1].req, 36) == 48,
          "bring-up: resource at the device's 64x48");
    const uint32_t *fb = gp(u64at(d->cmd[2].req, 32));
    CHECK(u32at(d->cmd[2].req, 40) == 64 * 48 * 4, "bring-up: backing is 64*48*4 bytes");
    CHECK(fb[0] == 0x00FFFFFFu && fb[3 * 64 + 3] == 0x00FFFFFFu && fb[30] == 0x00FFFFFFu,
          "bring-up: 4-pixel white frame (top/left)");
    CHECK(fb[47 * 64 + 63] == 0x00FFFFFFu && fb[20 * 64 + 60] == 0x00FFFFFFu &&
              fb[44 * 64 + 20] == 0x00FFFFFFu,
          "bring-up: white frame (bottom/right: x+4>=w, y+4>=h)");
    CHECK(fb[4 * 64 + 4] == ((15u << 16) | (21u << 8) | 0x40u),
          "bring-up: (4,4) = ramp r=4*255/64, g=4*255/48");
    CHECK(fb[10 * 64 + 20] == ((79u << 16) | (53u << 8) | 0x40u), "bring-up: (20,10) ramp");
    CHECK(fb[43 * 64 + 59] == ((235u << 16) | (228u << 8) | 0x40u),
          "bring-up: (59,43) last interior pixel");
    /* each frame edge on its own, away from the corners */
#define RAMP(x, y) ((((x) * 255u / 64u) << 16) | (((y) * 255u / 48u) << 8) | 0x40u)
    CHECK(fb[20 * 64 + 3] == 0x00FFFFFFu && fb[20 * 64 + 4] == RAMP(4u, 20u),
          "bring-up: left frame is x<4");
    CHECK(fb[3 * 64 + 20] == 0x00FFFFFFu && fb[4 * 64 + 20] == RAMP(20u, 4u),
          "bring-up: top frame is y<4");
    CHECK(fb[20 * 64 + 60] == 0x00FFFFFFu && fb[20 * 64 + 59] == RAMP(59u, 20u),
          "bring-up: right frame is x+4>=w");
    CHECK(fb[44 * 64 + 20] == 0x00FFFFFFu && fb[43 * 64 + 20] == RAMP(20u, 43u),
          "bring-up: bottom frame is y+4>=h");
    bool all = true;
    for (uint32_t y = 4; y < 44; y++)
        for (uint32_t x = 4; x < 60; x++) all = all && fb[y * 64 + x] == RAMP(x, y);
    CHECK(all, "bring-up: every interior pixel is the r/g ramp over 0x40 blue");
    CHECK(d->cmd[4].type == 0x0105u && u32at(d->cmd[4].req, 32) == 64 &&
              u32at(d->cmd[4].req, 36) == 48 && d->cmd[5].type == 0x0104u,
          "bring-up: whole-panel flush");
    CHECK(logged("  [virtio-gpu] scanout live + flushed; selfcheck 14/14\n"),
          "bring-up: selfcheck line");

    /* faults after the device is live */
    d->fail_type = VGPU_CMD_RESOURCE_CREATE_2D;
    d->fail_resp = T_ERR;
    log_clear();
    CHECK(virtio_gpu_bringup() == 1 && logged("[virtio-gpu] set_scanout failed"),
          "bring-up: set_scanout failure -> 1");
    d->fail_type = VGPU_CMD_RESOURCE_FLUSH;
    log_clear();
    CHECK(virtio_gpu_bringup() == 1 && logged("[virtio-gpu] flush failed"),
          "bring-up: flush failure -> 1");
    d->fail_type = 0;
    s_w_wire++;
    log_clear();
    CHECK(virtio_gpu_bringup() == 1 && logged("selfcheck 13/14"),
          "bring-up: a failing self-check -> 1");
}

/* g_param: 0 one row over budget (refused), 1 exactly the budget (driven) */
static void sc_bringup_budget(void)
{
    slot_t *d = one_gpu(1, 0, 2);
    d->pmodes[0].w = VGPU_MAX_W;
    d->pmodes[0].h = VGPU_MAX_H + (g_param == 0 ? 1u : 0u);
    log_clear();
    if (g_param == 0) {
        CHECK(virtio_gpu_bringup() == 0, "budget: over-budget mode -> 0 (honest, not a fault)");
        CHECK(logged("  [virtio-gpu] device mode 1920x1081 exceeds the static scanout budget "
                     "1920x1080 — REFUSED"),
              "budget: both sizes printed");
        CHECK(d->ncmd == 1, "budget: no resource created");
    } else {
        CHECK(virtio_gpu_bringup() == 0 && d->ncmd == 6, "budget: exact-budget mode is driven");
        const uint32_t *fb = gp(u64at(d->cmd[2].req, 32));
        CHECK(fb[1079u * 1920u + 1919u] == 0x00FFFFFFu &&
                  fb[540u * 1920u + 960u] == ((127u << 16) | (127u << 8) | 0x40u),
              "budget: full-size pattern written");
    }
}

/* A device that stops answering: the poll is bounded (200M spins), reported,
 * and the chain it never returned keeps its descriptors -- so on a depth-2
 * queue the NEXT command finds the ring exhausted and fails at once, without
 * a notify and without a second timeout. */
static void sc_timeout(void)
{
    slot_t *d = one_gpu(1, 0, 2);
    d->reg[VMMIO_QUEUE_NUM_MAX / 4] = 2;
    static uint32_t fb[64 * 48];
    CHECK(virtio_gpu_probe() && virtio_gpu_set_scanout(fb, 64, 48) == 0, "timeout: live");
    d->drop_type = VGPU_CMD_TRANSFER_TO_HOST_2D;
    log_clear();
    CHECK(virtio_gpu_flush_all() == -3, "timeout: unanswered TRANSFER -> -3");
    CHECK(logged("  [virtio-gpu] control queue timeout\n"), "timeout: reported");
    acc_clear();
    log_clear();
    CHECK(virtio_gpu_flush_all() == -3, "timeout: ring exhausted -> -3");
    CHECK(!was_written(0, VMMIO_QUEUE_NOTIFY) && !logged("timeout"),
          "timeout: exhausted ring fails at once, no notify");
}

/* A legacy slot is handed a 32-bit page frame number, so it can only address
 * rings below 16 TiB. A PIE host binary lives far above that (0x55..., 0x7f...),
 * and a -no-pie one lives below 4 GiB -- where every high address half is 0.
 * So the recipe builds this test twice: -no-pie (+ASan) runs everything, and a
 * PIE build re-runs the modern paths with non-zero high halves. */
static bool legacy_reachable(void)
{
    return ((uint64_t) (uintptr_t) s_ring >> 12) <= 0xFFFFFFFFu;
}

static void run_all(void)
{
    bool leg = legacy_reachable();
    printf("rings at %p: legacy %s, high address halves %s\n", (void *) s_ring,
           leg ? "reachable" : "SKIPPED (PFN is 32 bits)",
           ((uint64_t) (uintptr_t) s_desc >> 32) ? "non-zero" : "zero");
    run("pristine", sc_pristine);
    run("no transport", sc_no_transport);
    run("absent", sc_absent);
    for (g_param = 0; g_param <= 3; g_param += 3) run("bad transport", sc_bad_transport);
    run("features refused", sc_features_refused);
    for (g_param = 0; g_param < 6; g_param++) run("queue failed", sc_queue_failed);
    g_param = T_ERR;
    run("no display info (ERR)", sc_no_display_info);
    g_param = VGPU_RESP_OK_NODATA;
    run("no display info (OK_NODATA)", sc_no_display_info);
    for (g_param = 0; g_param < 2; g_param++) run("no scanout", sc_no_scanout);
    run("modern live", sc_modern_live);
    run("last scanout", sc_last_scanout);
    static const int depths[] = {2, 3, 4, 7, 8, 9, 256, -2, -4, -8, -256};
    for (unsigned i = 0; i < sizeof depths / sizeof depths[0]; i++) {
        g_param = depths[i];
        if (leg || g_param > 0) run("depth", sc_depth);
    }
    if (leg) run("legacy live", sc_legacy_live);
    for (g_param = 0; g_param < 5; g_param++)
        if (leg || (g_param != 1 && g_param != 3)) run("scan", sc_scan);
    run("selfcheck pokes", sc_selfcheck_pokes);
    run("bring-up", sc_bringup_ok);
    for (g_param = 0; g_param < 2; g_param++) run("bring-up budget", sc_bringup_budget);
    if (!getenv("VGPU_SKIP_TIMEOUT")) run("timeout", sc_timeout);
}

int main(void)
{
    g_cnt = mmap(NULL, sizeof *g_cnt, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (g_cnt == MAP_FAILED) return 2;
    trap_install();
    run_all();
    printf("test_virtio_gpu: %d/%d checks passed\n", g_cnt->checks - g_cnt->failures,
           g_cnt->checks);
    return g_cnt->failures ? 1 : 0;
}
