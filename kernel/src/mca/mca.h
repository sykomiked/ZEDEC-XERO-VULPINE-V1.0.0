/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mca.h — machine-check architecture: decode, classify and keep the record.
 *
 * Pure and architecture-neutral: the caller reads the registers (on x86_64,
 * kernel/arch/x86_64/mca_x86.c) and hands the raw values here. Nothing in this
 * module touches hardware, allocates or calls libc, so the same decoder runs
 * in the kernel and in the host test (test_mca.c).
 *
 * Sources for every bit below:
 *   Intel SDM vol. 3B ch. 16 (IA32_MCG_CAP, IA32_MCG_STATUS, IA32_MCi_STATUS,
 *     IA32_MCi_MISC, table 16-7 simple codes, 16-8/16-9 compound codes,
 *     16.6 severity with MCG_SER_P: UCNA, SRAO, SRAR).
 *   AMD APM vol. 2 ch. 9 and the family 17h/19h PPRs (MCA_STATUS, MCA_ADDR,
 *     MCA_IPID, MCA_SYND, the SMCA MSR map at 0xC0002000).
 * Bits 63..57 and the 16-bit error code are shared by both vendors; bits
 * 56..37 differ, so decoding takes the vendor.
 *
 * SEVERITY, worst last (the numeric order is the order of badness):
 *   NONE         status not valid: nothing was logged.
 *   CORRECTED    hardware fixed it (VAL=1, UC=0).
 *   DEFERRED     AMD only: uncorrected but not yet consumed (UC=0, bit 44).
 *   UC_RECOV     uncorrected, context intact and no action needed now:
 *                Intel UCNA/SRAO (needs MCG_SER_P), AMD UC with PCC=TCC=0.
 *   ACTION_REQ   uncorrected and the consumer must be stopped: Intel SRAR
 *                (S=AR=1), AMD TCC=1. This kernel has no task to kill, so
 *                a #MC at this level halts.
 *   FATAL        PCC=1; or UC with OVER=1 (an uncorrected record was lost);
 *                or Intel UC without MCG_SER_P (no recovery architecture).
 *
 * THE LOG is a ring of MCA_RING_CAP records. head - tail >= MCA_RING_CAP is
 * full; a new record then drops the OLDEST and counts it in `dropped`. The
 * newest records are the ones that explain a failure, so they are kept.
 *
 * DEGRADING. Each bank counts its corrected (and deferred) errors. When the
 * count reaches the threshold the bank is marked degrading and stays marked.
 * Default 16 (MCA_DEFAULT_THRESHOLD): one corrected ECC error is a normal
 * event (a particle strike), a handful in one bank is still ambiguous, and
 * sixteen since boot from one bank is the pattern of a stuck cell or a
 * failing DIMM rather than chance. This is a judgment, not a vendor number;
 * mca_set_threshold() changes it and 0 turns marking off. */
#ifndef ZXV_MCA_H
#define ZXV_MCA_H

#include <stdint.h>

/* ---- IA32_MCG_CAP (MSR 0x179) ---- */
#define MCA_MCG_COUNT_MASK 0xFFull
#define MCA_MCG_CTL_P      (1ull << 8)
#define MCA_MCG_EXT_P      (1ull << 9)
#define MCA_MCG_CMCI_P     (1ull << 10)
#define MCA_MCG_TES_P      (1ull << 11)
#define MCA_MCG_EXT_CNT_SH 16
#define MCA_MCG_SER_P      (1ull << 24)
#define MCA_MCG_EMC_P      (1ull << 25)
#define MCA_MCG_ELOG_P     (1ull << 26)
#define MCA_MCG_LMCE_P     (1ull << 27)

/* ---- IA32_MCG_STATUS (MSR 0x17A) ---- */
#define MCA_MCGS_RIPV   (1ull << 0)
#define MCA_MCGS_EIPV   (1ull << 1)
#define MCA_MCGS_MCIP   (1ull << 2)
#define MCA_MCGS_LMCE_S (1ull << 3)

