/*
 * xedit.h — X-EDIT Native Text & Code Editor
 *
 * Gap-buffer editor operating directly in system RAM.
 * Supports modal (Vi-like) and non-modal editing modes.
 * Real-time CID (Content Identifier) and Merkle root tracking.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: ZEDEC Open Patent License 1.0
 */

#ifndef ZEDEC_XEDIT_H
#define ZEDEC_XEDIT_H

#include <stdint.h>
#include <stdbool.h>

/* ===== Constants ===== */

#define XEDIT_MAX_BUFFERS    8
#define XEDIT_BUF_SIZE    65536
#define XEDIT_MAX_LINES    4096
#define XEDIT_MAX_FILENAME  128
#define XEDIT_GAP_MIN       32
#define XEDIT_GAP_GROW     1024
#define XEDIT_HASH_SIZE      32
#define XEDIT_MAX_UNDO      128

/* ===== Editor Modes ===== */

typedef enum {
    XEDIT_MODE_NORMAL  = 0,  /* Non-modal (standard) */
    XEDIT_MODE_INSERT  = 1,  /* Vi insert mode */
    XEDIT_MODE_COMMAND = 2,  /* Vi command mode */
    XEDIT_MODE_VISUAL  = 3,  /* Vi visual mode */
} xedit_mode_t;

/* ===== File Types ===== */

typedef enum {
    XEDIT_FILE_TEXT      = 0,
    XEDIT_FILE_C_SOURCE  = 1,
    XEDIT_FILE_SVERILOG  = 2,
    XEDIT_FILE_MANIFEST  = 3,
    XEDIT_FILE_CONFIG    = 4,
    XEDIT_FILE_BINARY    = 5,
} xedit_filetype_t;

/* ===== Undo Entry ===== */

typedef struct {
    uint32_t pos;       /* Position where change occurred */
    uint32_t del_len;   /* Length of deleted text */
    uint32_t ins_len;   /* Length of inserted text */
    char     text[256]; /* Changed text (truncated for small edits) */
} xedit_undo_t;

/* ===== Buffer (Gap Buffer) ===== */

typedef struct {
    char     data[XEDIT_BUF_SIZE];
    uint32_t gap_start;
    uint32_t gap_end;
    uint32_t cursor;
    uint32_t line_count;
    uint32_t line_starts[XEDIT_MAX_LINES];

    char     filename[XEDIT_MAX_FILENAME];
    xedit_filetype_t filetype;
    xedit_mode_t mode;
    bool     modified;
    bool     active;

    /* Integrity tracking */
    uint8_t  cid[XEDIT_HASH_SIZE];       /* Content identifier */
    uint8_t  merkle_root[XEDIT_HASH_SIZE]; /* Merkle root hash */
    bool     hash_valid;

    /* Undo stack */
    xedit_undo_t undo_stack[XEDIT_MAX_UNDO];
    uint32_t     undo_pos;
    uint32_t     undo_count;
} xedit_buffer_t;

/* ===== Editor State ===== */

typedef struct {
    xedit_buffer_t buffers[XEDIT_MAX_BUFFERS];
    uint32_t       active_buffer;
    uint32_t       num_buffers;
    bool           initialized;
} xedit_t;

/* ===== API ===== */

void xedit_init(xedit_t *e);
int32_t xedit_buffer_create(xedit_t *e, const char *filename);
int xedit_buffer_close(xedit_t *e, uint32_t idx);
int xedit_switch_buffer(xedit_t *e, uint32_t idx);

/* Gap buffer operations */
int xedit_insert(xedit_buffer_t *b, const char *text, uint32_t len);
int xedit_insert_char(xedit_buffer_t *b, char c);
int xedit_delete_back(xedit_buffer_t *b);
int xedit_delete_forward(xedit_buffer_t *b);
int xedit_cursor_left(xedit_buffer_t *b);
int xedit_cursor_right(xedit_buffer_t *b);
int xedit_cursor_up(xedit_buffer_t *b);
int xedit_cursor_down(xedit_buffer_t *b);
int xedit_cursor_line_start(xedit_buffer_t *b);
int xedit_cursor_line_end(xedit_buffer_t *b);
int xedit_cursor_goto(xedit_buffer_t *b, uint32_t pos);

/* Text retrieval */
uint32_t xedit_get_text(xedit_buffer_t *b, char *out, uint32_t max_len);
uint32_t xedit_get_line(xedit_buffer_t *b, uint32_t line, char *out, uint32_t max_len);
uint32_t xedit_line_at_cursor(xedit_buffer_t *b);
uint32_t xedit_col_at_cursor(xedit_buffer_t *b);

/* Mode switching */
void xedit_set_mode(xedit_buffer_t *b, xedit_mode_t mode);

/* File type detection */
xedit_filetype_t xedit_detect_filetype(const char *filename);
const char *xedit_filetype_name(xedit_filetype_t ft);

/* Integrity tracking */
void xedit_update_hash(xedit_buffer_t *b);
bool xedit_verify_integrity(xedit_buffer_t *b);

/* Undo/Redo */
int xedit_undo(xedit_buffer_t *b);
int xedit_redo(xedit_buffer_t *b);

/* Utility */
uint32_t xedit_buffer_size(xedit_buffer_t *b);
uint32_t xedit_gap_size(xedit_buffer_t *b);

#endif /* ZEDEC_XEDIT_H */
