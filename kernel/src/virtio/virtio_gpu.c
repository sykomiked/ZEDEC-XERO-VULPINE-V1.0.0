/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* virtio_gpu.c — virtio-gpu 2D scanout over virtio-mmio. See virtio_gpu.h.
 *
 * A DRIVER ON AN EXISTING TRANSPORT, NOT A NEW SUBSYSTEM
 * -----------------------------------------------------
 * virtio_bus.c already owns the mmio scan and the class registry; vring.c
 * already owns the split-virtqueue bookkeeping (the free list, the chains, the
 * avail/used wrap) under host test. So this file is only the two things that
 * are actually virtio-gpu: the device handshake, and the control-queue command
 * set. It re-implements neither, which is the whole reason the groundwork was
 * worth having.
 *
 * ENDIANNESS: every wire field below is little-endian by the VIRTIO 1.x
 * modern-device contract. All five ZXV targets are little-endian, so the
 * structs are used directly with no swap -- which is deliberate and not
 * laziness: __builtin_bswap on the ilp32 targets lowers to a libgcc helper that
 * does not exist here (ramfb.c:21-28 records that exact injury). A future
 * big-endian target must add a swap layer at the struct boundary; it must not
 * add builtins.
 *
 * DMA COHERENCE: this driver issues ordering barriers (ZXV_DSB) but NO cache
 * maintenance, because there is no portable cache-maintenance shim in this tree
 * -- virtio_snd.c open-codes `dc cvac`/`dc ivac` and is arm64-only as a result,
 * and copying that here would have made "portable across five arches" false.
 * Under QEMU (no cache emulation) barriers are sufficient. On real hardware
 * with a non-coherent interconnect this needs a ZXV_CACHE_CLEAN/INVALIDATE
 * abstraction beside ZXV_DSB; that gap is stated rather than hidden.
 *
 * BOTH TRANSPORT GENERATIONS, AND WHY
 * ----------------------------------
 * This driver drives virtio-mmio version 2 (modern) AND version 1 (legacy).
 * That is not generality for its own sake -- it is the fix for a measured
 * defect. QEMU's virtio-mmio proxy defaults to force-legacy=true, so a plain
 * `-device virtio-gpu-device` is presented on a VERSION 1 slot. MEASURED with
 * the QEMU monitor at slot 31 (0x0a003e00) on -M virt, QEMU 6.2.0:
 *
 *     xp/4wx 0x0a003e00 ->  0x74726976  0x00000001  0x00000010  0x554d4551
 *                           magic"virt"  VERSION=1   DEVICE_ID=16  "QEMU"
 *
 * A real, correctly identified GPU. The old probe called virtio_mmio_find(),
 * which matches version==2 only, got -1, and the boot log then said "no
 * virtio-gpu device". PRESENT-BUT-REFUSED had been collapsed into ABSENT. The
 * two are separated now by construction: the scan classifies the slot, every
 * failure records a REASON (virtio_gpu_state_t), and the [SKIP] line is
 * reachable only from a scan that genuinely found no device id 16 anywhere.
 *
 * The two generations differ in exactly two places, both isolated below:
 * feature negotiation (legacy has 32 feature bits, no VIRTIO_F_VERSION_1 and no
 * FEATURES_OK round trip) and queue publication (legacy takes ONE page frame
 * number and DERIVES avail/used from it, so the three rings must be laid out
 * contiguously at the offsets the device will compute).
 */
#include "zxv_barrier.h"
#include "virtio_mmio.h"
#include "virtio_bus.h"
#include "vring.h"
#include "virtio_gpu.h"

extern void uart_puts(const char *s);

/* ---- virtio-gpu control commands (VIRTIO 1.1 §5.7.6.1) ---- */
#define VGPU_CMD_GET_DISPLAY_INFO     0x0100u
#define VGPU_CMD_RESOURCE_CREATE_2D   0x0101u
#define VGPU_CMD_RESOURCE_UNREF       0x0102u
#define VGPU_CMD_SET_SCANOUT          0x0103u
#define VGPU_CMD_RESOURCE_FLUSH       0x0104u
#define VGPU_CMD_TRANSFER_TO_HOST_2D  0x0105u
#define VGPU_CMD_RESOURCE_ATTACH_BACKING 0x0106u
#define VGPU_RESP_OK_NODATA           0x1100u
#define VGPU_RESP_OK_DISPLAY_INFO     0x1101u

/* Pixel format. The framebuffer this kernel composes into is XRGB8888 in
 * memory-byte order B,G,R,X -- the same buffer ramfb is handed as DRM fourcc
 * 'XR24'. virtio-gpu spells that B8G8R8X8_UNORM (2); this is the mapping the
 * Linux virtio-gpu driver uses for DRM_FORMAT_XRGB8888, and getting it wrong
 * produces a picture with red and blue swapped rather than an error. */
#define VGPU_FORMAT_B8G8R8X8_UNORM    2u

#define VGPU_MAX_SCANOUTS   16u   /* fixed by the spec's pmodes[] array   */
#define VGPU_CTRLQ          0u    /* queue 0 = control, queue 1 = cursor  */
#define VGPU_RESOURCE_ID    1u    /* our single 2D scanout resource       */
#define VGPU_QDEPTH         8u    /* two descriptors per command; 8 is ample */

