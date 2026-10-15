/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* virtio_pci.h — the virtio 1.x PCI transport (VIRTIO 1.1, section 4.1), for
 * the virtio-blk and virtio-net drivers (vblk.c, vnet.c).
 *
 * KVM clouds (and QEMU's default -drive/-netdev devices) present their disks
 * and NICs as virtio-pci, not AHCI or e1000, so a disc that must come up on a
 * cloud VM needs these. Only the modern interface is driven: the common,
 * notify and device configuration structures the vendor capabilities point
 * at, in memory BARs. A legacy-only device (I/O BAR, no capabilities) is
 * reported and left alone. Polled: no MSI-X vector is assigned to anything.
 *
 * The split virtqueues are kernel/src/virtio/vring.c's engine; this file
 * gives it DMA memory and the device its addresses. */
#ifndef ZXV_VIRTIO_PCI_H
#define ZXV_VIRTIO_PCI_H

#include "hwdrv.h"
#include "../virtio/vring.h"

#define VPCI_VENDOR 0x1AF4u

/* device status bits */
#define VPCI_S_ACK         1u
#define VPCI_S_DRIVER      2u
#define VPCI_S_DRIVER_OK   4u
#define VPCI_S_FEATURES_OK 8u
#define VPCI_S_FAILED      128u

#define VPCI_F_VERSION_1 32u /* feature bit: the modern interface */

/* common configuration structure offsets */
#define VPCI_DFSELECT 0x00
#define VPCI_DF       0x04
#define VPCI_GFSELECT 0x08
#define VPCI_GF       0x0C
#define VPCI_MSIX     0x10
#define VPCI_NUMQ     0x12
#define VPCI_STATUS   0x14
#define VPCI_GEN      0x15
#define VPCI_QSELECT  0x16
#define VPCI_QSIZE    0x18
#define VPCI_QMSIX    0x1A
#define VPCI_QENABLE  0x1C
#define VPCI_QNOTIFY  0x1E
#define VPCI_QDESC    0x20
#define VPCI_QDRIVER  0x28
#define VPCI_QDEVICE  0x30

#define VPCI_NO_VECTOR 0xFFFFu
#define VPCI_QMAX      2u /* virtqueues a driver here uses: blk 1, net 2 */

typedef struct vpci_queue {
    vring_t vr;
    volatile void *notify; /* where this queue's index is written */
    uint16_t index;
    uint16_t depth;
} vpci_queue_t;

typedef struct vpci {
    const hw_plat_t *p;
    volatile void *common, *notify_base, *devcfg;
    uint32_t notify_mult;
    uint64_t features; /* negotiated */
    vpci_queue_t q[VPCI_QMAX];
} vpci_t;

/* Find the modern structures (vendor capabilities 1 common, 2 notify, 4
 * device; the ISR status, 3, is for interrupts and not used) and map them.
 * 0 = found; -1 = no common/notify/device structure (a legacy-only device),
 * with a phrase in note. */
int vpci_attach(vpci_t *v, const hw_plat_t *p, const hw_pci_dev_t *d, hw_line_t *note);

/* Reset, acknowledge, and agree on features: the device must offer
 * VERSION_1; the driver takes `want` & what is offered, plus VERSION_1, and
 * the device must accept it (FEATURES_OK reads back). 0 = agreed, -1 = not
 * (the device is marked FAILED and the reason is in note). */
int vpci_negotiate(vpci_t *v, uint64_t want, hw_line_t *note);

/* Set up queue `index` as v->q[slot]: the largest power of two up to
 * VRING_MAX_DEPTH that the device allows, and at least 4 (a request here is
 * up to three descriptors). 0 = enabled, -1 = no such queue, a queue too
 * short, or no DMA memory. */
int vpci_queue(vpci_t *v, uint32_t slot, uint16_t index, hw_line_t *note);

/* DRIVER_OK: the device may now use the queues. */
void vpci_ready(vpci_t *v);

/* Tell the device a queue has new buffers. */
void vpci_kick(vpci_t *v, uint32_t slot);

/* Reset the device: it stops all DMA and forgets the queues. */
void vpci_reset(vpci_t *v);

/* Device configuration bytes, read until config_generation holds still. */
void vpci_cfg_read(vpci_t *v, uint32_t off, void *out, uint32_t n);

/* Wait for one completion on v->q[slot], polling up to timeout_us. Returns the
 * chain's head descriptor and writes the used length; -1 on timeout. */
int32_t vpci_wait(vpci_t *v, uint32_t slot, uint32_t *len, uint32_t timeout_us);

#endif
