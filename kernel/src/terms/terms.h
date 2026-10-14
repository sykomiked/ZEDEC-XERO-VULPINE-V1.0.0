/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * DISCLAIMER: This module shows an agreement and records that a person
 * accepted it. It does not provide legal advice, make legal determinations,
 * or decide whether an agreement is enforceable anywhere. The agreement text
 * itself is prepared and reviewed outside the code.
 */
/* terms.h — the first-boot end-user agreement gate.
 *
 * Every person on a device sees the agreement at their first sign-in, after
 * the boot vault opens and before their desktop. The login screen
 * (zui_login) owns the drawing; this module owns everything that must be
 * the same on every screen and every port:
 *
 *   - the text (UTF-8, one paragraph per line) and its fingerprint, which is
 *     plain SHA-256 of the exact bytes, so `sha256sum` on the shipped file
 *     gives the same value;
 *   - wrapping it to the screen (codepoint columns, East Asian wide
 *     characters count two, CJK breaks between characters) and re-wrapping
 *     when the person changes text size, without losing their place;
 *   - the read gate: "I agree" is enabled only once the last line has been
 *     on screen. Any route counts the same: wheel, touch, scroll bar,
 *     keyboard (arrows, Page Down, End), the section list, or a screen
 *     reader reporting how far it has spoken. There is no timer;
 *   - [KEY] sections: a heading ending in " [KEY]" marks a clause that
 *     limits liability or decides disputes. Its lines are flagged so the
 *     screen draws them highlighted; the marker itself is not shown;
 *   - the acceptance record: one text line per acceptance, holding an
 *     opaque seat id (never a name), version, language, the fingerprint of
 *     the text shown and of the reference (English) text, the time, how the
 *     end was reached, and a SHA-256 chain to the previous record so the
 *     on-device log is tamper-evident. Declining writes nothing.
 *
 * Integer only, no allocation, no floats. The host supplies storage and the
 * clock. */
#ifndef ZXV_TERMS_H
#define ZXV_TERMS_H

#include <stddef.h>
#include <stdint.h>

#define TERMS_HASH_LEN     32
#define TERMS_MAX_LINES    4096 /* wrapped lines; ~25 KB of text at 20 columns */
#define TERMS_MAX_SECTIONS 64
#define TERMS_SEAT_MAX     48 /* seat id, [A-Za-z0-9._-], NUL included */
#define TERMS_TAG_MAX      16 /* version or BCP 47 language tag, NUL included */
#define TERMS_MIN_COLS     12
#define TERMS_RECORD_MAX   400 /* one formatted record line, NUL included */

/* How the end of the text was reached (bit mask, recorded for evidence). */
enum {
    TERMS_VIA_SCROLL = 1u << 0,   /* wheel, touch, scroll bar */
    TERMS_VIA_KEYBOARD = 1u << 1, /* arrows, Page Down, End */
    TERMS_VIA_SECTION = 1u << 2,  /* section list / table of contents */
    TERMS_VIA_READER = 1u << 3,   /* screen reader spoke to the end */
    TERMS_VIA_FIT = 1u << 4       /* the whole text fit on one screen */
};

enum { /* terms_line_t.kind */
       TERMS_LINE_BODY = 0,
       TERMS_LINE_BLANK,
       TERMS_LINE_TITLE,
       TERMS_LINE_HEADING
};

enum { /* return codes */
       TERMS_OK = 0,
       TERMS_ERR_ARG = -1,
       TERMS_ERR_FULL = -2, /* more than TERMS_MAX_LINES wrapped lines */
       TERMS_ERR_UTF8 = -3, /* text is not valid UTF-8 */
       TERMS_ERR_GATE = -4, /* accept before the end was shown */
       TERMS_ERR_PARSE = -5,
       TERMS_ERR_CHAIN = -6 /* record hash or chain link is wrong */
};

typedef struct terms_doc {
    const char *version; /* "1.0" */
    const char *lang;    /* BCP 47, "en", "zh-Hans" */
    const char *text;    /* UTF-8, '\n' between paragraphs */
    size_t len;
    int controlling;                   /* 1: this language version governs */
    const struct terms_doc *reference; /* English reference, NULL if this is it */
} terms_doc_t;

typedef struct {
    uint32_t off;     /* byte offset into doc->text */
    uint16_t len;     /* bytes shown (a heading's " [KEY]" is cut) */
    uint16_t cols;    /* display columns used */
    uint8_t kind;     /* TERMS_LINE_* */
    uint8_t key;      /* 1: inside a [KEY] section, draw highlighted */
    uint16_t section; /* 0 before the first numbered heading */
} terms_line_t;

/* Optional width of one codepoint in the host's units (pixels from the real
 * font). Without one, widths are character cells from terms_cp_width. */
typedef uint16_t (*terms_measure_fn)(void *ctx, uint32_t cp);

typedef struct {
    const terms_doc_t *doc;
    terms_measure_fn measure;
    void *measure_ctx;
    terms_line_t lines[TERMS_MAX_LINES];
    uint32_t nlines;
    uint16_t cols, rows; /* text area in character cells */
    uint32_t top;        /* first visible line */
    uint32_t seen;       /* furthest byte offset shown or spoken */
    uint8_t reached_end;
    uint8_t via; /* TERMS_VIA_* that reached the end */
    uint8_t declined;
    uint16_t nsections;
    uint16_t section_no[TERMS_MAX_SECTIONS];   /* heading number as written */
    uint32_t section_line[TERMS_MAX_SECTIONS]; /* first wrapped line */
    uint8_t section_key[TERMS_MAX_SECTIONS];
} terms_reader_t;

