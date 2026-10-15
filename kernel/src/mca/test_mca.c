/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_mca.c — the machine-check decoder against status words built bit by
 * bit from the Intel SDM vol. 3B ch. 16 and AMD APM vol. 2 ch. 9 tables:
 * corrected DRAM ECC, UCNA, SRAO, SRAR, PCC, overflow, invalid; the error
 * code classes; SMCA IPID; the log ring when full; bank thresholds; the
 * #MC resume rule; the text line. */
#include <stdio.h>
#include <string.h>
#include "mca.h"

static int checks, failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
        }                                                                                          \
    } while (0)

#define B(n) (1ull << (n))
/* Shared architectural bits, by name, so each value below is readable. */
#define VAL   B(63)
#define OVER  B(62)
#define UC    B(61)
#define EN    B(60)
#define MISCV B(59)
#define ADDRV B(58)
#define PCC   B(57)
#define S_    B(56)
#define AR_   B(55)
#define TCC_  B(55)
#define DEFER B(44)

/* QEMU's default capability: 10 banks, MCG_CTL_P, MCG_SER_P. */
#define CAP_QEMU (10ull | MCA_MCG_CTL_P | MCA_MCG_SER_P)
/* A server Intel part: 20 banks, CTL, CMCI, TES, SER, LMCE. */
#define CAP_XEON                                                                                   \
    (20ull | MCA_MCG_CTL_P | MCA_MCG_CMCI_P | MCA_MCG_TES_P | MCA_MCG_SER_P | MCA_MCG_LMCE_P)
/* An EPYC: 64 banks, CTL. */
#define CAP_EPYC (64ull | MCA_MCG_CTL_P)

static mca_state_t g_s;

static mca_raw_t raw(uint32_t bank, uint64_t status, uint64_t addr, uint64_t misc)
{
    mca_raw_t r;
    memset(&r, 0, sizeof r);
    r.bank = bank;
    r.status = status;
    r.addr = addr;
    r.misc = misc;
    return r;
}

static mca_raw_t raw_smca(uint32_t bank, uint64_t status, uint64_t addr, uint64_t ipid,
                          uint64_t synd)
{
    mca_raw_t r = raw(bank, status, addr, 0);
    r.smca = 1;
    r.ipid = ipid;
    r.synd = synd;
    return r;
}

static uint64_t ipid(uint32_t hwid, uint32_t type, uint32_t inst)
{
    return ((uint64_t) type << 48) | ((uint64_t) hwid << 32) | inst;
}

static void test_bits_are_the_sdm_bits(void)
{
    CHECK(MCA_ST_VAL == 0x8000000000000000ull);
    CHECK(MCA_ST_OVER == 0x4000000000000000ull);
    CHECK(MCA_ST_UC == 0x2000000000000000ull);
    CHECK(MCA_ST_EN == 0x1000000000000000ull);
    CHECK(MCA_ST_MISCV == 0x0800000000000000ull);
    CHECK(MCA_ST_ADDRV == 0x0400000000000000ull);
    CHECK(MCA_ST_PCC == 0x0200000000000000ull);
    CHECK(MCA_ST_S == 0x0100000000000000ull);
    CHECK(MCA_ST_AR == 0x0080000000000000ull);
    CHECK(MCA_ST_TCC == MCA_ST_AR); /* same bit, AMD meaning */
    CHECK(MCA_ST_SYNDV == B(53));
    CHECK(MCA_ST_CECC == B(46));
    CHECK(MCA_ST_UECC == B(45));
    CHECK(MCA_ST_DEFER == B(44));
    CHECK(MCA_ST_POISON == B(43));
    CHECK(MCA_MCG_CTL_P == 0x100ull);
    CHECK(MCA_MCG_CMCI_P == 0x400ull);
    CHECK(MCA_MCG_SER_P == 0x1000000ull);
    CHECK(MCA_MCG_LMCE_P == 0x8000000ull);
    CHECK(MCA_MCGS_RIPV == 1 && MCA_MCGS_EIPV == 2 && MCA_MCGS_MCIP == 4);
}

static void test_code_classes(void)
{
    uint8_t t = 0xAA, c = 0xAA;
    CHECK(mca_code_class(0x0000, &t, &c) == MCA_CC_NONE);
    CHECK(t == 0 && c == 0); /* cleared even when not memory */
    CHECK(mca_code_class(0x0001, 0, 0) == MCA_CC_UNCLASSIFIED);
    CHECK(mca_code_class(0x0002, 0, 0) == MCA_CC_UCODE_ROM);
    CHECK(mca_code_class(0x0003, 0, 0) == MCA_CC_EXTERNAL);
    CHECK(mca_code_class(0x0004, 0, 0) == MCA_CC_FRC);
    CHECK(mca_code_class(0x0005, 0, 0) == MCA_CC_INTERNAL_PAR);
    CHECK(mca_code_class(0x0006, 0, 0) == MCA_CC_SMM_ACCESS);
    CHECK(mca_code_class(0x0007, 0, 0) == MCA_CC_OTHER);
    CHECK(mca_code_class(0x0400, 0, 0) == MCA_CC_INTERNAL_TIMER);
    CHECK(mca_code_class(0x0401, 0, 0) == MCA_CC_INTERNAL);
    CHECK(mca_code_class(0x07FF, 0, 0) == MCA_CC_INTERNAL);
    /* Generic cache 0000_0000_0000_11LL: L0..L2 and generic. */
    for (uint16_t ll = 0; ll < 4; ll++) {
        CHECK(mca_code_class((uint16_t) (0x000C | ll), 0, 0) == MCA_CC_GENERIC_CACHE);
        CHECK(mca_code_class((uint16_t) (0x100C | ll), 0, 0) == MCA_CC_GENERIC_CACHE);
    }
    CHECK(mca_code_class(0x0008, 0, 0) == MCA_CC_OTHER);
    /* TLB 0000_0000_0001_TTLL. */
    CHECK(mca_code_class(0x0010, 0, 0) == MCA_CC_TLB);
    CHECK(mca_code_class(0x0014, 0, 0) == MCA_CC_TLB); /* data TLB, L0 */
    CHECK(mca_code_class(0x001F, 0, 0) == MCA_CC_TLB);
    CHECK(mca_code_class(0x101F, 0, 0) == MCA_CC_TLB); /* F bit set */
    CHECK(mca_code_class(0x0020, 0, 0) == MCA_CC_OTHER);
    /* Memory controller 0000_0000_1MMM_CCCC. */
    CHECK(mca_code_class(0x0080, &t, &c) == MCA_CC_MEMORY);
    CHECK(t == 0 && c == 0);
    CHECK(mca_code_class(0x0090, &t, &c) == MCA_CC_MEMORY); /* read, ch 0 */
    CHECK(t == 1 && c == 0);
    CHECK(mca_code_class(0x009F, &t, &c) == MCA_CC_MEMORY); /* read, unspecified */
    CHECK(t == 1 && c == 15);
    CHECK(mca_code_class(0x00A3, &t, &c) == MCA_CC_MEMORY); /* write, ch 3 */
    CHECK(t == 2 && c == 3);
    CHECK(mca_code_class(0x00B1, &t, &c) == MCA_CC_MEMORY); /* addr/cmd, ch 1 */
    CHECK(t == 3 && c == 1);
    CHECK(mca_code_class(0x00C0, &t, &c) == MCA_CC_MEMORY); /* scrub, ch 0 */
    CHECK(t == 4 && c == 0);
    CHECK(mca_code_class(0x10C5, &t, &c) == MCA_CC_MEMORY); /* F bit kept out */
    CHECK(t == 4 && c == 5);
    CHECK(mca_code_class(0x00FF, &t, &c) == MCA_CC_MEMORY);
    CHECK(t == 7 && c == 15);
    CHECK(mca_code_class(0x0090, 0, 0) == MCA_CC_MEMORY); /* null outputs */
    /* Cache 0000_0001_RRRR_TTLL. */
    CHECK(mca_code_class(0x0100, 0, 0) == MCA_CC_CACHE);
    CHECK(mca_code_class(0x0134, 0, 0) == MCA_CC_CACHE); /* DRD, data, L0 */
    CHECK(mca_code_class(0x017A, 0, 0) == MCA_CC_CACHE); /* EVICT, gen, L2 */
    CHECK(mca_code_class(0x11FF, 0, 0) == MCA_CC_CACHE);
    /* Bus 0000_1PPT_RRRR_IILL. */
    CHECK(mca_code_class(0x0800, 0, 0) == MCA_CC_BUS);
    CHECK(mca_code_class(0x0E0F, 0, 0) == MCA_CC_BUS);
    CHECK(mca_code_class(0x0FFF, 0, 0) == MCA_CC_BUS);
    CHECK(mca_code_class(0x1E0B, 0, 0) == MCA_CC_BUS);
    /* Bits 15:13 set: not an architectural code. */
    CHECK(mca_code_class(0x2090, &t, &c) == MCA_CC_OTHER);
    CHECK(t == 0 && c == 0);
    CHECK(mca_code_class(0x8000, 0, 0) == MCA_CC_OTHER);
    CHECK(mca_code_class(0xFFFF, 0, 0) == MCA_CC_OTHER);
    CHECK(mca_code_class(0x0200, 0, 0) == MCA_CC_OTHER);
    CHECK(mca_code_class(0x0040, 0, 0) == MCA_CC_OTHER);
}

