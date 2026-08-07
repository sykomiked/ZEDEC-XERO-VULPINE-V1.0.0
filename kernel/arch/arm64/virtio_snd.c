/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* virtio_snd.c — virtio-sound (VIRTIO 1.0) output over virtio-mmio, polled.
 *
 * Mirrors virtio_input.c / virtio_blk.c transport (identity map => VA==PA; clean
 * before handoff, invalidate after; static page-aligned rings; bounded poll).
 * virtio-snd is MULTI-QUEUE: we bring up control(0) + tx(2). The #1 trap vs the
 * single-queue drivers: QUEUE_NOTIFY must carry the QUEUE INDEX (0 for control,
 * 2 for tx), where blk/input always notify 0. The chime is pure integer PCM.
 */
#include <stdint.h>
#include <stdbool.h>
#include "board_profile.h"
#include "virtio_snd.h"

extern void uart_puts(const char *s);
extern void uart_put_dec(uint64_t v);

/* ---- virtio-mmio registers (same map the other drivers use) ---- */
#define VMMIO_MAGIC               0x000
#define VMMIO_VERSION             0x004
#define VMMIO_DEVICE_ID           0x008
#define VMMIO_DRIVER_FEATURES     0x020
#define VMMIO_DRIVER_FEATURES_SEL 0x024
#define VMMIO_QUEUE_SEL           0x030
#define VMMIO_QUEUE_NUM_MAX       0x034
#define VMMIO_QUEUE_NUM           0x038
#define VMMIO_QUEUE_READY         0x044
#define VMMIO_QUEUE_NOTIFY        0x050
#define VMMIO_INTERRUPT_STATUS    0x060
#define VMMIO_INTERRUPT_ACK       0x064
#define VMMIO_STATUS              0x070
#define VMMIO_QUEUE_DESC_LOW      0x080
#define VMMIO_QUEUE_DESC_HIGH     0x084
#define VMMIO_QUEUE_DRIVER_LOW    0x090
#define VMMIO_QUEUE_DRIVER_HIGH   0x094
#define VMMIO_QUEUE_DEVICE_LOW    0x0a0
#define VMMIO_QUEUE_DEVICE_HIGH   0x0a4
#define VMMIO_CONFIG              0x100   /* device config space */

#define VS_ACK 1
#define VS_DRIVER 2
#define VS_DRIVER_OK 4
#define VS_FEATURES_OK 8
#define VS_FAILED 128
#define VIRTIO_F_VERSION_1 32
#define VMAGIC 0x74726976u
#define VDEV_SND 25

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2
#define QDEPTH 16u

/* ---- virtio-snd control request codes / status / enums ---- */
#define VSND_R_PCM_INFO        0x0100
#define VSND_R_PCM_SET_PARAMS  0x0101
#define VSND_R_PCM_PREPARE     0x0102
#define VSND_R_PCM_RELEASE     0x0103
#define VSND_R_PCM_START       0x0104
#define VSND_R_PCM_STOP        0x0105
#define VSND_S_OK              0x8000
#define VSND_PCM_FMT_S16       5
#define VSND_PCM_RATE_48000    7
#define VSND_D_OUTPUT          0

/* ---- split-virtqueue memory layout (identical to the other drivers) ---- */
struct vdesc { uint64_t addr; uint32_t len; uint16_t flags; uint16_t next; };
struct vavail { uint16_t flags; uint16_t idx; uint16_t ring[QDEPTH]; };
struct vused_elem { uint32_t id; uint32_t len; };
struct vused { uint16_t flags; uint16_t idx; struct vused_elem ring[QDEPTH]; };

struct vq {
    struct vdesc  desc[QDEPTH] __attribute__((aligned(4096)));
    struct vavail avail        __attribute__((aligned(4096)));
    struct vused  used         __attribute__((aligned(4096)));
    uint16_t last_used;
    uint32_t qidx;
};
static struct vq s_ctl;   /* control queue (index 0) */
static struct vq s_tx;    /* tx / pcm queue (index 2) */

static volatile uint64_t s_base = 0;   /* device mmio base (shared by all queues) */
static int      s_ready  = 0;
static uint32_t s_stream = 0;          /* chosen OUTPUT stream id */

/* ---- virtio-snd wire structs ---- */
struct snd_query_info { uint32_t code; uint32_t start_id; uint32_t count; uint32_t size; };
struct snd_pcm_info   { uint32_t hda_fn_nid; uint32_t features; uint64_t formats; uint64_t rates;
                        uint8_t direction; uint8_t channels_min; uint8_t channels_max; uint8_t pad[5]; };
