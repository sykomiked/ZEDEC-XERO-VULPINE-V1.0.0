/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwdrv_probe.c — the built-in driver table, bus probe, and boot self-tests.
 *
 * Boot lines (the QEMU driver check greps for these):
 *   [DRIVER ONLINE] <drv> <bdf> <vid>:<did> <note>     verdict T
 *   [DRIVER FAILED] <drv> <bdf> <vid>:<did> <note>     verdict F
 *   [DRIVER HELD]   <drv> <bdf> <vid>:<did> <note>     verdict B or N
 *   [SELFTEST OK|NONE|FAILED] <device> <what happened>
 * None of these is a fault marker: hardware that fails is skipped, the boot
 * goes on. */
#include "hwdrv.h"

const hw_driver_t hw_drivers[] = {
    {"e1000", 0x02, 0x00, 0xFF, e1000_ids, e1000_bind},
    {"igb", 0x02, 0x00, 0xFF, igb_ids, igb_bind},
    {"r8169", 0x02, 0x00, 0xFF, r8169_ids, r8169_bind},
    {"nvme", 0x01, 0x08, 0x02, 0, nvme_bind},
    {"ahci", 0x01, 0x06, 0x01, 0, ahci_bind},
    {"xhci", 0x0C, 0x03, 0x30, 0, xhci_bind},
    {"hda", 0x04, 0x03, 0xFF, 0, hda_bind},
    {"arbiter", 0x08, 0x80, 0x5A, 0, arbmbox_bind},
};
const uint32_t hw_n_drivers = sizeof hw_drivers / sizeof hw_drivers[0];

#define HW_MAX_FUNCS 64
static hw_pci_dev_t g_devs[HW_MAX_FUNCS];

static void dev_line(hw_line_t *l, const char *tag, const char *drv, const hw_pci_dev_t *d)
{
    hw_line_init(l);
    hw_line_s(l, tag);
    hw_line_s(l, drv);
    hw_line_c(l, ' ');
    hw_line_bdf(l, d->bdf);
    hw_line_c(l, ' ');
    hw_line_x(l, d->vendor, 4);
    hw_line_c(l, ':');
    hw_line_x(l, d->device, 4);
}

hw_probe_stats_t hw_probe_all(const hw_plat_t *p, hw_registry_t *r)
{
    hw_probe_stats_t st = {0, 0, 0, 0, 0};
    st.functions = hw_pci_scan(p, g_devs, HW_MAX_FUNCS);
    hw_line_t l;
    for (uint32_t i = 0; i < st.functions; i++) {
        const hw_pci_dev_t *d = &g_devs[i];
        const hw_driver_t *drv = hw_match(hw_drivers, hw_n_drivers, d);
        if (!drv) {
            st.unclaimed++;
            continue;
        }
        hw_line_t note;
        hw_line_init(&note);
        hw_verdict_t v = drv->bind(p, d, r, &note);
        const char *tag = v == HW_T   ? "  [DRIVER ONLINE] "
                          : v == HW_F ? "  [DRIVER FAILED] "
                                      : "  [DRIVER HELD] ";
        if (v == HW_T)
            st.bound++;
        else if (v == HW_F)
            st.failed++;
        else
            st.held++;
        dev_line(&l, tag, drv->name, d);
        if (v == HW_B) hw_line_s(&l, " contradiction:");
        if (note.n) {
            hw_line_c(&l, ' ');
            hw_line_s(&l, note.b);
        }
        p->log(l.b);
    }
    hw_line_init(&l);
    hw_line_s(&l, "  [OK] class drivers: ");
    hw_line_u(&l, st.functions);
    hw_line_s(&l, " PCI functions, ");
    hw_line_u(&l, st.bound);
    hw_line_s(&l, " bound, ");
    hw_line_u(&l, st.failed);
    hw_line_s(&l, " failed, ");
    hw_line_u(&l, st.held);
    hw_line_s(&l, " held, ");
    hw_line_u(&l, st.unclaimed);
    hw_line_s(&l, " without a class driver");
    p->log(l.b);
    return st;
}

/* ---- self-tests ------------------------------------------------------------ */
static void st_line(const hw_plat_t *p, const char *tag, const char *dev, const char *what,
                    const hw_line_t *extra)
{
    hw_line_t l;
    hw_line_init(&l);
    hw_line_s(&l, tag);
    hw_line_s(&l, dev);
    hw_line_c(&l, ' ');
    hw_line_s(&l, what);
    if (extra && extra->n) hw_line_s(&l, extra->b);
    p->log(l.b);
}

