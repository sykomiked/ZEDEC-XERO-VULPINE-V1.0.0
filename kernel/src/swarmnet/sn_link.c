/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sn_link.c — one hwdrv NIC bound to the kernel's network stack, and DHCP
 * (swarmnet.h). */
#include "swarmnet.h"

/* Seeds on providers that hand out fixed addresses (the build system's
 * vps_seeds.conf, baked by tools/vps/gen_seedmap.py) get no DHCP answer at
 * all; the map lets a seed recognise its own NIC and configure statically. */
#if defined(__has_include)
#if __has_include("zxv_seedmap.h")
#include "zxv_seedmap.h"
#endif
#endif

static int sn_static(sn_link_t *l);

/* net_interface_t's transmit callback carries no context, so the link in use
 * is kept here: there is one fetch link at a time. */
static sn_link_t *g_link;

static void tx(const uint8_t *frame, uint32_t len)
{
    if (g_link && g_link->nic) g_link->nic->send(g_link->nic, frame, len);
}

void sn_pump(sn_link_t *l)
{
    for (uint32_t k = 0; k < 64; k++) {
        int n = l->nic->recv(l->nic, l->frame, sizeof l->frame);
        if (n <= 0) break;
        net_handle_eth(&l->net, l->eth, l->frame, (uint32_t) n);
    }
    uint64_t t = l->clk.ms(l->clk.ctx);
    if (t >= l->next_tick) {
        l->next_tick = t + SN_TICK_MS;
        net_tcp_tick(&l->net);
        if (l->clk.idle) l->clk.idle(l->clk.ctx);
    }
}

static void ip_s(hw_line_t *s, const uint8_t ip[4])
{
    for (int i = 0; i < 4; i++) {
        if (i) hw_line_c(s, '.');
        hw_line_u(s, ip[i]);
    }
}

int sn_link_up(sn_link_t *l, const hw_plat_t *p, hw_registry_t *r, const sn_clock_t *clk,
               uint32_t (*entropy)(void))
{
    hw_line_t s;
    hw_memset(l, 0, sizeof *l);
    l->p = p;
    l->clk = *clk;
    for (uint32_t i = 0; i < r->n_nic && !l->nic; i++)
        if (r->nic[i]->link_up) l->nic = r->nic[i];
    if (!l->nic) {
        hw_line_init(&s);
        hw_line_s(&s, "  [NET] ");
        if (r->n_nic) {
            hw_line_u(&s, r->n_nic);
            hw_line_s(&s, r->n_nic == 1 ? " NIC, link down" : " NICs, every link down");
        } else {
            hw_line_s(&s, "no NIC the kernel drives");
        }
        p->log(s.b);
        return 0;
    }
    net_init(&l->net);
    if (entropy) net_set_entropy(entropy);
    static const uint8_t zero[4] = {0, 0, 0, 0};
    int32_t ei = net_register_interface(&l->net, "eth0", NET_IF_ETHERNET, l->nic->mac, zero, zero,
                                        zero, tx, 0);
    if (ei < 0) return 0;
    l->eth = &l->net.interfaces[ei];
    g_link = l;
    hw_line_init(&s);
    hw_line_s(&s, "  [NET] eth0 = ");
    hw_line_s(&s, l->nic->name);
    hw_line_s(&s, ", link up, MAC ");
    for (int i = 0; i < 6; i++) {
        if (i) hw_line_c(&s, ':');
        hw_line_x(&s, l->nic->mac[i], 2);
    }
    p->log(s.b);

    for (uint32_t t = 1; t <= SN_DHCP_TRIES && !dhcp_is_bound(&l->net.dhcp); t++) {
        hw_line_init(&s);
        hw_line_s(&s, "  [NET] DHCP discover, attempt ");
        hw_line_u(&s, t);
        hw_line_s(&s, " of 3");
        p->log(s.b);
        net_dhcp_discover(&l->net, l->eth);
        uint64_t end = clk->ms(clk->ctx) + SN_DHCP_MS;
        while (clk->ms(clk->ctx) < end && !dhcp_is_bound(&l->net.dhcp)) sn_pump(l);
    }
    if (!dhcp_is_bound(&l->net.dhcp) && !sn_static(l)) {
        hw_line_init(&s);
        hw_line_s(&s, "  [NET] no lease, NIC MAC not in the seed map");
        p->log(s.b);
        return 0;
    }
    hw_line_init(&s);
    hw_line_s(&s, "  [NET] DHCP lease ");
    ip_s(&s, l->eth->ip);
    hw_line_s(&s, " mask ");
    ip_s(&s, l->eth->netmask);
    hw_line_s(&s, " gateway ");
    ip_s(&s, l->eth->gateway);
    hw_line_s(&s, " dns ");
    ip_s(&s, l->net.dns_server);
    p->log(s.b);
    return 1;
}

#if defined(ZXV_SEEDMAP_N) && ZXV_SEEDMAP_N
/* The seed's own MAC is in the baked map: take that line's address, mask and
 * gateway (DNS falls back to the gateway). 1 = a map line matched. */
static int sn_static(sn_link_t *l)
{
    hw_line_t s;
    for (uint32_t i = 0; i < ZXV_SEEDMAP_N; i++) {
        const zxv_seedmap_t *e = &zxv_seedmap[i];
        if (!hw_memeq(l->nic->mac, e->mac, 6)) continue;
        hw_memcpy(l->eth->ip, e->ip, NET_IP_LEN);
        hw_memcpy(l->eth->netmask, e->mask, NET_IP_LEN);
        hw_memcpy(l->eth->gateway, e->gw, NET_IP_LEN);
        hw_memcpy(l->net.dns_server, e->gw, NET_IP_LEN);
        hw_line_init(&s);
        hw_line_s(&s, "  [NET] no DHCP; seed map gives static lease ");
        ip_s(&s, l->eth->ip);
        l->p->log(s.b);
        return 1;
    }
    return 0;
}
#else
static int sn_static(sn_link_t *l)
{
    (void) l;
    return 0;
}
#endif

void sn_log_offline(const hw_plat_t *p, const hw_models_t *m)
{
    hw_line_t s;
    uint32_t disc = 0;
    for (uint32_t i = 0; i < m->nwant && !disc; i++)
        if (!m->want[i].attached) disc = m->want[i].disc;
    p->log("[NET] Link down or no DHCP response.");
    hw_line_init(&s);
    hw_line_s(&s, "[SWARM] Falling back to local storage scan: Attach Disc ");
    hw_line_u(&s, disc);
    hw_line_s(&s, " USB / drive.");
    p->log(s.b);
}
