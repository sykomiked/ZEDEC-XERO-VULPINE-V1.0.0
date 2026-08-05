/* virtio_blk.c — virtio-blk over virtio-mmio (modern / VIRTIO 1.0)
 *
 * A minimal, polling (no-IRQ) virtio block driver for the QEMU virt
 * machine's virtio-mmio transport.  It gives ZXV real persistent
 * storage: a `-drive` image survives reboots, replacing the volatile
 * RAM disk that had no persistence.
 *
 * Design constraints that make this safe in a freestanding kernel:
 *  - Single virtqueue (queue 0), depth 8, polled — no interrupts, so
 *    it composes with the existing GICv3/timer setup untouched.
 *  - Physical address == virtual address (kernel identity map), so a
 *    ring buffer's VA is handed straight to the device as a PA.
 *  - Rings and bounce buffer live in a static, page-aligned, cache-
 *    line-padded arena.  Because guest RAM is mapped Normal-cacheable
 *    and the emulated device DMAs against physical memory, every
 *    driver→device handoff is preceded by a cache clean (dc cvac) and
 *    every device→driver result by an invalidate (dc ivac), with dsb
 *    barriers — the same discipline el0_userspace.c uses for user code
 *    pages.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV persistence slice)
 * License: SEL-3.3
 */
#include <stdint.h>
#include <stdbool.h>
#include "board_profile.h"
#include "../include/blockdev.h"

extern void uart_puts(const char *s);
extern void uart_put_hex(uint64_t val);
extern void uart_put_dec(uint64_t val);

/* ---- virtio-mmio register offsets (spec 4.2.2) ---- */
#define VMMIO_MAGIC             0x000   /* 'virt' = 0x74726976 */
#define VMMIO_VERSION           0x004   /* 2 = modern */
#define VMMIO_DEVICE_ID         0x008   /* 2 = block */
#define VMMIO_VENDOR_ID         0x00c
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
#define VMMIO_CONFIG            0x100   /* device-specific config space */

/* status bits */
#define VS_ACK          1
#define VS_DRIVER       2
#define VS_DRIVER_OK    4
#define VS_FEATURES_OK  8
#define VS_FAILED       128

/* feature bit 32 = VIRTIO_F_VERSION_1 (modern) */
#define VIRTIO_F_VERSION_1  32

#define VMAGIC          0x74726976u
#define VDEV_BLOCK      2

/* virtqueue descriptor flags */
#define VRING_DESC_F_NEXT   1
#define VRING_DESC_F_WRITE  2   /* device writes (i.e. read from disk) */

/* block request types */
#define VIRTIO_BLK_T_IN     0   /* read from disk into memory */
#define VIRTIO_BLK_T_OUT    1   /* write memory to disk */

#define QDEPTH  8

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};
struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[QDEPTH];
};
struct vring_used_elem {
    uint32_t id;
    uint32_t len;
};
struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[QDEPTH];
};

struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

/* Static, page-aligned ring arena (PA==VA). Kept well apart so the
 * descriptor table, avail and used rings never share a cache line. */
static struct vring_desc  s_desc[QDEPTH]  __attribute__((aligned(4096)));
static struct vring_avail s_avail         __attribute__((aligned(4096)));
static struct vring_used  s_used          __attribute__((aligned(4096)));
static struct virtio_blk_req s_hdr        __attribute__((aligned(64)));
static uint8_t s_status                   __attribute__((aligned(64)));
static uint8_t s_bounce[BLOCKDEV_SECTOR_SIZE] __attribute__((aligned(64)));

static volatile uint64_t s_mmio = 0;      /* selected device base, 0 = none */
static uint16_t s_last_used = 0;
static uint64_t s_capacity_sectors = 0;

static inline void mmio_w32(uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(s_mmio + off) = v;
}
static inline uint32_t mmio_r32(uint32_t off) {
    return *(volatile uint32_t *)(s_mmio + off);
}

static inline void clean(const void *p, uint32_t len) {
    uint64_t a = (uint64_t)p & ~63ULL;
    uint64_t end = (uint64_t)p + len;
    for (; a < end; a += 64)
        __asm__ __volatile__("dc cvac, %0" :: "r"(a) : "memory");
    __asm__ __volatile__("dsb sy" ::: "memory");
}
static inline void invalidate(const void *p, uint32_t len) {
    uint64_t a = (uint64_t)p & ~63ULL;
    uint64_t end = (uint64_t)p + len;
    __asm__ __volatile__("dsb sy" ::: "memory");
    for (; a < end; a += 64)
        __asm__ __volatile__("dc ivac, %0" :: "r"(a) : "memory");
    __asm__ __volatile__("dsb sy" ::: "memory");
}

