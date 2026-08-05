/* pterm_commands.c — P-TERM command implementations wired to real subsystems
 *
 * Replaces the hardcoded stub output in pterm.c with real subsystem
 * queries: VFS directory listing, scheduler process list, network
 * interface status, M5 routing table, memory stats.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "pterm.h"

/* Forward declarations for subsystem APIs */
#ifndef TEST_HOST
#include "../vfs/vfs.h"
#include "../sched/sched.h"
#include "../net/net.h"
#include "../net/m5route.h"

/* External references to boot-time singletons */
extern vfs_state_t vfs;
extern scheduler_t sched;
extern net_state_t net;
extern m5_router_t router;
#endif

/* ---- Real subsystem-backed command implementations ---- */

#ifndef TEST_HOST
static void pterm_ps_write(void *ctx, const char *s) {
    pterm_write((pterm_t *)ctx, s);
}
#endif

void pterm_cmd_ls(pterm_t *t, const char *path) {
#ifdef TEST_HOST
    (void)path;
    pterm_write_attr(t, ".n9n63  .n9m63  .zxvi  .zxvc\n", PTERM_ATTR_CYAN);
    pterm_write_attr(t, "boot/  kernel/  init/  gui/  tests/\n", PTERM_ATTR_NORMAL);
#else
    vfs_node_t entries[32];
    int32_t count = vfs_list_dir(&vfs, path ? path : "/", entries, 32);
    if (count < 0) {
        pterm_write_attr(t, "ls: cannot access '", PTERM_ATTR_RED);
        pterm_write(t, path ? path : "/");
        pterm_write_attr(t, "'\n", PTERM_ATTR_RED);
        return;
    }
    for (int32_t i = 0; i < count; i++) {
        if (entries[i].is_dir)
            pterm_write_attr(t, entries[i].name, PTERM_ATTR_CYAN);
        else
            pterm_write(t, entries[i].name);
        pterm_write(t, "  ");
    }
    if (count > 0) pterm_newline(t);
#endif
}

void pterm_cmd_ps(pterm_t *t) {
    pterm_write_attr(t, "PID  STATE     NAME           MEM\n", PTERM_ATTR_BRIGHT);
#ifdef TEST_HOST
    pterm_write_attr(t, "1    RUNNING   kernel_main    4K\n", PTERM_ATTR_GREEN);
    pterm_write_attr(t, "2    READY     vbe_init       2K\n", PTERM_ATTR_CYAN);
    pterm_write_attr(t, "3    READY     net_stack      8K\n", PTERM_ATTR_YELLOW);
#else
    sched_list_tasks(&sched, pterm_ps_write, t);
#endif
}

void pterm_cmd_vmstat(pterm_t *t) {
#ifdef TEST_HOST
    pterm_write_attr(t, "Phase-tick: 10ms | Sched: 5-state\n", PTERM_ATTR_NORMAL);
    pterm_write_attr(t, "Memory: 256MB total, 248MB free\n", PTERM_ATTR_GREEN);
#else
    /* memory stats printed with our own tiny integer formatting */
    pterm_write_attr(t, "Scheduler: ", PTERM_ATTR_BRIGHT);
    pterm_write_attr(t, sched.initialized ? "ONLINE" : "OFFLINE", PTERM_ATTR_GREEN);
    pterm_write(t, "  tasks=");
    {
        char num[16];
        uint32_t val = sched.num_tasks;
        int i = 0;
        if (val == 0) { num[i++] = '0'; }
        else {
            char tmp[16]; int ti = 0;
            while (val > 0) { tmp[ti++] = (char)('0' + (val % 10)); val /= 10; }
            while (ti > 0) num[i++] = tmp[--ti];
        }
        num[i] = '\0';
        pterm_write(t, num);
    }
    pterm_write(t, "  ticks=");
    {
        char num[16];
        uint64_t val = sched.ticks;
        int i = 0;
        if (val == 0) { num[i++] = '0'; }
        else {
            char tmp[16]; int ti = 0;
            while (val > 0) { tmp[ti++] = (char)('0' + (val % 10)); val /= 10; }
            while (ti > 0) num[i++] = tmp[--ti];
        }
        num[i] = '\0';
        pterm_write(t, num);
    }
    pterm_newline(t);
#endif
}