/* ---- legacy (virtio-mmio version 1) transport registers --------------------
 * These live here rather than in virtio_mmio.h because that header is the
 * MODERN contract and is shared by every other driver in this tree, all of
 * which are modern-only; widening it is a different change with a different
 * blast radius. The offsets are VIRTIO 0.9.5 §4.2.4 (the "legacy interface"
 * appendix of VIRTIO 1.x). Note 0x044 (QueueReady) does NOT exist on a legacy
 * slot and is never touched on that path. */
#define VMMIO_L_GUEST_PAGE_SIZE 0x028
#define VMMIO_L_QUEUE_ALIGN     0x03c
#define VMMIO_L_QUEUE_PFN       0x040
#define VGPU_LEGACY_PAGE   4096u  /* the unit QueuePFN counts in           */
#define VGPU_LEGACY_SHIFT  12u    /* log2(VGPU_LEGACY_PAGE): a CONSTANT shift,
                                   * so no libgcc helper on the ilp32 targets */
#define VGPU_LEGACY_ALIGN  4096u  /* QueueAlign: where the used ring starts */
_Static_assert(VGPU_LEGACY_PAGE == (1u << VGPU_LEGACY_SHIFT), "PFN shift must match page size");

/* ---- wire structs (little-endian, naturally aligned, no packing needed) ---- */
typedef struct { uint32_t type, flags; uint64_t fence_id; uint32_t ctx_id;
                 uint8_t ring_idx, padding[3]; } vgpu_hdr_t;
typedef struct { uint32_t x, y, width, height; } vgpu_rect_t;
typedef struct { vgpu_rect_t r; uint32_t enabled, flags; } vgpu_display_one_t;
typedef struct { vgpu_hdr_t hdr; vgpu_display_one_t pmodes[VGPU_MAX_SCANOUTS]; }
        vgpu_resp_display_info_t;
typedef struct { vgpu_hdr_t hdr; uint32_t resource_id, format, width, height; }
        vgpu_create_2d_t;
typedef struct { uint64_t addr; uint32_t length, padding; } vgpu_mem_entry_t;
typedef struct { vgpu_hdr_t hdr; uint32_t resource_id, nr_entries;
                 vgpu_mem_entry_t entry; } vgpu_attach_backing_t;
typedef struct { vgpu_hdr_t hdr; vgpu_rect_t r; uint32_t scanout_id, resource_id; }
        vgpu_set_scanout_t;
typedef struct { vgpu_hdr_t hdr; vgpu_rect_t r; uint64_t offset;
                 uint32_t resource_id, padding; } vgpu_xfer_2d_t;
typedef struct { vgpu_hdr_t hdr; vgpu_rect_t r; uint32_t resource_id, padding; }
        vgpu_flush_t;

/* The wire layout is a contract with the host, so it is asserted at COMPILE
 * time rather than discovered as a blank screen. A padding change in any of
 * these fails the build instead of silently shifting every field after it. */
_Static_assert(sizeof(vgpu_hdr_t) == 24, "virtio_gpu ctrl_hdr must be 24 bytes");
_Static_assert(__builtin_offsetof(vgpu_create_2d_t, height) == 36, "create_2d layout");
_Static_assert(__builtin_offsetof(vgpu_set_scanout_t, resource_id) == 44, "set_scanout layout");
_Static_assert(__builtin_offsetof(vgpu_xfer_2d_t, offset) == 40, "transfer_2d layout");
_Static_assert(sizeof(vgpu_mem_entry_t) == 16, "mem_entry must be 16 bytes");

/* ---- driver state ---- */
static uint64_t s_base       = 0;   /* device mmio base                    */
static bool     s_live       = false;
static bool     s_have_info  = false;
static bool     s_scanout_up = false;
static uint32_t s_w = 0, s_h = 0;   /* READ from the device, never assumed */
static uint32_t s_scanout_id = 0;
/* Transport facts, all READ from the slot, none assumed. s_state is the single
 * place the "why" of a failed probe is recorded, which is what stops a present
 * device from ever being logged as an absent one again. */
static uint32_t s_slot    = 0;      /* mmio slot index the device sits in  */
static uint32_t s_version = 0;      /* transport version READ from the slot */
static bool     s_legacy  = false;  /* version 1: PFN queue, no VERSION_1  */
static virtio_gpu_state_t s_state = VGPU_STATE_UNPROBED;
/* An untouched snapshot of the rectangle as it arrived off the wire, written
 * ONLY by read_display_info(). s_resp is scratch and gets reused by every later
 * command, so it cannot serve as the witness; this can. The self-check compares
 * the live geometry against it, which is what makes "the size came from the
 * device" a checkable claim rather than a comment. */
static uint32_t s_w_wire = 0, s_h_wire = 0;
static bool     s_wire_seen = false;

/* control queue: the rings the device also sees (identity-mapped, VA == PA).
 * The MODERN transport takes three independent addresses, so three independent
 * objects are the natural fit. */
static vring_desc_t  s_desc[VRING_MAX_DEPTH] __attribute__((aligned(4096)));
static vring_avail_t s_avail                 __attribute__((aligned(4096)));
static vring_used_t  s_used                  __attribute__((aligned(4096)));
static vring_t       s_vq;

/* A LEGACY transport is handed ONE page frame number and derives the other two
 * addresses itself: avail immediately after desc[depth], used aligned up from
 * there to QueueAlign. The three statics above cannot express that -- their
 * addresses are whatever the linker chose -- so the legacy path places the same
 * three structures INSIDE this one aligned region at exactly the offsets the
 * device is going to compute. Nothing here is a second vring implementation:
 * vring_init() is handed interior pointers and owns the bookkeeping as before. */