/* What the login screen draws this frame. */
typedef struct {
    uint32_t first, count; /* visible lines: lines[first .. first+count) */
    uint16_t scroll256;    /* scroll bar thumb position, 0..256 */
    uint16_t thumb256;     /* thumb length, 1..256 */
    uint8_t can_accept;
    uint8_t hint; /* TERMS_HINT_* for the line above the buttons */
} terms_view_t;

enum {
    TERMS_HINT_READ = 0, /* "Read to the end to continue." */
    TERMS_HINT_READY,    /* "You've reached the end." */
    TERMS_HINT_DECLINED  /* "You declined. Nothing was recorded." */
};

/* ---- text ---- */
void terms_hash(const terms_doc_t *doc, uint8_t out[TERMS_HASH_LEN]);
int terms_utf8_valid(const char *s, size_t n);
/* Display width of one codepoint: 0 for combining marks, 2 for East Asian
 * wide/fullwidth, 1 otherwise. */
int terms_cp_width(uint32_t cp);

/* ---- reading ---- */
int terms_reader_init(terms_reader_t *r, const terms_doc_t *doc, uint16_t cols, uint16_t rows);
/* Same, wrapping to `width` in the units `measure` returns (e.g. pixels).
 * `rows` is still the number of lines that fit. */
int terms_reader_init_ex(terms_reader_t *r, const terms_doc_t *doc, uint16_t width, uint16_t rows,
                         terms_measure_fn measure, void *ctx);
/* Text size or window changed: re-wrap, keep the first visible paragraph
 * position and everything already seen. */
int terms_reader_resize(terms_reader_t *r, uint16_t cols, uint16_t rows);
void terms_scroll(terms_reader_t *r, int32_t lines, unsigned via);
void terms_page(terms_reader_t *r, int dir); /* keyboard Page Up/Down */
void terms_home(terms_reader_t *r);
void terms_end(terms_reader_t *r); /* keyboard End */
/* Jump to the section with heading number `no`; returns 0 or TERMS_ERR_ARG. */
int terms_goto_section(terms_reader_t *r, uint16_t no);
/* A screen reader has spoken up to byte `off`; the view follows it. */
void terms_reader_spoke(terms_reader_t *r, uint32_t off);
void terms_decline(terms_reader_t *r);
int terms_can_accept(const terms_reader_t *r);
terms_view_t terms_view(const terms_reader_t *r);
/* Copy line i's visible text (NUL-terminated); returns bytes written. */
size_t terms_line_text(const terms_reader_t *r, uint32_t i, char *out, size_t cap);
/* Plain-text frame for consoles and the terminal: visible lines, key
 * lines prefixed "▌ ", then a hint and the two choices. */
size_t terms_render_text(const terms_reader_t *r, char *out, size_t cap);

/* ---- acceptance records ---- */
typedef struct {
    char seat[TERMS_SEAT_MAX];
    char version[TERMS_TAG_MAX];
    char lang[TERMS_TAG_MAX];
    uint8_t shown[TERMS_HASH_LEN]; /* fingerprint of the text shown */
    uint8_t ref[TERMS_HASH_LEN];   /* fingerprint of the reference text */
    uint8_t controlling;           /* the shown language version governs */
    uint8_t via;
    uint64_t time;                /* Unix seconds, from the host clock */
    uint8_t prev[TERMS_HASH_LEN]; /* previous record's self, zero for the first */
    uint8_t self[TERMS_HASH_LEN]; /* SHA-256 of the line before " rec=" */
} terms_record_t;

int terms_seat_valid(const char *seat);
/* The person chose "I agree": fill a record. Fails with TERMS_ERR_GATE if
 * the end has not been shown or they declined. `prev` is the log tip from
 * terms_log_verify (NULL or zero for an empty log). */
int terms_accept(const terms_reader_t *r, const char *seat, uint64_t time,
                 const uint8_t prev[TERMS_HASH_LEN], terms_record_t *rec);
/* One line, '\n' terminated:
 * ZXVTERMS1 seat=S ver=V lang=L ctl=0|1 via=N t=T shown=HEX ref=HEX prev=HEX rec=HEX */
size_t terms_record_format(const terms_record_t *rec, char *out, size_t cap);
int terms_record_parse(const char *line, size_t len, terms_record_t *rec);

enum {                           /* terms_log_status results */
       TERMS_STATUS_NEEDED = 0,  /* this seat never accepted */
       TERMS_STATUS_CURRENT = 1, /* accepted exactly this text */
       TERMS_STATUS_OUTDATED = 2 /* accepted an earlier text: show the new one */
};
/* Check every line's hash and chain link. Returns the number of records, or
 * a negative error; on error *bad_line is the 1-based line that failed.
 * `tip` receives the last record's self (zero for an empty log). */
int terms_log_verify(const char *log, size_t len, uint8_t tip[TERMS_HASH_LEN], uint32_t *bad_line);
/* Does `seat` have to see `doc` at sign-in? Matches the reference
 * fingerprint, so accepting in any language counts for every language of the
 * same version. A log that fails verification returns its negative error:
 * the screen then shows the agreement again rather than trusting it. */
int terms_log_status(const char *log, size_t len, const char *seat, const terms_doc_t *doc);

/* The built-in agreement compiled into the image (terms_text.c, generated
 * by tools/terms/embed_terms.py). terms_builtin is the reference text;
 * terms_builtin_lang picks the version for a BCP 47 tag ("zh-Hans-CN"
 * matches "zh-Hans") and falls back to the reference. */
const terms_doc_t *terms_builtin(void);
const terms_doc_t *terms_builtin_lang(const char *lang);

#endif /* ZXV_TERMS_H */
