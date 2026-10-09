/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* virtio_bus.h — the omni-driver engine: a bus enumerator + declarative class
 * registry over virtio-mmio.
 *
 * Each class driver is a FAULT-CONTAINED CELL: a small vtable whose init_slot
 * brings up ONE device at a given mmio base — doing its OWN handshake and
 * feature negotiation (which is NOT uniform: virtio-net reads DEVICE_FEATURES
 * for its MAC while blk/input/snd write 0, so it must stay per-driver) — and
 * returns false on any failure without ever trapping. The bus only owns the
 * scan + the dispatch. This is omnicompatibility via STANDARD INTERFACES: we
 * implement the published virtio-mmio + split-virtqueue contracts natively; we
 * run no foreign driver binary and emulate no foreign device. Adding support
 * for a new conforming device is one registration, not a new scan loop.
 */
#ifndef VIRTIO_BUS_H
#define VIRTIO_BUS_H

#include <stdint.h>
#include <stdbool.h>

typedef struct virtio_driver {
    uint32_t     device_id;                 /* virtio DeviceID this cell claims */
    const char  *name;                      /* for honest boot/skip logging     */
    bool       (*init_slot)(uint64_t base); /* bring up ONE device; never traps */
    void       (*poll)(void);               /* optional per-cycle service (or 0)*/
} virtio_driver_t;

/* Register a class driver. Idempotent by device_id (re-register replaces). */
void virtio_register_driver(const virtio_driver_t *drv);

/* How many class drivers are registered (introspection / tests). */
uint32_t virtio_bus_driver_count(void);

/* Pure slot-classification decision, factored out so it is host-testable with
 * fabricated register words. Writes the chosen driver (or NULL). Returns:
 *   1  dispatch to *out   0  empty slot   -1 unclaimed id   -2 legacy (v1)   */
#define VCLS_DISPATCH   1
#define VCLS_EMPTY      0
#define VCLS_UNCLAIMED (-1)
#define VCLS_LEGACY    (-2)
int virtio_classify_slot(uint32_t magic, uint32_t device_id, uint32_t version,
                         const virtio_driver_t **out);

/* Enumerate every mmio slot ONCE and dispatch each populated slot to its
 * registered driver's init_slot (honest log for legacy / unclaimed). Returns
 * the number of devices brought up. (The single-probe boot path; today the
 * per-driver probes are still used at their existing sites for boot ordering.) */
int virtio_bus_probe(void);

/* Service every registered driver that has a poll() (skips NULLs). */
void virtio_bus_poll(void);

/* The shared mmio scan (also declared in virtio_mmio.h). Declared here WITHOUT
 * the register-map macros so existing drivers can replace their open-coded scan
 * loop by calling this, without their own local VMMIO_ or VDEV_ defines clashing.
 * Finds slot >= from_slot with matching DeviceID + modern (v2); -1 if none. */
int      virtio_mmio_find(uint32_t device_id, uint32_t from_slot, uint64_t *base_out);
uint32_t virtio_mmio_slot_count(void);
uint64_t virtio_mmio_slot_base(uint32_t i);

#endif /* VIRTIO_BUS_H */
