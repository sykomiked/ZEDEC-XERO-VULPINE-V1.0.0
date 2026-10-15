/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* chg_vocab.c — the chiglet's organs and held acts (chg_vocab.h V1-V3). */
#include "chg_vocab.h"

typedef struct {
    const char *name;
    uint8_t witness;
} organ_t;

/* V1: tools/tiamat/tiamat_swarm.json order; witness_from where the roster
 * names one, else the arbiter */
static const organ_t ORGANS[CHGV_ORGANS] = {
    {"voice_of_tiamat", CHGV_O_ARBITER},
    {"herald", CHGV_O_ARBITER},
    {"smith", CHGV_O_WARDEN},
    {"warden", CHGV_O_ARBITER},
    {"scribe", CHGV_O_ARBITER},
    {"reckoner", CHGV_O_ARBITER},
    {"arbiter", CHGV_O_ARBITER},
    {"librarian", CHGV_O_ARBITER},
    {"reader", CHGV_O_ARBITER},
    {"ear", CHGV_O_ARBITER},
    {"voice", CHGV_O_ARBITER},
    {"interpreter", CHGV_O_ARBITER},
    {"augur", CHGV_O_ARBITER},
    {"sentinel", CHGV_O_ARBITER},
    {"painter", CHGV_O_ARBITER},
    {"hand", CHGV_O_ARBITER},
    {"natural_philosopher", CHGV_O_RECKONER},
    {"council", CHGV_O_ARBITER},
    {"bard", CHGV_O_ARBITER},
    {"clerk", CHGV_O_ARBITER},
};

typedef struct {
    const char *name;
    uint8_t layer;
} act_t;

/* V2: index order is the enum's; append only */
static const act_t ACTS[CHGV_NACTS] = {
    {"enrol_key", 0},       {"apply_firmware", 0},     {"pick_boot_slot", 0},
    {"trust_device", 1},    {"deny_device", 1},        {"set_power_profile", 1},
    {"add_rule", 2},        {"set_budget", 2},         {"stop_app", 2},
    {"boot_safe_mode", 2},  {"release_quarantine", 3}, {"move_to_driver_vm", 3},
    {"set_guest_limit", 3}, {"stage_update", 4},       {"draft_audit", 4},
    {"admit_app", 4},       {"refuse_app", 4},         {"draft_policy", 4},
    {"add_to_backup", 4},   {"grant_visit", 4},        {"revoke_visit", 4},
    {"open_link", 4},       {"choose_backend", 5},     {"load_model", 5},
    {"regroup_organs", 5},  {"set_token_budget", 5},   {"write_code", 5},
    {"set_language", 6},    {"set_reading_help", 6},   {"arrange_space", 6},
    {"run_command", 7},     {"apply_edit", 7},         {"apply_fix", 7},
    {"draft_sbom", 7},      {"add_wire", 7},           {"add_block", 7},
    {"compose_app", 7},     {"publish_app", 7},        {"change_part", 7},
    {"draft_packet", 7},    {"file_hcl_report", 7},    {"import_machine", 7},
};

_Static_assert(CHGV_NACTS <= CHGV_ACTS, "acts must fit one uint64_t mask");

/* V3: exact match by length */
static int same(const char *lit, const char *w, size_t n)
{
    size_t i = 0;
    while (i < n && lit[i] && lit[i] == w[i]) i++;
    return i == n && lit[i] == '\0';
}

int chgv_organ_find(const char *name, size_t len)
{
    if (!name || len == 0) return -1;
    for (uint32_t i = 0; i < CHGV_ORGANS; i++)
        if (same(ORGANS[i].name, name, len)) return (int) i;
    return -1;
}

const char *chgv_organ_name(uint32_t organ)
{
    return organ < CHGV_ORGANS ? ORGANS[organ].name : "";
}

int chgv_organ_witness(uint32_t organ)
{
    return organ < CHGV_ORGANS ? (int) ORGANS[organ].witness : -1;
}

int chgv_act_find(const char *name, size_t len)
{
    if (!name || len == 0) return -1;
    for (uint32_t i = 0; i < CHGV_NACTS; i++)
        if (same(ACTS[i].name, name, len)) return (int) i;
    return -1;
}

const char *chgv_act_name(uint32_t act)
{
    return act < CHGV_NACTS ? ACTS[act].name : "";
}

int chgv_act_layer(uint32_t act)
{
    return act < CHGV_NACTS ? (int) ACTS[act].layer : -1;
}
