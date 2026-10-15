/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* igb.c — Intel 82576 / I350 / I210 / I211 gigabit, ADVANCED descriptors,
 * one queue pair, polled.
 *
 * Tested in QEMU ("igb" models an 82576, 8086:10C9). The I350/I210/I211 rows
 * follow the same datasheet sections and the Linux igb id table but are NOT
 * TESTED ON HARDWARE.
 *
 * Bring-up (82576 datasheet 4.5, 4.6; I210 datasheet 4.6):
 *   stop bus mastering (CTRL.GIO_MASTER_DISABLE, bounded wait), mask every
 *   interrupt (IMC, EIMC), reset (CTRL.RST), wait for the NVM auto-load,
 *   read the MAC from RAL0/RAH0 (EERD when empty, 82576/I350 only), set link
 *   up (CTRL.SLU), then queue 0: SRRCTL = 2 KB buffers, advanced one-buffer
 *   descriptors; RXDCTL/TXDCTL.ENABLE polled until the queue reports enabled;
 *   only then the tail and RCTL/TCTL enables.
 *
 * Receive descriptors are written back over the read format, so a consumed
 * descriptor is rewritten in full (buffer address, header address 0) before
 * it goes back through RDT. Transmit uses one advanced data descriptor per
 * frame with EOP|IFCS|RS|DEXT and waits (bounded) for DD. Write-back
 * thresholds are 0/1 so a polled driver sees each completion at once.
 *
 * Verdicts as e1000.c: N no BAR / table full; F all-ones, reset or queue
 * enable timeout, EEPROM silent, no MAC, out of DMA memory; B group MAC;
 * T with "link down" for a missing cable. */
#include "hwdrv.h"

#define G_CTRL     0x0000
#define G_STATUS   0x0008
#define G_EECD     0x0010
#define G_EERD     0x0014
#define G_CTRL_EXT 0x0018
#define G_MDIC     0x0020
#define G_EEMNGCTL 0x1010
#define G_ICR      0x00C0
#define G_IAM      0x00E0
#define G_IMC      0x00D8
#define G_EIMC     0x1528
#define G_EIAC     0x152C
#define G_EIAM     0x1530
#define G_RCTL     0x0100
#define G_TCTL     0x0400
#define G_RDBAL    0xC000
#define G_RDBAH    0xC004
#define G_RDLEN    0xC008
#define G_SRRCTL   0xC00C
#define G_RDH      0xC010
#define G_RDT      0xC018
#define G_RXDCTL   0xC028
#define G_TDBAL    0xE000
#define G_TDBAH    0xE004
#define G_TDLEN    0xE008
#define G_TDH      0xE010
#define G_TDT      0xE018
#define G_TXDCTL   0xE028
#define G_MTA      0x5200
#define G_RAL0     0x5400
#define G_RAH0     0x5404

#define CTRL_GIO_MASTER_DISABLE (1u << 2)
#define CTRL_LRST               (1u << 3)
#define CTRL_SLU                (1u << 6)
#define CTRL_ILOS               (1u << 7)
#define CTRL_FRCSPD             (1u << 11)
#define CTRL_FRCDPLX            (1u << 12)
#define CTRL_RST                (1u << 26)
#define CTRL_VME                (1u << 30)
#define CTRL_PHY_RST            (1u << 31)
#define STATUS_LU               (1u << 1)
#define STATUS_GIO_MASTER_EN    (1u << 19)
#define EECD_AUTO_RD            (1u << 9)
#define EEMNGCTL_CFG_DONE0      (1u << 18)
#define CTRL_EXT_DRV_LOAD       (1u << 28)
#define MDIC_PHY1_WRITE         ((1u << 21) | (1u << 26)) /* PHY address 1, opcode write */
#define MDIC_READY              (1u << 28)
#define MDIC_ERROR              (1u << 30)
#define BMCR_ANENABLE           0x1000
#define BMCR_ANRESTART          0x0200

#define RCTL_EN    (1u << 1)
#define RCTL_BAM   (1u << 15)
#define RCTL_SECRC (1u << 26)
#define TCTL_EN    (1u << 1)
#define TCTL_PSP   (1u << 3)
#define TCTL_CT    (0x0Fu << 4)
#define TCTL_COLD  (0x3Fu << 12)
#define TCTL_RTLC  (1u << 24)

