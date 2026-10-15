/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* r8169.c — Realtek RTL8169 / RTL8168 / RTL8111 / RTL8101 Ethernet, polled.
 *
 * UNTESTED ON HARDWARE AND UNTESTED IN QEMU: QEMU has no model of these
 * chips (only the older RTL8139, a different design). Written against the
 * public RTL8169S / RTL8168 / RTL8101 datasheet register layout and checked
 * only against the host fake in test/test_r8169.c.
 *
 * Register window: the first memory BAR of at least 256 bytes (BAR1 on the
 * 8169, BAR2 on the 8168/8101; BAR0 is the I/O window, which we skip).
 *
 * Bring-up: mask and acknowledge interrupts, ChipCmd.RST (self-clearing,
 * bounded wait), MAC from IDR0-5 (the chip loads it from its EEPROM/eFuse
 * at power-on; there is no read-back path here), then with the config
 * registers unlocked (9346CR = 0xC0): C+ command (no VLAN strip, no RX
 * checksum), RxMaxSize, descriptor ring addresses (high dword first);
 * lock, enable TX/RX, RxConfig (broadcast + own address, DMA burst
 * unlimited), TxConfig (normal IFG, DMA burst unlimited).
 *
 * Descriptors are 16 bytes: opts1 (OWN/EOR/FS/LS/length), opts2 (VLAN),
 * 64-bit buffer address; ring bases 256-byte aligned. The OWN bit is the
 * handoff in both directions; transmit kicks with TxPoll.NPQ. Receive
 * lengths include the 4-byte CRC, which is stripped here. Short frames are
 * padded to 60 bytes by the driver (several 8168 revisions do not pad).
 *
 * What real chips are likely to need that this does not do: the per-revision
 * PHY and ePHY fix-ups and firmware patches the Linux driver applies (the
 * PHY autonegotiates from its power-on defaults here), and the 8125/2.5G
 * parts are not covered at all. */
#include "hwdrv.h"

#define R_IDR0      0x00
#define R_MAR0      0x08
#define R_TNPDS_LO  0x20
#define R_TNPDS_HI  0x24
#define R_CHIPCMD   0x37
#define R_TXPOLL    0x38
#define R_INTRMASK  0x3C
#define R_INTRSTAT  0x3E
#define R_TXCONFIG  0x40
#define R_RXCONFIG  0x44
#define R_CFG9346   0x50
#define R_PHYSTATUS 0x6C
#define R_RXMAXSIZE 0xDA
#define R_CPLUSCMD  0xE0
#define R_RDSAR_LO  0xE4
#define R_RDSAR_HI  0xE8
#define R_MAXTXPKT  0xEC

#define CMD_RESET  0x10
#define CMD_RXENB  0x08
#define CMD_TXENB  0x04
#define TXPOLL_NPQ 0x40
#define CFG_UNLOCK 0xC0
#define CFG_LOCK   0x00
#define PHY_LINK   0x02
#define CP_RXCHK   0x0020
#define CP_RXVLAN  0x0040

#define RX_ACCEPT_MYPHYS (1u << 1)
#define RX_ACCEPT_BCAST  (1u << 3)
#define RX_DMA_UNLIM     (7u << 8)
#define RX_FIFO_NONE     (7u << 13) /* 8169: no RX FIFO threshold */
#define RX_128INT        (1u << 15) /* 8168/8101 */
#define TX_DMA_UNLIM     (7u << 8)
#define TX_IFG_NORMAL    (3u << 24)

#define D_OWN (1u << 31)
#define D_EOR (1u << 30)
#define D_FS  (1u << 29)
#define D_LS  (1u << 28)
#define D_RES (1u << 21) /* receive error summary */
#define D_LEN 0x3FFFu

#define RXN       32
#define TXN       16
#define BUF       2048u
#define MAX_FRAME 1514u
#define MIN_FRAME 60u

#define RESET_US 100000u
#define TX_US    50000u
#define LINK_US  2500000u

enum { K_8169, K_8168, K_8101 };