/* Scan the virtio-mmio slots for a block device and bring it up. */
static bool virtio_blk_probe(void) {
    const board_profile_t *bp = board_get_profile();
    if (!bp->virtio_mmio_base) return false;

    for (uint32_t i = 0; i < bp->virtio_mmio_count; i++) {
        uint64_t base = bp->virtio_mmio_base + (uint64_t)i * 0x200;
        if (*(volatile uint32_t *)(base + VMMIO_MAGIC) != VMAGIC) continue;
        uint32_t ver = *(volatile uint32_t *)(base + VMMIO_VERSION);
        uint32_t dev = *(volatile uint32_t *)(base + VMMIO_DEVICE_ID);
        if (dev != VDEV_BLOCK) continue;
        if (ver != 2) {
            uart_puts("[virtio-blk] found legacy (v1) device — unsupported, skipping\n");
            continue;
        }
        s_mmio = base;
        break;
    }
    if (!s_mmio) return false;

    /* --- init handshake (spec 3.1.1) --- */
    mmio_w32(VMMIO_STATUS, 0);                       /* reset */
    mmio_w32(VMMIO_STATUS, VS_ACK);
    mmio_w32(VMMIO_STATUS, VS_ACK | VS_DRIVER);

    /* Negotiate: we require only VIRTIO_F_VERSION_1 (feature word 1). */
    mmio_w32(VMMIO_DRIVER_FEATURES_SEL, 0);
    mmio_w32(VMMIO_DRIVER_FEATURES, 0);
    mmio_w32(VMMIO_DRIVER_FEATURES_SEL, 1);
    mmio_w32(VMMIO_DRIVER_FEATURES, 1u << (VIRTIO_F_VERSION_1 - 32));

    mmio_w32(VMMIO_STATUS, VS_ACK | VS_DRIVER | VS_FEATURES_OK);
    if (!(mmio_r32(VMMIO_STATUS) & VS_FEATURES_OK)) {
        mmio_w32(VMMIO_STATUS, VS_FAILED);
        uart_puts("[virtio-blk] FEATURES_OK rejected\n");
        s_mmio = 0;
        return false;
    }

    /* --- set up queue 0 --- */
    mmio_w32(VMMIO_QUEUE_SEL, 0);
    if (mmio_r32(VMMIO_QUEUE_READY) != 0) {
        s_mmio = 0; return false;
    }
    uint32_t qmax = mmio_r32(VMMIO_QUEUE_NUM_MAX);
    if (qmax < QDEPTH) { s_mmio = 0; return false; }
    mmio_w32(VMMIO_QUEUE_NUM, QDEPTH);

    mmio_w32(VMMIO_QUEUE_DESC_LOW,    (uint32_t)((uint64_t)s_desc));
    mmio_w32(VMMIO_QUEUE_DESC_HIGH,   (uint32_t)((uint64_t)s_desc >> 32));
    mmio_w32(VMMIO_QUEUE_DRIVER_LOW,  (uint32_t)((uint64_t)&s_avail));
    mmio_w32(VMMIO_QUEUE_DRIVER_HIGH, (uint32_t)((uint64_t)&s_avail >> 32));
    mmio_w32(VMMIO_QUEUE_DEVICE_LOW,  (uint32_t)((uint64_t)&s_used));
    mmio_w32(VMMIO_QUEUE_DEVICE_HIGH, (uint32_t)((uint64_t)&s_used >> 32));
    mmio_w32(VMMIO_QUEUE_READY, 1);

    mmio_w32(VMMIO_STATUS, VS_ACK | VS_DRIVER | VS_FEATURES_OK | VS_DRIVER_OK);

    /* Capacity: config space offset 0 = capacity in 512-byte sectors (u64). */
    uint32_t cap_lo = *(volatile uint32_t *)(s_mmio + VMMIO_CONFIG + 0);
    uint32_t cap_hi = *(volatile uint32_t *)(s_mmio + VMMIO_CONFIG + 4);
    s_capacity_sectors = ((uint64_t)cap_hi << 32) | cap_lo;

    s_avail.idx = 0;
    s_used.idx = 0;
    s_last_used = 0;
    return true;
}

/* Submit one request (read or write of a single 512-byte sector) and
 * poll the used ring until it completes. Returns 0 on success. */
