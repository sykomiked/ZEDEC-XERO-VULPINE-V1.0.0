/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ipp.h — the Internet Printing Protocol's message encoding (RFC 8010, IPP
 * 2.0) for Platen, ZXV's printers and scanners utility: the requests an IPP
 * Everywhere client sends and the parts of the replies it reads.
 *
 *   P1 NO ALLOCATION. Writers fill a caller's buffer and readers point into
 *      the caller's bytes. A writer that runs out of room sets `over` and
 *      writes nothing more; the caller checks it once at the end.
 *   P2 BOUNDED READS. Every length is checked against what is left; a
 *      message that does not add up is refused (IPP_E_FORMAT), never read
 *      past.
 *   P3 INTEGERS ONLY. Resolutions and ranges are kept as integers.
 *   P4 WHAT A CLIENT NEEDS. The summaries keep the printer and job
 *      attributes a print dialog shows; ipp_next walks every attribute for
 *      anything else. Collections are skipped whole by the summaries.
 *
 * The transport (HTTP POST to the printer's URI with Content-Type
 * application/ipp) is the host's: kernel/arch/hosted/zxv_print_host.c. */
#ifndef ZXV_IPP_H
#define ZXV_IPP_H

#include <stdbool.h>
#include <stdint.h>

enum { IPP_OK = 0, IPP_E_FORMAT = -1, IPP_E_ROOM = -2 };

/* operations (RFC 8011 5.2, 5.3) */
enum {
    IPP_OP_PRINT_JOB = 0x0002,
    IPP_OP_VALIDATE_JOB = 0x0004,
    IPP_OP_CANCEL_JOB = 0x0008,
    IPP_OP_GET_JOB_ATTRIBUTES = 0x0009,
    IPP_OP_GET_PRINTER_ATTRIBUTES = 0x000B,
};

/* delimiter tags (groups) */
enum {
    IPP_G_OPERATION = 0x01,
    IPP_G_JOB = 0x02,
    IPP_G_END = 0x03,
    IPP_G_PRINTER = 0x04,
    IPP_G_UNSUPPORTED = 0x05,
};

/* value tags */
enum {
    IPP_T_UNSUPPORTED = 0x10,
    IPP_T_UNKNOWN = 0x12,
    IPP_T_NO_VALUE = 0x13,
    IPP_T_INTEGER = 0x21,
    IPP_T_BOOLEAN = 0x22,
    IPP_T_ENUM = 0x23,
    IPP_T_OCTET = 0x30,
    IPP_T_DATETIME = 0x31,
    IPP_T_RESOLUTION = 0x32,
    IPP_T_RANGE = 0x33,
    IPP_T_BEGIN_COLLECTION = 0x34,
    IPP_T_END_COLLECTION = 0x37,
    IPP_T_TEXT = 0x41,
    IPP_T_NAME = 0x42,
    IPP_T_KEYWORD = 0x44,
    IPP_T_URI = 0x45,
    IPP_T_CHARSET = 0x47,
    IPP_T_LANGUAGE = 0x48,
    IPP_T_MIME = 0x49,
    IPP_T_MEMBER_NAME = 0x4A,
};

/* status codes: 0x0000-0x00FF are successes */
#define IPP_STATUS_OK(s) ((s) <= 0x00FFu)

/* printer-state and job-state (RFC 8011 5.4.11, 5.3.7) */
enum { IPP_PRINTER_IDLE = 3, IPP_PRINTER_PROCESSING = 4, IPP_PRINTER_STOPPED = 5 };
enum {
    IPP_JOB_PENDING = 3,
    IPP_JOB_HELD = 4,
    IPP_JOB_PROCESSING = 5,
    IPP_JOB_STOPPED = 6,
    IPP_JOB_CANCELED = 7,
    IPP_JOB_ABORTED = 8,
    IPP_JOB_COMPLETED = 9,
};

/* ===================== writing ===================== */

typedef struct {
    uint8_t *p;
    uint32_t len, cap;
    bool over; /* ran out of room (P1) */
} ipp_w_t;

void ipp_w_init(ipp_w_t *w, uint8_t *buf, uint32_t cap);
/* version 2.0, the operation and a request id */
void ipp_w_head(ipp_w_t *w, uint16_t op, uint32_t request_id);
void ipp_w_group(ipp_w_t *w, uint8_t tag);
/* name NULL: another value of the attribute just written */
void ipp_w_str(ipp_w_t *w, uint8_t tag, const char *name, const char *v);
void ipp_w_int(ipp_w_t *w, uint8_t tag, const char *name, int32_t v); /* INTEGER, ENUM */
void ipp_w_bool(ipp_w_t *w, const char *name, bool v);

/* The requests a client sends, operation attributes first: charset utf-8,
 * natural language en, the printer's URI and the requesting user. */
typedef struct {
    const char *uri;  /* ipp://host:631/ipp/print */
    const char *user; /* requesting-user-name */
} ipp_target_t;

