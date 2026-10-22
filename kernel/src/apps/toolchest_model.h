/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* toolchest_model.h — Toolchest, developer tools, as a model (zxm.h): the
 * SDK kits and language SDKs, what this zxvd offers them, and a kit's
 * release judged before the owner's word on it. The rules are
 * toolchest_rules.h.
 *
 * zxvd methods used: zxvd.version, rpc.discover, play.catalogue,
 * update.check and sys.disk. zxvd has no method that lists kits or their
 * releases, nor one that installs, lists or removes a kit: none is
 * invented, and nothing is ever installed. No drawing, no allocation,
 * integers only.
 *
 *   TM1 THE DAEMON: zxvd.version's libzxv ABI, X.Y.Z with each part 0..999
 *       and no leading zeros, is the version every kit is judged against
 *       (the kits are built on libzxv's C ABI, sdk/kits/README.md). An ABI
 *       in any other form is not zxvd's answer (ZXM_E_PROTOCOL). The app
 *       version is kept as text. rpc.discover's methods are counted, and
 *       whether fab.receive is among them, the only way a recipe arrives.
 *   TM2 SAMPLES: play.catalogue's templates, up to TOOLCHEST_SAMPLES of
 *       them kept (id and language, bounded), the rest counted.
 *   TM3 ROOM: sys.disk's total and avail (what the owner may still write)
 *       on the Hoard's volume. An update check (update.check) is one
 *       signed check; its status text is kept, nothing is installed.
 *   TM4 AN OFFER is a kit of the catalogue with its release: its version,
 *       the ABI it needs, its recipe's block sizes and its files' sizes,
 *       and the result of verifying each of its two signatures (1 yes, 0
 *       no). Its sizes are TC3 and TC4, need = download + installed, room
 *       = avail - need; it fits by TC1 and is allowed by TC2. Allowed, it
 *       waits for the owner's word (toolchest_confirm), who approves or
 *       declines it once; zxvd has no kit-install method, so approving
 *       records the word and installs nothing. Refused, it says why, the
 *       first that applies: unsigned, does not fit, no room.
 *   TM5 THE KITS: one record per catalogue kit, the last offer's, each
 *       checked to fit ^ZXTOOLCHEST (TC5) before it is kept. */
#ifndef TOOLCHEST_MODEL_H
#define TOOLCHEST_MODEL_H

#include "toolchest_rules.h"
#include "zxm.h"

#define TOOLCHEST_TEXT    63u  /* a version, status or sample id */
#define TOOLCHEST_SAMPLES 64u  /* samples kept */
#define TOOLCHEST_SUMMARY 383u /* the notice */
#define TOOLCHEST_KITS    32u  /* records, one per catalogue kit at most */

/* a kit's language (TC5's lang): libzxv's C and each language SDK in sdk/ */
enum {
    TOOLCHEST_LANG_C = 0,
    TOOLCHEST_LANG_CPP = 1,
    TOOLCHEST_LANG_RUST = 2,
    TOOLCHEST_LANG_GO = 3,
    TOOLCHEST_LANG_PYTHON = 4,
    TOOLCHEST_LANG_JAVA = 5,
    TOOLCHEST_LANG_TYPESCRIPT = 6,
    TOOLCHEST_LANG_SWIFT = 7,
    TOOLCHEST_LANG_RUBY = 8,
    TOOLCHEST_LANG_PHP = 9,
    TOOLCHEST_LANG_DOTNET = 10,
    TOOLCHEST_LANG_SOLIDITY = 11,
    TOOLCHEST_LANGS = 12
};

/* an offer's outcome (TM4) */
enum {
    TOOLCHEST_NONE = 0,
    TOOLCHEST_ALLOWED = 1,  /* waits for the owner */
    TOOLCHEST_UNSIGNED = 2, /* refused: not both signatures verified */
    TOOLCHEST_NO_FIT = 3,   /* refused: needs an ABI this zxvd does not run */
    TOOLCHEST_NO_ROOM = 4,  /* refused: more bytes than the owner may write */
    TOOLCHEST_APPROVED = 5, /* the owner's yes: recorded, nothing installed */
    TOOLCHEST_DECLINED = 6  /* the owner's no */
};

typedef struct {
    int64_t major, minor, patch;
} toolchest_version_t;

/* one kit of the catalogue: sdk/kits and the language SDKs */
typedef struct {
    const char *name;
    const char *folder; /* under sdk/ */
    int64_t lang;
} toolchest_entry_t;

typedef struct {
    bool known;              /* an offer was judged */
    toolchest_version_t ver; /* the release's own version */
    toolchest_version_t abi; /* the libzxv ABI it needs */
    int64_t download, installed, need, room;
    int64_t firstsig, secondsig;
    int64_t fits;
    int64_t outcome;
    int64_t used_after_x100; /* TC6, TOOLCHEST_REFUSED when it would not fit */
} toolchest_kit_t;

typedef struct {
    char id[TOOLCHEST_TEXT + 1];
    char lang[TOOLCHEST_TEXT + 1];
} toolchest_sample_t;

typedef struct {
    zxm_link_t *link;
    /* TM1 */
    bool known;
    toolchest_version_t abi;
    char app[TOOLCHEST_TEXT + 1];
    uint32_t methods;
    bool fab_receive;
    /* TM2 */
    uint32_t samples; /* all of play.catalogue's */
    uint32_t kept;    /* of them in sample[] */
    toolchest_sample_t sample[TOOLCHEST_SAMPLES];
    /* TM3 */
    bool disk_known;
    int64_t disk_total, disk_avail;
    char status[TOOLCHEST_TEXT + 1];
    /* TM4, TM5 */
    toolchest_kit_t kit[TOOLCHEST_KITS];
    int64_t pending; /* the catalogue index waiting for the owner, -1 none */
    char notice[TOOLCHEST_SUMMARY + 1];
} toolchest_t;

/* the catalogue: its size and entry i (NULL past the end) */
uint32_t toolchest_catalogue_n(void);
const toolchest_entry_t *toolchest_catalogue(uint32_t i);
/* the catalogue index of a kit by name, or -1 */
int32_t toolchest_find(const char *name);
/* TM1: X.Y.Z, each part 0..999 without leading zeros; ZXM_OK or ZXM_E_ARG. */
int toolchest_parse(const char *s, toolchest_version_t *v);
/* TC1 over two parsed versions: the kit's ABI and the running one. */
int64_t toolchest_fits(const toolchest_version_t *kit, const toolchest_version_t *run);

void toolchest_init(toolchest_t *t, zxm_link_t *link);
/* TM1: zxvd.version, then rpc.discover. */
int toolchest_read_daemon(toolchest_t *t);
/* TM2: play.catalogue. */
int toolchest_read_samples(toolchest_t *t);
/* samples kept in a language ("sutra", "kron", ...) */
uint32_t toolchest_samples_in(const toolchest_t *t, const char *lang);
/* TM3: sys.disk; update.check. */
int toolchest_read_disk(toolchest_t *t);
int toolchest_check_update(toolchest_t *t);
/* TM4: judge kit `kit`'s release; ZXM_E_STATE while the ABI or the room is
 * not known or another offer waits, ZXM_E_ARG on a kit, a version or sizes
 * that are no release. The outcome is t->kit[kit].outcome. */
int toolchest_offer(toolchest_t *t, uint32_t kit, const char *version, const char *needs,
                    const int64_t *blocks, uint32_t nblocks, const int64_t *files, uint32_t nfiles,
                    int64_t firstsig, int64_t secondsig);
/* TM4: the owner's word on the waiting offer; ZXM_E_STATE when none waits. */
int toolchest_confirm(toolchest_t *t, bool approve);
/* TC5 on kit i's record (false when it has none). */
bool toolchest_record_ok(const toolchest_t *t, uint32_t i);

#endif