/* ---- MCi_STATUS / MCA_STATUS, shared bits ---- */
#define MCA_ST_VAL   (1ull << 63)
#define MCA_ST_OVER  (1ull << 62)
#define MCA_ST_UC    (1ull << 61)
#define MCA_ST_EN    (1ull << 60)
#define MCA_ST_MISCV (1ull << 59)
#define MCA_ST_ADDRV (1ull << 58)
#define MCA_ST_PCC   (1ull << 57)
/* Intel */
#define MCA_ST_S      (1ull << 56) /* signaled (MCG_SER_P) */
#define MCA_ST_AR     (1ull << 55) /* action required (MCG_SER_P) */
#define MCA_ST_CEC_SH 38           /* corrected count 52:38 (MCG_CMCI_P) */
#define MCA_ST_CEC_MK 0x7FFFull
#define MCA_ST_TES_SH 53 /* threshold status 54:53 (MCG_TES_P, UC=0) */
/* AMD */
#define MCA_ST_COREIDV (1ull << 56)
#define MCA_ST_TCC     (1ull << 55)
#define MCA_ST_SYNDV   (1ull << 53)
#define MCA_ST_CECC    (1ull << 46)
#define MCA_ST_UECC    (1ull << 45)
#define MCA_ST_DEFER   (1ull << 44)
#define MCA_ST_POISON  (1ull << 43)

/* Intel MCi_MISC with MCG_SER_P: 5:0 address LSB, 8:6 address mode. */
#define MCA_MISC_MODE_PHYS 2u

/* AMD SMCA MCA_IPID: 63:48 McaType, 43:32 HardwareID, 31:0 InstanceId. */
#define MCA_IPID_TYPE(i) ((uint32_t) (((i) >> 48) & 0xFFFFu))
#define MCA_IPID_HWID(i) ((uint32_t) (((i) >> 32) & 0xFFFu))
#define MCA_IPID_INST(i) ((uint32_t) ((i) & 0xFFFFFFFFu))

#define MCA_MAX_BANKS         64u
#define MCA_RING_CAP          32u
#define MCA_DEFAULT_THRESHOLD 16u
#define MCA_TEXT_MAX          256u /* mca_format() never needs more */

enum { MCA_VENDOR_OTHER = 0, MCA_VENDOR_INTEL = 1, MCA_VENDOR_AMD = 2 };

enum {
    MCA_SEV_NONE = 0,
    MCA_SEV_CORRECTED = 1,
    MCA_SEV_DEFERRED = 2,
    MCA_SEV_UC_RECOV = 3,
    MCA_SEV_ACTION_REQ = 4,
    MCA_SEV_FATAL = 5,
};

/* Where a record came from. */
enum { MCA_SRC_BOOT = 0, MCA_SRC_POLL = 1, MCA_SRC_MCE = 2 };

/* Error-code class (MCACOD, SDM tables 16-7 to 16-9). */
enum {
    MCA_CC_NONE = 0,       /* 0000: no error */
    MCA_CC_UNCLASSIFIED,   /* 0001 */
    MCA_CC_UCODE_ROM,      /* 0002: microcode ROM parity */
    MCA_CC_EXTERNAL,       /* 0003: external error */
    MCA_CC_FRC,            /* 0004: functional redundancy check */
    MCA_CC_INTERNAL_PAR,   /* 0005: internal parity */
    MCA_CC_SMM_ACCESS,     /* 0006: SMM handler code access violation */
    MCA_CC_INTERNAL_TIMER, /* 0400: internal timer */
    MCA_CC_INTERNAL,       /* 01xx_xxxx_xxxx: internal unclassified */
    MCA_CC_GENERIC_CACHE,  /* 0000_0000_0000_11LL */
    MCA_CC_TLB,            /* 0000_0000_0001_TTLL */
    MCA_CC_MEMORY,         /* 0000_0000_1MMM_CCCC: memory controller */
    MCA_CC_CACHE,          /* 0000_0001_RRRR_TTLL */
    MCA_CC_BUS,            /* 0000_1PPT_RRRR_IILL */
    MCA_CC_OTHER,          /* reserved or model-specific */
};