static void test_severity_intel(void)
{
    uint64_t q = CAP_QEMU, noser = 10ull | MCA_MCG_CTL_P;
    uint32_t I = MCA_VENDOR_INTEL;
    CHECK(mca_severity(I, q, 0) == MCA_SEV_NONE);
    CHECK(mca_severity(I, q, UC | PCC | EN | 0x5) == MCA_SEV_NONE); /* VAL clear */
    CHECK(mca_severity(I, q, VAL | EN | 0x90) == MCA_SEV_CORRECTED);
    CHECK(mca_severity(I, q, VAL | OVER | EN | 0x90) == MCA_SEV_CORRECTED);   /* OVER alone */
    CHECK(mca_severity(I, q, VAL | UC | EN | 0x90) == MCA_SEV_UC_RECOV);      /* UCNA */
    CHECK(mca_severity(I, q, VAL | UC | 0x90) == MCA_SEV_UC_RECOV);           /* UCNA, EN=0 */
    CHECK(mca_severity(I, q, VAL | UC | EN | S_ | 0xC0) == MCA_SEV_UC_RECOV); /* SRAO */
    CHECK(mca_severity(I, q, VAL | UC | EN | S_ | AR_ | 0x134) == MCA_SEV_ACTION_REQ);
    CHECK(mca_severity(I, q, VAL | UC | EN | AR_ | 0x134) == MCA_SEV_ACTION_REQ);
    CHECK(mca_severity(I, q, VAL | UC | EN | PCC | 0x5) == MCA_SEV_FATAL);
    CHECK(mca_severity(I, q, VAL | UC | EN | PCC | S_ | 0x5) == MCA_SEV_FATAL);
    CHECK(mca_severity(I, q, VAL | UC | OVER | EN | 0x90) == MCA_SEV_FATAL);
    CHECK(mca_severity(I, q, VAL | PCC | EN | 0x5) == MCA_SEV_CORRECTED); /* PCC needs UC */
    /* No MCG_SER_P: no software recovery, every UC is fatal. */
    CHECK(mca_severity(I, noser, VAL | UC | EN | 0x90) == MCA_SEV_FATAL);
    CHECK(mca_severity(I, noser, VAL | UC | EN | S_ | 0xC0) == MCA_SEV_FATAL);
    CHECK(mca_severity(I, noser, VAL | EN | 0x90) == MCA_SEV_CORRECTED);
    /* An unknown vendor follows the architectural (Intel) rules. */
    CHECK(mca_severity(MCA_VENDOR_OTHER, q, VAL | UC | EN | S_ | AR_) == MCA_SEV_ACTION_REQ);
    CHECK(mca_severity(MCA_VENDOR_OTHER, noser, VAL | UC) == MCA_SEV_FATAL);
    /* Bit 44 means nothing on Intel. */
    CHECK(mca_severity(I, q, VAL | DEFER) == MCA_SEV_CORRECTED);
}

static void test_severity_amd(void)
{
    uint32_t A = MCA_VENDOR_AMD;
    uint64_t c = CAP_EPYC;
    CHECK(mca_severity(A, c, 0) == MCA_SEV_NONE);
    CHECK(mca_severity(A, c, VAL | EN | B(46)) == MCA_SEV_CORRECTED);
    CHECK(mca_severity(A, c, VAL | DEFER) == MCA_SEV_DEFERRED);
    CHECK(mca_severity(A, c, VAL | OVER | DEFER) == MCA_SEV_DEFERRED);
    CHECK(mca_severity(A, c, VAL | UC | EN) == MCA_SEV_UC_RECOV);
    CHECK(mca_severity(A, c, VAL | UC | EN | S_) == MCA_SEV_UC_RECOV); /* 56: core id */
    CHECK(mca_severity(A, c, VAL | UC | EN | TCC_) == MCA_SEV_ACTION_REQ);
    CHECK(mca_severity(A, c, VAL | UC | EN | PCC) == MCA_SEV_FATAL);
    CHECK(mca_severity(A, c, VAL | UC | EN | PCC | TCC_) == MCA_SEV_FATAL);
    CHECK(mca_severity(A, c, VAL | UC | OVER | EN) == MCA_SEV_FATAL);
    /* AMD has no SER_P dependency. */
    CHECK(mca_severity(A, 0, VAL | UC | EN) == MCA_SEV_UC_RECOV);
}

/* Intel corrected DRAM ECC as QEMU's monitor injects it:
 * mce 0 1 0x9c00000000010090 0 0x1000 0. */
