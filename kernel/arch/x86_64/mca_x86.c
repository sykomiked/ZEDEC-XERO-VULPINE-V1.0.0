/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mca_x86.c — machine checks on x86_64: the registers, the #MC gate and the
 * poll. Decoding, severity and the log are kernel/src/mca (pure).
 *
 * Boot order follows Intel SDM vol. 3B 16.8: MCG_CTL all ones if MCG_CTL_P,
 * every MCi_CTL all ones, each MCi_STATUS logged if valid and then cleared,
 * CR4.MCE last. Records found at boot are from before the reset (a warm
 * reset keeps them), so they are logged as source "boot" before clearing.
 *
 * MSR maps: architectural MCi_* at 0x400 + 4*i; on AMD with SMCA (CPUID
 * 0x80000007 EBX bit 3) MCA_CTL/STATUS/ADDR/MISC0/CONFIG/IPID/SYND at
 * 0xC0002000 + 0x10*i + 0..6.
 *
 * #MC: the gate runs on its own IST stack. The kernel is not built with
 * -mno-red-zone, so an exception pushed onto the interrupted stack could
 * overwrite a leaf function's red zone and make a resume unsafe; a separate
 * stack leaves the interrupted frame untouched. Like Linux, the handler
 * takes only uncorrected records and leaves corrected ones to the poll. If
 * the worst record is above uncorrected-recoverable, or RIPV is clear, it
 * prints [FAULT] and the existing exception line and halts WITHOUT clearing
 * the banks, so the next boot logs the same records as stale. Without a
 * free IST slot every #MC halts, since resuming would not be safe. */
#include <stdint.h>
#include "mca/mca.h"
#include "mca_x86.h"

extern void x86_exc_report(uint64_t vector, uint64_t rip);

#define MSR_MCG_CAP    0x179u
#define MSR_MCG_STATUS 0x17Au
#define MSR_MCG_CTL    0x17Bu
#define MSR_MC_BASE    0x400u
#define MSR_SMCA_BASE  0xC0002000u
#define CR4_MCE        (1ull << 6)
#define MC_VECTOR      18u

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t) hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t v)
{
    __asm__ __volatile__("wrmsr" : : "c"(msr), "a"((uint32_t) v), "d"((uint32_t) (v >> 32)));
}

static inline void cpuid(uint32_t leaf, uint32_t r[4])
{
    __asm__ __volatile__("cpuid"
                         : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3])
                         : "a"(leaf), "c"(0));
}

static mca_state_t g_mca;
static int g_mca_on;
static uint32_t g_ist; /* 0: no own stack, so never resume */
static void (*g_log)(const char *line);
static uint8_t g_mc_stack[8192] __attribute__((aligned(16)));

/* Register numbers of bank b: 0 CTL, 1 STATUS, 2 ADDR, 3 MISC, 5 IPID, 6 SYND
 * (IPID and SYND exist only in the SMCA map). */
static uint32_t bank_msr(uint32_t b, uint32_t reg)
{
    if (g_mca.smca) return MSR_SMCA_BASE + 0x10u * b + reg;
    return MSR_MC_BASE + 4u * b + reg;
}

static void read_bank(uint32_t b, uint64_t status, mca_raw_t *r)
{
    r->bank = b;
    r->status = status;
    r->addr = (status & MCA_ST_ADDRV) ? rdmsr(bank_msr(b, 2)) : 0;
    r->misc = (status & MCA_ST_MISCV) ? rdmsr(bank_msr(b, 3)) : 0;
    r->smca = g_mca.smca;
    r->ipid = g_mca.smca ? rdmsr(bank_msr(b, 5)) : 0;
    r->synd = (g_mca.smca && (status & MCA_ST_SYNDV)) ? rdmsr(bank_msr(b, 6)) : 0;
}

static uint32_t put(char *buf, uint32_t n, uint32_t cap, const char *s)
{
    while (*s && n + 1 < cap) buf[n++] = *s++;
    buf[n] = 0;
    return n;
}

