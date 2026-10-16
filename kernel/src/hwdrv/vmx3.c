/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vmx3.c — VMware vmxnet3 (the UPT para-virtual NIC), polled, one queue.
 *
 * PCI 15AD:07B0, class 02/00. Two memory BARs, both little-endian dword
 * registers:
 *   BAR0  the pass-through window: interrupt masks at 0x000 + 8*i and the
 *         queue doorbells TXH 0x600 + 8*q, RXH1 0x800 + 8*q, RXH2 0xa00 + 8*q.
 *         A doorbell takes the producer index (the slot after the last one
 *         the driver made valid).
 *   BAR1  the control window: revision and UPT version selects, the physical
 *         address of the driver-shared block, the command register (write a
 *         command, read the same register for its result), the MAC address
 *         and the interrupt/event registers.
 *
 * Bring-up (the vmxnet3/UPT1 specification; register level):
 *   select the revision (VRRS bit field, write the number back) and UPT
 *   version 1, hand the device the driver-shared block through DSL/DSH —
 *   it carries the queue-shared area (ring pointers and lengths), the MAC
 *   filter mode and the multicast table — read the station address with
 *   GET_MACL/GET_MACH, fill both receive rings with buffers, then ENABLE.
 *   RXMODE/SET_FILTER/FILTER come after; GET_LINK reports the wire.
 *
 * Ownership is a generation bit per descriptor. Producer-side descriptors
 * are written with the bit cleared and published by flipping the SOP bit's
 * generation last; completion descriptors are valid while their generation
 * bit equals the driver's expected value, which flips each ring wrap.
 *
 * The driver is polled only: interrupts stay masked (ds.ictrl disables all
 * sources) and both ends of each ring are advanced in recv/send.
 *
 * Verdicts: HW_N no BARs / registry full; HW_F registers dead, unsupported
 * revision or UPT version, no MAC, ENABLE refused, out of DMA memory; HW_B
 * a MAC with the group bit set. */
#include "hwdrv.h"

/* ---- registers ------------------------------------------------------------ */
/* BAR0: pass-through */
#define P0_IMASK(i) (0x000u + 8u * (i))
#define P0_TXH(q)   (0x600u + 8u * (q))
#define P0_RXH1(q)  (0x800u + 8u * (q))
#define P0_RXH2(q)  (0xA00u + 8u * (q))
/* BAR1: control */
#define B1_VRRS  0x00u /* revision report/selection */
#define B1_UVRS  0x08u /* UPT version report/selection */
#define B1_DSL   0x10u /* driver-shared physical address, low */
#define B1_DSH   0x18u
#define B1_CMD   0x20u /* command in, result out */
#define B1_MACL  0x28u
#define B1_MACH  0x30u
#define B1_INTR  0x38u
#define B1_EVENT 0x40u

#define CMD_ENABLE      0xCAFE0000u
#define CMD_DISABLE     0xCAFE0001u
#define CMD_RESET       0xCAFE0002u
#define CMD_SET_RXMODE  0xCAFE0003u
#define CMD_SET_FILTER  0xCAFE0004u
#define CMD_SET_FEATURE 0xCAFE0009u
#define CMD_GET_STATUS  0xF00D0000u
#define CMD_GET_LINK    0xF00D0002u /* bit 0: link up; bits 16+: Mb/s */
#define CMD_GET_MACL    0xF00D0003u
#define CMD_GET_MACH    0xF00D0004u

/* ---- driver-shared block ---------------------------------------------------
 * Field offsets into the block; it is allocated zeroed, so unlisted fields
 * stay 0. Sizes are the spec's; the block is 720 bytes. */
