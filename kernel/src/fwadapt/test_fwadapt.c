/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_fwadapt.c — the smart firmware adapter (fwadapt.h A1-A6). */
#include <stdio.h>
#include <string.h>
#include "fwadapt.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__);                                \
        }                                                                                          \
    } while (0)

static fwa_t A;

static void names(uint8_t bus, uint32_t loc, uint8_t src, uint32_t id, uint32_t cls, uint64_t age)
{
    fwa_claim_t c = {src, FWA_CLAIM_NAMES, id, cls, age};
    CHECK(fwa_claim(&A, bus, loc, &c) == FWA_OK, "claim");
}

static void kind(uint8_t bus, uint32_t loc, uint8_t src, uint8_t k)
{
    fwa_claim_t c = {src, k, FWA_ID_NONE, FWA_ID_NONE, 0};
    CHECK(fwa_claim(&A, bus, loc, &c) == FWA_OK, "claim kind");
}

static const fwa_device_t *dev(uint32_t loc)
{
    for (uint16_t i = 0; i < A.n; i++)
        if (A.dev[i].loc == loc) return &A.dev[i];
    return NULL;
}

#define VIO(d) ((0x1AF4u << 16) | (0x1040u + (d)))

int main(void)
{
    uint16_t nt;
    const fwa_driver_t *tab = fwa_builtin_table(&nt);
    CHECK(tab && nt >= 10, "builtin table");
    for (uint16_t i = 0; i < nt; i++)
        CHECK(!tab[i].by_class || tab[i].standard, "every class entry is a standard interface");

    fwa_init(&A, tab, nt, 100);

    /* 1: two sources agree on a virtio-blk -> specific, corroborated */
    names(FWA_BUS_PLATFORM, 1, FWA_SRC_DT, VIO(2), FWA_ID_NONE, 0);
    names(FWA_BUS_PLATFORM, 1, FWA_SRC_PROBE, VIO(2), FWA_ID_NONE, 0);
    /* 2: PCI NVMe by class only, vendor unknown to the table -> unavailable (nvme not built) */
    names(FWA_BUS_PCI, 2, FWA_SRC_PCI, 0x144DA808u, 0x010802u, 0);
    /* 3: ACPI and PCI disagree on the ID -> both -> safe generic (VGA class agreed) + flag */
    names(FWA_BUS_PCI, 3, FWA_SRC_ACPI, 0x80861234u, 0x030000u, 0);
    names(FWA_BUS_PCI, 3, FWA_SRC_PCI, 0x10DE5678u, 0x030000u, 0);
    /* 4: nobody names it, a probe sees something -> neither -> none (never guess) */
    names(FWA_BUS_PLATFORM, 4, FWA_SRC_PROBE, FWA_ID_NONE, FWA_ID_NONE, 0);
    /* 5: a probe faulted -> forbidden -> quarantine, even though PCI named it */
    names(FWA_BUS_PCI, 5, FWA_SRC_PCI, 0x10EC8139u, 0x020000u, 0);
    kind(FWA_BUS_PCI, 5, FWA_SRC_PROBE, FWA_CLAIM_FAULT);
    /* 6: one source says present, another empty -> both, class unknown -> disabled */
    names(FWA_BUS_PCI, 6, FWA_SRC_ACPI, 0x11112222u, FWA_ID_NONE, 0);
    kind(FWA_BUS_PCI, 6, FWA_SRC_PCI, FWA_CLAIM_EMPTY);
    /* 7: everyone says empty -> false -> none */
    kind(FWA_BUS_PCI, 7, FWA_SRC_PCI, FWA_CLAIM_EMPTY);
    kind(FWA_BUS_PCI, 7, FWA_SRC_ACPI, FWA_CLAIM_EMPTY);
    /* 8: rtl8139, single source, stale -> defer */
    names(FWA_BUS_PCI, 8, FWA_SRC_PCI, 0x10EC8139u, 0x020000u, 500);
    /* 9: rtl8139 fresh -> specific, neutral (one source) */
    names(FWA_BUS_PCI, 9, FWA_SRC_PCI, 0x10EC8139u, 0x020000u, 0);
    /* 10: unknown vendor IDE controller: class generic, built */
    names(FWA_BUS_PCI, 10, FWA_SRC_PCI, 0xABCD0001u, 0x01018Au, 0);
    /* 11: class conflict, identity agrees -> both, class not true -> disabled */
    names(FWA_BUS_PCI, 11, FWA_SRC_PCI, 0xABCD0002u, 0x010601u, 0);
    names(FWA_BUS_PCI, 11, FWA_SRC_ACPI, 0xABCD0002u, 0x010802u, 0);
    /* 12: USB boot keyboard by interface class: known standard, no USB stack yet */
    names(FWA_BUS_USB, 12, FWA_SRC_USB, 0x046DC31Cu, 0x030101u, 0);
    /* 13: an unknown device with a non-standard class -> none */
    names(FWA_BUS_PCI, 13, FWA_SRC_PCI, 0xDEAD0001u, 0xFF0000u, 0);
    /* 14: a virtio-pci device: the ID is known but the transport is not built */
    names(FWA_BUS_PCI, 14, FWA_SRC_PCI, 0x1AF41041u, 0x020000u, 0);

    fwa_decide(&A);
    const fwa_device_t *d;

    d = dev(1);
    CHECK(d->decision == FWA_DEC_SPECIFIC && !strcmp(d->driver->name, "virtio-blk"),
          "agreeing IDs bind");
    CHECK(lpx_base(d->tag) == LPX_B_TRUE && lpx_pol(d->tag) == LPX_P_NEGENTROPIC, "corroborated");
    CHECK(lpx_action(d->tag) == LPX_ACT_PROCEED, "proceed");

    d = dev(2);
    CHECK(d->decision == FWA_DEC_UNAVAILABLE && !strcmp(d->driver->name, "nvme"),
          "nvme known, not built");

    d = dev(3);
    CHECK(d->identity == LPRES_STATE_BOTH && d->klass == LPRES_STATE_TRUE,
          "id conflict, class agrees");
    CHECK(d->decision == FWA_DEC_SAFE_GENERIC && d->flagged &&
              !strcmp(d->driver->name, "vga-framebuffer"),
          "safe generic + flagged");
    CHECK(lpx_base(d->tag) == LPX_B_BOTH, "tag both");

    d = dev(4);
    CHECK(d->decision == FWA_DEC_NONE && !d->driver && lpx_base(d->tag) == LPX_B_NEITHER,
          "unknown: nothing touches it");
    CHECK(lpx_action(d->tag) == LPX_ACT_WAIT, "neither waits");

    d = dev(5);
    CHECK(d->decision == FWA_DEC_QUARANTINE && lpx_forbidden(d->tag) && !d->driver,
          "fault quarantines");
    CHECK(lpx_action(d->tag) == LPX_ACT_ROLLBACK, "rollback action");

    d = dev(6);
    CHECK(d->presence == LPRES_STATE_BOTH && d->decision == FWA_DEC_DISABLED && d->flagged,
          "presence conflict");

    d = dev(7);
    CHECK(d->presence == LPRES_STATE_FALSE && d->decision == FWA_DEC_NONE &&
              lpx_base(d->tag) == LPX_B_FALSE,
          "empty slot");

    d = dev(8);
    CHECK(d->decision == FWA_DEC_DEFER && lpx_pol(d->tag) == LPX_P_ENTROPIC && !d->driver,
          "stale defers");

    d = dev(9);
    CHECK(d->decision == FWA_DEC_SPECIFIC && !strcmp(d->driver->name, "rtl8139") &&
              lpx_pol(d->tag) == LPX_P_NEUTRAL,
          "single fresh source");

    d = dev(10);
    CHECK(d->decision == FWA_DEC_CLASS_GENERIC && !strcmp(d->driver->name, "ide-legacy"),
          "class generic IDE");

    d = dev(11);
    CHECK(d->klass == LPRES_STATE_BOTH && d->decision == FWA_DEC_DISABLED && d->flagged,
          "class conflict disables");

    d = dev(12);
    CHECK(d->decision == FWA_DEC_UNAVAILABLE && !strcmp(d->driver->name, "usb-hid-boot-keyboard"),
          "usb hid known");

    d = dev(13);
    CHECK(d->decision == FWA_DEC_NONE && !d->driver, "non-standard class: none");

    d = dev(14);
    CHECK(d->decision == FWA_DEC_UNAVAILABLE && !strcmp(d->driver->name, "virtio-pci"),
          "virtio-pci not built");

    CHECK(fwa_count(&A, FWA_DEC_SPECIFIC) == 2, "count specific");
    CHECK(fwa_count(&A, FWA_DEC_QUARANTINE) == 1, "count quarantine");

    /* A5 sweep: across every combination of sources, a non-standard class
     * and unknown ID never get a driver, and a fault never binds. */
    for (unsigned mask = 0; mask < 32; mask++) {
        fwa_init(&A, tab, nt, 0);
        for (uint8_t s = 0; s < FWA_SRC_COUNT; s++)
            if (mask & (1u << s)) names(FWA_BUS_PCI, 1, s, 0xDEAD0000u + s % 2, 0xFF0000u, 0);
        kind(FWA_BUS_PCI, 2, FWA_SRC_PROBE, FWA_CLAIM_FAULT);
        if (mask & 1) names(FWA_BUS_PCI, 2, FWA_SRC_PCI, 0x10EC8139u, 0x020000u, 0);
        fwa_decide(&A);
        d = dev(1);
        CHECK(!d || (d->decision != FWA_DEC_SPECIFIC && d->decision != FWA_DEC_CLASS_GENERIC &&
                     d->decision != FWA_DEC_SAFE_GENERIC),
              "never guess");
        CHECK(dev(2)->decision == FWA_DEC_QUARANTINE, "fault always quarantines");
    }

    /* A6: profile nodes */
    fwa_init(&A, tab, nt, 0);
    names(FWA_BUS_PLATFORM, 0x10, FWA_SRC_DT, VIO(1), FWA_ID_NONE, 0);
    names(FWA_BUS_PCI, 0x20, FWA_SRC_PCI, 0x10EC8139u, 0x020000u, 0);
    names(FWA_BUS_PCI, 0x20, FWA_SRC_ACPI, 0x10EC8100u, 0x020000u, 0);
    kind(FWA_BUS_PCI, 0x30, FWA_SRC_PCI, FWA_CLAIM_EMPTY);
    fwa_decide(&A);
    hwp_profile_t p;
    hwp_init(&p);
    uint16_t root;
    CHECK(hwp_node_add(&p, 1, 0xFFFF, HWP_CLASS_MACHINE, &root) == HWP_OK, "root");
    CHECK(fwa_to_hwp(&A, &p, 1, 10) == FWA_OK, "to hwp");
    CHECK(p.n == 3, "empty slot left out");
    CHECK(p.node[1].cls == HWP_CLASS_NETWORK && p.node[1].ext_len == 20 &&
              !memcmp(p.node[1].ext, "FWA1", 4),
          "virtio-net node");
    CHECK(p.node[2].ext[6] == FWA_DEC_DISABLED && p.node[2].ext[7] == 1,
          "conflict recorded in ext");
    static uint8_t buf[65536];
    uint32_t len;
    hwp_profile_t back;
    CHECK(hwp_encode(&p, buf, sizeof buf, &len) == HWP_OK &&
              hwp_decode(buf, len, &back) == HWP_OK && back.n == 3,
          "profile round-trips canonically");

    /* arguments */
    fwa_claim_t bad = {9, 0, 0, 0, 0};
    CHECK(fwa_claim(&A, FWA_BUS_PCI, 1, &bad) == FWA_E_ARG, "bad source");
    CHECK(fwa_claim(&A, 0, 1, &(fwa_claim_t){0, 0, 0, 0, 0}) == FWA_E_ARG, "bad bus");
    fwa_init(&A, tab, nt, 0);
    for (unsigned i = 0; i < FWA_MAX_CLAIMS; i++) names(FWA_BUS_PCI, 1, FWA_SRC_PCI, 1, 1, 0);
    CHECK(fwa_claim(&A, FWA_BUS_PCI, 1, &(fwa_claim_t){0, 0, 1, 1, 0}) == FWA_E_FULL,
          "claims full");
    CHECK(!strcmp(fwa_decision_name(FWA_DEC_SAFE_GENERIC), "safe-generic") &&
              !strcmp(fwa_src_name(FWA_SRC_DT), "devicetree"),
          "names");

    printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
