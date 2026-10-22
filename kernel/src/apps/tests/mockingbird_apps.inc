/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Mockingbird's model against zxvd's real dispatcher (LINK: sys.info), then
 * against machines of known size on a link of its own (MK_LINK), which
 * answers sys.info with MK_SYS: an emulated device sized, wired, booted
 * stage by stage and run. */
#include "mockingbird_model.h"

static mockingbird_t MK;
static zxm_link_t MK_LINK;
static const char *MK_SYS; /* the sys.info result, or NULL to refuse */

static int32_t mk_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    (void) len;
    if (!MK_SYS || !strstr(req, "\"sys.info\""))
        return snprintf(resp, cap,
                        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32601,"
                        "\"message\":\"no such method here\"}}",
                        l->seq);
    return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, MK_SYS);
}

#define MK_SYS_OF(cpus, total, avail)                                                              \
    "{\"time_ms\":1700000000000,\"os\":\"linux\",\"os_version\":\"6.1\",\"arch\":\"aarch64\","     \
    "\"host\":\"h\",\"cpus\":" cpus ",\"mem_total\":" total ",\"mem_avail\":" avail                \
    ",\"uptime_s\":5,\"load\":[1,2,3],\"zxvd\":{\"pid\":7,\"cpu_user_us\":0,"                      \
    "\"cpu_sys_us\":0,\"rss_bytes\":1,\"max_rss_bytes\":2}}"