static int virtio_blk_rw(uint32_t lba, uint8_t *buf, bool write) {
    if (!s_mmio) return -1;

    s_hdr.type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    s_hdr.reserved = 0;
    s_hdr.sector = lba;   /* virtio-blk sectors are always 512 bytes */
    s_status = 0xff;

    if (write) {
        for (int i = 0; i < BLOCKDEV_SECTOR_SIZE; i++) s_bounce[i] = buf[i];
    }

    /* 3-descriptor chain: header (R) -> data (R for OUT / W for IN) -> status (W) */
    s_desc[0].addr = (uint64_t)&s_hdr;
    s_desc[0].len = sizeof(s_hdr);
    s_desc[0].flags = VRING_DESC_F_NEXT;
    s_desc[0].next = 1;

    s_desc[1].addr = (uint64_t)s_bounce;
    s_desc[1].len = BLOCKDEV_SECTOR_SIZE;
    s_desc[1].flags = VRING_DESC_F_NEXT | (write ? 0 : VRING_DESC_F_WRITE);
    s_desc[1].next = 2;

    s_desc[2].addr = (uint64_t)&s_status;
    s_desc[2].len = 1;
    s_desc[2].flags = VRING_DESC_F_WRITE;
    s_desc[2].next = 0;

    uint16_t avail_slot = s_avail.idx % QDEPTH;
    s_avail.ring[avail_slot] = 0;   /* head descriptor index */

    /* Publish everything the device will read BEFORE bumping avail.idx. */
    clean(&s_hdr, sizeof(s_hdr));
    clean(s_bounce, BLOCKDEV_SECTOR_SIZE);
    clean(&s_status, sizeof(s_status));
    clean(s_desc, sizeof(s_desc));
    __asm__ __volatile__("dsb sy" ::: "memory");
    s_avail.idx++;
    clean(&s_avail, sizeof(s_avail));

    mmio_w32(VMMIO_QUEUE_NOTIFY, 0);

    /* Poll the used ring for completion. Bounded spin so a broken
     * device can't hang the kernel forever. */
    uint64_t spins = 0;
    for (;;) {
        invalidate(&s_used, sizeof(s_used));
        if (s_used.idx != s_last_used) break;
        if (++spins > 100000000ULL) {
            uart_puts("[virtio-blk] I/O timeout\n");
            return -1;
        }
    }
    s_last_used = s_used.idx;

    /* Ack any device interrupt line the transport may have raised so
     * the status register stays clean even though we poll. */
    uint32_t istat = mmio_r32(VMMIO_INTERRUPT_STATUS);
    if (istat) mmio_w32(VMMIO_INTERRUPT_ACK, istat);

    invalidate(&s_status, sizeof(s_status));
    if (s_status != 0) {   /* 0 = VIRTIO_BLK_S_OK */
        uart_puts("[virtio-blk] request status=");
        uart_put_dec((uint64_t)s_status);
        uart_puts("\n");
        return -1;
    }

    if (!write) {
        invalidate(s_bounce, BLOCKDEV_SECTOR_SIZE);
        for (int i = 0; i < BLOCKDEV_SECTOR_SIZE; i++) buf[i] = s_bounce[i];
    }
    return 0;
}

/* ---- block_device_t callbacks ---- */
static int vblk_read(block_device_t *dev, uint32_t lba, uint8_t *buf) {
    (void)dev;
    return virtio_blk_rw(lba, buf, false);
}
static int vblk_write(block_device_t *dev, uint32_t lba, const uint8_t *buf) {
    (void)dev;
    return virtio_blk_rw(lba, (uint8_t *)buf, true);
}

/* Public: probe + populate a block_device_t. Returns true if a
 * persistent virtio-blk device was found and initialized. */
bool virtio_blk_init(block_device_t *dev) {
    if (!dev) return false;
    if (!virtio_blk_probe()) return false;

    dev->present = true;
    dev->total_sectors = (s_capacity_sectors > 0xffffffffULL)
        ? 0xffffffffu : (uint32_t)s_capacity_sectors;
    dev->driver_data = 0;
    dev->read_sector = vblk_read;
    dev->write_sector = vblk_write;

    /* model string: "virtio-blk" */
    const char *m = "virtio-blk";
    int i = 0;
    for (; m[i] && i < 40; i++) dev->model[i] = m[i];
    dev->model[i] = '\0';
    return true;
}

uint64_t virtio_blk_capacity_sectors(void) { return s_capacity_sectors; }
