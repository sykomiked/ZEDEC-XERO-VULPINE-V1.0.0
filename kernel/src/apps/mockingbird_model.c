/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mockingbird_model.c — Mockingbird (see mockingbird_model.h). */
#include <string.h>

#include "mockingbird_model.h"

static const char *const KIND_NAME[MOCK_KIND_MAX + 1] = {
    "none", "uart", "timer", "display", "storage", "network", "input", "audio", "sensor"};
static const char *const STAGE_NAME[MOCK_STAGES] = {"rom", "loader", "kernel", "drivers", "apps"};

static void unboot(mockingbird_t *m)
{
    m->booted = false;
    m->cycles = 0;
    for (uint32_t i = 0; i < MOCK_STAGES; i++) m->code[i] = MOCK_NOT_RUN;
}

void mockingbird_init(mockingbird_t *m, zxm_link_t *link)
{
    memset(m, 0, sizeof *m);
    m->link = link;
    m->cpus = 1;
    m->ram = MOCK_RAM_MIN;
    unboot(m);
}

int mockingbird_refresh(mockingbird_t *m)
{
    zxm_link_t *l = m->link;
    char arch[sizeof m->arch];
    uint64_t cpus = 0, total = 0, avail = 0;
    if (!l) return ZXM_E_STATE;
    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "arch", arch, sizeof arch) ||
        zxm_get_u64(l, l->result, "cpus", &cpus) ||
        zxm_get_u64(l, l->result, "mem_total", &total) ||
        zxm_get_u64(l, l->result, "mem_avail", &avail) || avail > total)
        return ZXM_E_PROTOCOL;
    /* MM1: half of what the host can give, whole pages, at most 4 GiB */
    uint64_t half = avail / 2u;
    int64_t ram = half > (uint64_t) MOCK_RAM_MAX ? MOCK_RAM_MAX : (int64_t) half;
    ram -= ram % MOCK_PAGE;
    if (ram < MOCK_PAGE) return ZXM_E_STATE;
    if (m->ndev && m->dev[0].base < ram) ram = m->dev[0].base - m->dev[0].base % MOCK_PAGE;
    if (ram < MOCK_PAGE) return ZXM_E_STATE;
    memcpy(m->arch, arch, sizeof m->arch);
    m->host_cpus = cpus;
    m->host_mem_total = total;
    m->host_mem_avail = avail;
    m->cpus = cpus == 0 ? 1u : cpus > MOCK_CPUS_MAX ? MOCK_CPUS_MAX : (uint32_t) cpus;
    m->have = true;
    if (ram != m->ram) {
        m->ram = ram;
        unboot(m);
    }
    return ZXM_OK;
}

/* The map RAM first, then the devices as given; true when it is clean */
static bool map_clean(int64_t ram, const mockingbird_device_t *dev, uint32_t n)
{
    static int64_t base[MOCK_DEVICES_MAX + 1], size[MOCK_DEVICES_MAX + 1];
    int64_t over = 0;
    base[0] = 0;
    size[0] = ram;
    for (uint32_t i = 0; i < n; i++) {
        base[i + 1] = dev[i].base;
        size[i + 1] = dev[i].size;
    }
    return mockingbird_region_overlaps(base, size, n + 1u, &over) && over == 0;
}

int mockingbird_set_ram(mockingbird_t *m, int64_t bytes)
{
    if (!mockingbird_limits_ok(bytes, m->ndev, 0) || bytes % MOCK_PAGE != 0) return ZXM_E_ARG;
    if (!map_clean(bytes, m->dev, m->ndev)) return ZXM_E_ARG;
    m->ram = bytes;
    unboot(m);
    return ZXM_OK;
}

int mockingbird_add_device(mockingbird_t *m, int64_t kind, int64_t base, int64_t size, int64_t irq,
                           uint32_t *index)
{
    static mockingbird_device_t next[MOCK_DEVICES_MAX];
    if (!mockingbird_in(kind, 1, MOCK_KIND_MAX) || size < 1 ||
        !mockingbird_in(irq, 0, MOCK_IRQ_MAX))
        return ZXM_E_ARG;
    if (!mockingbird_limits_ok(m->ram, (int64_t) m->ndev + 1, 0)) return ZXM_E_TOOBIG;
    if (!mockingbird_device_record_ok((int64_t) m->ndev, kind, base, size, irq)) return ZXM_E_ARG;
    uint32_t at = 0;
    while (at < m->ndev && m->dev[at].base <= base) at++;
    memcpy(next, m->dev, at * sizeof next[0]);
    next[at] = (mockingbird_device_t){kind, base, size, irq};
    memcpy(next + at + 1, m->dev + at, (m->ndev - at) * sizeof next[0]);
    if (!map_clean(m->ram, next, m->ndev + 1u)) return ZXM_E_ARG;
    memcpy(m->dev, next, (m->ndev + 1u) * sizeof next[0]);
    m->ndev++;
    unboot(m);
    if (index) *index = at;
    return ZXM_OK;
}