struct snd_pcm_hdr    { uint32_t code; uint32_t stream_id; };
struct snd_set_params { uint32_t code; uint32_t stream_id; uint32_t buffer_bytes; uint32_t period_bytes;
                        uint32_t features; uint8_t channels; uint8_t format; uint8_t rate; uint8_t pad; };
struct snd_pcm_xfer   { uint32_t stream_id; };
struct snd_pcm_status { uint32_t status; uint32_t latency_bytes; };

/* control/tx scratch (static; cleaned before each handoff) */
static struct snd_query_info s_qi;
static struct snd_set_params s_sp;
static struct snd_pcm_hdr    s_ph;
static uint8_t               s_resp[1024];
static struct snd_pcm_xfer   s_xfer;
static struct snd_pcm_status s_xstat;

/* PCM: 0.5s stereo S16LE @ 48kHz = 24000 frames * 2ch * 2bytes = 96000 bytes */
#define SR            48000u
#define CHAN          2u
#define PERIOD_FRAMES 24000u
#define PERIOD_BYTES  (PERIOD_FRAMES * CHAN * 2u)
static int16_t s_pcm[PERIOD_FRAMES * CHAN] __attribute__((aligned(64)));

/* ---- mmio + cache helpers (same discipline as virtio_input.c) ---- */
static inline void     mw(uint32_t off, uint32_t v) { *(volatile uint32_t *)(s_base + off) = v; }
static inline uint32_t mr(uint32_t off)             { return *(volatile uint32_t *)(s_base + off); }
static inline uint32_t cfg32(uint32_t off)          { return *(volatile uint32_t *)(s_base + VMMIO_CONFIG + off); }
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

/* Bring up one queue (SELECT, size, publish ring addresses, READY). */
static bool setup_q(struct vq *q, uint32_t idx) {
    q->qidx = idx; q->last_used = 0;
    mw(VMMIO_QUEUE_SEL, idx);
    if (mr(VMMIO_QUEUE_READY) != 0) return false;
    if (mr(VMMIO_QUEUE_NUM_MAX) < QDEPTH) return false;
    mw(VMMIO_QUEUE_NUM, QDEPTH);
    q->avail.flags = 0; q->avail.idx = 0; q->used.idx = 0;
    mw(VMMIO_QUEUE_DESC_LOW,    (uint32_t)((uint64_t)q->desc));
    mw(VMMIO_QUEUE_DESC_HIGH,   (uint32_t)((uint64_t)q->desc >> 32));
    mw(VMMIO_QUEUE_DRIVER_LOW,  (uint32_t)((uint64_t)&q->avail));
    mw(VMMIO_QUEUE_DRIVER_HIGH, (uint32_t)((uint64_t)&q->avail >> 32));
    mw(VMMIO_QUEUE_DEVICE_LOW,  (uint32_t)((uint64_t)&q->used));
    mw(VMMIO_QUEUE_DEVICE_HIGH, (uint32_t)((uint64_t)&q->used >> 32));
    clean(q->desc, sizeof(q->desc));
    clean(&q->avail, sizeof(q->avail));
    mw(VMMIO_QUEUE_READY, 1);
    return true;
}

/* Submit a 2-descriptor control txn [req (R)] -> [resp (W)] and poll to
 * completion. Returns the response status code (first u32 of resp). */
static uint32_t ctl_txn(const void *req, uint32_t rlen, void *resp, uint32_t plen) {
    struct vq *q = &s_ctl;
    q->desc[0].addr = (uint64_t)req;  q->desc[0].len = rlen; q->desc[0].flags = VRING_DESC_F_NEXT;  q->desc[0].next = 1;
    q->desc[1].addr = (uint64_t)resp; q->desc[1].len = plen; q->desc[1].flags = VRING_DESC_F_WRITE; q->desc[1].next = 0;
    q->avail.ring[q->avail.idx % QDEPTH] = 0;      /* head descriptor */
    clean(req, rlen); clean(resp, plen); clean(q->desc, sizeof(q->desc));
    __asm__ __volatile__("dsb sy" ::: "memory");
    q->avail.idx++; clean(&q->avail, sizeof(q->avail));
    mw(VMMIO_QUEUE_NOTIFY, q->qidx);               /* control queue index = 0 */
    uint64_t spins = 0;
    for (;;) {
        invalidate(&q->used, sizeof(q->used));
        if (q->used.idx != q->last_used) break;
        if (++spins > 200000000ULL) { uart_puts("  [virtio-snd] ctl timeout\n"); return 0xFFFFFFFFu; }
    }
    q->last_used = q->used.idx;
    uint32_t istat = mr(VMMIO_INTERRUPT_STATUS); if (istat) mw(VMMIO_INTERRUPT_ACK, istat);
    invalidate(resp, plen);
    return *(volatile uint32_t *)resp;
}

