/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* virtio_mmio.h — the ONE virtio-mmio register/ABI contract for ZXV.
 *
 * The register map, status bits, magic, feature-1 bit, and device IDs used to be
 * re-declared inside every driver (virtio_blk/input/net/snd). This is the single
 * source of truth for the STANDARD interface — "crack the contract once, drive
 * every conforming device." Drivers may keep their own copies for now; the bus
 * (virtio_bus.c) and any new driver should include this instead.
 */
#ifndef VIRTIO_MMIO_H
#define VIRTIO_MMIO_H

#include <stdint.h>

/* virtio-mmio register offsets (spec 4.2.2) */
#define VMMIO_MAGIC               0x000
#define VMMIO_VERSION             0x004
#define VMMIO_DEVICE_ID           0x008
#define VMMIO_VENDOR_ID           0x00c
#define VMMIO_DEVICE_FEATURES     0x010
#define VMMIO_DEVICE_FEATURES_SEL 0x014
#define VMMIO_DRIVER_FEATURES     0x020
#define VMMIO_DRIVER_FEATURES_SEL 0x024
#define VMMIO_QUEUE_SEL           0x030
#define VMMIO_QUEUE_NUM_MAX       0x034
#define VMMIO_QUEUE_NUM           0x038
#define VMMIO_QUEUE_READY         0x044
#define VMMIO_QUEUE_NOTIFY        0x050
#define VMMIO_INTERRUPT_STATUS    0x060
#define VMMIO_INTERRUPT_ACK       0x064
#define VMMIO_STATUS              0x070
#define VMMIO_QUEUE_DESC_LOW      0x080
#define VMMIO_QUEUE_DESC_HIGH     0x084
#define VMMIO_QUEUE_DRIVER_LOW    0x090
#define VMMIO_QUEUE_DRIVER_HIGH   0x094
#define VMMIO_QUEUE_DEVICE_LOW    0x0a0
#define VMMIO_QUEUE_DEVICE_HIGH   0x0a4
#define VMMIO_CONFIG              0x100

/* device status bits (spec 2.1) */
#define VS_ACK         1
#define VS_DRIVER      2
#define VS_DRIVER_OK   4
#define VS_FEATURES_OK 8
#define VS_FAILED      128

#define VIRTIO_F_VERSION_1 32
#define VMAGIC             0x74726976u   /* "virt" little-endian */

/* virtio device IDs (the subset ZXV drives natively) */
#define VDEV_NET     1
#define VDEV_BLOCK   2
#define VDEV_CONSOLE 3
#define VDEV_GPU     16
#define VDEV_INPUT   18
#define VDEV_SND     25

/* Number of virtio-mmio slots the board exposes, and slot i's base address. */
uint32_t virtio_mmio_slot_count(void);
uint64_t virtio_mmio_slot_base(uint32_t i);

/* Find the next slot >= from_slot whose DEVICE_ID matches and which is modern
 * (version 2). Returns the slot index (>=0) and writes *base_out, or -1 if none.
 * This is the scan every driver used to open-code — now written once. */
int virtio_mmio_find(uint32_t device_id, uint32_t from_slot, uint64_t *base_out);

#endif /* VIRTIO_MMIO_H */