#define DS_MAGIC    0u  /* 0xBABEFEE1 */
#define DS_VERSION  8u  /* driver version, 0x00010000 */
#define DS_GUEST    12u /* OS type | 32/64-bit */
#define DS_REVISION 16u /* vmxnet3 revision the driver speaks: 1 */
#define DS_UPTVER   20u /* UPT version the driver speaks: 1 */
#define DS_UPTFEAT  24u /* u64 feature bits (0: none needed) */
#define DS_DRVDATA  32u /* u64: ~0 = no driver data region */
#define DS_QS       40u /* u64: queue-shared physical address */
#define DS_DRVDDLEN 48u
#define DS_QSLEN    52u
#define DS_MTU      56u
#define DS_NRXSG    60u  /* u16 */
#define DS_NTXQ     62u  /* u8 */
#define DS_NRXQ     63u  /* u8 */
#define DS_AUTOMASK 80u  /* u8 */
#define DS_NINTR    81u  /* u8 */
#define DS_EVINTR   82u  /* u8 */
#define DS_MODLEVEL 83u  /* u8[25]: one per interrupt */
#define DS_ICTRL    108u /* 1 = every interrupt source off */
#define DS_RXMODE   120u
#define DS_MCASTLEN 124u /* u16 */
#define DS_MCAST    128u /* u64: multicast table physical address */
#define DS_VLAN     136u /* u32[128]: vlan filter, all 0 = none tagged */
#define DS_SIZE     720u

#define DS_MAGIC_REV1   0xBABEFEE1u
#define DS_DRV_VER      0x00010000u
#define GOS_UNKNOWN     0x00u
#define GOS_64BIT       0x02u
#define ICTRL_OFF       0x01u
#define IMOD_ADAPTIVE   8u
#define RXMODE_UCAST    0x01u
#define RXMODE_BCAST    0x04u
#define RXMODE_ALLMULTI 0x08u
#define MTU_ETH         1500u

/* queue-shared block: txq_shared 256 bytes at +0, rxq_shared at +256 */
#define TS_INTRTHR 4u
#define TS_CMD     16u /* u64 tx ring */
#define TS_COMP    32u /* u64 tx completion ring */
#define TS_DRVD    40u
#define TS_CMDLEN  56u
#define TS_COMPLEN 64u
#define TS_DRVDLEN 68u
#define TS_INTRIDX 72u
#define TS_STOPPED 80u
#define TS_ERROR   84u
#define TS_SIZE    256u

#define RS_UPDHEAD 0u  /* device sets it when the producer index went stale */
#define RS_CMD0    16u /* u64 rx ring 1 (headers) */
#define RS_CMD1    24u /* u64 rx ring 2 (bodies) */
#define RS_COMP    32u
#define RS_DRVD    40u
#define RS_CMDLEN0 56u
#define RS_CMDLEN1 60u
#define RS_COMPLEN 64u
#define RS_DRVDLEN 68u
#define RS_INTRIDX 72u
#define RS_STOPPED 80u
#define RS_ERROR   84u
#define RS_SIZE    256u
#define QS_SIZE    (TS_SIZE + RS_SIZE)

/* ---- descriptors ------------------------------------------------------------
 * All 16 bytes, little-endian.
 *   tx:   addr@0; w2@8 len bits 0..13 + gen bit 14; w3@12 om|EOP|COMPREQ
 *   txc:  w0@0 eop index bits 0..11; w3@12 gen bit 31
 *   rx:   addr@0; w2@8 len bits 0..13 + btype bit 14 + gen bit 31
 *   rxc:  w0@0 idx bits 0..11 + EOP bit 14 + SOP bit 15 + qid bits 16..25;
 *         w2@8 len bits 0..13 + ERROR bit 14; w3@12 gen bit 31 */
#define TX_GEN        (1u << 14)
#define TX_EOP        (1u << 12)
#define TX_COMPREQ    (1u << 13)
#define TXC_GEN       (1u << 31)
#define RX_BTYPE_BODY (1u << 14)
#define RX_GEN        (1u << 31)
#define RXC_GEN       (1u << 31)
#define RXC_EOP       (1u << 14)
#define RXC_SOP       (1u << 15)
#define RXC_ERROR     (1u << 14)

