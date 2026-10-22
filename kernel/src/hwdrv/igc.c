/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* igc.c — Intel I225 / I226 2.5 GbE (and the I220/I221 1 GbE parts of the same
 * family), ADVANCED descriptors, one queue pair, polled.
 *
 * UNTESTED ON HARDWARE AND UNTESTED IN QEMU: QEMU (through 8.2 at least) has
 * no I225/I226 model. Written against the register layout and bring-up order
 * of the mainline Linux igc driver (v7.2: igc_regs.h, igc_defines.h,
 * igc_base.c, igc_mac.c, igc_main.c) and checked only against the host fake
 * in test/test_igc.c. The device ids are igc_hw.h's IGC_DEV_ID_* table, all
 * of which igc_pci_tbl binds.
 *
 * The queue registers, advanced descriptors, RCTL/TCTL, RAL/RAH and MTA sit
 * where they do on the igb parts (igb.c), but the legacy interrupt block
 * moved (ICR 0x1500, IMC 0x150C, IAM 0x1510) and so did EERD (0x12014),
 * which is why this is its own driver.
 *
 * Bring-up (igc_reset_hw_base, igc_configure_rx_ring/tx_ring):
 *   stop bus mastering (CTRL.GIO_MASTER_DISABLE, bounded wait), mask every
 *   interrupt (IMC, EIMC), RCTL off, TCTL to PSP only, 10 ms, reset
 *   (CTRL.RST, self-clearing, bounded wait), wait for the NVM auto-load
 *   (EECD.AUTO_RD; not gating, as in Linux), read the MAC from RAL0/RAH0
 *   (the hardware loads it there from the NVM; parts with a blank NVM,
 *   0x125F and 0x15FD, report "no MAC address"), clear the other 15
 *   receive addresses and the multicast table, set link up (CTRL.SLU, no
 *   forced speed), restart PHY autonegotiation through MDIC (PHY address 0,
 *   as Linux uses), then queue 0: SRRCTL = 2 KB buffers, 256-byte header
 *   size, advanced one-buffer descriptors; RXDCTL/TXDCTL.ENABLE polled
 *   until the queue reports enabled; only then the tail and RCTL/TCTL.
 *
 * Receive descriptors are written back over the read format, so a consumed
 * descriptor is rewritten in full (buffer address, header address 0) before
 * it goes back through RDT; a frame is dropped on STATERR.RXE, as Linux
 * does. Transmit uses one advanced data descriptor per frame with
 * EOP|IFCS|RS|DEXT and waits (bounded) for DD. Write-back thresholds are
 * 0/1 (Linux uses 16/4 with interrupts) so a polled driver sees each
 * completion at once.
 *
 * The transmit ring is bounded by free-running produce/reclaim counters:
 * tx_head - tx_tail >= TX_CAP is backpressure (-2). TX_CAP is TXN - 1
 * because TDT == TDH means an empty ring to the hardware.
 *
 * What real parts may need that this does not do: the SW_FW_SYNC semaphore
 * around MDIC (only matters when manageability firmware shares the PHY),
 * the I225 PHY firmware-version workarounds, EEE and LTR set-up. Without
 * the semaphore the autonegotiation restart can lose to firmware; the PHY
 * then negotiates from its power-on defaults, which advertise 2.5G.
 *
 * Verdicts as igb.c: N no BAR / table full; F all-ones, reset or queue
 * enable timeout, no MAC, out of DMA memory; B group MAC; T with
 * "link down" for a missing cable. */
#include "hwdrv.h"

#define C_CTRL     0x0000
#define C_STATUS   0x0008
#define C_EECD     0x0010
#define C_CTRL_EXT 0x0018
#define C_MDIC     0x0020
#define C_RCTL     0x0100
#define C_TCTL     0x0400
#define C_ICR      0x1500
#define C_IMC      0x150C
#define C_IAM      0x1510
#define C_EIMC     0x1528
#define C_EIAC     0x152C
#define C_EIAM     0x1530
#define C_MTA      0x5200
#define C_RAL0     0x5400
#define C_RAH0     0x5404
#define C_RDBAL    0xC000
#define C_RDBAH    0xC004
#define C_RDLEN    0xC008
#define C_SRRCTL   0xC00C
#define C_RDH      0xC010
#define C_RDT      0xC018
#define C_RXDCTL   0xC028
#define C_TDBAL    0xE000
#define C_TDBAH    0xE004
#define C_TDLEN    0xE008
#define C_TDH      0xE010
#define C_TDT      0xE018
#define C_TXDCTL   0xE028

