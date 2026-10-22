/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* silt_model.h — Silt, the official records office, as a model (zxm.h):
 * records received from the Hoard, stamped with a register number of
 * their year, filed in a case file tree, kept to their series' retention
 * schedule and disposed of only to the Trash, on the records officer's
 * approval and the owner's confirmed word. No drawing, no allocation,
 * integers only. The rules it runs are silt_rules.h (SR1-SR5).
 *
 * zxvd methods used: files.list, files.stat, files.read, files.write,
 * files.mkdir, files.trash, backup.versions, journal.query,
 * journal.verify and web4.verify.
 *
 *   SM1 THE REGISTER is Silt/register.txt, one record a line, fourteen
 *       tab-separated fields: regno, series, caseno, doc, dated (the
 *       record's own date, days since 1970), received (days since 1970),
 *       size, seq (the line's place, 1 for the first), prev (the seq of
 *       the line before, 0 for the first), jseq (the journal's newest seq
 *       when it was stamped, 0 with no journal), hold (the legal hold, 0
 *       or 1), kind (1 filed, 2 disposed of to the Trash), the reference
 *       and the record's Hoard path. Every line holds SR4
 *       (case_file_node_ok) and banker.entry_ok (series as the folder,
 *       regno's seq as the seq) before it is written, and a line that
 *       does not is refused on load. A missing register is an empty one.
 *   SM2 RECEIVING. silt_receive checks the receipt first: its six fields
 *       (path, reference, series, case, date, today) are counted missing
 *       or outside and must be sealwax.sign_ready; the same path twice is
 *       refused. Then the owner's word, then files.stat (a folder is
 *       refused), and a PDF (".pdf") must be broadsheet.well_formed: its
 *       header "%PDF-" and its last startxref inside the file. The year's
 *       next seq is tallystick.next_doc_number of its last (refused past
 *       999999: the year's register is full), its register number SR1,
 *       its doc the case's next. The register is written whole
 *       (files.write); a write that fails leaves it as it was.
 *   SM3 ONE REFERENCE. Two records under one reference with different
 *       dates are BOTH (SR5): both are filed and flagged, never merged,
 *       and neither may be disposed of until the owner corrects one.
 *   SM4 RETENTION. A record's days left are cartulary.retention_left of
 *       its date and its series' schedule (years, months; each schedule
 *       must hold Cartulary's limits, 1..36525 days).
 *   SM5 DISPOSAL. A record may be disposed of when SR2 holds (record_
 *       may_dispose: over, no hold, approved), joined with its reference's
 *       state by lpres_conjoin, so BOTH or NEITHER never lets it go.
 *       silt_dispose then moves it to the Trash (files.trash) on the
 *       owner's confirmed word; nothing is erased. A legal hold is set or
 *       lifted on the owner's word too.
 *   SM6 THE TRAIL joins two witnesses with lpres_kjoin: the register's
 *       own chain (each line linked to the one before, witness.chain_ok)
 *       and the journal (journal.verify intact, and each stamped jseq
 *       inside it). No journal says nothing (NEITHER); a break is FALSE;
 *       when the two disagree it is BOTH, held.
 *   SM7 MEASURES. The inbox's records not yet received (files.list); a
 *       record's versions (backup.versions at Keepsake's destination); a
 *       series' bytes (banker.total) and its share of the register's
 *       (banker.share); the count of one kind (witness.kind_count); the
 *       filing grid, series by year (cartulary.class_year_cell); the
 *       backlog forecast (SR3), exact.
 *   SM8 A SIGNED COPY is checked with web4.verify (its envelope's hex in a
 *       Hoard file); the verdict is shown, never acted on. */
#ifndef SILT_MODEL_H
#define SILT_MODEL_H

#include "silt_rules.h"
#include "zxm.h"

#define SILT_DIR      "Silt"
#define SILT_REGISTER "Silt/register.txt"
#define SILT_ROWS     64u   /* records in the register */
#define SILT_REF_MAX  32u   /* bytes of a reference */
#define SILT_PATH_MAX 200u  /* bytes of a record's path */
#define SILT_DEST_MAX 1024u /* the backup's destination */
#define SILT_SIG_MAX  8192u /* hex of a signed envelope */
#define SILT_NOTICE   240u

typedef struct {
    int64_t regno, series, caseno, doc, dated, received, size, seq, prev, jseq, hold, kind;
    char ref[SILT_REF_MAX + 1];
    char path[SILT_PATH_MAX + 1];
} silt_row_t;

typedef struct {
    zxm_link_t *link;
    bool loaded;
    char dest[SILT_DEST_MAX + 1]; /* Keepsake's backup destination */
    silt_row_t row[SILT_ROWS];
    uint32_t n;
    bool sig_checked, sig_valid; /* SM8 */
    int64_t sig_bytes;
    char notice[SILT_NOTICE];
} silt_t;

void silt_init(silt_t *s, zxm_link_t *link);
/* the backup's destination, an absolute path (Keepsake's) */
int silt_set_dest(silt_t *s, const char *dest);
/* SM1 */
int silt_load(silt_t *s);
int32_t silt_find(const silt_t *s, int64_t regno);
/* SM2: dated and today are days since 1970; *regno the number stamped */
int silt_receive(silt_t *s, const char *path, int64_t series, int64_t caseno, const char *ref,
                 int64_t dated, int64_t today, bool owner_confirmed, int64_t *regno);
/* SM3 */
lpres_state_t silt_reference(const silt_t *s, const char *ref);
/* SM4, SM5 */
int silt_days_left(const silt_t *s, uint32_t i, int64_t years, int64_t months, int64_t today,
                   int64_t *left);
lpres_state_t silt_disposable(const silt_t *s, uint32_t i, int64_t years, int64_t months,
                              int64_t today, int64_t approved);
int silt_dispose(silt_t *s, int64_t regno, int64_t years, int64_t months, int64_t today,
                 int64_t approved, bool owner_confirmed);
int silt_hold(silt_t *s, int64_t regno, int64_t on, bool owner_confirmed);
/* SM6 */
lpres_state_t silt_trail(silt_t *s);
/* SM7 */
int silt_inbox(silt_t *s, const char *folder, uint32_t *waiting);
int silt_versions(silt_t *s, int64_t regno, uint32_t *count);
int silt_series_bytes(const silt_t *s, int64_t series, int64_t *bytes);
int64_t silt_series_share(const silt_t *s, int64_t series);
int64_t silt_count(const silt_t *s, int64_t kind);
int64_t silt_grid(const silt_t *s, uint32_t i);
int silt_backlog(silt_t *s, const int64_t *arrived, const int64_t *cleared, uint32_t n,
                 int64_t backlog, rat_t *days);
/* civil year of a day since 1970 */
int64_t silt_year(int64_t day);
/* SM8 */
int silt_check_signed(silt_t *s, const char *sigpath);

#endif
