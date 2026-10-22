/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vscsi.c — virtio-scsi over the PCI transport (VIRTIO 1.1, section 5.6),
 * polled, one command at a time on the first request queue.
 *
 * The disk controller of KVM clouds that present SCSI disks (Contabo among
 * them): without it the disc boots from firmware but cannot see itself. The
 * bind scans targets 0..7 (or max_target, if lower) at LUN 0 and registers
 * every direct-access disk it finds, each as its own hw_blk_t sharing the
 * controller's queue and buffers:
 *   INQUIRY                 peripheral type 0 (disk), qualifier 0
 *   TEST UNIT READY         until the reset's unit attention is cleared
 *   READ CAPACITY (10)      or (16) when the disk is past 2^32 blocks
 *   READ / WRITE (10)       or (16) for blocks past 2^32
 * Each command is a request header, then the data (device-readable for a
 * write), then the response, then the data (device-writable for a read), in
 * the order the transport requires: everything the device reads before
 * everything it writes. Data goes through a 64 KB bounce buffer, so callers'
 * buffers need no alignment.
 *
 * Verdicts: HW_T one or more disks registered; HW_N no disk on the bus;
 * HW_F a command timed out or the transport failed; HW_B the controller
 * contradicts itself (a configuration with no request queue, or a CDB or
 * sense size it will not take while saying it is virtio-scsi). A disk whose
 * block size is not a power of two from 512 to 4096 is reported and skipped. */
#include "virtio_pci.h"

#define Q_REQUEST 2u /* control 0, event 1, the first request queue 2 */

#define CDB_SIZE   32u
#define SENSE_SIZE 96u
#define REQ_LEN    (8u + 8u + 3u + CDB_SIZE)             /* lun, tag, attr/prio/crn, cdb */
#define RESP_LEN   (4u + 4u + 2u + 1u + 1u + SENSE_SIZE) /* sense_len resid qual status resp */

#define S_OK         0u
#define S_BAD_TARGET 3u

#define ST_GOOD  0x00u
#define ST_CHECK 0x02u

#define SK_NOT_READY  0x2u
#define SK_UNIT_ATTN  0x6u
#define SCAN_TARGETS  8u
#define MAX_DISKS     4u
#define BOUNCE        65536u
#define TIMEOUT       5000000u /* 5 s per command */
#define READY_RETRIES 8u

/* cmd() results */
#define C_GOOD  0
#define C_NODEV 1  /* bad target: nothing there */
#define C_CHECK 2  /* CHECK CONDITION; the sense key is in s->sense_key */
#define C_FAIL  -1 /* timeout, transport failure, or a status other than the above */

struct vscsi;

typedef struct vsd {
    struct vscsi *s;
    uint8_t target;
    hw_blk_t blk;
} vsd_t;

typedef struct vscsi {
    vpci_t v;
    uint8_t *req, *resp, *buf;
    uint64_t tag;
    uint32_t max_bytes; /* bytes one command may move: bounce and max_sectors */
    uint8_t sense_key;
    vsd_t disk[MAX_DISKS];
    uint32_t ndisk;
} vscsi_t;

const hw_id_t vscsi_ids[] = {{VPCI_VENDOR, 0x1004}, {VPCI_VENDOR, 0x1048}, {0, 0}};

static uint32_t g_vscsi_n;

static void be32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t) (v >> 24);
    b[1] = (uint8_t) (v >> 16);
    b[2] = (uint8_t) (v >> 8);
    b[3] = (uint8_t) v;
}

static uint32_t get_be32(const uint8_t *b)
{
    return ((uint32_t) b[0] << 24) | ((uint32_t) b[1] << 16) | ((uint32_t) b[2] << 8) | b[3];
}

static uint32_t get_le32(const uint8_t *b)
{
    return (uint32_t) b[0] | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16) |
           ((uint32_t) b[3] << 24);
}

/* One command to (target, LUN 0). `data` is s->buf, `len` bytes, read by the
 * device when to_dev, written by it otherwise; len 0 = no data. */
static int cmd(vscsi_t *s, uint8_t target, const uint8_t *cdb, uint32_t cdb_len, uint32_t len,
               int to_dev)
{
    hw_memset(s->req, 0, REQ_LEN);
    s->req[0] = 1;
    s->req[1] = target;
    s->req[2] = 0x40; /* LUN 0, flat addressing */
    s->tag++;
    for (uint32_t i = 0; i < 8; i++) s->req[8 + i] = (uint8_t) (s->tag >> (8 * i));
    hw_memcpy(s->req + 19, cdb, cdb_len);
    hw_memset(s->resp, 0xFF, RESP_LEN);
    vring_buf_t b[3];
    uint16_t n = 0;
    b[n++] = (vring_buf_t){s->req, REQ_LEN, false};
    if (len && to_dev) b[n++] = (vring_buf_t){s->buf, len, false};
    b[n++] = (vring_buf_t){s->resp, RESP_LEN, true};
    if (len && !to_dev) b[n++] = (vring_buf_t){s->buf, len, true};
    if (vring_add(&s->v.q[0].vr, b, n) < 0) return C_FAIL;
    vpci_kick(&s->v, 0);
    uint32_t used;
    if (vpci_wait(&s->v, 0, &used, TIMEOUT) < 0) return C_FAIL;
    uint8_t response = s->resp[11], status = s->resp[10];
    if (response == S_BAD_TARGET) return C_NODEV;
    if (response != S_OK) return C_FAIL;
    if (status == ST_GOOD) return get_le32(s->resp + 4) == 0 ? C_GOOD : C_FAIL; /* resid */
    if (status != ST_CHECK) return C_FAIL;
    uint32_t sl = get_le32(s->resp);
    const uint8_t *sense = s->resp + 12;
    s->sense_key = 0;
    if (sl >= 3 && (sense[0] & 0x7E) == 0x70) s->sense_key = sense[2] & 0xF; /* fixed */
    if (sl >= 2 && (sense[0] & 0x7E) == 0x72) s->sense_key = sense[1] & 0xF; /* descriptor */
    return C_CHECK;
}

