/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vnet.c — virtio-net over the PCI transport (VIRTIO 1.1, section 5.1),
 * polled.
 *
 * The NIC of KVM clouds (Contabo among them) and of QEMU's default -nic.
 * Receive queue 0 keeps RXN buffers posted; each completed buffer is copied
 * out and posted again at once. Transmit queue 1 sends one frame at a time
 * and waits for the device to take it, as the e1000 driver does. Every frame
 * on either queue carries the 12-byte virtio-net header (all zero on
 * transmit: no checksum offload, no segmentation), which VERSION_1 makes
 * fixed size.
 *
 * Features asked for: MAC (the device's station address; without it the
 * driver takes the locally administered 02:5A:58:56:00:<n> and says so) and
 * STATUS (link state; without it the link counts as up). phy_off resets the
 * device: it stops all DMA and forgets both queues. There is no PHY to power
 * down, so it returns 1 (MAC stopped, PHY left as it is). */
#include "virtio_pci.h"

#define F_MAC    5u
#define F_STATUS 16u

#define HDR       12u
#define MAX_FRAME 1514u
#define BUF       2048u
#define RXN       16u
#define TX_US     1000000u /* 1 s for the device to take a frame */

#define Q_RX 0u
#define Q_TX 1u

typedef struct vnet {
    vpci_t v;
    uint8_t *rx[RXN];
    uint8_t *tx;
    uint8_t rx_of[VRING_MAX_DEPTH]; /* head descriptor -> receive buffer */
    hw_nic_t nic;
} vnet_t;

const hw_id_t vnet_ids[] = {{VPCI_VENDOR, 0x1000}, {VPCI_VENDOR, 0x1041}, {0, 0}};

static int post_rx(vnet_t *e, uint32_t i)
{
    vring_buf_t b = {e->rx[i], BUF, true};
    int32_t head = vring_add(&e->v.q[Q_RX].vr, &b, 1);
    if (head < 0) return -1;
    e->rx_of[head] = (uint8_t) i;
    return 0;
}

static int vnet_recv(hw_nic_t *n, void *buf, uint32_t max)
{
    vnet_t *e = (vnet_t *) n->drv;
    if (!buf || max == 0) return 0;
    for (uint32_t guard = 0; guard < RXN; guard++) {
        uint32_t len;
        int32_t head = vring_get_used(&e->v.q[Q_RX].vr, &len);
        if (head < 0) return 0;
        uint32_t i = e->rx_of[head], got = 0;
        if (len > HDR + 13 && len <= BUF) {
            got = len - HDR < max ? len - HDR : max;
            hw_memcpy(buf, e->rx[i] + HDR, got);
        }
        post_rx(e, i); /* cannot fail: its own descriptor just came back */
        vpci_kick(&e->v, Q_RX);
        if (got) return (int) got;
    }
    return 0;
}

static int vnet_send(hw_nic_t *n, const void *frame, uint32_t len)
{
    vnet_t *e = (vnet_t *) n->drv;
    if (!frame || len < 14 || len > MAX_FRAME) return -1;
    hw_memset(e->tx, 0, HDR);
    hw_memcpy(e->tx + HDR, frame, len);
    vring_buf_t b = {e->tx, HDR + len, false};
    if (vring_add(&e->v.q[Q_TX].vr, &b, 1) < 0) return -2;
    vpci_kick(&e->v, Q_TX);
    uint32_t used;
    if (vpci_wait(&e->v, Q_TX, &used, TX_US) < 0) return -1;
    return 0;
}

static int vnet_phy_off(hw_nic_t *n)
{
    vnet_t *e = (vnet_t *) n->drv;
    vpci_reset(&e->v);
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

hw_verdict_t vnet_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r, hw_line_t *note)
{
    if (r->n_nic >= HW_MAX_NIC) {
        hw_line_s(note, "not registered: NIC table full");
        return HW_N;
    }
    vnet_t *e = (vnet_t *) p->dma_alloc(p->ctx, sizeof(vnet_t), 64, 0);
    if (!e) {
        hw_line_s(note, "out of DMA memory");
        return HW_F;
    }
    if (vpci_attach(&e->v, p, d, note)) return HW_N;
    if (vpci_negotiate(&e->v, (1ull << F_MAC) | (1ull << F_STATUS), note)) return HW_F;
    uint8_t *rxmem = (uint8_t *) p->dma_alloc(p->ctx, RXN * BUF, 4096, 0);
    e->tx = (uint8_t *) p->dma_alloc(p->ctx, BUF, 64, 0);
    if (!rxmem || !e->tx) {
        hw_line_s(note, "out of DMA memory");
        vpci_reset(&e->v);
        return HW_F;
    }
    for (uint32_t i = 0; i < RXN; i++) e->rx[i] = rxmem + i * BUF;
    if (vpci_queue(&e->v, Q_RX, 0, note) || vpci_queue(&e->v, Q_TX, 1, note)) {
        vpci_reset(&e->v);
        return HW_F;
    }
    if (e->v.q[Q_RX].depth < RXN) {
        hw_line_s(note, "a receive queue shorter than 16");
        vpci_reset(&e->v);
        return HW_F;
    }
    for (uint32_t i = 0; i < RXN; i++) post_rx(e, i);
    vpci_ready(&e->v);
    vpci_kick(&e->v, Q_RX);

    hw_nic_t *n = &e->nic;
    uint8_t c[8];
    vpci_cfg_read(&e->v, 0, c, sizeof c);
    int own = !((e->v.features >> F_MAC) & 1);
    if (own) {
        static const uint8_t base[5] = {0x02, 0x5A, 0x58, 0x56, 0x00};
        hw_memcpy(n->mac, base, 5);
        n->mac[5] = (uint8_t) r->n_nic;
    } else {
        hw_memcpy(n->mac, c, 6);
    }
    n->link_up = (e->v.features >> F_STATUS) & 1 ? (uint8_t) (c[6] & 1) : 1;
    hw_line_s(note, "virtio NIC ");
    note_mac(note, n->mac);
    if (own) hw_line_s(note, " (its own: the device has none)");
    if (n->mac[0] & 1) {
        hw_line_s(note, " has the group bit set");
        vpci_reset(&e->v);
        return HW_B;
    }
    n->name[0] = 'e';
    n->name[1] = 't';
    n->name[2] = 'h';
    n->name[3] = (char) ('0' + r->n_nic);
    n->name[4] = 0;
    n->drv = e;
    n->send = vnet_send;
    n->recv = vnet_recv;
    n->phy_off = vnet_phy_off;
    n->on_pci = 1;
    n->bdf = d->bdf;
    hw_line_s(note, n->link_up ? " link up as " : " link down as ");
    hw_line_s(note, n->name);
    hw_reg_nic(r, n); /* cannot fail: checked above */
    return HW_T;
}