APPS_TEST(mockingbird)
{
    static int64_t base[MOCK_DEVICES_MAX + 1], size[MOCK_DEVICES_MAX + 1];
    uint32_t n = 0, at = 0;
    int64_t cyc = 0, over = -1;

    /* the rules, as the blocks run them */
    {
        int64_t ok[MOCK_STAGES] = {0, 0, 0, 0, 0}, bad[MOCK_STAGES] = {0, 0, 3, -1, -1};
        int64_t b3[3] = {0, 4096, 8000}, s3[3] = {4096, 4096, 10};
        CHECK(mockingbird_boot_passed(ok) && !mockingbird_boot_passed(bad) &&
                  mockingbird_first_failed(bad) == 2 && mockingbird_first_failed(ok) == MOCK_STAGES,
              "mockingbird: a boot passes only when every stage gave 0");
        CHECK(mockingbird_region_overlaps(b3, s3, 3, &over) && over == 1,
              "mockingbird: one pair of neighbours overlaps");
        CHECK(mockingbird_run_cycles(1000, 4, 7, &cyc) && cyc == 4007 &&
                  !mockingbird_run_cycles(MOCK_STEPS_MAX + 1, 1, 0, &cyc),
              "mockingbird: a run's cycles, refused past Sutra's picture");
        CHECK(mockingbird_limits_ok(MOCK_RAM_MAX, 64, MOCK_CYCLES_MAX) &&
                  !mockingbird_limits_ok(MOCK_RAM_MAX + 1, 0, 0) &&
                  !mockingbird_limits_ok(1, 65, 0) && !mockingbird_limits_ok(1, 0, -1),
              "mockingbird: limits (Eve's ranges)");
        CHECK(mockingbird_device_record_ok(99, 8, INT64_C(999999999999999), 1, 255) &&
                  !mockingbird_device_record_ok(100, 1, 0, 1, 1) &&
                  !mockingbird_device_record_ok(0, 1, 0, 1, 1000),
              "mockingbird: the record's digit widths (MYRA's)");
    }

    /* MM1 against the real zxvd: the machine this test runs on */
    mockingbird_init(&MK, &LINK);
    CHECK(!MK.have && MK.ram == MOCK_RAM_MIN && MK.cpus == 1, "mockingbird: a blank device");
    int rc = mockingbird_refresh(&MK);
    CHECK(rc == ZXM_OK && MK.have && MK.arch[0] && MK.cpus >= 1 && MK.cpus <= MOCK_CPUS_MAX &&
              MK.ram >= MOCK_PAGE && MK.ram <= MOCK_RAM_MAX && MK.ram % MOCK_PAGE == 0 &&
              (uint64_t) MK.ram <= MK.host_mem_avail / 2u,
          "mockingbird: sized from sys.info (%d, %s, %u cpus, %lld bytes): %s", rc, MK.arch,
          MK.cpus, (long long) MK.ram, LINK.err);

    /* MM1 against known machines */
    zxm_link_init(&MK_LINK, mk_send, &MK_LINK);
    mockingbird_init(&MK, &MK_LINK);
    MK_SYS = NULL;
    CHECK(mockingbird_refresh(&MK) == ZXM_E_REMOTE && !MK.have, "mockingbird: no facts, no device");
    CHECK(mockingbird_boot(&MK) == ZXM_E_STATE && MK.code[MOCK_ROM] == 1 &&
              MK.code[MOCK_LOADER] == MOCK_NOT_RUN && MK.code[MOCK_APPS] == MOCK_NOT_RUN &&
              mockingbird_failed_stage(&MK) == MOCK_ROM && !MK.booted,
          "mockingbird: without machine facts the rom stage fails and nothing after it runs");
    CHECK(mockingbird_run(&MK, 1, 1, 0, &cyc) == ZXM_E_STATE, "mockingbird: no boot, no run");
    MK_SYS = MK_SYS_OF("16", "34359738368", "17179869184");
    CHECK(mockingbird_refresh(&MK) == ZXM_OK && MK.have && MK.ram == MOCK_RAM_MAX &&
              MK.cpus == MOCK_CPUS_MAX && MK.host_cpus == 16 && !strcmp(MK.arch, "aarch64"),
          "mockingbird: a large host gives 4 GiB and 8 cpus at most");
    MK_SYS = MK_SYS_OF("2", "4000000000", "3221225472");
    CHECK(mockingbird_refresh(&MK) == ZXM_OK && MK.ram == INT64_C(1610612736) && MK.cpus == 2,
          "mockingbird: half of 3 GiB free is 1.5 GiB: %lld", (long long) MK.ram);
    MK_SYS = MK_SYS_OF("2", "4000000000", "100000");
    CHECK(mockingbird_refresh(&MK) == ZXM_E_STATE && MK.ram == INT64_C(1610612736),
          "mockingbird: a host without a page to give keeps the device as it was");
    MK_SYS = MK_SYS_OF("2", "4000", "8000");
    CHECK(mockingbird_refresh(&MK) == ZXM_E_PROTOCOL,
          "mockingbird: more free than total is no fact");

    /* MM2 and MM3: the map, wired stage by stage */
    CHECK(mockingbird_boot(&MK) == ZXM_E_STATE && MK.code[MOCK_ROM] == 0 &&
              MK.code[MOCK_LOADER] == 0 && MK.code[MOCK_KERNEL] == 3 &&
              MK.code[MOCK_DRIVERS] == MOCK_NOT_RUN && mockingbird_failed_stage(&MK) == MOCK_KERNEL,
          "mockingbird: no timer: the kernel stage fails");
    const int64_t io = INT64_C(0x80000000);
    CHECK(mockingbird_add_device(&MK, MOCK_TIMER, io + 0x1000, 0x1000, 3, &at) == ZXM_OK && at == 0,
          "mockingbird: a timer above RAM");
    CHECK(mockingbird_boot(&MK) == ZXM_E_STATE && MK.code[MOCK_DRIVERS] == 0 &&
              MK.code[MOCK_APPS] == 5 && mockingbird_failed_stage(&MK) == MOCK_APPS,
          "mockingbird: no console: the apps stage fails");
    CHECK(mockingbird_add_device(&MK, MOCK_UART, io, 0x1000, 4, &at) == ZXM_OK && at == 0 &&
              MK.dev[1].kind == MOCK_TIMER,
          "mockingbird: a uart goes first in the sorted map");
    CHECK(mockingbird_add_device(&MK, MOCK_DISPLAY, io + 0x1800, 0x1000, 5, NULL) == ZXM_E_ARG &&
              mockingbird_add_device(&MK, MOCK_DISPLAY, INT64_C(0x50000000), 0x1000, 5, NULL) ==
                  ZXM_E_ARG &&
              mockingbird_add_device(&MK, MOCK_DISPLAY, io + 0x2000, 0, 5, NULL) == ZXM_E_ARG &&
              mockingbird_add_device(&MK, 9, io + 0x2000, 0x1000, 5, NULL) == ZXM_E_ARG &&
              mockingbird_add_device(&MK, MOCK_DISPLAY, io + 0x2000, 0x1000, 256, NULL) ==
                  ZXM_E_ARG &&
              mockingbird_add_device(&MK, MOCK_DISPLAY, MOCK_ADDR_MAX, 0x1000, 5, NULL) ==
                  ZXM_E_ARG &&
              MK.ndev == 2,
          "mockingbird: overlapping a device or RAM, no size, no kind, no irq, or past the "
          "address space: refused, nothing changed");
    CHECK(mockingbird_boot(&MK) == ZXM_OK && MK.booted &&
              mockingbird_failed_stage(&MK) == MOCK_STAGES,
          "mockingbird: rom, loader, kernel, drivers, apps: booted");
    CHECK(mockingbird_map(&MK, base, size, 2, &n) == ZXM_E_TOOBIG &&
              mockingbird_map(&MK, base, size, MOCK_DEVICES_MAX + 1, &n) == ZXM_OK && n == 3 &&
              base[0] == 0 && size[0] == INT64_C(1610612736) && base[1] == io &&
              base[2] == io + 0x1000 && mockingbird_region_overlaps(base, size, n, &over) &&
              over == 0,
          "mockingbird: the map, RAM first, clean");
    CHECK(mockingbird_record(&MK, 0) && mockingbird_record(&MK, 1) && !mockingbird_record(&MK, 2),
          "mockingbird: each device makes its record");
    CHECK(!strcmp(mockingbird_kind_name(MK.dev[0].kind), "uart") &&
              !strcmp(mockingbird_kind_name(0), "none") &&
              !strcmp(mockingbird_stage_name(MOCK_DRIVERS), "drivers") &&
              !strcmp(mockingbird_stage_name(MOCK_STAGES), "none"),
          "mockingbird: kinds and stages by name");

    /* MM4: runs, counted against 10^9 cycles */
    CHECK(mockingbird_run(&MK, 1000, 4, 7, &cyc) == ZXM_OK && cyc == 4007 && MK.cycles == 4007,
          "mockingbird: a run of 4007 cycles");
    CHECK(mockingbird_run(&MK, -1, 4, 7, &cyc) == ZXM_E_ARG &&
              mockingbird_run(&MK, 1, MOCK_COST_MAX + 1, 0, &cyc) == ZXM_E_ARG,
          "mockingbird: a run outside the rule is refused");
    CHECK(mockingbird_run(&MK, 99999, 9999, 0, &cyc) == ZXM_OK &&
              MK.cycles == 4007 + INT64_C(999890001) &&
              mockingbird_run(&MK, 99999, 2, 0, &cyc) == ZXM_E_TOOBIG &&
              MK.cycles == 4007 + INT64_C(999890001),
          "mockingbird: past 10^9 cycles a run is refused and not counted: %lld",
          (long long) MK.cycles);
    CHECK(mockingbird_run(&MK, 0, 0, 1000000000 - 4007 - 999890001, NULL) == ZXM_OK &&
              MK.cycles == MOCK_CYCLES_MAX && mockingbird_run(&MK, 0, 0, 1, NULL) == ZXM_E_TOOBIG,
          "mockingbird: exactly 10^9 cycles, and not one more");

    /* MM3: two devices on one irq; RAM too small for the image */
    CHECK(mockingbird_add_device(&MK, MOCK_DISPLAY, io + 0x2000, 0x10000, 4, NULL) == ZXM_OK &&
              !MK.booted && MK.cycles == 0,
          "mockingbird: wiring a device ends the boot");
    CHECK(mockingbird_boot(&MK) == ZXM_E_STATE && MK.code[MOCK_DRIVERS] == 4 &&
              MK.code[MOCK_APPS] == MOCK_NOT_RUN,
          "mockingbird: a uart and a display on irq 4: the drivers stage fails");
    CHECK(mockingbird_remove_device(&MK, 3) == ZXM_E_ARG &&
              mockingbird_remove_device(&MK, 2) == ZXM_OK && MK.ndev == 2 &&
              mockingbird_boot(&MK) == ZXM_OK,
          "mockingbird: unwired, it boots again");
    CHECK(mockingbird_set_ram(&MK, 65537) == ZXM_E_ARG &&
              mockingbird_set_ram(&MK, MOCK_RAM_MAX + MOCK_PAGE) == ZXM_E_ARG &&
              mockingbird_set_ram(&MK, 0) == ZXM_E_ARG &&
              mockingbird_set_ram(&MK, io + MOCK_PAGE) == ZXM_E_ARG && MK.booted,
          "mockingbird: RAM not in whole pages, past 4 GiB or over a device: refused");
    CHECK(mockingbird_set_ram(&MK, MOCK_PAGE) == ZXM_OK && !MK.booted &&
              mockingbird_boot(&MK) == ZXM_E_STATE && MK.code[MOCK_LOADER] == 2 &&
              MK.code[MOCK_KERNEL] == MOCK_NOT_RUN,
          "mockingbird: 64 KiB of RAM: the image does not fit, the loader stage fails");
    CHECK(mockingbird_set_ram(&MK, io) == ZXM_OK && mockingbird_boot(&MK) == ZXM_OK,
          "mockingbird: RAM right up to the first device");

    /* MM2: at most 64 devices */
    for (uint32_t i = 2; i < MOCK_DEVICES_MAX; i++)
        CHECK(mockingbird_add_device(&MK, MOCK_SENSOR, io + 0x100000 + (int64_t) i * 0x1000, 0x1000,
                                     10 + (int64_t) i, NULL) == ZXM_OK,
              "mockingbird: device %u", i);
    CHECK(MK.ndev == MOCK_DEVICES_MAX &&
              mockingbird_add_device(&MK, MOCK_SENSOR, io + 0x800000, 0x1000, 200, NULL) ==
                  ZXM_E_TOOBIG &&
              mockingbird_map(&MK, base, size, MOCK_DEVICES_MAX + 1, &n) == ZXM_OK &&
              n == MOCK_DEVICES_MAX + 1 && mockingbird_region_overlaps(base, size, n, &over) &&
              over == 0 && mockingbird_boot(&MK) == ZXM_OK,
          "mockingbird: 64 devices, the 65th refused, and the full map boots");
}