/* A command retried while the disk reports unit attention or becoming ready. */
static int cmd_retry(vscsi_t *s, uint8_t target, const uint8_t *cdb, uint32_t cdb_len, uint32_t len,
                     int to_dev)
{
    int r = C_FAIL;
    for (uint32_t i = 0; i < READY_RETRIES; i++) {
        r = cmd(s, target, cdb, cdb_len, len, to_dev);
        if (r != C_CHECK || (s->sense_key != SK_UNIT_ATTN && s->sense_key != SK_NOT_READY))
            return r;
        s->v.p->delay_us(s->v.p->ctx, 1000);
    }
    return r;
}

static int vsd_rw(hw_blk_t *hb, uint64_t lba, uint32_t n, void *ubuf, int write)
{
    vsd_t *d = (vsd_t *) hb->drv;
    vscsi_t *s = d->s;
    if (lba >= hb->blocks || n > hb->blocks - lba) return -1;
    uint32_t per = s->max_bytes / hb->block_size;
    uint8_t *u = (uint8_t *) ubuf;
    while (n) {
        uint32_t k = n < per ? n : per, bytes = k * hb->block_size;
        uint8_t cdb[16];
        hw_memset(cdb, 0, sizeof cdb);
        uint32_t cl;
        if (lba + k - 1 > 0xFFFFFFFFull) {
            cdb[0] = write ? 0x8A : 0x88;
            be32(cdb + 2, (uint32_t) (lba >> 32));
            be32(cdb + 6, (uint32_t) lba);
            be32(cdb + 10, k);
            cl = 16;
        } else {
            cdb[0] = write ? 0x2A : 0x28;
            be32(cdb + 2, (uint32_t) lba);
            cdb[7] = (uint8_t) (k >> 8);
            cdb[8] = (uint8_t) k;
            cl = 10;
        }
        if (write) hw_memcpy(s->buf, u, bytes);
        if (cmd_retry(s, d->target, cdb, cl, bytes, write) != C_GOOD) return -1;
        if (!write) hw_memcpy(u, s->buf, bytes);
        u += bytes;
        lba += k;
        n -= k;
    }
    return 0;
}

static int vsd_read(hw_blk_t *hb, uint64_t lba, uint32_t n, void *buf)
{
    return vsd_rw(hb, lba, n, buf, 0);
}

static int vsd_write(hw_blk_t *hb, uint64_t lba, uint32_t n, const void *buf)
{
    return vsd_rw(hb, lba, n, (void *) buf, 1);
}

/* Size of the disk at target t: 0 = found (blocks, block size written),
 * C_NODEV / C_CHECK = none usable, C_FAIL = the controller failed. */
static int probe_disk(vscsi_t *s, uint8_t t, uint64_t *blocks, uint32_t *bs)
{
    uint8_t cdb[16];
    hw_memset(cdb, 0, sizeof cdb);
    cdb[0] = 0x12; /* INQUIRY, 36 bytes */
    cdb[4] = 36;
    int r = cmd_retry(s, t, cdb, 6, 36, 0);
    if (r != C_GOOD) return r;
    if (s->buf[0] != 0x00) return C_NODEV; /* qualifier 0, type 0: a disk, attached */
    hw_memset(cdb, 0, sizeof cdb);
    r = cmd_retry(s, t, cdb, 6, 0, 0); /* TEST UNIT READY */
    if (r != C_GOOD) return r;
    hw_memset(cdb, 0, sizeof cdb);
    cdb[0] = 0x25; /* READ CAPACITY (10) */
    r = cmd_retry(s, t, cdb, 10, 8, 0);
    if (r != C_GOOD) return r;
    uint64_t last = get_be32(s->buf);
    *bs = get_be32(s->buf + 4);
    if (last == 0xFFFFFFFFull) {
        hw_memset(cdb, 0, sizeof cdb);
        cdb[0] = 0x9E; /* SERVICE ACTION IN: READ CAPACITY (16) */
        cdb[1] = 0x10;
        cdb[13] = 32;
        r = cmd_retry(s, t, cdb, 16, 32, 0);
        if (r != C_GOOD) return r;
        last = ((uint64_t) get_be32(s->buf) << 32) | get_be32(s->buf + 4);
        *bs = get_be32(s->buf + 8);
        if (last == ~0ull) return C_CHECK;
    }
    *blocks = last + 1;
    return C_GOOD;
}

