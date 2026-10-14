/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* usbmsc.c — USB mass storage, Bulk-Only Transport with SCSI commands
 * (interface class 8, subclass 6, protocol 0x50), on the USB core in xhci.c.
 *
 * Each command is a 31-byte CBW on bulk OUT, an optional data stage, and a
 * 13-byte CSW on bulk IN (BOT 1.0 section 5). A stalled data stage is cleared
 * and the CSW still read; a stalled CSW read is retried once; a CSW that is
 * short, mis-tagged or reports a phase error triggers reset recovery (Bulk-
 * Only Mass Storage Reset, then clear halt on both endpoints) and the command
 * is tried once more. Data moves through a 16 KB bounce buffer, so callers'
 * buffers need no alignment. LUN 0 only.
 *
 * Bring-up: INQUIRY, TEST UNIT READY (retried while the unit becomes ready),
 * READ CAPACITY(10), and (16) when the disk is too large for (10). */
#include "usb.h"

#define MSC_BUF   16384u
#define T_CMD_US  2000000u
#define T_DATA_US 5000000u
#define CBW_SIG   0x43425355u /* "USBC" */
#define CSW_SIG   0x53425355u /* "USBS" */

typedef struct msc {
    usb_dev_t *d;
    usb_ep_t *in, *out;
    uint8_t *buf; /* MSC_BUF data bounce */
    uint64_t buf_phys;
    uint8_t *cbw; /* CBW at 0, CSW at 64 */
    uint64_t cbw_phys;
    uint32_t tag;
    uint32_t resets; /* reset recoveries done (diagnostics) */
    uint8_t sense_key, asc, ascq;
    hw_blk_t blk;
} msc_t;

static uint32_t g_msc_n; /* usb0, usb1, ... across controllers */

static void reset_recovery(msc_t *m)
{
    m->resets++;
    usb_control(m->d, 0x21, 0xFF, 0, m->d->intf, 0, 0); /* Bulk-Only Mass Storage Reset */
    usb_clear_halt(m->d, m->in);
    usb_clear_halt(m->d, m->out);
}

/* One BOT command, len bytes of data through m->buf. Returns the CSW status
 * (0 passed, 1 failed) or -1 on a transport error (after reset recovery).
 * *moved is the data stage byte count. */
static int bot_once(msc_t *m, const uint8_t *cb, uint32_t cblen, int in, uint32_t len,
                    uint32_t *moved)
{
    usb_dev_t *d = m->d;
    uint8_t *w = m->cbw;
    uint32_t tag = ++m->tag;
    hw_memset(w, 0, 31);
    usb_put32(w, CBW_SIG);
    usb_put32(w + 4, tag);
    usb_put32(w + 8, len);
    w[12] = (uint8_t) ((in && len) ? 0x80 : 0);
    w[14] = (uint8_t) cblen;
    hw_memcpy(w + 15, cb, cblen);
    *moved = 0;
    if (usb_xfer(d, m->out, m->cbw_phys, 31, T_CMD_US) != 31) {
        reset_recovery(m);
        return -1;
    }
    if (len) {
        usb_ep_t *e = in ? m->in : m->out;
        int rc = usb_xfer(d, e, m->buf_phys, len, T_DATA_US);
        if (rc == USB_ESTALL) {
            if (usb_clear_halt(d, e) != 0) return -1;
        } else if (rc < 0) {
            reset_recovery(m);
            return -1;
        } else {
            *moved = (uint32_t) rc;
        }
    }
    uint8_t *s = m->cbw + 64;
    hw_memset(s, 0, 13);
    int rc = usb_xfer(d, m->in, m->cbw_phys + 64, 13, T_CMD_US);
    if (rc == USB_ESTALL && usb_clear_halt(d, m->in) == 0)
        rc = usb_xfer(d, m->in, m->cbw_phys + 64, 13, T_CMD_US);
    if (rc != 13 || usb_get32(s) != CSW_SIG || usb_get32(s + 4) != tag || s[12] > 1) {
        reset_recovery(m); /* short or invalid CSW, or phase error (status 2) */
        return -1;
    }
    return s[12];
}

