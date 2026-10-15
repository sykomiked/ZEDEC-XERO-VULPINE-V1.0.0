/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* chg_vocab.h — the chiglet's shared vocabulary: the Tiamat organs it asks
 * and the held acts it may propose at each layer of the stack.
 *
 * The chiglet is the way a person meets every layer (zxv-docs/
 * CHIGLET_LAYER_MAP.md). At each layer it reads, an organ answers, a witness
 * checks, and anything that would change the machine is a held act the
 * person accepts or rejects. Three places must agree on the names: the
 * intent layer (an "act <verb> <object>" proposal, intent.h I7), app
 * manifests (a "chiglet <organ> <acts>" line, zxapp.h H1) and the organ
 * router (chg_organ.h). This table is that one source.
 *
 *   V1 ORGANS. The 20 organs of tools/tiamat/tiamat_swarm.json, in its
 *      order. Each has a job and a WITNESS ORGAN: the organ whose cells
 *      check its answers. smith is witnessed by warden, reckoner by
 *      arbiter, natural_philosopher by reckoner (the roster's witness_from);
 *      every other organ by the arbiter, the swarm's verifier.
 *   V2 ACTS. A fixed list of verbs, each tagged with the stack layer it
 *      changes (L0 firmware .. L7 apps, zxv-docs/APP_ARCHITECTURE_407.md
 *      section 2). An act is always held: proposing one changes nothing.
 *      At most CHGV_ACTS (64) so a set of acts fits one uint64_t mask, bit
 *      i for act i. Indexes are part of the format: new acts are appended,
 *      never inserted or renumbered.
 *   V3 NAMES. Lower case [a-z_], matched exactly by length; anything else
 *      is not found (-1). No guessing, no prefixes.
 *
 * Freestanding C11: no libc, no allocation, no float.
 */
#ifndef ZXV_CHG_VOCAB_H
#define ZXV_CHG_VOCAB_H

#include <stddef.h>
#include <stdint.h>

#define CHGV_ORGANS 20u
#define CHGV_ACTS   64u /* the mask width; the table uses CHGV_NACTS of them */

enum {
    CHGV_O_VOICE_OF_TIAMAT = 0,
    CHGV_O_HERALD,
    CHGV_O_SMITH,
    CHGV_O_WARDEN,
    CHGV_O_SCRIBE,
    CHGV_O_RECKONER,
    CHGV_O_ARBITER,
    CHGV_O_LIBRARIAN,
    CHGV_O_READER,
    CHGV_O_EAR,
    CHGV_O_VOICE,
    CHGV_O_INTERPRETER,
    CHGV_O_AUGUR,
    CHGV_O_SENTINEL,
    CHGV_O_PAINTER,
    CHGV_O_HAND,
    CHGV_O_NATURAL_PHILOSOPHER,
    CHGV_O_COUNCIL,
    CHGV_O_BARD,
    CHGV_O_CLERK
};

/* V2: the acts, by layer */
enum {
    /* L0 silicon and firmware */
    CHGV_A_ENROL_KEY = 0,
    CHGV_A_APPLY_FIRMWARE,
    CHGV_A_PICK_BOOT_SLOT,
    /* L1 hardware interface */
    CHGV_A_TRUST_DEVICE,
    CHGV_A_DENY_DEVICE,
    CHGV_A_SET_POWER_PROFILE,
    /* L2 kernel */
    CHGV_A_ADD_RULE,
    CHGV_A_SET_BUDGET,
    CHGV_A_STOP_APP,
    CHGV_A_BOOT_SAFE_MODE,
    /* L3 drivers */
    CHGV_A_RELEASE_QUARANTINE,
    CHGV_A_MOVE_TO_DRIVER_VM,
    CHGV_A_SET_GUEST_LIMIT,
    /* L4 system services */
    CHGV_A_STAGE_UPDATE,
    CHGV_A_DRAFT_AUDIT,
    CHGV_A_ADMIT_APP,
    CHGV_A_REFUSE_APP,
    CHGV_A_DRAFT_POLICY,
    CHGV_A_ADD_TO_BACKUP,
    CHGV_A_GRANT_VISIT,
    CHGV_A_REVOKE_VISIT,
    CHGV_A_OPEN_LINK,
    /* L5 runtimes */
    CHGV_A_CHOOSE_BACKEND,
    CHGV_A_LOAD_MODEL,
    CHGV_A_REGROUP_ORGANS,
    CHGV_A_SET_TOKEN_BUDGET,
    CHGV_A_WRITE_CODE,
    /* L6 GUI */
    CHGV_A_SET_LANGUAGE,
    CHGV_A_SET_READING_HELP,
    CHGV_A_ARRANGE_SPACE,
    /* L7 apps and tools */
    CHGV_A_RUN_COMMAND,
    CHGV_A_APPLY_EDIT,
    CHGV_A_APPLY_FIX,
    CHGV_A_DRAFT_SBOM,
    CHGV_A_ADD_WIRE,
    CHGV_A_ADD_BLOCK,
    CHGV_A_COMPOSE_APP,
    CHGV_A_PUBLISH_APP,
    CHGV_A_CHANGE_PART,
    CHGV_A_DRAFT_PACKET,
    CHGV_A_FILE_HCL_REPORT,
    CHGV_A_IMPORT_MACHINE,
    CHGV_NACTS
};

/* V1/V3: the organ's index, or -1 */
int chgv_organ_find(const char *name, size_t len);
/* its name ("" when out of range) */
const char *chgv_organ_name(uint32_t organ);
/* V1: the organ whose cells witness this one's answers (-1 out of range) */
int chgv_organ_witness(uint32_t organ);

/* V2/V3: the act's index, or -1 */
int chgv_act_find(const char *name, size_t len);
/* its name ("" when out of range) */
const char *chgv_act_name(uint32_t act);
/* V2: the stack layer 0..7 it changes, or -1 out of range */
int chgv_act_layer(uint32_t act);

#endif /* ZXV_CHG_VOCAB_H */