#define SRRCTL_2K         2u
#define SRRCTL_ADV_ONEBUF (1u << 25)
#define QCTL_ENABLE       (1u << 25)
#define RXDCTL_VAL        (8u | (8u << 8) | (1u << 16)) /* PTHRESH 8, HTHRESH 8, WTHRESH 1 */
#define TXDCTL_VAL        (8u | (1u << 8))              /* PTHRESH 8, HTHRESH 1, WTHRESH 0 */

/* advanced descriptors */
#define RXW_DD      (1u << 0)
#define RXW_EOP     (1u << 1)
#define RXW_ERRMASK 0x97000000u /* CE | SE | SEQ | CXE | RXE */
#define TXA_DTYP    (3u << 20)
#define TXA_EOP     (1u << 24)
#define TXA_IFCS    (1u << 25)
#define TXA_RS      (1u << 27)
#define TXA_DEXT    (1u << 29)
#define TXW_DD      (1u << 0)

#define RXN       32
#define TXN       16
#define BUF       2048u
#define MAX_FRAME 1514u

#define MASTER_US 80000u
#define RESET_US  100000u
#define CFG_US    20000u
#define EEPROM_US 10000u
#define QUEUE_US  10000u
#define MDIC_US   10000u
#define TX_US     50000u
#define LINK_US   2500000u

enum { K_82576, K_I350, K_I210 };

#define IGB_MODELS(X)                                                                              \
    X(0x10C9, K_82576, "82576")                                                                    \
    X(0x10E8, K_82576, "82576")                                                                    \
    X(0x1526, K_82576, "82576")                                                                    \
    X(0x150A, K_82576, "82576NS")                                                                  \
    X(0x1521, K_I350, "I350")                                                                      \
    X(0x1533, K_I210, "I210")                                                                      \
    X(0x1535, K_I210, "I210")                                                                      \
    X(0x157B, K_I210, "I210")                                                                      \
    X(0x1539, K_I210, "I211")

#define IGB_ID(did, kind, name) {0x8086, did},
const hw_id_t igb_ids[] = {IGB_MODELS(IGB_ID){0, 0}};

typedef struct igb_model {
    uint16_t did;
    uint8_t kind;
    const char *name;
} igb_model_t;
#define IGB_ROW(did, kind, name) {did, kind, name},
static const igb_model_t igb_models[] = {IGB_MODELS(IGB_ROW){0, 0, 0}};

typedef struct igb {
    const hw_plat_t *p;
    volatile void *r;
    uint8_t kind;
    uint8_t rx_discard;
    volatile uint8_t *rxd, *txd;
    uint8_t *rxbuf, *txbuf;
    uint64_t rxd_phys, txd_phys, rxbuf_phys, txbuf_phys;
    uint32_t rx_next, tx_next;
    uint8_t tx_busy[TXN];
    hw_nic_t nic;
} igb_t;

static void put32(volatile uint8_t *b, uint32_t off, uint32_t v)
{
    b[off] = (uint8_t) v;
    b[off + 1] = (uint8_t) (v >> 8);
    b[off + 2] = (uint8_t) (v >> 16);
    b[off + 3] = (uint8_t) (v >> 24);
}

static void put64(volatile uint8_t *b, uint32_t off, uint64_t v)
{
    put32(b, off, (uint32_t) v);
    put32(b, off + 4, (uint32_t) (v >> 32));
}

static uint32_t get32(const volatile uint8_t *b, uint32_t off)
{
    return (uint32_t) b[off] | ((uint32_t) b[off + 1] << 8) | ((uint32_t) b[off + 2] << 16) |
           ((uint32_t) b[off + 3] << 24);
}

static const igb_model_t *find_model(uint16_t did)
{
    for (const igb_model_t *m = igb_models; m->name; m++)
        if (m->did == did) return m;
    return 0;
}

static int link_is_up(igb_t *g)
{
    return (hw_rd32(g->r, G_STATUS) & STATUS_LU) != 0;
}

/* Rewrite descriptor i in read format: buffer i, header address 0 (which
 * also clears the written-back DD). */
static void rx_arm(igb_t *g, uint32_t i)
{
    volatile uint8_t *d = g->rxd + i * 16;
    put64(d, 0, g->rxbuf_phys + (uint64_t) i * BUF);
    put64(d, 8, 0);
}

