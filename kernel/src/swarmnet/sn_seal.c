/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sn_seal.c — the post-hydration seal (swarmnet.h).
 *
 * Kept out of the hydration code it wipes: the kernel's linker script puts
 * sn_link, sn_proto, sn_http, sn_fetch and the ipfs_node walker between
 * __zxv_hydrate_start and __zxv_hydrate_end, and this file elsewhere. */
#include "swarmnet.h"

static int g_sealed;
static lpres_state_t g_verdict = LPRES_STATE_NEITHER;

static struct {
    int (*fn)(void *ctx);
    void *ctx;
    char name[SN_SEAL_NAME_MAX + 1u];
} g_hook[SN_SEAL_HOOK_MAX];
static uint32_t g_nhook;

int sn_sealed(void)
{
    return g_sealed;
}

lpres_state_t sn_seal_verdict(void *ctx)
{
    (void) ctx;
    return g_verdict;
}

int sn_seal_on(int (*fn)(void *ctx), void *ctx, const char *name)
{
    uint32_t n = 0;
    if (!fn || !name || g_sealed || g_nhook >= SN_SEAL_HOOK_MAX) return -1;
    while (name[n] && n <= SN_SEAL_NAME_MAX) n++;
    if (n == 0 || n > SN_SEAL_NAME_MAX) return -1;
    for (uint32_t i = 0; i < g_nhook; i++)
        if (g_hook[i].fn == fn && g_hook[i].ctx == ctx) return -1;
    g_hook[g_nhook].fn = fn;
    g_hook[g_nhook].ctx = ctx;
    hw_memcpy(g_hook[g_nhook].name, name, n);
    g_hook[g_nhook].name[n] = 0;
    g_nhook++;
    return 0;
}

static void say(const hw_plat_t *p, const char *a, const char *b)
{
    hw_line_t s;
    hw_line_init(&s);
    hw_line_s(&s, a);
    if (b) hw_line_s(&s, b);
    p->log(s.b);
}

/* PROMPT_USER: p or m on any console within SN_SEAL_PROMPT_MS; no answer
 * powers the NIC down, the mode that leaves nothing reachable. */
static uint8_t ask(sn_link_t *l, const sn_seal_io_t *io)
{
    const hw_plat_t *p = l->p;
    p->log("[SEAL] Swarm discs hydrated. Seal the network: press p to power the NIC down "
           "(air-gapped), or m to keep it for the local mesh only.");
    p->log("[SEAL] No answer in 30 seconds powers the NIC down.");
    uint64_t end = l->clk.ms(l->clk.ctx) + SN_SEAL_PROMPT_MS;
    while (l->clk.ms(l->clk.ctx) < end) {
        int k = io->key ? io->key(io->ctx) : 0;
        if (k == 'p' || k == 'P') {
            p->log("[SEAL] p: powering the NIC down.");
            return HW_SEAL_POWERDOWN_NIC;
        }
        if (k == 'm' || k == 'M') {
            p->log("[SEAL] m: keeping the NIC for the local mesh only.");
            return HW_SEAL_LOCAL_MESH_ONLY;
        }
        if (l->clk.idle) l->clk.idle(l->clk.ctx);
    }
    p->log("[SEAL] No answer: powering the NIC down.");
    return HW_SEAL_POWERDOWN_NIC;
}

