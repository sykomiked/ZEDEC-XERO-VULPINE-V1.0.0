/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mca.c — machine-check decode, severity, bounded log (mca.h). */
#include "mca.h"

typedef char mca_ring_pow2[(MCA_RING_CAP & (MCA_RING_CAP - 1u)) == 0 ? 1 : -1];
typedef char mca_banks_fit[MCA_MAX_BANKS <= 0xFFFFu ? 1 : -1];

static uint32_t sat_add(uint32_t a, uint32_t b)
{
    return (a > 0xFFFFFFFFu - b) ? 0xFFFFFFFFu : a + b;
}

static void rec_clear(mca_rec_t *r)
{
    r->seq = 0;
    r->bank = 0;
    r->source = 0;
    r->sev = 0;
    r->flags = 0;
    r->mcacod = 0;
    r->mscod = 0;
    r->cclass = 0;
    r->mem_txn = 0;
    r->mem_chan = 0;
    r->xec = 0;
    r->smca = 0;
    r->addr_kind = 0;
    r->addr_lsb = 0;
    r->pad = 0;
    r->cec = 0;
    r->pad2 = 0;
    r->addr = 0;
    r->status = 0;
    r->raw_addr = 0;
    r->misc = 0;
    r->ipid = 0;
    r->synd = 0;
}

/* Field by field, so no compiler-generated memcpy call is needed. */
static void rec_copy(mca_rec_t *d, const mca_rec_t *s)
{
    d->seq = s->seq;
    d->bank = s->bank;
    d->source = s->source;
    d->sev = s->sev;
    d->flags = s->flags;
    d->mcacod = s->mcacod;
    d->mscod = s->mscod;
    d->cclass = s->cclass;
    d->mem_txn = s->mem_txn;
    d->mem_chan = s->mem_chan;
    d->xec = s->xec;
    d->smca = s->smca;
    d->addr_kind = s->addr_kind;
    d->addr_lsb = s->addr_lsb;
    d->pad = 0;
    d->cec = s->cec;
    d->pad2 = 0;
    d->addr = s->addr;
    d->status = s->status;
    d->raw_addr = s->raw_addr;
    d->misc = s->misc;
    d->ipid = s->ipid;
    d->synd = s->synd;
}

void mca_init(mca_state_t *s, uint32_t vendor, uint64_t mcg_cap, uint32_t smca)
{
    if (!s) return;
    uint32_t count = (uint32_t) (mcg_cap & MCA_MCG_COUNT_MASK);
    s->vendor = vendor;
    s->smca = (vendor == MCA_VENDOR_AMD && smca) ? 1u : 0u;
    s->mcg_cap = mcg_cap;
    s->nbanks = count > MCA_MAX_BANKS ? MCA_MAX_BANKS : count;
    s->clamped = count > MCA_MAX_BANKS ? count - MCA_MAX_BANKS : 0u;
    s->threshold = MCA_DEFAULT_THRESHOLD;
    s->seq = 0;
    s->head = 0;
    s->tail = 0;
    s->dropped = 0;
    s->rejected = 0;
    for (uint32_t i = 0; i <= MCA_SEV_FATAL; i++) s->by_sev[i] = 0;
    for (uint32_t i = 0; i < MCA_MAX_BANKS; i++) {
        s->corrected[i] = 0;
        s->uncorrected[i] = 0;
        s->degrading[i] = 0;
    }
    for (uint32_t i = 0; i < MCA_RING_CAP; i++) rec_clear(&s->ring[i]);
}

void mca_set_threshold(mca_state_t *s, uint32_t threshold)
{
    if (!s) return;
    s->threshold = threshold;
    /* A lower threshold applies to counts already held. */
    for (uint32_t i = 0; i < s->nbanks; i++)
        if (threshold && s->corrected[i] >= threshold) s->degrading[i] = 1;
}

