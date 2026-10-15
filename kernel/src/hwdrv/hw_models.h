/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hw_models.h — the golden discs' AI models, checked by the kernel when
 * they are first needed rather than by the loader on every boot.
 *
 * Why. The EFI stub used to re-hash every model before leaving boot
 * services, so boot time grew with the disc (36 s for 1.6 GB under
 * emulation) and every byte passed through firmware. A swarm of many
 * gigabytes cannot work that way. Now the boot only reads the manifests;
 * each model is hashed when something asks for it (hw_models_require), and
 * whatever nobody asked for is hashed in the idle loop after boot
 * (hw_models_step), a cluster budget at a time.
 *
 * The discs (build_system/mk_golden_disc.sh):
 *   core disc, one per architecture: an ESP (FAT32) with /ZXV/MODELS.SHA and
 *     the core models in /ZXV/MODELS/
 *   swarm discs, the same for every architecture: one FAT32 partition of GPT
 *     type HW_GPT_ZXV_SWARM named ZXV_SWARM_<GROUP>, with /MODELS.SHA and the
 *     group's models in /MODELS/. Any number may be attached, each with its
 *     own manifest and release cycle; the core disc does not list them.
 * MODELS.SHA, one line per model:
 *   <sha256, 64 hex> <bytes> <name> <esp|swarm> <8.3 file name>
 * The partition word must say where the manifest is ("esp" on the core
 * disc, "swarm" on a swarm disc). The 8.3 name is what the kernel's FAT32
 * driver reads (it has no long file names); <name> is the model's real name,
 * for people, and is unique across all attached discs.
 *
 * Each model's verdict is four-valued like the rest of the kernel:
 * NEITHER (not checked yet), TRUE (size and SHA-256 match), FALSE (they
 * do not, or it cannot be read). A model is only handed out TRUE. */
#ifndef ZXV_HW_MODELS_H
#define ZXV_HW_MODELS_H

#include "hw_storage.h"
#include "../robin_debanks/sha256.h"

#define HW_MODELS_MAX     32
#define HW_MODEL_NAME     64
#define HW_MODELS_SHA_MAX 8192u
/* the core disc's ESP and one swarm partition per disk the registry holds */
#define HW_MODELS_VOLS (1 + HW_MAX_BLK)

typedef struct hw_model {
    char name[HW_MODEL_NAME]; /* real name, for the log */
    char file[13];            /* 8.3 name on its volume */
    uint8_t sha[32];
    uint32_t bytes;       /* FAT32 files are < 4 GiB */
    uint8_t vol;          /* 0 the core disc's ESP, else a swarm disc */
    uint8_t found;        /* the directory entry exists, size checked below */
    uint32_t first;       /* first cluster, from the directory entry */
    uint32_t disc_bytes;  /* size in the directory entry */
    hw_verdict_t verdict; /* HW_N until checked */
} hw_model_t;

/* One FAT32 volume holding models. */
typedef struct hw_models_vol {
    hw_blk_t *disk;
    hw_part_t part; /* part.name is the swarm disc's ZXV_SWARM_<GROUP> */
    hw_bdev_t bd;
    fat32_state_t fs;
    uint32_t fat_sector; /* sector held in fat_buf, 0xFFFFFFFF for none */
    uint8_t fat_buf[512];
} hw_models_vol_t;

/* Where a model's hash has got to. */
typedef struct hw_model_cur {
    int idx; /* -1 when idle */
    uint32_t cluster, hops;
    uint64_t done;
    sha256_ctx_t ctx;
} hw_model_cur_t;

/* A swarm disc the core disc expects (/ZXV/SWARM.LST). */
#define HW_SWARM_WANT_MAX 16
typedef struct hw_swarm_want {
    uint32_t disc;  /* 2, 3, ... */
    char group[17]; /* REASON: the partition is ZXV_SWARM_REASON */
    char image[48]; /* zxv-swarm-disc2-reason.img */
} hw_swarm_want_t;

typedef struct hw_models {
    hw_model_t m[HW_MODELS_MAX];
    uint32_t n;
    hw_models_vol_t vol[HW_MODELS_VOLS];
    uint32_t nvol;     /* vol[0] is used only when a core disc was found */
    hw_model_cur_t bg; /* the idle-loop check */
    uint32_t ok, bad;
    int reported;
    /* from the core disc: where the release lives and what it holds */
    char cid[121], ipns[121]; /* /ZXV/UPSTREAM.CID, "" when absent */
    hw_swarm_want_t want[HW_SWARM_WANT_MAX];
    uint32_t nwant;
} hw_models_t;

/* Find the models: the first registered disk whose ESP holds
 * /ZXV/MODELS.SHA (the core disc), then every disk with a ZXV_SWARM_*
 * partition (swarm discs). Reads and checks each manifest and logs:
 *   [UPSTREAM] ipns <name>, bucket <cid>  from the core's /ZXV/UPSTREAM.CID
 *   [SWARM_MOUNTED] <label> on <disk>: <n> models     per swarm disc
 *   [SWARM_MISSING] disc <n> ZXV_SWARM_<GROUP> (<image>): ...  for each
 *   swarm disc the core's /ZXV/SWARM.LST names that is not attached, or
 *   [SWARM_COMPLETE] when all are
 *   [MODELS_INDEXED] <n> listed (<e> on the boot partition, <s> on <d>
 *   swarm discs); each is verified when first used
 * or [MODELS_NONE] / [MODELS_FAIL] <why>; a bad manifest drops only its own
 * disc's models. Hashes nothing. Returns n. */
uint32_t hw_models_index(hw_models_t *s, const hw_plat_t *p, hw_registry_t *r);

/* Hash at most `clusters` clusters of the next unchecked model. Logs
 * [MODEL_OK] / [MODEL_BAD] as each model finishes and, once all have a
 * verdict, [MODELS_OK] <n> verified or [MODELS_FAIL] <bad> of <n> models
 * bad. Returns 1 while models remain unchecked, else 0. */
int hw_models_step(hw_models_t *s, const hw_plat_t *p, uint32_t clusters);

/* The verdict for model `name` (real name or 8.3 name), hashing it now if
 * it has none yet: first use pays for its own check. HW_F for an unknown
 * name. */
hw_verdict_t hw_models_require(hw_models_t *s, const hw_plat_t *p, const char *name);

#endif