static uint32_t selftest_blk(const hw_plat_t *p, hw_blk_t *b)
{
    static uint8_t buf[4096];
    hw_line_t x;
    hw_line_init(&x);
    hw_line_u(&x, b->blocks);
    hw_line_s(&x, " blocks of ");
    hw_line_u(&x, b->block_size);
    if (b->block_size == 0 || b->block_size > sizeof buf || b->blocks < 2) {
        st_line(p, "  [SELFTEST NONE] ", b->name, "no readable media: ", &x);
        return 0;
    }
    if (b->read(b, 1, 1, buf) != 0) {
        st_line(p, "  [SELFTEST FAILED] ", b->name, "read LBA 1 failed; ", &x);
        return 0;
    }
    int gpt = hw_memeq(buf, "EFI PART", 8);
    st_line(p, "  [SELFTEST OK] ", b->name,
            gpt ? "read LBA 1: GPT header 'EFI PART'; " : "read LBA 1: no GPT header; ", &x);
    return 1;
}

/* DHCPDISCOVER as a broadcast: any DHCP server on the segment answers with an
 * offer (QEMU user networking, home routers, office networks), so it proves
 * transmit and receive end to end without knowing the gateway. */
static uint16_t ip_csum(const uint8_t *h, uint32_t n)
{
    uint32_t s = 0;
    for (uint32_t i = 0; i + 1 < n; i += 2) s += ((uint32_t) h[i] << 8) | h[i + 1];
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return (uint16_t) ~s;
}

static uint32_t build_discover(uint8_t *f, const uint8_t mac[6], uint32_t xid)
{
    hw_memset(f, 0, 342);
    hw_memset(f, 0xFF, 6); /* eth dst broadcast */
    hw_memcpy(f + 6, mac, 6);
    f[12] = 0x08; /* IPv4 */
    uint8_t *ip = f + 14;
    ip[0] = 0x45;
    uint32_t iplen = 20 + 8 + 300;
    ip[2] = (uint8_t) (iplen >> 8);
    ip[3] = (uint8_t) iplen;
    ip[8] = 64;
    ip[9] = 17; /* UDP */
    hw_memset(ip + 16, 0xFF, 4);
    uint16_t c = ip_csum(ip, 20);
    ip[10] = (uint8_t) (c >> 8);
    ip[11] = (uint8_t) c;
    uint8_t *udp = ip + 20;
    udp[1] = 68;
    udp[3] = 67;
    udp[4] = (uint8_t) ((8 + 300) >> 8);
    udp[5] = (uint8_t) (8 + 300);
    uint8_t *b = udp + 8; /* BOOTP */
    b[0] = 1;
    b[1] = 1;
    b[2] = 6;
    b[4] = (uint8_t) (xid >> 24);
    b[5] = (uint8_t) (xid >> 16);
    b[6] = (uint8_t) (xid >> 8);
    b[7] = (uint8_t) xid;
    b[10] = 0x80; /* broadcast flag */
    hw_memcpy(b + 28, mac, 6);
    b[236] = 99;
    b[237] = 130;
    b[238] = 83;
    b[239] = 99;
    b[240] = 53; /* DHCP message type */
    b[241] = 1;
    b[242] = 1; /* DISCOVER */
    b[243] = 255;
    return 14 + iplen;
}

/* 1 = a BOOTREPLY for xid; *yiaddr and *server are filled. */
static int is_offer(const uint8_t *f, uint32_t n, uint32_t xid, uint8_t yiaddr[4],
                    uint8_t server[4])
{
    if (n < 14 + 20 + 8 + 240 || f[12] != 0x08 || f[13] != 0x00) return 0;
    const uint8_t *ip = f + 14;
    uint32_t ihl = (uint32_t) (ip[0] & 0xF) * 4;
    if ((ip[0] >> 4) != 4 || ihl < 20 || ip[9] != 17) return 0;
    if (n < 14 + ihl + 8 + 240) return 0;
    const uint8_t *udp = ip + ihl;
    if (udp[0] != 0 || udp[1] != 67 || udp[2] != 0 || udp[3] != 68) return 0;
    const uint8_t *b = udp + 8;
    uint32_t rx = ((uint32_t) b[4] << 24) | ((uint32_t) b[5] << 16) | ((uint32_t) b[6] << 8) | b[7];
    if (b[0] != 2 || rx != xid) return 0;
    hw_memcpy(yiaddr, b + 16, 4);
    hw_memcpy(server, ip + 12, 4);
    return 1;
}

static void line_ip(hw_line_t *l, const uint8_t a[4])
{
    for (int i = 0; i < 4; i++) {
        if (i) hw_line_c(l, '.');
        hw_line_u(l, a[i]);
    }
}

