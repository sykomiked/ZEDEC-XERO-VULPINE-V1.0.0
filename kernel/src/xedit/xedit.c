/*
 * xedit.c — X-EDIT Native Text & Code Editor Implementation
 *
 * Gap-buffer editor with real-time CID/Merkle tracking.
 * Supports modal and non-modal editing.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */

#ifdef TEST_HOST
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#else
#include "freestanding.h"
#endif

#include "xedit.h"

/* ===== Helpers ===== */

static void xe_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    uint32_t i;
    for (i = 0; i < n; i++) d[i] = (uint8_t)v;
}

static uint32_t xe_strlen(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static void xe_strcpy(char *dst, const char *src) {
    uint32_t i = 0;
    while (src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int xe_strcmp(const char *a, const char *b) {
    uint32_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        i++;
    }
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

static int xe_strendswith(const char *s, const char *suffix) {
    uint32_t slen = xe_strlen(s);
    uint32_t suflen = xe_strlen(suffix);
    if (suflen > slen) return 0;
    return xe_strcmp(s + slen - suflen, suffix) == 0;
}

/* ===== Simple hash (FNV-1a) for CID ===== */

static void xe_fnv1a(const char *data, uint32_t len, uint8_t *out) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    uint32_t i;
    for (i = 0; i < len; i++) {
        hash ^= (uint8_t)data[i];
        hash *= 0x100000001b3ULL;
    }
    for (i = 0; i < XEDIT_HASH_SIZE; i++) {
        out[i] = (uint8_t)(hash >> ((i % 8) * 8));
        if (i == 7) hash = hash * 0x100000001b3ULL ^ 0x5a;
    }
}

/* ===== Init ===== */

void xedit_init(xedit_t *e) {
    xe_memset(e, 0, sizeof(xedit_t));
    e->active_buffer = 0;
    e->num_buffers = 0;
    e->initialized = true;
}

/* ===== Buffer Management ===== */

int32_t xedit_buffer_create(xedit_t *e, const char *filename) {
    if (e->num_buffers >= XEDIT_MAX_BUFFERS)
        return -1;

    int32_t idx = (int32_t)e->num_buffers;
    xedit_buffer_t *b = &e->buffers[idx];
    xe_memset(b, 0, sizeof(xedit_buffer_t));

    /* Initialize gap at start */
    b->gap_start = 0;
    b->gap_end = XEDIT_BUF_SIZE;
    b->cursor = 0;
    b->mode = XEDIT_MODE_NORMAL;
    b->active = true;
    b->modified = false;
    b->hash_valid = false;
    b->line_count = 1;
    b->line_starts[0] = 0;
    b->undo_pos = 0;
    b->undo_count = 0;

    if (filename) {
        xe_strcpy(b->filename, filename);
        b->filetype = xedit_detect_filetype(filename);
    } else {
        xe_strcpy(b->filename, "[unnamed]");
        b->filetype = XEDIT_FILE_TEXT;
    }

    e->num_buffers++;
    return idx;
}

int xedit_buffer_close(xedit_t *e, uint32_t idx) {
    if (idx >= e->num_buffers) return -1;
    e->buffers[idx].active = false;
    return 0;
}

int xedit_switch_buffer(xedit_t *e, uint32_t idx) {
    if (idx >= e->num_buffers) return -1;
    e->active_buffer = idx;
    return 0;
}

/* ===== Gap Buffer Operations ===== */

static void gap_move_left(xedit_buffer_t *b) {
    if (b->gap_start > 0) {
        b->gap_start--;
        b->gap_end--;
        b->data[b->gap_end] = b->data[b->gap_start];
    }
}

static void gap_move_right(xedit_buffer_t *b) {
    if (b->gap_end < XEDIT_BUF_SIZE) {
        b->data[b->gap_start] = b->data[b->gap_end];
        b->gap_start++;
        b->gap_end++;
    }
}

static void gap_move_to(xedit_buffer_t *b, uint32_t pos) {
    while (b->gap_start > pos) gap_move_left(b);
    while (b->gap_start < pos) gap_move_right(b);
}

uint32_t xedit_gap_size(xedit_buffer_t *b) {
    return b->gap_end - b->gap_start;
}

uint32_t xedit_buffer_size(xedit_buffer_t *b) {
    return XEDIT_BUF_SIZE - xedit_gap_size(b);
}

int xedit_insert(xedit_buffer_t *b, const char *text, uint32_t len) {
    gap_move_to(b, b->cursor);

    /* Grow gap if needed */
    if (xedit_gap_size(b) < len + XEDIT_GAP_MIN) {
        /* In a real implementation, we'd realloc. For now, fail if no space. */
        if (xedit_gap_size(b) < len)
            return -1;
    }

    uint32_t i;
    for (i = 0; i < len; i++) {
        if (b->gap_start >= b->gap_end) break;
        b->data[b->gap_start++] = text[i];
    }
    b->cursor = b->gap_start;
    b->modified = true;
    b->hash_valid = false;

    /* Recompute line starts */
    b->line_count = 1;
    b->line_starts[0] = 0;
    uint32_t pos = 0;
    /* Walk the logical buffer (before gap + after gap) */
    for (i = 0; i < b->gap_start; i++) {
        if (b->data[i] == '\n') {
            if (b->line_count < XEDIT_MAX_LINES)
                b->line_starts[b->line_count++] = i + 1;
        }
        pos++;
    }
    for (i = b->gap_end; i < XEDIT_BUF_SIZE; i++) {
        if (b->data[i] == '\n') {
            if (b->line_count < XEDIT_MAX_LINES)
                b->line_starts[b->line_count++] = i + 1;
        }
        pos++;
    }

    return (int)len;
}

int xedit_insert_char(xedit_buffer_t *b, char c) {
    return xedit_insert(b, &c, 1);
}

int xedit_delete_back(xedit_buffer_t *b) {
    if (b->cursor == 0 || b->gap_start == 0)
        return -1;

    gap_move_to(b, b->cursor);
    b->gap_start--;
    b->cursor = b->gap_start;
    b->modified = true;
    b->hash_valid = false;
    return 0;
}

int xedit_delete_forward(xedit_buffer_t *b) {
    if (b->gap_end >= XEDIT_BUF_SIZE)
        return -1;

    gap_move_to(b, b->cursor);
    b->gap_end++;
    b->modified = true;
    b->hash_valid = false;
    return 0;
}

/* ===== Cursor Movement ===== */

int xedit_cursor_left(xedit_buffer_t *b) {
    if (b->cursor > 0) {
        b->cursor--;
        return 0;
    }
    return -1;
}

int xedit_cursor_right(xedit_buffer_t *b) {
    if (b->cursor < xedit_buffer_size(b)) {
        b->cursor++;
        return 0;
    }
    return -1;
}

int xedit_cursor_up(xedit_buffer_t *b) {
    uint32_t line = xedit_line_at_cursor(b);
    if (line == 0) return -1;
    uint32_t target_line = line - 1;
    if (target_line < b->line_count) {
        b->cursor = b->line_starts[target_line];
    }
    return 0;
}

int xedit_cursor_down(xedit_buffer_t *b) {
    uint32_t line = xedit_line_at_cursor(b);
    if (line + 1 >= b->line_count) return -1;
    b->cursor = b->line_starts[line + 1];
    return 0;
}

int xedit_cursor_line_start(xedit_buffer_t *b) {
    uint32_t line = xedit_line_at_cursor(b);
    b->cursor = b->line_starts[line];
    return 0;
}

int xedit_cursor_line_end(xedit_buffer_t *b) {
    uint32_t line = xedit_line_at_cursor(b);
    if (line + 1 < b->line_count) {
        b->cursor = b->line_starts[line + 1] - 1;
    } else {
        b->cursor = xedit_buffer_size(b);
    }
    return 0;
}

int xedit_cursor_goto(xedit_buffer_t *b, uint32_t pos) {
    if (pos > xedit_buffer_size(b)) return -1;
    b->cursor = pos;
    return 0;
}

/* ===== Text Retrieval ===== */

uint32_t xedit_get_text(xedit_buffer_t *b, char *out, uint32_t max_len) {
    uint32_t len = xedit_buffer_size(b);
    if (len > max_len) len = max_len;

    uint32_t before = b->gap_start;
    uint32_t after_start = b->gap_end;
    uint32_t after_len = XEDIT_BUF_SIZE - b->gap_end;

    uint32_t copy = before < len ? before : len;
    uint32_t i;
    for (i = 0; i < copy; i++) out[i] = b->data[i];

    if (copy < len) {
        uint32_t remaining = len - copy;
        if (remaining > after_len) remaining = after_len;
        for (i = 0; i < remaining; i++)
            out[copy + i] = b->data[after_start + i];
    }

    out[len] = '\0';
    return len;
}

uint32_t xedit_get_line(xedit_buffer_t *b, uint32_t line, char *out, uint32_t max_len) {
    if (line >= b->line_count) return 0;

    uint32_t start = b->line_starts[line];
    uint32_t end;
    if (line + 1 < b->line_count)
        end = b->line_starts[line + 1] - 1;  /* Exclude newline */
    else
        end = xedit_buffer_size(b);

    if (end > start + max_len) end = start + max_len;

    /* Read from logical buffer */
    uint32_t len = 0;
    uint32_t pos;
    for (pos = start; pos < end && len < max_len; pos++) {
        char c;
        if (pos < b->gap_start)
            c = b->data[pos];
        else if (pos >= b->gap_end)
            c = b->data[pos];
        else
            break;  /* In the gap — shouldn't happen */
        out[len++] = c;
    }
    out[len] = '\0';
    return len;
}

uint32_t xedit_line_at_cursor(xedit_buffer_t *b) {
    uint32_t line = 0;
    uint32_t i;
    for (i = 0; i < b->line_count; i++) {
        if (b->line_starts[i] <= b->cursor)
            line = i;
        else
            break;
    }
    return line;
}

uint32_t xedit_col_at_cursor(xedit_buffer_t *b) {
    uint32_t line = xedit_line_at_cursor(b);
    return b->cursor - b->line_starts[line];
}

/* ===== Mode Switching ===== */

void xedit_set_mode(xedit_buffer_t *b, xedit_mode_t mode) {
    b->mode = mode;
}

/* ===== File Type Detection ===== */

xedit_filetype_t xedit_detect_filetype(const char *filename) {
    if (xe_strendswith(filename, ".c") || xe_strendswith(filename, ".h"))
        return XEDIT_FILE_C_SOURCE;
    if (xe_strendswith(filename, ".sv") || xe_strendswith(filename, ".v"))
        return XEDIT_FILE_SVERILOG;
    if (xe_strendswith(filename, ".json") || xe_strendswith(filename, ".yaml"))
        return XEDIT_FILE_MANIFEST;
    if (xe_strendswith(filename, ".conf") || xe_strendswith(filename, ".cfg"))
        return XEDIT_FILE_CONFIG;
    if (xe_strendswith(filename, ".bin") || xe_strendswith(filename, ".36n9"))
        return XEDIT_FILE_BINARY;
    return XEDIT_FILE_TEXT;
}

const char *xedit_filetype_name(xedit_filetype_t ft) {
    switch (ft) {
        case XEDIT_FILE_C_SOURCE: return "C Source";
        case XEDIT_FILE_SVERILOG: return "SystemVerilog";
        case XEDIT_FILE_MANIFEST: return "Manifest";
        case XEDIT_FILE_CONFIG:   return "Config";
        case XEDIT_FILE_BINARY:   return "Binary";
        default:                  return "Text";
    }
}

/* ===== Integrity Tracking ===== */

void xedit_update_hash(xedit_buffer_t *b) {
    char text[XEDIT_BUF_SIZE];
    uint32_t len = xedit_get_text(b, text, XEDIT_BUF_SIZE - 1);
    xe_fnv1a(text, len, b->cid);

    /* Simple Merkle root: hash the CID again */
    xe_fnv1a((const char *)b->cid, XEDIT_HASH_SIZE, b->merkle_root);
    b->hash_valid = true;
}

bool xedit_verify_integrity(xedit_buffer_t *b) {
    if (!b->hash_valid) return false;
    uint8_t saved_cid[XEDIT_HASH_SIZE];
    uint8_t saved_merkle[XEDIT_HASH_SIZE];
    uint32_t i;
    for (i = 0; i < XEDIT_HASH_SIZE; i++) {
        saved_cid[i] = b->cid[i];
        saved_merkle[i] = b->merkle_root[i];
    }
    xedit_update_hash(b);
    for (i = 0; i < XEDIT_HASH_SIZE; i++) {
        if (b->cid[i] != saved_cid[i]) return false;
        if (b->merkle_root[i] != saved_merkle[i]) return false;
    }
    return true;
}

/* ===== Undo/Redo ===== */

int xedit_undo(xedit_buffer_t *b) {
    if (b->undo_pos == 0) return -1;
    b->undo_pos--;
    /* Simplified undo: just mark as modified */
    b->modified = true;
    b->hash_valid = false;
    return 0;
}

int xedit_redo(xedit_buffer_t *b) {
    if (b->undo_pos >= b->undo_count) return -1;
    b->undo_pos++;
    b->modified = true;
    b->hash_valid = false;
    return 0;
}