#define RXN       32u        /* descriptors per receive ring */
#define TXN       16u        /* transmit descriptors */
#define RXCN      (2u * RXN) /* both receive rings share one completion ring */
#define BUF       2048u
#define MAX_FRAME 1514u /* no FCS, no VLAN tag */
#define MCAST_LEN (682u * 6u)

#define CMD_US  1000u  /* a command result that never comes */
#define TX_US   50000u /* a transmit completion that never comes */
#define LINK_US 50000u /* link query settled */

const hw_id_t vmx3_ids[] = {{0x15AD, 0x07B0}, {0, 0}};

typedef struct vmx3 {
    const hw_plat_t *p;
    volatile void *r0; /* BAR0: doorbells */
    volatile void *r1; /* BAR1: control */
    volatile uint8_t *ds, *qs, *mcast;
    volatile uint8_t *txd, *txcd, *rxd0, *rxd1, *rxcd;
    uint8_t *txbuf, *rxbuf;
    uint64_t ds_phys, qs_phys, mcast_phys;
    uint64_t txd_phys, txcd_phys, rxd0_phys, rxd1_phys, rxcd_phys;
    uint64_t txbuf_phys, rxbuf_phys;
    uint32_t tx_prod, tx_gen; /* producer index, current generation bit */
    uint32_t tx_used;         /* descriptors handed to the device */
    uint32_t txc_next, txc_gen;
    uint32_t rx_fill[2], rx_gen[2]; /* per ring */
    uint32_t rxc_next, rxc_gen;
    uint32_t rx_plen;       /* bytes of the current frame copied so far */
    uint8_t rx_in, rx_drop; /* mid-frame / dropping the current frame */
    uint8_t db[2];          /* ring's producer index moved since doorbell */
    hw_nic_t nic;
} vmx3_t;

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

/* A command write goes into B1_CMD and the answer replaces it there. Real
 * hardware answers synchronously, so the first read returns the result; the
 * bounded poll covers a device that takes a moment (and host fakes, which
 * only tick while the driver waits). res gets the result; 0 = the device
 * never answered (the register still holds the command). */
static int vmx_cmd(vmx3_t *e, uint32_t c, uint32_t *res)
{
    hw_wr32(e->r1, B1_CMD, c);
    for (uint32_t t = 0;; t++) {
        uint32_t v = hw_rd32(e->r1, B1_CMD);
        if (v != c) {
            *res = v;
            return 1;
        }
        if (t >= CMD_US) break;
        e->p->delay_us(e->p->ctx, 1);
    }
    *res = c;
    return 0;
}

static int link_up(vmx3_t *e)
{
    uint32_t v;
    if (!vmx_cmd(e, CMD_GET_LINK, &v)) return 0;
    return v & 1;
}

/* Drain every completed transmit. Completions arrive in order; the eop index
 * of the last one identifies the newest finished frame. */
static uint32_t drain_tx(vmx3_t *e)
{
    uint32_t n = 0;
    while (e->tx_used) {
        const volatile uint8_t *c = e->txcd + e->txc_next * 16;
        if ((get32(c, 12) & TXC_GEN) != e->txc_gen) break;
        hw_wmb();
        e->tx_used--;
        if (++e->txc_next == TXN) {
            e->txc_next = 0;
            e->txc_gen ^= TXC_GEN;
        }
        n++;
    }
    return n;
}

/* Give the slot the device consumed back at the fill position (the rings are
 * FIFO, so it is the same slot), then ring the ring's doorbell if the device
 * asked for the producer index. */
static void rx_refill(vmx3_t *e, uint32_t rid)
{
    volatile uint8_t *ring = rid ? e->rxd1 : e->rxd0;
    uint32_t f = e->rx_fill[rid];
    volatile uint8_t *d = ring + f * 16;
    put64(d, 0, e->rxbuf_phys + ((uint64_t) rid * RXN + f) * BUF);
    put32(d, 8, BUF | (rid ? RX_BTYPE_BODY : 0) | e->rx_gen[rid]);
    put32(d, 12, 0);
    if (++e->rx_fill[rid] == RXN) {
        e->rx_fill[rid] = 0;
        e->rx_gen[rid] ^= RX_GEN;
    }
    e->db[rid] = 1;
}

