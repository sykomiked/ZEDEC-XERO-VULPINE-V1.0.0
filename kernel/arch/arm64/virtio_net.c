/* virtio_net.c — virtio-net over virtio-mmio (modern / VIRTIO 1.0).
 *
 * Thin MMIO glue over the host-tested split-virtqueue engine (vring). Two
 * queues: RX (0) pre-filled with buffers the device writes into, TX (1) we
 * fill and the device reads. Poll-driven, no interrupts. Mirrors the proven
 * virtio_blk handshake.
 */
#include "virtio_net.h"
#include "board_profile.h"
#include "../../src/virtio/vring.h"

void uart_puts(const char *s);

/* ---- virtio-mmio register offsets (same layout as virtio_blk) ---- */
#define VMMIO_MAGIC              0x000
#define VMMIO_VERSION            0x004
#define VMMIO_DEVICE_ID          0x008
#define VMMIO_DEVICE_FEATURES    0x010
#define VMMIO_DEVICE_FEATURES_SEL 0x014
#define VMMIO_DRIVER_FEATURES    0x020
#define VMMIO_DRIVER_FEATURES_SEL 0x024
#define VMMIO_QUEUE_SEL          0x030
#define VMMIO_QUEUE_NUM_MAX      0x034
#define VMMIO_QUEUE_NUM          0x038
#define VMMIO_QUEUE_READY        0x044
#define VMMIO_QUEUE_NOTIFY       0x050
#define VMMIO_STATUS             0x070
#define VMMIO_QUEUE_DESC_LOW     0x080
#define VMMIO_QUEUE_DESC_HIGH    0x084
#define VMMIO_QUEUE_DRIVER_LOW   0x090
#define VMMIO_QUEUE_DRIVER_HIGH  0x094
#define VMMIO_QUEUE_DEVICE_LOW   0x0a0
#define VMMIO_QUEUE_DEVICE_HIGH  0x0a4
#define VMMIO_CONFIG             0x100

#define VS_ACK 1
#define VS_DRIVER 2
#define VS_DRIVER_OK 4
#define VS_FEATURES_OK 8
#define VS_FAILED 128
#define VIRTIO_F_VERSION_1 32
#define VMAGIC        0x74726976u
#define VDEV_NET      1
#define VIRTIO_NET_F_MAC 5

#define QDEPTH 16u

/* virtio-net header (VIRTIO 1.0, no mrg_rxbuf => 10 bytes; QEMU modern uses
 * 12 with num_buffers present). We always allocate 12 and zero it. */
#define VNET_HDR_LEN 12u

struct vnet_hdr {
    uint8_t  flags;
    uint8_t  gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    uint16_t num_buffers;
} __attribute__((packed));

/* ---- per-queue shared rings (page-aligned, PA==VA) ---- */
static vring_desc_t  rx_desc[VRING_MAX_DEPTH] __attribute__((aligned(4096)));
static vring_avail_t rx_avail                 __attribute__((aligned(4096)));
static vring_used_t  rx_used                  __attribute__((aligned(4096)));
static vring_desc_t  tx_desc[VRING_MAX_DEPTH] __attribute__((aligned(4096)));
static vring_avail_t tx_avail                 __attribute__((aligned(4096)));
static vring_used_t  tx_used                  __attribute__((aligned(4096)));

/* RX buffers: header + frame, one per descriptor slot. TX uses a shared
 * scratch (single in-flight frame is fine for a polled v1 driver). */
static uint8_t rx_buf[QDEPTH][VNET_HDR_LEN + VNET_MAX_FRAME] __attribute__((aligned(64)));
static uint8_t tx_buf[VNET_HDR_LEN + VNET_MAX_FRAME]         __attribute__((aligned(64)));

static vring_t s_rx, s_tx;
static volatile uint64_t s_mmio = 0;
static uint8_t s_mac[VNET_MAC_LEN];
static bool s_up = false;

static inline void mmio_w32(uint32_t off, uint32_t v) { *(volatile uint32_t *)(s_mmio + off) = v; }
static inline uint32_t mmio_r32(uint32_t off) { return *(volatile uint32_t *)(s_mmio + off); }

