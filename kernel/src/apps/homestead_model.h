/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* homestead_model.h — Homestead, the owner's home devices and scenes, as a
 * model (zxm.h): lights, plugs, blinds, locks, heaters and ovens with
 * their ranges and setpoints, scenes that set several at once, each
 * device's energy in watt-hours, and whether the ZXV node bridging a
 * device is reachable.
 *
 * zxvd has no home-device method (devmesh.* is the owner's computers and
 * phones), and none is used: a command the rules allow is kept as the
 * device's setpoint in HOMESTEAD_FILE, for a device bridge to carry out.
 * The file is one UTF-8 text file in the Banker, read with files.read and
 * written whole with files.write; reachability is net.peers. zxvd has no
 * notification method either, so the model only composes each notice's
 * text (homestead_t.notice) for the view to show. No drawing, no
 * allocation, integers only; the rules are homestead_rules.h.
 *
 *   HM1 The file's first line is "homestead 1".
 *   HM2 Then one line per device, "device <id> <kind> <lo> <hi>
 *       <setpoint> <node> <name>", ids rising: a record HR5 allows, lo at
 *       most hi and the setpoint inside lo..hi; node is "-" or the bridging
 *       node's NodeID (64 lower-case hex digits); the name is 1 to
 *       HOMESTEAD_NAME_MAX bytes with no control byte, to the end of the
 *       line.
 *   HM3 Then one line per scene step, "step <scene> <device> <setpoint>",
 *       scenes rising and devices rising inside a scene: scene
 *       HOMESTEAD_SCENE_FIRST..HOMESTEAD_SCENE_MAX, a kept device, the
 *       setpoint inside its range.
 *   HM4 Then one line per energy reading, "use <device> <wh>", in the
 *       order added: a kept device, 0..HOMESTEAD_WH_MAX watt-hours.
 *   HM5 Every line ends with "\n", numbers are plain decimal digits, a
 *       minus sign only before a number other than 0, no leading zero.
 *       Anything else is not Homestead's file: the load is refused
 *       (ZXM_E_PROTOCOL) and every change is refused (ZXM_E_STATE) until a
 *       load succeeds, so a file the model cannot read is never
 *       overwritten. Changes are checked, written whole, and only then
 *       kept: a write zxvd refuses leaves the model as it was.
 *   HM6 A COMMAND sets one device's setpoint when device_cmd_ok (HR2)
 *       allows it: inside the device's range, and for a lock, a heater or
 *       an oven only with warren.approval_ok's yes (the owner's session
 *       and the owner's approval). Without it nothing changes and the
 *       answer is HOMESTEAD_OWNER.
 *   HM7 AN AI'S SUGGESTION is never run: it is held (HOMESTEAD_HELD) until
 *       the owner accepts it, and then runs as the owner's command, under
 *       HM6 like any other; the owner may reject it.
 *   HM8 A SCENE runs whole or not at all: every step passes HM6 first, and
 *       then every setpoint is written in one change.
 *   HM9 A device's energy is device_energy (HR4) over the readings. */
#ifndef HOMESTEAD_MODEL_H
#define HOMESTEAD_MODEL_H

#include "homestead_rules.h"
#include "zxm.h"

#define HOMESTEAD_FILE       "Homestead.home" /* in the Banker's home */
#define HOMESTEAD_DEVICES    64u
#define HOMESTEAD_STEPS      256u
#define HOMESTEAD_READINGS   1024u
#define HOMESTEAD_NAME_MAX   63u
#define HOMESTEAD_NODE_HEX   64u       /* a NodeID (net.peers) */
#define HOMESTEAD_WH_MAX     999999999 /* one reading, nine digits */
#define HOMESTEAD_TEXT_MAX   40960u    /* the whole file, every line full */
#define HOMESTEAD_NOTICE_MAX 511u
#define HOMESTEAD_E_FILES    (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */

