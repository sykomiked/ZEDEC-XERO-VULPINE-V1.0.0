/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* e1000.c — Intel PRO/1000 family with LEGACY descriptors, polled.
 *
 * One programming model covers three generations:
 *   8254x  82540/82545/82546 PCI parts (QEMU "e1000" is an 82540EM)
 *   82574  82574L/LA PCIe (QEMU "e1000e")
 *   PCH    the MAC inside Intel chipsets with an 8256x/8257x/I21x PHY:
 *          ICH9 82566/82567, PCH 82577/82578, PCH2 82579, I217, I218, I219.
 *          These keep the legacy descriptor rings and the 8254x register map
 *          for everything this driver touches. NOT TESTED ON HARDWARE: the ids
 *          come from the Linux e1000e table and the init follows the Intel
 *          datasheets as far as a polled driver needs them.
 *
 * Bring-up (8254x software developer's manual, ch. 14; 82574 datasheet 4.6):
 *   reset (CTRL.RST, wait for it to self-clear), mask every interrupt, read
 *   the MAC from RAL0/RAH0 (from the EEPROM through EERD when the 8254x/82574
 *   left RAL0 empty), set link up (CTRL.SLU + ASDE), one receive ring of 2 KB
 *   buffers and one transmit ring, both legacy 16-byte descriptors.
 *
 * Receive polls the DD bit of the next descriptor and hands it straight back
 * (RDT). Transmit copies the frame into the slot's own buffer, sets
 * EOP|IFCS|RS and waits (bounded) for DD. Every wait goes through delay_us.
 *
 * Verdicts: HW_N no BAR / registry full; HW_F registers read all-ones, reset
 * never completes, the EEPROM never answers, no MAC, out of DMA memory; HW_B
 * a MAC with the group (multicast) bit set, which no station address can
 * have. A cable that is not plugged in is HW_T with "link down". */
#include "hwdrv.h"

/* ---- registers ------------------------------------------------------------ */
#define E_CTRL     0x0000
#define E_STATUS   0x0008
#define E_EECD     0x0010
#define E_EERD     0x0014
#define E_MDIC     0x0020
#define E_CTRL_EXT 0x0018
#define E_ICR      0x00C0
#define E_IMC      0x00D8
#define E_RCTL     0x0100
#define E_TCTL     0x0400
#define E_TIPG     0x0410
#define E_RDBAL    0x2800
#define E_RDBAH    0x2804
#define E_RDLEN    0x2808
#define E_RDH      0x2810
#define E_RDT      0x2818
#define E_TDBAL    0x3800
#define E_TDBAH    0x3804
#define E_TDLEN    0x3808
#define E_TDH      0x3810
#define E_TDT      0x3818
#define E_TXDCTL   0x3828
#define E_TARC0    0x3840
#define E_RFCTL    0x5008
#define E_MTA      0x5200
#define E_RAL0     0x5400
#define E_RAH0     0x5404
#define E_GCR      0x5B00
#define E_GCR2     0x5B64

#define CTRL_LRST    (1u << 3)
#define CTRL_ASDE    (1u << 5)
#define CTRL_SLU     (1u << 6)
#define CTRL_ILOS    (1u << 7)
#define CTRL_FRCSPD  (1u << 11)
#define CTRL_FRCDPLX (1u << 12)
#define CTRL_RST     (1u << 26)
#define CTRL_VME     (1u << 30)
#define CTRL_PHY_RST (1u << 31)

/* MDIC: PHY register access. The 8254x and 82574 PHY sits at address 1; the
 * PCH parts' PHY needs the software/firmware semaphore, which this driver
 * does not take, so their PHY is left alone. */
#define MDIC_PHY1      (1u << 21)
#define MDIC_OP_WRITE  (1u << 26)
#define MDIC_READY     (1u << 28)
#define MDIC_ERROR     (1u << 30)
#define BMCR_PDOWN     0x0800u /* PHY control register 0: power down */
#define MDIC_US        10000u

#define STATUS_LU            (1u << 1)
#define STATUS_FUNC_1        (1u << 2)
#define STATUS_LAN_INIT_DONE (1u << 9)  /* PCH */
#define EECD_AUTO_RD         (1u << 9)  /* 82574 / PCH */
#define CTRL_EXT_B22         (1u << 22) /* "must be 1" on 82574 and PCH */
#define CTRL_EXT_B23         (1u << 23) /* "must be 0" on 82574 */
#define CTRL_EXT_DRV_LOAD    (1u << 28) /* tell the management engine we own it */

#define RCTL_EN    (1u << 1)
#define RCTL_BAM   (1u << 15)
#define RCTL_SECRC (1u << 26) /* BSIZE 00 = 2048 bytes */

#define TCTL_EN   (1u << 1)
#define TCTL_PSP  (1u << 3)
#define TCTL_CT   (0x0Fu << 4)
#define TCTL_COLD (0x40u << 12) /* full duplex */
#define TCTL_RTLC (1u << 24)    /* 82571 and later */

#define TIPG_8254X  (10u | (8u << 10) | (6u << 20))
#define TIPG_E1000E (8u | (8u << 10) | (6u << 20))

#define RAH_AV (1u << 31)

/* legacy descriptors */
#define RXD_STATUS 12
#define RXD_ERRORS 13
#define RXD_DD     0x01
#define RXD_EOP    0x02
#define RXERR_MASK 0x97 /* CE | SE | SEQ | CXE | RXE; checksum errors are off */
#define TXD_CMD    11
#define TXD_STATUS 12
#define TXCMD_EOP  0x01
#define TXCMD_IFCS 0x02
#define TXCMD_RS   0x08
#define TXD_DD     0x01

#define RXN       32
#define TXN       16
#define BUF       2048u
#define MAX_FRAME 1514u /* no FCS, no VLAN tag */

#define RESET_US   100000u
#define EEPROM_US  10000u
#define TX_US      50000u
#define LINK_US    2500000u
#define AUTORD_US  20000u
#define LANINIT_US 150000u

enum { K_8254X, K_82574, K_PCH };

/* Model table: device id, generation, name for the boot line. The PCH rows
 * (K_PCH) are not tested on hardware. */
#define E1000_MODELS(X)                                                                            \
    X(0x100E, K_8254X, "82540EM")                                                                  \
    X(0x1015, K_8254X, "82540EM")                                                                  \
    X(0x1016, K_8254X, "82540EP")                                                                  \
    X(0x1017, K_8254X, "82540EP")                                                                  \
    X(0x101E, K_8254X, "82540EP")                                                                  \
    X(0x100F, K_8254X, "82545EM")                                                                  \
    X(0x1026, K_8254X, "82545GM")                                                                  \
    X(0x1010, K_8254X, "82546EB")                                                                  \
    X(0x10D3, K_82574, "82574L")                                                                   \
    X(0x10F6, K_82574, "82574LA")                                                                  \
    X(0x10BD, K_PCH, "82566DM-2")                                                                  \
    X(0x294C, K_PCH, "82566DC-2")                                                                  \
    X(0x10F5, K_PCH, "82567LM")                                                                    \
    X(0x10EA, K_PCH, "82577LM")                                                                    \
    X(0x10EB, K_PCH, "82577LC")                                                                    \
    X(0x10EF, K_PCH, "82578DM")                                                                    \
    X(0x10F0, K_PCH, "82578DC")                                                                    \
    X(0x1502, K_PCH, "82579LM")                                                                    \
    X(0x1503, K_PCH, "82579V")                                                                     \
    X(0x153A, K_PCH, "I217-LM")                                                                    \
    X(0x153B, K_PCH, "I217-V")                                                                     \
    X(0x155A, K_PCH, "I218-LM")                                                                    \
    X(0x1559, K_PCH, "I218-V")                                                                     \
    X(0x15A0, K_PCH, "I218-LM")                                                                    \
    X(0x15A1, K_PCH, "I218-V")                                                                     \
    X(0x15A2, K_PCH, "I218-LM")                                                                    \
    X(0x15A3, K_PCH, "I218-V")                                                                     \
    X(0x156F, K_PCH, "I219-LM")                                                                    \
    X(0x1570, K_PCH, "I219-V")                                                                     \
    X(0x15B7, K_PCH, "I219-LM")                                                                    \
    X(0x15B8, K_PCH, "I219-V")                                                                     \
    X(0x15B9, K_PCH, "I219-LM")                                                                    \
    X(0x15D7, K_PCH, "I219-LM")                                                                    \
    X(0x15D8, K_PCH, "I219-V")                                                                     \
    X(0x15E3, K_PCH, "I219-LM")                                                                    \
    X(0x15D6, K_PCH, "I219-V")                                                                     \
    X(0x15BD, K_PCH, "I219-LM")                                                                    \
    X(0x15BE, K_PCH, "I219-V")                                                                     \
    X(0x15BB, K_PCH, "I219-LM")                                                                    \
    X(0x15BC, K_PCH, "I219-V")                                                                     \
    X(0x15DF, K_PCH, "I219-LM")                                                                    \
    X(0x15E0, K_PCH, "I219-V")                                                                     \
    X(0x15E1, K_PCH, "I219-LM")                                                                    \
    X(0x15E2, K_PCH, "I219-V")                                                                     \
    X(0x0D4E, K_PCH, "I219-LM")                                                                    \
    X(0x0D4F, K_PCH, "I219-V")                                                                     \
    X(0x0D4C, K_PCH, "I219-LM")                                                                    \
    X(0x0D4D, K_PCH, "I219-V")                                                                     \
    X(0x0D53, K_PCH, "I219-LM")                                                                    \
    X(0x0D55, K_PCH, "I219-V")                                                                     \
    X(0x15FB, K_PCH, "I219-LM")                                                                    \
    X(0x15FC, K_PCH, "I219-V")                                                                     \
    X(0x15F9, K_PCH, "I219-LM")                                                                    \
    X(0x15FA, K_PCH, "I219-V")                                                                     \
    X(0x1A1E, K_PCH, "I219-LM")                                                                    \
    X(0x1A1F, K_PCH, "I219-V")                                                                     \
    X(0x1A1C, K_PCH, "I219-LM")                                                                    \
    X(0x1A1D, K_PCH, "I219-V")                                                                     \
    X(0x550A, K_PCH, "I219-LM")                                                                    \
    X(0x550B, K_PCH, "I219-V")                                                                     \
    X(0x550C, K_PCH, "I219-LM")                                                                    \
    X(0x550D, K_PCH, "I219-V")

#define E1000_ID(did, kind, name) {0x8086, did},
const hw_id_t e1000_ids[] = {E1000_MODELS(E1000_ID){0, 0}};

typedef struct e1k_model {
    uint16_t did;
    uint8_t kind;
    const char *name;
} e1k_model_t;
#define E1000_ROW(did, kind, name) {did, kind, name},
static const e1k_model_t e1k_models[] = {E1000_MODELS(E1000_ROW){0, 0, 0}};

typedef struct e1k {
    const hw_plat_t *p;
    volatile void *r;
    uint8_t kind;
    uint8_t rx_discard; /* dropping the rest of a frame that spanned buffers */
    volatile uint8_t *rxd, *txd;
    uint8_t *rxbuf, *txbuf;
    uint64_t rxd_phys, txd_phys, rxbuf_phys, txbuf_phys;
    uint32_t rx_next, tx_next;
    uint8_t tx_busy[TXN]; /* slot handed to the NIC, DD not yet seen */
    hw_nic_t nic;
} e1k_t;

static void put16(volatile uint8_t *b, uint32_t off, uint32_t v)
{
    b[off] = (uint8_t) v;
    b[off + 1] = (uint8_t) (v >> 8);
}

static void put32(volatile uint8_t *b, uint32_t off, uint32_t v)
{
    put16(b, off, v);
    put16(b, off + 2, v >> 16);
}

static void put64(volatile uint8_t *b, uint32_t off, uint64_t v)
{
    put32(b, off, (uint32_t) v);
    put32(b, off + 4, (uint32_t) (v >> 32));
}

static uint32_t get16(const volatile uint8_t *b, uint32_t off)
{
    return (uint32_t) b[off] | ((uint32_t) b[off + 1] << 8);
}

static const e1k_model_t *find_model(uint16_t did)
{
    for (const e1k_model_t *m = e1k_models; m->name; m++)
        if (m->did == did) return m;
    return 0;
}

static int link_is_up(e1k_t *e)
{
    return (hw_rd32(e->r, E_STATUS) & STATUS_LU) != 0;
}

static int e1k_recv(hw_nic_t *n, void *buf, uint32_t max)
{
    e1k_t *e = (e1k_t *) n->drv;
    if (!buf || max == 0) return 0;
    for (uint32_t guard = 0; guard < RXN; guard++) {
        uint32_t i = e->rx_next;
        volatile uint8_t *d = e->rxd + i * 16;
        uint8_t st = d[RXD_STATUS];
        if (!(st & RXD_DD)) return 0;
        hw_wmb(); /* read the rest of the descriptor only after DD */
        uint32_t len = get16(d, 8);
        uint8_t err = d[RXD_ERRORS];
        int good = !e->rx_discard && (st & RXD_EOP) && !(err & RXERR_MASK) && len > 0 && len <= BUF;
        e->rx_discard = (st & RXD_EOP) ? 0 : 1;
        uint32_t got = 0;
        if (good) {
            got = len < max ? len : max;
            hw_memcpy(buf, e->rxbuf + i * BUF, got);
        }
        /* Give the descriptor back: same buffer, status cleared. */
        d[RXD_STATUS] = 0;
        d[RXD_ERRORS] = 0;
        hw_wmb();
        hw_wr32(e->r, E_RDT, i);
        e->rx_next = (i + 1) % RXN;
        if (got) return (int) got;
    }
    return 0;
}

static int e1k_send(hw_nic_t *n, const void *frame, uint32_t len)
{
    e1k_t *e = (e1k_t *) n->drv;
    if (!frame || len == 0 || len > MAX_FRAME) return -1;
    uint32_t i = e->tx_next;
    volatile uint8_t *d = e->txd + i * 16;
    /* At most TXN-1 frames outstanding: a full ring (TDT == TDH) would read
     * as empty to the NIC. Slot i+1 still busy means the ring is full. */
    uint32_t nx = (i + 1) % TXN;
    if (e->tx_busy[nx] && !(e->txd[nx * 16 + TXD_STATUS] & TXD_DD)) return -2;
    hw_memcpy(e->txbuf + i * BUF, frame, len);
    put64(d, 0, e->txbuf_phys + (uint64_t) i * BUF);
    put16(d, 8, len);
    d[10] = 0; /* CSO */
    d[TXD_CMD] = TXCMD_EOP | TXCMD_IFCS | TXCMD_RS;
    d[TXD_STATUS] = 0;
    d[13] = 0; /* CSS */
    put16(d, 14, 0);
    e->tx_busy[i] = 1;
    e->tx_next = (i + 1) % TXN;
    hw_wmb();
    hw_wr32(e->r, E_TDT, e->tx_next);
    for (uint32_t waited = 0;; waited += 10) {
        if (d[TXD_STATUS] & TXD_DD) {
            e->tx_busy[i] = 0;
            n->link_up = (uint8_t) link_is_up(e);
            return 0;
        }
        if (waited >= TX_US) break;
        e->p->delay_us(e->p->ctx, 10);
    }
    /* No DD: with the link down the frame waits in the ring for a cable,
     * which is "queued"; with the link up the transmitter is stuck. */
    n->link_up = (uint8_t) link_is_up(e);
    return n->link_up ? -1 : 0;
}

/* One 16-bit EEPROM word through EERD. The 8254x put DONE at bit 4 and the
 * address at bit 8; the 82574 at bit 1 and bit 2. 1 = read. */
static int eerd_read(e1k_t *e, uint32_t word, uint16_t *out)
{
    uint32_t done = e->kind == K_8254X ? (1u << 4) : (1u << 1);
    uint32_t shift = e->kind == K_8254X ? 8 : 2;
    hw_wr32(e->r, E_EERD, (word << shift) | 1u);
    if (!hw_wait32(e->p, e->r, E_EERD, done, done, EEPROM_US)) return 0;
    *out = (uint16_t) (hw_rd32(e->r, E_EERD) >> 16);
    return 1;
}

static void irq_off(e1k_t *e);

/* hw_nic_t.phy_off: receiver and transmitter off, the MAC's link dropped,
 * then the PHY powered down through MDIC (not on PCH parts). */
static int e1k_phy_off(hw_nic_t *n)
{
    e1k_t *e = (e1k_t *) n->drv;
    volatile void *regs = e->r;
    irq_off(e);
    hw_wr32(regs, E_RCTL, 0);
    hw_wr32(regs, E_TCTL, 0);
    hw_wr32(regs, E_CTRL, hw_rd32(regs, E_CTRL) & ~CTRL_SLU);
    n->link_up = 0;
    if (e->kind == K_PCH) return 1;
    hw_wr32(regs, E_MDIC, MDIC_OP_WRITE | MDIC_PHY1 | BMCR_PDOWN);
    if (!hw_wait32(e->p, regs, E_MDIC, MDIC_READY, MDIC_READY, MDIC_US)) return -1;
    return (hw_rd32(regs, E_MDIC) & MDIC_ERROR) ? -1 : 0;
}

static void irq_off(e1k_t *e)
{
    hw_wr32(e->r, E_IMC, 0xFFFFFFFFu);
    (void) hw_rd32(e->r, E_ICR); /* read-to-clear */
}

/* Generation specific bits around reset (Linux e1000e init_hw_bits and the
 * 82574 / PCH datasheets). None of them gates anything: on hardware without
 * the status bit the wait simply runs out. */
static void post_reset(e1k_t *e)
{
    if (e->kind == K_8254X) return;
    hw_wait32(e->p, e->r, E_EECD, EECD_AUTO_RD, EECD_AUTO_RD, AUTORD_US);
    uint32_t ext = hw_rd32(e->r, E_CTRL_EXT) | CTRL_EXT_B22 | CTRL_EXT_DRV_LOAD;
    if (e->kind == K_82574) {
        ext &= ~CTRL_EXT_B23;
        hw_wr32(e->r, E_GCR, hw_rd32(e->r, E_GCR) | (1u << 22));
        hw_wr32(e->r, E_GCR2, hw_rd32(e->r, E_GCR2) | 1u);
    } else {
        if (hw_wait32(e->p, e->r, E_STATUS, STATUS_LAN_INIT_DONE, STATUS_LAN_INIT_DONE, LANINIT_US))
            hw_wr32(e->r, E_STATUS, hw_rd32(e->r, E_STATUS) & ~STATUS_LAN_INIT_DONE);
        hw_wr32(e->r, E_TARC0,
                hw_rd32(e->r, E_TARC0) | (1u << 23) | (1u << 24) | (1u << 26) | (1u << 27));
        hw_wr32(e->r, E_RFCTL, hw_rd32(e->r, E_RFCTL) | (1u << 6) | (1u << 7));
    }
    hw_wr32(e->r, E_CTRL_EXT, ext);
}

static void note_mac(hw_line_t *note, const uint8_t *mac)
{
    for (int i = 0; i < 6; i++) {
        if (i) hw_line_c(note, ':');
        hw_line_x(note, mac[i], 2);
    }
}

static int alloc_rings(e1k_t *e)
{
    const hw_plat_t *p = e->p;
    e->rxd = (volatile uint8_t *) p->dma_alloc(p->ctx, RXN * 16, 128, &e->rxd_phys);
    e->txd = (volatile uint8_t *) p->dma_alloc(p->ctx, TXN * 16, 128, &e->txd_phys);
    e->rxbuf = (uint8_t *) p->dma_alloc(p->ctx, RXN * BUF, 4096, &e->rxbuf_phys);
    e->txbuf = (uint8_t *) p->dma_alloc(p->ctx, TXN * BUF, 4096, &e->txbuf_phys);
    return e->rxd && e->txd && e->rxbuf && e->txbuf;
}

static void setup_rings(e1k_t *e)
{
    volatile void *r = e->r;
    for (uint32_t i = 0; i < RXN; i++) {
        volatile uint8_t *d = e->rxd + i * 16;
        put64(d, 0, e->rxbuf_phys + (uint64_t) i * BUF);
        put32(d, 8, 0);
        put32(d, 12, 0);
    }
    hw_wr32(r, E_RDBAL, (uint32_t) e->rxd_phys);
    hw_wr32(r, E_RDBAH, (uint32_t) (e->rxd_phys >> 32));
    hw_wr32(r, E_RDLEN, RXN * 16);
    hw_wr32(r, E_RDH, 0);
    hw_wr32(r, E_RDT, RXN - 1); /* head == tail means empty: one slot stays unused */
    e->rx_next = 0;
    hw_wr32(r, E_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);

    hw_wr32(r, E_TDBAL, (uint32_t) e->txd_phys);
    hw_wr32(r, E_TDBAH, (uint32_t) (e->txd_phys >> 32));
    hw_wr32(r, E_TDLEN, TXN * 16);
    hw_wr32(r, E_TDH, 0);
    hw_wr32(r, E_TDT, 0);
    e->tx_next = 0;
    if (e->kind != K_8254X) /* GRAN = descriptors, WTHRESH 1, bit 22 must be 1 */
        hw_wr32(r, E_TXDCTL,
                (hw_rd32(r, E_TXDCTL) & ~(0x3Fu << 16)) | (1u << 16) | (1u << 22) | (1u << 24));
    uint32_t tctl = TCTL_EN | TCTL_PSP | TCTL_CT | TCTL_COLD;
    if (e->kind != K_8254X) tctl |= TCTL_RTLC;
    hw_wr32(r, E_TCTL, tctl);
    hw_wr32(r, E_TIPG, e->kind == K_8254X ? TIPG_8254X : TIPG_E1000E);
}

hw_verdict_t e1000_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r,
                        hw_line_t *note)
{
    const e1k_model_t *m = find_model(d->device);
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
    e1k_t *e = (e1k_t *) p->dma_alloc(p->ctx, sizeof(e1k_t), 64, 0);
    if (!e) {
        hw_line_s(note, "out of DMA memory");
        return HW_F;
    }
    e->p = p;
    e->r = regs;
    e->kind = m->kind;
    hw_pci_enable(p, d);
    if (hw_rd32(regs, E_STATUS) == 0xFFFFFFFFu) {
        hw_line_s(note, "registers read all-ones");
        return HW_F;
    }

    /* 1. Reset. Interrupts masked before (so nothing fires mid-reset) and
     * again after (reset restores the defaults). The settle time before the
     * first poll is what Linux uses per generation. */
    irq_off(e);
    hw_wr32(regs, E_RCTL, 0);
    hw_wr32(regs, E_TCTL, 0);
    hw_wr32(regs, E_CTRL, hw_rd32(regs, E_CTRL) | CTRL_RST);
    p->delay_us(p->ctx, e->kind == K_8254X ? 5000 : e->kind == K_82574 ? 10000 : 20000);
    if (!hw_wait32(p, regs, E_CTRL, CTRL_RST, 0, RESET_US)) {
        hw_line_s(note, "reset timed out");
        return HW_F;
    }
    irq_off(e);
    post_reset(e);

    /* 2. Station address. */
    uint8_t *mac = e->nic.mac;
    uint32_t ral = hw_rd32(regs, E_RAL0), rah = hw_rd32(regs, E_RAH0);
    if (ral == 0 && (rah & 0xFFFF) == 0 && e->kind != K_PCH) {
        uint16_t w[3];
        for (uint32_t i = 0; i < 3; i++) {
            if (!eerd_read(e, i, &w[i])) {
                hw_line_s(note, "EEPROM read timed out");
                return HW_F;
            }
        }
        ral = (uint32_t) w[0] | ((uint32_t) w[1] << 16);
        rah = w[2];
        /* 82546: both ports share one EEPROM; port B flips the last bit. */
        if (d->device == 0x1010 && (hw_rd32(regs, E_STATUS) & STATUS_FUNC_1)) rah ^= 0x0100;
    }
    for (uint32_t i = 0; i < 4; i++) mac[i] = (uint8_t) (ral >> (8 * i));
    mac[4] = (uint8_t) rah;
    mac[5] = (uint8_t) (rah >> 8);
    if ((ral | (rah & 0xFFFF)) == 0) {
        hw_line_s(note, "no MAC address");
        return HW_F;
    }
    note_mac(note, mac);
    if (mac[0] & 1) {
        hw_line_s(note, " has the group bit set");
        return HW_B;
    }
    hw_wr32(regs, E_RAL0, ral);
    hw_wr32(regs, E_RAH0, (rah & 0xFFFF) | RAH_AV);
    for (uint32_t i = 0; i < 128; i++) hw_wr32(regs, E_MTA + 4 * i, 0);

    /* 3. Rings. */
    if (!alloc_rings(e)) {
        hw_line_s(note, " out of DMA memory");
        return HW_F;
    }
    setup_rings(e);

    /* 4. Link: auto speed detection, no forced speed/duplex, PHY out of
     * reset. The PHY autonegotiates on its own; wait a bounded time. */
    uint32_t ctrl = hw_rd32(regs, E_CTRL);
    ctrl &=
        ~(CTRL_LRST | CTRL_ILOS | CTRL_FRCSPD | CTRL_FRCDPLX | CTRL_RST | CTRL_VME | CTRL_PHY_RST);
    hw_wr32(regs, E_CTRL, ctrl | CTRL_SLU | CTRL_ASDE);
    e->nic.link_up = (uint8_t) hw_wait32(p, regs, E_STATUS, STATUS_LU, STATUS_LU, LINK_US);

    hw_nic_t *n = &e->nic;
    n->name[0] = 'e';
    n->name[1] = 't';
    n->name[2] = 'h';
    n->name[3] = (char) ('0' + r->n_nic);
    n->name[4] = 0;
    n->drv = e;
    n->send = e1k_send;
    n->recv = e1k_recv;
    n->phy_off = e1k_phy_off;
    n->on_pci = 1;
    n->bdf = d->bdf;
    hw_line_s(note, n->link_up ? " link up as " : " link down as ");
    hw_line_s(note, n->name);
    hw_reg_nic(r, n); /* cannot fail: checked above */
    return HW_T;
}
