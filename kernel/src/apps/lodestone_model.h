/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* lodestone_model.h — Lodestone, the inspector for developers, as a model
 * (zxm.h): what this zxvd serves and how fast it answers. The rules are
 * lodestone_rules.h.
 *
 * zxvd methods used: rpc.discover, model.info, net.peers, zxvd.version and
 * features.get, every one read only. Nothing else is called and none is
 * invented; Lodestone changes no setting and switches nothing on. No
 * drawing, no allocation, integers only; the time a call takes comes from
 * the caller's clock (milliseconds), since a model makes no system calls.
 *
 *   LM1 METHODS: rpc.discover's protocol and method names. Each name is
 *       1..LODESTONE_NAME bytes of a-z, 0-9, '_' and '.', else the answer
 *       is not zxvd's (ZXM_E_PROTOCOL) and nothing of it is kept. Up to
 *       LODESTONE_METHODS names are kept (the rest counted); each is filed
 *       under its group (LR1), up to LODESTONE_GROUPS groups, and a method
 *       whose group finds no room is counted as left out. A group's
 *       methods are counted with LR1 (witness.kind_count).
 *   LM2 THE MODEL: model.info's loaded, can_generate, name, arch, n_layers,
 *       n_vocab and status; an answer without loaded and name is not
 *       zxvd's (ZXM_E_PROTOCOL).
 *   LM3 PEERS: net.peers' contacts, up to LODESTONE_CONTACTS kept (the
 *       rest counted). Every contact's NodeID is 64 lower-case hex digits
 *       and every one kept fits its record (LR4) before the list is kept;
 *       otherwise ZXM_E_PROTOCOL and the last list stays. With networking
 *       off zxvd answers an error, passed on as ZXM_E_REMOTE. The money
 *       a contact holds (held, receivable, social) is not Lodestone's and
 *       is not read.
 *   LM4 CAPABILITIES: zxvd.version's app, protocol, libzxv version and
 *       feature mask, and features.get's switches (up to
 *       LODESTONE_FEATURES kept, the rest counted): each one's name, whether it is on, whether it
 *       is regulated, and its methods summed over its groups. Read only:
 *       features.set is the owner's, in Keystone, never Lodestone's.
 *   LM5 PROBES: lodestone_probe times one of LM1-LM4's reads with the
 *       caller's clock: the clock before, the read, the clock after. When
 *       zxvd answered (ZXM_OK, or ZXM_E_REMOTE: an error is an answer too)
 *       the latency is kept in a ring of LODESTONE_SAMPLES; when the ring
 *       is full (head - tail >= LODESTONE_SAMPLES) the oldest is dropped
 *       and counted. A clock that goes back counts the call as 0 ms; a
 *       call past LODESTONE_LATENCY_MAX is no latency (LR2): not kept,
 *       counted as too slow. A transport or protocol failure keeps no
 *       sample.
 *   LM6 LATENCY: over the samples kept, of one probe or of all: their
 *       count, the exact mean (LR2) and the fastest and slowest (LR3,
 *       retort.latency_fastest_ms and latency_slowest_ms). ZXM_E_STATE
 *       when there is no sample. */
#ifndef LODESTONE_MODEL_H
#define LODESTONE_MODEL_H

#include "lodestone_rules.h"
#include "zxm.h"

#define LODESTONE_NAME     47u  /* a method name (zxvd's are far shorter) */
#define LODESTONE_GROUP    23u  /* a group name, a feature's name (features.get's 23) */
#define LODESTONE_TEXT     63u  /* a model's name, arch, status, a version, an address */
#define LODESTONE_NODE     64u  /* a NodeID: 32 bytes as hex */
#define LODESTONE_METHODS  512u /* method names kept */
#define LODESTONE_GROUPS   64u  /* groups kept */
#define LODESTONE_CONTACTS 64u  /* contacts kept */
#define LODESTONE_FEATURES 32u  /* feature switches kept */
#define LODESTONE_SAMPLES  64u  /* latencies kept, a ring */
#define LODESTONE_SUMMARY  383u /* the notice */