int sn_seal(sn_link_t *l, sn_fetch_t *f, const hw_models_t *m, const sn_seal_io_t *io)
{
    const hw_plat_t *p = l->p;
    hw_nic_t *nic = l->nic;
    hw_line_t s;
    uint8_t mode = m->seal == HW_SEAL_PROMPT_USER ? ask(l, io) : m->seal;

    /* 0. the flush hooks, while the network is still up */
    for (uint32_t i = 0; i < g_nhook; i++) {
        int rc = g_hook[i].fn(g_hook[i].ctx);
        hw_line_init(&s);
        hw_line_s(&s, rc ? "  [SEAL_FLUSH_FAIL] " : "  [SEAL_FLUSH] ");
        hw_line_s(&s, g_hook[i].name);
        hw_line_s(&s, rc ? ": did not drain; its buffers go with the network" : ": drained");
        p->log(s.b);
    }

    /* 1. the NIC: off the bus, or kept for the mesh */
    int off = 0;
    if (mode == HW_SEAL_POWERDOWN_NIC) {
        off = hw_nic_power_off(p, nic);
        hw_line_init(&s);
        hw_line_s(&s, off < 0 ? "  [SEAL_FAIL] " : "  [SEAL_NIC] ");
        hw_line_s(&s, nic->name);
        if (nic->on_pci) {
            hw_line_s(&s, " (");
            hw_line_bdf(&s, nic->bdf);
            hw_line_c(&s, ')');
        }
        if (off < 0)
            hw_line_s(&s, nic->on_pci ? ": PCI Bus Master or Memory Space did not clear; the "
                                        "network is NOT severed"
                                      : ": not a PCI device; the network is NOT severed");
        else
            hw_line_s(&s, off & HW_NIC_OFF_PHY
                              ? ": PHY powered down; PCI Bus Master and Memory Space cleared "
                                "and read back"
                              : ": PHY left as it is (no safe PHY access on this part); PCI Bus "
                                "Master and Memory Space cleared and read back");
        p->log(s.b);
    }

    /* 2. everything the hydration held: the gateway connections with: the
     * rest of the stack's state and leases, the buffers, then the code itself, overwritten with
     * INT3 so a stray call traps */
    uint32_t held = (uint32_t) sizeof *f + (uint32_t) sizeof *l;
    char name[sizeof nic->name];
    hw_memcpy(name, nic->name, sizeof name);
    hw_memset(f, 0, sizeof *f);
    const sn_clock_t clk = l->clk;
    hw_memset(l, 0, sizeof *l);
    l->clk = clk;
    uint32_t bad = 0;
    for (uint32_t i = 0; i < io->code_len; i++) io->code[i] = 0xCC;
    for (uint32_t i = 0; i < io->code_len; i++) bad += io->code[i] != 0xCC;
    g_sealed = 1;
    hw_line_init(&s);
    hw_line_s(&s, bad ? "  [SEAL_FAIL] hydration code: " : "  [SEAL_WIPE] hydration code: ");
    hw_line_u(&s, io->code_len);
    hw_line_s(&s, bad ? " bytes, NOT all overwritten" : " bytes overwritten with INT3, read back");
    hw_line_s(&s, "; fetch buffers and network state: ");
    hw_line_u(&s, held);
    hw_line_s(&s, " bytes zeroed");
    p->log(s.b);
    g_verdict = off < 0 || bad ? LPRES_STATE_FALSE : LPRES_STATE_TRUE;
    if (off < 0 || bad) return -1;

    /* 3. the receipt */
    uint32_t last = 2;
    for (uint32_t i = 0; i < m->nwant; i++)
        if (m->want[i].disc > last) last = m->want[i].disc;
    hw_line_init(&s);
    hw_line_s(&s, "[SEAL] Swarm Discs 2..");
    hw_line_u(&s, last);
    hw_line_s(&s, " successfully hydrated and verified.");
    p->log(s.b);
    say(p, "[SEAL] Master CID: ", m->cid);
    if (mode == HW_SEAL_POWERDOWN_NIC) {
        p->log("[SEAL] Network hydration channel severed: NIC bus mastering disabled.");
        p->log("[SEAL] Operating system is now immutable and air-gapped.");
    } else {
        hw_line_init(&s);
        hw_line_s(&s, "[SEAL] Network hydration channel severed: WAN routes, DHCP and the "
                      "fetcher removed; ");
        hw_line_s(&s, name);
        hw_line_s(&s, " kept for the local mesh.");
        p->log(s.b);
        p->log("[SEAL] Operating system is now immutable; its network is the local mesh only.");
    }
    return mode;
}
