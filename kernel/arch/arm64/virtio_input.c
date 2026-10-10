/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* virtio_input.c — virtio-input over virtio-mmio (VIRTIO 1.0), polled.
 *
 * ONE native driver for the virtio-input CONTRACT gives ZXV a mouse, tablet,
 * and keyboard on every hypervisor — QEMU/KVM, cloud, and anything that speaks
 * virtio. This is "crack the interface once, support every conforming device":
 * a new virtio pointer/keyboard needs no new code. Mirrors virtio_blk.c's
 * transport discipline (identity map => VA==PA; clean before handoff, invalidate
 * after; static page-aligned rings). Polled, no IRQ, so it composes untouched.
 */
#include <stdint.h>
#include <stdbool.h>
#include "board_profile.h"
#include "virtio_bus.h"   /* omni-driver engine: shared scan + class registry */

extern void uart_puts(const char *s);
extern void uart_put_dec(uint64_t v);

/* ---- virtio-mmio registers (same map virtio_blk.c uses) ---- */
#define VMMIO_MAGIC             0x000
#define VMMIO_VERSION           0x004
#define VMMIO_DEVICE_ID         0x008
#define VMMIO_DEVICE_FEATURES   0x010
#define VMMIO_DEVICE_FEATURES_SEL 0x014
#define VMMIO_DRIVER_FEATURES   0x020
#define VMMIO_DRIVER_FEATURES_SEL 0x024
#define VMMIO_QUEUE_SEL         0x030
#define VMMIO_QUEUE_NUM_MAX     0x034
#define VMMIO_QUEUE_NUM         0x038
#define VMMIO_QUEUE_READY       0x044
#define VMMIO_QUEUE_NOTIFY      0x050
#define VMMIO_INTERRUPT_STATUS  0x060
#define VMMIO_INTERRUPT_ACK     0x064
#define VMMIO_STATUS            0x070
#define VMMIO_QUEUE_DESC_LOW    0x080
#define VMMIO_QUEUE_DESC_HIGH   0x084
#define VMMIO_QUEUE_DRIVER_LOW  0x090
#define VMMIO_QUEUE_DRIVER_HIGH 0x094
#define VMMIO_QUEUE_DEVICE_LOW  0x0a0
#define VMMIO_QUEUE_DEVICE_HIGH 0x0a4

#define VS_ACK 1
#define VS_DRIVER 2
#define VS_DRIVER_OK 4
#define VS_FEATURES_OK 8
#define VS_FAILED 128
#define VIRTIO_F_VERSION_1 32
#define VMAGIC 0x74726976u
#define VDEV_INPUT 18

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

/* Linux evdev event codes (the virtio-input event stream IS evdev) */
#define EV_SYN 0
#define EV_KEY 1
#define EV_REL 2
#define EV_ABS 3
#define REL_X 0
#define REL_Y 1
#define ABS_X 0
#define ABS_Y 1
#define BTN_LEFT   0x110
#define BTN_RIGHT  0x111
#define BTN_MIDDLE 0x112

#define QDEPTH   16u
#define VIN_MAX  4u     /* keyboard + tablet + mouse, comfortably */

struct vdesc { uint64_t addr; uint32_t len; uint16_t flags; uint16_t next; };
struct vavail { uint16_t flags; uint16_t idx; uint16_t ring[QDEPTH]; };
struct vused_elem { uint32_t id; uint32_t len; };
struct vused { uint16_t flags; uint16_t idx; struct vused_elem ring[QDEPTH]; };
struct vin_event { uint16_t type; uint16_t code; uint32_t value; };

/* one ring set per input device */
struct vin_dev {
    volatile uint64_t mmio;
    struct vdesc  desc[QDEPTH]  __attribute__((aligned(4096)));
    struct vavail avail         __attribute__((aligned(4096)));
    struct vused  used          __attribute__((aligned(4096)));
    struct vin_event ev[QDEPTH] __attribute__((aligned(64)));
    uint16_t last_used;
};
static struct vin_dev s_dev[VIN_MAX];
static uint32_t s_ndev = 0;

/* shared pointer/keyboard state the desktop reads */
static int32_t s_cx = 640, s_cy = 360, s_w = 1280, s_h = 720;
static uint32_t s_btn = 0;              /* bit0 = left, bit1 = right, bit2 = mid */
static uint32_t s_moves = 0, s_clicks = 0;

/* a small ring of typed key-down keycodes the shell drains each frame */
#define KEYQ_N 64u
static uint16_t s_keyq[KEYQ_N];
static uint32_t s_khead = 0, s_ktail = 0;

