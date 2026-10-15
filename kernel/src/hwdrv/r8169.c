/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* r8169.c — Realtek RTL8169 / RTL8168 / RTL8111 / RTL8101 Ethernet and the
 * RTL8125 2.5 GbE family, polled.
 *
 * UNTESTED ON HARDWARE AND UNTESTED IN QEMU: QEMU has no model of these
 * chips (only the older RTL8139, a different design). Written against the
 * public RTL8169S / RTL8168 / RTL8101 datasheet register layout (and, for
 * the RTL8125, the mainline Linux r8169 driver, v7.2 r8169_main.c) and
 * checked only against the host fakes in test/test_r8169.c and
 * test/test_r8125.c.
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
 * The transmit ring is bounded by free-running produce/reclaim counters:
 * tx_head - tx_tail >= TXN is backpressure (-2). All TXN descriptors can be
 * outstanding because OWN, not a head/tail pair, is the handoff.
 *
 * RTL8125 (10EC:8125, and the Killer E3000, 10EC:3000, which Linux binds
 * the same way): the register window is the 64 KB memory BAR. The chip is
 * identified from TxConfig (XID = TxConfig >> 20 & 0xFCF, matched under
 * 0x7CF against Linux's rtl_chip_infos 8125 rows: 8125A/B/D/K/BP/CP and
 * RTL9151A); an id that says 8125 over an XID that does not (an RTL8126
 * behind a 0x8125 id, say) is a contradiction, HW_B. What differs from the
 * 8168, and what this does about it:
 *   - interrupts: 32-bit IntrMask_8125 0x38 / IntrStatus_8125 0x3C (0x38 is
 *     TxPoll on the older chips) and INT_CFG0_8125 0x34; all masked;
 *   - transmit kick: TxPoll_8125 0x90, 16-bit, bit 0 = queue 0;
 *   - before reset, receive is closed and gated (MISC.RXDV_GATED_EN) and
 *     the TX/RX FIFOs drain (MCU.RXTX_EMPTY, bounded, not gating), as
 *     rtl8169_cleanup does; after it the chip is taken out of out-of-band
 *     mode (MCU.NOW_IS_OOB), and receive is ungated once the rings are set;
 *   - one queue: RSS_CTRL_8125 0x4500 = 0, Q_NUM_CTRL_8125 0x4800 = 0, and
 *     the legacy transmit descriptor format (MAC OCP 0xEB58 bit 0 clear,
 *     through OCPDR 0xB0), which the rings here use;
 *   - RxConfig: RX_FETCH_DFLT_8125 plus RX_PAUSE_SLOT_ON on 8125B and later,
 *     VLAN stripping (bits 22, 23) off; MaxTxPacketSize is not written;
 *   - the station address comes from MAC0_BKP 0x19E0 first, then IDR0-5
 *     (Linux's order), and is written back to IDR0-5.
 *
 * What real chips are likely to need that this does not do: the per-revision
 * PHY and ePHY fix-ups, MAC OCP tuning and firmware patches the Linux driver
 * applies (the PHY autonegotiates from its power-on defaults here). Link is
 * PHYstatus.LinkStatus on every chip; Linux reads the 8125's link through
 * its PHY driver instead, so on the 8125 that bit follows the Realtek
 * vendor driver's convention and is not verified against mainline. */
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
#define R_OCPDR     0xB0
#define R_MCU       0xD3
#define R_MISC      0xF0
/* RTL8125 */
#define R_INTCFG0_8125   0x34
#define R_INTRMASK_8125  0x38
#define R_INTRSTAT_8125  0x3C
#define R_TXPOLL_8125    0x90
#define R_MAC0_BKP       0x19E0
#define R_RSS_CTRL_8125  0x4500
#define R_QNUM_CTRL_8125 0x4800
#define REGS_8125        0x10000u /* the memory BAR must cover the 8125 registers */

#define CMD_RESET  0x10
#define CMD_RXENB  0x08
#define CMD_TXENB  0x04
#define TXPOLL_NPQ 0x40
#define CFG_UNLOCK 0xC0
#define CFG_LOCK   0x00
#define PHY_LINK   0x02
#define CP_RXCHK   0x0020
#define CP_RXVLAN  0x0040

#define MCU_NOW_IS_OOB  0x80
#define MCU_RXTX_EMPTY  0x30
#define MISC_RXDV_GATED (1u << 19)
#define OCP_FLAG        (1u << 31)
#define OCP_TXDESC_FMT  0xEB58 /* bit 0: new (8125) transmit descriptor format */

#define RX_ACCEPT_MYPHYS (1u << 1)
#define RX_ACCEPT_BCAST  (1u << 3)
#define RX_DMA_UNLIM     (7u << 8)
#define RX_FIFO_NONE     (7u << 13) /* 8169: no RX FIFO threshold */
#define RX_128INT        (1u << 15) /* 8168/8101 */
#define TX_DMA_UNLIM     (7u << 8)
#define TX_IFG_NORMAL    (3u << 24)
#define RX_ACCEPT_MASK   0x3Fu
#define RX_PAUSE_SLOT_ON (1u << 11) /* 8125B and later */
#define RX_FETCH_8125    (8u << 27)

#define D_OWN (1u << 31)
#define D_EOR (1u << 30)
#define D_FS  (1u << 29)
#define D_LS  (1u << 28)
#define D_RES (1u << 21) /* receive error summary */
#define D_LEN 0x3FFFu

#define RXN       32u
#define TXN       16u
#define BUF       2048u
#define MAX_FRAME 1514u
#define MIN_FRAME 60u

_Static_assert((RXN & (RXN - 1u)) == 0 && (TXN & (TXN - 1u)) == 0, "r8169 rings: powers of two");

#define RESET_US 100000u
#define FIFO_US  4200u /* Linux: 42 polls of 100 us */
#define TX_US    50000u
#define LINK_US  2500000u

enum { K_8169, K_8168, K_8101, K_8125 };

#define R8169_MODELS(X)                                                                            \
    X(0x8169, K_8169, "RTL8169")                                                                   \
    X(0x8167, K_8169, "RTL8169SC")                                                                 \
    X(0x8168, K_8168, "RTL8168/8111")                                                              \
    X(0x8161, K_8168, "RTL8168/8111")                                                              \
    X(0x8136, K_8101, "RTL8101/8102")                                                              \
    X(0x8125, K_8125, "RTL8125")                                                                   \
    X(0x3000, K_8125, "Killer E3000")

#define R8169_ID(did, kind, name) {0x10EC, did},
const hw_id_t r8169_ids[] = {R8169_MODELS(R8169_ID){0, 0}};

typedef struct rtl_model {
    uint16_t did;
    uint8_t kind;
    const char *name;
} rtl_model_t;
#define R8169_ROW(did, kind, name) {did, kind, name},
static const rtl_model_t rtl_models[] = {R8169_MODELS(R8169_ROW){0, 0, 0}};

/* RTL8125 chip versions by TxConfig XID (Linux rtl_chip_infos, mask 0x7CF). */
typedef struct rtl_chip {
    uint16_t xid;
    uint8_t pause_slot; /* RX_PAUSE_SLOT_ON: every version after 8125A */
    const char *name;
} rtl_chip_t;
static const rtl_chip_t rtl8125_chips[] = {
    {0x609, 0, "RTL8125A"},  {0x641, 1, "RTL8125B"},  {0x688, 1, "RTL8125D"},
    {0x689, 1, "RTL8125D"},  {0x68A, 1, "RTL8125K"},  {0x68B, 1, "RTL9151A"},
    {0x708, 1, "RTL8125CP"}, {0x681, 1, "RTL8125BP"}, {0, 0, 0},
};

typedef struct rtl {
    const hw_plat_t *p;
    volatile void *r;
    uint8_t kind;
    uint8_t rx_discard;
    volatile uint8_t *rxd, *txd;
    uint8_t *rxbuf, *txbuf;
    uint64_t rxd_phys, txd_phys, rxbuf_phys, txbuf_phys;
    uint32_t rx_next;
    uint32_t tx_head, tx_tail; /* free running: produced, reclaimed */
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

static const rtl_chip_t *find_8125(uint32_t xid)
{
    for (const rtl_chip_t *c = rtl8125_chips; c->name; c++)
        if ((xid & 0x7CFu) == c->xid) return c;
    return 0;
}

/* MAC OCP register access through OCPDR (r8168_mac_ocp_read/write). The
 * read gives the chip a microsecond between command and data. */
static void ocp_write(rtl_t *t, uint32_t reg, uint16_t v)
{
    hw_wr32(t->r, R_OCPDR, OCP_FLAG | (reg << 15) | v);
}

static uint16_t ocp_read(rtl_t *t, uint32_t reg)
{
    hw_wr32(t->r, R_OCPDR, reg << 15);
    t->p->delay_us(t->p->ctx, 1);
    return (uint16_t) hw_rd32(t->r, R_OCPDR);
}

static void irq_off(rtl_t *t)
{
    if (t->kind == K_8125) {
        hw_wr32(t->r, R_INTRMASK_8125, 0);
        hw_wr32(t->r, R_INTRSTAT_8125, 0xFFFFFFFFu);
        hw_wr8(t->r, R_INTCFG0_8125, 0);
    } else {
        hw_wr16(t->r, R_INTRMASK, 0);
        hw_wr16(t->r, R_INTRSTAT, 0xFFFF);
    }
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

/* Advance tx_tail over every descriptor the chip has handed back. */
static void tx_reclaim(rtl_t *t)
{
    while (t->tx_tail != t->tx_head && !(get32(t->txd + (t->tx_tail % TXN) * 16, 0) & D_OWN))
        t->tx_tail++;
}

static int rtl_send(hw_nic_t *n, const void *frame, uint32_t len)
{
    rtl_t *t = (rtl_t *) n->drv;
    if (!frame || len == 0 || len > MAX_FRAME) return -1;
    tx_reclaim(t);
    if (t->tx_head - t->tx_tail >= TXN) return -2; /* every slot is the chip's */
    uint32_t i = t->tx_head % TXN;
    volatile uint8_t *d = t->txd + i * 16;
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
    t->tx_head++;
    hw_wmb();
    if (t->kind == K_8125)
        hw_wr16(t->r, R_TXPOLL_8125, 1); /* queue 0 */
    else
        hw_wr8(t->r, R_TXPOLL, TXPOLL_NPQ);
    for (uint32_t waited = 0;; waited += 10) {
        if (!(get32(d, 0) & D_OWN)) {
            tx_reclaim(t);
            n->link_up = (uint8_t) link_is_up(t);
            return 0;
        }
        if (waited >= TX_US) break;
        t->p->delay_us(t->p->ctx, 10);
    }
    n->link_up = (uint8_t) link_is_up(t);
    return n->link_up ? -1 : 0;
}

static volatile void *map_regs(const hw_plat_t *p, const hw_pci_dev_t *d, uint64_t min)
{
    for (uint32_t i = 0; i < 6; i++)
        if (d->bar[i] && !(d->bar_flags[i] & HW_BAR_IO) && d->bar_size[i] >= min)
            return hw_pci_map_bar(p, d, i);
    return 0;
}

/* The 8125's saved station address (MAC0_BKP), when it is a usable one. */
static int read_bkp_mac(rtl_t *t, uint8_t mac[6])
{
    uint32_t any = 0;
    for (uint32_t i = 0; i < 6; i++) {
        mac[i] = hw_rd8(t->r, R_MAC0_BKP + i);
        any |= mac[i];
    }
    return any && !(mac[0] & 1);
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
    const rtl_model_t *m = d->vendor == 0x10EC ? find_model(d->device) : 0;
    volatile void *regs = m ? map_regs(p, d, m->kind == K_8125 ? REGS_8125 : 256) : 0;
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
    uint32_t txc = hw_rd32(regs, R_TXCONFIG);
    if (txc == 0xFFFFFFFFu) {
        hw_line_s(note, "registers read all-ones");
        return HW_F;
    }
    const rtl_chip_t *chip = 0;
    if (t->kind == K_8125) {
        uint32_t xid = (txc >> 20) & 0xFCFu;
        chip = find_8125(xid);
        if (!chip) {
            hw_line_s(note, "TxConfig XID ");
            hw_line_x(note, xid, 3);
            hw_line_s(note, " is not an RTL8125");
            return HW_B;
        }
        hw_line_s(note, chip->name);
        hw_line_c(note, ' ');
    }

    /* 1. Quiet and reset. The 8125 first closes and gates receive and lets
     * its FIFOs drain (not gating: a chip that never reports empty is reset
     * anyway). */
    irq_off(t);
    if (t->kind == K_8125) {
        hw_wr32(regs, R_RXCONFIG, hw_rd32(regs, R_RXCONFIG) & ~RX_ACCEPT_MASK);
        hw_wr32(regs, R_MISC, hw_rd32(regs, R_MISC) | MISC_RXDV_GATED);
        p->delay_us(p->ctx, 2000);
        wait8(p, regs, R_MCU, MCU_RXTX_EMPTY, MCU_RXTX_EMPTY, FIFO_US);
    }
    hw_wr8(regs, R_CHIPCMD, 0);
    hw_wr8(regs, R_CHIPCMD, CMD_RESET);
    if (!wait8(p, regs, R_CHIPCMD, CMD_RESET, 0, RESET_US)) {
        hw_line_s(note, "reset timed out");
        return HW_F;
    }
    irq_off(t);
    if (t->kind == K_8125) hw_wr8(regs, R_MCU, (uint8_t) (hw_rd8(regs, R_MCU) & ~MCU_NOW_IS_OOB));

    /* 2. Station address (8125: the saved copy first, as Linux does). */
    uint8_t *mac = t->nic.mac;
    uint32_t any = 0;
    int from_bkp = t->kind == K_8125 && read_bkp_mac(t, mac);
    for (uint32_t i = 0; i < 6; i++) {
        if (!from_bkp) mac[i] = hw_rd8(regs, R_IDR0 + i);
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
    t->rx_next = t->tx_head = t->tx_tail = 0;

    hw_wr8(regs, R_CFG9346, CFG_UNLOCK);
    if (from_bkp) { /* rtl_rar_set: high half first */
        hw_wr32(regs, R_IDR0 + 4, (uint32_t) mac[4] | ((uint32_t) mac[5] << 8));
        hw_wr32(regs, R_IDR0,
                (uint32_t) mac[0] | ((uint32_t) mac[1] << 8) | ((uint32_t) mac[2] << 16) |
                    ((uint32_t) mac[3] << 24));
    }
    hw_wr16(regs, R_CPLUSCMD,
            (uint16_t) (hw_rd16(regs, R_CPLUSCMD) & ~(uint32_t) (CP_RXCHK | CP_RXVLAN)));
    hw_wr16(regs, R_RXMAXSIZE, (uint16_t) BUF);
    if (t->kind == K_8125) {
        hw_wr32(regs, R_RSS_CTRL_8125, 0);
        hw_wr16(regs, R_QNUM_CTRL_8125, 0);
        ocp_write(t, OCP_TXDESC_FMT, (uint16_t) (ocp_read(t, OCP_TXDESC_FMT) & ~1u));
    } else {
        hw_wr8(regs, R_MAXTXPKT, 0x3F);
    }
    hw_wr32(regs, R_TNPDS_HI, (uint32_t) (t->txd_phys >> 32));
    hw_wr32(regs, R_TNPDS_LO, (uint32_t) t->txd_phys);
    hw_wr32(regs, R_RDSAR_HI, (uint32_t) (t->rxd_phys >> 32));
    hw_wr32(regs, R_RDSAR_LO, (uint32_t) t->rxd_phys);
    hw_wr8(regs, R_CFG9346, CFG_LOCK);

    if (t->kind == K_8125) hw_wr32(regs, R_MISC, hw_rd32(regs, R_MISC) & ~MISC_RXDV_GATED);
    hw_wr8(regs, R_CHIPCMD, CMD_TXENB | CMD_RXENB);
    uint32_t rxc = RX_DMA_UNLIM | RX_ACCEPT_BCAST | RX_ACCEPT_MYPHYS;
    if (t->kind == K_8125)
        rxc |= RX_FETCH_8125 | (chip->pause_slot ? RX_PAUSE_SLOT_ON : 0);
    else
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
    hw_line_s(note, n->link_up ? " link up as " : " link down as ");
    hw_line_s(note, n->name);
    hw_reg_nic(r, n);
    return HW_T;
}
