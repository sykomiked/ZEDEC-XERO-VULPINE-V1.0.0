/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* typecase_model.h — Typecase, fonts, as a model (zxm.h): the font files
 * kept in the Hoard, each one's table directory checked, its family name,
 * which scripts it covers and how much of each, a preview of the first
 * characters it has of each script, and an install that copies an accepted
 * font into a folder of the Hoard. The rules are typecase_rules.h.
 *
 * zxvd methods used: files.list, files.read and files.write. zxvd has no
 * font registry, so installing is a copy in the Hoard, nothing more. No
 * drawing, no allocation, integers only.
 *
 *   TM1 A FONT FILE is an sfnt: version 0x00010000, 'true' or 'OTTO', then
 *       numTables and a 16-byte record per table (tag, checksum, offset,
 *       length), all big-endian. A file shorter than its own header and
 *       directory, or with another version (a collection 'ttcf' among
 *       them), is not a font (TYPECASE_F_FORM). The directory is checked by
 *       TR1 (typecase_font_ok); a font it does not accept is kept open
 *       with its tables listed, so the view can show why, and nothing more
 *       is read from it (TYPECASE_F_TABLES).
 *   TM2 SCRIPTS. Each script is counted over the letters Typecase names
 *       for it (typecase_model.c, SCRIPTS): Latin, Greek, Cyrillic, Hebrew,
 *       Arabic, Devanagari, Thai, Hiragana, Katakana, Hangul and Han. A
 *       code point is covered when the font's character map gives it a
 *       glyph other than 0. Its coverage is TR2 (typecase_coverage_x100).
 *   TM3 THE CHARACTER MAP read is the best Unicode subtable the 'cmap'
 *       table has: format 12 (platform 3 encoding 10, or platform 0
 *       encoding 4 or 6) before format 4 (platform 3 encoding 1, or
 *       platform 0 encoding 0..3). Its ranges must rise and never overlap,
 *       and every byte it names must lie inside the table; anything else,
 *       or no Unicode subtable of those formats, is TYPECASE_F_CMAP and
 *       the font covers nothing.
 *   TM4 THE FAMILY NAME is name ID 1 of the 'name' table, Windows Unicode
 *       (platform 3, UTF-16BE) first, then Unicode (platform 0), then Mac
 *       Roman (platform 1, its ASCII bytes); at most TYPECASE_NAME bytes of
 *       UTF-8, cut on a character boundary. A character outside the Basic
 *       Multilingual Plane, or a Mac Roman byte past ASCII, is shown as
 *       '?'. A font without one has the name "".
 *   TM5 THE PREVIEW of a script is the first TYPECASE_SAMPLE code points of
 *       it the font covers, in order, as UTF-8.
 *   TM6 INSTALL copies an accepted font (TR1) byte for byte into a folder
 *       of the Hoard under its own file name, in parts (files.write begin,
 *       add, end), never over a file that is there. The folder must exist.
 *       A source that changes size while it is copied stops the copy
 *       (ZXM_E_STATE), and nothing appears. */
#ifndef TYPECASE_MODEL_H
#define TYPECASE_MODEL_H

#include "typecase_rules.h"
#include "zxm.h"

#define TYPECASE_PATH_MAX 255u /* a path in the Hoard that Typecase keeps */
#define TYPECASE_LIST     64u  /* font files kept from one folder */
#define TYPECASE_NAME     63u  /* bytes of a family name */
#define TYPECASE_SCRIPTS  11u  /* TM2 */
#define TYPECASE_SAMPLE   16u  /* TM5: code points in a preview */

/* Why a file is not a font, or covers nothing (typecase_t.fault). */
enum {
    TYPECASE_F_NONE = 0,
    TYPECASE_F_FORM = 1,   /* TM1: not an sfnt, or shorter than its directory */
    TYPECASE_F_TABLES = 2, /* TR1 does not accept the directory */
    TYPECASE_F_CMAP = 3,   /* TM3: no usable character map */
    TYPECASE_F_COVER = 4,  /* TR2 refused a count: the map is not what it says */
};

typedef struct {
    char path[TYPECASE_PATH_MAX + 1];
    uint64_t size;
    int64_t mtime;
} typecase_file_t;

typedef struct {
    uint32_t tag; /* four bytes, big-endian: 'cmap' is 0x636D6170 */
    int64_t off, len;
} typecase_table_t;

typedef struct {
    zxm_link_t *link;
    /* files.list: the font files found */
    typecase_file_t file[TYPECASE_LIST];
    uint32_t nfiles;
    /* the font open (TM1), "" for none */
    char path[TYPECASE_PATH_MAX + 1];
    int64_t size;
    uint32_t ntables; /* as the directory says, even past TYPECASE_TABLES_MAX */
    typecase_table_t table[TYPECASE_TABLES_MAX];
    int64_t accepted; /* TR1: 1, 0, or TYPECASE_REFUSED */
    int32_t fault;
    char family[TYPECASE_NAME + 1]; /* TM4 */
    /* TM2, TM5 */
    int64_t covered[TYPECASE_SCRIPTS];
    int64_t cover_x100[TYPECASE_SCRIPTS];
    uint32_t sample[TYPECASE_SCRIPTS][TYPECASE_SAMPLE];
    uint32_t nsample[TYPECASE_SCRIPTS];
    /* TM6: where the last install put it */
    char installed[TYPECASE_PATH_MAX + 1];
} typecase_t;

void typecase_init(typecase_t *t, zxm_link_t *link);

/* TM2: a script's name ("Latin"...), or NULL past the last. */
const char *typecase_script_name(uint32_t script);
/* TM2: how many code points a script has, as Typecase counts them; 0 past the last. */
int64_t typecase_script_points(uint32_t script);

/* files.list: the .ttf and .otf files in a folder of the Hoard ("" its top). */
int typecase_list(typecase_t *t, const char *folder);
/* files.read: open the font at path and check it (TM1..TM4). ZXM_E_ARG with
 * fault set when it is no font, not accepted, or its map is unusable. */
int typecase_open(typecase_t *t, const char *path);
/* TM5: the preview of a script into out (at most cap bytes, NUL added); its
 * length in bytes, or -1 when no font is open, the script is past the last
 * or out is too small. */
int32_t typecase_preview(const typecase_t *t, uint32_t script, char *out, uint32_t cap);
/* TM6: copy the open font into folder ("" the Hoard's top). ZXM_E_STATE when
 * no accepted font is open. */
int typecase_install(typecase_t *t, const char *folder);

#endif