static uint32_t selftest_nic(const hw_plat_t *p, hw_nic_t *n)
{
    static uint8_t tx[342], rx[2048];
    hw_line_t x;
    hw_line_init(&x);
    for (int i = 0; i < 6; i++) {
        if (i) hw_line_c(&x, ':');
        hw_line_x(&x, n->mac[i], 2);
    }
    uint32_t xid = 0x5A585600u ^ ((uint32_t) n->mac[4] << 8) ^ n->mac[5];
    uint32_t len = build_discover(tx, n->mac, xid);
    /* Up to three tries, one second each: a link that just came up may drop
     * the first frame. */
    for (int attempt = 0; attempt < 3; attempt++) {
        if (n->send(n, tx, len) != 0) {
            st_line(p, "  [SELFTEST FAILED] ", n->name, "transmit refused; mac ", &x);
            return 0;
        }
        for (uint32_t t = 0; t < 1000; t++) {
            uint32_t got = (uint32_t) n->recv(n, rx, sizeof rx);
            uint8_t yi[4], sv[4];
            if (got && is_offer(rx, got, xid, yi, sv)) {
                hw_line_s(&x, "; offered ");
                line_ip(&x, yi);
                hw_line_s(&x, " by ");
                line_ip(&x, sv);
                st_line(p, "  [SELFTEST OK] ", n->name, "DHCP offer received; mac ", &x);
                return 1;
            }
            if (!got) p->delay_us(p->ctx, 1000);
        }
    }
    st_line(p, "  [SELFTEST NONE] ", n->name, "no DHCP offer (no cable or no server); mac ", &x);
    return 0;
}

static uint32_t selftest_input(const hw_plat_t *p, hw_input_t *in)
{
    /* Two seconds of polling: long enough for an automated test to inject keys
     * (build_system/hw_drivers_check.sh does), short enough not to hold the
     * boot. The keys typed are echoed so the log proves the path. */
    hw_line_t x;
    hw_line_init(&x);
    uint32_t keys = 0;
    for (uint32_t t = 0; t < 2000; t++) {
        hw_key_t ev;
        while (in->poll(in, &ev)) {
            if (ev.pressed && ev.ascii >= 0x20 && ev.ascii < 0x7F && x.n < 40)
                hw_line_c(&x, (char) ev.ascii);
            if (ev.pressed) keys++;
        }
        p->delay_us(p->ctx, 1000);
    }
    hw_line_t y;
    hw_line_init(&y);
    hw_line_u(&y, keys);
    hw_line_s(&y, " key press(es)");
    if (keys) {
        hw_line_s(&y, ": \"");
        hw_line_s(&y, x.b);
        hw_line_c(&y, '"');
    }
    st_line(p, "  [SELFTEST OK] ", in->name, "polling; ", &y);
    return 1;
}

static uint32_t selftest_audio(const hw_plat_t *p, hw_audio_t *a)
{
    /* 0.1 s of a 750 Hz square wave at low volume (48000/750 = 64 frames per
     * period). Integer samples; the point is that the stream DMA engine runs. */
    static int16_t pcm[4800 * 2];
    for (uint32_t i = 0; i < 4800; i++) {
        int16_t s = (i % 64) < 32 ? 1200 : -1200;
        pcm[2 * i] = pcm[2 * i + 1] = s;
    }
    hw_line_t x;
    hw_line_init(&x);
    if (a->play(a, pcm, 4800) != 0) {
        st_line(p, "  [SELFTEST FAILED] ", a->name, "stream did not start", 0);
        return 0;
    }
    uint32_t pos = 0;
    /* Wait for the stream to start, then keep polling until the whole tone
     * has played: position() is where a driver notices the end and stops the
     * stream, so stopping early would leave real hardware looping the tone. */
    uint32_t total = 4800u * 4u;
    for (uint32_t t = 0; t < 300 && pos < total; t++) {
        p->delay_us(p->ctx, 1000);
        uint32_t now = a->position(a);
        if (now > pos) pos = now;
    }
    hw_line_u(&x, pos);
    hw_line_s(&x, " bytes");
    st_line(p, pos ? "  [SELFTEST OK] " : "  [SELFTEST FAILED] ", a->name,
            "output stream DMA position ", &x);
    return pos ? 1 : 0;
}

uint32_t hw_selftest_all(const hw_plat_t *p, hw_registry_t *r)
{
    uint32_t ok = 0;
    for (uint32_t i = 0; i < r->n_blk; i++) ok += selftest_blk(p, r->blk[i]);
    for (uint32_t i = 0; i < r->n_nic; i++) ok += selftest_nic(p, r->nic[i]);
    for (uint32_t i = 0; i < r->n_input; i++) ok += selftest_input(p, r->input[i]);
    for (uint32_t i = 0; i < r->n_audio; i++) ok += selftest_audio(p, r->audio[i]);
    return ok;
}
