/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_fake_device.h — a printer (IPP Everywhere) and a scanner (eSCL) on
 * 127.0.0.1, in a forked child, for the Platen tests (test_zxvd_rpc.c,
 * kernel/src/apps/test_apps.c). Include once; POSIX only.
 *
 *   POST /ipp/print       a printer: PDF and JPEG, job 77
 *   POST /ipp/refuse      IPP status 0x0400 with a message
 *   POST /ipp/garbage     HTTP 200 with a body that is not IPP
 *   POST /ipp/500         HTTP 500
 *   POST /ipp/continue    "100 Continue", then the printer's answer
 *   POST /ipp/close       closes without answering
 *   GET  /eSCL/...        a scanner: two pages after one "busy"
 *   POST /busy/ScanJobs, /nopaper/ScanJobs, /noloc/ScanJobs   refusals
 *   POST /half/ScanJobs   one page, then HTTP 500
 *   POST /stuck/ScanJobs  busy for ever
 *   GET  /fake/last       what the last print or scan job asked for
 *   GET  /fake/quit       the child exits */
#ifndef TEST_FAKE_DEVICE_H
#define TEST_FAKE_DEVICE_H

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ipp.h"

static pid_t FAKE_PID;
static int FAKE_PORT;

static void fd_send(int c, const void *d, size_t n)
{
    const char *p = d;
    while (n) {
        ssize_t k = send(c, p, n, 0);
        if (k <= 0) return;
        p += k;
        n -= (size_t) k;
    }
}

static void fd_reply(int c, int status, const char *ctype, const void *body, size_t n, bool chunked,
                     const char *extra)
{
    char h[512];
    int k = snprintf(h, sizeof h, "HTTP/1.1 %d X\r\n%s%s%s", status, ctype ? "Content-Type: " : "",
                     ctype ? ctype : "", ctype ? "\r\n" : "");
    if (extra) k += snprintf(h + k, sizeof h - (size_t) k, "%s", extra);
    if (chunked)
        k += snprintf(h + k, sizeof h - (size_t) k, "Transfer-Encoding: chunked\r\n\r\n");
    else
        k += snprintf(h + k, sizeof h - (size_t) k, "Content-Length: %zu\r\n\r\n", n);
    fd_send(c, h, (size_t) k);
    if (!chunked) {
        fd_send(c, body, n);
        return;
    }
    /* in pieces of at most 1000 bytes, with an extension on the first */
    size_t off = 0;
    while (off < n) {
        size_t piece = n - off < 1000 ? n - off : 1000;
        char sz[32];
        int m = snprintf(sz, sizeof sz, "%zx%s\r\n", piece, off ? "" : ";x=1");
        fd_send(c, sz, (size_t) m);
        fd_send(c, (const char *) body + off, piece);
        fd_send(c, "\r\n", 2);
        off += piece;
    }
    fd_send(c, "0\r\n\r\n", 5);
}

static char FAKE_LAST[512];
static int FAKE_PAGES; /* NextDocument calls since the last ScanJobs */

