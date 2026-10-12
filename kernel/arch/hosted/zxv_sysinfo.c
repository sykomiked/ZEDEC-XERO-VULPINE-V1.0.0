/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_sysinfo.c — see zxv_sysinfo.h */
#if defined(__linux__)
#    define _GNU_SOURCE
#elif defined(__APPLE__)
#    define _DARWIN_C_SOURCE
#endif

#include "zxv_sysinfo.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#    include <winsock2.h>
#    include <windows.h>
#    include <psapi.h>
#else
#    include <arpa/inet.h>
#    include <ifaddrs.h>
#    include <net/if.h>
#    include <netinet/in.h>
#    include <stdlib.h>
#    include <sys/resource.h>
#    include <sys/socket.h>
#    include <sys/statvfs.h>
#    include <sys/types.h>
#    include <sys/utsname.h>
#    include <time.h>
#    include <unistd.h>
#endif
#if defined(__linux__)
#    include <linux/if_link.h>
#    include <sys/sysinfo.h>
#elif defined(__APPLE__)
#    include <mach/mach.h>
#    include <net/if_dl.h>
#    include <sys/sysctl.h>
#endif

static void copy(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

#if defined(__linux__)
/* one "Key:   123 kB" line of /proc/meminfo, in bytes; 0 when absent */
static uint64_t meminfo(const char *key)
{
    FILE *f = fopen("/proc/meminfo", "r");
    char line[128];
    uint64_t v = 0;
    size_t kl = strlen(key);
    if (!f) return 0;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, key, kl) && line[kl] == ':') {
            unsigned long long kb = 0;
            if (sscanf(line + kl + 1, "%llu", &kb) == 1) v = (uint64_t) kb * 1024u;
            break;
        }
    fclose(f);
    return v;
}
#endif

int zsi_host(zsi_host_t *h)
{
    memset(h, 0, sizeof *h);
    h->load_x100[0] = h->load_x100[1] = h->load_x100[2] = -1;
#if defined(_WIN32)
    copy(h->os, sizeof h->os, "windows");
    OSVERSIONINFOW v = {sizeof v, 0, 0, 0, 0, {0}};
    /* GetVersionEx reports 6.2 to unmanifested programs on Windows 8 and
     * later; RtlGetVersion tells the truth. */
    typedef LONG(WINAPI * rtl_fn)(OSVERSIONINFOW *);
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    rtl_fn rtl = nt ? (rtl_fn) (void *) GetProcAddress(nt, "RtlGetVersion") : NULL;
    if (rtl && rtl(&v) == 0)
        snprintf(h->os_version, sizeof h->os_version, "%lu.%lu.%lu",
                 (unsigned long) v.dwMajorVersion, (unsigned long) v.dwMinorVersion,
                 (unsigned long) v.dwBuildNumber);
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    copy(h->arch, sizeof h->arch,
         si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64   ? "x86_64"
         : si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "arm64"
         : si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL ? "x86"
                                                                     : "other");
    h->cpus = si.dwNumberOfProcessors;
    DWORD hl = sizeof h->host;
    if (!GetComputerNameA(h->host, &hl)) h->host[0] = 0;
    MEMORYSTATUSEX ms = {sizeof ms, 0, 0, 0, 0, 0, 0, 0, 0};
    if (GlobalMemoryStatusEx(&ms)) {
        h->mem_total = ms.ullTotalPhys;
        h->mem_avail = ms.ullAvailPhys;
    }
    h->uptime_s = GetTickCount64() / 1000u;
    return 0;
#else
    struct utsname u;
#    if defined(__APPLE__)
    copy(h->os, sizeof h->os, "macos");
#    elif defined(__linux__)
    copy(h->os, sizeof h->os, "linux");
#    else
    copy(h->os, sizeof h->os, "other");
#    endif
    if (uname(&u) == 0) {
        copy(h->os_version, sizeof h->os_version, u.release);
        copy(h->arch, sizeof h->arch, !strcmp(u.machine, "aarch64") ? "arm64" : u.machine);
    }
    if (gethostname(h->host, sizeof h->host - 1) != 0) h->host[0] = 0;
    h->host[sizeof h->host - 1] = 0;
    long c = sysconf(_SC_NPROCESSORS_ONLN);
    h->cpus = c > 0 ? (uint32_t) c : 0;
    double la[3];
    if (getloadavg(la, 3) == 3)
        for (int i = 0; i < 3; i++) h->load_x100[i] = (int32_t) (la[i] * 100.0 + 0.5);
#    if defined(__linux__)
    struct sysinfo s;
    if (sysinfo(&s) == 0) {
        h->uptime_s = (uint64_t) s.uptime;
        h->mem_total = (uint64_t) s.totalram * s.mem_unit;
    }
    h->mem_avail = meminfo("MemAvailable");
#    elif defined(__APPLE__)
    uint64_t mem = 0;
    size_t ml = sizeof mem;
    if (sysctlbyname("hw.memsize", &mem, &ml, NULL, 0) == 0) h->mem_total = mem;
    struct timeval bt;
    size_t bl = sizeof bt;
    if (sysctlbyname("kern.boottime", &bt, &bl, NULL, 0) == 0) {
        time_t now = time(NULL);
        if (now > bt.tv_sec) h->uptime_s = (uint64_t) (now - bt.tv_sec);
    }
    vm_statistics64_data_t vm;
    mach_msg_type_number_t cnt = HOST_VM_INFO64_COUNT;
    vm_size_t page = 0;
    if (host_page_size(mach_host_self(), &page) == KERN_SUCCESS &&
        host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t) &vm, &cnt) ==
            KERN_SUCCESS)
        h->mem_avail = ((uint64_t) vm.free_count + vm.inactive_count + vm.purgeable_count) * page;