/* AMD SMCA bank types, from IPID (HardwareID, McaType). */
enum {
    MCA_SMCA_NONE = 0, /* not SMCA, or IPID zero */
    MCA_SMCA_LS,       /* load-store 0xB0/0x0 */
    MCA_SMCA_LS_V2,    /* 0xB0/0x10 */
    MCA_SMCA_IF,       /* instruction fetch 0xB0/0x1 */
    MCA_SMCA_L2,       /* 0xB0/0x2 */
    MCA_SMCA_DE,       /* decode 0xB0/0x3 */
    MCA_SMCA_EX,       /* execution 0xB0/0x5 */
    MCA_SMCA_FP,       /* floating point 0xB0/0x6 */
    MCA_SMCA_L3,       /* 0xB0/0x7 */
    MCA_SMCA_CS,       /* data fabric coherent slave 0x2E/0x0 */
    MCA_SMCA_PIE,      /* data fabric power/interrupt 0x2E/0x1 */
    MCA_SMCA_CS_V2,    /* 0x2E/0x2 */
    MCA_SMCA_UMC,      /* unified memory controller 0x96/0x0 */
    MCA_SMCA_UMC_V2,   /* 0x96/0x1 */
    MCA_SMCA_PB,       /* parameter block 0x05/0x0 */
    MCA_SMCA_PSP,      /* platform security processor 0xFF/0x0 */
    MCA_SMCA_SMU,      /* system management unit 0x01/0x0 */
    MCA_SMCA_NBIO,     /* northbridge IO 0x18/0x0 */
    MCA_SMCA_PCIE,     /* 0x46/0x0 */
    MCA_SMCA_UNKNOWN,  /* IPID nonzero, pair not in the table */
};

/* What kind of address MCi_ADDR holds. */
enum {
    MCA_ADDR_NONE = 0, /* ADDRV clear */
    MCA_ADDR_PHYS,     /* system physical address */
    MCA_ADDR_UMC_NORM, /* AMD UMC normalized address (needs DF translation) */
    MCA_ADDR_OTHER,    /* valid but not physical (linear, segment, unknown) */
};

/* Decoded flags (mca_rec_t.flags). */
#define MCA_F_VALID    (1u << 0)
#define MCA_F_OVER     (1u << 1)
#define MCA_F_UC       (1u << 2)
#define MCA_F_EN       (1u << 3)
#define MCA_F_MISCV    (1u << 4)
#define MCA_F_ADDRV    (1u << 5)
#define MCA_F_PCC      (1u << 6)
#define MCA_F_S        (1u << 7)
#define MCA_F_AR       (1u << 8)
#define MCA_F_TCC      (1u << 9)
#define MCA_F_DEFER    (1u << 10)
#define MCA_F_POISON   (1u << 11)
#define MCA_F_SYNDV    (1u << 12)
#define MCA_F_CEC      (1u << 13) /* cec field holds the hardware count */
#define MCA_F_MEMORY   (1u << 14) /* memory error: compound code or UMC */
#define MCA_F_DRAM_ECC (1u << 15) /* AMD UMC extended code 0: DRAM ECC */

/* Raw registers of one bank, as read. ipid/synd only with SMCA. */
typedef struct {
    uint32_t bank;
    uint32_t smca; /* nonzero: ipid and synd were read */
    uint64_t status, addr, misc, ipid, synd;
} mca_raw_t;

typedef struct {
    uint32_t seq; /* 1-based, in logging order */
    uint16_t bank;
    uint8_t source; /* MCA_SRC_* */
    uint8_t sev;    /* MCA_SEV_* */
    uint32_t flags; /* MCA_F_* */
    uint16_t mcacod;
    uint16_t mscod;   /* 31:16, model specific */
    uint8_t cclass;   /* MCA_CC_* */
    uint8_t mem_txn;  /* MMM for MCA_CC_MEMORY */
    uint8_t mem_chan; /* CCCC for MCA_CC_MEMORY, 15 = unspecified */
    uint8_t xec;      /* AMD extended error code 21:16 */
    uint8_t smca;     /* MCA_SMCA_* */
    uint8_t addr_kind;
    uint8_t addr_lsb; /* lowest valid address bit */
    uint8_t pad;
    uint16_t cec; /* Intel corrected count (MCA_F_CEC) */
    uint16_t pad2;
    uint64_t addr; /* masked to its valid bits; 0 if ADDRV clear */
    uint64_t status, raw_addr, misc, ipid, synd;
} mca_rec_t;

