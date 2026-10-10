/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_update_host.h — the hosted app's update checker: kernel/src/update
 * (zx_upcheck: signed manifests in IPFS update buckets) over a trustless
 * gateway, reported to the window and the notification bus.
 *
 *   C1 NO SURPRISE TRAFFIC. Nothing is fetched at start-up and nothing is
 *      fetched in OFF or LAN mode unless the user presses "Check for updates"
 *      (POST /api/update). Automatic checks run only while networking is
 *      ONLINE, at most once a day. The checker's own master switch
 *      (zxu_config_t.enabled) is on only for the duration of a check.
 *   C2 TRANSPORT. The gateway GET runs `curl` (fork + execvp with an argv
 *      array, never a shell; HTTPS_PROXY is honoured by curl). The URL is
 *      built by ipfs_node from the gateway base and a CID, and is refused
 *      unless every character is in a fixed URL-safe set. Every byte that
 *      comes back is re-hashed against its CID by ipfs_node, so a lying
 *      gateway is caught. Windows: not wired (no curl spawn yet); a check
 *      reports that plainly.
 *   C3 TRUST. No release key is trusted by default, so the built-in bucket
 *      reports UNSIGNED (never installable) until the user trusts one. This
 *      file only checks and reports; it installs nothing.
 *   C4 BLOCKING. A check runs on the app's one thread (curl --max-time 20 per
 *      request), so the window waits while it runs.
 */
#ifndef ZXV_UPDATE_HOST_H
#define ZXV_UPDATE_HOST_H

#include <stdbool.h>
#include <stdint.h>

#include "zx_notify.h"

#define ZXV_UPDATE_GATEWAY      "https://trustless-gateway.link"
#define ZXV_UPDATE_AUTO_SECONDS 86400u

typedef struct {
    bool checked;     /* at least one check ran */
    bool installable; /* a signed update for this system was found */
    bool auto_on;     /* automatic checks are allowed now (ONLINE) */
    char status[40];  /* zxu_status_str of the built-in bucket, or "not checked" */
    char title[64], body[192];
    char gateway[128];
    uint32_t checks, requests;
    uint64_t last_check; /* Unix seconds, 0 = never */
    char error[96];
} zxv_update_status_t;

/* Set up (no network). gateway NULL = ZXV_UPDATE_GATEWAY; it must be https://,
 * or http:// to 127.0.0.1 / localhost (a local IPFS node). bus may be NULL.
 * installed is the app's packed version (ZXU_VERSION). 0 or -1. */
int zxv_update_init(const char *gateway, zxn_bus_t *bus, uint32_t installed);
/* The user asked: check every enabled bucket now. Returns the built-in
 * bucket's zxu_status_t, or -1 if the check could not run. */
int zxv_update_check_now(uint64_t now_unix);
/* C1: call often; checks only when online and a day has passed. */
void zxv_update_tick(uint64_t now_unix, bool online);
void zxv_update_status(zxv_update_status_t *st);
void zxv_update_free(void);

#endif /* ZXV_UPDATE_HOST_H */