static void test_decode_intel_corrected_dram(void)
{
    mca_init(&g_s, MCA_VENDOR_INTEL, CAP_QEMU, 0);
    uint64_t st = VAL | EN | MISCV | ADDRV | (0x0001ull << 16) | 0x0090;
    CHECK(st == 0x9c00000000010090ull);
    mca_raw_t r = raw(1, st, 0x1000, (MCA_MISC_MODE_PHYS << 6) | 6);
    mca_rec_t d;
    CHECK(mca_decode(&g_s, &r, &d) == MCA_SEV_CORRECTED);
    CHECK(d.sev == MCA_SEV_CORRECTED);
    CHECK(d.bank == 1);
    CHECK(d.flags & MCA_F_VALID);
    CHECK(d.flags & MCA_F_EN);
    CHECK(d.flags & MCA_F_MISCV);
    CHECK(d.flags & MCA_F_ADDRV);
    CHECK(!(d.flags & MCA_F_UC));
    CHECK(!(d.flags & MCA_F_PCC));
    CHECK(!(d.flags & MCA_F_OVER));
    CHECK(!(d.flags & MCA_F_S));
    CHECK(!(d.flags & MCA_F_CEC)); /* no MCG_CMCI_P on this CPU */
    CHECK(d.flags & MCA_F_MEMORY);
    CHECK(!(d.flags & MCA_F_DRAM_ECC)); /* that flag is the AMD UMC one */
    CHECK(d.mcacod == 0x0090);
    CHECK(d.mscod == 0x0001);
    CHECK(d.cclass == MCA_CC_MEMORY);
    CHECK(d.mem_txn == 1 && d.mem_chan == 0);
    CHECK(d.addr_kind == MCA_ADDR_PHYS);
    CHECK(d.addr_lsb == 6);
    CHECK(d.addr == 0x1000);
    CHECK(d.raw_addr == 0x1000);
    CHECK(d.status == st);
    CHECK(d.smca == MCA_SMCA_NONE);
    CHECK(d.seq == 0); /* decode does not number */

    /* The LSB masks the low bits: a cache-line granular address. */
    r = raw(1, st, 0x12345678ABCDull, (MCA_MISC_MODE_PHYS << 6) | 6);
    CHECK(mca_decode(&g_s, &r, &d) == MCA_SEV_CORRECTED);
    CHECK(d.addr == 0x12345678ABC0ull);
    CHECK(d.raw_addr == 0x12345678ABCDull);
    /* Page granular (LSB 12). */
    r = raw(1, st, 0x12345678ABCDull, (MCA_MISC_MODE_PHYS << 6) | 12);
    mca_decode(&g_s, &r, &d);
    CHECK(d.addr == 0x12345678A000ull);
    CHECK(d.addr_lsb == 12);
    /* MISC says linear address mode: not physical. */
    r = raw(1, st, 0x7000, (1u << 6) | 0);
    mca_decode(&g_s, &r, &d);
    CHECK(d.addr_kind == MCA_ADDR_OTHER);
    /* MISCV clear: MISC is ignored, address taken as physical, LSB 0. */
    r = raw(1, st & ~MISCV, 0x7001, (1u << 6) | 12);
    mca_decode(&g_s, &r, &d);
    CHECK(d.addr_kind == MCA_ADDR_PHYS && d.addr == 0x7001 && d.addr_lsb == 0);
    CHECK(!(d.flags & MCA_F_MISCV));
    /* ADDRV clear: no address reported at all. */
    r = raw(1, st & ~ADDRV, 0x7000, 0);
    mca_decode(&g_s, &r, &d);
    CHECK(d.addr_kind == MCA_ADDR_NONE && d.addr == 0 && d.raw_addr == 0x7000);
    /* LSB 63: only bit 63 survives. */
    r = raw(1, st, 0xFFFFFFFFFFFFFFFFull, (MCA_MISC_MODE_PHYS << 6) | 63);
    mca_decode(&g_s, &r, &d);
    CHECK(d.addr == 0x8000000000000000ull);
}

static void test_decode_intel_cec_and_recovery(void)
{
    mca_init(&g_s, MCA_VENDOR_INTEL, CAP_XEON, 0);
    CHECK(g_s.nbanks == 20 && g_s.clamped == 0);
    /* Corrected, count 5, memory read ch 2 with F bit. */
    uint64_t st = VAL | EN | ADDRV | (5ull << MCA_ST_CEC_SH) | 0x1092;
    mca_raw_t r = raw(7, st, 0xABC000, 0);
    mca_rec_t d;
    CHECK(mca_decode(&g_s, &r, &d) == MCA_SEV_CORRECTED);
    CHECK(d.flags & MCA_F_CEC);
    CHECK(d.cec == 5);
    CHECK(d.mem_txn == 1 && d.mem_chan == 2);
    /* Max count 0x7FFF; bits above 52 are not count. */
    st = VAL | EN | (0x7FFFull << MCA_ST_CEC_SH) | B(53) | B(37) | 0x90;
    r = raw(7, st, 0, 0);
    mca_decode(&g_s, &r, &d);
    CHECK(d.cec == 0x7FFF);

    /* SRAO: memory scrub, UC, S, not AR. */
    st = VAL | UC | EN | MISCV | ADDRV | S_ | 0x00C0;
    CHECK(st == 0xBD000000000000C0ull);
    r = raw(3, st, 0x40000, (MCA_MISC_MODE_PHYS << 6) | 12);
    CHECK(mca_decode(&g_s, &r, &d) == MCA_SEV_UC_RECOV);
    CHECK((d.flags & (MCA_F_UC | MCA_F_S)) == (MCA_F_UC | MCA_F_S));
    CHECK(!(d.flags & MCA_F_AR));
    CHECK(d.mem_txn == 4 && d.addr == 0x40000);

    /* SRAR: data load, UC, S, AR. */
    st = VAL | UC | EN | MISCV | ADDRV | S_ | AR_ | 0x0134;
    CHECK(st == 0xBD80000000000134ull);
    r = raw(1, st, 0x5000, (MCA_MISC_MODE_PHYS << 6) | 6);
    CHECK(mca_decode(&g_s, &r, &d) == MCA_SEV_ACTION_REQ);
    CHECK(d.flags & MCA_F_AR);
    CHECK(d.cclass == MCA_CC_CACHE);
    CHECK(!(d.flags & MCA_F_MEMORY));

    /* PCC fatal: internal parity. */
    st = VAL | UC | EN | PCC | 0x0005;
    CHECK(st == 0xB200000000000005ull);
    r = raw(0, st, 0, 0);
    CHECK(mca_decode(&g_s, &r, &d) == MCA_SEV_FATAL);
    CHECK(d.flags & MCA_F_PCC);
    CHECK(d.cclass == MCA_CC_INTERNAL_PAR);
    CHECK(d.addr_kind == MCA_ADDR_NONE);

    /* Overflowed uncorrected. */
    st = VAL | OVER | UC | EN | 0x0090;
    r = raw(2, st, 0, 0);
    CHECK(mca_decode(&g_s, &r, &d) == MCA_SEV_FATAL);
    CHECK(d.flags & MCA_F_OVER);

    /* Invalid record: everything decoded, severity none. */
    r = raw(2, 0x0000000000000090ull, 0, 0);
    CHECK(mca_decode(&g_s, &r, &d) == MCA_SEV_NONE);
    CHECK(!(d.flags & MCA_F_VALID));

    /* S and AR are only read with MCG_SER_P. */
    mca_init(&g_s, MCA_VENDOR_INTEL, 10ull, 0);
    r = raw(1, VAL | EN | S_ | AR_ | 0x90, 0, 0);
    mca_decode(&g_s, &r, &d);
    CHECK(!(d.flags & (MCA_F_S | MCA_F_AR)));
    /* And MISC is not an LSB without SER_P. */
    r = raw(1, VAL | EN | MISCV | ADDRV | 0x90, 0x1FFF, 12);
    mca_decode(&g_s, &r, &d);
    CHECK(d.addr == 0x1FFF && d.addr_lsb == 0 && d.addr_kind == MCA_ADDR_PHYS);
    /* Intel never fills the AMD fields. */
    r = raw(1, VAL | EN | TCC_ | DEFER | B(43) | B(53) | (0x3Full << 16), 0, 0);
    mca_decode(&g_s, &r, &d);
    CHECK(!(d.flags & (MCA_F_TCC | MCA_F_DEFER | MCA_F_POISON | MCA_F_SYNDV)));
    CHECK(d.xec == 0);
}