/* Publish one PCM period on the tx queue (does NOT poll — the device only
 * consumes it after PCM_START). Chain: [xfer hdr (R)] -> [pcm (R)] -> [status (W)]. */
static void tx_submit(void) {
    struct vq *q = &s_tx;
    s_xfer.stream_id = s_stream;
    s_xstat.status = 0xffffffffu; s_xstat.latency_bytes = 0;
    q->desc[0].addr = (uint64_t)&s_xfer;  q->desc[0].len = sizeof(s_xfer);  q->desc[0].flags = VRING_DESC_F_NEXT;  q->desc[0].next = 1;
    q->desc[1].addr = (uint64_t)s_pcm;    q->desc[1].len = PERIOD_BYTES;    q->desc[1].flags = VRING_DESC_F_NEXT;  q->desc[1].next = 2;
    q->desc[2].addr = (uint64_t)&s_xstat; q->desc[2].len = sizeof(s_xstat); q->desc[2].flags = VRING_DESC_F_WRITE; q->desc[2].next = 0;
    q->avail.ring[q->avail.idx % QDEPTH] = 0;
    clean(&s_xfer, sizeof(s_xfer)); clean(s_pcm, PERIOD_BYTES);
    clean(&s_xstat, sizeof(s_xstat)); clean(q->desc, sizeof(q->desc));
    __asm__ __volatile__("dsb sy" ::: "memory");
    q->avail.idx++; clean(&q->avail, sizeof(q->avail));
    mw(VMMIO_QUEUE_NOTIFY, q->qidx);               /* tx queue index = 2 (THE gotcha) */
}

/* Wait (bounded) for the device to consume the queued period. */
static void tx_wait(void) {
    struct vq *q = &s_tx;
    uint64_t spins = 0;
    for (;;) {
        invalidate(&q->used, sizeof(q->used));
        if (q->used.idx != q->last_used) break;
        if (++spins > 20000000000ULL) { uart_puts("  [virtio-snd] tx timeout\n"); return; }
    }
    q->last_used = q->used.idx;
    uint32_t istat = mr(VMMIO_INTERRUPT_STATUS); if (istat) mw(VMMIO_INTERRUPT_ACK, istat);
}

/* Generate a pure-integer stereo S16LE rising major arpeggio (A4 C#5 E5 A5),
 * square waves, with short attack + tail fades to kill clicks. No float. */
static void gen_chime(void) {
    static const uint32_t notes[4] = { 440, 554, 659, 880 };
    const int32_t amp = 7000;
    const uint32_t per_note = PERIOD_FRAMES / 4u;
    uint32_t f = 0;
    for (int n = 0; n < 4; n++) {
        uint32_t half = SR / (2u * notes[n]); if (half == 0) half = 1;
        uint32_t cnt = 0; int32_t cur = amp;
        for (uint32_t i = 0; i < per_note && f < PERIOD_FRAMES; i++, f++) {
            s_pcm[f * 2 + 0] = (int16_t)cur;
            s_pcm[f * 2 + 1] = (int16_t)cur;
            if (++cnt >= half) { cnt = 0; cur = -cur; }
        }
    }
    while (f < PERIOD_FRAMES) { s_pcm[f*2] = 0; s_pcm[f*2+1] = 0; f++; }   /* pad tail */
    /* attack fade-in over the first 256 frames */
    for (uint32_t i = 0; i < 256u; i++) {
        s_pcm[i*2+0] = (int16_t)((int32_t)s_pcm[i*2+0] * (int32_t)i / 256);
        s_pcm[i*2+1] = (int16_t)((int32_t)s_pcm[i*2+1] * (int32_t)i / 256);
    }
    /* release fade-out over the final 3000 frames */
    const uint32_t fade = 3000u;
    for (uint32_t i = 0; i < fade; i++) {
        uint32_t idx = PERIOD_FRAMES - 1u - i;
        s_pcm[idx*2+0] = (int16_t)((int32_t)s_pcm[idx*2+0] * (int32_t)i / (int32_t)fade);
        s_pcm[idx*2+1] = (int16_t)((int32_t)s_pcm[idx*2+1] * (int32_t)i / (int32_t)fade);
    }
}