static int igb_recv(hw_nic_t *n, void *buf, uint32_t max)
{
    igb_t *g = (igb_t *) n->drv;
    if (!buf || max == 0) return 0;
    for (uint32_t guard = 0; guard < RXN; guard++) {
        uint32_t i = g->rx_next;
        volatile uint8_t *d = g->rxd + i * 16;
        uint32_t se = get32(d, 8);
        if (!(se & RXW_DD)) return 0;
        hw_wmb();
        uint32_t len = get32(d, 12) & 0xFFFF;
        int good = !g->rx_discard && (se & RXW_EOP) && !(se & RXW_ERRMASK) && len > 0 && len <= BUF;
        g->rx_discard = (se & RXW_EOP) ? 0 : 1;
        uint32_t got = 0;
        if (good) {
            got = len < max ? len : max;
            hw_memcpy(buf, g->rxbuf + i * BUF, got);
        }
        rx_arm(g, i);
        hw_wmb();
        hw_wr32(g->r, G_RDT, i);
        g->rx_next = (i + 1) % RXN;
        if (got) return (int) got;
    }
    return 0;
}

static int igb_send(hw_nic_t *n, const void *frame, uint32_t len)
{
    igb_t *g = (igb_t *) n->drv;
    if (!frame || len == 0 || len > MAX_FRAME) return -1;
    uint32_t i = g->tx_next, nx = (i + 1) % TXN;
    volatile uint8_t *d = g->txd + i * 16;
    /* at most TXN-1 outstanding: a full ring would read as empty */
    if (g->tx_busy[nx] && !(get32(g->txd + nx * 16, 12) & TXW_DD)) return -2;
    hw_memcpy(g->txbuf + i * BUF, frame, len);
    put64(d, 0, g->txbuf_phys + (uint64_t) i * BUF);
    put32(d, 8, len | TXA_DTYP | TXA_EOP | TXA_IFCS | TXA_RS | TXA_DEXT);
    put32(d, 12, len << 14); /* PAYLEN; DD clear */
    g->tx_busy[i] = 1;
    g->tx_next = nx;
    hw_wmb();
    hw_wr32(g->r, G_TDT, nx);
    for (uint32_t waited = 0;; waited += 10) {
        if (get32(d, 12) & TXW_DD) {
            g->tx_busy[i] = 0;
            n->link_up = (uint8_t) link_is_up(g);
            return 0;
        }
        if (waited >= TX_US) break;
        g->p->delay_us(g->p->ctx, 10);
    }
    n->link_up = (uint8_t) link_is_up(g);
    return n->link_up ? -1 : 0;
}

static int eerd_read(igb_t *g, uint32_t word, uint16_t *out)
{
    hw_wr32(g->r, G_EERD, (word << 2) | 1u);
    if (!hw_wait32(g->p, g->r, G_EERD, 2u, 2u, EEPROM_US)) return 0;
    *out = (uint16_t) (hw_rd32(g->r, G_EERD) >> 16);
    return 1;
}

/* Write PHY register `reg` (internal PHY, address 1) through MDIC. 1 = done. */
static int phy_write(igb_t *g, uint32_t reg, uint16_t val)
{
    hw_wr32(g->r, G_MDIC, val | (reg << 16) | MDIC_PHY1_WRITE);
    if (!hw_wait32(g->p, g->r, G_MDIC, MDIC_READY, MDIC_READY, MDIC_US)) return 0;
    return !(hw_rd32(g->r, G_MDIC) & MDIC_ERROR);
}

static void irq_off(igb_t *g)
{
    hw_wr32(g->r, G_EIMC, 0xFFFFFFFFu);
    hw_wr32(g->r, G_EIAC, 0);
    hw_wr32(g->r, G_EIAM, 0);
    hw_wr32(g->r, G_IAM, 0);
    hw_wr32(g->r, G_IMC, 0xFFFFFFFFu);
    (void) hw_rd32(g->r, G_ICR);
}