static void test_smca(void)
{
    CHECK(mca_smca_type(0) == MCA_SMCA_NONE);
    CHECK(mca_smca_type(ipid(0xB0, 0x0, 0)) == MCA_SMCA_LS);
    CHECK(mca_smca_type(ipid(0xB0, 0x10, 0)) == MCA_SMCA_LS_V2);
    CHECK(mca_smca_type(ipid(0xB0, 0x1, 0)) == MCA_SMCA_IF);
    CHECK(mca_smca_type(ipid(0xB0, 0x2, 0)) == MCA_SMCA_L2);
    CHECK(mca_smca_type(ipid(0xB0, 0x3, 0)) == MCA_SMCA_DE);
    CHECK(mca_smca_type(ipid(0xB0, 0x5, 0)) == MCA_SMCA_EX);
    CHECK(mca_smca_type(ipid(0xB0, 0x6, 0)) == MCA_SMCA_FP);
    CHECK(mca_smca_type(ipid(0xB0, 0x7, 0x2)) == MCA_SMCA_L3);
    CHECK(mca_smca_type(ipid(0xB0, 0x4, 0)) == MCA_SMCA_UNKNOWN);
    CHECK(mca_smca_type(ipid(0x2E, 0x0, 0)) == MCA_SMCA_CS);
    CHECK(mca_smca_type(ipid(0x2E, 0x1, 0)) == MCA_SMCA_PIE);
    CHECK(mca_smca_type(ipid(0x2E, 0x2, 0)) == MCA_SMCA_CS_V2);
    CHECK(mca_smca_type(ipid(0x96, 0x0, 0x50f00)) == MCA_SMCA_UMC);
    CHECK(mca_smca_type(ipid(0x96, 0x1, 0)) == MCA_SMCA_UMC_V2);
    CHECK(mca_smca_type(ipid(0x05, 0x0, 0)) == MCA_SMCA_PB);
    CHECK(mca_smca_type(ipid(0xFF, 0x0, 0)) == MCA_SMCA_PSP);
    CHECK(mca_smca_type(ipid(0x01, 0x0, 0)) == MCA_SMCA_SMU);
    CHECK(mca_smca_type(ipid(0x18, 0x0, 0)) == MCA_SMCA_NBIO);
    CHECK(mca_smca_type(ipid(0x46, 0x0, 0)) == MCA_SMCA_PCIE);
    CHECK(mca_smca_type(ipid(0x97, 0x0, 0)) == MCA_SMCA_UNKNOWN);
    /* An instance id alone is not a type. */
    CHECK(mca_smca_type(0x1234) == MCA_SMCA_UNKNOWN);
    /* Bits 47:44 are not part of HWID or McaType. */
    CHECK(mca_smca_type(ipid(0x96, 0x0, 0) | (0xFull << 44)) == MCA_SMCA_UMC);
    CHECK(MCA_IPID_HWID(ipid(0xABC, 0x1234, 0x89ABCDEF)) == 0xABC);
    CHECK(MCA_IPID_TYPE(ipid(0xABC, 0x1234, 0x89ABCDEF)) == 0x1234);
    CHECK(MCA_IPID_INST(ipid(0xABC, 0x1234, 0x89ABCDEF)) == 0x89ABCDEF);

    mca_init(&g_s, MCA_VENDOR_AMD, CAP_EPYC, 1);
    CHECK(g_s.smca == 1 && g_s.nbanks == 64);
    /* UMC corrected DRAM ECC: CECC, SyndV, AddrV, XEC 0, AddrLsb 6. */
    uint64_t st = VAL | EN | ADDRV | MISCV | MCA_ST_SYNDV | MCA_ST_CECC | (0x00ull << 16) | 0x009F;
    uint64_t a = (6ull << 56) | 0x0000001234567ABCull;
    mca_raw_t r = raw_smca(15, st, a, ipid(0x96, 0x0, 0x150f00), 0x5A5A0000000001ull);
    mca_rec_t d;
    CHECK(mca_decode(&g_s, &r, &d) == MCA_SEV_CORRECTED);
    CHECK(d.smca == MCA_SMCA_UMC);
    CHECK(d.flags & MCA_F_MEMORY);
    CHECK(d.flags & MCA_F_DRAM_ECC);
    CHECK(d.flags & MCA_F_SYNDV);
    CHECK(d.xec == 0);
    CHECK(d.addr_kind == MCA_ADDR_UMC_NORM);
    CHECK(d.addr_lsb == 6);
    CHECK(d.addr == 0x0000001234567A80ull);
    CHECK(d.raw_addr == a);
    CHECK(d.ipid == ipid(0x96, 0x0, 0x150f00));
    CHECK(d.synd == 0x5A5A0000000001ull);
    CHECK(!(d.flags & MCA_F_CEC)); /* Intel field */
    /* UMC, XEC 1 (data poison): memory but not DRAM ECC. */
    r.status = st | (0x01ull << 16);
    mca_decode(&g_s, &r, &d);
    CHECK(d.xec == 1 && (d.flags & MCA_F_MEMORY) && !(d.flags & MCA_F_DRAM_ECC));
    /* XEC is 6 bits: 21:16. */
    r.status = st | (0x3Full << 16) | (1ull << 22);
    mca_decode(&g_s, &r, &d);
    CHECK(d.xec == 0x3F);
    /* UMC uncorrected ECC with poison: recoverable, not fatal. */
    r.status = VAL | UC | EN | ADDRV | MCA_ST_UECC | MCA_ST_POISON | 0x009F;
    CHECK(mca_decode(&g_s, &r, &d) == MCA_SEV_UC_RECOV);
    CHECK(d.flags & MCA_F_POISON);
    /* Deferred on the UMC. */
    r.status = VAL | EN | ADDRV | DEFER | 0x009F;
    CHECK(mca_decode(&g_s, &r, &d) == MCA_SEV_DEFERRED);
    CHECK(d.flags & MCA_F_DEFER);
    /* Load-store bank: physical address, TCC is action required. */
    r = raw_smca(0, VAL | UC | EN | ADDRV | TCC_ | 0x0134, (0ull << 56) | 0xFEED000,
                 ipid(0xB0, 0, 0), 0);
    CHECK(mca_decode(&g_s, &r, &d) == MCA_SEV_ACTION_REQ);
    CHECK(d.smca == MCA_SMCA_LS && d.addr_kind == MCA_ADDR_PHYS && d.addr == 0xFEED000);
    CHECK(d.flags & MCA_F_TCC);
    CHECK(!(d.flags & MCA_F_MEMORY));
    /* PSP bank address is not a memory address. */
    r = raw_smca(20, VAL | EN | ADDRV | 0x0005, 0x10, ipid(0xFF, 0, 0), 0);
    mca_decode(&g_s, &r, &d);
    CHECK(d.addr_kind == MCA_ADDR_OTHER);
    /* Unknown IPID: still decoded, type unknown. */
    r = raw_smca(21, VAL | EN | 0x0005, 0, ipid(0x123, 0x9, 0), 0);
    mca_decode(&g_s, &r, &d);
    CHECK(d.smca == MCA_SMCA_UNKNOWN && d.addr_kind == MCA_ADDR_NONE);
    /* Raw not read through the SMCA map: IPID ignored, address kept whole. */
    r = raw(15, st, a, 0);
    r.ipid = ipid(0x96, 0, 0);
    mca_decode(&g_s, &r, &d);
    CHECK(d.smca == MCA_SMCA_NONE && d.ipid == 0);
    CHECK(d.addr == a && d.addr_kind == MCA_ADDR_PHYS);

    /* SMCA requested on a non-AMD vendor is refused. */
    mca_init(&g_s, MCA_VENDOR_INTEL, CAP_QEMU, 1);
    CHECK(g_s.smca == 0);
    /* AMD without SMCA (QEMU's qemu64): legacy layout. */
    mca_init(&g_s, MCA_VENDOR_AMD, CAP_QEMU, 0);
    CHECK(g_s.smca == 0);
    r = raw_smca(4, VAL | EN | ADDRV | 0x009F, a, ipid(0x96, 0, 0), 0);
    mca_decode(&g_s, &r, &d);
    CHECK(d.smca == MCA_SMCA_NONE && d.addr == a);
    CHECK(d.flags & MCA_F_MEMORY);
}

