/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_caps.h — which apps may use which capabilities, behind Gatehouse
 * (caps.*). The names are the manifest's (kernel/src/apps/zxapp.h R3:
 * "needs fs.read", "needs notify", ...).
 *
 *   K1 GRANTS. The owner's answer for one app and one capability: "always",
 *      "once" (the next use only) or "never". With no answer the default
 *      (K2) applies; caps.revoke with forget takes an answer back.
 *   K2 DEFAULT. An app may use a capability its manifest declares, unless
 *      the capability is SENSITIVE (zcp_sensitive: camera, mic, location,
 *      shell, ledger.post, journal.write, caps, proc.control): those need the owner's
 *      "always" or "once" first. An app that asks for one without an answer
 *      goes on the list of pending asks (one entry per app and capability)
 *      and gets nothing until the owner answers. Checking that the manifest
 *      declares the capability is the app runtime's part (zxm.h's gate);
 *      zxvd only keeps the owner's answers.
 *   K3 LOCATION. "location" is kept by place.* (zxv_place.h P2), so an app
 *      has one answer for it: caps.grant / caps.revoke set place.permit's,
 *      zcp_check asks through zpl_ask (a "once" is used up there), and
 *      zcp_list shows place's apps.
 *   K4 THE FILE. One text file, 0600, rewritten whole and swapped in place,
 *      ending in the SHA-256 of what is above it. Only lasting answers
 *      (always, never) are written; pending asks and "once" last for this
 *      run. A damaged file is moved aside as "<file>.damaged" and the
 *      answers start empty (zcp_was_reset).
 *
 * zxvd uses one thread (zxvd_log.h L3); so does this. */
#ifndef ZXV_CAPS_H
#define ZXV_CAPS_H

#include <stdbool.h>
#include <stdint.h>

#define ZCP_APP_MAX  31u /* bytes of an app's name (as zxv_place.h's) */
#define ZCP_CAP_MAX  23u /* bytes of a capability: a-z 0-9 and . */
#define ZCP_GRANTS   512u
#define ZCP_PATH_MAX 900u

enum {
    ZCP_OK = 0,         /* allowed */
    ZCP_E_ARG = -1,     /* a bad app, capability or mode */
    ZCP_E_FULL = -2,    /* no room for another answer */
    ZCP_E_IO = -3,      /* the file could not be written */
    ZCP_E_DENIED = -4,  /* the owner said never */
    ZCP_E_PENDING = -5, /* waiting for the owner's answer (K2) */
    ZCP_E_NOTFOUND = -6 /* no answer to forget */
};

typedef enum { ZCP_DEFAULT, ZCP_ONCE, ZCP_ALWAYS, ZCP_NEVER } zcp_mode_t;

typedef struct {
    char app[ZCP_APP_MAX + 1];
    char cap[ZCP_CAP_MAX + 1];
    uint8_t mode;  /* zcp_mode_t */
    bool pending;  /* asked with no answer yet (K2) */
    int64_t asked; /* when it last asked, seconds since 1970; 0 = never */
} zcp_grant_t;

/* Load the answers file; NULL or "" keeps them in memory. 0, or ZCP_E_ARG /
 * ZCP_E_IO; a damaged file is moved aside and 0 returned (zcp_was_reset). */
int zcp_open(const char *path);
void zcp_close(void);
bool zcp_was_reset(void);
void zcp_set_clock(int64_t (*now)(void)); /* tests; NULL restores time() */

bool zcp_cap_ok(const char *cap); /* the name's form (K1), not whether it is known */
bool zcp_sensitive(const char *cap);

/* K2/K3: may `app` use `cap` now? ZCP_OK, ZCP_E_PENDING or ZCP_E_DENIED
 * (or ZCP_E_ARG / ZCP_E_FULL). A "once" is used up. */
int zcp_check(const char *app, const char *cap);
/* K1: the owner's answer; ZCP_DEFAULT forgets it (ZCP_E_NOTFOUND when
 * there was none). Answering clears a pending ask. */
int zcp_set(const char *app, const char *cap, zcp_mode_t mode);

/* Every answer and pending ask, location's included (K3), in the order
 * they were made; `app` NULL or "" for all apps. Up to cap rows; the
 * count that matched. */
uint32_t zcp_list(const char *app, zcp_grant_t *out, uint32_t cap);

const char *zcp_mode_name(zcp_mode_t m); /* "default", "once", "always", "never" */
int zcp_mode_parse(const char *s);       /* -1 when unknown */
const char *zcp_strerror(int code);

#endif