static inline void clean(const void *p, uint32_t len) {
    uint64_t a = (uint64_t)p & ~63ULL, end = (uint64_t)p + len;
    for (; a < end; a += 64) __asm__ __volatile__("dc cvac, %0" :: "r"(a) : "memory");
    __asm__ __volatile__("dsb sy" ::: "memory");
}
static inline void invalidate(const void *p, uint32_t len) {
    uint64_t a = (uint64_t)p & ~63ULL, end = (uint64_t)p + len;
    __asm__ __volatile__("dsb sy" ::: "memory");
    for (; a < end; a += 64) __asm__ __volatile__("dc ivac, %0" :: "r"(a) : "memory");
    __asm__ __volatile__("dsb sy" ::: "memory");
}

static void setup_queue(uint32_t qsel, vring_desc_t *d, vring_avail_t *a, vring_used_t *u) {
    mmio_w32(VMMIO_QUEUE_SEL, qsel);
    mmio_w32(VMMIO_QUEUE_NUM, QDEPTH);
    mmio_w32(VMMIO_QUEUE_DESC_LOW,    (uint32_t)(uint64_t)d);
    mmio_w32(VMMIO_QUEUE_DESC_HIGH,   (uint32_t)((uint64_t)d >> 32));
    mmio_w32(VMMIO_QUEUE_DRIVER_LOW,  (uint32_t)(uint64_t)a);
    mmio_w32(VMMIO_QUEUE_DRIVER_HIGH, (uint32_t)((uint64_t)a >> 32));
    mmio_w32(VMMIO_QUEUE_DEVICE_LOW,  (uint32_t)(uint64_t)u);
    mmio_w32(VMMIO_QUEUE_DEVICE_HIGH, (uint32_t)((uint64_t)u >> 32));
    mmio_w32(VMMIO_QUEUE_READY, 1);
}

/* Post one RX buffer (device-writable: header + frame in one descriptor). */
static void rx_refill_one(uint32_t slot) {
    vring_buf_t b = { rx_buf[slot], VNET_HDR_LEN + VNET_MAX_FRAME, true };
    int32_t h = vring_add(&s_rx, &b, 1);
    if (h >= 0) {
        clean(&rx_avail, sizeof rx_avail);
        mmio_w32(VMMIO_QUEUE_NOTIFY, 0);   /* notify RX queue */
    }
}

bool virtio_net_init(void) {
    const board_profile_t *bp = board_get_profile();
    if (!bp->virtio_mmio_base) return false;

    for (uint32_t i = 0; i < bp->virtio_mmio_count; i++) {
        uint64_t base = bp->virtio_mmio_base + (uint64_t)i * 0x200;
        if (*(volatile uint32_t *)(base + VMMIO_MAGIC) != VMAGIC) continue;
        if (*(volatile uint32_t *)(base + VMMIO_VERSION) != 2) continue;
        if (*(volatile uint32_t *)(base + VMMIO_DEVICE_ID) != VDEV_NET) continue;
        s_mmio = base; break;
    }
    if (!s_mmio) return false;

    mmio_w32(VMMIO_STATUS, 0);
    mmio_w32(VMMIO_STATUS, VS_ACK);
    mmio_w32(VMMIO_STATUS, VS_ACK | VS_DRIVER);

    /* read device features (word 0) so we can accept MAC if offered */
    mmio_w32(VMMIO_DEVICE_FEATURES_SEL, 0);
    uint32_t feat0 = mmio_r32(VMMIO_DEVICE_FEATURES);
    uint32_t want0 = feat0 & (1u << VIRTIO_NET_F_MAC);
    mmio_w32(VMMIO_DRIVER_FEATURES_SEL, 0);
    mmio_w32(VMMIO_DRIVER_FEATURES, want0);
    mmio_w32(VMMIO_DRIVER_FEATURES_SEL, 1);
    mmio_w32(VMMIO_DRIVER_FEATURES, 1u << (VIRTIO_F_VERSION_1 - 32));

    mmio_w32(VMMIO_STATUS, VS_ACK | VS_DRIVER | VS_FEATURES_OK);
    if (!(mmio_r32(VMMIO_STATUS) & VS_FEATURES_OK)) {
        mmio_w32(VMMIO_STATUS, VS_FAILED); s_mmio = 0; return false;
    }

    uint32_t qmax = (mmio_w32(VMMIO_QUEUE_SEL, 0), mmio_r32(VMMIO_QUEUE_NUM_MAX));
    if (qmax < QDEPTH) { s_mmio = 0; return false; }

    if (!vring_init(&s_rx, rx_desc, &rx_avail, &rx_used, (uint16_t)QDEPTH) ||
        !vring_init(&s_tx, tx_desc, &tx_avail, &tx_used, (uint16_t)QDEPTH)) {
        s_mmio = 0; return false;
    }
    setup_queue(0, rx_desc, &rx_avail, &rx_used);
    setup_queue(1, tx_desc, &tx_avail, &tx_used);
    mmio_w32(VMMIO_STATUS, VS_ACK | VS_DRIVER | VS_FEATURES_OK | VS_DRIVER_OK);

    /* MAC from config space (offset 0, 6 bytes) if the feature was accepted */
    for (uint32_t i = 0; i < VNET_MAC_LEN; i++)
        s_mac[i] = want0 ? *(volatile uint8_t *)(s_mmio + VMMIO_CONFIG + i) : 0;

    /* pre-fill the RX ring so the device has somewhere to put frames */
    for (uint32_t i = 0; i < QDEPTH - 1u; i++) rx_refill_one(i);

    s_up = true;
    return true;
}

