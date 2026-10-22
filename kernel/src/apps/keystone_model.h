/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* keystone_model.h — Keystone, Settings, as a model (zxm.h): the settings
 * zxvd keeps and can say, read and set only through methods it has. The
 * rules are keystone_rules.h (KS1-KS3); the model keeps a copy for the
 * view and writes nothing of its own.
 *
 *   KM1 READ, NEVER GUESSED. keystone_refresh reads the machine (sys.info:
 *       its processors and memory), the Hoard (files.root: where it is and
 *       the largest file it takes), the model slot (model.info: whether a
 *       model is loaded and its name) and the interface (ui.get: simple
 *       mode). A zxvd that does not serve ui.get (-32601) leaves simple
 *       mode unknown; any other refusal, or an answer out of bounds,
 *       changes nothing (ZXM_E_PROTOCOL for a bad answer).
 *   KM2 SIMPLE MODE. keystone_set_simple sends ui.set and keeps the answer
 *       only when it echoes what was asked. zxvd asks no confirmation for
 *       it (ui.set H8: it changes only what the interfaces show), but it is
 *       saved, so the model sends it only on the owner's word (the toggle
 *       the owner moved); without it ZXM_E_STATE and nothing is sent.
 *   KM3 BUDGET SHARES. keystone_propose splits the device's energy budget
 *       (whole millijoules over window ms) across up to KEYSTONE_APPS apps
 *       by the owner's weights with Crown's largest-remainder split
 *       (crown_allot, KS3), checks every share against the budget limits
 *       (keystone_energy_budget_ok, KS2) and holds the proposal with a
 *       notice. It sends nothing: a share of 0, a weight or a name out of
 *       bounds, or a duplicate name refuses the whole proposal.
 *   KM4 THE OWNER CONFIRMS. keystone_apply sends power.budget for each held
 *       app (max_uj = share * 1000) and then for the device, only with the
 *       owner's confirmation, and only the held proposal; the word is spent
 *       by one apply. applied counts the budgets zxvd took; on a refusal
 *       the rest are not sent and the proposal stays held for another try
 *       or a cancel. keystone_clear sends a window of 0 for one app, also
 *       only with the owner's word. keystone_cancel drops the proposal.
 *
 * Settings zxvd has no method for (brightness, volume, text size, the
 * keyboard, the language in use, the network, sharing, choosing a model)
 * are not kept here: KS1 is the rule a later layer checks them with.
 * No drawing, no allocation, integers only. */
#ifndef KEYSTONE_MODEL_H
#define KEYSTONE_MODEL_H

#include "crown_rules.h"
#include "keystone_rules.h"
#include "zxm.h"

#define KEYSTONE_APPS       16u /* apps in one proposal */
#define KEYSTONE_APP_NAME   31u /* power.budget's executable name */
#define KEYSTONE_ROOT_MAX   255u
#define KEYSTONE_MODEL_NAME 63u
#define KEYSTONE_NOTICE_MAX 255u

typedef struct {
    char app[KEYSTONE_APP_NAME + 1];
    int64_t weight; /* 0..CROWN_NINE */
    int64_t share;  /* millijoules in a window (KS3) */
} keystone_share_t;

typedef struct {
    bool pending;
    int64_t window; /* ms */
    int64_t whole;  /* the device's millijoules in a window */
    uint32_t n;
    keystone_share_t app[KEYSTONE_APPS];
} keystone_proposal_t;

typedef struct {
    zxm_link_t *link;
    bool have; /* a refresh read everything */
    uint32_t cpus;
    uint64_t mem_total; /* bytes */
    char root[KEYSTONE_ROOT_MAX + 1];
    uint64_t file_max; /* the largest file the Hoard takes, bytes */
    bool model_loaded;
    char model_name[KEYSTONE_MODEL_NAME + 1];
    bool simple_known, simple_mode;
    keystone_proposal_t held; /* KM3 */
    uint32_t applied;         /* KM4: budgets zxvd took in the last apply */
    char notice[KEYSTONE_NOTICE_MAX + 1];
} keystone_t;

void keystone_init(keystone_t *k, zxm_link_t *link);

/* KM1 */
int keystone_refresh(keystone_t *k);
/* KM2 */
int keystone_set_simple(keystone_t *k, bool on, bool owner_confirmed);
/* KM3: n apps (1..KEYSTONE_APPS), each a name of 1..KEYSTONE_APP_NAME
 * printable bytes, no spaces, and a weight. */
int keystone_propose(keystone_t *k, int64_t window, int64_t whole, const char *const *apps,
                     const int64_t *weight, uint32_t n);
/* KM4 */
int keystone_apply(keystone_t *k, bool owner_confirmed);
int keystone_clear(keystone_t *k, const char *app, bool owner_confirmed);
void keystone_cancel(keystone_t *k);

#endif
