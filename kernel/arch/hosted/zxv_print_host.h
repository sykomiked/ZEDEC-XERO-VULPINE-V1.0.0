/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_print_host.h — Platen's printers and scanners on the local network:
 * IPP Everywhere printing (kernel/src/print/ipp.h) and eSCL scanning
 * (escl.h) over HTTP/1.1, behind zxvd's print.* and scan.* methods.
 *
 *   N1 ADDRESSES. A printer is ipp://host[:631]/path or http://host[:port]/path;
 *      a scanner is http://host[:port]/root (its eSCL root, e.g. /eSCL).
 *      ipps:// and https:// are refused for now: there is no TLS client in
 *      the hosted layer yet.
 *   N2 LOCAL ONLY. The host must resolve to a loopback, private (10/8,
 *      172.16/12, 192.168/16), link-local, or IPv6 unique-local address.
 *      Printers and scanners live there, and zxvd never becomes a way to
 *      send documents to the internet.
 *   N3 TIMEOUTS. 10 seconds to connect and for each read or write.
 *   N4 FILES. A document is read from the ZXV folder (the space in use,
 *      zxv_files.h) and scanned pages are written into it, each a new file;
 *      nothing outside the folder is read or written.
 *   N5 FORMATS. A document is sent only in a format the printer says it
 *      takes (document-format-supported); the format comes from the file's
 *      extension. ZXV does not convert documents yet.
 *
 * POSIX only for now; on Windows every call returns ZPH_E_PLATFORM. */
#ifndef ZXV_PRINT_HOST_H
#define ZXV_PRINT_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "escl.h"
#include "ipp.h"
#include "zxv_files.h"

enum {
    ZPH_OK = 0,
    ZPH_E_URI = -1,      /* not an address N1 allows */
    ZPH_E_LOCAL = -2,    /* not on the local network (N2) */
    ZPH_E_NET = -3,      /* could not connect, or the connection failed */
    ZPH_E_HTTP = -4,     /* the device answered with an HTTP error */
    ZPH_E_PROTO = -5,    /* the answer was not IPP or eSCL */
    ZPH_E_FILE = -6,     /* the ZXV folder refused (see zxf code in the message) */
    ZPH_E_FORMAT = -7,   /* the printer does not take this file's format (N5) */
    ZPH_E_PLATFORM = -8, /* not on this platform yet */
};

#define ZPH_URI_MAX   512u
#define ZPH_PAGES_MAX 50u /* pages saved from one scan */
#define ZPH_ERR       160u

/* The document format for a file name ("application/pdf", ...), or NULL. */
const char *zph_format_of(const char *name);

int zph_printer(const char *uri, ipp_printer_t *p, char *err, size_t cap);
/* Check the printer takes the file's format, then send it (Print-Job). */
int zph_print(const char *uri, const char *path, const ipp_job_opts_t *o, ipp_job_t *job, char *err,
              size_t cap);
/* Get-Job-Attributes, or Cancel-Job when cancel. */
int zph_job(const char *uri, int32_t id, bool cancel, ipp_job_t *job, char *err, size_t cap);

int zph_scanner(const char *url, escl_caps_t *c, escl_status_t *s, char *err, size_t cap);
/* Scan with s and save each page in `folder` as "Scan N.<ext>" (the first
 * free N). *pages saved; their paths in saved[0..*pages). Pages saved
 * before a failure are kept and counted. */
int zph_scan(const char *url, const escl_settings_t *s, const char *folder,
             char saved[][ZXF_PATH_MAX + 1], uint32_t cap, uint32_t *pages, char *err,
             size_t errcap);

/* N3's timeout, in milliseconds (tests shorten it); 0 restores 10 s. */
void zph_set_timeout_ms(uint32_t ms);

#endif
