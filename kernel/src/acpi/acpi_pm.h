/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* acpi_pm.h — ACPI power management on top of the AML interpreter.
 *
 *   acpi_pm_init     FADT -> DSDT + SSDTs into the namespace, ECDT or PNP0C09
 *                    embedded controller, _REG and _INI, sleep packages
 *   acpi_pm_report   one boot line per finding (namespace size, \_S5_, S3,
 *                    power button, battery, lid, thermal zones, processors,
 *                    backlight), so a boot log shows what the firmware offers
 *   acpi_enter_sleep _PTS, then SLP_TYP|SLP_EN into PM1a/PM1b control (or the
 *                    sleep control register on hardware-reduced platforms);
 *                    _WAK on return. acpi_poweroff is S5.
 *   acpi_poll_events fixed power button (PM1 status), GPE methods (_Lxx/_Exx),
 *                    EC queries, queued Notify() — polled, no SCI handler.
 *
 * Integer only; every register goes through aml_plat_t (port I/O or mapped
 * memory, per the FADT's Generic Address Structures). */
#ifndef ZXV_ACPI_PM_H
#define ZXV_ACPI_PM_H

#include <stdint.h>

#include "acpi.h"
#include "aml.h"

typedef struct acpi_gas {
    uint8_t space; /* 0 memory, 1 I/O */
    uint8_t bit_width, bit_offset, access;
    uint64_t addr;
} acpi_gas_t;

typedef struct acpi_fadt_info {
    uint8_t rev;
    uint32_t flags;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t acpi_enable, acpi_disable;
    acpi_gas_t pm1a_evt, pm1b_evt, pm1a_cnt, pm1b_cnt, pm_tmr, gpe0, gpe1;
    acpi_gas_t reset, sleep_ctl, sleep_sts;
    uint8_t pm1_evt_len, pm1_cnt_len, gpe0_len, gpe1_len, gpe1_base, reset_value;
    uint64_t dsdt, facs;
} acpi_fadt_info_t;

#define ACPI_FADT_PWR_BUTTON  (1u << 4)  /* 1: control-method power button */
#define ACPI_FADT_SLP_BUTTON  (1u << 5)
#define ACPI_FADT_RESET_REG   (1u << 10)
#define ACPI_FADT_HW_REDUCED  (1u << 20)

#define ACPI_PM1_PWRBTN (1u << 8)
#define ACPI_PM1_WAK    (1u << 15)
#define ACPI_PM1_SCI_EN (1u << 0)
#define ACPI_SLP_EN     (1u << 13)

/* Events from acpi_poll_events. */
#define ACPI_EV_PWRBTN 0x01
#define ACPI_EV_GPE    0x02
#define ACPI_EV_NOTIFY 0x04
#define ACPI_EV_LID    0x08
#define ACPI_EV_EC     0x10

/* Parse a FADT (any revision); 0 = ok. X_ fields win when present. */
int acpi_parse_fadt(const uint8_t *fadt, uint32_t len, acpi_fadt_info_t *out);

typedef struct acpi_pm {
    aml_ctx_t *aml;
    const aml_plat_t *plat;
    acpi_fadt_info_t fadt;
    uint8_t have_fadt;
    uint8_t sleep_ok[6];
    uint8_t typa[6], typb[6];
    uint32_t loaded, load_errors, inis;
    uint32_t gpe_methods;
    uint8_t acpi_mode; /* SCI_EN set */
    void (*flush_caches)(void); /* optional, before SLP_EN (wbinvd) */
} acpi_pm_t;

/* Load everything; failures are logged and the rest continues. Returns the
 * number of definition blocks loaded (0 = no namespace). */
int acpi_pm_init(acpi_pm_t *pm, aml_ctx_t *aml, const aml_plat_t *plat, acpi_state_t *tables);
/* Switch the chipset into ACPI mode (SMI_CMD <- ACPI_ENABLE) if needed. */
int acpi_pm_enable(acpi_pm_t *pm);
/* Boot report: one line per finding, through plat->log. Returns the lines. */
uint32_t acpi_pm_report(acpi_pm_t *pm);

int acpi_enter_sleep(acpi_pm_t *pm, uint32_t state);
int acpi_poweroff(acpi_pm_t *pm);
uint32_t acpi_poll_events(acpi_pm_t *pm);

/* Register access through a GAS (width from bit_width, default 8). */
uint32_t acpi_gas_read(const aml_plat_t *p, const acpi_gas_t *g, uint32_t off, uint32_t width);
void acpi_gas_write(const aml_plat_t *p, const acpi_gas_t *g, uint32_t off, uint32_t width,
                    uint32_t v);

#endif