static void test_ring_and_counters(void)
{
    mca_init(&g_s, MCA_VENDOR_INTEL, CAP_QEMU, 0);
    CHECK(mca_ring_count(&g_s) == 0);
    mca_rec_t d;
    CHECK(mca_ring_get(&g_s, 0, &d) == -1);

    /* Invalid status is not logged. */
    mca_raw_t r = raw(0, 0x90, 0, 0);
    CHECK(mca_log(&g_s, &r, MCA_SRC_POLL, &d) == MCA_SEV_NONE);
    CHECK(mca_ring_count(&g_s) == 0);
    CHECK(g_s.seq == 0);
    /* Out-of-range bank is rejected and counted, nothing logged. */
    r = raw(10, VAL | EN | 0x90, 0, 0);
    CHECK(mca_log(&g_s, &r, MCA_SRC_POLL, &d) == -1);
    CHECK(g_s.rejected == 1);
    CHECK(mca_ring_count(&g_s) == 0);
    r.bank = 0xFFFFFFFFu;
    CHECK(mca_log(&g_s, &r, MCA_SRC_POLL, 0) == -1);
    CHECK(g_s.rejected == 2);
    CHECK(mca_log(0, &r, 0, 0) == -1);
    CHECK(mca_log(&g_s, 0, 0, 0) == -1);

    /* Fill exactly to capacity: nothing dropped. */
    for (uint32_t i = 0; i < MCA_RING_CAP; i++) {
        r = raw(i % 10, VAL | EN | ADDRV | 0x90, 0x1000u * (i + 1), 0);
        CHECK(mca_log(&g_s, &r, MCA_SRC_BOOT, &d) == MCA_SEV_CORRECTED);
        CHECK(d.seq == i + 1);
        CHECK(d.source == MCA_SRC_BOOT);
    }
    CHECK(mca_ring_count(&g_s) == MCA_RING_CAP);
    CHECK(g_s.dropped == 0);
    CHECK(mca_ring_get(&g_s, 0, &d) == 0 && d.seq == 1 && d.raw_addr == 0x1000);
    CHECK(mca_ring_get(&g_s, MCA_RING_CAP - 1, &d) == 0 && d.seq == MCA_RING_CAP);
    CHECK(mca_ring_get(&g_s, MCA_RING_CAP, &d) == -1);

    /* One more: the oldest goes, counted. */
    r = raw(9, VAL | UC | EN | 0x90, 0xDEAD000, 0);
    CHECK(mca_log(&g_s, &r, MCA_SRC_MCE, &d) == MCA_SEV_UC_RECOV);
    CHECK(d.seq == MCA_RING_CAP + 1 && d.source == MCA_SRC_MCE);
    CHECK(mca_ring_count(&g_s) == MCA_RING_CAP);
    CHECK(g_s.dropped == 1);
    CHECK(mca_ring_get(&g_s, 0, &d) == 0 && d.seq == 2);
    CHECK(mca_ring_get(&g_s, MCA_RING_CAP - 1, &d) == 0 && d.seq == MCA_RING_CAP + 1);
    CHECK(d.raw_addr == 0xDEAD000 && d.bank == 9);

    /* Many more: the ring stays bounded and in order. */
    for (uint32_t i = 0; i < 3 * MCA_RING_CAP + 5; i++) {
        r = raw(2, VAL | EN | 0x90, i, 0);
        mca_log(&g_s, &r, MCA_SRC_POLL, 0);
    }
    CHECK(mca_ring_count(&g_s) == MCA_RING_CAP);
    CHECK(g_s.dropped == 1 + 3 * MCA_RING_CAP + 5);
    int ordered = 1;
    for (uint32_t i = 1; i < MCA_RING_CAP; i++) {
        mca_rec_t p, q;
        mca_ring_get(&g_s, i - 1, &p);
        mca_ring_get(&g_s, i, &q);
        if (q.seq != p.seq + 1) ordered = 0;
    }
    CHECK(ordered);
    mca_ring_get(&g_s, MCA_RING_CAP - 1, &d);
    CHECK(d.seq == g_s.seq);
    CHECK(d.raw_addr == 3 * MCA_RING_CAP + 4);

    /* Severity totals. */
    CHECK(g_s.by_sev[MCA_SEV_CORRECTED] == MCA_RING_CAP + 3 * MCA_RING_CAP + 5);
    CHECK(g_s.by_sev[MCA_SEV_UC_RECOV] == 1);
    CHECK(g_s.by_sev[MCA_SEV_NONE] == 0);
    CHECK(mca_bank_uncorrected(&g_s, 9) == 1);
    CHECK(mca_bank_uncorrected(&g_s, 2) == 0);

    /* Free-running counters across the 32-bit wrap. */
    mca_init(&g_s, MCA_VENDOR_INTEL, CAP_QEMU, 0);
    g_s.head = g_s.tail = 0xFFFFFFF0u;
    for (uint32_t i = 0; i < MCA_RING_CAP + 3; i++) {
        r = raw(1, VAL | EN | 0x90, i, 0);
        mca_log(&g_s, &r, MCA_SRC_POLL, 0);
    }
    CHECK(mca_ring_count(&g_s) == MCA_RING_CAP);
    CHECK(g_s.dropped == 3);
    CHECK(mca_ring_get(&g_s, 0, &d) == 0 && d.raw_addr == 3);
    CHECK(mca_ring_get(&g_s, MCA_RING_CAP - 1, &d) == 0 && d.raw_addr == MCA_RING_CAP + 2);

    /* Dropped saturates. */
    g_s.dropped = 0xFFFFFFFFu;
    mca_log(&g_s, &r, MCA_SRC_POLL, 0);
    CHECK(g_s.dropped == 0xFFFFFFFFu);
    /* Null-safety of the readers. */
    CHECK(mca_ring_count(0) == 0);
    CHECK(mca_ring_get(0, 0, &d) == -1);
    CHECK(mca_ring_get(&g_s, 0, 0) == -1);
}