#define R8169_MODELS(X)                                                                            \
    X(0x8169, K_8169, "RTL8169")                                                                   \
    X(0x8167, K_8169, "RTL8169SC")                                                                 \
    X(0x8168, K_8168, "RTL8168/8111")                                                              \
    X(0x8161, K_8168, "RTL8168/8111")                                                              \
    X(0x8136, K_8101, "RTL8101/8102")

#define R8169_ID(did, kind, name) {0x10EC, did},
const hw_id_t r8169_ids[] = {R8169_MODELS(R8169_ID){0, 0}};

typedef struct rtl_model {
    uint16_t did;
    uint8_t kind;
    const char *name;
} rtl_model_t;
#define R8169_ROW(did, kind, name) {did, kind, name},
static const rtl_model_t rtl_models[] = {R8169_MODELS(R8169_ROW){0, 0, 0}};

typedef struct rtl {
    const hw_plat_t *p;
    volatile void *r;
    uint8_t kind;
    uint8_t rx_discard;
    volatile uint8_t *rxd, *txd;
    uint8_t *rxbuf, *txbuf;
    uint64_t rxd_phys, txd_phys, rxbuf_phys, txbuf_phys;
    uint32_t rx_next, tx_next;
    hw_nic_t nic;
} rtl_t;

static void put32(volatile uint8_t *b, uint32_t off, uint32_t v)
{
    b[off] = (uint8_t) v;
    b[off + 1] = (uint8_t) (v >> 8);
    b[off + 2] = (uint8_t) (v >> 16);
    b[off + 3] = (uint8_t) (v >> 24);
}

static uint32_t get32(const volatile uint8_t *b, uint32_t off)
{
    return (uint32_t) b[off] | ((uint32_t) b[off + 1] << 8) | ((uint32_t) b[off + 2] << 16) |
           ((uint32_t) b[off + 3] << 24);
}

static const rtl_model_t *find_model(uint16_t did)
{
    for (const rtl_model_t *m = rtl_models; m->name; m++)
        if (m->did == did) return m;
    return 0;
}

/* Poll an 8-bit register until (v & mask) == want. 1 = met. */
static int wait8(const hw_plat_t *p, volatile void *r, uint32_t off, uint8_t mask, uint8_t want,
                 uint32_t timeout_us)
{
    for (uint32_t waited = 0;; waited += 10) {
        if ((hw_rd8(r, off) & mask) == want) return 1;
        if (waited >= timeout_us) return 0;
        p->delay_us(p->ctx, 10);
    }
}

static int link_is_up(rtl_t *t)
{
    return (hw_rd8(t->r, R_PHYSTATUS) & PHY_LINK) != 0;
}

/* Hand RX descriptor i to the chip: address first, OWN last. */
static void rx_arm(rtl_t *t, uint32_t i)
{
    volatile uint8_t *d = t->rxd + i * 16;
    uint64_t a = t->rxbuf_phys + (uint64_t) i * BUF;
    put32(d, 8, (uint32_t) a);
    put32(d, 12, (uint32_t) (a >> 32));
    put32(d, 4, 0);
    hw_wmb();
    put32(d, 0, D_OWN | (i == RXN - 1 ? D_EOR : 0) | BUF);
}

static int rtl_recv(hw_nic_t *n, void *buf, uint32_t max)
{
    rtl_t *t = (rtl_t *) n->drv;
    if (!buf || max == 0) return 0;
    for (uint32_t guard = 0; guard < RXN; guard++) {
        uint32_t i = t->rx_next;
        volatile uint8_t *d = t->rxd + i * 16;
        uint32_t o1 = get32(d, 0);
        if (o1 & D_OWN) return 0;
        hw_wmb();
        uint32_t raw = o1 & D_LEN;
        int whole = (o1 & D_FS) && (o1 & D_LS);
        int good = !t->rx_discard && whole && !(o1 & D_RES) && raw > 4 && raw <= BUF;
        t->rx_discard = (o1 & D_LS) ? 0 : 1;
        uint32_t got = 0;
        if (good) {
            uint32_t len = raw - 4; /* CRC */
            got = len < max ? len : max;
            hw_memcpy(buf, t->rxbuf + i * BUF, got);
        }
        rx_arm(t, i);
        t->rx_next = (i + 1) % RXN;
        if (got) return (int) got;
    }
    return 0;
}