static void disk_name(char *name, uint32_t n)
{
    hw_memcpy(name, "vsd", 3);
    name[3] = (char) ('0' + n % 10);
    name[4] = 0;
}

hw_verdict_t vscsi_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r,
                        hw_line_t *note)
{
    vscsi_t *s = (vscsi_t *) p->dma_alloc(p->ctx, sizeof(vscsi_t), 64, 0);
    if (!s) {
        hw_line_s(note, "out of DMA memory");
        return HW_F;
    }
    if (vpci_attach(&s->v, p, d, note)) return HW_N;
    if (vpci_negotiate(&s->v, 0, note)) return HW_F;
    uint8_t c[36];
    vpci_cfg_read(&s->v, 0, c, sizeof c);
    uint32_t nq = get_le32(c), max_sectors = get_le32(c + 8);
    uint32_t max_target = (uint32_t) c[30] | ((uint32_t) c[31] << 8);
    if (nq == 0) {
        hw_line_s(note, "a SCSI controller with no request queue");
        vpci_reset(&s->v);
        return HW_B;
    }
    if (get_le32(c + 20) != SENSE_SIZE || get_le32(c + 24) != CDB_SIZE) {
        /* writable in the device configuration; the defaults are these */
        uint8_t w[8] = {0};
        for (uint32_t i = 0; i < 4; i++) {
            w[i] = (uint8_t) (SENSE_SIZE >> (8 * i));
            w[4 + i] = (uint8_t) (CDB_SIZE >> (8 * i));
        }
        for (uint32_t i = 0; i < 8; i++) hw_wr8(s->v.devcfg, 20 + i, w[i]);
        p->delay_us(p->ctx, 1);
        vpci_cfg_read(&s->v, 20, c + 20, 8);
        if (get_le32(c + 20) != SENSE_SIZE || get_le32(c + 24) != CDB_SIZE) {
            hw_line_s(note, "will not take a 32-byte CDB and 96-byte sense");
            vpci_reset(&s->v);
            return HW_B;
        }
    }
    s->req = (uint8_t *) p->dma_alloc(p->ctx, REQ_LEN, 16, 0);
    s->resp = (uint8_t *) p->dma_alloc(p->ctx, RESP_LEN, 16, 0);
    s->buf = (uint8_t *) p->dma_alloc(p->ctx, BOUNCE, 4096, 0);
    if (!s->req || !s->resp || !s->buf) {
        hw_line_s(note, "out of DMA memory");
        vpci_reset(&s->v);
        return HW_F;
    }
    if (vpci_queue(&s->v, 0, Q_REQUEST, note)) {
        vpci_reset(&s->v);
        return HW_F;
    }
    vpci_ready(&s->v);
    s->max_bytes = BOUNCE;
    if (max_sectors && max_sectors < BOUNCE / 512) s->max_bytes = max_sectors * 512;
    uint32_t scan = max_target + 1 < SCAN_TARGETS ? max_target + 1 : SCAN_TARGETS;
    uint32_t odd = 0;
    for (uint32_t t = 0; t < scan && s->ndisk < MAX_DISKS; t++) {
        uint64_t blocks = 0;
        uint32_t bs = 0;
        int pr = probe_disk(s, (uint8_t) t, &blocks, &bs);
        if (pr == C_FAIL) {
            hw_line_s(note, "a command to target ");
            hw_line_u(note, t);
            hw_line_s(note, " failed or timed out");
            vpci_reset(&s->v);
            return HW_F;
        }
        if (pr != C_GOOD) continue;
        if (bs < 512 || bs > 4096 || (bs & (bs - 1)) || s->max_bytes < bs) {
            odd++;
            continue;
        }
        vsd_t *k = &s->disk[s->ndisk];
        k->s = s;
        k->target = (uint8_t) t;
        disk_name(k->blk.name, g_vscsi_n);
        k->blk.block_size = bs;
        k->blk.blocks = blocks;
        k->blk.drv = k;
        k->blk.read = vsd_read;
        k->blk.write = vsd_write;
        if (hw_reg_blk(r, &k->blk) != 0) break;
        if (s->ndisk == 0) {
            hw_line_s(note, "virtio SCSI disk ");
        } else {
            hw_line_s(note, "; ");
        }
        hw_line_u(note, blocks);
        hw_line_s(note, " x ");
        hw_line_u(note, bs);
        hw_line_s(note, " B at target ");
        hw_line_u(note, t);
        hw_line_s(note, " as ");
        hw_line_s(note, k->blk.name);
        s->ndisk++;
        g_vscsi_n++;
    }
    if (s->ndisk == 0) {
        hw_line_s(note, odd ? "only disks with a block size it cannot use" : "no disk on the bus");
        return HW_N;
    }
    return HW_T;
}