static void test_thresholds(void)
{
    mca_init(&g_s, MCA_VENDOR_INTEL, CAP_QEMU, 0);
    CHECK(g_s.threshold == MCA_DEFAULT_THRESHOLD);
    CHECK(MCA_DEFAULT_THRESHOLD == 16);
    mca_raw_t r = raw(3, VAL | EN | ADDRV | 0x90, 0x1000, 0);
    for (uint32_t i = 1; i < MCA_DEFAULT_THRESHOLD; i++) {
        mca_log(&g_s, &r, MCA_SRC_POLL, 0);
        CHECK(mca_bank_corrected(&g_s, 3) == i);
        CHECK(!mca_bank_degrading(&g_s, 3));
    }
    CHECK(mca_degrading_count(&g_s) == 0);
    mca_log(&g_s, &r, MCA_SRC_POLL, 0);
    CHECK(mca_bank_corrected(&g_s, 3) == MCA_DEFAULT_THRESHOLD);
    CHECK(mca_bank_degrading(&g_s, 3));
    CHECK(mca_degrading_count(&g_s) == 1);
    CHECK(!mca_bank_degrading(&g_s, 2));
    CHECK(!mca_bank_degrading(&g_s, 4));
    /* Uncorrected errors do not count toward degrading. */
    r = raw(4, VAL | UC | EN | 0x90, 0, 0);
    for (int i = 0; i < 40; i++) mca_log(&g_s, &r, MCA_SRC_MCE, 0);
    CHECK(mca_bank_corrected(&g_s, 4) == 0);
    CHECK(mca_bank_uncorrected(&g_s, 4) == 40);
    CHECK(!mca_bank_degrading(&g_s, 4));
    /* Out of range banks read as zero. */
    CHECK(mca_bank_corrected(&g_s, 10) == 0);
    CHECK(mca_bank_uncorrected(&g_s, 10) == 0);
    CHECK(!mca_bank_degrading(&g_s, 10));
    CHECK(mca_bank_corrected(0, 0) == 0);
    CHECK(!mca_bank_degrading(0, 0));
    CHECK(mca_degrading_count(0) == 0);

    /* Lowering the threshold applies to counts already held. */
    mca_init(&g_s, MCA_VENDOR_INTEL, CAP_QEMU, 0);
    r = raw(5, VAL | EN | 0x90, 0, 0);
    for (int i = 0; i < 3; i++) mca_log(&g_s, &r, MCA_SRC_POLL, 0);
    CHECK(!mca_bank_degrading(&g_s, 5));
    mca_set_threshold(&g_s, 3);
    CHECK(mca_bank_degrading(&g_s, 5));
    /* Raising it does not clear a latched mark. */
    mca_set_threshold(&g_s, 100);
    CHECK(mca_bank_degrading(&g_s, 5));
    /* Threshold 0: marking off. */
    mca_init(&g_s, MCA_VENDOR_INTEL, CAP_QEMU, 0);
    mca_set_threshold(&g_s, 0);
    for (int i = 0; i < 100; i++) mca_log(&g_s, &r, MCA_SRC_POLL, 0);
    CHECK(mca_bank_corrected(&g_s, 5) == 100);
    CHECK(!mca_bank_degrading(&g_s, 5));
    /* Threshold 1: the first error marks. */
    mca_init(&g_s, MCA_VENDOR_INTEL, CAP_QEMU, 0);
    mca_set_threshold(&g_s, 1);
    mca_log(&g_s, &r, MCA_SRC_POLL, 0);
    CHECK(mca_bank_degrading(&g_s, 5));

    /* The hardware corrected count is added, not just one per record. */
    mca_init(&g_s, MCA_VENDOR_INTEL, CAP_XEON, 0);
    r = raw(6, VAL | EN | (12ull << MCA_ST_CEC_SH) | 0x90, 0, 0);
    mca_log(&g_s, &r, MCA_SRC_POLL, 0);
    CHECK(mca_bank_corrected(&g_s, 6) == 12);
    CHECK(!mca_bank_degrading(&g_s, 6));
    mca_log(&g_s, &r, MCA_SRC_POLL, 0);
    CHECK(mca_bank_corrected(&g_s, 6) == 24);
    CHECK(mca_bank_degrading(&g_s, 6));
    /* A zero hardware count still counts the record itself. */
    r = raw(7, VAL | EN | 0x90, 0, 0);
    mca_log(&g_s, &r, MCA_SRC_POLL, 0);
    CHECK(mca_bank_corrected(&g_s, 7) == 1);
    /* Saturation. */
    g_s.corrected[7] = 0xFFFFFFFEu;
    r = raw(7, VAL | EN | (9ull << MCA_ST_CEC_SH) | 0x90, 0, 0);
    mca_log(&g_s, &r, MCA_SRC_POLL, 0);
    CHECK(mca_bank_corrected(&g_s, 7) == 0xFFFFFFFFu);

    /* AMD deferred errors count with corrected ones. */
    mca_init(&g_s, MCA_VENDOR_AMD, CAP_EPYC, 1);
    mca_set_threshold(&g_s, 2);
    r = raw_smca(15, VAL | EN | DEFER | 0x9F, 0, ipid(0x96, 0, 0), 0);
    mca_log(&g_s, &r, MCA_SRC_POLL, 0);
    r.status = VAL | EN | MCA_ST_CECC | 0x9F;
    mca_log(&g_s, &r, MCA_SRC_POLL, 0);
    CHECK(mca_bank_corrected(&g_s, 15) == 2);
    CHECK(mca_bank_degrading(&g_s, 15));
    CHECK(g_s.by_sev[MCA_SEV_DEFERRED] == 1);
    CHECK(g_s.by_sev[MCA_SEV_CORRECTED] == 1);
}

static void test_init_and_quirks(void)
{
    mca_init(&g_s, MCA_VENDOR_INTEL, 0xFFull | MCA_MCG_CTL_P, 0);
    CHECK(g_s.nbanks == MCA_MAX_BANKS);
    CHECK(g_s.clamped == 0xFF - MCA_MAX_BANKS);
    mca_init(&g_s, MCA_VENDOR_INTEL, 0, 0);
    CHECK(g_s.nbanks == 0 && g_s.clamped == 0);
    mca_raw_t r = raw(0, VAL | EN | 0x90, 0, 0);
    CHECK(mca_log(&g_s, &r, MCA_SRC_POLL, 0) == -1); /* no banks at all */
    mca_init(&g_s, MCA_VENDOR_AMD, 64, 1);
    CHECK(g_s.nbanks == 64 && g_s.clamped == 0);
    mca_init(0, 0, 0, 0);    /* no crash */
    mca_set_threshold(0, 3); /* no crash */

    CHECK(!mca_bank_ctl_writable(MCA_VENDOR_INTEL, 6, 0x0F, 0));
    CHECK(mca_bank_ctl_writable(MCA_VENDOR_INTEL, 6, 0x0F, 1));
    CHECK(mca_bank_ctl_writable(MCA_VENDOR_INTEL, 6, 0x1A, 0));
    CHECK(mca_bank_ctl_writable(MCA_VENDOR_INTEL, 6, 0x55, 0));
    CHECK(mca_bank_ctl_writable(MCA_VENDOR_INTEL, 15, 0x02, 0));
    CHECK(mca_bank_ctl_writable(MCA_VENDOR_AMD, 6, 0x0F, 0));
    CHECK(mca_bank_ctl_writable(MCA_VENDOR_AMD, 0x19, 0x11, 0));
    CHECK(!mca_bank_ctl_writable(MCA_VENDOR_AMD, 0x19, 0x11, MCA_MAX_BANKS));
}