static int rtl_send(hw_nic_t *n, const void *frame, uint32_t len)
{
    rtl_t *t = (rtl_t *) n->drv;
    if (!frame || len == 0 || len > MAX_FRAME) return -1;
    uint32_t i = t->tx_next;
    volatile uint8_t *d = t->txd + i * 16;
    if (get32(d, 0) & D_OWN) return -2; /* the chip still owns this slot */
    uint8_t *b = t->txbuf + i * BUF;
    hw_memcpy(b, frame, len);
    if (len < MIN_FRAME) {
        hw_memset(b + len, 0, MIN_FRAME - len);
        len = MIN_FRAME;
    }
    uint64_t a = t->txbuf_phys + (uint64_t) i * BUF;
    put32(d, 8, (uint32_t) a);
    put32(d, 12, (uint32_t) (a >> 32));
    put32(d, 4, 0);
    hw_wmb();
    put32(d, 0, D_OWN | D_FS | D_LS | (i == TXN - 1 ? D_EOR : 0) | len);
    t->tx_next = (i + 1) % TXN;
    hw_wmb();
    hw_wr8(t->r, R_TXPOLL, TXPOLL_NPQ);
    for (uint32_t waited = 0;; waited += 10) {
        if (!(get32(d, 0) & D_OWN)) {
            n->link_up = (uint8_t) link_is_up(t);
            return 0;
        }
        if (waited >= TX_US) break;
        t->p->delay_us(t->p->ctx, 10);
    }
    n->link_up = (uint8_t) link_is_up(t);
    return n->link_up ? -1 : 0;
}

static volatile void *map_regs(const hw_plat_t *p, const hw_pci_dev_t *d)
{
    for (uint32_t i = 0; i < 6; i++)
        if (d->bar[i] && !(d->bar_flags[i] & HW_BAR_IO) && d->bar_size[i] >= 256)
            return hw_pci_map_bar(p, d, i);
    return 0;
}

static int alloc_rings(rtl_t *t)
{
    const hw_plat_t *p = t->p;
    t->rxd = (volatile uint8_t *) p->dma_alloc(p->ctx, RXN * 16, 256, &t->rxd_phys);
    t->txd = (volatile uint8_t *) p->dma_alloc(p->ctx, TXN * 16, 256, &t->txd_phys);
    t->rxbuf = (uint8_t *) p->dma_alloc(p->ctx, RXN * BUF, 4096, &t->rxbuf_phys);
    t->txbuf = (uint8_t *) p->dma_alloc(p->ctx, TXN * BUF, 4096, &t->txbuf_phys);
    return t->rxd && t->txd && t->rxbuf && t->txbuf;
}