void pterm_cmd_net(pterm_t *t) {
    pterm_write_attr(t, "Network interfaces:\n", PTERM_ATTR_BRIGHT);
#ifdef TEST_HOST
    pterm_write_attr(t, "  lo: LOOPBACK (UP) 127.0.0.1\n", PTERM_ATTR_GREEN);
#else
    for (uint32_t i = 0; i < net.num_interfaces; i++) {
        net_interface_t *iface = &net.interfaces[i];
        if (!iface->up) continue;
        pterm_write(t, "  ");
        pterm_write(t, iface->name);
        pterm_write(t, ": ");
        pterm_write_attr(t, iface->type == NET_IF_LOOPBACK ? "LOOPBACK" : "ETHERNET",
                         PTERM_ATTR_CYAN);
        pterm_write_attr(t, iface->up ? " (UP)" : " (DOWN)",
                         iface->up ? PTERM_ATTR_GREEN : PTERM_ATTR_RED);
        pterm_write(t, " ");
        /* IP address */
        for (int j = 0; j < 4; j++) {
            char octet[4];
            uint8_t o = iface->ip[j];
            int k = 0;
            if (o >= 100) octet[k++] = (char)('0' + o/100);
            if (o >= 10) octet[k++] = (char)('0' + (o/10)%10);
            octet[k++] = (char)('0' + o%10);
            octet[k] = '\0';
            pterm_write(t, octet);
            if (j < 3) pterm_write(t, ".");
        }
        pterm_newline(t);
    }
#endif
}

void pterm_cmd_route(pterm_t *t) {
    pterm_write_attr(t, "M5 Omni-Router table:\n", PTERM_ATTR_BRIGHT);
#ifdef TEST_HOST
    pterm_write_attr(t, "  default -> m5://0.0.0.0 (lo)\n", PTERM_ATTR_NORMAL);
#else
    for (uint32_t i = 0; i < router.num_routes; i++) {
        m5_route_entry_t *r = &router.routes[i];
        if (!r->active) continue;
        pterm_write(t, "  ");
        pterm_write(t, r->dest.addr);
        pterm_write(t, " -> ");
        pterm_write(t, m5_proto_name(r->link_proto));
        pterm_write(t, " (metric=");
        {
            char num[16];
            uint32_t val = r->metric;
            int k = 0;
            if (val == 0) num[k++] = '0';
            else {
                char tmp[16]; int ti = 0;
                while (val > 0) { tmp[ti++] = (char)('0' + (val % 10)); val /= 10; }
                while (ti > 0) num[k++] = tmp[--ti];
            }
            num[k] = '\0';
            pterm_write(t, num);
        }
        pterm_write(t, ")");
        pterm_newline(t);
    }
#endif
}

void pterm_cmd_date(pterm_t *t) {
    pterm_write_attr(t, "Phase clock: ", PTERM_ATTR_NORMAL);
#ifdef TEST_HOST
    pterm_write(t, "tick=0 | uptime=0s");
#else
    {
        char num[16];
        uint64_t val = sched.ticks;
        int i = 0;
        if (val == 0) num[i++] = '0';
        else {
            char tmp[16]; int ti = 0;
            while (val > 0) { tmp[ti++] = (char)('0' + (val % 10)); val /= 10; }
            while (ti > 0) num[i++] = tmp[--ti];
        }
        num[i] = '\0';
        pterm_write(t, "tick=");
        pterm_write(t, num);
        pterm_write(t, " | uptime=");
        /* ticks * 10ms = uptime in ms */
        uint64_t uptime_s = val / 100;
        i = 0;
        if (uptime_s == 0) num[i++] = '0';
        else {
            char tmp[16]; int ti = 0;
            while (uptime_s > 0) { tmp[ti++] = (char)('0' + (uptime_s % 10)); uptime_s /= 10; }
            while (ti > 0) num[i++] = tmp[--ti];
        }
        num[i] = '\0';
        pterm_write(t, num);
        pterm_write(t, "s");
    }
#endif
    pterm_newline(t);
}