static void test_resume_rule(void)
{
    uint64_t ripv = MCA_MCGS_RIPV | MCA_MCGS_MCIP;
    CHECK(mca_mc_may_resume(ripv, MCA_SEV_NONE));
    CHECK(mca_mc_may_resume(ripv, MCA_SEV_CORRECTED));
    CHECK(mca_mc_may_resume(ripv, MCA_SEV_DEFERRED));
    CHECK(mca_mc_may_resume(ripv, MCA_SEV_UC_RECOV));
    CHECK(!mca_mc_may_resume(ripv, MCA_SEV_ACTION_REQ));
    CHECK(!mca_mc_may_resume(ripv, MCA_SEV_FATAL));
    CHECK(!mca_mc_may_resume(MCA_MCGS_MCIP, MCA_SEV_CORRECTED));
    CHECK(!mca_mc_may_resume(MCA_MCGS_EIPV | MCA_MCGS_MCIP, MCA_SEV_UC_RECOV));
    CHECK(!mca_mc_may_resume(0, MCA_SEV_NONE));
}

static void test_format(void)
{
    char buf[MCA_TEXT_MAX];
    mca_init(&g_s, MCA_VENDOR_INTEL, CAP_QEMU, 0);
    mca_raw_t r = raw(1, 0x9c00000000010090ull, 0x1000, (MCA_MISC_MODE_PHYS << 6) | 6);
    mca_rec_t d;
    CHECK(mca_log(&g_s, &r, MCA_SRC_POLL, &d) == MCA_SEV_CORRECTED);
    uint32_t n = mca_format(&d, buf, sizeof buf);
    CHECK(n == strlen(buf));
    CHECK(strcmp(buf, "bank 1 corrected memory read ch0 status=0x9c00000000010090 "
                      "pa=0x0000000000001000 misc=0x0000000000000086 src=poll") == 0);

    r = raw(0, VAL | UC | EN | PCC | OVER | 0x5, 0, 0);
    mca_log(&g_s, &r, MCA_SRC_MCE, &d);
    mca_format(&d, buf, sizeof buf);
    CHECK(strcmp(buf, "bank 0 fatal internal-parity OVER PCC status=0xf200000000000005 src=mce") ==
          0);

    mca_init(&g_s, MCA_VENDOR_AMD, CAP_EPYC, 1);
    r = raw_smca(15, VAL | EN | ADDRV | MCA_ST_CECC | 0x9F, (6ull << 56) | 0x1040, ipid(0x96, 0, 0),
                 0);
    mca_log(&g_s, &r, MCA_SRC_BOOT, &d);
    mca_format(&d, buf, sizeof buf);
    CHECK(strcmp(buf, "bank 15 corrected memory read ch15 smca=umc dram-ecc "
                      "status=0x940040000000009f umc-addr=0x0000000000001040 src=boot") == 0);

    mca_init(&g_s, MCA_VENDOR_INTEL, CAP_XEON, 0);
    r = raw(19, VAL | UC | EN | S_ | AR_ | ADDRV | MISCV | (3ull << MCA_ST_CEC_SH) | 0x134, 0x5000,
            (1u << 6));
    mca_log(&g_s, &r, MCA_SRC_MCE, &d);
    mca_format(&d, buf, sizeof buf);
    CHECK(strcmp(buf, "bank 19 uncorrected-action-required cache AR status=0xbd8000c000000134 "
                      "addr=0x0000000000005000 misc=0x0000000000000040 count=3 src=mce") == 0);

    /* Truncation: bounded and terminated at every length. */
    uint32_t full = mca_format(&d, buf, sizeof buf);
    int ok = 1;
    for (uint32_t len = 1; len <= full + 1; len++) {
        char small[MCA_TEXT_MAX];
        memset(small, 'Z', sizeof small);
        uint32_t m = mca_format(&d, small, len);
        if (m != (len - 1 < full ? len - 1 : full)) ok = 0;
        if (small[m] != 0) ok = 0;
        if (len < sizeof small && small[len] != 'Z') ok = 0;
        if (strncmp(small, buf, m) != 0) ok = 0;
    }
    CHECK(ok);
    CHECK(mca_format(&d, buf, 0) == 0);
    CHECK(mca_format(&d, 0, 10) == 0);
    CHECK(mca_format(0, buf, sizeof buf) == 0 && buf[0] == 0);

    /* The longest line this decoder can produce fits MCA_TEXT_MAX. */
    mca_rec_t w;
    memset(&w, 0, sizeof w);
    w.bank = 0xFFFF;
    w.sev = MCA_SEV_ACTION_REQ;
    w.cclass = MCA_CC_MEMORY;
    w.mem_txn = 3;
    w.mem_chan = 15;
    w.smca = MCA_SMCA_UNKNOWN;
    w.flags = 0xFFFFFFFFu;
    w.addr_kind = MCA_ADDR_UMC_NORM;
    w.cec = 0x7FFF;
    w.source = MCA_SRC_POLL;
    char big[512];
    uint32_t longest = mca_format(&w, big, sizeof big);
    CHECK(longest + 1 <= MCA_TEXT_MAX);
    w.cclass = MCA_CC_UCODE_ROM;
    CHECK(mca_format(&w, big, sizeof big) + 1 <= MCA_TEXT_MAX);

    /* Every name is defined, including out of range. */
    for (uint32_t i = 0; i <= MCA_SEV_FATAL + 1; i++) CHECK(mca_sev_name(i)[0] != 0);
    for (uint32_t i = 0; i <= MCA_CC_OTHER + 1; i++) CHECK(mca_class_name(i)[0] != 0);
    for (uint32_t i = 0; i <= MCA_SMCA_UNKNOWN + 1; i++) CHECK(mca_smca_name(i)[0] != 0);
    for (uint32_t i = 0; i <= MCA_SRC_MCE + 1; i++) CHECK(mca_src_name(i)[0] != 0);
    CHECK(strcmp(mca_sev_name(MCA_SEV_UC_RECOV), "uncorrected-recoverable") == 0);
    CHECK(strcmp(mca_smca_name(MCA_SMCA_UMC), "umc") == 0);
}

/* Every 16-bit code against the SDM patterns, matched as text: a second
 * implementation that shares no masks with the decoder. 'F' is the filter
 * bit (either value), lower-case letters are fields (any value). */
static int pat_match(const char *pat, uint16_t c)
{
    for (int i = 0; i < 16; i++) {
        char p = pat[i];
        int bit = (c >> (15 - i)) & 1;
        if (p == '0' && bit) return 0;
        if (p == '1' && !bit) return 0;
    }
    return 1;
}

