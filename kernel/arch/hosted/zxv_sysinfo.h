/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_sysinfo.h — facts about the machine and zxvd itself, for the system
 * utilities (Almanac, Tidewatch, Cellar, Sounding: docs/SYSTEM_UTILITIES.md)
 * through zxvd's sys.* methods.
 *
 *   I1 READ ONLY. Every call reads; none changes the machine.
 *   I2 HONEST GAPS. A fact the platform does not give is reported as unknown
 *      (0, -1 or `known = false`), never guessed.
 *   I3 LOCAL. These facts answer the owner on the control socket; nothing
 *      here is sent to peers.
 *
 * macOS, Linux and Windows are covered; anything else reports what POSIX
 * gives. */
#ifndef ZXV_SYSINFO_H
#define ZXV_SYSINFO_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char os[16];          /* "macos", "linux", "windows" or "other" */
    char os_version[64];  /* the kernel release or Windows version */
    char arch[16];        /* "x86_64", "arm64", ... */
    char host[64];        /* the machine's name */
    uint32_t cpus;        /* online logical CPUs, 0 = unknown */
    uint64_t mem_total;   /* bytes, 0 = unknown */
    uint64_t mem_avail;   /* bytes the system can give without swapping, 0 = unknown */
    uint64_t uptime_s;    /* since the machine started, 0 = unknown */
    int32_t load_x100[3]; /* 1, 5 and 15 minute load averages x100; -1 = unknown */
} zsi_host_t;

typedef struct {
    uint64_t cpu_user_us, cpu_sys_us; /* CPU time zxvd has used */
    uint64_t rss_bytes;               /* memory resident now, 0 = unknown */
    uint64_t max_rss_bytes;           /* the most it has held, 0 = unknown */
    uint64_t pid;
} zsi_self_t;

typedef struct {
    uint64_t total, free, avail; /* bytes; avail = what this user may write */
} zsi_disk_t;

#define ZSI_IF_ADDRS 8u
typedef struct {
    char name[40];
    bool up, loopback;
    char addr[ZSI_IF_ADDRS][48]; /* IPv4 and IPv6, as text */
    uint32_t naddr;
    bool counters; /* false: the platform gave no byte counts */
    uint64_t rx_bytes, tx_bytes, rx_packets, tx_packets;
} zsi_if_t;

int zsi_host(zsi_host_t *h); /* 0, or -1 when nothing could be read */
int zsi_self(zsi_self_t *s);
int zsi_disk(const char *path, zsi_disk_t *d); /* the volume holding path */
/* Up to cap interfaces, in the order the system lists them; *n = how many.
 * Returns -1 when the platform has no listing here (*n = 0). */
int zsi_interfaces(zsi_if_t *out, uint32_t cap, uint32_t *n);

#endif