uint32_t mca_code_class(uint16_t c, uint8_t *txn, uint8_t *chan)
{
    if (txn) *txn = 0;
    if (chan) *chan = 0;
    switch (c) {
    case 0x0000:
        return MCA_CC_NONE;
    case 0x0001:
        return MCA_CC_UNCLASSIFIED;
    case 0x0002:
        return MCA_CC_UCODE_ROM;
    case 0x0003:
        return MCA_CC_EXTERNAL;
    case 0x0004:
        return MCA_CC_FRC;
    case 0x0005:
        return MCA_CC_INTERNAL_PAR;
    case 0x0006:
        return MCA_CC_SMM_ACCESS;
    case 0x0400:
        return MCA_CC_INTERNAL_TIMER;
    default:
        break;
    }
    if ((c & 0xFC00u) == 0x0400u) return MCA_CC_INTERNAL;
    uint16_t f = (uint16_t) (c & 0xEFFFu); /* bit 12 is the filter flag */
    if ((f & 0xE800u) == 0x0800u) return MCA_CC_BUS;
    if ((f & 0xEF00u) == 0x0100u) return MCA_CC_CACHE;
    if ((f & 0xEF80u) == 0x0080u) {
        if (txn) *txn = (uint8_t) ((f >> 4) & 7u);
        if (chan) *chan = (uint8_t) (f & 0xFu);
        return MCA_CC_MEMORY;
    }
    if ((f & 0xEFF0u) == 0x0010u) return MCA_CC_TLB;
    if ((f & 0xEFFCu) == 0x000Cu) return MCA_CC_GENERIC_CACHE;
    return MCA_CC_OTHER;
}

static const struct {
    uint16_t hwid, type;
    uint8_t id;
} g_smca[] = {
    {0xB0, 0x0, MCA_SMCA_LS},     {0xB0, 0x10, MCA_SMCA_LS_V2}, {0xB0, 0x1, MCA_SMCA_IF},
    {0xB0, 0x2, MCA_SMCA_L2},     {0xB0, 0x3, MCA_SMCA_DE},     {0xB0, 0x5, MCA_SMCA_EX},
    {0xB0, 0x6, MCA_SMCA_FP},     {0xB0, 0x7, MCA_SMCA_L3},     {0x2E, 0x0, MCA_SMCA_CS},
    {0x2E, 0x1, MCA_SMCA_PIE},    {0x2E, 0x2, MCA_SMCA_CS_V2},  {0x96, 0x0, MCA_SMCA_UMC},
    {0x96, 0x1, MCA_SMCA_UMC_V2}, {0x05, 0x0, MCA_SMCA_PB},     {0xFF, 0x0, MCA_SMCA_PSP},
    {0x01, 0x0, MCA_SMCA_SMU},    {0x18, 0x0, MCA_SMCA_NBIO},   {0x46, 0x0, MCA_SMCA_PCIE},
};

uint32_t mca_smca_type(uint64_t ipid)
{
    if (ipid == 0) return MCA_SMCA_NONE;
    uint32_t hw = MCA_IPID_HWID(ipid), ty = MCA_IPID_TYPE(ipid);
    for (uint32_t i = 0; i < sizeof g_smca / sizeof g_smca[0]; i++)
        if (g_smca[i].hwid == hw && g_smca[i].type == ty) return g_smca[i].id;
    return MCA_SMCA_UNKNOWN;
}

uint32_t mca_severity(uint32_t vendor, uint64_t mcg_cap, uint64_t st)
{
    if (!(st & MCA_ST_VAL)) return MCA_SEV_NONE;
    int uc = (st & MCA_ST_UC) != 0;
    if (uc && (st & MCA_ST_PCC)) return MCA_SEV_FATAL;
    if (uc && (st & MCA_ST_OVER)) return MCA_SEV_FATAL;
    if (vendor == MCA_VENDOR_AMD) {
        if (uc) return (st & MCA_ST_TCC) ? MCA_SEV_ACTION_REQ : MCA_SEV_UC_RECOV;
        return (st & MCA_ST_DEFER) ? MCA_SEV_DEFERRED : MCA_SEV_CORRECTED;
    }
    if (!uc) return MCA_SEV_CORRECTED;
    if (!(mcg_cap & MCA_MCG_SER_P)) return MCA_SEV_FATAL;
    /* AR without S is not a defined encoding: take the stricter reading. */
    if (st & MCA_ST_AR) return MCA_SEV_ACTION_REQ;
    return MCA_SEV_UC_RECOV;
}

static uint32_t smca_addr_kind(uint32_t type)
{
    switch (type) {
    case MCA_SMCA_UMC:
    case MCA_SMCA_UMC_V2:
        return MCA_ADDR_UMC_NORM;
    case MCA_SMCA_LS:
    case MCA_SMCA_LS_V2:
    case MCA_SMCA_IF:
    case MCA_SMCA_L2:
    case MCA_SMCA_DE:
    case MCA_SMCA_EX:
    case MCA_SMCA_FP:
    case MCA_SMCA_L3:
    case MCA_SMCA_CS:
    case MCA_SMCA_PIE:
    case MCA_SMCA_CS_V2:
        return MCA_ADDR_PHYS;
    default:
        return MCA_ADDR_OTHER;
    }
}