static uint32_t put_dec(char *buf, uint32_t n, uint32_t cap, uint64_t v)
{
    char t[20];
    uint32_t i = 0;
    do {
        t[i++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v && i < sizeof t);
    while (i && n + 1 < cap) buf[n++] = t[--i];
    buf[n] = 0;
    return n;
}

static uint32_t put_hex(char *buf, uint32_t n, uint32_t cap, uint64_t v)
{
    static const char d[] = "0123456789abcdef";
    n = put(buf, n, cap, "0x");
    for (int i = 15; i >= 0 && n + 1 < cap; i--) buf[n++] = d[(v >> (i * 4)) & 0xFu];
    buf[n] = 0;
    return n;
}

static void log_rec(const char *prefix, const mca_rec_t *d)
{
    char line[32 + MCA_TEXT_MAX];
    uint32_t n = put(line, 0, sizeof line, prefix);
    mca_format(d, line + n, (uint32_t) sizeof line - n);
    g_log(line);
}

/* Log bank b if valid. Returns the severity logged (MCA_SEV_NONE if none). */
static uint32_t take(uint32_t b, uint64_t status, uint8_t src, const char *prefix)
{
    mca_raw_t r;
    mca_rec_t d;
    int was = mca_bank_degrading(&g_mca, b);
    read_bank(b, status, &r);
    int sev = mca_log(&g_mca, &r, src, &d);
    if (sev <= MCA_SEV_NONE) return MCA_SEV_NONE;
    log_rec(prefix, &d);
    if (!was && mca_bank_degrading(&g_mca, b)) {
        char line[96];
        uint32_t n = put(line, 0, sizeof line, "  [MCA] bank ");
        n = put_dec(line, n, sizeof line, b);
        n = put(line, n, sizeof line, " degrading: ");
        n = put_dec(line, n, sizeof line, mca_bank_corrected(&g_mca, b));
        n = put(line, n, sizeof line, " corrected (threshold ");
        n = put_dec(line, n, sizeof line, g_mca.threshold);
        put(line, n, sizeof line, ")");
        g_log(line);
    }
    return (uint32_t) sev;
}

uint32_t x86_mca_poll(void)
{
    uint32_t n = 0;
    if (!g_mca_on) return 0;
    for (uint32_t b = 0; b < g_mca.nbanks; b++) {
        uint64_t st = rdmsr(bank_msr(b, 1));
        if (!(st & MCA_ST_VAL)) continue;
        if (take(b, st, MCA_SRC_POLL, "  [MCA] ") != MCA_SEV_NONE) n++;
        /* Clear only what was read: a record that arrived since stays. */
        if (rdmsr(bank_msr(b, 1)) == st) wrmsr(bank_msr(b, 1), 0);
    }
    return n;
}

/* Called from x86_mca_isr on the IST stack; returns only to resume. */
void x86_mca_exception(uint64_t rip, uint64_t cs);
void x86_mca_exception(uint64_t rip, uint64_t cs)
{
    uint64_t mcgs = rdmsr(MSR_MCG_STATUS);
    uint32_t worst = MCA_SEV_NONE;
    uint64_t taken = 0; /* banks logged here, bit per bank below 64 */
    for (uint32_t b = 0; b < g_mca.nbanks; b++) {
        uint64_t st = rdmsr(bank_msr(b, 1));
        if (!(st & MCA_ST_VAL) || !(st & MCA_ST_UC)) continue;
        uint32_t sev = take(b, st, MCA_SRC_MCE, "  [MCA] #MC ");
        if (sev > worst) worst = sev;
        taken |= 1ull << b;
    }
    if (!g_ist || !mca_mc_may_resume(mcgs, worst)) {
        char line[160];
        uint32_t n = put(line, 0, sizeof line, "[FAULT] machine check: ");
        n = put(line, n, sizeof line, mca_sev_name(worst));
        n = put(line, n, sizeof line, " MCG_STATUS=");
        n = put_hex(line, n, sizeof line, mcgs);
        n = put(line, n, sizeof line, " CS=");
        n = put_hex(line, n, sizeof line, cs);
        put(line, n, sizeof line, "; halting, banks left for the next boot");
        g_log(line);
        x86_exc_report(MC_VECTOR, rip);
        for (;;) __asm__ __volatile__("cli; hlt");
    }
    for (uint32_t b = 0; b < g_mca.nbanks; b++)
        if (taken & (1ull << b)) wrmsr(bank_msr(b, 1), 0);
    wrmsr(MSR_MCG_STATUS, 0); /* MCIP clear: a later #MC is not a shutdown */
    char line[96];
    uint32_t n = put(line, 0, sizeof line, "  [MCA] #MC recovered (");
    n = put(line, n, sizeof line, mca_sev_name(worst));
    n = put(line, n, sizeof line, "), resuming at ");
    put_hex(line, n, sizeof line, rip);
    g_log(line);
}

/* The gate: save the caller-saved registers, call the C handler with the
 * frame's RIP and CS, restore, iretq. 9 pushes after the 5-word frame keep
 * RSP 16-byte aligned at the call. */
__asm__(".pushsection .text.x86_mca_isr,\"ax\",@progbits\n"
        ".globl x86_mca_isr\n"
        "x86_mca_isr:\n"
        "  pushq %rax\n  pushq %rcx\n  pushq %rdx\n  pushq %rsi\n  pushq %rdi\n"
        "  pushq %r8\n  pushq %r9\n  pushq %r10\n  pushq %r11\n"
        "  cld\n"
        "  movq 72(%rsp), %rdi\n"
        "  movq 80(%rsp), %rsi\n"
        "  call x86_mca_exception\n"
        "  popq %r11\n  popq %r10\n  popq %r9\n  popq %r8\n"
        "  popq %rdi\n  popq %rsi\n  popq %rdx\n  popq %rcx\n  popq %rax\n"
        "  iretq\n"
        ".popsection\n");
extern void x86_mca_isr(void);

struct mca_dtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* Point a free IST slot of the live TSS at g_mc_stack. Returns the slot
 * (1..7), or 0 if there is no TSS or no free slot. */
static uint32_t claim_ist(void)
{
    struct mca_dtr g;
    uint16_t tr;
    __asm__ __volatile__("sgdt %0" : "=m"(g));
    __asm__ __volatile__("str %0" : "=r"(tr));
    if ((tr & ~7u) == 0 || (uint32_t) (tr & ~7u) + 15u > g.limit) return 0;
    const uint64_t *desc = (const uint64_t *) (uintptr_t) (g.base + (tr & ~7u));
    uint64_t lo = desc[0], hi = desc[1];
    uint64_t tss = ((lo >> 16) & 0xFFFFFFull) | (((lo >> 56) & 0xFFull) << 24) | (hi << 32);
    uint64_t top = (uint64_t) (uintptr_t) (g_mc_stack + sizeof g_mc_stack);
    for (uint32_t i = 1; i <= 7; i++) {
        /* IST i is at TSS offset 0x24 + 8*(i-1): 4-byte aligned, two words. */
        volatile uint32_t *w = (volatile uint32_t *) (uintptr_t) (tss + 0x24u + 8u * (i - 1u));
        if (w[0] == 0 && w[1] == 0) {
            w[0] = (uint32_t) top;
            w[1] = (uint32_t) (top >> 32);
            return i;
        }
    }
    return 0;
}

/* Write the vector 18 gate. Returns 0, or -1 if the IDT has no entry 18. */
static int install_gate(uint32_t ist)
{
    struct mca_dtr idt;
    uint16_t cs;
    __asm__ __volatile__("sidt %0" : "=m"(idt));
    __asm__ __volatile__("mov %%cs, %0" : "=r"(cs));
    if (idt.limit < MC_VECTOR * 16u + 15u) return -1;
    volatile uint32_t *g = (volatile uint32_t *) (uintptr_t) (idt.base + MC_VECTOR * 16u);
    uint64_t a = (uint64_t) (uintptr_t) x86_mca_isr;
    g[0] = (uint32_t) (a & 0xFFFFu) | ((uint32_t) cs << 16);
    g[1] = (uint32_t) (a & 0xFFFF0000u) | 0x8E00u | (ist & 7u); /* present, intr gate */
    g[2] = (uint32_t) (a >> 32);
    g[3] = 0;
    return 0;
}

uint32_t x86_mca_init(void (*log)(const char *line))
{
    uint32_t r[4];
    char line[128];
    uint32_t n;
    g_log = log;
    cpuid(0, r);
    uint32_t maxleaf = r[0];
    uint32_t vendor = MCA_VENDOR_OTHER;
    if (r[1] == 0x756e6547u && r[3] == 0x49656e69u && r[2] == 0x6c65746eu)
        vendor = MCA_VENDOR_INTEL; /* GenuineIntel */
    else if ((r[1] == 0x68747541u && r[3] == 0x69746e65u && r[2] == 0x444d4163u) ||
             (r[1] == 0x6f677948u && r[3] == 0x6e65476eu && r[2] == 0x656e6975u))
        vendor = MCA_VENDOR_AMD; /* AuthenticAMD, HygonGenuine */
    uint32_t edx = 0, eax1 = 0;
    if (maxleaf >= 1) {
        cpuid(1, r);
        eax1 = r[0];
        edx = r[3];
    }
    if (!(edx & (1u << 7)) || !(edx & (1u << 14))) {
        n = put(line, 0, sizeof line, "  [--] machine check: CPUID has no MCE/MCA (edx=");
        n = put_hex(line, n, sizeof line, edx);
        put(line, n, sizeof line, "), not enabled");
        log(line);
        return 0;
    }
    uint32_t family = (eax1 >> 8) & 0xFu, model = (eax1 >> 4) & 0xFu;
    if (family == 0xF) family += (eax1 >> 20) & 0xFFu;
    if (family == 6 || family >= 0xF) model |= ((eax1 >> 16) & 0xFu) << 4;
    uint32_t smca = 0;
    if (vendor == MCA_VENDOR_AMD) {
        cpuid(0x80000000u, r);
        if (r[0] >= 0x80000007u) {
            cpuid(0x80000007u, r);
            smca = (r[1] >> 3) & 1u;
        }
    }

    uint64_t cap = rdmsr(MSR_MCG_CAP);
    mca_init(&g_mca, vendor, cap, smca);
    uint32_t ist = claim_ist();
    g_ist = ist;
    if (install_gate(ist) != 0) {
        log("  [--] machine check: IDT has no vector 18 entry, not enabled");
        return 0;
    }
    if (cap & MCA_MCG_CTL_P) wrmsr(MSR_MCG_CTL, ~0ull);
    for (uint32_t b = 0; b < g_mca.nbanks; b++)
        if (mca_bank_ctl_writable(vendor, family, model, b)) wrmsr(bank_msr(b, 0), ~0ull);
    g_mca_on = 1; /* take() and the poll need the map */
    uint32_t stale = 0;
    for (uint32_t b = 0; b < g_mca.nbanks; b++) {
        uint64_t st = rdmsr(bank_msr(b, 1));
        if (!(st & MCA_ST_VAL)) continue;
        if (take(b, st, MCA_SRC_BOOT, "  [MCA] stale ") != MCA_SEV_NONE) stale++;
        wrmsr(bank_msr(b, 1), 0);
    }
    wrmsr(MSR_MCG_STATUS, 0);
    uint64_t cr4;
    __asm__ __volatile__("mov %%cr4, %0" : "=r"(cr4));
    __asm__ __volatile__("mov %0, %%cr4" : : "r"(cr4 | CR4_MCE));

    n = put(line, 0, sizeof line, "  [OK] machine check: ");
    n = put_dec(line, n, sizeof line, g_mca.nbanks);
    n = put(line, n, sizeof line, " banks, SMCA ");
    n = put(line, n, sizeof line, smca ? "yes" : "no");
    n = put(line, n, sizeof line, ", ");
    n = put_dec(line, n, sizeof line, stale);
    n = put(line, n, sizeof line, " stale records (MCG_CAP=");
    n = put_hex(line, n, sizeof line, cap);
    n = put(line, n, sizeof line, ist ? ", #MC on IST" : ", #MC on current stack");
    put(line, n, sizeof line, ")");
    log(line);
    if (g_mca.clamped) {
        n = put(line, 0, sizeof line, "  [--] machine check: banks beyond ");
        n = put_dec(line, n, sizeof line, MCA_MAX_BANKS);
        n = put(line, n, sizeof line, " not watched: ");
        put_dec(line, n, sizeof line, g_mca.clamped);
        log(line);
    }
    return g_mca.nbanks;
}
