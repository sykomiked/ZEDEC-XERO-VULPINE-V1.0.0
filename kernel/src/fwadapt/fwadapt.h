/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fwadapt.h — the smart firmware adapter: which driver, if any, may touch a
 * device, decided by paraconsistent evidence.
 *
 * A machine describes its devices in several places that can disagree: ACPI
 * tables, a device tree, PCI and USB configuration IDs, and live probes. The
 * adapter collects one CLAIM per source per device location, joins them with
 * lpres (knowledge join: told-true | told-false), tags the result with the
 * lpres_hex hexagram and decides:
 *
 *   A1 CLAIMS. A source either NAMES the device (vendor:device and/or a class
 *      code), says the location is EMPTY, or FAULTED while probing (a bus
 *      error, a timeout the bus reports as an error, a parity failure).
 *   A2 JOIN. Per device, identity and class are each one lpres value:
 *      every source naming X tells "X: true"; a source naming Y != X tells
 *      "X: false". The join over sources is TRUE (all who speak agree),
 *      BOTH (they conflict) or NEITHER (nobody names it). Presence joins the
 *      same way (named = present, EMPTY = absent).
 *   A3 POLARITY. Two or more agreeing sources: negentropic (corroborated);
 *      one: neutral; any claim older than the caller's freshness limit:
 *      entropic. A FAULT is forbidden (lpres_hex X4: absorbing).
 *   A4 DECISION (the only ways a driver can be chosen):
 *        forbidden        -> QUARANTINE: no driver, the device is left in
 *                            reset/unpowered by the caller, and the claim
 *                            set is recorded;
 *        presence BOTH or
 *        identity BOTH or
 *        class BOTH       -> SAFE_GENERIC: the class's standard-interface
 *                            driver when the class itself is TRUE and one
 *                            exists, else DISABLED; always FLAGGED for a
 *                            person (ESCALATE);
 *        identity TRUE    -> SPECIFIC: a table driver matching vendor:device;
 *                            if none, fall through to the class rule;
 *        class TRUE       -> CLASS_GENERIC: only a driver for a class code
 *                            whose register interface is defined by a public
 *                            standard (xHCI, NVMe, AHCI, HDA, USB HID, ...);
 *        otherwise        -> NONE: nothing touches the device.
 *      A driver that is listed but not built into this image is reported as
 *      UNAVAILABLE rather than bound. Agreeing but stale evidence (TRUE,
 *      entropic: the action table says WAIT) is DEFER: probe again first.
 *   A5 NEVER GUESS. No rule writes a register on a device whose interface is
 *      not known: SPECIFIC needs an exact ID match, the generic rules need a
 *      standard class code. That is the honest limit: the adapter cannot make
 *      truly unknown hardware work without a spec or a driver; it picks the
 *      right driver when one exists and fails safe when it does not.
 *   A6 PROFILE. fwa_to_hwp appends each decided device to a ZHP-1 profile
 *      (hwemu/hwp.h) as a node of the matching class, its IDs, tag and
 *      decision in the node's extension bytes, so the emulation matrix sees
 *      what is natively served and what is not.
 *
 * Freestanding C11: no libc, no allocation, no float.
 */
#ifndef FWADAPT_H
#define FWADAPT_H

#include <stdbool.h>
#include <stdint.h>
#include "lpres_hex.h"
#include "../hwemu/hwp.h"

#define FWA_MAX_DEVICES 64u
#define FWA_MAX_CLAIMS  8u /* per device */

typedef enum {
    FWA_SRC_ACPI = 0,
    FWA_SRC_DT = 1,    /* device tree */
    FWA_SRC_PCI = 2,   /* PCI configuration space */
    FWA_SRC_USB = 3,   /* USB descriptors */
    FWA_SRC_PROBE = 4, /* a live, read-only probe */
    FWA_SRC_COUNT = 5
} fwa_src_t;

typedef enum {
    FWA_CLAIM_NAMES = 0, /* the source names a device here */
    FWA_CLAIM_EMPTY = 1, /* the source says nothing is here */
    FWA_CLAIM_FAULT = 2  /* probing here faulted */
} fwa_claim_kind_t;