hw_verdict_t r8169_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r,
                        hw_line_t *note)
{
    const rtl_model_t *m = find_model(d->device);
    volatile void *regs = m ? map_regs(p, d) : 0;
    if (!m || !regs) {
        hw_line_s(note, m ? "no memory BAR" : "unknown device id");
        return HW_N;
    }
    hw_line_s(note, m->name);
    hw_line_c(note, ' ');
    if (r->n_nic >= HW_MAX_NIC) {
        hw_line_s(note, "not registered: NIC table full");
        return HW_N;
    }
    rtl_t *t = (rtl_t *) p->dma_alloc(p->ctx, sizeof(rtl_t), 64, 0);
    if (!t) {
        hw_line_s(note, "out of DMA memory");
        return HW_F;
    }
    t->p = p;
    t->r = regs;
    t->kind = m->kind;
    hw_pci_enable(p, d);
    if (hw_rd32(regs, R_TXCONFIG) == 0xFFFFFFFFu) {
        hw_line_s(note, "registers read all-ones");
        return HW_F;
    }

    /* 1. Quiet and reset. */
    hw_wr16(regs, R_INTRMASK, 0);
    hw_wr16(regs, R_INTRSTAT, 0xFFFF);
    hw_wr8(regs, R_CHIPCMD, 0);
    hw_wr8(regs, R_CHIPCMD, CMD_RESET);
    if (!wait8(p, regs, R_CHIPCMD, CMD_RESET, 0, RESET_US)) {
        hw_line_s(note, "reset timed out");
        return HW_F;
    }
    hw_wr16(regs, R_INTRMASK, 0);
    hw_wr16(regs, R_INTRSTAT, 0xFFFF);

    /* 2. Station address. */
    uint8_t *mac = t->nic.mac;
    uint32_t any = 0;
    for (uint32_t i = 0; i < 6; i++) {
        mac[i] = hw_rd8(regs, R_IDR0 + i);
        any |= mac[i];
    }
    if (!any) {
        hw_line_s(note, "no MAC address");
        return HW_F;
    }
    for (int i = 0; i < 6; i++) {
        if (i) hw_line_c(note, ':');
        hw_line_x(note, mac[i], 2);
    }
    if (mac[0] & 1) {
        hw_line_s(note, " has the group bit set");
        return HW_B;
    }

    /* 3. Rings. */
    if (!alloc_rings(t)) {
        hw_line_s(note, " out of DMA memory");
        return HW_F;
    }
    for (uint32_t i = 0; i < RXN; i++) rx_arm(t, i);
    for (uint32_t i = 0; i < TXN; i++) put32(t->txd + i * 16, 0, i == TXN - 1 ? D_EOR : 0);
    t->rx_next = t->tx_next = 0;

    hw_wr8(regs, R_CFG9346, CFG_UNLOCK);
    hw_wr16(regs, R_CPLUSCMD,
            (uint16_t) (hw_rd16(regs, R_CPLUSCMD) & ~(uint32_t) (CP_RXCHK | CP_RXVLAN)));
    hw_wr16(regs, R_RXMAXSIZE, (uint16_t) BUF);
    hw_wr8(regs, R_MAXTXPKT, 0x3F);
    hw_wr32(regs, R_TNPDS_HI, (uint32_t) (t->txd_phys >> 32));
    hw_wr32(regs, R_TNPDS_LO, (uint32_t) t->txd_phys);
    hw_wr32(regs, R_RDSAR_HI, (uint32_t) (t->rxd_phys >> 32));
    hw_wr32(regs, R_RDSAR_LO, (uint32_t) t->rxd_phys);
    hw_wr8(regs, R_CFG9346, CFG_LOCK);

    hw_wr8(regs, R_CHIPCMD, CMD_TXENB | CMD_RXENB);
    uint32_t rxc = RX_DMA_UNLIM | RX_ACCEPT_BCAST | RX_ACCEPT_MYPHYS;
    rxc |= t->kind == K_8169 ? RX_FIFO_NONE : RX_128INT;
    hw_wr32(regs, R_RXCONFIG, rxc);
    hw_wr32(regs, R_TXCONFIG, TX_IFG_NORMAL | TX_DMA_UNLIM);
    hw_wr32(regs, R_MAR0, 0);
    hw_wr32(regs, R_MAR0 + 4, 0);

    /* 4. Link, from the PHY's own autonegotiation. */
    t->nic.link_up = (uint8_t) wait8(p, regs, R_PHYSTATUS, PHY_LINK, PHY_LINK, LINK_US);

    hw_nic_t *n = &t->nic;
    n->name[0] = 'e';
    n->name[1] = 't';
    n->name[2] = 'h';
    n->name[3] = (char) ('0' + r->n_nic);
    n->name[4] = 0;
    n->drv = t;
    n->send = rtl_send;
    n->recv = rtl_recv;
    n->on_pci = 1;
    n->bdf = d->bdf;
    hw_line_s(note, n->link_up ? " link up as " : " link down as ");
    hw_line_s(note, n->name);
    hw_reg_nic(r, n);
    return HW_T;
}