uint32_t mca_decode(const mca_state_t *s, const mca_raw_t *raw, mca_rec_t *out)
{
    if (!s || !raw || !out) return MCA_SEV_NONE;
    rec_clear(out);
    uint64_t st = raw->status;
    uint32_t f = 0;
    if (st & MCA_ST_VAL) f |= MCA_F_VALID;
    if (st & MCA_ST_OVER) f |= MCA_F_OVER;
    if (st & MCA_ST_UC) f |= MCA_F_UC;
    if (st & MCA_ST_EN) f |= MCA_F_EN;
    if (st & MCA_ST_MISCV) f |= MCA_F_MISCV;
    if (st & MCA_ST_ADDRV) f |= MCA_F_ADDRV;
    if (st & MCA_ST_PCC) f |= MCA_F_PCC;

    int amd = s->vendor == MCA_VENDOR_AMD;
    int ser = (s->mcg_cap & MCA_MCG_SER_P) != 0;
    if (amd) {
        if (st & MCA_ST_TCC) f |= MCA_F_TCC;
        if (st & MCA_ST_DEFER) f |= MCA_F_DEFER;
        if (st & MCA_ST_POISON) f |= MCA_F_POISON;
        if (st & MCA_ST_SYNDV) f |= MCA_F_SYNDV;
        out->xec = (uint8_t) ((st >> 16) & 0x3Fu);
    } else {
        if (ser && (st & MCA_ST_S)) f |= MCA_F_S;
        if (ser && (st & MCA_ST_AR)) f |= MCA_F_AR;
        if (s->mcg_cap & MCA_MCG_CMCI_P) {
            f |= MCA_F_CEC;
            out->cec = (uint16_t) ((st >> MCA_ST_CEC_SH) & MCA_ST_CEC_MK);
        }
    }

    out->mcacod = (uint16_t) (st & 0xFFFFu);
    out->mscod = (uint16_t) ((st >> 16) & 0xFFFFu);
    out->cclass = (uint8_t) mca_code_class(out->mcacod, &out->mem_txn, &out->mem_chan);
    if (out->cclass == MCA_CC_MEMORY) f |= MCA_F_MEMORY;

    uint32_t type = MCA_SMCA_NONE;
    if (amd && s->smca && raw->smca) {
        type = mca_smca_type(raw->ipid);
        if (type == MCA_SMCA_UMC || type == MCA_SMCA_UMC_V2) {
            f |= MCA_F_MEMORY;
            if (out->xec == 0) f |= MCA_F_DRAM_ECC;
        }
        out->ipid = raw->ipid;
        out->synd = raw->synd;
    }
    out->smca = (uint8_t) type;

    if (st & MCA_ST_ADDRV) {
        uint64_t a = raw->addr;
        uint32_t lsb = 0, kind = MCA_ADDR_PHYS;
        if (amd && s->smca && raw->smca) {
            lsb = (uint32_t) ((a >> 56) & 0x3Fu);
            a &= 0x00FFFFFFFFFFFFFFull;
            kind = smca_addr_kind(type);
        } else if (!amd && ser && (st & MCA_ST_MISCV)) {
            lsb = (uint32_t) (raw->misc & 0x3Fu);
            if (((raw->misc >> 6) & 7u) != MCA_MISC_MODE_PHYS) kind = MCA_ADDR_OTHER;
        }
        a &= ~((1ull << lsb) - 1ull); /* lsb <= 63 */
        out->addr = a;
        out->addr_lsb = (uint8_t) lsb;
        out->addr_kind = (uint8_t) kind;
    }

    out->flags = f;
    out->bank = (uint16_t) (raw->bank > 0xFFFFu ? 0xFFFFu : raw->bank);
    out->status = st;
    out->raw_addr = raw->addr;
    out->misc = raw->misc;
    out->sev = (uint8_t) mca_severity(s->vendor, s->mcg_cap, st);
    return out->sev;
}