/* what a probe times (LM5) */
enum {
    LODESTONE_PROBE_METHODS = 0,  /* rpc.discover */
    LODESTONE_PROBE_MODEL = 1,    /* model.info */
    LODESTONE_PROBE_PEERS = 2,    /* net.peers */
    LODESTONE_PROBE_VERSION = 3,  /* zxvd.version */
    LODESTONE_PROBE_FEATURES = 4, /* features.get */
    LODESTONE_PROBES = 5
};

typedef int64_t (*lodestone_clock_fn)(void *ctx); /* milliseconds, any origin */

typedef struct {
    char name[LODESTONE_GROUP + 1];
    int64_t methods; /* LR1 */
} lodestone_group_t;

typedef struct {
    char id[LODESTONE_NODE + 1];
    char addr[LODESTONE_TEXT + 1]; /* "" for a non-IPv4 address */
    int64_t bucket, seen, pending;
    bool ubh, in_book, member;
} lodestone_contact_t;

typedef struct {
    char name[LODESTONE_GROUP + 1];
    bool enabled, regulated;
    int64_t methods;
} lodestone_feature_t;

typedef struct {
    int64_t probe;
    int64_t ms;
} lodestone_sample_t;

typedef struct {
    int64_t samples;
    int64_t mean_num, mean_den; /* LR2 */
    int64_t fastest, slowest;   /* LR3 */
} lodestone_latency_t;

typedef struct {
    zxm_link_t *link;
    lodestone_clock_fn clock;
    void *clock_ctx;
    /* LM1 */
    bool methods_known;
    char protocol[LODESTONE_TEXT + 1];
    uint32_t methods, methods_kept, methods_left_out;
    char method[LODESTONE_METHODS][LODESTONE_NAME + 1];
    int64_t method_group[LODESTONE_METHODS]; /* 1.. (LR1); 0 when its group found no room */
    uint32_t ngroups;
    lodestone_group_t group[LODESTONE_GROUPS];
    /* LM2 */
    bool model_known, loaded, can_generate;
    char model[LODESTONE_TEXT + 1], arch[LODESTONE_TEXT + 1], status[LODESTONE_TEXT + 1];
    int64_t layers, vocab;
    /* LM3 */
    bool peers_known;
    uint32_t contacts, contacts_kept, members;
    lodestone_contact_t contact[LODESTONE_CONTACTS];
    /* LM4 */
    bool version_known, features_known;
    char app[LODESTONE_TEXT + 1], wire[LODESTONE_TEXT + 1], libzxv[LODESTONE_TEXT + 1];
    int64_t lib_features;
    uint32_t features, features_kept, features_on;
    lodestone_feature_t feature[LODESTONE_FEATURES];
    /* LM5 */
    uint64_t head, tail, dropped, too_slow;
    lodestone_sample_t sample[LODESTONE_SAMPLES];
    char notice[LODESTONE_SUMMARY + 1];
} lodestone_t;

void lodestone_init(lodestone_t *t, zxm_link_t *link, lodestone_clock_fn clock, void *clock_ctx);
/* LM1-LM4: one read each. */
int lodestone_read_methods(lodestone_t *t);
int lodestone_read_model(lodestone_t *t);
int lodestone_read_peers(lodestone_t *t);
int lodestone_read_version(lodestone_t *t);
int lodestone_read_features(lodestone_t *t);
/* LM5: the read's own result; ZXM_E_ARG for no such probe, ZXM_E_STATE
 * with no clock. */
int lodestone_probe(lodestone_t *t, int64_t probe);
/* LM6: probe -1 for every probe; ZXM_E_ARG for no such probe. */
int lodestone_latency(const lodestone_t *t, int64_t probe, lodestone_latency_t *out);
/* LR1: the methods of a group by name, or -1 when the group is not known. */
int64_t lodestone_group_methods(const lodestone_t *t, const char *group);
/* LM1: is this method served (among those kept)? */
bool lodestone_has_method(const lodestone_t *t, const char *name);
/* LR4 on contact i (false when there is none). */
bool lodestone_contact_ok(const lodestone_t *t, uint32_t i);

#endif
