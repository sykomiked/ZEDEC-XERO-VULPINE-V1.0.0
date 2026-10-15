/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* escl.h — eSCL ("AirScan"-style driverless scanning over HTTP, Mopria
 * eSCL 2.x) for Platen: the ScanSettings a client posts to /eSCL/ScanJobs,
 * and what it reads back from /eSCL/ScannerCapabilities and
 * /eSCL/ScannerStatus.
 *
 *   E1 NO ALLOCATION. Writers fill a caller's buffer; readers copy into
 *      fixed fields, cut on UTF-8 boundaries.
 *   E2 ELEMENTS BY LOCAL NAME. The reader matches an element's name after
 *      any namespace prefix (pwg:, scan:), which is how scanners differ;
 *      it reads only element text, never attributes, entities beyond the
 *      five XML ones, or DTDs.
 *   E3 INTEGERS ONLY. Sizes are in 1/300 inch, as eSCL counts them.
 *
 * The transport (HTTP GET/POST/DELETE) is the host's:
 * kernel/arch/hosted/zxv_print_host.c. */
#ifndef ZXV_ESCL_H
#define ZXV_ESCL_H

#include <stdbool.h>
#include <stdint.h>

enum { ESCL_OK = 0, ESCL_E_ROOM = -1, ESCL_E_FORMAT = -2, ESCL_E_ARG = -3 };

enum { ESCL_SRC_PLATEN, ESCL_SRC_FEEDER };
enum { ESCL_COLOR_RGB, ESCL_COLOR_GRAY, ESCL_COLOR_BW };

typedef struct {
    uint8_t source;     /* ESCL_SRC_* */
    uint8_t color;      /* ESCL_COLOR_* */
    uint32_t dpi;       /* 75 to 1200 */
    uint32_t width;     /* 1/300 inch; 0: A4's 2480 */
    uint32_t height;    /* 1/300 inch; 0: A4's 3508 */
    const char *format; /* "image/jpeg", "application/pdf" or "image/png" */
} escl_settings_t;

/* The ScanSettings document. ESCL_E_ARG for a resolution or format out of
 * range, ESCL_E_ROOM when cap is too small. *len excludes the NUL. */
int escl_settings(const escl_settings_t *s, char *out, uint32_t cap, uint32_t *len);

#define ESCL_LIST 8u
#define ESCL_WORD 32u
#define ESCL_TEXT 128u

typedef struct {
    char make[ESCL_TEXT]; /* MakeAndModel */
    bool platen, feeder;
    uint32_t dpi[ESCL_LIST]; /* XResolution of each DiscreteResolution, as listed */
    uint32_t ndpi;
    char colors[ESCL_LIST][ESCL_WORD]; /* ColorMode: RGB24, Grayscale8, BlackAndWhite1 */
    uint32_t ncolors;
    char formats[ESCL_LIST][ESCL_WORD]; /* DocumentFormatExt, else DocumentFormat */
    uint32_t nformats;
    uint32_t max_width, max_height; /* the platen's, 1/300 inch; 0 = not given */
} escl_caps_t;

typedef struct {
    char state[ESCL_WORD];     /* pwg:State: Idle, Processing, Testing, Stopped, Down */
    char adf_state[ESCL_WORD]; /* AdfState, "" when there is no feeder */
    uint32_t jobs;             /* JobInfo elements */
} escl_status_t;

int escl_parse_caps(const char *xml, uint32_t len, escl_caps_t *c);
int escl_parse_status(const char *xml, uint32_t len, escl_status_t *s);

/* Element text by local name (E2): the first match at or after *from, with
 * *from moved past it; false when there is none. Exposed for tests and for
 * reading an element the summaries do not keep. */
bool escl_text(const char *xml, uint32_t len, uint32_t *from, const char *name, char *out,
               uint32_t cap);

/* The job's path from a ScanJobs reply's Location header value
 * ("http://host/eSCL/ScanJobs/42" or "/eSCL/ScanJobs/42"): "/eSCL/ScanJobs/42".
 * false when it is not one. */
bool escl_job_path(const char *location, char *out, uint32_t cap);

#endif
