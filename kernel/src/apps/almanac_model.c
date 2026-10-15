/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* almanac_model.c — Almanac (see almanac_model.h). */
#include <stdio.h>
#include <string.h>

#include "almanac_model.h"

const zxm_ai_action_t ALMANAC_AI[ALMANAC_AI_COUNT] = {
    {"Explain this machine", "Companion", 400},
};

void almanac_init(almanac_t *a, zxm_link_t *link)
{
    memset(a, 0, sizeof *a);
    a->link = link;
}

int almanac_refresh(almanac_t *a)
{
    zxm_link_t *l = a->link;
    static almanac_t n; /* read into a copy: a failed read keeps the old facts */
    n = *a;
    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc) return rc;
    uint64_t c;
    int32_t z = zxm_get(l, l->result, "zxvd");
    if (zxm_get_str(l, l->result, "os", n.os, sizeof n.os) ||
        zxm_get_str(l, l->result, "os_version", n.os_version, sizeof n.os_version) ||
        zxm_get_str(l, l->result, "arch", n.arch, sizeof n.arch) ||
        zxm_get_str(l, l->result, "host", n.host, sizeof n.host) ||
        zxm_get_u64(l, l->result, "cpus", &c) || c > UINT32_MAX ||
        zxm_get_u64(l, l->result, "mem_total", &n.mem_total) ||
        zxm_get_u64(l, l->result, "mem_avail", &n.mem_avail) ||
        zxm_get_u64(l, l->result, "uptime_s", &n.uptime_s) || z < 0 || l->tok[z].type != W4_J_OBJ ||
        zxm_get_u64(l, z, "pid", &n.pid) || zxm_get_u64(l, z, "cpu_user_us", &n.cpu_user_us) ||
        zxm_get_u64(l, z, "cpu_sys_us", &n.cpu_sys_us) ||
        zxm_get_u64(l, z, "rss_bytes", &n.rss_bytes) ||
        zxm_get_u64(l, z, "max_rss_bytes", &n.max_rss_bytes))
        return ZXM_E_PROTOCOL;
    n.cpus = (uint32_t) c;
    int32_t ld = zxm_get(l, l->result, "load");
    n.load_known = false;
    if (ld >= 0 && l->tok[ld].type == W4_J_ARR && zxm_len(l, ld) == 3) {
        n.load_known = true;
        for (uint32_t i = 0; i < 3; i++) {
            int32_t t = zxm_at(l, ld, i);
            int64_t v;
            if (t < 0 || w4_json_i64(l->resp, &l->tok[t], &v) != W4_OK || v < 0 || v > INT32_MAX)
                return ZXM_E_PROTOCOL;
            n.load_x100[i] = (int32_t) v;
        }
    } else if (ld < 0 || l->tok[ld].type != W4_J_NULL)
        return ZXM_E_PROTOCOL;
    zxm_begin(l, "sys.disk");
    rc = zxm_call(l);
    n.disk_known = false;
    if (rc == ZXM_OK) {
        if (zxm_get_str(l, l->result, "path", n.disk_path, sizeof n.disk_path) ||
            zxm_get_u64(l, l->result, "total", &n.disk_total) ||
            zxm_get_u64(l, l->result, "free", &n.disk_free) ||
            zxm_get_u64(l, l->result, "avail", &n.disk_avail))
            return ZXM_E_PROTOCOL;
        n.disk_known = true;
    } else if (rc != ZXM_E_REMOTE)
        return rc;
    n.have = true;
    *a = n;
    return ZXM_OK;
}

static int32_t used_x100(uint64_t total, uint64_t left)
{
    if (!total || left > total) return -1;
    return (int32_t) ((total - left) * 10000u / total);
}

int32_t almanac_mem_used_x100(const almanac_t *a)
{
    return a->have && a->mem_avail ? used_x100(a->mem_total, a->mem_avail) : -1;
}

int32_t almanac_disk_used_x100(const almanac_t *a)
{
    return a->disk_known ? used_x100(a->disk_total, a->disk_free) : -1;
}

void almanac_uptime_text(uint64_t s, char *out, uint32_t cap)
{
    uint64_t d = s / 86400u, h = s % 86400u / 3600u, m = s % 3600u / 60u;
    if (!cap) return;
    if (d)
        snprintf(out, cap, "%llu day%s, %llu hour%s", (unsigned long long) d, d == 1 ? "" : "s",
                 (unsigned long long) h, h == 1 ? "" : "s");
    else if (h)
        snprintf(out, cap, "%llu hour%s, %llu minute%s", (unsigned long long) h, h == 1 ? "" : "s",
                 (unsigned long long) m, m == 1 ? "" : "s");
    else
        snprintf(out, cap, "%llu minute%s", (unsigned long long) m, m == 1 ? "" : "s");
}

void almanac_bytes_text(uint64_t b, char *out, uint32_t cap)
{
    static const char *const U[] = {"bytes", "KB", "MB", "GB", "TB", "PB", "EB"};
    uint32_t u = 0;
    uint64_t whole = b, tenth = 0;
    if (!cap) return;
    while (whole >= 1000u && u < 6) {
        tenth = whole % 1000u / 100u;
        whole /= 1000u;
        u++;
    }
    if (!u)
        snprintf(out, cap, "%llu %s", (unsigned long long) b, b == 1 ? "byte" : U[0]);
    else if (whole < 10 && tenth)
        snprintf(out, cap, "%llu.%llu %s", (unsigned long long) whole, (unsigned long long) tenth,
                 U[u]);
    else
        snprintf(out, cap, "%llu %s", (unsigned long long) whole, U[u]);
}

int almanac_ai(almanac_t *a, int32_t action)
{
    if (action < 0 || action >= ALMANAC_AI_COUNT) return ZXM_E_ARG;
    if (!a->have) return ZXM_E_STATE;
    static char in[1024];
    char mt[32], ma[32], up[48], dt[32], da[32];
    almanac_bytes_text(a->mem_total, mt, sizeof mt);
    almanac_bytes_text(a->mem_avail, ma, sizeof ma);
    almanac_uptime_text(a->uptime_s, up, sizeof up);
    almanac_bytes_text(a->disk_total, dt, sizeof dt);
    almanac_bytes_text(a->disk_avail, da, sizeof da);
    int n = snprintf(in, sizeof in,
                     "System: %s %s on %s, %u CPUs\nMemory: %s, %s available\nUp: %s\n"
                     "Disk: %s, %s free for ZXV",
                     a->os, a->os_version, a->arch, (unsigned) a->cpus, mt, ma, up,
                     a->disk_known ? dt : "unknown", a->disk_known ? da : "unknown");
    if (n < 0) return ZXM_E_ARG;
    return zxm_ai_ask(a->link, &ALMANAC_AI[action], action,
                      "Explain these facts about this computer in plain words, and say if "
                      "anything looks short (memory, disk). Be brief.",
                      in, (uint32_t) ((size_t) n < sizeof in ? (size_t) n : sizeof in - 1),
                      &a->held);
}

int almanac_ai_accept(almanac_t *a)
{
    return zxm_held_accept(&a->held);
}

int almanac_ai_reject(almanac_t *a)
{
    return zxm_held_reject(&a->held);
}