/* Buses for the location key and for driver matching. */
typedef enum { FWA_BUS_PCI = 1, FWA_BUS_USB = 2, FWA_BUS_PLATFORM = 3 } fwa_bus_t;

#define FWA_ID_NONE 0xFFFFFFFFu /* "this source does not say" */

typedef struct {
    uint8_t src;  /* fwa_src_t */
    uint8_t kind; /* fwa_claim_kind_t */
    uint32_t id;  /* vendor << 16 | device, or FWA_ID_NONE */
    uint32_t cls; /* bus class code (PCI: class<<16|sub<<8|progif;
                     USB: interface class<<16|sub<<8|proto), or FWA_ID_NONE */
    uint64_t age; /* how old the claim is, in the caller's ticks */
} fwa_claim_t;

typedef enum {
    FWA_DEC_NONE = 0,
    FWA_DEC_SPECIFIC = 1,
    FWA_DEC_CLASS_GENERIC = 2,
    FWA_DEC_SAFE_GENERIC = 3,
    FWA_DEC_DISABLED = 4,
    FWA_DEC_QUARANTINE = 5,
    FWA_DEC_UNAVAILABLE = 6, /* the right driver is known but not in this image */
    FWA_DEC_DEFER = 7        /* agreeing but stale evidence: probe again first */
} fwa_decision_t;

/* A driver the adapter may choose. A SPECIFIC entry matches id under
 * id_mask; a CLASS entry matches cls under cls_mask and must be marked
 * standard (its register interface is a public standard). */
typedef struct {
    const char *name;
    uint8_t bus;   /* fwa_bus_t */
    bool by_class; /* false: match id; true: match cls */
    uint32_t id, id_mask;
    uint32_t cls, cls_mask;
    uint32_t hwp_class; /* hwp_class_t for the profile */
    bool standard;      /* class driver for a standard interface */
    bool built;         /* present in this image */
} fwa_driver_t;

typedef struct {
    uint8_t bus;
    uint32_t loc; /* bus address (PCI bdf, USB port path, ...) */
    fwa_claim_t claim[FWA_MAX_CLAIMS];
    uint8_t nclaim;
    /* results (fwa_decide) */
    lpres_state_t presence, identity, klass;
    uint32_t id, cls; /* the agreed values (FWA_ID_NONE if none) */
    lpx_t tag;
    uint8_t decision;           /* fwa_decision_t */
    bool flagged;               /* needs a person (A4 BOTH) */
    const fwa_driver_t *driver; /* chosen or known-but-unavailable */
} fwa_device_t;

typedef struct {
    fwa_device_t dev[FWA_MAX_DEVICES];
    uint16_t n;
    const fwa_driver_t *table;
    uint16_t ntable;
    uint64_t fresh_limit; /* claims older than this are stale (A3); 0 = never */
} fwa_t;

typedef enum { FWA_OK = 0, FWA_E_ARG = -1, FWA_E_FULL = -2 } fwa_status_t;

/* The built-in driver table: virtio and the legacy drivers that exist in
 * this tree, plus standard class interfaces (marked built only where the
 * driver is in the tree). */
const fwa_driver_t *fwa_builtin_table(uint16_t *out_n);

void fwa_init(fwa_t *a, const fwa_driver_t *table, uint16_t ntable, uint64_t fresh_limit);
/* Add one claim for the device at (bus, loc); the device is created on its
 * first claim. */
int fwa_claim(fwa_t *a, uint8_t bus, uint32_t loc, const fwa_claim_t *c);
/* Join the claims and decide every device (A2-A5). */
void fwa_decide(fwa_t *a);
/* Count devices with a decision. */
uint16_t fwa_count(const fwa_t *a, fwa_decision_t d);
/* A6: append each device (parent = `parent` id, ids from `first_id`). */
int fwa_to_hwp(const fwa_t *a, hwp_profile_t *p, uint16_t parent, uint16_t first_id);

const char *fwa_decision_name(fwa_decision_t d);
const char *fwa_src_name(fwa_src_t s);

#endif /* FWADAPT_H */