static inline void mw(struct vin_dev *d, uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(d->mmio + off) = v;
}
static inline uint32_t mr(struct vin_dev *d, uint32_t off) {
    return *(volatile uint32_t *)(d->mmio + off);
}
static inline void clean(const void *p, uint32_t len) {
    uint64_t a = (uint64_t)p & ~63ULL, e = (uint64_t)p + len;
    for (; a < e; a += 64) __asm__ __volatile__("dc cvac, %0" :: "r"(a) : "memory");
    __asm__ __volatile__("dsb sy" ::: "memory");
}
static inline void invalidate(const void *p, uint32_t len) {
    uint64_t a = (uint64_t)p & ~63ULL, e = (uint64_t)p + len;
    __asm__ __volatile__("dsb sy" ::: "memory");
    for (; a < e; a += 64) __asm__ __volatile__("dc ivac, %0" :: "r"(a) : "memory");
    __asm__ __volatile__("dsb sy" ::: "memory");
}

/* bring up one virtio-input device: negotiate, set up the event queue (0), and
 * hand ALL QDEPTH event buffers to the device so it can fill them. */
static bool vin_bringup(struct vin_dev *d) {
    mw(d, VMMIO_STATUS, 0);
    mw(d, VMMIO_STATUS, VS_ACK);
    mw(d, VMMIO_STATUS, VS_ACK | VS_DRIVER);
    mw(d, VMMIO_DRIVER_FEATURES_SEL, 0); mw(d, VMMIO_DRIVER_FEATURES, 0);
    mw(d, VMMIO_DRIVER_FEATURES_SEL, 1);
    mw(d, VMMIO_DRIVER_FEATURES, 1u << (VIRTIO_F_VERSION_1 - 32));
    mw(d, VMMIO_STATUS, VS_ACK | VS_DRIVER | VS_FEATURES_OK);
    if (!(mr(d, VMMIO_STATUS) & VS_FEATURES_OK)) { mw(d, VMMIO_STATUS, VS_FAILED); return false; }

    mw(d, VMMIO_QUEUE_SEL, 0);
    if (mr(d, VMMIO_QUEUE_READY) != 0) return false;
    if (mr(d, VMMIO_QUEUE_NUM_MAX) < QDEPTH) return false;
    mw(d, VMMIO_QUEUE_NUM, QDEPTH);

    /* every descriptor is device-WRITABLE and points at its event slot */
    for (uint32_t i = 0; i < QDEPTH; i++) {
        d->desc[i].addr = (uint64_t)&d->ev[i];
        d->desc[i].len = sizeof(struct vin_event);
        d->desc[i].flags = VRING_DESC_F_WRITE;
        d->desc[i].next = 0;
        d->avail.ring[i] = (uint16_t)i;
    }
    d->avail.flags = 0;
    d->avail.idx = (uint16_t)QDEPTH;    /* all buffers offered up front */
    d->used.idx = 0;
    d->last_used = 0;

    mw(d, VMMIO_QUEUE_DESC_LOW,    (uint32_t)((uint64_t)d->desc));
    mw(d, VMMIO_QUEUE_DESC_HIGH,   (uint32_t)((uint64_t)d->desc >> 32));
    mw(d, VMMIO_QUEUE_DRIVER_LOW,  (uint32_t)((uint64_t)&d->avail));
    mw(d, VMMIO_QUEUE_DRIVER_HIGH, (uint32_t)((uint64_t)&d->avail >> 32));
    mw(d, VMMIO_QUEUE_DEVICE_LOW,  (uint32_t)((uint64_t)&d->used));
    mw(d, VMMIO_QUEUE_DEVICE_HIGH, (uint32_t)((uint64_t)&d->used >> 32));
    clean(d->desc, sizeof(d->desc));
    clean(&d->avail, sizeof(d->avail));
    mw(d, VMMIO_QUEUE_READY, 1);
    mw(d, VMMIO_STATUS, VS_ACK | VS_DRIVER | VS_FEATURES_OK | VS_DRIVER_OK);
    mw(d, VMMIO_QUEUE_NOTIFY, 0);       /* tell the device buffers are ready */
    return true;
}

/* Bring up ONE virtio-input device at the given base (an omni-bus registry cell). */
static bool input_init_slot(uint64_t base) {
    if (s_ndev >= VIN_MAX) return false;
    struct vin_dev *d = &s_dev[s_ndev];
    d->mmio = base;
    if (!vin_bringup(d)) { uart_puts("  [virtio-input] bringup FAILED\n"); return false; }
    s_ndev++;
    return true;
}
void virtio_input_poll(void);   /* forward decl for the cell vtable */
static const virtio_driver_t INPUT_DRV = { VDEV_INPUT, "virtio-input", input_init_slot, virtio_input_poll };