static void fd_printer(int c, const uint8_t *body, size_t n, bool chunked)
{
    ipp_r_t r;
    ipp_attr_t a;
    static uint8_t out[4096];
    ipp_w_t w;
    ipp_w_init(&w, out, sizeof out);
    if (ipp_r_init(&r, body, (uint32_t) n)) {
        fd_reply(c, 400, NULL, "", 0, false, NULL);
        return;
    }
    char fmt[64] = "", name[64] = "", sides[40] = "";
    int32_t copies = 0, job = 0;
    while (ipp_next(&r, &a) == 1) {
        if (ipp_name_is(&a, "document-format")) ipp_str(&a, fmt, sizeof fmt);
        if (ipp_name_is(&a, "job-name")) ipp_str(&a, name, sizeof name);
        if (ipp_name_is(&a, "sides")) ipp_str(&a, sides, sizeof sides);
        if (ipp_name_is(&a, "copies")) ipp_int(&a, &copies);
        if (ipp_name_is(&a, "job-id")) ipp_int(&a, &job);
    }
    uint16_t op = r.status;
    ipp_w_head(&w, 0, r.request_id);
    ipp_w_group(&w, IPP_G_OPERATION);
    ipp_w_str(&w, IPP_T_CHARSET, "attributes-charset", "utf-8");
    if (op == IPP_OP_GET_PRINTER_ATTRIBUTES) {
        ipp_w_group(&w, IPP_G_PRINTER);
        ipp_w_str(&w, IPP_T_NAME, "printer-name", "Fake");
        ipp_w_str(&w, IPP_T_TEXT, "printer-make-and-model", "Fake Printer 1");
        ipp_w_int(&w, IPP_T_ENUM, "printer-state", IPP_PRINTER_IDLE);
        ipp_w_str(&w, IPP_T_KEYWORD, "printer-state-reasons", "none");
        ipp_w_bool(&w, "printer-is-accepting-jobs", true);
        ipp_w_bool(&w, "color-supported", true);
        ipp_w_int(&w, IPP_T_INTEGER, "queued-job-count", 0);
        ipp_w_str(&w, IPP_T_MIME, "document-format-supported", "application/pdf");
        ipp_w_str(&w, IPP_T_MIME, NULL, "image/jpeg");
        ipp_w_str(&w, IPP_T_KEYWORD, "media-supported", "iso_a4_210x297mm");
        ipp_w_str(&w, IPP_T_KEYWORD, "media-default", "iso_a4_210x297mm");
        ipp_w_str(&w, IPP_T_KEYWORD, "sides-supported", "one-sided");
    } else if (op == IPP_OP_PRINT_JOB) {
        snprintf(FAKE_LAST, sizeof FAKE_LAST, "print fmt=%s name=%s copies=%d sides=%s doc=%u:%.*s",
                 fmt, name, (int) copies, sides, (unsigned) (n - ipp_r_end(&r)),
                 (int) (n - ipp_r_end(&r) < 8 ? n - ipp_r_end(&r) : 8),
                 (const char *) body + ipp_r_end(&r));
        ipp_w_group(&w, IPP_G_JOB);
        ipp_w_int(&w, IPP_T_INTEGER, "job-id", 77);
        ipp_w_int(&w, IPP_T_ENUM, "job-state", IPP_JOB_PENDING);
        ipp_w_str(&w, IPP_T_KEYWORD, "job-state-reasons", "none");
    } else if (op == IPP_OP_GET_JOB_ATTRIBUTES || op == IPP_OP_CANCEL_JOB) {
        if (job != 77) {
            out[2] = 0x04, out[3] = 0x06; /* client-error-not-found */
            ipp_w_str(&w, IPP_T_TEXT, "status-message", "no such job");
        } else {
            ipp_w_group(&w, IPP_G_JOB);
            ipp_w_int(&w, IPP_T_INTEGER, "job-id", 77);
            ipp_w_int(&w, IPP_T_ENUM, "job-state",
                      op == IPP_OP_CANCEL_JOB ? IPP_JOB_CANCELED : IPP_JOB_COMPLETED);
            ipp_w_int(&w, IPP_T_INTEGER, "job-impressions-completed",
                      op == IPP_OP_CANCEL_JOB ? 0 : 2);
        }
    }
    ipp_w_group(&w, IPP_G_END);
    fd_reply(c, 200, "application/ipp", out, w.len, chunked, NULL);
}

static const char FAKE_CAPS[] =
    "<?xml version=\"1.0\"?><scan:ScannerCapabilities><pwg:MakeAndModel>Fake "
    "Scanner</pwg:MakeAndModel>"
    "<scan:Platen><scan:MaxWidth>2550</scan:MaxWidth><scan:MaxHeight>3508</scan:MaxHeight>"
    "<scan:ColorMode>RGB24</scan:ColorMode><scan:ColorMode>Grayscale8</scan:ColorMode>"
    "<scan:DocumentFormatExt>image/jpeg</scan:DocumentFormatExt>"
    "<scan:XResolution>300</scan:XResolution><scan:XResolution>600</scan:XResolution></scan:Platen>"
    "</scan:ScannerCapabilities>";