/* Ring the doorbell of every ring whose producer index moved. */
static void rx_doorbells(vmx3_t *e)
{
    for (uint32_t r = 0; r < 2; r++) {
        if (e->db[r]) {
            hw_wr32(e->r0, r ? P0_RXH2(0) : P0_RXH1(0), e->rx_fill[r]);
            e->db[r] = 0;
        }
    }
}

static int vmx3_recv(hw_nic_t *n, void *buf, uint32_t max)
{
    vmx3_t *e = (vmx3_t *) n->drv;
    if (!buf || max == 0) return 0;
    for (uint32_t guard = 0; guard < RXCN; guard++) {
        const volatile uint8_t *c = e->rxcd + e->rxc_next * 16;
        if ((get32(c, 12) & RXC_GEN) != e->rxc_gen) break;
        hw_wmb(); /* the fields are read only after the generation bit */
        uint32_t w0 = get32(c, 0), w2 = get32(c, 8);
        if (++e->rxc_next == RXCN) {
            e->rxc_next = 0;
            e->rxc_gen ^= RXC_GEN;
        }
        uint32_t idx = w0 & 0xFFFu;
        uint32_t rid = ((w0 >> 16) & 0x3FFu) ? 1 : 0; /* queue 0: qid 0/1 = ring 1/2 */
        rx_refill(e, rid);
        if (!e->rx_in && !(w0 & RXC_SOP)) continue; /* a stray fragment */
        if (w0 & RXC_SOP) {
            e->rx_in = 1;
            e->rx_drop = 0;
            e->rx_plen = 0;
        }
        uint32_t len = w2 & 0x3FFFu;
        if ((w2 & RXC_ERROR) || idx >= RXN) e->rx_drop = 1; /* a wild index poisons it */
        if (e->rx_in && !e->rx_drop && len && e->rx_plen < max) {
            const uint8_t *src = e->rxbuf + ((uint64_t) rid * RXN + idx) * BUF;
            uint32_t take = len < max - e->rx_plen ? len : max - e->rx_plen;
            hw_memcpy((uint8_t *) buf + e->rx_plen, src, take);
            e->rx_plen += take;
        }
        if (!(w0 & RXC_EOP)) continue;
        uint32_t got = (e->rx_in && !e->rx_drop) ? e->rx_plen : 0;
        e->rx_in = 0;
        rx_doorbells(e);
        if (got) return (int) got;
    }
    rx_doorbells(e);
    return 0;
}

static int vmx3_send(hw_nic_t *n, const void *frame, uint32_t len)
{
    vmx3_t *e = (vmx3_t *) n->drv;
    if (!frame || len == 0 || len > MAX_FRAME) return -1;
    drain_tx(e);
    if (e->tx_used >= TXN - 1) return -2; /* a full ring reads empty to the NIC */
    uint32_t i = e->tx_prod;
    hw_memcpy(e->txbuf + i * BUF, frame, len);
    volatile uint8_t *d = e->txd + i * 16;
    /* Written with the generation bit cleared, then published by setting it:
     * the device only picks up a descriptor whose gen matches its own. */
    put64(d, 0, e->txbuf_phys + (uint64_t) i * BUF);
    put32(d, 8, len | (e->tx_gen ^ TX_GEN));
    put32(d, 12, TX_EOP | TX_COMPREQ);
    hw_wmb();
    put32(d, 8, len | e->tx_gen);
    e->tx_prod = (i + 1) % TXN;
    if (e->tx_prod == 0) e->tx_gen ^= TX_GEN;
    e->tx_used++;
    hw_wmb();
    hw_wr32(e->r0, P0_TXH(0), e->tx_prod);
    /* The completions come in ring order: ours is the newest, so the ring is
     * done with it when every outstanding descriptor has been drained. */
    for (uint32_t waited = 0;; waited += 10) {
        drain_tx(e);
        if (!e->tx_used) {
            n->link_up = (uint8_t) link_up(e);
            return 0;
        }
        if (waited >= TX_US) break;
        e->p->delay_us(e->p->ctx, 10);
    }
    /* No completion: with the link down the frame waits in the ring for a
     * cable, which is "queued"; with the link up the transmitter is stuck. */
    n->link_up = (uint8_t) link_up(e);
    return n->link_up ? -1 : 0;
}