#define RAR_ENTRIES 16
#define MTA_WORDS   128

#define CTRL_GIO_MASTER_DISABLE (1u << 2)
#define CTRL_SLU                (1u << 6)
#define CTRL_FRCSPD             (1u << 11)
#define CTRL_FRCDPX             (1u << 12)
#define CTRL_RST                (1u << 26)
#define CTRL_VME                (1u << 30)
#define CTRL_PHY_RST            (1u << 31)
#define STATUS_LU               (1u << 1)
#define STATUS_SPEED_100        (1u << 6)
#define STATUS_SPEED_1000       (1u << 7)
#define STATUS_GIO_MASTER_EN    (1u << 19)
#define STATUS_SPEED_2500       (1u << 22)
#define EECD_AUTO_RD            (1u << 9)
#define CTRL_EXT_DRV_LOAD       (1u << 28)
#define MDIC_PHY0_WRITE         (1u << 26) /* PHY address 0, opcode write */
#define MDIC_READY              (1u << 28)
#define MDIC_ERROR              (1u << 30)
#define RAH_AV                  (1u << 31)
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
#define SRRCTL_HDR_256    (4u << 8)
#define SRRCTL_ADV_ONEBUF (1u << 25)
#define QCTL_ENABLE       (1u << 25)
#define RXDCTL_VAL        (8u | (8u << 8) | (1u << 16)) /* PTHRESH 8, HTHRESH 8, WTHRESH 1 */
#define TXDCTL_VAL        (8u | (1u << 8))              /* PTHRESH 8, HTHRESH 1, WTHRESH 0 */

/* advanced descriptors */
#define RXW_DD   (1u << 0)
#define RXW_EOP  (1u << 1)
#define RXW_RXE  (1u << 31)
#define TXA_DTYP (3u << 20)
#define TXA_EOP  (1u << 24)
#define TXA_IFCS (1u << 25)
#define TXA_RS   (1u << 27)
#define TXA_DEXT (1u << 29)
#define TXW_DD   (1u << 0)

#define RXN       32u
#define TXN       16u
#define TX_CAP    (TXN - 1u)
#define BUF       2048u
#define MAX_FRAME 1514u

_Static_assert((RXN & (RXN - 1u)) == 0 && (TXN & (TXN - 1u)) == 0, "igc rings: powers of two");

#define MASTER_US 80000u
#define RESET_US  100000u
#define AUTORD_US 20000u
#define QUEUE_US  10000u
#define MDIC_US   10000u
#define TX_US     50000u
#define LINK_US   3000000u /* 2.5GBASE-T autonegotiation is slower than 1G */

#define IGC_MODELS(X)                                                                              \
    X(0x15F2, "I225-LM")                                                                           \
    X(0x15F3, "I225-V")                                                                            \
    X(0x15F8, "I225-I")                                                                            \
    X(0x15F7, "I220-V")                                                                            \
    X(0x3100, "I225-K")                                                                            \
    X(0x3101, "I225-K2")                                                                           \
    X(0x3102, "I226-K")                                                                            \
    X(0x5502, "I225-LMvP")                                                                         \
    X(0x5503, "I226-LMvP")                                                                         \
    X(0x0D9F, "I225-IT")                                                                           \
    X(0x125B, "I226-LM")                                                                           \
    X(0x125C, "I226-V")                                                                            \
    X(0x125D, "I226-IT")                                                                           \
    X(0x125E, "I221-V")                                                                            \
    X(0x125F, "I226 (blank NVM)")                                                                  \
    X(0x15FD, "I225 (blank NVM)")

#define IGC_ID(did, name) {0x8086, did},
const hw_id_t igc_ids[] = {IGC_MODELS(IGC_ID){0, 0}};

typedef struct igc_model {
    uint16_t did;
    const char *name;
} igc_model_t;
#define IGC_ROW(did, name) {did, name},
static const igc_model_t igc_models[] = {IGC_MODELS(IGC_ROW){0, 0}};