static const char FAKE_STATUS[] =
    "<scan:ScannerStatus><pwg:State>Idle</pwg:State></scan:ScannerStatus>";

static void fd_serve(int c)
{
    static char req[1 << 20];
    size_t n = 0;
    char *end = NULL;
    while (!end && n < sizeof req - 1) {
        ssize_t k = recv(c, req + n, sizeof req - 1 - n, 0);
        if (k <= 0) return;
        n += (size_t) k;
        req[n] = 0;
        end = strstr(req, "\r\n\r\n");
    }
    if (!end) return;
    size_t hl = (size_t) (end + 4 - req), clen = 0;
    const char *cl = strstr(req, "Content-Length: ");
    if (cl && cl < end) clen = (size_t) strtoul(cl + 16, NULL, 10);
    while (n < hl + clen && n < sizeof req - 1) {
        ssize_t k = recv(c, req + n, sizeof req - 1 - n, 0);
        if (k <= 0) return;
        n += (size_t) k;
    }
    const uint8_t *body = (const uint8_t *) req + hl;
    char method[8] = "", path[256] = "";
    sscanf(req, "%7s %255s", method, path);
    if (!strcmp(path, "/ipp/print"))
        fd_printer(c, body, clen, false);
    else if (!strcmp(path, "/ipp/chunked"))
        fd_printer(c, body, clen, true);
    else if (!strcmp(path, "/ipp/continue")) {
        fd_send(c, "HTTP/1.1 100 Continue\r\nX-A: b\r\n\r\n", 33);
        fd_printer(c, body, clen, false);
    } else if (!strcmp(path, "/ipp/refuse")) {
        static uint8_t out[256];
        ipp_w_t w;
        ipp_w_init(&w, out, sizeof out);
        ipp_w_head(&w, 0x0400, 1);
        ipp_w_group(&w, IPP_G_OPERATION);
        ipp_w_str(&w, IPP_T_TEXT, "status-message", "bad request");
        ipp_w_group(&w, IPP_G_END);
        fd_reply(c, 200, "application/ipp", out, w.len, false, NULL);
    } else if (!strcmp(path, "/ipp/garbage"))
        fd_reply(c, 200, "application/ipp", "hello", 5, false, NULL);
    else if (!strcmp(path, "/ipp/500"))
        fd_reply(c, 500, NULL, "", 0, false, NULL);
    else if (!strcmp(path, "/ipp/close"))
        return;
    else if (!strcmp(path, "/eSCL/ScannerCapabilities"))
        fd_reply(c, 200, "text/xml", FAKE_CAPS, sizeof FAKE_CAPS - 1, true, NULL);
    else if (!strcmp(path, "/eSCL/ScannerStatus"))
        fd_reply(c, 200, "text/xml", FAKE_STATUS, sizeof FAKE_STATUS - 1, false, NULL);
    else if (!strcmp(path, "/eSCL/ScanJobs") && !strcmp(method, "POST")) {
        const char *d = strstr((const char *) body, "<scan:XResolution>");
        const char *src = strstr((const char *) body, "<pwg:InputSource>");
        snprintf(FAKE_LAST, sizeof FAKE_LAST, "scan dpi=%.4s src=%.6s", d ? d + 18 : "?",
                 src ? src + 17 : "?");
        FAKE_PAGES = 0;
        char loc[128];
        snprintf(loc, sizeof loc, "Location: http://127.0.0.1:%d/eSCL/ScanJobs/5\r\n", FAKE_PORT);
        fd_reply(c, 201, NULL, "", 0, false, loc);
    } else if (!strcmp(path, "/eSCL/ScanJobs/5/NextDocument")) {
        FAKE_PAGES++;
        static char page[3000];
        memset(page, 'A', sizeof page);
        if (FAKE_PAGES == 1)
            fd_reply(c, 503, NULL, "", 0, false, NULL);
        else if (FAKE_PAGES == 2)
            fd_reply(c, 200, "image/jpeg", page, sizeof page, true, NULL);
        else if (FAKE_PAGES == 3)
            fd_reply(c, 200, "image/jpeg", "BBBBBBBBBB", 10, false, NULL);
        else
            fd_reply(c, 404, NULL, "", 0, false, NULL);
    } else if ((!strcmp(path, "/half/ScanJobs") || !strcmp(path, "/stuck/ScanJobs")) &&
               !strcmp(method, "POST")) {
        FAKE_PAGES = 0;
        fd_reply(c, 201, NULL, "", 0, false,
                 path[1] == 'h' ? "Location: /half/ScanJobs/6\r\n"
                                : "Location: /stuck/ScanJobs/7\r\n");
    } else if (!strcmp(path, "/half/ScanJobs/6/NextDocument")) {
        if (++FAKE_PAGES == 1)
            fd_reply(c, 200, "image/jpeg", "CCCC", 4, false, NULL);
        else
            fd_reply(c, 500, NULL, "", 0, false, NULL);
    } else if (!strcmp(path, "/stuck/ScanJobs/7/NextDocument"))
        fd_reply(c, 503, NULL, "", 0, false, NULL);
    else if (!strcmp(path, "/busy/ScanJobs"))
        fd_reply(c, 503, NULL, "", 0, false, NULL);
    else if (!strcmp(path, "/nopaper/ScanJobs"))
        fd_reply(c, 409, NULL, "", 0, false, NULL);
    else if (!strcmp(path, "/noloc/ScanJobs"))
        fd_reply(c, 201, NULL, "", 0, false, NULL);
    else if (!strcmp(path, "/fake/last"))
        fd_reply(c, 200, "text/plain", FAKE_LAST, strlen(FAKE_LAST), false, NULL);
    else if (!strcmp(path, "/fake/quit")) {
        fd_reply(c, 200, NULL, "", 0, false, NULL);
        close(c);
        _exit(0);
    } else
        fd_reply(c, 404, NULL, "", 0, false, NULL);
}

