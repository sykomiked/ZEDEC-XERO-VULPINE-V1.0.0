/* chronicle_persist.h — make the Chronicle survive power loss
 *
 * The Chronicle remembers the system's judgments, but only in RAM; a reboot
 * or a crash forgets everything. This layer writes the ledger to ZXVFS and
 * reads it back, so the memory is DURABLE. It is kept separate from
 * chronicle.c on purpose: the ledger core has no filesystem dependency, and
 * persistence is a thin composition of Chronicle + ZXVFS.
 *
 * CRASH SAFETY COMES FROM THE LAYER BELOW
 * ---------------------------------------
 * A save is a single zxvfs_write, and ZXVFS commits every write through its
 * redo journal — the write either lands whole or not at all. So a power loss
 * during a save leaves either the previous ledger or the new one on disk,
 * never a torn one. On load the chain is re-verified end to end, so a
 * corrupted or tampered image is refused rather than trusted.
 *
 * ON-DISK FORMAT (little-endian, struct-padding-free)
 * ---------------------------------------------------
 *   u32 magic 'CHR1' | u32 version | u32 n_entries | u64 next_seq | u8[32] head
 *   then n_entries * { u64 seq | u8 verdict | u8 reason
 *                      | u8[32] event_digest | u8[32] entry_hash }
 *
 * A ZXVFS file is at most ZXVFS_FILE_MAX_BYTES, so at most CHRON_PERSIST_MAX
 * entries fit one file; saving a longer ledger returns false (the caller
 * should rotate). That bound is stated, not hidden.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV Chronicle durability slice)
 * License: SEL-3.3
 */
#ifndef ZXV_CHRONICLE_PERSIST_H
#define ZXV_CHRONICLE_PERSIST_H

#include <stdbool.h>
#include "chronicle.h"
#include "../zxvfs/zxvfs.h"

/* Header 52 bytes; each entry 74 bytes. How many entries fit one ZXVFS file. */
#define CHRON_PERSIST_HEADER  52u
#define CHRON_PERSIST_ENTRY   74u
#define CHRON_PERSIST_MAX     ((ZXVFS_FILE_MAX_BYTES - CHRON_PERSIST_HEADER) / CHRON_PERSIST_ENTRY)

/* Write the ledger to `name` on `fs`. Atomic (journaled). Returns false on a
 * write error or if the ledger has more than CHRON_PERSIST_MAX entries. */
bool chronicle_save(const chronicle_t *c, zxvfs_t *fs, const char *name);

/* Read the ledger back from `name`, then verify the whole chain. On any
 * problem (missing file, bad magic/version, truncated, or a chain that does
 * not verify) it returns false and leaves `c` freshly initialised — a
 * corrupted ledger is never silently accepted. */
bool chronicle_load(chronicle_t *c, zxvfs_t *fs, const char *name);

#endif /* ZXV_CHRONICLE_PERSIST_H */