static int bot(msc_t *m, const uint8_t *cb, uint32_t cblen, int in, uint32_t len, uint32_t *moved)
{
    int rc = bot_once(m, cb, cblen, in, len, moved);
    if (rc < 0 && !usb_dead(m->d)) rc = bot_once(m, cb, cblen, in, len, moved);
    return rc;
}

static void be32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t) (v >> 24);
    b[1] = (uint8_t) (v >> 16);
    b[2] = (uint8_t) (v >> 8);
    b[3] = (uint8_t) v;
}

static uint32_t rbe32(const uint8_t *b)
{
    return ((uint32_t) b[0] << 24) | ((uint32_t) b[1] << 16) | ((uint32_t) b[2] << 8) | b[3];
}

/* REQUEST SENSE into m->sense_key / m->asc; 0 = got it. */
static int sense(msc_t *m)
{
    uint8_t cb[6] = {0x03, 0, 0, 0, 18, 0};
    uint32_t got;
    m->sense_key = m->asc = m->ascq = 0;
    if (bot(m, cb, 6, 1, 18, &got) != 0 || got < 14) return -1;
    m->sense_key = m->buf[2] & 0xF;
    m->asc = m->buf[12];
    m->ascq = m->buf[13];
    return 0;
}

static int msc_rw(hw_blk_t *b, uint64_t lba, uint32_t count, uint8_t *u, int write)
{
    msc_t *m = (msc_t *) b->drv;
    uint32_t per = MSC_BUF / b->block_size;
    while (count) {
        uint32_t k = count < per ? count : per, bytes = k * b->block_size, got;
        uint8_t cb[16];
        hw_memset(cb, 0, sizeof cb);
        uint32_t cblen = 10;
        if (lba + k > 0xFFFFFFFFull) { /* READ(16) / WRITE(16) */
            cb[0] = write ? 0x8A : 0x88;
            be32(cb + 2, (uint32_t) (lba >> 32));
            be32(cb + 6, (uint32_t) lba);
            be32(cb + 10, k);
            cblen = 16;
        } else { /* READ(10) / WRITE(10) */
            cb[0] = write ? 0x2A : 0x28;
            be32(cb + 2, (uint32_t) lba);
            cb[7] = (uint8_t) (k >> 8);
            cb[8] = (uint8_t) k;
        }
        if (write) hw_memcpy(m->buf, u, bytes);
        if (bot(m, cb, cblen, !write, bytes, &got) != 0 || got != bytes) return -1;
        if (!write) hw_memcpy(u, m->buf, bytes);
        u += bytes;
        lba += k;
        count -= k;
    }
    return 0;
}

static int msc_read(hw_blk_t *b, uint64_t lba, uint32_t n, void *buf)
{
    if (lba >= b->blocks || n > b->blocks - lba) return -1;
    return msc_rw(b, lba, n, (uint8_t *) buf, 0);
}

static int msc_write(hw_blk_t *b, uint64_t lba, uint32_t n, const void *buf)
{
    if (lba >= b->blocks || n > b->blocks - lba) return -1;
    return msc_rw(b, lba, n, (uint8_t *) (uintptr_t) buf, 1);
}

static void note_text(hw_line_t *note, const uint8_t *s, uint32_t n)
{
    while (n && (s[n - 1] == ' ' || s[n - 1] == 0)) n--;
    uint32_t i = 0;
    while (i < n && s[i] == ' ') i++;
    for (; i < n; i++) hw_line_c(note, (s[i] >= 0x20 && s[i] < 0x7F) ? (char) s[i] : '?');
}

