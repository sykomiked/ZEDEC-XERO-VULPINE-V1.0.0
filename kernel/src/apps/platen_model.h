/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* platen_model.h — Platen, printers and scanners, as a model (zxm.h): the
 * owner's printers and scanners, what each takes, printing a file from the
 * ZXV folder and following its job, and scanning pages into a folder
 * (zxvd's print.* and scan.*). AI can explain a printer's trouble in plain
 * words, held until read (M3). No drawing, no allocation. */
#ifndef PLATEN_MODEL_H
#define PLATEN_MODEL_H

#include "zxm.h"

#define PLATEN_DEVICES 8u
#define PLATEN_JOBS    16u
#define PLATEN_URI     511u
#define PLATEN_WORDS   8u /* formats and sides kept per printer */
#define PLATEN_WORD    48u
#define PLATEN_TEXT    128u
#define PLATEN_PAGES   50u

typedef struct {
    char uri[PLATEN_URI + 1];
    bool known; /* the last status call succeeded */
    char name[PLATEN_TEXT], make[PLATEN_TEXT];
    char state[16]; /* "idle", "printing", "stopped", "unknown" */
    char reasons[256];
    bool accepting, color;
    char formats[PLATEN_WORDS][PLATEN_WORD];
    uint32_t nformats;
    char sides[PLATEN_WORDS][PLATEN_WORD];
    uint32_t nsides;
} platen_printer_t;

typedef struct {
    uint32_t printer; /* index at the time it was sent */
    int32_t id;
    char state[16];
    char message[PLATEN_TEXT];
    int32_t pages;
    char path[256]; /* what was printed, cut to fit */
} platen_job_t;

typedef struct {
    char url[PLATEN_URI + 1];
    bool known;
    char make[PLATEN_TEXT];
    char state[32];
    bool platen, feeder;
    uint32_t dpi[8];
    uint32_t ndpi;
} platen_scanner_t;

typedef struct {
    zxm_link_t *link;
    char space[49]; /* where files are read and pages saved; "" = home */
    platen_printer_t printer[PLATEN_DEVICES];
    uint32_t nprinters;
    platen_scanner_t scanner[PLATEN_DEVICES];
    uint32_t nscanners;
    platen_job_t job[PLATEN_JOBS]; /* newest last; the oldest goes when full */
    uint32_t njobs;
    char saved[PLATEN_PAGES][256]; /* the last scan's pages */
    uint32_t nsaved;
    zxm_held_t held;
} platen_t;

enum { PLATEN_AI_EXPLAIN, PLATEN_AI_COUNT };
extern const zxm_ai_action_t PLATEN_AI[PLATEN_AI_COUNT];

void platen_init(platen_t *p, zxm_link_t *link);
int platen_set_space(platen_t *p, const char *space);

/* Add a printer by its address and read its status; a printer that does
 * not answer is not added. ZXM_E_STATE when the list is full or it is
 * already there. */
int platen_add_printer(platen_t *p, const char *uri);
int platen_refresh_printer(platen_t *p, uint32_t i); /* a failure marks it not known */
int platen_remove_printer(platen_t *p, uint32_t i);
/* The document format for a file name, and whether printer i takes it. */
const char *platen_format_of(const char *name);
bool platen_takes(const platen_t *p, uint32_t i, const char *name);

typedef struct {
    uint32_t copies;   /* 0: the printer's default */
    const char *sides; /* NULL: default */
    bool mono;         /* print-color-mode monochrome */
} platen_opts_t;

/* Print a file of the space; the job joins the list. */
int platen_print(platen_t *p, uint32_t printer, const char *path, const platen_opts_t *o);
/* Ask each job not yet finished how it is doing. */
int platen_follow(platen_t *p);
int platen_cancel(platen_t *p, uint32_t job);
bool platen_job_done(const platen_job_t *j); /* completed, canceled or aborted */

int platen_add_scanner(platen_t *p, const char *url);
int platen_remove_scanner(platen_t *p, uint32_t i);
typedef struct {
    bool feeder;
    const char *color;  /* "color", "gray", "bw"; NULL: color */
    uint32_t dpi;       /* 0: 300 */
    const char *format; /* NULL: image/jpeg */
} platen_scan_t;
/* Scan into `folder` of the space; the saved paths are in p->saved. */
int platen_scan(platen_t *p, uint32_t scanner, const char *folder, const platen_scan_t *s);

/* AI: what printer i's state reasons mean and what to do (held, M3). */
int platen_ai(platen_t *p, int32_t action, uint32_t printer);
int platen_ai_accept(platen_t *p);
int platen_ai_reject(platen_t *p);

#endif
