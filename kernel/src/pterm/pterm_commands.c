/* pterm_commands.c — P-TERM command implementations wired to real subsystems
 *
 * Replaces the hardcoded stub output in pterm.c with real subsystem
 * queries: VFS directory listing, scheduler process list, network
 * interface status, M5 routing table, memory stats.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
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
#if defined(TEST_HOST) && (defined(__x86_64__) || defined(__i386__))
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
#if defined(TEST_HOST) && (defined(__x86_64__) || defined(__i386__))
    pterm_write_attr(t, "1    RUNNING   kernel_main    4K\n", PTERM_ATTR_GREEN);
    pterm_write_attr(t, "2    READY     vbe_init       2K\n", PTERM_ATTR_CYAN);
    pterm_write_attr(t, "3    READY     net_stack      8K\n", PTERM_ATTR_YELLOW);
#else
    sched_list_tasks(&sched, pterm_ps_write, t);
#endif
}

void pterm_cmd_vmstat(pterm_t *t) {
#if defined(TEST_HOST) && (defined(__x86_64__) || defined(__i386__))
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
#if defined(TEST_HOST) && (defined(__x86_64__) || defined(__i386__))
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
#if defined(TEST_HOST) && (defined(__x86_64__) || defined(__i386__))
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
#if defined(TEST_HOST) && (defined(__x86_64__) || defined(__i386__))
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

/* The Game Genie live-interception suite (m5_genie.c): `genie list`,
 * `genie enter <code>` and `genie clear`. A code is the canonical symbolic
 * spelling "AAAA-BBBB-CCCC"; genie_decode_code folds it to the
 * {address, compare, replace} tuple and it is armed in the ISVT, so every
 * emulated core's next matching bus read returns the replacement. */
#include "../emu/m5_genie.h"

static int genie_str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void pterm_genie_u32(char *buf, uint32_t v)
{
    char d[10];
    uint32_t n = 0;
    do {
        d[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v && n < 10u);
    while (n) *buf++ = d[--n];
    *buf = 0;
}

void pterm_cmd_genie(pterm_t *t, const char *sub, const char *arg)
{
    char num[16];
    if (!sub) {
        pterm_write_attr(t, "usage: genie list | genie enter <code> | genie clear\n", PTERM_ATTR_YELLOW);
        return;
    }
    if (genie_str_eq(sub, "clear") == 0) {
        genie_clear();
        pterm_write_attr(t, "genie: interception table cleared\n", PTERM_ATTR_GREEN);
        return;
    }
    if (genie_str_eq(sub, "list") == 0) {
        uint32_t n = genie_count();
        pterm_write_attr(t, "genie: active interception vectors\n", PTERM_ATTR_BRIGHT);
        for (uint32_t i = 0; i < n; i++) {
            const zxv_genie_patch_t *p = genie_get(i);
            if (!(p->flags & ZXV_GENIE_FLAG_ENABLED)) continue;
            pterm_write_attr(t, "  ", PTERM_ATTR_NORMAL);
            if (p->code_str[0]) {
                pterm_write(t, p->code_str);
                pterm_write(t, "  ");
            }
            pterm_genie_u32(num, (uint32_t) p->target_addr);
            pterm_write(t, "addr=");
            pterm_write(t, num);
            pterm_genie_u32(num, (uint32_t) p->compare_val);
            pterm_write(t, " cmp=");
            pterm_write(t, num);
            pterm_genie_u32(num, (uint32_t) p->replace_val);
            pterm_write(t, " rep=");
            pterm_write(t, num);
            pterm_genie_u32(num, p->width_bytes);
            pterm_write(t, " w=");
            pterm_write(t, num);
            pterm_genie_u32(num, p->hits);
            pterm_write(t, " hits=");
            pterm_write(t, num);
            pterm_newline(t);
        }
        if (n == 0) pterm_write(t, "  (empty)\n");
        return;
    }
    if (genie_str_eq(sub, "enter") == 0 && arg) {
        zxv_genie_patch_t p;
        if (genie_decode_code(arg, &p) != 0) {
            pterm_write_attr(t, "genie: a code is three dash-separated words, e.g. ZXV-LATT-144X\n",
                             PTERM_ATTR_RED);
            return;
        }
        int idx = genie_apply_intercept(arg, p.target_addr, p.compare_val,
                                        p.replace_val, p.width_bytes);
        if (idx < 0) {
            pterm_write_attr(t, "genie: interception table full\n", PTERM_ATTR_RED);
            return;
        }
        pterm_write_attr(t, "genie: armed ", PTERM_ATTR_GREEN);
        pterm_write(t, arg);
        pterm_write_attr(t, " (ISVT slot ", PTERM_ATTR_GREEN);
        pterm_genie_u32(num, (uint32_t) idx);
        pterm_write(t, num);
        pterm_write_attr(t, ")\n", PTERM_ATTR_GREEN);
        return;
    }
    pterm_write_attr(t, "usage: genie list | genie enter <code> | genie clear\n", PTERM_ATTR_YELLOW);
}
