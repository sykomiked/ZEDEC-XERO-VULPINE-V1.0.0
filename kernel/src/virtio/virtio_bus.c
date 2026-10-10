/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* virtio_bus.c — the omni-driver engine. See virtio_bus.h.
 *
 * The registry + classify decision are host-testable (compile with -DHOST_TEST);
 * the mmio scan/probe + board access are target-only. */
#include "virtio_mmio.h"
#include "virtio_bus.h"

#define VBUS_MAX_DRIVERS 16
static virtio_driver_t s_drv[VBUS_MAX_DRIVERS];
static uint32_t        s_ndrv = 0;

void virtio_register_driver(const virtio_driver_t *drv) {
    if (!drv || !drv->init_slot) return;
    for (uint32_t i = 0; i < s_ndrv; i++)
        if (s_drv[i].device_id == drv->device_id) { s_drv[i] = *drv; return; }  /* replace */
    if (s_ndrv < VBUS_MAX_DRIVERS) s_drv[s_ndrv++] = *drv;
}

uint32_t virtio_bus_driver_count(void) { return s_ndrv; }

static const virtio_driver_t *find_driver(uint32_t device_id) {
    for (uint32_t i = 0; i < s_ndrv; i++)
        if (s_drv[i].device_id == device_id) return &s_drv[i];
    return 0;
}

int virtio_classify_slot(uint32_t magic, uint32_t device_id, uint32_t version,
                         const virtio_driver_t **out) {
    if (out) *out = 0;
    if (magic != VMAGIC || device_id == 0) return VCLS_EMPTY;   /* no device here   */
    const virtio_driver_t *d = find_driver(device_id);
    if (!d) return VCLS_UNCLAIMED;                              /* nobody claims it */
    if (version != 2) return VCLS_LEGACY;                       /* modern-only      */
    if (out) *out = d;
    return VCLS_DISPATCH;
}

void virtio_bus_poll(void) {
    for (uint32_t i = 0; i < s_ndrv; i++)
        if (s_drv[i].poll) s_drv[i].poll();
}

#ifndef HOST_TEST   /* ---- target-only: board access + mmio scan ---- */
#include "board_profile.h"
extern void uart_puts(const char *s);
extern void uart_put_dec(uint64_t v);

uint32_t virtio_mmio_slot_count(void) {
    const board_profile_t *bp = board_get_profile();
    return bp ? bp->virtio_mmio_count : 0;
}
uint64_t virtio_mmio_slot_base(uint32_t i) {
    const board_profile_t *bp = board_get_profile();
    return bp ? bp->virtio_mmio_base + (uint64_t)i * 0x200 : 0;
}

int virtio_mmio_find(uint32_t device_id, uint32_t from_slot, uint64_t *base_out) {
    uint32_t n = virtio_mmio_slot_count();
    for (uint32_t i = from_slot; i < n; i++) {
        uint64_t base = virtio_mmio_slot_base(i);
        if (*(volatile uint32_t *)(base + VMMIO_MAGIC)   != VMAGIC)   continue;
        if (*(volatile uint32_t *)(base + VMMIO_DEVICE_ID) != device_id) continue;
        if (*(volatile uint32_t *)(base + VMMIO_VERSION) != 2)        continue;  /* modern only */
        if (base_out) *base_out = base;
        return (int)i;
    }
    return -1;
}

int virtio_bus_probe(void) {
    uint32_t n = virtio_mmio_slot_count();
    int brought = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t base = virtio_mmio_slot_base(i);
        uint32_t magic = *(volatile uint32_t *)(base + VMMIO_MAGIC);
        uint32_t id    = *(volatile uint32_t *)(base + VMMIO_DEVICE_ID);
        uint32_t ver   = *(volatile uint32_t *)(base + VMMIO_VERSION);
        const virtio_driver_t *d;
        int c = virtio_classify_slot(magic, id, ver, &d);
        if (c == VCLS_EMPTY) continue;
        if (c == VCLS_UNCLAIMED) {
            uart_puts("  [omni] slot "); uart_put_dec(i);
            uart_puts(": unclaimed virtio device id="); uart_put_dec((uint64_t)id); uart_puts("\n");
            continue;
        }
        if (c == VCLS_LEGACY) {
            uart_puts("  [omni] slot "); uart_put_dec(i);
            uart_puts(": legacy (v1) — need -global virtio-mmio.force-legacy=false\n");
            continue;
        }
        if (d->init_slot(base)) {   /* fault-contained: a failing cell never stops the scan */
            brought++;
            uart_puts("  [omni] slot "); uart_put_dec(i);
            uart_puts(": "); uart_puts(d->name); uart_puts(" up\n");
        } else {
            uart_puts("  [omni] slot "); uart_put_dec(i);
            uart_puts(": "); uart_puts(d->name); uart_puts(" init FAILED (skipped)\n");
        }
    }
    return brought;
}
#endif /* !HOST_TEST */

/* ---- DECLARATION -----------------------------------------------------------
 * The transport layer under every virtio device: it walks the MMIO windows the
 * board profile declares, so it needs translation in place before it may touch
 * one -- hence REQUIRES mm_ready, and nothing else.
 *
 * The bring-up asks the bus what it can see. A slot count of zero means the
 * board profile handed us no transport window at all, which is a configuration
 * fault rather than an absent disk, and is worth separating from "the disk did
 * not answer". */
#ifndef HOST_TEST /* the bring-up reads the board profile: target-only */
#    include "zxv_decl.h"

static int virtio_bus_bringup(void) {
    if (virtio_mmio_slot_count() == 0u) return -1;
    /* slot 0 must have a real base -- a zero base is an unfilled profile */
    if (virtio_mmio_slot_base(0) == 0u) return -1;
    return 0;
}

ZXV_DECLARE(virtio_bus,
    ZXV_PROVIDES(virtio_bus_ready),
    ZXV_REQUIRES(mm_ready),
    ZXV_BRINGUP(virtio_bus_bringup));
#endif /* !HOST_TEST */