static bool fake_start(void)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    socklen_t al = sizeof a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (s < 0 || bind(s, (struct sockaddr *) &a, sizeof a) || listen(s, 16) ||
        getsockname(s, (struct sockaddr *) &a, &al))
        return false;
    FAKE_PORT = ntohs(a.sin_port);
    fflush(stdout);
    FAKE_PID = fork();
    if (FAKE_PID < 0) return false;
    if (FAKE_PID == 0) {
        signal(SIGPIPE, SIG_IGN);
        alarm(120); /* never outlive a stuck test */
        for (;;) {
            int c = accept(s, NULL, NULL);
            if (c < 0) continue;
            fd_serve(c);
            close(c);
        }
    }
    close(s);
    return true;
}

/* GET a path from the fake device: the body, or "" */
static const char *fake_get(const char *path)
{
    static char out[1024];
    out[0] = 0;
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t) FAKE_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (s < 0 || connect(s, (struct sockaddr *) &a, sizeof a)) return out;
    char q[300];
    int k = snprintf(q, sizeof q, "GET %s HTTP/1.1\r\nHost: x\r\n\r\n", path);
    fd_send(s, q, (size_t) k);
    static char resp[2048];
    size_t n = 0;
    ssize_t r;
    while (n < sizeof resp - 1 && (r = recv(s, resp + n, sizeof resp - 1 - n, 0)) > 0)
        n += (size_t) r;
    resp[n] = 0;
    close(s);
    const char *b = strstr(resp, "\r\n\r\n");
    if (b) snprintf(out, sizeof out, "%s", b + 4);
    return out;
}

static void fake_stop(void)
{
    if (FAKE_PID <= 0) return;
    fake_get("/fake/quit");
    int st;
    waitpid(FAKE_PID, &st, 0);
    FAKE_PID = 0;
}

#endif