typedef struct igc {
    const hw_plat_t *p;
    volatile void *r;
    uint8_t rx_discard;
    volatile uint8_t *rxd, *txd;
    uint8_t *rxbuf, *txbuf;
    uint64_t rxd_phys, txd_phys, rxbuf_phys, txbuf_phys;
    uint32_t rx_next;
    uint32_t tx_head, tx_tail; /* free running: produced, reclaimed */
    hw_nic_t nic;
} igc_t;

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

static const igc_model_t *find_model(uint16_t did)
{
    for (const igc_model_t *m = igc_models; m->name; m++)
        if (m->did == did) return m;
    return 0;
}

static int link_is_up(igc_t *g)
{
    return (hw_rd32(g->r, C_STATUS) & STATUS_LU) != 0;
}

/* Rewrite descriptor i in read format: buffer i, header address 0 (which
 * also clears the written-back DD). */
static void rx_arm(igc_t *g, uint32_t i)
{
    volatile uint8_t *d = g->rxd + i * 16;
    put64(d, 0, g->rxbuf_phys + (uint64_t) i * BUF);
    put64(d, 8, 0);
}

static int igc_recv(hw_nic_t *n, void *buf, uint32_t max)
{
    igc_t *g = (igc_t *) n->drv;
    if (!buf || max == 0) return 0;
    for (uint32_t guard = 0; guard < RXN; guard++) {
        uint32_t i = g->rx_next;
        volatile uint8_t *d = g->rxd + i * 16;
        uint32_t se = get32(d, 8);
        if (!(se & RXW_DD)) return 0;
        hw_wmb();
        uint32_t len = get32(d, 12) & 0xFFFF;
        int good = !g->rx_discard && (se & RXW_EOP) && !(se & RXW_RXE) && len > 0 && len <= BUF;
        g->rx_discard = (se & RXW_EOP) ? 0 : 1;
        uint32_t got = 0;
        if (good) {
            got = len < max ? len : max;
            hw_memcpy(buf, g->rxbuf + i * BUF, got);
        }
        rx_arm(g, i);
        hw_wmb();
        hw_wr32(g->r, C_RDT, i);
        g->rx_next = (i + 1) % RXN;
        if (got) return (int) got;
    }
    return 0;
}

/* Advance tx_tail over every descriptor the hardware has finished. */
static void tx_reclaim(igc_t *g)
{
    while (g->tx_tail != g->tx_head && (get32(g->txd + (g->tx_tail % TXN) * 16, 12) & TXW_DD))
        g->tx_tail++;
}

static int igc_send(hw_nic_t *n, const void *frame, uint32_t len)
{
    igc_t *g = (igc_t *) n->drv;
    if (!frame || len == 0 || len > MAX_FRAME) return -1;
    tx_reclaim(g);
    if (g->tx_head - g->tx_tail >= TX_CAP) return -2;
    uint32_t i = g->tx_head % TXN;
    volatile uint8_t *d = g->txd + i * 16;
    hw_memcpy(g->txbuf + i * BUF, frame, len);
    put64(d, 0, g->txbuf_phys + (uint64_t) i * BUF);
    put32(d, 8, len | TXA_DTYP | TXA_EOP | TXA_IFCS | TXA_RS | TXA_DEXT);
    put32(d, 12, len << 14); /* PAYLEN; DD clear */
    g->tx_head++;
    hw_wmb();
    hw_wr32(g->r, C_TDT, g->tx_head % TXN);
    for (uint32_t waited = 0;; waited += 10) {
        if (get32(d, 12) & TXW_DD) {
            tx_reclaim(g);
            n->link_up = (uint8_t) link_is_up(g);
            return 0;
        }
        if (waited >= TX_US) break;
        g->p->delay_us(g->p->ctx, 10);
    }
    n->link_up = (uint8_t) link_is_up(g);
    return n->link_up ? -1 : 0;
}

/* Write PHY register `reg` (internal PHY, address 0) through MDIC. 1 = done. */
static int phy_write(igc_t *g, uint32_t reg, uint16_t val)
{
    hw_wr32(g->r, C_MDIC, val | (reg << 16) | MDIC_PHY0_WRITE);
    if (!hw_wait32(g->p, g->r, C_MDIC, MDIC_READY, MDIC_READY, MDIC_US)) return 0;
    return !(hw_rd32(g->r, C_MDIC) & MDIC_ERROR);
}