int mca_log(mca_state_t *s, const mca_raw_t *raw, uint8_t source, mca_rec_t *out)
{
    if (!s || !raw) return -1;
    /* Check. */
    if (raw->bank >= s->nbanks) {
        s->rejected = sat_add(s->rejected, 1);
        return -1;
    }
    /* Compute. */
    mca_rec_t r;
    uint32_t sev = mca_decode(s, raw, &r);
    if (out) rec_copy(out, &r);
    if (sev == MCA_SEV_NONE) return MCA_SEV_NONE;
    /* Reserve: a full ring gives up its oldest record. */
    if (s->head - s->tail >= MCA_RING_CAP) {
        s->tail++;
        s->dropped = sat_add(s->dropped, 1);
    }
    /* Mutate. */
    s->seq++;
    r.seq = s->seq;
    r.source = source;
    s->by_sev[sev] = sat_add(s->by_sev[sev], 1);
    if (sev == MCA_SEV_CORRECTED || sev == MCA_SEV_DEFERRED) {
        uint32_t inc = ((r.flags & MCA_F_CEC) && r.cec) ? r.cec : 1u;
        s->corrected[raw->bank] = sat_add(s->corrected[raw->bank], inc);
        if (s->threshold && s->corrected[raw->bank] >= s->threshold) s->degrading[raw->bank] = 1;
    } else {
        s->uncorrected[raw->bank] = sat_add(s->uncorrected[raw->bank], 1);
    }
    /* Commit. */
    rec_copy(&s->ring[s->head % MCA_RING_CAP], &r);
    s->head++;
    if (out) rec_copy(out, &r);
    return (int) sev;
}

uint32_t mca_ring_count(const mca_state_t *s)
{
    return s ? s->head - s->tail : 0u;
}

int mca_ring_get(const mca_state_t *s, uint32_t i, mca_rec_t *out)
{
    if (!s || !out || i >= mca_ring_count(s)) return -1;
    rec_copy(out, &s->ring[(s->tail + i) % MCA_RING_CAP]);
    return 0;
}

uint32_t mca_bank_corrected(const mca_state_t *s, uint32_t bank)
{
    return (s && bank < s->nbanks) ? s->corrected[bank] : 0u;
}

uint32_t mca_bank_uncorrected(const mca_state_t *s, uint32_t bank)
{
    return (s && bank < s->nbanks) ? s->uncorrected[bank] : 0u;
}

int mca_bank_degrading(const mca_state_t *s, uint32_t bank)
{
    return (s && bank < s->nbanks) ? s->degrading[bank] : 0;
}

uint32_t mca_degrading_count(const mca_state_t *s)
{
    uint32_t n = 0;
    if (!s) return 0;
    for (uint32_t i = 0; i < s->nbanks; i++) n += s->degrading[i] ? 1u : 0u;
    return n;
}

int mca_mc_may_resume(uint64_t mcg_status, uint32_t worst_sev)
{
    return (mcg_status & MCA_MCGS_RIPV) && worst_sev <= MCA_SEV_UC_RECOV;
}

int mca_bank_ctl_writable(uint32_t vendor, uint32_t family, uint32_t model, uint32_t bank)
{
    if (bank >= MCA_MAX_BANKS) return 0;
    if (vendor == MCA_VENDOR_INTEL && family == 6 && model < 0x1A && bank == 0) return 0;
    return 1;
}

const char *mca_sev_name(uint32_t sev)
{
    switch (sev) {
    case MCA_SEV_NONE:
        return "none";
    case MCA_SEV_CORRECTED:
        return "corrected";
    case MCA_SEV_DEFERRED:
        return "deferred";
    case MCA_SEV_UC_RECOV:
        return "uncorrected-recoverable";
    case MCA_SEV_ACTION_REQ:
        return "uncorrected-action-required";
    case MCA_SEV_FATAL:
        return "fatal";
    default:
        return "?";
    }
}

const char *mca_class_name(uint32_t cc)
{
    switch (cc) {
    case MCA_CC_NONE:
        return "no-error";
    case MCA_CC_UNCLASSIFIED:
        return "unclassified";
    case MCA_CC_UCODE_ROM:
        return "microcode-rom-parity";
    case MCA_CC_EXTERNAL:
        return "external";
    case MCA_CC_FRC:
        return "frc";
    case MCA_CC_INTERNAL_PAR:
        return "internal-parity";
    case MCA_CC_SMM_ACCESS:
        return "smm-access";
    case MCA_CC_INTERNAL_TIMER:
        return "internal-timer";
    case MCA_CC_INTERNAL:
        return "internal";
    case MCA_CC_GENERIC_CACHE:
        return "generic-cache";
    case MCA_CC_TLB:
        return "tlb";
    case MCA_CC_MEMORY:
        return "memory";
    case MCA_CC_CACHE:
        return "cache";
    case MCA_CC_BUS:
        return "bus";
    default:
        return "other";
    }
}

