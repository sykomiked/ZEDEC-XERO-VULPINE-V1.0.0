/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hw_sed.c — unlock self-encrypting NVMe disks at boot (hwdrv.h).
 *
 * Opal travels over NVMe Security Send/Receive (nvme_security). An AHCI or
 * USB disk has no such path here (ATA TRUSTED SEND/RECEIVE is not built), so
 * it is reported and skipped. QEMU's NVMe has no Opal: Security Receive is
 * refused, and that is the "not an SED" line, not a fault.
 *
 * Defaults taken (no spec names them): the Nth NVMe disk in probe order uses
 * arbiter key slot N, signs as Admin1 of the Locking SP (the authority that
 * may set both the range and MBRControl without extra ACEs), and unlocks the
 * global range. The arbiter gets 100 polls 1 ms apart to answer. */
#include "hwdrv.h"
#include "../opal/opal_arb.h"

#define SED_POLLS   100u
#define SED_WAIT_US 1000u

static opal_dev_t g_sed; /* 4 KB of buffers: kept off the boot stack */

static int sed_send(void *ctx, uint8_t proto, uint16_t sp, uint8_t *buf, uint32_t len)
{
    return nvme_security((hw_blk_t *) ctx, 1, proto, sp, buf, len);
}

static int sed_recv(void *ctx, uint8_t proto, uint16_t sp, uint8_t *buf, uint32_t len)
{
    return nvme_security((hw_blk_t *) ctx, 0, proto, sp, buf, len);
}

static void sed_wait(void *ctx)
{
    const hw_plat_t *p = (const hw_plat_t *) ctx;
    p->delay_us(p->ctx, SED_WAIT_US);
}

static void sed_what(hw_line_t *l, const opal_disc_t *ds)
{
    hw_line_s(l, opal_ssc_str(ds->ssc));
    hw_line_s(l, " SED, ComID 0x");
    hw_line_x(l, ds->base_comid, 4);
}

uint32_t hw_sed_unlock_all(const hw_plat_t *p, hw_registry_t *r, struct arb_host *arb)
{
    static const opal_auth_t admin1 = {OPAL_AUTH_ADMIN1, 0};
    uint32_t unlocked = 0, slot = 0;
    for (uint32_t i = 0; i < r->n_blk; i++) {
        hw_blk_t *b = r->blk[i];
        hw_line_t l;
        hw_line_init(&l);
        hw_line_s(&l, "  [SED] ");
        hw_line_s(&l, b->name);
        hw_line_s(&l, ": ");
        if (nvme_security(b, 0, 0, 0, 0, 0) == -2) {
            hw_line_s(&l, "no Security Send/Receive path for this disk, skipped");
            p->log(l.b);
            continue;
        }
        uint32_t s = slot++;
        opal_init(&g_sed, sed_send, sed_recv, b);
        opal_unlock_result_t res;
        int rc = opal_arb_unlock(&g_sed, arb, s, admin1, 0, SED_POLLS, sed_wait, (void *) p, &res);
        if (rc == OPAL_E_TRANSPORT && res.stage == OPAL_STAGE_DISCOVERY) {
            hw_line_s(&l, "not an SED (Security Receive refused), nothing to unlock");
        } else if (rc == OPAL_E_NOT_SED) {
            hw_line_s(&l, "not an SED (no Locking feature), nothing to unlock");
        } else if (rc == OPAL_E_UNSUPPORTED) {
            hw_line_s(&l, g_sed.disc.enterprise ? "Enterprise SSC SED, not supported, left as is"
                                                : "Locking without a known SSC, left as is");
        } else if (rc == OPAL_E_DISC) {
            hw_line_s(&l, "Level 0 Discovery malformed, left as is");
        } else if (rc == OPAL_OK && !res.needed) {
            sed_what(&l, &g_sed.disc);
            hw_line_s(&l, ", not locked");
        } else if (rc == OPAL_OK) {
            sed_what(&l, &g_sed.disc);
            hw_line_s(&l, ", unlocked with arbiter key slot ");
            hw_line_u(&l, s);
            hw_line_s(&l, " as Admin1");
            if (res.flags_after & OPAL_LK_LOCKED) hw_line_s(&l, " (other ranges stay locked)");
            unlocked++;
        } else if (rc == OPAL_E_NOT_ATTESTED) {
            sed_what(&l, &g_sed.disc);
            hw_line_s(&l, ", locked; no attested arbiter, left locked");
        } else if (rc == OPAL_E_KEY || (rc == OPAL_E_ARG && res.stage == OPAL_STAGE_KEY)) {
            sed_what(&l, &g_sed.disc);
            hw_line_s(&l, ", locked; arbiter key slot ");
            hw_line_u(&l, s);
            hw_line_s(&l, " not released (status ");
            hw_line_u(&l, res.key_status);
            hw_line_s(&l, "), left locked");
        } else {
            hw_line_s(&l, "unlock failed at ");
            hw_line_s(&l, opal_stage_str(res.stage));
            hw_line_s(&l, ": ");
            hw_line_s(&l, opal_err_str(rc));
            if (rc == OPAL_E_STATUS) {
                hw_line_s(&l, " ");
                hw_line_s(&l, opal_status_str(res.status));
            }
        }
        p->log(l.b);
    }
    opal_wipe(&g_sed, sizeof g_sed);
    return unlocked;
}