static void irq_off(igc_t *g)
{
    hw_wr32(g->r, C_EIMC, 0xFFFFFFFFu);
    hw_wr32(g->r, C_EIAC, 0);
    hw_wr32(g->r, C_EIAM, 0);
    hw_wr32(g->r, C_IAM, 0);
    hw_wr32(g->r, C_IMC, 0xFFFFFFFFu);
    (void) hw_rd32(g->r, C_ICR);
}

static int alloc_rings(igc_t *g)
{
    const hw_plat_t *p = g->p;
    g->rxd = (volatile uint8_t *) p->dma_alloc(p->ctx, RXN * 16, 128, &g->rxd_phys);
    g->txd = (volatile uint8_t *) p->dma_alloc(p->ctx, TXN * 16, 128, &g->txd_phys);
    g->rxbuf = (uint8_t *) p->dma_alloc(p->ctx, RXN * BUF, 4096, &g->rxbuf_phys);
    g->txbuf = (uint8_t *) p->dma_alloc(p->ctx, TXN * BUF, 4096, &g->txbuf_phys);
    return g->rxd && g->txd && g->rxbuf && g->txbuf;
}

/* 0 = both queues enabled; else the failing phrase. */
static const char *setup_queues(igc_t *g)
{
    volatile void *r = g->r;
    const hw_plat_t *p = g->p;
    for (uint32_t i = 0; i < RXN; i++) rx_arm(g, i);
    hw_wr32(r, C_RXDCTL, 0);
    hw_wr32(r, C_RDBAL, (uint32_t) g->rxd_phys);
    hw_wr32(r, C_RDBAH, (uint32_t) (g->rxd_phys >> 32));
    hw_wr32(r, C_RDLEN, RXN * 16);
    hw_wr32(r, C_RDH, 0);
    hw_wr32(r, C_RDT, 0);
    hw_wr32(r, C_SRRCTL, SRRCTL_2K | SRRCTL_HDR_256 | SRRCTL_ADV_ONEBUF);
    hw_wr32(r, C_RXDCTL, RXDCTL_VAL | QCTL_ENABLE);
    /* The bit reads back set once the queue runs; look after a moment. */
    p->delay_us(p->ctx, 10);
    if (!hw_wait32(p, r, C_RXDCTL, QCTL_ENABLE, QCTL_ENABLE, QUEUE_US))
        return "RX queue did not enable";
    hw_wr32(r, C_RDT, RXN - 1);
    g->rx_next = 0;
    hw_wr32(r, C_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);

    hw_wr32(r, C_TXDCTL, 0);
    hw_wr32(r, C_TDLEN, TXN * 16);
    hw_wr32(r, C_TDBAL, (uint32_t) g->txd_phys);
    hw_wr32(r, C_TDBAH, (uint32_t) (g->txd_phys >> 32));
    hw_wr32(r, C_TDH, 0);
    hw_wr32(r, C_TDT, 0);
    hw_wr32(r, C_TXDCTL, TXDCTL_VAL | QCTL_ENABLE);
    p->delay_us(p->ctx, 10);
    if (!hw_wait32(p, r, C_TXDCTL, QCTL_ENABLE, QCTL_ENABLE, QUEUE_US))
        return "TX queue did not enable";
    g->tx_head = g->tx_tail = 0;
    hw_wr32(r, C_TCTL, TCTL_EN | TCTL_PSP | TCTL_CT | TCTL_COLD | TCTL_RTLC);
    return 0;
}

static void note_speed(hw_line_t *note, uint32_t status)
{
    if (status & STATUS_SPEED_2500)
        hw_line_s(note, ", 2500 Mb/s");
    else if (status & STATUS_SPEED_1000)
        hw_line_s(note, ", 1000 Mb/s");
    else if (status & STATUS_SPEED_100)
        hw_line_s(note, ", 100 Mb/s");
    else
        hw_line_s(note, ", 10 Mb/s");
}

