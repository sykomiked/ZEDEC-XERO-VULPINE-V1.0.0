/* doc.c — ZXV AppKit document model. See doc.h. */
#include "doc.h"

/* ---- small local helpers (freestanding: no libc) ---- */
static void dmemcpy(char *d, const char *s, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}
static void dmemmove(char *d, const char *s, uint32_t n) {
    if (d < s) { for (uint32_t i = 0; i < n; i++) d[i] = s[i]; }
    else       { for (uint32_t i = n; i > 0; i--) d[i-1] = s[i-1]; }
}
static uint32_t dstrlen(const char *s) {
    uint32_t n = 0; while (s && s[n]) n++; return n;
}

/* ---- lifecycle ---- */
void doc_init(doc_t *d, const char *name) {
    if (!d) return;
    d->gap_start = 0;
    d->gap_end = DOC_CAPACITY;
    d->dirty = false;
    d->undo_count = d->redo_count = 0;
    d->coalescing = false;
    d->last_insert_end = 0;
    d->total_edits = 0;
    uint32_t i = 0;
    if (name) while (i < DOC_NAME_LEN - 1 && name[i]) { d->name[i] = name[i]; i++; }
    d->name[i] = '\0';
}

uint32_t doc_length(const doc_t *d) {
    if (!d) return 0;
    return d->gap_start + (DOC_CAPACITY - d->gap_end);
}

bool doc_is_dirty(const doc_t *d) { return d && d->dirty; }

char doc_char_at(const doc_t *d, uint32_t idx) {
    if (!d || idx >= doc_length(d)) return 0;
    return (idx < d->gap_start) ? d->buf[idx]
                                : d->buf[idx - d->gap_start + d->gap_end];
}

uint32_t doc_text(const doc_t *d, char *out, uint32_t max) {
    if (!d || !out) return 0;
    uint32_t n = doc_length(d);
    if (n > max) n = max;
    for (uint32_t i = 0; i < n; i++) out[i] = doc_char_at(d, i);
    return n;
}

/* Move the gap so it begins at `pos`. */
static void gap_move(doc_t *d, uint32_t pos) {
    if (pos == d->gap_start) return;
    if (pos < d->gap_start) {
        uint32_t n = d->gap_start - pos;
        dmemmove(&d->buf[d->gap_end - n], &d->buf[pos], n);
        d->gap_start -= n; d->gap_end -= n;
    } else {
        uint32_t n = pos - d->gap_start;
        dmemmove(&d->buf[d->gap_start], &d->buf[d->gap_end], n);
        d->gap_start += n; d->gap_end += n;
    }
}

/* ---- history ---- */
static void push_undo(doc_t *d, doc_op_t op, uint32_t pos,
                      const char *text, uint32_t len) {
    if (len > DOC_UNDO_TEXT) len = DOC_UNDO_TEXT;
    if (d->undo_count == DOC_UNDO_DEPTH) {
        /* drop the oldest record so history stays bounded */
        for (uint32_t i = 1; i < DOC_UNDO_DEPTH; i++) d->undo[i-1] = d->undo[i];
        d->undo_count--;
    }
    doc_edit_t *e = &d->undo[d->undo_count++];
    e->op = op; e->pos = pos; e->len = len;
    if (text) dmemcpy(e->text, text, len);
}

void doc_break_coalesce(doc_t *d) { if (d) d->coalescing = false; }

/* ---- editing ---- */
bool doc_insert(doc_t *d, uint32_t pos, const char *s, uint32_t len) {
    if (!d || !s) return false;
    if (len == 0) return true;
    uint32_t used = doc_length(d);
    if (pos > used) return false;
    if (used + len > DOC_CAPACITY) return false;    /* fail, never truncate */

    /* Coalesce a run of typing into the previous undo record so one
     * undo removes the whole run rather than a single keystroke. */
    bool folded = false;
    if (d->coalescing && d->undo_count > 0) {
        doc_edit_t *top = &d->undo[d->undo_count - 1];
        if (top->op == DOC_OP_INSERT && pos == d->last_insert_end &&
            top->len + len <= DOC_UNDO_TEXT) {
            dmemcpy(&top->text[top->len], s, len);
            top->len += len;
            folded = true;
        }
    }
    if (!folded) push_undo(d, DOC_OP_INSERT, pos, s, len);

    gap_move(d, pos);
    dmemcpy(&d->buf[d->gap_start], s, len);
    d->gap_start += len;

    d->coalescing = true;
    d->last_insert_end = pos + len;
    d->redo_count = 0;              /* a new edit invalidates redo */
    d->dirty = true;
    d->total_edits++;
    return true;
}