static uint32_t ref_class(uint16_t c)
{
    static const struct {
        const char *pat;
        uint32_t cc;
    } simple[] = {
        {"0000000000000000", MCA_CC_NONE},       {"0000000000000001", MCA_CC_UNCLASSIFIED},
        {"0000000000000010", MCA_CC_UCODE_ROM},  {"0000000000000011", MCA_CC_EXTERNAL},
        {"0000000000000100", MCA_CC_FRC},        {"0000000000000101", MCA_CC_INTERNAL_PAR},
        {"0000000000000110", MCA_CC_SMM_ACCESS}, {"0000010000000000", MCA_CC_INTERNAL_TIMER},
        {"000001xxxxxxxxxx", MCA_CC_INTERNAL},   {"000F1pptrrrriill", MCA_CC_BUS},
        {"000F0001rrrrttll", MCA_CC_CACHE},      {"000F00001mmmcccc", MCA_CC_MEMORY},
        {"000F00000001ttll", MCA_CC_TLB},        {"000F0000000011ll", MCA_CC_GENERIC_CACHE},
    };
    for (uint32_t i = 0; i < sizeof simple / sizeof simple[0]; i++)
        if (pat_match(simple[i].pat, c)) return simple[i].cc;
    return MCA_CC_OTHER;
}

static void test_code_classes_exhaustive(void)
{
    uint32_t bad = 0, mem = 0;
    for (uint32_t c = 0; c <= 0xFFFF; c++) {
        uint8_t t = 0xEE, ch = 0xEE;
        uint32_t got = mca_code_class((uint16_t) c, &t, &ch);
        uint32_t want = ref_class((uint16_t) c);
        CHECK(got == want);
        if (got != want && bad++ < 5) printf("  code 0x%04x: got %u want %u\n", c, got, want);
        if (want == MCA_CC_MEMORY) {
            mem++;
            CHECK(t == ((c >> 4) & 7) && ch == (c & 0xF));
        } else {
            CHECK(t == 0 && ch == 0);
        }
    }
    CHECK(mem == 2 * 128); /* F x MMM x CCCC */
}

/* Severity of every combination of the bits that decide it, against the
 * rule list written as a table in the order a reader applies it. */
static uint32_t ref_sev(uint32_t vendor, int ser, uint64_t st)
{
    struct rule {
        uint64_t mask, value;
        uint32_t sev;
    };
    static const struct rule amd[] = {
        {VAL, 0, MCA_SEV_NONE},
        {UC | PCC, UC | PCC, MCA_SEV_FATAL},
        {UC | OVER, UC | OVER, MCA_SEV_FATAL},
        {UC | TCC_, UC | TCC_, MCA_SEV_ACTION_REQ},
        {UC, UC, MCA_SEV_UC_RECOV},
        {DEFER, DEFER, MCA_SEV_DEFERRED},
        {0, 0, MCA_SEV_CORRECTED},
    };
    static const struct rule intel_ser[] = {
        {VAL, 0, MCA_SEV_NONE},
        {UC | PCC, UC | PCC, MCA_SEV_FATAL},
        {UC | OVER, UC | OVER, MCA_SEV_FATAL},
        {UC | AR_, UC | AR_, MCA_SEV_ACTION_REQ},
        {UC, UC, MCA_SEV_UC_RECOV},
        {0, 0, MCA_SEV_CORRECTED},
    };
    static const struct rule intel_noser[] = {
        {VAL, 0, MCA_SEV_NONE},
        {UC, UC, MCA_SEV_FATAL},
        {0, 0, MCA_SEV_CORRECTED},
    };
    const struct rule *r = vendor == MCA_VENDOR_AMD ? amd : ser ? intel_ser : intel_noser;
    for (;; r++)
        if ((st & r->mask) == r->value) return r->sev;
}

static void test_severity_exhaustive(void)
{
    static const uint64_t bits[] = {VAL, OVER, UC, EN, MISCV, ADDRV, PCC, S_, AR_, DEFER, B(43)};
    const uint32_t nb = sizeof bits / sizeof bits[0];
    for (uint32_t m = 0; m < (1u << nb); m++) {
        uint64_t st = 0x0090;
        for (uint32_t i = 0; i < nb; i++)
            if (m & (1u << i)) st |= bits[i];
        CHECK(mca_severity(MCA_VENDOR_INTEL, CAP_QEMU, st) == ref_sev(MCA_VENDOR_INTEL, 1, st));
        CHECK(mca_severity(MCA_VENDOR_INTEL, 10, st) == ref_sev(MCA_VENDOR_INTEL, 0, st));
        CHECK(mca_severity(MCA_VENDOR_AMD, CAP_EPYC, st) == ref_sev(MCA_VENDOR_AMD, 0, st));
        /* decode agrees with severity and reports the bits it saw */
        mca_init(&g_s, MCA_VENDOR_INTEL, CAP_QEMU, 0);
        mca_raw_t r = raw(1, st, 0x1000, 0);
        mca_rec_t d;
        CHECK(mca_decode(&g_s, &r, &d) == ref_sev(MCA_VENDOR_INTEL, 1, st));
        CHECK(!!(d.flags & MCA_F_UC) == !!(st & UC));
        CHECK(!!(d.flags & MCA_F_PCC) == !!(st & PCC));
        CHECK(!!(d.flags & MCA_F_OVER) == !!(st & OVER));
        CHECK(!!(d.flags & MCA_F_VALID) == !!(st & VAL));
        CHECK(!!(d.flags & MCA_F_AR) == !!(st & AR_));
        CHECK((d.addr_kind != MCA_ADDR_NONE) == !!(st & ADDRV));
    }
}

/* The ring after n records, for every n up to four times its size. */
static void test_ring_sweep(void)
{
    for (uint32_t n = 0; n <= 4 * MCA_RING_CAP; n++) {
        mca_init(&g_s, MCA_VENDOR_INTEL, CAP_QEMU, 0);
        mca_raw_t r = raw(0, VAL | EN | 0x90, 0, 0);
        for (uint32_t i = 0; i < n; i++) {
            r.addr = i;
            mca_log(&g_s, &r, MCA_SRC_POLL, 0);
        }
        uint32_t keep = n < MCA_RING_CAP ? n : MCA_RING_CAP;
        mca_rec_t d;
        CHECK(mca_ring_count(&g_s) == keep);
        CHECK(g_s.dropped == n - keep);
        CHECK(mca_bank_corrected(&g_s, 0) == n);
        if (keep) {
            CHECK(mca_ring_get(&g_s, 0, &d) == 0 && d.seq == n - keep + 1);
            CHECK(mca_ring_get(&g_s, keep - 1, &d) == 0 && d.seq == n);
        }
        CHECK(mca_ring_get(&g_s, keep, &d) == -1);
    }
}

int main(void)
{
    test_bits_are_the_sdm_bits();
    test_code_classes();
    test_severity_intel();
    test_severity_amd();
    test_decode_intel_corrected_dram();
    test_decode_intel_cec_and_recovery();
    test_smca();
    test_ring_and_counters();
    test_thresholds();
    test_init_and_quirks();
    test_resume_rule();
    test_format();
    test_code_classes_exhaustive();
    test_severity_exhaustive();
    test_ring_sweep();
    printf("%s: %d checks, %d failed\n", failures ? "FAILED" : "ok", checks, failures);
    return failures ? 1 : 0;
}