int mockingbird_remove_device(mockingbird_t *m, uint32_t index)
{
    if (index >= m->ndev) return ZXM_E_ARG;
    memmove(m->dev + index, m->dev + index + 1, (m->ndev - index - 1u) * sizeof m->dev[0]);
    m->ndev--;
    unboot(m);
    return ZXM_OK;
}

int mockingbird_map(const mockingbird_t *m, int64_t *base, int64_t *size, uint32_t cap, uint32_t *n)
{
    if (!base || !size || !n) return ZXM_E_ARG;
    if (cap < m->ndev + 1u) return ZXM_E_TOOBIG;
    base[0] = 0;
    size[0] = m->ram;
    for (uint32_t i = 0; i < m->ndev; i++) {
        base[i + 1] = m->dev[i].base;
        size[i + 1] = m->dev[i].size;
    }
    *n = m->ndev + 1u;
    return ZXM_OK;
}

static bool has_kind(const mockingbird_t *m, int64_t kind)
{
    for (uint32_t i = 0; i < m->ndev; i++)
        if (m->dev[i].kind == kind) return true;
    return false;
}

static bool irq_shared(const mockingbird_t *m)
{
    for (uint32_t i = 0; i < m->ndev; i++)
        for (uint32_t j = i + 1; j < m->ndev; j++)
            if (m->dev[i].irq == m->dev[j].irq) return true;
    return false;
}

int mockingbird_boot(mockingbird_t *m)
{
    int64_t fault[MOCK_STAGES];
    fault[MOCK_ROM] = m->have ? 0 : 1;
    fault[MOCK_LOADER] = m->ram >= MOCK_RAM_MIN && map_clean(m->ram, m->dev, m->ndev) ? 0 : 2;
    fault[MOCK_KERNEL] = has_kind(m, MOCK_TIMER) ? 0 : 3;
    fault[MOCK_DRIVERS] = irq_shared(m) ? 4 : 0;
    fault[MOCK_APPS] = has_kind(m, MOCK_UART) || has_kind(m, MOCK_DISPLAY) ? 0 : 5;
    unboot(m);
    /* MM3: in order, and nothing after the first failure runs */
    for (uint32_t i = 0; i < MOCK_STAGES; i++) {
        m->code[i] = fault[i];
        if (fault[i] != 0) break;
    }
    m->booted = mockingbird_boot_passed(m->code);
    return m->booted ? ZXM_OK : ZXM_E_STATE;
}

uint32_t mockingbird_failed_stage(const mockingbird_t *m)
{
    return mockingbird_first_failed(m->code);
}

int mockingbird_run(mockingbird_t *m, int64_t steps, int64_t cost, int64_t stall, int64_t *cycles)
{
    int64_t c = 0;
    if (!m->booted) return ZXM_E_STATE;
    if (!mockingbird_run_cycles(steps, cost, stall, &c)) return ZXM_E_ARG;
    if (c > MOCK_CYCLES_MAX || !mockingbird_limits_ok(m->ram, m->ndev, m->cycles + c))
        return ZXM_E_TOOBIG;
    m->cycles += c;
    if (cycles) *cycles = c;
    return ZXM_OK;
}

bool mockingbird_record(const mockingbird_t *m, uint32_t index)
{
    if (index >= m->ndev) return false;
    const mockingbird_device_t *d = &m->dev[index];
    return mockingbird_device_record_ok((int64_t) index, d->kind, d->base, d->size, d->irq);
}

const char *mockingbird_kind_name(int64_t kind)
{
    return mockingbird_in(kind, 1, MOCK_KIND_MAX) ? KIND_NAME[kind] : KIND_NAME[0];
}

const char *mockingbird_stage_name(uint32_t stage)
{
    return stage < MOCK_STAGES ? STAGE_NAME[stage] : "none";
}