typedef struct {
    const char *job_name;
    const char *format; /* document-format, e.g. "application/pdf" */
    int32_t copies;     /* 0: the printer's default */
    const char *sides;  /* "one-sided", "two-sided-long-edge", ...; NULL: default */
    const char *color;  /* print-color-mode: "color", "monochrome"; NULL: default */
    const char *media;  /* e.g. "iso_a4_210x297mm"; NULL: default */
} ipp_job_opts_t;

int ipp_req_printer(ipp_w_t *w, uint32_t id, const ipp_target_t *t);
/* the job's document follows the returned bytes in the same HTTP body */
int ipp_req_print_job(ipp_w_t *w, uint32_t id, const ipp_target_t *t, const ipp_job_opts_t *o);
int ipp_req_validate_job(ipp_w_t *w, uint32_t id, const ipp_target_t *t, const ipp_job_opts_t *o);
/* IPP_OP_GET_JOB_ATTRIBUTES or IPP_OP_CANCEL_JOB */
int ipp_req_job(ipp_w_t *w, uint16_t op, uint32_t id, const ipp_target_t *t, int32_t job_id);

/* ===================== reading ===================== */

typedef struct {
    const uint8_t *p;
    uint32_t len, off;
    uint8_t major, minor;
    uint16_t status; /* or the operation, in a request */
    uint32_t request_id;
    uint8_t group; /* the group being read */
    bool done;     /* the end tag was read */
} ipp_r_t;

typedef struct {
    uint8_t group, tag;
    const char *name; /* not terminated; name_len 0 for another value */
    uint16_t name_len;
    const uint8_t *val;
    uint16_t val_len;
} ipp_attr_t;

int ipp_r_init(ipp_r_t *r, const uint8_t *msg, uint32_t len);
/* 1 and the next attribute, 0 at the end, or IPP_E_FORMAT. A collection
 * comes back as one BEGIN_COLLECTION attribute; its members are skipped. */
int ipp_next(ipp_r_t *r, ipp_attr_t *a);
/* Where the attribute data ends: a request's document starts here. */
uint32_t ipp_r_end(const ipp_r_t *r);

bool ipp_name_is(const ipp_attr_t *a, const char *name);
/* INTEGER, ENUM or BOOLEAN as a number; false for any other tag or size */
bool ipp_int(const ipp_attr_t *a, int32_t *v);
/* text-like values copied and terminated, cut on a UTF-8 boundary */
void ipp_str(const ipp_attr_t *a, char *out, uint32_t cap);

/* ===================== summaries ===================== */

#define IPP_LIST  16u /* values kept of a list attribute */
#define IPP_WORD  48u /* bytes of one keyword or MIME type */
#define IPP_TEXT  128u
#define IPP_STATE 256u /* printer-state-reasons, comma separated */

typedef struct {
    uint16_t status;
    char name[IPP_TEXT];  /* printer-name */
    char info[IPP_TEXT];  /* printer-info */
    char make[IPP_TEXT];  /* printer-make-and-model */
    char where[IPP_TEXT]; /* printer-location */
    int32_t state;        /* IPP_PRINTER_*; 0 = not given */
    char reasons[IPP_STATE];
    bool accepting; /* printer-is-accepting-jobs */
    bool color;     /* color-supported */
    int32_t queued; /* queued-job-count; -1 = not given */
    char formats[IPP_LIST][IPP_WORD];
    uint32_t nformats;
    char media[IPP_LIST][IPP_WORD];
    uint32_t nmedia;
    char media_default[IPP_WORD];
    char sides[IPP_LIST][IPP_WORD];
    uint32_t nsides;
    int32_t copies_max; /* from copies-supported; 0 = not given */
    uint32_t more;      /* list values past IPP_LIST, not kept */
} ipp_printer_t;

typedef struct {
    uint16_t status;
    int32_t id;    /* job-id; 0 = not given */
    int32_t state; /* IPP_JOB_*; 0 = not given */
    char reasons[IPP_STATE];
    char message[IPP_TEXT]; /* job-state-message, or the status-message */
    int32_t pages;          /* job-impressions-completed; -1 = not given */
} ipp_job_t;

/* Read a Get-Printer-Attributes or job reply. IPP_OK even when the status
 * is an error: the caller looks at `status`. */
int ipp_parse_printer(const uint8_t *msg, uint32_t len, ipp_printer_t *p);
int ipp_parse_job(const uint8_t *msg, uint32_t len, ipp_job_t *j);
/* Is `format` one the printer says it takes (or does it sniff any:
 * application/octet-stream)? */
bool ipp_takes(const ipp_printer_t *p, const char *format);

/* Names for a view. */
const char *ipp_printer_state_name(int32_t state); /* "idle", "printing", "stopped", "unknown" */
const char *ipp_job_state_name(int32_t state);

#endif
