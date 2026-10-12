/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* xhci.c — placeholder until the driver lands. */
#include "hwdrv.h"



hw_verdict_t xhci_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r,
                       hw_line_t *note)
{
    (void) p;
    (void) d;
    (void) r;
    hw_line_s(note, "driver not built yet");
    return HW_N;
}