static int alloc_rings(igb_t *g)
{
    const hw_plat_t *p = g->p;
    g->rxd = (volatile uint8_t *) p->dma_alloc(p->ctx, RXN * 16, 128, &g->rxd_phys);
    g->txd = (volatile uint8_t *) p->dma_alloc(p->ctx, TXN * 16, 128, &g->txd_phys);
    g->rxbuf = (uint8_t *) p->dma_alloc(p->ctx, RXN * BUF, 4096, &g->rxbuf_phys);
    g->txbuf = (uint8_t *) p->dma_alloc(p->ctx, TXN * BUF, 4096, &g->txbuf_phys);
    return g->rxd && g->txd && g->rxbuf && g->txbuf;
}

/* 0 = both queues enabled; else the failing phrase. */
static const char *setup_queues(igb_t *g)
{
    volatile void *r = g->r;
    const hw_plat_t *p = g->p;
    for (uint32_t i = 0; i < RXN; i++) rx_arm(g, i);
    hw_wr32(r, G_RXDCTL, 0);
    hw_wr32(r, G_RDBAL, (uint32_t) g->rxd_phys);
    hw_wr32(r, G_RDBAH, (uint32_t) (g->rxd_phys >> 32));
    hw_wr32(r, G_RDLEN, RXN * 16);
    hw_wr32(r, G_SRRCTL, SRRCTL_2K | SRRCTL_ADV_ONEBUF);
    hw_wr32(r, G_RDH, 0);
    hw_wr32(r, G_RDT, 0);
    hw_wr32(r, G_RXDCTL, RXDCTL_VAL | QCTL_ENABLE);
    /* The bit reads back set once the queue runs; look after a moment. */
    p->delay_us(p->ctx, 10);
    if (!hw_wait32(p, r, G_RXDCTL, QCTL_ENABLE, QCTL_ENABLE, QUEUE_US))
        return "RX queue did not enable";
    hw_wr32(r, G_RDT, RXN - 1);
    g->rx_next = 0;
    hw_wr32(r, G_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);

    hw_wr32(r, G_TXDCTL, 0);
    hw_wr32(r, G_TDBAL, (uint32_t) g->txd_phys);
    hw_wr32(r, G_TDBAH, (uint32_t) (g->txd_phys >> 32));
    hw_wr32(r, G_TDLEN, TXN * 16);
    hw_wr32(r, G_TDH, 0);
    hw_wr32(r, G_TDT, 0);
    hw_wr32(r, G_TXDCTL, TXDCTL_VAL | QCTL_ENABLE);
    p->delay_us(p->ctx, 10);
    if (!hw_wait32(p, r, G_TXDCTL, QCTL_ENABLE, QCTL_ENABLE, QUEUE_US))
        return "TX queue did not enable";
    g->tx_next = 0;
    hw_wr32(r, G_TCTL, TCTL_EN | TCTL_PSP | TCTL_CT | TCTL_COLD | TCTL_RTLC);
    return 0;
}

