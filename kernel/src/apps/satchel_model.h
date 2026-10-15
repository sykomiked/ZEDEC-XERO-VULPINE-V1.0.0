/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* satchel_model.h — Satchel, the packager, as a model (zxm.h): a folder of
 * the Hoard becomes one zxpkg bundle, its files go into zxvd's block store,
 * zxvd signs the public recipe that rebuilds the bundle from them, and a
 * recipe is installed only with the owner's word. The rules are
 * satchel_rules.h.
 *
 * zxvd methods used: files.root, files.list, files.stat, files.read,
 * files.write, fab.put, fab.send and fab.receive. No drawing, no
 * allocation, integers only.
 *
 *   SM1 A PACKAGE is the files of one folder of the Hoard (its subfolders,
 *       bundles (.zxpkg) and recipes (.fab) left out), in the order
 *       files.list gives them. A file's name is 1..SATCHEL_NAME_MAX bytes
 *       of A-Z a-z 0-9 '.' '_' '-', not starting with '.'; a file is at
 *       least 1 byte; the package fits SR1. Anything else refuses the
 *       whole package (fault says why) and the package open stays.
 *   SM2 THE BUNDLE is the index, then each file's bytes in package order.
 *       The index is ASCII lines, each ending in LF:
 *           zxpkg 1 HHHHHHHH      the index's own length, 8 digits
 *           name <package>
 *           files <n>
 *           bytes <total>
 *           file <size> <kind> <name>     one per file (kind: SR4)
 *           end
 *       satchel_build writes it as a new file of the Hoard (never over
 *       one that is there), copying each file through files.read and
 *       files.write in parts, then checks its size and puts every file
 *       into the block store (fab.put): one block each.
 *   SM3 SIGNING. satchel_sign asks zxvd for the public recipe (fab.send,
 *       no "to") that rebuilds the bundle from those blocks, signed with
 *       zxvd's ML-DSA-87 key, and files the package record (SR5): its
 *       number, files, bytes, signed and the time.
 *   SM4 INSTALLING is receiving a recipe (fab.receive) from a sender on
 *       the owner's trust list (1..SATCHEL_TRUST_MAX sender ids), and only
 *       after the owner said yes (satchel_confirm). The word is spent by
 *       one install, whatever its outcome; it is never carried over. The
 *       bundle is written as a new file of the Hoard, and its index is read
 *       back (satchel_inspect) and must fit SR1 and the bundle's length.
 *       A recipe this model signed must rebuild the very bundle it signed. */
#ifndef SATCHEL_MODEL_H
#define SATCHEL_MODEL_H

#include "satchel_rules.h"
#include "zxm.h"

#define SATCHEL_NAME_MAX  64u   /* bytes of a file's name, or of a package's */
#define SATCHEL_PATH_MAX  512u  /* a path in the Hoard that Satchel keeps */
#define SATCHEL_ABS_MAX   1023u /* an absolute path for fab.* (ZXVD_FAB_PATH - 1) */
#define SATCHEL_CID_MAX   95u
#define SATCHEL_ID_HEX    64u /* a sender id: the SHA-256 of its key, in hex */
#define SATCHEL_TRUST_MAX 16u
#define SATCHEL_HEAD_MAX  8192u /* bytes of an index */
#define SATCHEL_FIRST     17u   /* "zxpkg 1 HHHHHHHH\n" */
#define SATCHEL_CHUNK     9216u /* bytes copied per step: 12288 of base64 in one request */
#define SATCHEL_B64_MAX   12288u

/* SM1, SM4: why a package or an index was refused (satchel_t.fault). */
enum {
    SATCHEL_FINE = 0,
    SATCHEL_F_NAME = 1,   /* a name past SATCHEL_NAME_MAX or with a byte not allowed */
    SATCHEL_F_EMPTY = 2,  /* a file of 0 bytes */
    SATCHEL_F_LIMITS = 3, /* outside SR1 */
    SATCHEL_F_INDEX = 4,  /* an index that is not SM2's, or does not fit its bundle */
};

typedef struct {
    char name[SATCHEL_NAME_MAX + 1];
    int64_t size;
    int64_t share; /* SR3, hundredths of a percent */
    int64_t kind;  /* SR4 */
    char cid[SATCHEL_CID_MAX + 1];
} satchel_file_t;

/* One package: opened from a folder (SM1) or read from a bundle's index. */
typedef struct {
    char name[SATCHEL_NAME_MAX + 1];
    satchel_file_t file[SATCHEL_FILES_MAX];
    uint32_t nfiles;
    int64_t total;                /* SR2 */
    int64_t largest;              /* its largest file */
    int64_t count[SATCHEL_KINDS]; /* files of each kind (witness.kind_count) */
    int64_t head_len;             /* the index's bytes */
} satchel_pkg_t;

typedef struct {
    zxm_link_t *link;
    char root[SATCHEL_ABS_MAX + 1]; /* files.root, once asked */
    /* SM1 */
    char folder[SATCHEL_PATH_MAX + 1];
    satchel_pkg_t pkg;
    bool open;
    int32_t fault; /* of the last package or index refused */
    /* SM2 */
    char bundle[SATCHEL_PATH_MAX + 1];
    int64_t bundle_len;
    bool built;
    /* SM3 */
    char recipe[SATCHEL_PATH_MAX + 1];
    char cid[SATCHEL_CID_MAX + 1]; /* the bundle's CID, as the recipe names it */
    int64_t ops, recipe_len;
    bool is_signed; /* zxvd signed the recipe for this bundle */
    int64_t seq;    /* the last package record's number, 0 for none */
    int64_t stamp;  /* its time, ms since 1970 */
    /* SM4 */
    bool confirmed;
    bool installed;
    char installed_at[SATCHEL_PATH_MAX + 1];
    char sender[SATCHEL_ID_HEX + 1];
    satchel_pkg_t got; /* the installed (or inspected) bundle's index */
    /* working space */
    char head[SATCHEL_HEAD_MAX + 1];
    char b64[SATCHEL_B64_MAX + 1];
} satchel_t;

void satchel_init(satchel_t *s, zxm_link_t *link);

/* SM1: files.list the folder ("" the Hoard's top) as the package `name`. */
int satchel_open(satchel_t *s, const char *folder, const char *name);

/* SM2: the index of a package into out (cap bytes, terminated); its
 * length, or -1 when it does not fit. */
int32_t satchel_index(const satchel_pkg_t *p, char *out, uint32_t cap);

/* SM2: write the bundle at out_path (a new .zxpkg file of the Hoard) and
 * put each file into the block store. */
int satchel_build(satchel_t *s, const char *out_path);

/* SM3: the signed public recipe at recipe_path (a .fab file of the
 * Hoard); now_ms is the time for the package record. */
int satchel_sign(satchel_t *s, const char *recipe_path, int64_t now_ms);

/* SM4: the owner's word for the next install. */
void satchel_confirm(satchel_t *s, bool yes);

/* SM4: install the recipe at recipe_path as the bundle out_path (a new
 * .zxpkg file of the Hoard), from a sender in trust (ntrust ids of
 * SATCHEL_ID_HEX hex digits). ZXM_E_STATE, nothing sent, without the
 * owner's word. */
int satchel_install(satchel_t *s, const char *recipe_path, const char *out_path,
                    const char *const *trust, uint32_t ntrust);

/* SM4: read a bundle's index into s->got. */
int satchel_inspect(satchel_t *s, const char *bundle_path);

#endif
