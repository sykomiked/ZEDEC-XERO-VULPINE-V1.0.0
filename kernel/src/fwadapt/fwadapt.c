/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fwadapt.c — the smart firmware adapter. See fwadapt.h (A1-A6). */
#include "fwadapt.h"

/* virtio IDs use the modern virtio-pci numbering (0x1040 + device type) on
 * every transport; this tree's virtio drivers are virtio-mmio (platform). */
#define VIRTIO(dev) ((0x1AF4u << 16) | (0x1040u + (dev)))

static const fwa_driver_t BUILTIN[] = {
    /* exact IDs: drivers in this tree */
    {"virtio-net", FWA_BUS_PLATFORM, false, VIRTIO(1), 0xFFFFFFFFu, 0, 0, HWP_CLASS_NETWORK, false, true},
    {"virtio-blk", FWA_BUS_PLATFORM, false, VIRTIO(2), 0xFFFFFFFFu, 0, 0, HWP_CLASS_STORAGE, false, true},
    {"virtio-gpu", FWA_BUS_PLATFORM, false, VIRTIO(16), 0xFFFFFFFFu, 0, 0, HWP_CLASS_GPU, false, true},
    {"virtio-input", FWA_BUS_PLATFORM, false, VIRTIO(18), 0xFFFFFFFFu, 0, 0, HWP_CLASS_IO, false, true},
    {"virtio-snd", FWA_BUS_PLATFORM, false, VIRTIO(25), 0xFFFFFFFFu, 0, 0, HWP_CLASS_AUDIO, false, true},
    {"rtl8139", FWA_BUS_PCI, false, 0x10EC8139u, 0xFFFFFFFFu, 0, 0, HWP_CLASS_NETWORK, false, true},
    /* exact IDs: known, transport not in this tree */
    {"virtio-pci", FWA_BUS_PCI, false, 0x1AF40000u, 0xFFFF0000u, 0, 0, HWP_CLASS_IO, false, false},
    /* standard class interfaces (PCI class<<16 | subclass<<8 | prog-if) */
    {"ide-legacy", FWA_BUS_PCI, true, 0, 0, 0x010100u, 0xFFFF00u, HWP_CLASS_STORAGE, true, true},
    {"ahci", FWA_BUS_PCI, true, 0, 0, 0x010601u, 0xFFFFFFu, HWP_CLASS_STORAGE, true, false},
    {"nvme", FWA_BUS_PCI, true, 0, 0, 0x010802u, 0xFFFFFFu, HWP_CLASS_STORAGE, true, false},
    {"vga-framebuffer", FWA_BUS_PCI, true, 0, 0, 0x030000u, 0xFFFFFFu, HWP_CLASS_DISPLAY, true, true},
    {"hda", FWA_BUS_PCI, true, 0, 0, 0x040300u, 0xFFFF00u, HWP_CLASS_AUDIO, true, false},
    {"xhci", FWA_BUS_PCI, true, 0, 0, 0x0C0330u, 0xFFFFFFu, HWP_CLASS_IO, true, false},
    /* USB interface classes (class<<16 | subclass<<8 | protocol) */
    {"usb-hid-boot-keyboard", FWA_BUS_USB, true, 0, 0, 0x030101u, 0xFFFFFFu, HWP_CLASS_IO, true, false},
    {"usb-hid-boot-mouse", FWA_BUS_USB, true, 0, 0, 0x030102u, 0xFFFFFFu, HWP_CLASS_IO, true, false},
    {"usb-mass-storage", FWA_BUS_USB, true, 0, 0, 0x080650u, 0xFFFFFFu, HWP_CLASS_STORAGE, true, false},
    {"usb-cdc-ecm", FWA_BUS_USB, true, 0, 0, 0x020600u, 0xFFFF00u, HWP_CLASS_NETWORK, true, false},
};

const fwa_driver_t *fwa_builtin_table(uint16_t *out_n)
{
    if (out_n) *out_n = (uint16_t) (sizeof BUILTIN / sizeof BUILTIN[0]);
    return BUILTIN;
}

static void zero(void *p, uint32_t n)
{
    uint8_t *b = (uint8_t *) p;
    while (n--) *b++ = 0;
}

