/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mockingbird_model.h — Mockingbird, the hardware emulator, as a model
 * (zxm.h): an emulated ZXV device, sized from the machine zxvd runs on
 * (sys.info), to try drivers and apps against. Its RAM, the devices wired
 * to its memory map, a boot through five stages in order, and runs counted
 * in cycles. No allocation, integers only; addresses and sizes are bytes
 * (mockingbird_rules.h). zxvd has no emulator method: everything below
 * lives in the model, and zxvd is only asked for the machine's facts.
 *
 *   MM1 MACHINE FACTS. mockingbird_refresh reads sys.info (arch, cpus,
 *       mem_total, mem_avail). The emulated device gets the host's arch,
 *       its cpus up to MOCK_CPUS_MAX, and RAM of half what the host can
 *       give without swapping, in whole MOCK_PAGE pages, at most
 *       MOCK_RAM_MAX. A host that cannot give one page is ZXM_E_STATE; a
 *       failed read keeps the facts it had.
 *   MM2 THE MAP. RAM is the region at base 0; each device is a region of
 *       its own (mockingbird_device_t), the map sorted by base. A change
 *       that would make two regions overlap (mockingbird_region_overlaps,
 *       blocks/mockingbird/overlaps.kron and .tb, witnessed), break the
 *       limits (mockingbird_limits_ok, limits.eve) or a device's record
 *       (mockingbird_device_record_ok, device.myra) is refused (ZXM_E_ARG,
 *       ZXM_E_TOOBIG past the device limit) and nothing is changed.
 *   MM3 BOOT. Five stages in order, each with its code (0 passed):
 *       rom (1: no machine facts read), loader (2: RAM under MOCK_RAM_MIN,
 *       the image does not fit), kernel (3: no timer for its tick),
 *       drivers (4: two devices wired to one irq) and apps (5: no console,
 *       neither a uart nor a display). After the first failure the rest
 *       are MOCK_NOT_RUN. The boot passed when mockingbird_boot_passed says
 *       so (boot.eve and boot.kron, witnessed); otherwise ZXM_E_STATE, the
 *       codes kept to show.
 *   MM4 RUNS. A booted device runs steps of cost cycles and stall cycles
 *       besides (mockingbird_run_cycles, cycles.sutra and cycles.tb,
 *       witnessed). The cycles of every run since boot stay inside
 *       MOCK_CYCLES_MAX (limits.eve); a run that would pass it is refused
 *       (ZXM_E_TOOBIG) and not counted. Changing the RAM or the map ends
 *       the boot. */
#ifndef MOCKINGBIRD_MODEL_H
#define MOCKINGBIRD_MODEL_H

#include "mockingbird_rules.h"
#include "zxm.h"

#define MOCK_PAGE     INT64_C(65536)
#define MOCK_RAM_MIN  INT64_C(1048576) /* 1 MiB: the boot image */
#define MOCK_CPUS_MAX 8u
#define MOCK_IRQ_MAX  INT64_C(255)

enum {
    MOCK_UART = 1,
    MOCK_TIMER,
    MOCK_DISPLAY,
    MOCK_STORAGE,
    MOCK_NETWORK,
    MOCK_INPUT,
    MOCK_AUDIO,
    MOCK_SENSOR,
    MOCK_KIND_MAX = MOCK_SENSOR
};

enum { MOCK_ROM, MOCK_LOADER, MOCK_KERNEL, MOCK_DRIVERS, MOCK_APPS };

typedef struct {
    int64_t kind, base, size, irq;
} mockingbird_device_t;

typedef struct {
    zxm_link_t *link;
    bool have; /* machine facts read */
    char arch[16];
    uint64_t host_cpus, host_mem_total, host_mem_avail;
    uint32_t cpus; /* the emulated device's */
    int64_t ram;
    uint32_t ndev;
    mockingbird_device_t dev[MOCK_DEVICES_MAX]; /* sorted by base */
    int64_t code[MOCK_STAGES];
    bool booted;
    int64_t cycles; /* run since boot */
} mockingbird_t;

void mockingbird_init(mockingbird_t *m, zxm_link_t *link);
/* MM1 */
int mockingbird_refresh(mockingbird_t *m);
/* MM2: RAM in whole pages, MOCK_PAGE .. MOCK_RAM_MAX, below every device */
int mockingbird_set_ram(mockingbird_t *m, int64_t bytes);
/* MM2: kind 1 .. MOCK_KIND_MAX, size at least 1, irq 0 .. MOCK_IRQ_MAX;
 * *index (may be NULL) is where it went in the sorted map */
int mockingbird_add_device(mockingbird_t *m, int64_t kind, int64_t base, int64_t size, int64_t irq,
                           uint32_t *index);
int mockingbird_remove_device(mockingbird_t *m, uint32_t index);
/* The whole map, RAM first: ZXM_E_TOOBIG when cap is short */
int mockingbird_map(const mockingbird_t *m, int64_t *base, int64_t *size, uint32_t cap,
                    uint32_t *n);
/* MM3 */
int mockingbird_boot(mockingbird_t *m);
/* The first stage that did not pass, MOCK_STAGES when all did */
uint32_t mockingbird_failed_stage(const mockingbird_t *m);
/* MM4: *cycles (may be NULL) is what this run took */
int mockingbird_run(mockingbird_t *m, int64_t steps, int64_t cost, int64_t stall, int64_t *cycles);
/* MM2: device index's record, by its place in the map */
bool mockingbird_record(const mockingbird_t *m, uint32_t index);
const char *mockingbird_kind_name(int64_t kind);
const char *mockingbird_stage_name(uint32_t stage);

#endif
