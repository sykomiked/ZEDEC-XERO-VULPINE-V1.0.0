/* doc.h — ZXV AppKit document model
 *
 * The scaffolding every native ZXV application is built on. One text
 * document = a gap buffer + a bounded undo/redo history + a dirty flag
 * + a ZXVFS-backed load/save path. Writer, Sheet, Deck, Sound and Reel
 * all sit on this: they differ in how they *interpret* the buffer, not
 * in how editing, history or persistence work.
 *
 * Design notes
 * ------------
 * - Gap buffer: insertion at the caret is O(1) amortised, which is the
 *   dominant operation in an editor. Moving the caret moves the gap.
 * - Undo records are COALESCED: a run of single-character typing folds
 *   into one undo step, so ctrl-Z behaves the way a person expects
 *   rather than undoing one letter at a time.
 * - Everything is fixed-capacity and freestanding — no malloc, no libc.
 *   A document that would exceed its capacity fails the edit rather
 *   than truncating silently.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV AppKit slice)
 * License: SEL-3.3
 */
#ifndef ZXV_APPKIT_DOC_H
#define ZXV_APPKIT_DOC_H

#include <stdint.h>
#include <stdbool.h>

#define DOC_CAPACITY     7168     /* fits one ZXVFS extent with headroom */
#define DOC_UNDO_DEPTH   64
#define DOC_UNDO_TEXT    512      /* bytes of text held per undo record */
#define DOC_NAME_LEN     32

typedef enum {
    DOC_OP_NONE = 0,
    DOC_OP_INSERT,     /* text was inserted at pos */
    DOC_OP_DELETE,     /* text was removed from pos */
} doc_op_t;

typedef struct {
    doc_op_t op;
    uint32_t pos;
    uint32_t len;
    char     text[DOC_UNDO_TEXT];   /* the affected bytes */
} doc_edit_t;

typedef struct {
    /* gap buffer: [0,gap_start) text, [gap_start,gap_end) gap, [gap_end,cap) text */
    char     buf[DOC_CAPACITY];
    uint32_t gap_start;
    uint32_t gap_end;

    char     name[DOC_NAME_LEN];
    bool     dirty;

    /* bounded undo/redo rings */
    doc_edit_t undo[DOC_UNDO_DEPTH];
    uint32_t   undo_count;
    doc_edit_t redo[DOC_UNDO_DEPTH];
    uint32_t   redo_count;

    /* coalescing: consecutive typing folds into the top undo record */
    bool     coalescing;
    uint32_t last_insert_end;

    /* stats */
    uint32_t total_edits;
} doc_t;

/* lifecycle */
void     doc_init(doc_t *d, const char *name);
uint32_t doc_length(const doc_t *d);
bool     doc_is_dirty(const doc_t *d);

/* read the document into a flat buffer; returns bytes written */
uint32_t doc_text(const doc_t *d, char *out, uint32_t max);

/* single character at index, or 0 */
char     doc_char_at(const doc_t *d, uint32_t idx);

/* editing — all return false if the edit would exceed capacity */
bool doc_insert(doc_t *d, uint32_t pos, const char *s, uint32_t len);
bool doc_delete(doc_t *d, uint32_t pos, uint32_t len);

/* history */
bool doc_undo(doc_t *d);
bool doc_redo(doc_t *d);
void doc_break_coalesce(doc_t *d);   /* end the current typing run */

/* search: index of first occurrence at/after `from`, or -1 */
int32_t doc_find(const doc_t *d, const char *needle, uint32_t from);

/* line helpers (documents are line-oriented for Writer/Sheet) */
uint32_t doc_line_count(const doc_t *d);
uint32_t doc_line_start(const doc_t *d, uint32_t line);

#endif /* ZXV_APPKIT_DOC_H */