bool virtio_snd_init(void) {
    const board_profile_t *bp = board_get_profile();
    if (!bp->virtio_mmio_base) return false;
    for (uint32_t i = 0; i < bp->virtio_mmio_count; i++) {
        uint64_t base = bp->virtio_mmio_base + (uint64_t)i * 0x200;
        if (*(volatile uint32_t *)(base + VMMIO_MAGIC) != VMAGIC) continue;
        if (*(volatile uint32_t *)(base + VMMIO_DEVICE_ID) != VDEV_SND) continue;
        if (*(volatile uint32_t *)(base + VMMIO_VERSION) != 2) {
            uart_puts("  [virtio-snd] slot "); uart_put_dec(i);
            uart_puts(" is legacy (v1) — need -global virtio-mmio.force-legacy=false\n");
            continue;
        }
        s_base = base;

        /* handshake: reset -> ACK -> DRIVER -> features(VERSION_1) -> FEATURES_OK */
        mw(VMMIO_STATUS, 0);
        mw(VMMIO_STATUS, VS_ACK);
        mw(VMMIO_STATUS, VS_ACK | VS_DRIVER);
        mw(VMMIO_DRIVER_FEATURES_SEL, 0); mw(VMMIO_DRIVER_FEATURES, 0);
        mw(VMMIO_DRIVER_FEATURES_SEL, 1); mw(VMMIO_DRIVER_FEATURES, 1u << (VIRTIO_F_VERSION_1 - 32));
        mw(VMMIO_STATUS, VS_ACK | VS_DRIVER | VS_FEATURES_OK);
        if (!(mr(VMMIO_STATUS) & VS_FEATURES_OK)) { mw(VMMIO_STATUS, VS_FAILED); s_base = 0; continue; }

        if (!setup_q(&s_ctl, 0) || !setup_q(&s_tx, 2)) {
            uart_puts("  [virtio-snd] queue setup failed\n"); s_base = 0; continue;
        }
        mw(VMMIO_STATUS, VS_ACK | VS_DRIVER | VS_FEATURES_OK | VS_DRIVER_OK);

        uint32_t streams = cfg32(4);                 /* config: {jacks, streams, chmaps} */
        uint32_t count = streams > 8u ? 8u : (streams ? streams : 1u);

        /* find an OUTPUT PCM stream */
        s_qi.code = VSND_R_PCM_INFO; s_qi.start_id = 0; s_qi.count = count; s_qi.size = sizeof(struct snd_pcm_info);
        uint32_t st = ctl_txn(&s_qi, sizeof(s_qi), s_resp,
                              sizeof(uint32_t) + count * sizeof(struct snd_pcm_info));
        int chosen = -1;
        if (st == VSND_S_OK) {
            struct snd_pcm_info *inf = (struct snd_pcm_info *)(s_resp + sizeof(uint32_t));
            for (uint32_t k = 0; k < count; k++) if (inf[k].direction == VSND_D_OUTPUT) { chosen = (int)k; break; }
        }
        s_stream = (uint32_t)(chosen < 0 ? 0 : chosen);

        /* SET_PARAMS: stereo S16 @ 48kHz, one-period buffer */
        s_sp.code = VSND_R_PCM_SET_PARAMS; s_sp.stream_id = s_stream;
        s_sp.buffer_bytes = PERIOD_BYTES; s_sp.period_bytes = PERIOD_BYTES; s_sp.features = 0;
        s_sp.channels = CHAN; s_sp.format = VSND_PCM_FMT_S16; s_sp.rate = VSND_PCM_RATE_48000; s_sp.pad = 0;
        if (ctl_txn(&s_sp, sizeof(s_sp), s_resp, sizeof(uint32_t)) != VSND_S_OK) {
            uart_puts("  [virtio-snd] SET_PARAMS failed\n"); s_base = 0; return false;
        }
        s_ph.code = VSND_R_PCM_PREPARE; s_ph.stream_id = s_stream;
        if (ctl_txn(&s_ph, sizeof(s_ph), s_resp, sizeof(uint32_t)) != VSND_S_OK) {
            uart_puts("  [virtio-snd] PREPARE failed\n"); s_base = 0; return false;
        }
        s_ready = 1;
        uart_puts("  [DRIVER ONLINE] virtio-snd — PCM output stream ready (id ");
        uart_put_dec((uint64_t)s_stream); uart_puts(")\n");
        return true;
    }
    return false;
}

void virtio_snd_chime(void) {
    if (!s_ready) return;
    gen_chime();
    tx_submit();                                          /* queue the period BEFORE start */
    s_ph.code = VSND_R_PCM_START; s_ph.stream_id = s_stream;
    ctl_txn(&s_ph, sizeof(s_ph), s_resp, sizeof(uint32_t));
    tx_wait();                                            /* block until the period drains */
    s_ph.code = VSND_R_PCM_STOP; s_ph.stream_id = s_stream;
    ctl_txn(&s_ph, sizeof(s_ph), s_resp, sizeof(uint32_t));
    uart_puts("  [virtio-snd] boot chime played\n");
}

bool virtio_snd_ready(void) { return s_ready != 0; }