void fwa_init(fwa_t *a, const fwa_driver_t *table, uint16_t ntable, uint64_t fresh_limit)
{
    if (!a) return;
    zero(a, (uint32_t) sizeof *a);
    a->table = table;
    a->ntable = table ? ntable : 0;
    a->fresh_limit = fresh_limit;
}

int fwa_claim(fwa_t *a, uint8_t bus, uint32_t loc, const fwa_claim_t *c)
{
    if (!a || !c || c->src >= FWA_SRC_COUNT || c->kind > FWA_CLAIM_FAULT || bus < FWA_BUS_PCI ||
        bus > FWA_BUS_PLATFORM)
        return FWA_E_ARG;
    fwa_device_t *d = 0;
    for (uint16_t i = 0; i < a->n; i++)
        if (a->dev[i].bus == bus && a->dev[i].loc == loc) d = &a->dev[i];
    if (!d) {
        if (a->n >= FWA_MAX_DEVICES) return FWA_E_FULL;
        d = &a->dev[a->n++];
        zero(d, (uint32_t) sizeof *d);
        d->bus = bus;
        d->loc = loc;
    }
    if (d->nclaim >= FWA_MAX_CLAIMS) return FWA_E_FULL;
    d->claim[d->nclaim++] = *c;
    return FWA_OK;
}

/* A2: join "value X" over the sources that state a value. *agree counts
 * sources that state the agreed value. */
static lpres_state_t join_value(const fwa_device_t *d, bool want_cls, uint32_t *out, uint8_t *agree)
{
    uint32_t x = FWA_ID_NONE;
    unsigned told = 0; /* bit0 told true, bit1 told false: the lpres encoding */
    uint8_t srcmask = 0;
    for (uint8_t i = 0; i < d->nclaim; i++) {
        const fwa_claim_t *c = &d->claim[i];
        uint32_t v = want_cls ? c->cls : c->id;
        if (c->kind != FWA_CLAIM_NAMES || v == FWA_ID_NONE) continue;
        if (x == FWA_ID_NONE) x = v;
        if (v == x) {
            told |= 1u;
            srcmask |= (uint8_t) (1u << c->src);
        } else {
            told |= 2u;
        }
    }
    uint8_t n = 0;
    for (; srcmask; srcmask &= (uint8_t) (srcmask - 1u)) n++;
    *agree = n;
    *out = told == 1u ? x : FWA_ID_NONE;
    return (lpres_state_t) told; /* 0 NEITHER, 1 TRUE, 3 BOTH (2 cannot occur) */
}

static const fwa_driver_t *find(const fwa_t *a, uint8_t bus, bool by_class, uint32_t v)
{
    for (uint16_t i = 0; i < a->ntable; i++) {
        const fwa_driver_t *t = &a->table[i];
        if (t->bus != bus || t->by_class != by_class) continue;
        if (!by_class && (v & t->id_mask) == (t->id & t->id_mask)) return t;
        if (by_class && t->standard && (v & t->cls_mask) == (t->cls & t->cls_mask)) return t;
    }
    return 0;
}