#    endif
    return 0;
#endif
}

int zsi_self(zsi_self_t *s)
{
    memset(s, 0, sizeof *s);
#if defined(_WIN32)
    FILETIME c, e, k, u;
    HANDLE p = GetCurrentProcess();
    s->pid = GetCurrentProcessId();
    if (GetProcessTimes(p, &c, &e, &k, &u)) {
        /* 100 ns units */
        s->cpu_user_us = (((uint64_t) u.dwHighDateTime << 32) | u.dwLowDateTime) / 10u;
        s->cpu_sys_us = (((uint64_t) k.dwHighDateTime << 32) | k.dwLowDateTime) / 10u;
    }
    PROCESS_MEMORY_COUNTERS pm;
    if (GetProcessMemoryInfo(p, &pm, sizeof pm)) {
        s->rss_bytes = pm.WorkingSetSize;
        s->max_rss_bytes = pm.PeakWorkingSetSize;
    }
    return 0;
#else
    struct rusage r;
    s->pid = (uint64_t) getpid();
    if (getrusage(RUSAGE_SELF, &r) == 0) {
        s->cpu_user_us = (uint64_t) r.ru_utime.tv_sec * 1000000u + (uint64_t) r.ru_utime.tv_usec;
        s->cpu_sys_us = (uint64_t) r.ru_stime.tv_sec * 1000000u + (uint64_t) r.ru_stime.tv_usec;
#    if defined(__APPLE__)
        s->max_rss_bytes = (uint64_t) r.ru_maxrss; /* bytes on macOS */
#    else
        s->max_rss_bytes = (uint64_t) r.ru_maxrss * 1024u; /* KiB on Linux and the BSDs */
#    endif
    }
#    if defined(__linux__)
    FILE *f = fopen("/proc/self/statm", "r");
    unsigned long long size, res;
    if (f) {
        if (fscanf(f, "%llu %llu", &size, &res) == 2) {
            long pg = sysconf(_SC_PAGESIZE);
            if (pg > 0) s->rss_bytes = (uint64_t) res * (uint64_t) pg;
        }
        fclose(f);
    }
#    elif defined(__APPLE__)
    mach_task_basic_info_data_t ti;
    mach_msg_type_number_t n = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t) &ti, &n) == KERN_SUCCESS)
        s->rss_bytes = ti.resident_size;
#    endif
    return 0;
#endif
}

int zsi_disk(const char *path, zsi_disk_t *d)
{
    memset(d, 0, sizeof *d);
#if defined(_WIN32)
    ULARGE_INTEGER avail, total, free_;
    if (!GetDiskFreeSpaceExA(path, &avail, &total, &free_)) return -1;
    d->total = total.QuadPart;
    d->free = free_.QuadPart;
    d->avail = avail.QuadPart;
    return 0;
#else
    struct statvfs v;
    if (statvfs(path, &v) != 0) return -1;
    uint64_t unit = v.f_frsize ? v.f_frsize : v.f_bsize;
    d->total = (uint64_t) v.f_blocks * unit;
    d->free = (uint64_t) v.f_bfree * unit;
    d->avail = (uint64_t) v.f_bavail * unit;
    return 0;
#endif
}

int zsi_interfaces(zsi_if_t *out, uint32_t cap, uint32_t *n)
{
    *n = 0;
#if defined(_WIN32)
    (void) out;
    (void) cap;
    return -1; /* not yet: needs iphlpapi in the Windows build */
#else
    struct ifaddrs *all, *a;
    if (getifaddrs(&all) != 0) return -1;
    for (a = all; a; a = a->ifa_next) {
        if (!a->ifa_name) continue;
        uint32_t i = 0;
        while (i < *n && strcmp(out[i].name, a->ifa_name)) i++;
        if (i == *n) {
            if (*n == cap) continue;
            memset(&out[i], 0, sizeof out[i]);
            copy(out[i].name, sizeof out[i].name, a->ifa_name);
            (*n)++;
        }
        zsi_if_t *f = &out[i];
        f->up = f->up || (a->ifa_flags & IFF_UP);
        f->loopback = f->loopback || (a->ifa_flags & IFF_LOOPBACK);
        if (!a->ifa_addr) continue;
        int fam = a->ifa_addr->sa_family;
        if ((fam == AF_INET || fam == AF_INET6) && f->naddr < ZSI_IF_ADDRS) {
            const void *src =
                fam == AF_INET
                    ? (const void *) &((struct sockaddr_in *) (void *) a->ifa_addr)->sin_addr
                    : (const void *) &((struct sockaddr_in6 *) (void *) a->ifa_addr)->sin6_addr;
            if (inet_ntop(fam, src, f->addr[f->naddr], sizeof f->addr[0])) f->naddr++;
        }
#    if defined(__linux__)
        if (fam == AF_PACKET && a->ifa_data) {
            const struct rtnl_link_stats *st = a->ifa_data;
            f->counters = true;
            f->rx_bytes = st->rx_bytes;
            f->tx_bytes = st->tx_bytes;
            f->rx_packets = st->rx_packets;
            f->tx_packets = st->tx_packets;
        }
#    elif defined(__APPLE__)
        if (fam == AF_LINK && a->ifa_data) {
            const struct if_data *st = a->ifa_data;
            f->counters = true;
            f->rx_bytes = st->ifi_ibytes;
            f->tx_bytes = st->ifi_obytes;
            f->rx_packets = st->ifi_ipackets;
            f->tx_packets = st->ifi_opackets;
        }
#    endif
    }
    freeifaddrs(all);
    return 0;
#endif
}