typedef struct {
    uint32_t vendor;
    uint32_t smca;
    uint64_t mcg_cap;
    uint32_t nbanks;    /* min(MCG_CAP count, MCA_MAX_BANKS) */
    uint32_t clamped;   /* banks the CPU has beyond MCA_MAX_BANKS */
    uint32_t threshold; /* corrected count that marks degrading; 0 = off */
    uint32_t seq;
    uint32_t head, tail; /* free-running; head - tail is the fill */
    uint32_t dropped;    /* oldest records overwritten, saturating */
    uint32_t rejected;   /* raw records naming a bank out of range */
    uint32_t by_sev[MCA_SEV_FATAL + 1];
    uint32_t corrected[MCA_MAX_BANKS];   /* corrected + deferred, saturating */
    uint32_t uncorrected[MCA_MAX_BANKS]; /* saturating */
    uint8_t degrading[MCA_MAX_BANKS];
    mca_rec_t ring[MCA_RING_CAP];
} mca_state_t;

/* Set up an empty state. vendor is MCA_VENDOR_*, smca nonzero if the CPU
 * advertises the AMD scalable MCA (CPUID 0x80000007 EBX bit 3). */
void mca_init(mca_state_t *s, uint32_t vendor, uint64_t mcg_cap, uint32_t smca);
void mca_set_threshold(mca_state_t *s, uint32_t threshold);

/* Classify a 16-bit error code. For MCA_CC_MEMORY *txn and *chan receive
 * MMM and CCCC; both may be null. */
uint32_t mca_code_class(uint16_t mcacod, uint8_t *txn, uint8_t *chan);
/* Bank type from an SMCA IPID. */
uint32_t mca_smca_type(uint64_t ipid);
/* Severity of a status word alone. */
uint32_t mca_severity(uint32_t vendor, uint64_t mcg_cap, uint64_t status);

/* Decode without logging. Returns the severity. */
uint32_t mca_decode(const mca_state_t *s, const mca_raw_t *raw, mca_rec_t *out);
/* Decode and log. Returns the severity (MCA_SEV_NONE and nothing logged if
 * VAL is clear), or -1 if raw->bank >= s->nbanks (counted in `rejected`).
 * out, if not null, receives the logged record. */
int mca_log(mca_state_t *s, const mca_raw_t *raw, uint8_t source, mca_rec_t *out);

/* The ring, oldest first: i in [0, mca_ring_count). 0 on success, -1 if i is
 * out of range. */
uint32_t mca_ring_count(const mca_state_t *s);
int mca_ring_get(const mca_state_t *s, uint32_t i, mca_rec_t *out);

uint32_t mca_bank_corrected(const mca_state_t *s, uint32_t bank);
uint32_t mca_bank_uncorrected(const mca_state_t *s, uint32_t bank);
int mca_bank_degrading(const mca_state_t *s, uint32_t bank);
uint32_t mca_degrading_count(const mca_state_t *s);

/* After a #MC: 1 if execution may resume at the saved RIP (RIPV set and
 * the worst record is at most MCA_SEV_UC_RECOV), else 0. */
int mca_mc_may_resume(uint64_t mcg_status, uint32_t worst_sev);

/* Whether boot code should write all ones to MCi_CTL of this bank. Intel
 * family 6 models below 0x1A keep bank 0 as firmware set it (its CTL is
 * model specific there). */
int mca_bank_ctl_writable(uint32_t vendor, uint32_t family, uint32_t model, uint32_t bank);

/* One line of text for a record, NUL-terminated, at most len - 1 bytes.
 * Returns the length written. */
uint32_t mca_format(const mca_rec_t *r, char *buf, uint32_t len);

const char *mca_sev_name(uint32_t sev);
const char *mca_class_name(uint32_t cclass);
const char *mca_smca_name(uint32_t type);
const char *mca_src_name(uint32_t src);

#endif