/* hw_nic_t.phy_off: quiesce the device; there is no PHY to power down. */
static int vmx3_phy_off(hw_nic_t *n)
{
    vmx3_t *e = (vmx3_t *) n->drv;
    uint32_t v;
    vmx_cmd(e, CMD_DISABLE, &v);
    n->link_up = 0;
    return 1;
}

static void note_mac(hw_line_t *note, const uint8_t *mac)
{
    for (int i = 0; i < 6; i++) {
        if (i) hw_line_c(note, ':');
        hw_line_x(note, mac[i], 2);
    }
}

static void fill_rx_ring(vmx3_t *e, uint32_t rid)
{
    volatile uint8_t *ring = rid ? e->rxd1 : e->rxd0;
    for (uint32_t i = 0; i < RXN; i++) {
        volatile uint8_t *d = ring + i * 16;
        put64(d, 0, e->rxbuf_phys + ((uint64_t) rid * RXN + i) * BUF);
        put32(d, 8, BUF | (rid ? RX_BTYPE_BODY : 0) | e->rx_gen[rid]);
        put32(d, 12, 0);
        if (i + 1 == RXN) e->rx_gen[rid] ^= RX_GEN;
    }
}

hw_verdict_t vmx3_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r, hw_line_t *note)
{
    /* BAR slots, not BAR numbers: a 64-bit BAR0 takes slots 0 and 1, so the
     * control window sits at slot 2 (it is at slot 1 when BAR0 is 32-bit). */
    volatile void *r0 = hw_pci_map_bar(p, d, 0);
    volatile void *r1 = hw_pci_map_bar(p, d, (d->bar_flags[0] & HW_BAR_64) ? 2 : 1);
    if (!r0 || !r1) {
        hw_line_s(note, "BAR0/BAR1 not assigned");
        return HW_N;
    }
    hw_line_s(note, "vmxnet3 ");
    if (r->n_nic >= HW_MAX_NIC) {
        hw_line_s(note, "not registered: NIC table full");
        return HW_N;
    }
    vmx3_t *e = (vmx3_t *) p->dma_alloc(p->ctx, sizeof(vmx3_t), 64, 0);
    if (!e) {
        hw_line_s(note, "out of DMA memory");
        return HW_F;
    }
    e->p = p;
    e->r0 = r0;
    e->r1 = r1;
    hw_pci_enable(p, d);
    if (hw_rd32(r1, B1_VRRS) == 0xFFFFFFFFu) {
        hw_line_s(note, "registers read all-ones");
        return HW_F;
    }

    /* 1. Revision and UPT version: the registers report a bitmask of
     * supported versions; the driver writes back the one it speaks. */
    uint32_t vrrs = hw_rd32(r1, B1_VRRS);
    uint32_t rev = (vrrs & 2) ? 2 : (vrrs & 1) ? 1 : 0;
    if (!rev) {
        hw_line_s(note, "unsupported hardware version");
        return HW_F;
    }
    hw_wr32(r1, B1_VRRS, rev);
    if (!(hw_rd32(r1, B1_UVRS) & 1)) {
        hw_line_s(note, "incompatible UPT version");
        return HW_F;
    }
    hw_wr32(r1, B1_UVRS, 1);

    /* 2. DMA: the driver-shared block, the queue-shared block, one transmit
     * and one transmit-completion ring, two receive rings, one shared
     * receive-completion ring, the frame buffers and the multicast table. */
    e->ds = (volatile uint8_t *) p->dma_alloc(p->ctx, DS_SIZE, 8, &e->ds_phys);
    e->qs = (volatile uint8_t *) p->dma_alloc(p->ctx, QS_SIZE, 128, &e->qs_phys);
    e->txd = (volatile uint8_t *) p->dma_alloc(p->ctx, TXN * 16, 512, &e->txd_phys);
    e->txcd = (volatile uint8_t *) p->dma_alloc(p->ctx, TXN * 16, 512, &e->txcd_phys);
    e->rxd0 = (volatile uint8_t *) p->dma_alloc(p->ctx, RXN * 16, 512, &e->rxd0_phys);
    e->rxd1 = (volatile uint8_t *) p->dma_alloc(p->ctx, RXN * 16, 512, &e->rxd1_phys);
    e->rxcd = (volatile uint8_t *) p->dma_alloc(p->ctx, RXCN * 16, 512, &e->rxcd_phys);
    e->txbuf = (uint8_t *) p->dma_alloc(p->ctx, TXN * BUF, 4096, &e->txbuf_phys);
    e->rxbuf = (uint8_t *) p->dma_alloc(p->ctx, 2 * RXN * BUF, 4096, &e->rxbuf_phys);
    e->mcast = (volatile uint8_t *) p->dma_alloc(p->ctx, MCAST_LEN, 32, &e->mcast_phys);
    if (!e->ds || !e->qs || !e->txd || !e->txcd || !e->rxd0 || !e->rxd1 || !e->rxcd || !e->txbuf ||
        !e->rxbuf || !e->mcast) {
        hw_line_s(note, "out of DMA memory");
        return HW_F;
    }

    /* 3. Queue-shared block: where the rings live and how deep they are. */
    put32(e->qs + 0, TS_INTRTHR, 1);
    put64(e->qs + 0, TS_CMD, e->txd_phys);
    put64(e->qs + 0, TS_COMP, e->txcd_phys);
    put64(e->qs + 0, TS_DRVD, ~0ULL);
    put32(e->qs + 0, TS_CMDLEN, TXN);
    put32(e->qs + 0, TS_COMPLEN, TXN);
    put32(e->qs + 0, TS_DRVDLEN, 0);
    e->qs[TS_INTRIDX] = 0;
    e->qs[TS_STOPPED] = 1;
    put64(e->qs + 256, RS_CMD0, e->rxd0_phys);
    put64(e->qs + 256, RS_CMD1, e->rxd1_phys);
    put64(e->qs + 256, RS_COMP, e->rxcd_phys);
    put64(e->qs + 256, RS_DRVD, ~0ULL);
    put32(e->qs + 256, RS_CMDLEN0, RXN);
    put32(e->qs + 256, RS_CMDLEN1, RXN);
    put32(e->qs + 256, RS_COMPLEN, RXCN);
    put32(e->qs + 256, RS_DRVDLEN, 0);
    e->qs[256 + RS_INTRIDX] = 0;
    e->qs[256 + RS_STOPPED] = 1;

    /* 4. Driver-shared block: identity, capabilities, filter state. */
    put32(e->ds, DS_MAGIC, DS_MAGIC_REV1);
    put32(e->ds, DS_VERSION, DS_DRV_VER);
    put32(e->ds, DS_GUEST, GOS_UNKNOWN | GOS_64BIT);
    put32(e->ds, DS_REVISION, 1);
    put32(e->ds, DS_UPTVER, 1);
    put64(e->ds, DS_UPTFEAT, 0);
    put64(e->ds, DS_DRVDATA, ~0ULL);
    put64(e->ds, DS_QS, e->qs_phys);
    put32(e->ds, DS_DRVDDLEN, 0);
    put32(e->ds, DS_QSLEN, QS_SIZE);
    put32(e->ds, DS_MTU, MTU_ETH);
    e->ds[DS_NRXSG] = 1; /* one buffer per packet (no LRO) */
    e->ds[DS_NTXQ] = 1;
    e->ds[DS_NRXQ] = 1;
    e->ds[DS_AUTOMASK] = 1;
    e->ds[DS_NINTR] = 1;
    e->ds[DS_EVINTR] = 0;
    for (uint32_t i = 0; i < 25; i++) e->ds[DS_MODLEVEL + i] = IMOD_ADAPTIVE;
    put32(e->ds, DS_ICTRL, ICTRL_OFF);
    put32(e->ds, DS_RXMODE, RXMODE_UCAST | RXMODE_BCAST | RXMODE_ALLMULTI);
    put32(e->ds, DS_MCASTLEN, 0);
    put64(e->ds, DS_MCAST, e->mcast_phys);
    hw_wmb();
    hw_wr32(r1, B1_DSL, (uint32_t) e->ds_phys);
    hw_wr32(r1, B1_DSH, (uint32_t) (e->ds_phys >> 32));

    /* 5. Station address. */
    uint32_t macl, mach;
    if (!vmx_cmd(e, CMD_GET_MACL, &macl) || !vmx_cmd(e, CMD_GET_MACH, &mach)) {
        hw_line_s(note, "the command register never answered");
        return HW_F;
    }
    uint8_t *mac = e->nic.mac;
    for (uint32_t i = 0; i < 4; i++) mac[i] = (uint8_t) (macl >> (8 * i));
    mac[4] = (uint8_t) mach;
    mac[5] = (uint8_t) (mach >> 8);
    if (!macl && !(mach & 0xFFFF)) {
        hw_line_s(note, "no MAC address");
        return HW_F;
    }
    note_mac(note, mac);
    if (mac[0] & 1) {
        hw_line_s(note, " has the group bit set");
        return HW_B;
    }
    hw_wr32(r1, B1_MACL, macl);
    hw_wr32(r1, B1_MACH, mach & 0xFFFF);

    /* 6. Rings: both receive rings filled, transmit ring empty. */
    e->rx_gen[0] = e->rx_gen[1] = RX_GEN;
    fill_rx_ring(e, 0);
    fill_rx_ring(e, 1);
    e->rx_fill[0] = e->rx_fill[1] = 0;
    e->rxc_gen = RXC_GEN;
    e->tx_gen = TX_GEN;
    e->txc_gen = TXC_GEN;
    hw_wr32(r0, P0_RXH1(0), 0);
    hw_wr32(r0, P0_RXH2(0), 0);

    /* 7. Up. */
    uint32_t res;
    if (!vmx_cmd(e, CMD_ENABLE, &res) || res) {
        hw_line_s(note, "the device refused to enable");
        return HW_F;
    }
    uint32_t ignored;
    vmx_cmd(e, CMD_SET_FEATURE, &ignored);
    vmx_cmd(e, CMD_SET_FILTER, &ignored);
    vmx_cmd(e, CMD_SET_RXMODE, &ignored);
    for (uint32_t t = 0; t < LINK_US / 10; t++) {
        if (link_up(e)) break;
        p->delay_us(p->ctx, 10);
    }
    e->nic.link_up = (uint8_t) link_up(e);

    hw_nic_t *n = &e->nic;
    n->name[0] = 'e';
    n->name[1] = 't';
    n->name[2] = 'h';
    n->name[3] = (char) ('0' + r->n_nic);
    n->name[4] = 0;
    n->drv = e;
    n->send = vmx3_send;
    n->recv = vmx3_recv;
    n->phy_off = vmx3_phy_off;
    n->on_pci = 1;
    n->bdf = d->bdf;
    hw_line_s(note, n->link_up ? " link up as " : " link down as ");
    hw_line_s(note, n->name);
    hw_reg_nic(r, n); /* cannot fail: checked above */
    return HW_T;
}
