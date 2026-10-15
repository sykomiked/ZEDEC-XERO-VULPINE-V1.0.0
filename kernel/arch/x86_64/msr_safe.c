/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* msr_safe.c — fault-safe MSR access + CPUID-gated RAPL helpers (x86_64).
 * See msr_safe.h for the contract; x86_extable.h for the fixup mechanism. */
#include "msr_safe.h"
#include "x86_extable.h"

/* msr_safe_asm.S: the instructions themselves, each with a fixup entry. */
extern int x86_rdmsr_raw_safe(uint32_t msr, uint64_t *val);
extern int x86_wrmsr_raw_safe(uint32_t msr, uint64_t val);
/* ring3.c: 1 once the kernel IDT (with the fixup-aware #GP gate) is live. */
extern int x86_idt_ready(void);

/* linker64.ld */
extern const x86_extable_entry_t __zxv_extable_start[];
extern const x86_extable_entry_t __zxv_extable_end[];

uint64_t x86_extable_fixup(uint64_t rip)
{
    return x86_extable_search(__zxv_extable_start, __zxv_extable_end, rip);
}

int x86_rdmsr_safe(uint32_t msr, uint64_t *val)
{
    if (!val) return X86_MSR_EINVAL;
    if (!x86_idt_ready()) return X86_MSR_ENOIDT;
    return x86_rdmsr_raw_safe(msr, val) == 0 ? X86_MSR_OK : X86_MSR_EFAULT;
}

int x86_wrmsr_safe(uint32_t msr, uint64_t val)
{
    if (!x86_idt_ready()) return X86_MSR_ENOIDT;
    return x86_wrmsr_raw_safe(msr, val) == 0 ? X86_MSR_OK : X86_MSR_EFAULT;
}

/* ---------------------------- CPUID ---------------------------- */
static void cpuid2(uint32_t leaf, uint32_t sub, uint32_t r[4])
{
    __asm__ __volatile__("cpuid"
                         : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3])
                         : "a"(leaf), "c"(sub));
}

/* Vendor string is EBX,EDX,ECX of leaf 0, compared as three dwords. */
#define VND(a, b, c, d)                                                                            \
    ((uint32_t) (a) | ((uint32_t) (b) << 8) | ((uint32_t) (c) << 16) | ((uint32_t) (d) << 24))

x86_rapl_kind_t x86_rapl_kind(void)
{
    uint32_t r[4];
    cpuid2(0, 0, r);
    uint32_t max = r[0], b = r[1], c = r[2], d = r[3];
    if (max < 1) return X86_RAPL_NONE;
    int intel = b == VND('G', 'e', 'n', 'u') && d == VND('i', 'n', 'e', 'I') &&
                c == VND('n', 't', 'e', 'l');
    int amd = (b == VND('A', 'u', 't', 'h') && d == VND('e', 'n', 't', 'i') &&
               c == VND('c', 'A', 'M', 'D')) ||
              (b == VND('H', 'y', 'g', 'o') && d == VND('n', 'G', 'e', 'n') &&
               c == VND('u', 'i', 'n', 'e'));
    cpuid2(1, 0, r);
    uint32_t fam = (r[0] >> 8) & 0xF;
    if (fam == 0xF) fam += (r[0] >> 20) & 0xFF;
    if (intel) return fam == 6 ? X86_RAPL_INTEL : X86_RAPL_NONE;
    if (amd && fam >= 0x17) {
        cpuid2(0x80000000u, 0, r);
        if (r[0] < 0x80000007u) return X86_RAPL_NONE;
        cpuid2(0x80000007u, 0, r);
        return (r[3] & (1u << 14)) ? X86_RAPL_AMD : X86_RAPL_NONE;
    }
    return X86_RAPL_NONE;
}

int x86_rapl_units(uint32_t *power_unit, uint32_t *energy_unit, uint32_t *time_unit)
{
    x86_rapl_kind_t k = x86_rapl_kind();
    if (k == X86_RAPL_NONE) return X86_MSR_ENODEV;
    uint64_t v = 0;
    int rc = x86_rdmsr_safe(k == X86_RAPL_AMD ? MSR_AMD_RAPL_POWER_UNIT : MSR_RAPL_POWER_UNIT, &v);
    if (rc != X86_MSR_OK) return rc;
    if (power_unit) *power_unit = (uint32_t) (v & 0xF);
    if (energy_unit) *energy_unit = (uint32_t) ((v >> 8) & 0x1F);
    if (time_unit) *time_unit = (uint32_t) ((v >> 16) & 0xF);
    return X86_MSR_OK;
}

int x86_rapl_energy(x86_rapl_domain_t domain, uint64_t *raw)
{
    static const uint32_t intel_msr[X86_RAPL_NDOMAINS] = {
        MSR_PKG_ENERGY_STATUS,  MSR_PP0_ENERGY_STATUS,      MSR_PP1_ENERGY_STATUS,
        MSR_DRAM_ENERGY_STATUS, MSR_PLATFORM_ENERGY_STATUS,
    };
    static const uint32_t amd_msr[X86_RAPL_NDOMAINS] = {
        MSR_AMD_PKG_ENERGY_STATUS, MSR_AMD_CORE_ENERGY_STATUS, 0, 0, 0,
    };
    if (!raw || (unsigned) domain >= (unsigned) X86_RAPL_NDOMAINS) return X86_MSR_EINVAL;
    x86_rapl_kind_t k = x86_rapl_kind();
    if (k == X86_RAPL_NONE) return X86_MSR_ENODEV;
    uint32_t msr = k == X86_RAPL_AMD ? amd_msr[domain] : intel_msr[domain];
    if (!msr) return X86_MSR_ENODEV;
    uint64_t v = 0;
    int rc = x86_rdmsr_safe(msr, &v);
    if (rc != X86_MSR_OK) return rc;
    *raw = v & 0xFFFFFFFFu;
    return X86_MSR_OK;
}