bool virtio_net_present(void) { return s_up; }
const uint8_t *virtio_net_mac(void) { return s_mac; }

int virtio_net_tx(const uint8_t *frame, uint32_t len) {
    if (!s_up) return -1;
    if (!frame || len == 0 || len > VNET_MAX_FRAME) return -2;

    struct vnet_hdr *h = (struct vnet_hdr *)tx_buf;
    for (uint32_t i = 0; i < VNET_HDR_LEN; i++) tx_buf[i] = 0;  /* zero header */
    h->num_buffers = 1;
    for (uint32_t i = 0; i < len; i++) tx_buf[VNET_HDR_LEN + i] = frame[i];

    vring_buf_t b = { tx_buf, VNET_HDR_LEN + len, false };   /* device reads it */
    clean(tx_buf, VNET_HDR_LEN + len);
    int32_t hd = vring_add(&s_tx, &b, 1);
    if (hd < 0) return -3;
    clean(&tx_avail, sizeof tx_avail);
    mmio_w32(VMMIO_QUEUE_NOTIFY, 1);                          /* notify TX queue */

    /* poll for completion (single in-flight) */
    for (uint32_t spin = 0; spin < 1000000u; spin++) {
        invalidate(&tx_used, sizeof tx_used);
        uint32_t l = 0;
        if (vring_get_used(&s_tx, &l) >= 0) return 0;
    }
    return -4;   /* timed out */
}

int virtio_net_rx_poll(uint8_t *out, uint32_t max) {
    if (!s_up) return -1;
    invalidate(&rx_used, sizeof rx_used);
    uint32_t used_len = 0;
    int32_t head = vring_get_used(&s_rx, &used_len);
    if (head < 0) return 0;                                    /* nothing yet */

    uint8_t *buf = rx_buf[head % QDEPTH];
    invalidate(buf, used_len);
    uint32_t frame_len = (used_len > VNET_HDR_LEN) ? used_len - VNET_HDR_LEN : 0;
    if (frame_len > max) frame_len = max;
    for (uint32_t i = 0; i < frame_len; i++) out[i] = buf[VNET_HDR_LEN + i];

    rx_refill_one((uint32_t)head % QDEPTH);                    /* recycle the buffer */
    return (int)frame_len;
}

bool virtio_net_selftest(void) {
    if (!s_up) return false;
    /* device up, both queues initialised, RX pre-filled with room to spare */
    return vring_num_free(&s_rx) < (uint16_t)QDEPTH &&      /* some RX posted */
           vring_num_free(&s_tx) == (uint16_t)QDEPTH;       /* TX idle */
}
