/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* test_virtio_bus.c — host unit test for the omni-driver engine's registry +
 * slot classification. Build+run on the host (no QEMU, freestanding-safe logic):
 *   cc -DHOST_TEST -Ikernel/src/virtio kernel/src/virtio/virtio_bus.c \
 *      kernel/src/virtio/test_virtio_bus.c -o /tmp/tvb && /tmp/tvb
 */
#include "virtio_mmio.h"
#include "virtio_bus.h"
#include <stdio.h>

static bool mock_ok(uint64_t base)   { (void)base; return true; }
static bool mock_fail(uint64_t base) { (void)base; return false; }
static int  g_polls = 0;
static void mock_poll(void)          { g_polls++; }

int main(void) {
    int fails = 0;
    #define CHECK(c) do { if (!(c)) { printf("  FAIL: %s (line %d)\n", #c, __LINE__); fails++; } } while (0)

    virtio_driver_t blk = { VDEV_BLOCK, "virtio-blk",   mock_ok, 0 };
    virtio_driver_t inp = { VDEV_INPUT, "virtio-input", mock_ok, mock_poll };
    virtio_register_driver(&blk);
    virtio_register_driver(&inp);
    CHECK(virtio_bus_driver_count() == 2);

    const virtio_driver_t *d = 0;
    CHECK(virtio_classify_slot(VMAGIC, VDEV_BLOCK, 2, &d) == VCLS_DISPATCH);   /* good blk slot */
    CHECK(d && d->device_id == VDEV_BLOCK);
    CHECK(virtio_classify_slot(0xdeadbeefu, VDEV_BLOCK, 2, &d) == VCLS_EMPTY); /* bad magic     */
    CHECK(virtio_classify_slot(VMAGIC, 0, 2, &d) == VCLS_EMPTY);              /* id 0 = empty   */
    CHECK(virtio_classify_slot(VMAGIC, VDEV_GPU, 2, &d) == VCLS_UNCLAIMED);   /* nobody claims  */
    CHECK(d == 0);
    CHECK(virtio_classify_slot(VMAGIC, VDEV_INPUT, 1, &d) == VCLS_LEGACY);    /* v1 legacy      */

    /* re-register replaces (does not duplicate) */
    virtio_driver_t blk2 = { VDEV_BLOCK, "virtio-blk2", mock_fail, 0 };
    virtio_register_driver(&blk2);
    CHECK(virtio_bus_driver_count() == 2);
    virtio_classify_slot(VMAGIC, VDEV_BLOCK, 2, &d);
    CHECK(d && d->init_slot == mock_fail);

    /* a vtable with no init_slot is rejected */
    virtio_driver_t bad = { 99, "bad", 0, 0 };
    virtio_register_driver(&bad);
    CHECK(virtio_bus_driver_count() == 2);

    /* poll only drives cells that have a poll() */
    virtio_bus_poll();
    CHECK(g_polls == 1);

    if (fails == 0) printf("test_virtio_bus: ALL PASS (%u drivers registered)\n", virtio_bus_driver_count());
    else            printf("test_virtio_bus: %d CHECK(S) FAILED\n", fails);
    return fails ? 1 : 0;
}