static uint8_t s_ring[2u * VGPU_LEGACY_PAGE] __attribute__((aligned(4096)));

/* one request scratch (the largest command) + one response scratch */
static union { vgpu_hdr_t hdr; vgpu_create_2d_t c2d; vgpu_attach_backing_t ab;
               vgpu_set_scanout_t ss; vgpu_xfer_2d_t xf; vgpu_flush_t fl; }
    s_req __attribute__((aligned(64)));
static vgpu_resp_display_info_t s_resp __attribute__((aligned(64)));

/* ---- mmio helpers (portable: plain volatile loads/stores, no asm) ---- */
static inline void     mw(uint32_t off, uint32_t v) { *(volatile uint32_t *)(uintptr_t)(s_base + off) = v; }
static inline uint32_t mr(uint32_t off)             { return *(volatile uint32_t *)(uintptr_t)(s_base + off); }

static void zero(void *p, uint32_t n) {
    volatile uint8_t *b = (volatile uint8_t *)p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

/* uint32 decimal, formatted here rather than via uart_put_dec: arm32's main has
 * no put_dec at all, and a 64-bit divide by a variable lowers to __udivdi3,
 * which Makefile.riscv32 has no libgcc to supply. Same reasoning as
 * zxv_decl_gate.c. */
static void put_u32(uint32_t v) {
    char b[12]; uint32_t n = 0;
    if (v == 0) { uart_puts("0"); return; }
    while (v && n < 11u) { b[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    char o[12]; uint32_t k = 0;
    while (n) o[k++] = b[--n];
    o[k] = 0;
    uart_puts(o);
}

/* ---- one control-queue transaction: [req R] -> [resp W], polled ------------
 * Bounded, never blocking forever: a device that stops answering degrades to a
 * reported timeout, not a hung boot. Returns the response header `type`, or 0
 * if nothing came back. */
static uint32_t gpu_cmd(uint32_t rlen, uint32_t plen) {
    vring_buf_t bufs[2];
    bufs[0].addr = &s_req;  bufs[0].len = rlen; bufs[0].device_writable = false;
    bufs[1].addr = &s_resp; bufs[1].len = plen; bufs[1].device_writable = true;

    zero(&s_resp, plen);
    if (vring_add(&s_vq, bufs, 2) < 0) return 0;      /* ring exhausted */
    ZXV_DSB();                                        /* rings visible before notify */
    mw(VMMIO_QUEUE_NOTIFY, VGPU_CTRLQ);

    for (uint32_t spins = 0; spins < 200000000u; spins++) {
        ZXV_DSB();                                    /* forces a reload of used.idx */
        uint32_t len = 0;
        if (vring_get_used(&s_vq, &len) >= 0) {
            uint32_t istat = mr(VMMIO_INTERRUPT_STATUS);
            if (istat) mw(VMMIO_INTERRUPT_ACK, istat);
            return s_resp.hdr.type;
        }
    }
    uart_puts("  [virtio-gpu] control queue timeout\n");
    return 0;
}

/* ---- how deep a queue the DEVICE will give us (never a fixed value) ---- */
static uint32_t negotiate_depth(void) {
    uint32_t max = mr(VMMIO_QUEUE_NUM_MAX);
    if (max == 0) return 0;                           /* queue does not exist  */
    uint32_t depth = VGPU_QDEPTH;
    if (max < depth) depth = max;                     /* adapt: ask the device */
    if (depth < 2u) return 0;                         /* need a 2-desc chain   */
    return depth;
}

/* ---- LEGACY (v1) queue bring-up: one page frame number ---------------------
 * The device computes avail and used from the PFN, so the offsets are computed
 * here the SAME way -- from the SPEC size of the avail ring (4 + 2*depth + 2),
 * not from sizeof(vring_avail_t), which is deliberately oversized to a fixed
 * VRING_MAX_DEPTH. Using the struct size would have put our used ring on a
 * different page from the device's whenever the two rounded differently: a
 * silent, intermittent corruption instead of an error. The oversize is then
 * checked for FIT rather than assumed. */
static bool setup_ctrlq_legacy(void) {
    mw(VMMIO_QUEUE_SEL, VGPU_CTRLQ);
    if (mr(VMMIO_L_QUEUE_PFN) != 0) return false;     /* queue already in use  */
    uint32_t depth = negotiate_depth();
    if (depth == 0) return false;

    uint32_t avail_off  = depth * (uint32_t)sizeof(vring_desc_t);
    uint32_t avail_wire = 4u + 2u * depth + 2u;       /* flags,idx,ring[],event */
    uint32_t used_off   = (avail_off + avail_wire + (VGPU_LEGACY_ALIGN - 1u))
                          & ~(VGPU_LEGACY_ALIGN - 1u);
    /* our fixed-size structs must fit in the slots the wire layout leaves */
    if (avail_off + (uint32_t)sizeof(vring_avail_t) > used_off) return false;
    if (used_off  + (uint32_t)sizeof(vring_used_t)  > (uint32_t)sizeof(s_ring)) return false;

    zero(s_ring, (uint32_t)sizeof(s_ring));
    vring_desc_t  *d = (vring_desc_t  *)(void *)(s_ring);
    vring_avail_t *a = (vring_avail_t *)(void *)(s_ring + avail_off);
    vring_used_t  *u = (vring_used_t  *)(void *)(s_ring + used_off);
    if (!vring_init(&s_vq, d, a, u, (uint16_t)depth)) return false;

    mw(VMMIO_QUEUE_NUM, depth);
    mw(VMMIO_L_QUEUE_ALIGN, VGPU_LEGACY_ALIGN);
    ZXV_DSB();                                        /* rings built before published */
    mw(VMMIO_L_QUEUE_PFN, (uint32_t)((uintptr_t)s_ring >> VGPU_LEGACY_SHIFT));
    return true;
}

/* ---- MODERN (v2) queue bring-up (SELECT, size, publish addresses, READY) ---- */
static bool setup_ctrlq_modern(void) {
    mw(VMMIO_QUEUE_SEL, VGPU_CTRLQ);
    if (mr(VMMIO_QUEUE_READY) != 0) return false;
    uint32_t depth = negotiate_depth();
    if (depth == 0) return false;

    if (!vring_init(&s_vq, s_desc, &s_avail, &s_used, (uint16_t)depth)) return false;
    mw(VMMIO_QUEUE_NUM, depth);

    /* 64-bit addresses published as two 32-bit halves. The >> 32 is by a
     * CONSTANT, so it costs no libgcc helper on the ilp32 targets (where
     * uintptr_t is 32 bits and the high half is correctly 0). */
    uint64_t d = (uint64_t)(uintptr_t)s_desc;
    uint64_t a = (uint64_t)(uintptr_t)&s_avail;
    uint64_t u = (uint64_t)(uintptr_t)&s_used;
    mw(VMMIO_QUEUE_DESC_LOW,    (uint32_t)d);       mw(VMMIO_QUEUE_DESC_HIGH,   (uint32_t)(d >> 32));
    mw(VMMIO_QUEUE_DRIVER_LOW,  (uint32_t)a);       mw(VMMIO_QUEUE_DRIVER_HIGH, (uint32_t)(a >> 32));
    mw(VMMIO_QUEUE_DEVICE_LOW,  (uint32_t)u);       mw(VMMIO_QUEUE_DEVICE_HIGH, (uint32_t)(u >> 32));
    ZXV_DSB();
    mw(VMMIO_QUEUE_READY, 1);
    return true;
}

/* ---- GET_DISPLAY_INFO: the ONLY source of the resolution -------------------
 * Nothing in this driver contains a mode. We ask the device, take the first
 * ENABLED scanout, and refuse to proceed if it reports none -- a fabricated
 * fallback size is precisely the defect this project already carries elsewhere
 * (hc_project baking 1920x1080) and must not gain a second instance. */
static bool read_display_info(void) {
    zero(&s_req, sizeof(s_req));
    s_req.hdr.type = VGPU_CMD_GET_DISPLAY_INFO;
    uint32_t rt = gpu_cmd(sizeof(vgpu_hdr_t), (uint32_t)sizeof(s_resp));
    if (rt != VGPU_RESP_OK_DISPLAY_INFO) {
        /* decimal, not hex: put_u32 is the only formatter portable to all five
         * mains (arm32's has no put_dec), and printing a decimal under an "0x"
         * label would be a lie in the log. 4353 == 0x1101 == OK_DISPLAY_INFO. */
        uart_puts("  [virtio-gpu] GET_DISPLAY_INFO refused (resp type ");
        put_u32(rt); uart_puts(", wanted 4353)\n");
        s_state = VGPU_STATE_NO_DISPLAY_INFO;
        return false;
    }
    for (uint32_t i = 0; i < VGPU_MAX_SCANOUTS; i++) {
        if (!s_resp.pmodes[i].enabled) continue;
        if (!s_resp.pmodes[i].r.width || !s_resp.pmodes[i].r.height) continue;
        s_scanout_id = i;
        s_w = s_resp.pmodes[i].r.width;
        s_h = s_resp.pmodes[i].r.height;
        s_w_wire = s_resp.pmodes[i].r.width;   /* the witness, never rewritten */
        s_h_wire = s_resp.pmodes[i].r.height;
        s_wire_seen = true;
        s_have_info = true;
        return true;
    }
    /* The device ANSWERED and answered with nothing enabled. That is a present
     * device with no panel attached, not an absent device, and the two must not
     * share a log line. A 0x0 or all-disabled pmodes[] is recorded as its own
     * state and never falls through to the [SKIP] path. */
    uart_puts("  [virtio-gpu] device PRESENT and answered, but reports no enabled"
              " scanout — no display to drive\n");
    s_state = VGPU_STATE_NO_SCANOUT;
    return false;
}

bool virtio_gpu_display_size(uint32_t *w_out, uint32_t *h_out, uint32_t *sc_out) {
    if (!s_have_info) return false;
    if (w_out)  *w_out  = s_w;
    if (h_out)  *h_out  = s_h;
    if (sc_out) *sc_out = s_scanout_id;
    return true;
}

bool virtio_gpu_is_live(void) { return s_live; }

/* ---- publish a backing buffer as the active scanout ---- */
int virtio_gpu_set_scanout(volatile uint32_t *fb, uint32_t w, uint32_t h) {
    if (!s_live || !s_have_info) return -1;
    if (!fb || !w || !h) return -2;
    /* The geometry must be the one the DEVICE reported. Refused, not scaled:
     * composing for a screen you did not get is the bug display.h exists to
     * prevent, and silently scaling here would reintroduce it one layer down. */
    if (w != s_w || h != s_h) return -3;

    zero(&s_req, sizeof(s_req));
    s_req.c2d.hdr.type     = VGPU_CMD_RESOURCE_CREATE_2D;
    s_req.c2d.resource_id  = VGPU_RESOURCE_ID;
    s_req.c2d.format       = VGPU_FORMAT_B8G8R8X8_UNORM;
    s_req.c2d.width        = w;
    s_req.c2d.height       = h;
    if (gpu_cmd(sizeof(vgpu_create_2d_t), sizeof(vgpu_hdr_t)) != VGPU_RESP_OK_NODATA) {
        uart_puts("  [virtio-gpu] RESOURCE_CREATE_2D failed\n"); return -4;
    }

    zero(&s_req, sizeof(s_req));
    s_req.ab.hdr.type      = VGPU_CMD_RESOURCE_ATTACH_BACKING;
    s_req.ab.resource_id   = VGPU_RESOURCE_ID;
    s_req.ab.nr_entries    = 1;                       /* one contiguous region */
    s_req.ab.entry.addr    = (uint64_t)(uintptr_t)fb;
    s_req.ab.entry.length  = w * h * 4u;
    if (gpu_cmd(sizeof(vgpu_attach_backing_t), sizeof(vgpu_hdr_t)) != VGPU_RESP_OK_NODATA) {
        uart_puts("  [virtio-gpu] RESOURCE_ATTACH_BACKING failed\n"); return -5;
    }

    zero(&s_req, sizeof(s_req));
    s_req.ss.hdr.type      = VGPU_CMD_SET_SCANOUT;
    s_req.ss.r.x = 0; s_req.ss.r.y = 0; s_req.ss.r.width = w; s_req.ss.r.height = h;
    s_req.ss.scanout_id    = s_scanout_id;
    s_req.ss.resource_id   = VGPU_RESOURCE_ID;
    if (gpu_cmd(sizeof(vgpu_set_scanout_t), sizeof(vgpu_hdr_t)) != VGPU_RESP_OK_NODATA) {
        uart_puts("  [virtio-gpu] SET_SCANOUT failed\n"); return -6;
    }

    s_scanout_up = true;
    return 0;
}

int virtio_gpu_flush(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!s_scanout_up) return -1;
    if (!w || !h) return -2;
    if (x >= s_w || y >= s_h) return -2;
    if (x + w > s_w) w = s_w - x;                     /* clip to the real panel */
    if (y + h > s_h) h = s_h - y;

    zero(&s_req, sizeof(s_req));
    s_req.xf.hdr.type    = VGPU_CMD_TRANSFER_TO_HOST_2D;
    s_req.xf.r.x = x; s_req.xf.r.y = y; s_req.xf.r.width = w; s_req.xf.r.height = h;
    /* byte offset of (x,y) in the backing buffer; 32-bit multiply widened once,
     * so no 64-bit variable arithmetic on the ilp32 targets */
    s_req.xf.offset      = (uint64_t)((y * s_w + x) * 4u);
    s_req.xf.resource_id = VGPU_RESOURCE_ID;
    if (gpu_cmd(sizeof(vgpu_xfer_2d_t), sizeof(vgpu_hdr_t)) != VGPU_RESP_OK_NODATA) return -3;

    zero(&s_req, sizeof(s_req));
    s_req.fl.hdr.type    = VGPU_CMD_RESOURCE_FLUSH;
    s_req.fl.r.x = x; s_req.fl.r.y = y; s_req.fl.r.width = w; s_req.fl.r.height = h;
    s_req.fl.resource_id = VGPU_RESOURCE_ID;
    if (gpu_cmd(sizeof(vgpu_flush_t), sizeof(vgpu_hdr_t)) != VGPU_RESP_OK_NODATA) return -4;
    return 0;
}

int virtio_gpu_flush_all(void) { return virtio_gpu_flush(0, 0, s_w, s_h); }

/* ---- bring up ONE device at a given mmio base (an omni-bus cell) ----------
 * Fault-contained: does its own handshake and returns false on any failure
 * without ever trapping, so a bad GPU never stops the bus scan. */
static bool gpu_init_slot(uint64_t base) {
    if (s_live) return false;                         /* singleton scanout */
    s_base = base;

    /* Ask the slot what it is rather than trusting the caller: this entry point
     * is also the omni-bus vtable's init_slot, so the transport generation must
     * be established here, once, from the registers themselves. */
    uint32_t magic = mr(VMMIO_MAGIC);
    uint32_t did   = mr(VMMIO_DEVICE_ID);
    s_version      = mr(VMMIO_VERSION);
    if (magic != VMAGIC || did != VDEV_GPU) { s_base = 0; s_version = 0; return false; }
    s_legacy = (s_version == 1u);
    if (s_version != 1u && s_version != 2u) {
        uart_puts("  [virtio-gpu] device PRESENT on slot "); put_u32(s_slot);
        uart_puts(" but transport version "); put_u32(s_version);
        uart_puts(" is neither legacy (1) nor modern (2) — refusing to guess its"
                  " register map\n");
        s_state = VGPU_STATE_BAD_TRANSPORT; s_base = 0; return false;
    }

    /* handshake: reset -> ACK -> DRIVER -> features -> (FEATURES_OK) -> queue.
     * We accept NO optional feature -- explicitly NOT VIRTIO_GPU_F_VIRGL,
     * because this driver implements 2D only and claiming a feature we do not
     * implement is how a device gets handed 3D commands we cannot issue. The
     * one feature the MODERN path does negotiate, VIRTIO_F_VERSION_1, is the
     * transport contract itself and does not exist on a legacy slot: legacy has
     * 32 feature bits, no bit 32, and no FEATURES_OK round trip at all. */
    mw(VMMIO_STATUS, 0);
    mw(VMMIO_STATUS, VS_ACK);
    mw(VMMIO_STATUS, VS_ACK | VS_DRIVER);
    uint32_t status_ok = VS_ACK | VS_DRIVER;
    if (s_legacy) {
        mw(VMMIO_DEVICE_FEATURES_SEL, 0); (void)mr(VMMIO_DEVICE_FEATURES);
        mw(VMMIO_DRIVER_FEATURES_SEL, 0); mw(VMMIO_DRIVER_FEATURES, 0);
        /* every ring address is derived from a PAGE FRAME NUMBER, so the device
         * must be told the page size we are counting in before we hand one over */
        mw(VMMIO_L_GUEST_PAGE_SIZE, VGPU_LEGACY_PAGE);
    } else {
        mw(VMMIO_DRIVER_FEATURES_SEL, 0); mw(VMMIO_DRIVER_FEATURES, 0);
        mw(VMMIO_DRIVER_FEATURES_SEL, 1); mw(VMMIO_DRIVER_FEATURES, 1u << (VIRTIO_F_VERSION_1 - 32));
        mw(VMMIO_STATUS, VS_ACK | VS_DRIVER | VS_FEATURES_OK);
        if (!(mr(VMMIO_STATUS) & VS_FEATURES_OK)) {
            /* PRESENT AND REFUSING US. This used to return false in silence and
             * be reported one frame up as "no virtio-gpu device"; a device that
             * rejects our feature set is a different fact and now says so. */
            uart_puts("  [virtio-gpu] device PRESENT on slot "); put_u32(s_slot);
            uart_puts(" but REFUSED our feature set (VIRTIO_F_VERSION_1)"
                      " — present, not usable\n");
            s_state = VGPU_STATE_FEATURES_FAILED;
            mw(VMMIO_STATUS, VS_FAILED); s_base = 0; return false;
        }
        status_ok |= VS_FEATURES_OK;
    }

    if (!(s_legacy ? setup_ctrlq_legacy() : setup_ctrlq_modern())) {
        uart_puts("  [virtio-gpu] device PRESENT on slot "); put_u32(s_slot);
        uart_puts(" but its control queue would not come up (transport v");
        put_u32(s_version); uart_puts(") — present, not usable\n");
        s_state = VGPU_STATE_QUEUE_FAILED;
        mw(VMMIO_STATUS, VS_FAILED); s_base = 0; return false;
    }
    mw(VMMIO_STATUS, status_ok | VS_DRIVER_OK);

    s_live = true;                                    /* queue live: cmds legal */
    if (!read_display_info()) {                       /* it set the reason itself */
        s_live = false; mw(VMMIO_STATUS, VS_FAILED); s_base = 0; return false;
    }

    s_state = VGPU_STATE_LIVE;
    uart_puts("  [DRIVER ONLINE] virtio-gpu 2D — device reports ");
    put_u32(s_w); uart_puts("x"); put_u32(s_h);
    uart_puts(" on scanout "); put_u32(s_scanout_id);
    uart_puts(" (read from GET_DISPLAY_INFO, not assumed); mmio slot ");
    put_u32(s_slot); uart_puts(", transport v"); put_u32(s_version);
    uart_puts(s_legacy ? " legacy\n" : " modern\n");
    return true;
}

/* This class driver as an omni-bus registry cell (no per-cycle poll: a 2D
 * scanout is driven by explicit flushes, not by servicing a queue). */
static const virtio_driver_t GPU_DRV = { VDEV_GPU, "virtio-gpu", gpu_init_slot, 0 };

/* ---- the transport scan ----------------------------------------------------
 * NOT virtio_mmio_find(). That helper filters on version==2, so a GPU sitting
 * on a legacy slot comes back as -1 -- indistinguishable, at the call site,
 * from no GPU at all. That indistinguishability WAS the bug (see the file
 * header: measured version 1, device id 16, plain `-device virtio-gpu-device`).
 * This scan classifies instead of filtering: it reports WHICH slot, and at
 * WHAT transport version, and lets the caller decide.
 *
 * Modern is preferred when both generations are visible -- the modern register
 * map is the one the rest of this tree speaks -- but a legacy slot is taken
 * rather than abandoned. Returns the slot index, or -1 when no slot anywhere
 * carries device id 16. */
static int scan_for_gpu(uint64_t *base_out, uint32_t *ver_out) {
    uint32_t n = virtio_mmio_slot_count();
    int      found = -1;
    uint64_t fbase = 0;
    uint32_t fver  = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t b = virtio_mmio_slot_base(i);
        if (b == 0) continue;
        if (*(volatile uint32_t *)(uintptr_t)(b + VMMIO_MAGIC)     != VMAGIC)   continue;
        if (*(volatile uint32_t *)(uintptr_t)(b + VMMIO_DEVICE_ID) != VDEV_GPU) continue;
        uint32_t v = *(volatile uint32_t *)(uintptr_t)(b + VMMIO_VERSION);
        if (found < 0 || (fver != 2u && v == 2u)) { found = (int)i; fbase = b; fver = v; }
        if (fver == 2u) break;                        /* modern found: done    */
    }
    if (found < 0) return -1;
    if (base_out) *base_out = fbase;
    if (ver_out)  *ver_out  = fver;
    return found;
}

bool virtio_gpu_probe(void) {
    if (s_live) return true;                          /* idempotent */
    virtio_register_driver(&GPU_DRV);                 /* claim id 16 on the bus */

    if (virtio_mmio_slot_count() == 0u) {
        /* No transport window at all: the board profile declared none. That is a
         * CONFIGURATION fact about the machine, not a statement about whether a
         * GPU exists, and it is worth keeping separate from both. */
        s_state = VGPU_STATE_NO_TRANSPORT;
        return false;
    }
    uint64_t base = 0; uint32_t ver = 0;
    int slot = scan_for_gpu(&base, &ver);
    if (slot < 0) { s_state = VGPU_STATE_ABSENT; return false; }  /* truly absent */

    s_slot = (uint32_t)slot;
    uart_puts("  [virtio-gpu] device id 16 found at mmio slot "); put_u32(s_slot);
    uart_puts(", transport v"); put_u32(ver);
    uart_puts(ver == 1u ? " (legacy)\n" : (ver == 2u ? " (modern)\n" : " (unknown)\n"));
    return gpu_init_slot(base);
}

virtio_gpu_state_t virtio_gpu_state(void) { return s_state; }

bool virtio_gpu_transport(uint32_t *slot_out, uint32_t *version_out) {
    if (s_state == VGPU_STATE_UNPROBED || s_state == VGPU_STATE_ABSENT ||
        s_state == VGPU_STATE_NO_TRANSPORT) return false;
    if (slot_out)    *slot_out    = s_slot;
    if (version_out) *version_out = s_version;
    return true;
}

const char *virtio_gpu_state_str(void) {
    switch (s_state) {
    case VGPU_STATE_UNPROBED:       return "not probed yet";
    case VGPU_STATE_ABSENT:         return "no device id 16 on any mmio slot";
    case VGPU_STATE_NO_TRANSPORT:   return "board profile declares no virtio-mmio window";
    case VGPU_STATE_BAD_TRANSPORT:  return "present; unknown transport version";
    case VGPU_STATE_FEATURES_FAILED:return "present; device refused our feature set";
    case VGPU_STATE_QUEUE_FAILED:   return "present; control queue would not come up";
    case VGPU_STATE_NO_DISPLAY_INFO:return "present; GET_DISPLAY_INFO refused";
    case VGPU_STATE_NO_SCANOUT:     return "present; answered, but no enabled scanout";
    case VGPU_STATE_LIVE:           return "live";
    }
    return "unknown";
}

/* ---- self-check ----------------------------------------------------------
 * Two kinds of check, deliberately separated. The wire-format ones run
 * everywhere and need no device. The geometry one only runs when a device is
 * live, and it is the check that matters here: it asserts the reported size is
 * NOT one of the constants a hardcoding bug would have produced. Returns passes;
 * *n_out receives the number that RAN, so "all passed" and "nothing ran" stay
 * distinguishable -- a gate that certifies a subsystem with zero checks has
 * burned this project before. */
uint32_t virtio_gpu_selfcheck(uint32_t *n_out) {
    uint32_t n = 0, ok = 0;

    /* Wire-format invariants. These are also _Static_assert-ed at the top; here
     * they are re-stated at runtime so the count is non-zero on a machine with
     * no GPU at all, and a report of "0/0" can only ever mean the self-check
     * itself was not reached. */
    { uint32_t sz = (uint32_t)sizeof(vgpu_hdr_t);
      n++; if (sz == 24u) ok++; }                                /* ctrl_hdr ABI  */
    { uint32_t sz = (uint32_t)sizeof(vgpu_mem_entry_t);
      n++; if (sz == 16u) ok++; }                                /* mem_entry ABI */
    { uint32_t off = (uint32_t)__builtin_offsetof(vgpu_set_scanout_t, scanout_id);
      n++; if (off == 40u) ok++; }                               /* rect placement */
    { uint32_t fmt = VGPU_FORMAT_B8G8R8X8_UNORM;
      n++; if (fmt == 2u) ok++; }                                /* XRGB8888 map  */

    /* State consistency: live implies a base was found and a mode was read. */
    n++; if (s_live == (s_base != 0)) ok++;
    n++; if (!s_live || s_have_info) ok++;
    /* THE PRESENT-vs-ABSENT CHECK. The reported reason must agree with reality:
     * VGPU_STATE_LIVE exactly when the driver is live, and the two "nothing was
     * there" states never claimed while it is. This is the invariant whose
     * violation was the original defect, restated as something that can fail. */
    n++; if ((s_state == VGPU_STATE_LIVE) == s_live) ok++;
    n++; if (!s_live || (s_state != VGPU_STATE_ABSENT &&
                         s_state != VGPU_STATE_NO_TRANSPORT)) ok++;

    if (s_live) {
        /* The transport generation was READ, and the legacy flag derives from
         * it rather than from a build-time assumption. */
        n++; if (s_version == 1u || s_version == 2u) ok++;
        n++; if (s_legacy == (s_version == 1u)) ok++;
        /* On legacy the device DERIVES the used ring from the page frame number
         * we published; if our used pointer is not on a QueueAlign boundary
         * inside that region, we and the device disagree about where it is. */
        n++; if (!s_legacy ||
                 (((uintptr_t)(void *)s_vq.used - (uintptr_t)(void *)s_ring)
                   % VGPU_LEGACY_ALIGN) == 0u) ok++;
        n++; if (s_w && s_h) ok++;                   /* non-degenerate geometry  */
        /* THE ANTI-HARDCODE CHECK. s_w/s_h must still equal the untouched
         * snapshot taken straight off GET_DISPLAY_INFO. If any future edit ever
         * assigns them from a constant -- the hc_project defect, one layer down
         * -- these diverge and this check fails. */
        n++; if (s_wire_seen && s_w == s_w_wire && s_h == s_h_wire) ok++;
        /* The mode must be one we can actually back, or we must not have
         * claimed a scanout. Never "clamped and carried on". */
        n++; if (!s_scanout_up ||
                 (uint64_t)s_w * s_h <= (uint64_t)VGPU_MAX_W * VGPU_MAX_H) ok++;
    }
    if (n_out) *n_out = n;
    return ok;
}

/* ---- DECLARATION -----------------------------------------------------------
 * The bring-up is the ONLY thing that reaches this driver in this build: adding
 * a call site in an arch main was out of scope (another workflow owns those
 * files), and a declaration keeps the module in the image without one -- the
 * bring-up is reached through a relocation from the retained section pointer,
 * which is exactly why zxv_decl.h says a declared module needs no GC root.
 *
 * REQUIRES virtio_bus_ready: the transport window must have been declared by
 * the board profile before a device on it may be touched. Nothing more --
 * virtio-gpu is independent of ramfb, of display mode negotiation, and of the
 * compositor.
 *
 * IT RETURNS 0 WHEN NO GPU IS PRESENT, AND THAT IS THE POINT. A missing device
 * is the ordinary case on a machine booted without -device virtio-gpu-device;
 * reporting it as a bring-up FAILURE would turn "the user did not ask for a
 * GPU" into a gate problem and train everyone to ignore the count. It fails
 * only when a device IS present and will not come up -- a real fault.
 *
 * That distinction used to be made by the WRONG TEST. It read probe()'s bool,
 * which is false for both cases, so "present and broken" silently took the
 * "absent" exit and the gate never saw it. It now reads virtio_gpu_state(),
 * which carries the reason: only ABSENT and NO_TRANSPORT may print [SKIP] and
 * return 0; every other non-live state is a present device and returns 1. */
#include "zxv_decl.h"

static int virtio_gpu_bringup(void) {
    if (virtio_gpu_probe()) {
        /* Prove the whole 2D path end to end, at the size the DEVICE gave us.
         * Refused rather than clamped if it exceeds the static budget, with
         * both numbers printed, so an unbacked mode is a stated fact. */
        if ((uint64_t)s_w * s_h > (uint64_t)VGPU_MAX_W * VGPU_MAX_H) {
            uart_puts("  [virtio-gpu] device mode "); put_u32(s_w); uart_puts("x"); put_u32(s_h);
            uart_puts(" exceeds the static scanout budget "); put_u32(VGPU_MAX_W);
            uart_puts("x"); put_u32(VGPU_MAX_H);
            uart_puts(" — REFUSED, not clamped (rebuild with -DVGPU_MAX_W/-DVGPU_MAX_H)\n");
            return 0;                                 /* honest, and not a fault */
        }
        static uint32_t s_fb[VGPU_MAX_W * VGPU_MAX_H] __attribute__((aligned(64)));
        /* A pattern that is obviously ours and obviously derived from the READ
         * geometry: a horizontal/vertical ramp with a framed border. Integer
         * only; the divides are by 32-bit variables, which is legal on every
         * target (only 64-bit variable divides lack a libgcc helper here). */
        for (uint32_t y = 0; y < s_h; y++) {
            uint32_t row = y * s_w;
            uint32_t g = (y * 255u) / s_h;
            for (uint32_t x = 0; x < s_w; x++) {
                uint32_t r = (x * 255u) / s_w;
                uint32_t edge = (x < 4u || y < 4u || x + 4u >= s_w || y + 4u >= s_h);
                s_fb[row + x] = edge ? 0x00FFFFFFu
                                     : ((r << 16) | (g << 8) | 0x40u);
            }
        }
        int rc = virtio_gpu_set_scanout(s_fb, s_w, s_h);
        if (rc != 0) {
            uart_puts("  [virtio-gpu] set_scanout failed\n");
            return 1;                                 /* device present, broken */
        }
        if (virtio_gpu_flush_all() != 0) {
            uart_puts("  [virtio-gpu] flush failed\n");
            return 1;
        }
        uint32_t ran = 0, pass = virtio_gpu_selfcheck(&ran);
        uart_puts("  [virtio-gpu] scanout live + flushed; selfcheck ");
        put_u32(pass); uart_puts("/"); put_u32(ran); uart_puts("\n");
        return (ran && pass == ran) ? 0 : 1;
    }
    /* DEGRADE, or FAIL -- and the two are told apart by the recorded REASON,
     * never by the fact that probe() returned false. Only a scan that found no
     * device id 16 anywhere (or no transport window to scan) may print [SKIP];
     * every other outcome means a device IS there and could not be driven, and
     * that is a fault, reported as one, with the reason on the line. */
    if (s_state == VGPU_STATE_ABSENT || s_state == VGPU_STATE_NO_TRANSPORT) {
        uart_puts("  [SKIP] no virtio-gpu device — display stays on ramfb"
                  " (add -device virtio-gpu-device)\n");
        return 0;
    }
    uart_puts("  [virtio-gpu] DEVICE PRESENT BUT NOT USABLE: ");
    uart_puts(virtio_gpu_state_str());
    uart_puts(" — display stays on ramfb\n");
    return 1;
}

ZXV_DECLARE(virtio_gpu,
    ZXV_PROVIDES(virtio_gpu_probed),
    ZXV_REQUIRES(virtio_bus_ready),
    ZXV_BRINGUP(virtio_gpu_bringup));