static void decide_one(const fwa_t *a, fwa_device_t *d)
{
    bool fault = false, stale = false;
    unsigned pres = 0;
    for (uint8_t i = 0; i < d->nclaim; i++) {
        const fwa_claim_t *c = &d->claim[i];
        if (c->kind == FWA_CLAIM_FAULT) fault = true;
        else pres |= c->kind == FWA_CLAIM_NAMES ? 1u : 2u;
        if (a->fresh_limit && c->age > a->fresh_limit) stale = true;
    }
    uint8_t agree_id, agree_cls;
    d->presence = (lpres_state_t) pres;
    d->identity = join_value(d, false, &d->id, &agree_id);
    d->klass = join_value(d, true, &d->cls, &agree_cls);
    d->driver = 0;
    d->flagged = false;

    /* the verdict base */
    uint8_t base;
    if (d->presence == LPRES_STATE_BOTH || d->identity == LPRES_STATE_BOTH || d->klass == LPRES_STATE_BOTH)
        base = LPX_B_BOTH;
    else if (d->presence == LPRES_STATE_FALSE)
        base = LPX_B_FALSE;
    else if (d->identity == LPRES_STATE_TRUE || d->klass == LPRES_STATE_TRUE)
        base = LPX_B_TRUE;
    else
        base = LPX_B_NEITHER;
    /* A3 */
    uint8_t agree = agree_id > agree_cls ? agree_id : agree_cls;
    uint8_t pol = stale ? LPX_P_ENTROPIC : agree >= 2 ? LPX_P_NEGENTROPIC : LPX_P_NEUTRAL;
    if (fault) pol = LPX_P_FORBIDDEN;
    d->tag = lpx_make(base, pol, LPX_PH_MANIFEST);

    /* A4 */
    if (fault) {
        d->decision = FWA_DEC_QUARANTINE;
        return;
    }
    if (base == LPX_B_FALSE) {
        d->decision = FWA_DEC_NONE; /* nothing there */
        return;
    }
    if (base == LPX_B_BOTH) {
        d->flagged = true;
        const fwa_driver_t *g = d->klass == LPRES_STATE_TRUE ? find(a, d->bus, true, d->cls) : 0;
        d->driver = g;
        d->decision = !g ? FWA_DEC_DISABLED : g->built ? FWA_DEC_SAFE_GENERIC : FWA_DEC_UNAVAILABLE;
        return;
    }
    if (lpx_action(d->tag) == LPX_ACT_WAIT && base == LPX_B_TRUE) {
        d->decision = FWA_DEC_DEFER; /* stale evidence: probe again first */
        return;
    }
    if (d->identity == LPRES_STATE_TRUE) {
        const fwa_driver_t *s = find(a, d->bus, false, d->id);
        if (s) {
            d->driver = s;
            d->decision = s->built ? FWA_DEC_SPECIFIC : FWA_DEC_UNAVAILABLE;
            if (s->built) return;
        }
    }
    if (d->klass == LPRES_STATE_TRUE) {
        const fwa_driver_t *g = find(a, d->bus, true, d->cls);
        if (g && (g->built || !d->driver)) {
            d->driver = g;
            d->decision = g->built ? FWA_DEC_CLASS_GENERIC : FWA_DEC_UNAVAILABLE;
            return;
        }
    }
    if (!d->driver) d->decision = FWA_DEC_NONE; /* A5: never guess */
}

void fwa_decide(fwa_t *a)
{
    if (!a) return;
    for (uint16_t i = 0; i < a->n; i++) decide_one(a, &a->dev[i]);
}

uint16_t fwa_count(const fwa_t *a, fwa_decision_t dec)
{
    uint16_t n = 0;
    for (uint16_t i = 0; a && i < a->n; i++) n += a->dev[i].decision == (uint8_t) dec;
    return n;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

int fwa_to_hwp(const fwa_t *a, hwp_profile_t *p, uint16_t parent, uint16_t first_id)
{
    if (!a || !p) return FWA_E_ARG;
    for (uint16_t i = 0; i < a->n; i++) {
        const fwa_device_t *d = &a->dev[i];
        if (d->decision == FWA_DEC_NONE && d->presence == LPRES_STATE_FALSE) continue;
        uint32_t cls = d->driver ? d->driver->hwp_class : HWP_CLASS_IO;
        uint16_t idx;
        int r = hwp_node_add(p, (uint16_t) (first_id + i), parent, cls, &idx);
        if (r != HWP_OK) return r;
        /* ext: "FWA1" | bus | tag | decision | flagged | loc | id | cls */
        uint8_t ext[20] = {'F', 'W', 'A', '1', d->bus, d->tag, d->decision, d->flagged};
        put32(ext + 8, d->loc);
        put32(ext + 12, d->id);
        put32(ext + 16, d->cls);
        r = hwp_ext_set(p, idx, ext, (uint16_t) sizeof ext);
        if (r != HWP_OK) return r;
        r = hwp_cap_set(p, idx, HWP_CAP_COUNT, 1);
        if (r != HWP_OK) return r;
    }
    return FWA_OK;
}

const char *fwa_decision_name(fwa_decision_t d)
{
    static const char *const N[] = {"none",     "specific",   "class-generic", "safe-generic",
                                    "disabled", "quarantine", "unavailable",   "defer"};
    return (unsigned) d < sizeof N / sizeof N[0] ? N[d] : "?";
}

const char *fwa_src_name(fwa_src_t s)
{
    static const char *const N[] = {"acpi", "devicetree", "pci", "usb", "probe"};
    return (unsigned) s < FWA_SRC_COUNT ? N[s] : "?";
}