/* What a command came to, beside ZXM_OK (it ran) and the ZXM_E_* codes. */
enum {
    HOMESTEAD_OWNER = 1, /* HM6: waits for the owner's approval; nothing changed */
    HOMESTEAD_HELD = 2,  /* HM7: an AI's suggestion, held for the owner */
};

typedef struct {
    int32_t id;   /* 1..HOMESTEAD_DEVICE_MAX */
    int32_t kind; /* HOMESTEAD_LIGHT..HOMESTEAD_OVEN */
    int64_t lo, hi, setpoint;
    char node[HOMESTEAD_NODE_HEX + 1]; /* "" when no node bridges it */
    char name[HOMESTEAD_NAME_MAX + 1];
    bool reachable; /* its node was in net.peers at the last homestead_bridges */
} homestead_device_t;

typedef struct {
    int32_t scene, device;
    int64_t setpoint;
} homestead_step_t;

typedef struct {
    int32_t device;
    int64_t wh;
} homestead_reading_t;

typedef struct {
    bool pending;
    int32_t device;
    int64_t setpoint;
} homestead_suggestion_t;

typedef struct {
    zxm_link_t *link;
    bool loaded; /* a load succeeded: changes may be written (HM5) */
    homestead_device_t dev[HOMESTEAD_DEVICES];
    uint32_t ndev;
    homestead_step_t step[HOMESTEAD_STEPS];
    uint32_t nstep;
    homestead_reading_t use[HOMESTEAD_READINGS];
    uint32_t nuse;
    homestead_suggestion_t held; /* HM7: at most one waits */
    char notice[HOMESTEAD_NOTICE_MAX + 1];
} homestead_t;

void homestead_init(homestead_t *h, zxm_link_t *link);
/* Read HOMESTEAD_FILE; a file not made yet is an empty home (ZXM_OK). */
int homestead_load(homestead_t *h);
const homestead_device_t *homestead_find(const homestead_t *h, int64_t id);

/* Keep a new device (HM2): ZXM_E_ARG when the record does not fit, lo is
 * above hi, the setpoint is outside lo..hi, the node is neither NULL, ""
 * nor a NodeID, the name does not fit, or the id is kept already. */
int homestead_add_device(homestead_t *h, int64_t id, int64_t kind, int64_t lo, int64_t hi,
                         int64_t setpoint, const char *node, const char *name);
/* Remove a device with its scene steps and readings. */
int homestead_remove_device(homestead_t *h, int64_t id);

/* HM6: the owner's or a person's command. owner and approved are
 * warren.approval_ok's flags (1 the owner's session, 1 the owner said yes). */
int homestead_command(homestead_t *h, int64_t id, int64_t setpoint, int64_t owner,
                      int64_t approved);
/* HM7: an AI's suggestion, held (HOMESTEAD_HELD); ZXM_E_ARG when it names
 * no kept device or a setpoint outside its range. A new one replaces the
 * one waiting. */
int homestead_suggest(homestead_t *h, int64_t id, int64_t setpoint);
/* The owner's word on the suggestion waiting: accept runs it as a command
 * (HM6, so a lock still needs approved 1); reject drops it. ZXM_E_STATE
 * when none waits. */
int homestead_accept(homestead_t *h, int64_t owner, int64_t approved);
int homestead_reject(homestead_t *h);

/* HM3: a scene step, kept or replaced. */
int homestead_set_step(homestead_t *h, int64_t scene, int64_t device, int64_t setpoint);
int homestead_remove_scene(homestead_t *h, int64_t scene);
/* HM8: run every step of a scene, or none. */
int homestead_run_scene(homestead_t *h, int64_t scene, int64_t owner, int64_t approved);

/* HM4 and HM9. */
int homestead_add_reading(homestead_t *h, int64_t device, int64_t wh);
int homestead_device_energy(const homestead_t *h, int64_t device, int64_t *total);

/* Which bridging nodes are reachable (net.peers): sets each device's
 * reachable and *count (the devices whose node is reachable). */
int homestead_bridges(homestead_t *h, uint32_t *count);

#endif