/* Scan the shared virtio-mmio bus for EVERY virtio-input device and bring each up. */
bool virtio_input_probe(void) {
    s_ndev = 0;
    uint64_t base; uint32_t from = 0; int slot;
    while (s_ndev < VIN_MAX && (slot = virtio_mmio_find(VDEV_INPUT, from, &base)) >= 0) {
        from = (uint32_t)slot + 1;
        if (input_init_slot(base)) { uart_puts("  [virtio-input] brought up slot "); uart_put_dec((uint64_t)slot); uart_puts("\n"); }
    }
    if (s_ndev == 0) return false;
    virtio_register_driver(&INPUT_DRV);
    return true;
}

void virtio_input_set_bounds(int32_t w, int32_t h) {
    if (w > 0) s_w = w;
    if (h > 0) s_h = h;
    if (s_cx >= s_w) s_cx = s_w - 1;
    if (s_cy >= s_h) s_cy = s_h - 1;
}

static void apply(const struct vin_event *e) {
    switch (e->type) {
    case EV_REL:
        if (e->code == REL_X) s_cx += (int32_t)e->value, s_moves++;
        else if (e->code == REL_Y) s_cy += (int32_t)e->value, s_moves++;
        break;
    case EV_ABS:
        if (e->code == ABS_X) s_cx = (int32_t)((uint64_t)e->value * (uint32_t)(s_w - 1) / 32767u), s_moves++;
        else if (e->code == ABS_Y) s_cy = (int32_t)((uint64_t)e->value * (uint32_t)(s_h - 1) / 32767u), s_moves++;
        break;
    case EV_KEY:
        if (e->code == BTN_LEFT)   { if (e->value) { s_btn |= 1u; s_clicks++; } else s_btn &= ~1u; }
        else if (e->code == BTN_RIGHT)  { if (e->value) s_btn |= 2u; else s_btn &= ~2u; }
        else if (e->code == BTN_MIDDLE) { if (e->value) s_btn |= 4u; else s_btn &= ~4u; }
        else if (e->value) {                       /* keyboard key-down -> queue */
            uint32_t nt = (s_ktail + 1u) % KEYQ_N;
            if (nt != s_khead) { s_keyq[s_ktail] = e->code; s_ktail = nt; }
        }
        break;
    default: break;   /* EV_SYN etc. */
    }
    if (s_cx < 0) s_cx = 0; if (s_cx >= s_w) s_cx = s_w - 1;
    if (s_cy < 0) s_cy = 0; if (s_cy >= s_h) s_cy = s_h - 1;
}

/* Drain every device's used ring, decode events, and re-offer the buffers. */
void virtio_input_poll(void) {
    for (uint32_t n = 0; n < s_ndev; n++) {
        struct vin_dev *d = &s_dev[n];
        invalidate(&d->used, sizeof(d->used));
        while (d->last_used != d->used.idx) {
            uint16_t slot = d->last_used % QDEPTH;
            uint32_t id = d->used.ring[slot].id;
            if (id < QDEPTH) {
                invalidate(&d->ev[id], sizeof(struct vin_event));
                apply(&d->ev[id]);
                /* re-offer this buffer to the device */
                uint16_t a = d->avail.idx % QDEPTH;
                d->avail.ring[a] = (uint16_t)id;
                clean(&d->avail, sizeof(d->avail));
                d->avail.idx++;
                clean(&d->avail, sizeof(d->avail));
            }
            d->last_used++;
        }
        mw(d, VMMIO_QUEUE_NOTIFY, 0);
    }
}

void virtio_input_get(int32_t *x, int32_t *y, uint32_t *buttons) {
    if (x) *x = s_cx;
    if (y) *y = s_cy;
    if (buttons) *buttons = s_btn;
}
/* pop one queued keyboard keycode (Linux evdev code), or -1 if none */
int32_t virtio_input_pop_key(void) {
    if (s_khead == s_ktail) return -1;
    uint16_t k = s_keyq[s_khead];
    s_khead = (s_khead + 1u) % KEYQ_N;
    return (int32_t)k;
}
uint32_t virtio_input_device_count(void) { return s_ndev; }
uint32_t virtio_input_event_count(void)  { return s_moves + s_clicks; }

/* Left-button PRESSES since the last call (delta of the monotonic press counter).
 * Lets the compositor catch fast taps that begin and end between two of its
 * ~16.7Hz button-level samples, which the level alone (virtio_input_get) drops. */
uint32_t virtio_input_pop_clicks(void) {
    static uint32_t s_last = 0;
    uint32_t now = s_clicks, delta = now - s_last;
    s_last = now;
    return delta;
}