hw_verdict_t igc_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r, hw_line_t *note)
{
    const igc_model_t *m = find_model(d->device);
    volatile void *regs = m && d->vendor == 0x8086 ? hw_pci_map_bar(p, d, 0) : 0;
    if (!m || d->vendor != 0x8086 || !regs) {
        hw_line_s(note, m && d->vendor == 0x8086 ? "BAR0 not assigned" : "unknown device id");
        return HW_N;
    }
    hw_line_s(note, m->name);
    hw_line_c(note, ' ');
    if (r->n_nic >= HW_MAX_NIC) {
        hw_line_s(note, "not registered: NIC table full");
        return HW_N;
    }
    igc_t *g = (igc_t *) p->dma_alloc(p->ctx, sizeof(igc_t), 64, 0);
    if (!g) {
        hw_line_s(note, "out of DMA memory");
        return HW_F;
    }
    g->p = p;
    g->r = regs;
    hw_pci_enable(p, d);
    if (hw_rd32(regs, C_STATUS) == 0xFFFFFFFFu) {
        hw_line_s(note, "registers read all-ones");
        return HW_F;
    }

    /* 1. Quiesce (firmware may have left DMA running), reset. */
    hw_wr32(regs, C_CTRL, hw_rd32(regs, C_CTRL) | CTRL_GIO_MASTER_DISABLE);
    hw_wait32(p, regs, C_STATUS, STATUS_GIO_MASTER_EN, 0, MASTER_US);
    irq_off(g);
    hw_wr32(regs, C_RCTL, 0);
    hw_wr32(regs, C_TCTL, TCTL_PSP);
    p->delay_us(p->ctx, 10000);
    hw_wr32(regs, C_CTRL, hw_rd32(regs, C_CTRL) | CTRL_RST);
    p->delay_us(p->ctx, 5000);
    if (!hw_wait32(p, regs, C_CTRL, CTRL_RST, 0, RESET_US)) {
        hw_line_s(note, "reset timed out");
        return HW_F;
    }
    /* NVM auto-load; not gating (Linux: no EEPROM still gets link). */
    hw_wait32(p, regs, C_EECD, EECD_AUTO_RD, EECD_AUTO_RD, AUTORD_US);
    irq_off(g);
    hw_wr32(regs, C_CTRL_EXT, hw_rd32(regs, C_CTRL_EXT) | CTRL_EXT_DRV_LOAD);

    /* 2. Station address, as the NVM loader left it in RAR[0]. */
    uint8_t *mac = g->nic.mac;
    uint32_t ral = hw_rd32(regs, C_RAL0), rah = hw_rd32(regs, C_RAH0);
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
    hw_wr32(regs, C_RAL0, ral);
    hw_wr32(regs, C_RAH0, (rah & 0xFFFF) | RAH_AV);
    for (uint32_t i = 1; i < RAR_ENTRIES; i++) {
        hw_wr32(regs, C_RAL0 + 8 * i, 0);
        hw_wr32(regs, C_RAH0 + 8 * i, 0);
    }
    for (uint32_t i = 0; i < MTA_WORDS; i++) hw_wr32(regs, C_MTA + 4 * i, 0);

    /* 3. Link up: MAC follows the PHY (SLU, nothing forced), the PHY
     * restarts autonegotiation. Then the queues. */
    uint32_t ctrl = hw_rd32(regs, C_CTRL);
    ctrl &=
        ~(CTRL_GIO_MASTER_DISABLE | CTRL_FRCSPD | CTRL_FRCDPX | CTRL_RST | CTRL_VME | CTRL_PHY_RST);
    hw_wr32(regs, C_CTRL, ctrl | CTRL_SLU);
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
    g->nic.link_up = (uint8_t) hw_wait32(p, regs, C_STATUS, STATUS_LU, STATUS_LU, LINK_US);

    hw_nic_t *n = &g->nic;
    n->name[0] = 'e';
    n->name[1] = 't';
    n->name[2] = 'h';
    n->name[3] = (char) ('0' + r->n_nic);
    n->name[4] = 0;
    n->drv = g;
    n->send = igc_send;
    n->recv = igc_recv;
    hw_line_s(note, n->link_up ? " link up as " : " link down as ");
    hw_line_s(note, n->name);
    if (n->link_up) note_speed(note, hw_rd32(regs, C_STATUS));
    if (!phy_ok) hw_line_s(note, " (PHY did not answer)");
    hw_reg_nic(r, n);
    return HW_T;
}
