/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gatehouse_model.h — Gatehouse, permissions and privacy, as a model
 * (zxm.h): which app asked for which capability, what the owner granted or
 * revoked, and how many apps may use each one. The rules are
 * gatehouse_rules.h (GR1-GR4), the same ones the app's Eve, KRON, C++ and
 * Trebago blocks run. No drawing, no allocation, integers only.
 *
 *   GH1 ASKED is what an app's manifest needs (zxapp_needs): the view reads
 *       each manifest and tells the model with gatehouse_ask. An app that
 *       asks for location through zxvd (place.ask) is asked too.
 *   GH2 GRANTED and REVOKED change only on the owner's confirmed word
 *       (confirmed true): Gatehouse advises, the owner decides. A
 *       capability nobody asked for is never granted. Revoking keeps the
 *       grant on record, so a use is allowed again only when the owner
 *       grants it again (which clears the revoke).
 *   GH3 LOCATION is kept by zxvd (zxv_place.h P2): a refresh reads every
 *       app's answer (place.apps): always or once is granted, never is
 *       revoked, ask is neither; a waiting ask is asked. A grant or revoke
 *       of location is the owner's answer sent to zxvd (place.permit
 *       always or never) and changes the model only once zxvd took it. zxvd
 *       has no method for any other capability, so the model keeps those.
 *   GH4 A REFRESH reads zxvd's clock (sys.info time_ms, for when each grant
 *       changed) and place.apps, checks every value before it changes
 *       anything (a reply out of bounds leaves what was shown as it was),
 *       then counts the holders of each capability (GR4) and the asks that
 *       wait for the owner, and composes the notice for them. zxvd has no
 *       method that posts a notice, so the model only composes its text. */
#ifndef GATEHOUSE_MODEL_H
#define GATEHOUSE_MODEL_H

#include "gatehouse_rules.h"
#include "zxm.h"

#define GATEHOUSE_APPS       64u  /* zxv_place.h ZPL_APPS */
#define GATEHOUSE_APP_MAX    31u  /* zxv_place.h ZPL_APP_MAX */
#define GATEHOUSE_NOTICE_MAX 480u /* bytes of the notice */
#define GATEHOUSE_LOCATION   2    /* GR1: "location" */

typedef struct {
    char app[GATEHOUSE_APP_MAX + 1];
    uint32_t asked;     /* one bit per capability (GR1) */
    uint32_t granted;   /* granted by the owner (GH2) */
    uint32_t revoked;   /* revoked by the owner (GH2) */
    int64_t changed_ms; /* zxvd's clock at the owner's last change; 0 none */
} gatehouse_app_t;

typedef struct {
    zxm_link_t *link;
    gatehouse_app_t app[GATEHOUSE_APPS];
    uint32_t napps;
    int64_t now_ms;                    /* zxvd's clock at the last refresh */
    int64_t holders[GATEHOUSE_CAPS];   /* apps allowed each capability (GR4) */
    uint32_t waiting;                  /* asks with no grant and no revoke */
    char notice[GATEHOUSE_NOTICE_MAX]; /* "" when nothing waits */
} gatehouse_t;

void gatehouse_init(gatehouse_t *g, zxm_link_t *link);
/* GH1: app asks for cap (a name of GR1). Sends nothing. ZXM_E_ARG for a bad
 * app name or an unknown capability, ZXM_E_TOOBIG when no app fits. */
int gatehouse_ask(gatehouse_t *g, const char *app, const char *cap);
/* GH2/GH3: the owner's confirmed grant or revoke. ZXM_E_ARG for a bad name,
 * ZXM_E_STATE when not confirmed, or (grant) when the app never asked. */
int gatehouse_grant(gatehouse_t *g, const char *app, const char *cap, bool confirmed);
int gatehouse_revoke(gatehouse_t *g, const char *app, const char *cap, bool confirmed);
/* GH4 */
int gatehouse_refresh(gatehouse_t *g);
/* GR3 for one app and capability; false for an app or capability unknown */
bool gatehouse_allowed(const gatehouse_t *g, const char *app, const char *cap);
/* GR4 for one capability, as the last refresh or change counted it; -1 for
 * an unknown capability */
int64_t gatehouse_count(const gatehouse_t *g, const char *cap);
const gatehouse_app_t *gatehouse_find(const gatehouse_t *g, const char *app);

#endif