int usbmsc_attach(usb_dev_t *d, hw_registry_t *r, hw_line_t *note)
{
    const hw_plat_t *p = d->p;
    msc_t *m = (msc_t *) p->dma_alloc(p->ctx, sizeof(msc_t), 64, 0);
    char name[8] = {'u', 's', 'b', (char) ('0' + g_msc_n % 10), 0};
    hw_line_s(note, name);
    hw_line_s(note, " ");
    if (!m) {
        hw_line_s(note, "out of DMA memory");
        return -1;
    }
    m->d = d;
    m->in = usb_find_ep(d, 2, 0x80);
    m->out = usb_find_ep(d, 2, 0);
    if (!m->in || !m->out) {
        hw_line_s(note, "lacks bulk endpoints");
        return -1;
    }
    m->buf = (uint8_t *) p->dma_alloc(p->ctx, MSC_BUF, MSC_BUF, &m->buf_phys);
    m->cbw = (uint8_t *) p->dma_alloc(p->ctx, 128, 128, &m->cbw_phys);
    if (!m->buf || !m->cbw) {
        hw_line_s(note, "out of DMA memory");
        return -1;
    }
    uint32_t got;
    /* INQUIRY: vendor and product for the boot line */
    uint8_t inq[6] = {0x12, 0, 0, 0, 36, 0};
    if (bot(m, inq, 6, 1, 36, &got) != 0 || got < 32) {
        hw_line_s(note, "INQUIRY failed");
        return -1;
    }
    if ((m->buf[0] & 0x1F) != 0) {
        hw_line_s(note, "not a direct-access device");
        return 0;
    }
    hw_line_c(note, '(');
    note_text(note, m->buf + 16, 16);
    /* TEST UNIT READY until ready: a fresh disk reports unit attention a few
     * times; a spinning one says "becoming ready" (04/01) for seconds */
    uint8_t tur[6] = {0, 0, 0, 0, 0, 0};
    int ready = 0;
    for (int t = 0, tries = 8; t < tries && !ready; t++) {
        int rc = bot(m, tur, 6, 0, 0, &got);
        if (rc == 0) {
            ready = 1;
        } else {
            if (rc > 0) sense(m);
            if (rc > 0 && m->sense_key == 2 && m->asc == 0x3A) break; /* no medium */
            if (rc > 0 && m->sense_key == 2 && m->asc == 0x04 && m->ascq == 0x01) tries = 80;
            p->delay_us(p->ctx, 100000);
        }
    }
    if (!ready) {
        int nomedium = m->sense_key == 2 && m->asc == 0x3A;
        hw_line_s(note, nomedium ? ", no medium)" : ", not ready)");
        return nomedium ? 0 : -1;
    }
    uint8_t rc10[10] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    if (bot(m, rc10, 10, 1, 8, &got) != 0 || got < 8) {
        hw_line_s(note, ", READ CAPACITY failed)");
        return -1;
    }
    uint64_t last = rbe32(m->buf);
    uint32_t bs = rbe32(m->buf + 4);
    if (last == 0xFFFFFFFFu) { /* larger than 2 TB of 512 B blocks */
        uint8_t rc16[16] = {0x9E, 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 0, 0};
        if (bot(m, rc16, 16, 1, 32, &got) != 0 || got < 12) {
            hw_line_s(note, ", READ CAPACITY(16) failed)");
            return -1;
        }
        last = ((uint64_t) rbe32(m->buf) << 32) | rbe32(m->buf + 4);
        bs = rbe32(m->buf + 8);
    }
    if (bs < 512 || bs > 4096 || (bs & (bs - 1))) {
        hw_line_s(note, ", unsupported block size)");
        return -1;
    }
    hw_blk_t *b = &m->blk;
    hw_memcpy(b->name, name, 5);
    b->block_size = bs;
    b->blocks = last + 1;
    b->drv = m;
    b->read = msc_read;
    b->write = msc_write;
    hw_line_c(note, ' ');
    hw_line_u(note, b->blocks);
    hw_line_s(note, " x ");
    hw_line_u(note, bs);
    hw_line_s(note, " B)");
    if (hw_reg_blk(r, b) != 0) {
        hw_line_s(note, " registry full");
        return 0;
    }
    g_msc_n++;
    return 1;
}