const char *mca_smca_name(uint32_t t)
{
    switch (t) {
    case MCA_SMCA_NONE:
        return "none";
    case MCA_SMCA_LS:
        return "ls";
    case MCA_SMCA_LS_V2:
        return "ls-v2";
    case MCA_SMCA_IF:
        return "if";
    case MCA_SMCA_L2:
        return "l2";
    case MCA_SMCA_DE:
        return "de";
    case MCA_SMCA_EX:
        return "ex";
    case MCA_SMCA_FP:
        return "fp";
    case MCA_SMCA_L3:
        return "l3";
    case MCA_SMCA_CS:
        return "cs";
    case MCA_SMCA_PIE:
        return "pie";
    case MCA_SMCA_CS_V2:
        return "cs-v2";
    case MCA_SMCA_UMC:
        return "umc";
    case MCA_SMCA_UMC_V2:
        return "umc-v2";
    case MCA_SMCA_PB:
        return "pb";
    case MCA_SMCA_PSP:
        return "psp";
    case MCA_SMCA_SMU:
        return "smu";
    case MCA_SMCA_NBIO:
        return "nbio";
    case MCA_SMCA_PCIE:
        return "pcie";
    default:
        return "unknown";
    }
}

const char *mca_src_name(uint32_t src)
{
    switch (src) {
    case MCA_SRC_BOOT:
        return "boot";
    case MCA_SRC_POLL:
        return "poll";
    case MCA_SRC_MCE:
        return "mce";
    default:
        return "?";
    }
}

static const char *mem_txn_name(uint32_t t)
{
    static const char *const n[8] = {"generic", "read",  "write", "addr-cmd",
                                     "scrub",   "rsvd5", "rsvd6", "rsvd7"};
    return n[t & 7u];
}

/* Bounded text builder: never writes past len - 1, always terminates. */
typedef struct {
    char *b;
    uint32_t len, n;
} fmt_t;

static void put_s(fmt_t *f, const char *s)
{
    while (*s && f->n + 1 < f->len) f->b[f->n++] = *s++;
    f->b[f->n] = 0;
}

static void put_u(fmt_t *f, uint32_t v)
{
    char t[10];
    uint32_t i = 0;
    do {
        t[i++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v && i < sizeof t);
    while (i && f->n + 1 < f->len) f->b[f->n++] = t[--i];
    f->b[f->n] = 0;
}

static void put_x(fmt_t *f, uint64_t v)
{
    static const char d[] = "0123456789abcdef";
    put_s(f, "0x");
    for (int i = 15; i >= 0 && f->n + 1 < f->len; i--) f->b[f->n++] = d[(v >> (i * 4)) & 0xFu];
    f->b[f->n] = 0;
}

uint32_t mca_format(const mca_rec_t *r, char *buf, uint32_t len)
{
    if (!buf || len == 0) return 0;
    fmt_t f = {buf, len, 0};
    buf[0] = 0;
    if (!r) return 0;
    put_s(&f, "bank ");
    put_u(&f, r->bank);
    put_s(&f, " ");
    put_s(&f, mca_sev_name(r->sev));
    put_s(&f, " ");
    put_s(&f, mca_class_name(r->cclass));
    if (r->cclass == MCA_CC_MEMORY) {
        put_s(&f, " ");
        put_s(&f, mem_txn_name(r->mem_txn));
        put_s(&f, " ch");
        put_u(&f, r->mem_chan);
    }
    if (r->smca != MCA_SMCA_NONE) {
        put_s(&f, " smca=");
        put_s(&f, mca_smca_name(r->smca));
    }
    if (r->flags & MCA_F_DRAM_ECC) put_s(&f, " dram-ecc");
    if (r->flags & MCA_F_OVER) put_s(&f, " OVER");
    if (r->flags & MCA_F_PCC) put_s(&f, " PCC");
    if (r->flags & MCA_F_AR) put_s(&f, " AR");
    if (r->flags & MCA_F_TCC) put_s(&f, " TCC");
    if (r->flags & MCA_F_POISON) put_s(&f, " POISON");
    put_s(&f, " status=");
    put_x(&f, r->status);
    if (r->addr_kind != MCA_ADDR_NONE) {
        put_s(&f, r->addr_kind == MCA_ADDR_PHYS       ? " pa="
                  : r->addr_kind == MCA_ADDR_UMC_NORM ? " umc-addr="
                                                      : " addr=");
        put_x(&f, r->addr);
    }
    if (r->flags & MCA_F_MISCV) {
        put_s(&f, " misc=");
        put_x(&f, r->misc);
    }
    if ((r->flags & MCA_F_CEC) && r->cec) {
        put_s(&f, " count=");
        put_u(&f, r->cec);
    }
    put_s(&f, " src=");
    put_s(&f, mca_src_name(r->source));
    return f.n;
}
