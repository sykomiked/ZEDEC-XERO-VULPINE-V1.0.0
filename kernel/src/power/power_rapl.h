/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* power_rapl.h — x86 RAPL energy counters as power_ledger meters, for bare
 * metal (and anywhere else that can read MSRs).
 *
 *   R1 UNITS. The energy status unit is bits 12:8 of the power-unit MSR
 *      (Intel 0x606, AMD 0xC0010299): one count is 1/2^ESU joule. Counters
 *      are 32 bits and wrap; pw_rapl_to_uj converts a raw count to whole
 *      microjoules and the wrap is (2^32 * 10^6) >> ESU, exact for ESU <= 38.
 *   R2 DOMAINS. Intel: psys 0x64D (WHOLE), package 0x611 (PART), DRAM 0x619
 *      (PART), cores PP0 0x639 (SUB), uncore/iGPU PP1 0x641 (SUB, it sits
 *      inside the package). AMD: package 0xC001029B (PART). A domain whose
 *      MSR does not read is left out, so a machine without psys or DRAM
 *      simply has fewer meters.
 *   R3 READER. The caller supplies rdmsr. Reading an MSR the CPU lacks raises
 *      #GP, so on bare metal the arch layer must give a reader that catches
 *      the fault and returns non-zero; this module never executes rdmsr
 *      itself. AMD reports RAPL in CPUID 0x80000007 EDX bit 14
 *      (pw_rapl_amd_cpuid_ok); Intel has no CPUID bit, so its reader must be
 *      fault-safe.
 *   Server DRAM domains on some Intel parts use a fixed 15.3 uJ unit instead
 *   of the ESU; this module uses the ESU everywhere and says so.
 */
#ifndef POWER_RAPL_H
#define POWER_RAPL_H

#include "power_ledger.h"

#define PW_RAPL_MAX_DOMAINS 5

#define PW_MSR_INTEL_UNIT 0x606u
#define PW_MSR_INTEL_PKG  0x611u
#define PW_MSR_INTEL_DRAM 0x619u
#define PW_MSR_INTEL_PP0  0x639u
#define PW_MSR_INTEL_PP1  0x641u
#define PW_MSR_INTEL_PSYS 0x64Du
#define PW_MSR_AMD_UNIT   0xC0010299u
#define PW_MSR_AMD_PKG    0xC001029Bu

typedef int (*pw_rdmsr_fn)(void *ctx, uint32_t msr, uint64_t *val); /* 0 = read */

typedef enum { PW_RAPL_INTEL = 0, PW_RAPL_AMD = 1 } pw_rapl_vendor_t;

typedef struct {
    uint32_t msr;
    int meter; /* power_ledger meter index */
} pw_rapl_dom_t;

typedef struct {
    pw_rapl_vendor_t vendor;
    uint32_t esu;
    uint64_t wrap_uj;
    pw_rapl_dom_t dom[PW_RAPL_MAX_DOMAINS];
    uint32_t ndom;
    pw_rdmsr_fn rd;
    void *ctx;
} pw_rapl_t;

uint64_t pw_rapl_to_uj(uint64_t raw, uint32_t esu);
uint64_t pw_rapl_wrap_uj(uint32_t esu);
bool pw_rapl_amd_cpuid_ok(uint32_t edx_80000007);

/* Reads the unit MSR and adds a ledger meter for every domain that reads.
 * max_mw is each meter's plausibility ceiling. Returns the domain count or a
 * pw_err_t (PW_E_FAULT when the unit MSR does not read). */
int pw_rapl_probe(pw_rapl_t *r, pw_ledger_t *l, pw_rapl_vendor_t vendor, pw_rdmsr_fn rd, void *ctx,
                  uint64_t max_mw);

/* Samples every probed domain into the ledger. Returns how many sampled. */
int pw_rapl_sample(pw_rapl_t *r, pw_ledger_t *l, uint64_t now_ms);

#endif /* POWER_RAPL_H */
