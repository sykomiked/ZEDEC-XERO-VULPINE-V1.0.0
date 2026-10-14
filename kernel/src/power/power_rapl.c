/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* power_rapl.c — see power_rapl.h. */
#include "power_rapl.h"

uint64_t pw_rapl_to_uj(uint64_t raw, uint32_t esu)
{
    raw &= 0xFFFFFFFFu;
    if (esu > 63u) return 0;
    return pw_muldiv(raw, 1000000u, (uint64_t) 1u << esu);
}

uint64_t pw_rapl_wrap_uj(uint32_t esu)
{
    if (esu > 63u) return 0;
    return pw_muldiv((uint64_t) 1u << 32, 1000000u, (uint64_t) 1u << esu);
}

bool pw_rapl_amd_cpuid_ok(uint32_t edx_80000007)
{
    return (edx_80000007 >> 14) & 1u;
}

typedef struct {
    uint32_t msr;
    const char *name;
    pw_domain_t domain;
    pw_scope_t scope;
} pw_rapl_spec_t;

static const pw_rapl_spec_t intel_doms[] = {
    {PW_MSR_INTEL_PSYS, "rapl-psys", PW_DOM_SYSTEM, PW_SCOPE_WHOLE},
    {PW_MSR_INTEL_PKG, "rapl-package", PW_DOM_PACKAGE, PW_SCOPE_PART},
    {PW_MSR_INTEL_DRAM, "rapl-dram", PW_DOM_DRAM, PW_SCOPE_PART},
    {PW_MSR_INTEL_PP0, "rapl-cores", PW_DOM_CORES, PW_SCOPE_SUB},
    {PW_MSR_INTEL_PP1, "rapl-uncore", PW_DOM_GPU, PW_SCOPE_SUB},
};

static const pw_rapl_spec_t amd_doms[] = {
    {PW_MSR_AMD_PKG, "rapl-package", PW_DOM_PACKAGE, PW_SCOPE_PART},
};

int pw_rapl_probe(pw_rapl_t *r, pw_ledger_t *l, pw_rapl_vendor_t vendor, pw_rdmsr_fn rd, void *ctx,
                  uint64_t max_mw)
{
    if (!r || !l || !rd || (vendor != PW_RAPL_INTEL && vendor != PW_RAPL_AMD)) return PW_E_ARG;
    r->vendor = vendor;
    r->ndom = 0;
    r->rd = rd;
    r->ctx = ctx;
    uint64_t unit;
    if (rd(ctx, vendor == PW_RAPL_INTEL ? PW_MSR_INTEL_UNIT : PW_MSR_AMD_UNIT, &unit) != 0)
        return PW_E_FAULT;
    r->esu = (uint32_t) ((unit >> 8) & 0x1Fu);
    r->wrap_uj = pw_rapl_wrap_uj(r->esu);
    const pw_rapl_spec_t *spec = vendor == PW_RAPL_INTEL ? intel_doms : amd_doms;
    uint32_t n = vendor == PW_RAPL_INTEL ? (uint32_t) (sizeof(intel_doms) / sizeof(intel_doms[0]))
                                         : (uint32_t) (sizeof(amd_doms) / sizeof(amd_doms[0]));
    for (uint32_t i = 0; i < n; i++) {
        uint64_t v;
        if (rd(ctx, spec[i].msr, &v) != 0) continue;
        int m = pw_meter_add(l, spec[i].name, spec[i].domain, spec[i].scope, r->wrap_uj, max_mw);
        if (m < 0) return m;
        r->dom[r->ndom].msr = spec[i].msr;
        r->dom[r->ndom].meter = m;
        r->ndom++;
    }
    return (int) r->ndom;
}

int pw_rapl_sample(pw_rapl_t *r, pw_ledger_t *l, uint64_t now_ms)
{
    if (!r || !l || !r->rd) return PW_E_ARG;
    int ok = 0;
    for (uint32_t i = 0; i < r->ndom; i++) {
        uint64_t v;
        if (r->rd(r->ctx, r->dom[i].msr, &v) != 0) continue;
        if (pw_meter_sample(l, (uint32_t) r->dom[i].meter, pw_rapl_to_uj(v, r->esu), now_ms) ==
            PW_OK)
            ok++;
    }
    return ok;
}