hw_verdict_t igb_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r, hw_line_t *note)
{
    const igb_model_t *m = find_model(d->device);
    volatile void *regs = hw_pci_map_bar(p, d, 0);
    if (!m || !regs) {
        hw_line_s(note, m ? "BAR0 not assigned" : "unknown device id");
        return HW_N;
    }
    hw_line_s(note, m->name);
    hw_line_c(note, ' ');
    if (r->n_nic >= HW_MAX_NIC) {
        hw_line_s(note, "not registered: NIC table full");
        return HW_N;
    }
    igb_t *g = (igb_t *) p->dma_alloc(p->ctx, sizeof(igb_t), 64, 0);
    if (!g) {
        hw_line_s(note, "out of DMA memory");
        return HW_F;
    }
    g->p = p;
    g->r = regs;
    g->kind = m->kind;
    hw_pci_enable(p, d);
    if (hw_rd32(regs, G_STATUS) == 0xFFFFFFFFu) {
        hw_line_s(note, "registers read all-ones");
        return HW_F;
    }

    /* 1. Quiesce (firmware may have left DMA running), reset. */
    irq_off(g);
    hw_wr32(regs, G_RCTL, 0);
    hw_wr32(regs, G_TCTL, TCTL_PSP);
    hw_wr32(regs, G_CTRL, hw_rd32(regs, G_CTRL) | CTRL_GIO_MASTER_DISABLE);
    hw_wait32(p, regs, G_STATUS, STATUS_GIO_MASTER_EN, 0, MASTER_US);
    p->delay_us(p->ctx, 10000);
    hw_wr32(regs, G_CTRL, hw_rd32(regs, G_CTRL) | CTRL_RST);
    p->delay_us(p->ctx, 5000);
    if (!hw_wait32(p, regs, G_CTRL, CTRL_RST, 0, RESET_US)) {
        hw_line_s(note, "reset timed out");
        return HW_F;
    }
    irq_off(g);
    /* NVM auto-load: per-port CFG_DONE, plus EECD.AUTO_RD on the 82576. Not
     * gating: parts without the bit just use up the wait. */
    uint32_t cfg = EEMNGCTL_CFG_DONE0 << (d->bdf & 3);
    hw_wait32(p, regs, G_EEMNGCTL, cfg, cfg, CFG_US);
    if (g->kind == K_82576) hw_wait32(p, regs, G_EECD, EECD_AUTO_RD, EECD_AUTO_RD, CFG_US);
    hw_wr32(regs, G_CTRL_EXT, hw_rd32(regs, G_CTRL_EXT) | CTRL_EXT_DRV_LOAD);

    /* 2. Station address. I210/I211 keep theirs in iNVM/flash that only the
     * hardware loader reads, so there is no fallback there. */
    uint8_t *mac = g->nic.mac;
    uint32_t ral = hw_rd32(regs, G_RAL0), rah = hw_rd32(regs, G_RAH0);
    if (ral == 0 && (rah & 0xFFFF) == 0 && g->kind != K_I210) {
        uint16_t w[3];
        for (uint32_t i = 0; i < 3; i++) {
            if (!eerd_read(g, i, &w[i])) {
                hw_line_s(note, "EEPROM read timed out");
                return HW_F;
            }
        }
        ral = (uint32_t) w[0] | ((uint32_t) w[1] << 16);
        rah = w[2];
    }
    for (uint32_t i = 0; i < 4; i++) mac[i] = (uint8_t) (ral >> (8 * i));
    mac[4] = (uint8_t) rah;
    mac[5] = (uint8_t) (rah >> 8);
    if ((ral | (rah & 0xFFFF)) == 0) {
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
    hw_wr32(regs, G_RAL0, ral);
    hw_wr32(regs, G_RAH0, (rah & 0xFFFF) | (1u << 31));
    for (uint32_t i = 0; i < 128; i++) hw_wr32(regs, G_MTA + 4 * i, 0);

    /* 3. Link up: MAC follows the PHY (SLU), and the PHY restarts
     * autonegotiation (after a MAC reset the 82576 PHY, and QEMU's model of
     * it, does not report link until it is told to). Then the queues. */
    uint32_t ctrl = hw_rd32(regs, G_CTRL);
    ctrl &= ~(CTRL_GIO_MASTER_DISABLE | CTRL_LRST | CTRL_ILOS | CTRL_FRCSPD | CTRL_FRCDPLX |
              CTRL_RST | CTRL_VME | CTRL_PHY_RST);
    hw_wr32(regs, G_CTRL, ctrl | CTRL_SLU);
    int phy_ok = phy_write(g, 0, BMCR_ANENABLE | BMCR_ANRESTART);
    if (!alloc_rings(g)) {
        hw_line_s(note, " out of DMA memory");
        return HW_F;
    }
    const char *qerr = setup_queues(g);
    if (qerr) {
        hw_line_c(note, ' ');
        hw_line_s(note, qerr);
        return HW_F;
    }
    g->nic.link_up = (uint8_t) hw_wait32(p, regs, G_STATUS, STATUS_LU, STATUS_LU, LINK_US);

    hw_nic_t *n = &g->nic;
    n->name[0] = 'e';
    n->name[1] = 't';
    n->name[2] = 'h';
    n->name[3] = (char) ('0' + r->n_nic);
    n->name[4] = 0;
    n->drv = g;
    n->send = igb_send;
    n->recv = igb_recv;
    n->on_pci = 1;
    n->bdf = d->bdf;
    hw_line_s(note, n->link_up ? " link up as " : " link down as ");
    hw_line_s(note, n->name);
    if (!phy_ok) hw_line_s(note, " (PHY did not answer)");
    hw_reg_nic(r, n);
    return HW_T;
}
