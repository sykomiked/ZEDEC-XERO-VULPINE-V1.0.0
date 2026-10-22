/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cartulary_model.h — Cartulary, long-term records, as a model (zxm.h):
 * Hoard files filed under a retention class, each version the content a
 * Keepsake backup holds, found by search, kept to a schedule and proven
 * years later against zxvd's journal. No drawing, no allocation, integers
 * only. The rules it runs are cartulary_rules.h (CR1-CR8).
 *
 * zxvd methods used: files.stat, files.read, files.write, files.mkdir,
 * search.query, backup.versions, backup.list, journal.query and
 * journal.verify.
 *
 *   CM1 THE INDEX is Cartulary/index.txt, one version a line, eleven
 *       tab-separated fields: class, record, version, filed (days since
 *       1970), size, content, source, ruled, seq, prev and the record's
 *       Hoard path. content is the first 15 hex digits of the version's
 *       SHA-256 (backup.versions) as an integer, never 0. source is 1 for
 *       this Hoard, 2 for a copy that arrived from elsewhere (CM4). ruled
 *       is 0, or 1 (kept) or 2 (set aside) by the owner's ruling. seq is
 *       the journal's newest seq when the version was filed (0 with no
 *       journal) and prev the seq of the record's version before it (0
 *       for the first). Holds are Cartulary/holds.txt, one held record a
 *       line: zxvd has no retention-hold flag, so it is a Hoard file.
 *       Every line holds CR7 and banker.entry_ok (class as the folder,
 *       record as the seq) before it is written; a line that does not is
 *       refused on load. Missing files are empty ones.
 *   CM2 SCHEDULE. A version's days left are CR2 of its filed day and its
 *       class's CR1 row; every row holds CR4.
 *   CM3 FILING. cartulary_file stats the file (files.stat), asks Keepsake's
 *       backups for its newest content (backup.versions at the backup's
 *       destination; a file never backed up is refused: back it up
 *       first), and files it as a new record, or as the record's next
 *       version when its content changed (the same content files
 *       nothing). Checked first, then the index is written whole
 *       (files.write); a write that fails leaves the index as it was.
 *   CM4 TWO SOURCES. cartulary_arrive files a version a second source
 *       gives (another device's copy of the index). When both give the
 *       same version with different content and no ruling, its state is
 *       BOTH (CR8): both rows are kept side by side and neither may go;
 *       only the owner's ruling (cartulary_rule, confirmed) makes one TRUE.
 *   CM5 PRUNE ADVICE. A version may go when CR8 is TRUE and CR3 holds,
 *       joined with lpres_conjoin (so BOTH or NEITHER never lets it go);
 *       keepok is keepsake.keep_ok of the versions newer than it, so the
 *       newest never goes. What a Keepsake prune keeping `keep` snapshots
 *       would take is keepsake.removed of backup.list's total. Advice
 *       only: Cartulary removes nothing; holds and rulings need the
 *       owner's confirmed word.
 *   CM6 FINDING. cartulary_search asks search.query and keeps the hits
 *       that are filed records, ranked by their scent.score (zxvd's score,
 *       the same formula), best first, a tie in zxvd's order.
 *   CM7 MEASURES. A class's bytes are banker.total of its versions'
 *       sizes; the index grid is CR6 over each version's class and civil
 *       year; storage growth is CR5 over a class's bytes at the end of
 *       each of the last n months.
 *   CM8 PROOF. cartulary_prove joins two witnesses of a record with
 *       lpres_kjoin: its history (each version linked to the one before,
 *       witness.chain_ok, every line fitting witness.audit_ok), and the
 *       journal (journal.verify intact, and each version's seq still in
 *       it, journal.query). No journal says nothing (NEITHER); a break or
 *       a missing seq is FALSE; when the two disagree it is BOTH, held. */
#ifndef CARTULARY_MODEL_H
#define CARTULARY_MODEL_H

#include "cartulary_rules.h"
#include "zxm.h"

#define CARTULARY_DIR      "Cartulary"
#define CARTULARY_INDEX    "Cartulary/index.txt"
#define CARTULARY_HOLDS    "Cartulary/holds.txt"
#define CARTULARY_ROWS     64u  /* versions in the index */
#define CARTULARY_HELD     32u  /* records on hold */
#define CARTULARY_PATH_MAX 200u /* bytes of a record's path */
#define CARTULARY_DEST_MAX 1024u
#define CARTULARY_HITS     20u
#define CARTULARY_QUERY    256u /* search.query's q */
#define CARTULARY_MONTHS   24u  /* CR5: months of history at most */
#define CARTULARY_NOTICE   200u

typedef struct {
    int64_t cls, record, version, filed, size, content, source, ruled, seq, prev;
    char path[CARTULARY_PATH_MAX + 1];
} cartulary_row_t;

typedef struct {
    zxm_link_t *link;
    bool loaded;
    char dest[CARTULARY_DEST_MAX + 1]; /* the Keepsake backup's destination */
    cartulary_row_t row[CARTULARY_ROWS];
    uint32_t n;
    int64_t held[CARTULARY_HELD];
    uint32_t nheld;
    uint32_t hit[CARTULARY_HITS]; /* row indexes (CM6) */
    int64_t score[CARTULARY_HITS];
    uint32_t nhits;
    char notice[CARTULARY_NOTICE];
} cartulary_t;

void cartulary_init(cartulary_t *c, zxm_link_t *link);
/* the backup's destination, an absolute path (Keepsake's) */
int cartulary_set_dest(cartulary_t *c, const char *dest);
/* CM1 */
int cartulary_load(cartulary_t *c);
/* CM3: today is days since 1970; *record the record filed or found */
int cartulary_file(cartulary_t *c, int64_t cls, const char *path, int64_t today, int64_t *record);
/* CM4 */
int cartulary_arrive(cartulary_t *c, int64_t record, int64_t version, int64_t content,
                     int64_t today);
lpres_state_t cartulary_state(const cartulary_t *c, int64_t record, int64_t version);
int cartulary_rule(cartulary_t *c, int64_t record, int64_t version, int64_t ruling, bool confirmed);
/* CM1: hold on (1) or off (0), on the owner's word */
int cartulary_hold(cartulary_t *c, int64_t record, int64_t on, bool confirmed);
bool cartulary_held(const cartulary_t *c, int64_t record);
/* CM2, CM5 */
int cartulary_days_left(const cartulary_t *c, uint32_t i, int64_t today, int64_t *left);
lpres_state_t cartulary_prunable(const cartulary_t *c, uint32_t i, int64_t today);
uint32_t cartulary_due(const cartulary_t *c, int64_t today);
int cartulary_prune_advice(cartulary_t *c, int64_t keep, int64_t *removed);
/* CM6 */
int cartulary_search(cartulary_t *c, const char *query);
/* CM7 */
int cartulary_class_bytes(const cartulary_t *c, int64_t cls, int64_t *bytes);
int64_t cartulary_grid(const cartulary_t *c, uint32_t i);
int cartulary_growth(const cartulary_t *c, int64_t cls, int64_t today, uint32_t months,
                     int64_t ahead, int64_t *projected);
/* civil year of a day since 1970 */
int64_t cartulary_year(int64_t day);
/* CM8 */
lpres_state_t cartulary_prove(cartulary_t *c, int64_t record);
/* the row of record's version from source (1 or 2), or -1 */
int32_t cartulary_find(const cartulary_t *c, int64_t record, int64_t version, int64_t source);

#endif