bool doc_delete(doc_t *d, uint32_t pos, uint32_t len) {
    if (!d || len == 0) return true;
    uint32_t used = doc_length(d);
    if (pos >= used) return false;
    if (pos + len > used) len = used - pos;

    /* capture the removed text so undo can restore it */
    char saved[DOC_UNDO_TEXT];
    uint32_t keep = (len > DOC_UNDO_TEXT) ? DOC_UNDO_TEXT : len;
    for (uint32_t i = 0; i < keep; i++) saved[i] = doc_char_at(d, pos + i);
    push_undo(d, DOC_OP_DELETE, pos, saved, keep);

    gap_move(d, pos);
    d->gap_end += len;              /* absorb the text into the gap */

    d->coalescing = false;
    d->redo_count = 0;
    d->dirty = true;
    d->total_edits++;
    return true;
}

/* Apply an edit record without touching history (used by undo/redo). */
static void apply_raw(doc_t *d, doc_op_t op, uint32_t pos,
                      const char *text, uint32_t len) {
    if (op == DOC_OP_INSERT) {
        gap_move(d, pos);
        dmemcpy(&d->buf[d->gap_start], text, len);
        d->gap_start += len;
    } else if (op == DOC_OP_DELETE) {
        gap_move(d, pos);
        d->gap_end += len;
    }
}

bool doc_undo(doc_t *d) {
    if (!d || d->undo_count == 0) return false;
    doc_edit_t e = d->undo[--d->undo_count];

    if (e.op == DOC_OP_INSERT) {
        apply_raw(d, DOC_OP_DELETE, e.pos, 0, e.len);      /* remove it */
    } else {
        apply_raw(d, DOC_OP_INSERT, e.pos, e.text, e.len); /* put it back */
    }
    if (d->redo_count < DOC_UNDO_DEPTH) d->redo[d->redo_count++] = e;
    d->coalescing = false;
    d->dirty = true;
    return true;
}

bool doc_redo(doc_t *d) {
    if (!d || d->redo_count == 0) return false;
    doc_edit_t e = d->redo[--d->redo_count];

    if (e.op == DOC_OP_INSERT) {
        apply_raw(d, DOC_OP_INSERT, e.pos, e.text, e.len);
    } else {
        apply_raw(d, DOC_OP_DELETE, e.pos, 0, e.len);
    }
    if (d->undo_count < DOC_UNDO_DEPTH) d->undo[d->undo_count++] = e;
    d->coalescing = false;
    d->dirty = true;
    return true;
}

/* ---- search ---- */
int32_t doc_find(const doc_t *d, const char *needle, uint32_t from) {
    if (!d || !needle) return -1;
    uint32_t nl = dstrlen(needle);
    if (nl == 0) return -1;
    uint32_t len = doc_length(d);
    if (nl > len) return -1;
    for (uint32_t i = from; i + nl <= len; i++) {
        uint32_t k = 0;
        while (k < nl && doc_char_at(d, i + k) == needle[k]) k++;
        if (k == nl) return (int32_t)i;
    }
    return -1;
}

/* ---- lines ---- */
uint32_t doc_line_count(const doc_t *d) {
    if (!d) return 0;
    uint32_t len = doc_length(d);
    if (len == 0) return 1;
    uint32_t lines = 1;
    for (uint32_t i = 0; i < len; i++)
        if (doc_char_at(d, i) == '\n') lines++;
    return lines;
}

uint32_t doc_line_start(const doc_t *d, uint32_t line) {
    if (!d || line == 0) return 0;
    uint32_t len = doc_length(d), seen = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (doc_char_at(d, i) == '\n') {
            if (++seen == line) return i + 1;
        }
    }
    return len;
}
