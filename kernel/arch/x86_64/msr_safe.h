/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* msr_safe.h — fault-safe MSR access and CPUID-gated RAPL helpers (x86_64).
 *
 * WHAT IT GUARANTEES. x86_rdmsr_safe / x86_wrmsr_safe never crash the kernel
 * on an MSR the CPU (or hypervisor) does not implement, or on a write with
 * reserved bits: the #GP that RDMSR/WRMSR raises is caught by the exception-
 * fixup table (x86_extable.h) in the kernel IDT's #GP handler and turned into
 * the return value X86_MSR_EFAULT. Every other fault keeps its old behaviour.
 *
 * WHEN IT IS USABLE. The fixup lives in the kernel's own IDT, which
 * x86_ring3_init() installs early in kernel_main_x86_64 (BIOS and UEFI boot
 * both reach that code; under UEFI the stub exits boot services and enters
 * the same _entry32 path, so the firmware IDT is gone by then). Before that
 * IDT is loaded, both calls refuse with X86_MSR_ENOIDT without executing the
 * instruction, rather than risk a #GP through someone else's handler.
 *
 * Ring 0 only. No locking: MSRs are per-CPU and the kernel is single-CPU here.
 *
 * RAPL (Running Average Power Limit) energy counters, for the power ledger:
 *
 *   uint32_t pu, eu, tu; uint64_t e0, e1;
 *   if (x86_rapl_units(&pu, &eu, &tu) == X86_MSR_OK &&
 *       x86_rapl_energy(X86_RAPL_PKG, &e0) == X86_MSR_OK) {
 *       ... wait ...
 *       x86_rapl_energy(X86_RAPL_PKG, &e1);
 *       uint64_t ticks = (e1 - e0) & 0xFFFFFFFFu;    // 32-bit counter wraps
 *       // energy in joules = ticks / 2^eu ; microjoules = (ticks * 1000000) >> eu
 *   }
 *
 * Units are returned as the raw exponents from the power-unit MSR: a value n
 * means the unit is 1/2^n of watts (power), joules (energy) or seconds (time).
 * Integer-only; no libc. */
#ifndef X86_MSR_SAFE_H
#define X86_MSR_SAFE_H

#include <stdint.h>

/* Return codes (0 = success, negative = failure). */
#define X86_MSR_OK     0
#define X86_MSR_EFAULT (-1) /* RDMSR/WRMSR raised #GP and was recovered       */
#define X86_MSR_ENOIDT (-2) /* kernel IDT not installed yet; nothing executed */
#define X86_MSR_ENODEV (-3) /* CPUID says the feature/domain is absent        */
#define X86_MSR_EINVAL (-4) /* bad argument (NULL pointer, unknown domain)    */

/* Architectural / well-known MSR numbers used here. */
#define MSR_IA32_TSC               0x00000010u
#define MSR_IA32_APIC_BASE         0x0000001Bu
#define MSR_X2APIC_APICID          0x00000802u /* #GP unless x2APIC mode enabled  */
#define MSR_IA32_PKRS              0x000006E1u /* bits 63:32 reserved -> #GP */
#define MSR_RAPL_POWER_UNIT        0x00000606u /* Intel */
#define MSR_PKG_ENERGY_STATUS      0x00000611u
#define MSR_DRAM_ENERGY_STATUS     0x00000619u
#define MSR_PP0_ENERGY_STATUS      0x00000639u
#define MSR_PP1_ENERGY_STATUS      0x00000641u
#define MSR_PLATFORM_ENERGY_STATUS 0x0000064Du /* PSYS */
#define MSR_AMD_RAPL_POWER_UNIT    0xC0010299u /* AMD family 17h+ */
#define MSR_AMD_CORE_ENERGY_STATUS 0xC001029Au
#define MSR_AMD_PKG_ENERGY_STATUS  0xC001029Bu

/* Read MSR `msr` into *val. 0 on success; X86_MSR_EFAULT if the CPU raised
 * #GP (then *val is untouched); X86_MSR_ENOIDT before the kernel IDT is up;
 * X86_MSR_EINVAL if val is NULL. */
int x86_rdmsr_safe(uint32_t msr, uint64_t *val);

/* Write `val` to MSR `msr`. 0 on success; X86_MSR_EFAULT if the CPU raised
 * #GP (the MSR is unchanged); X86_MSR_ENOIDT before the kernel IDT is up. */
int x86_wrmsr_safe(uint32_t msr, uint64_t val);

/* Which RAPL interface CPUID advertises (no MSR is touched):
 *   X86_RAPL_INTEL  GenuineIntel, family 6 (RAPL MSRs 0x606/0x611/...;
 *                   Intel has no CPUID bit for RAPL, so the MSR reads in
 *                   x86_rapl_units decide in the end)
 *   X86_RAPL_AMD    AuthenticAMD/HygonGenuine, family >= 17h and
 *                   CPUID 8000_0007h EDX[14] (RAPL) set
 *   X86_RAPL_NONE   anything else (e.g. QEMU TCG's default CPU) */
typedef enum { X86_RAPL_NONE = 0, X86_RAPL_INTEL = 1, X86_RAPL_AMD = 2 } x86_rapl_kind_t;
x86_rapl_kind_t x86_rapl_kind(void);

/* Read the RAPL unit exponents (see the top of this file). Any pointer may be
 * NULL. 0 on success; X86_MSR_ENODEV when x86_rapl_kind() is NONE;
 * X86_MSR_EFAULT when CPUID claimed RAPL but the unit MSR faulted (common
 * under hypervisors). A consumer can treat any non-zero result as "RAPL
 * absent". */
int x86_rapl_units(uint32_t *power_unit, uint32_t *energy_unit, uint32_t *time_unit);

/* RAPL energy domains. AMD implements PKG and PP0 (per-core counter of the
 * current core) only. */
typedef enum {
    X86_RAPL_PKG = 0,  /* Intel 0x611 / AMD 0xC001029B */
    X86_RAPL_PP0 = 1,  /* Intel 0x639 / AMD 0xC001029A (cores) */
    X86_RAPL_PP1 = 2,  /* Intel 0x641 (uncore / graphics)     */
    X86_RAPL_DRAM = 3, /* Intel 0x619                         */
    X86_RAPL_PSYS = 4, /* Intel 0x64D (platform)              */
    X86_RAPL_NDOMAINS
} x86_rapl_domain_t;

/* Read the raw energy counter of `domain` into *raw: bits 31:0 of the status
 * MSR (a free-running 32-bit counter in energy units; it wraps, so take
 * differences modulo 2^32). 0 on success; X86_MSR_ENODEV if the vendor has
 * no such domain or RAPL is absent; X86_MSR_EFAULT if the MSR faulted (the
 * domain is not implemented on this part); X86_MSR_EINVAL for a NULL raw or
 * out-of-range domain. */
int x86_rapl_energy(x86_rapl_domain_t domain, uint64_t *raw);

/* For the #GP handler (ring3_asm.S): the recovery address for a faulting
 * RIP, or 0 when RIP is not a registered fault-safe instruction. */
uint64_t x86_extable_fixup(uint64_t rip);

#endif
