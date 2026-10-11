/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hostile_worker.c — a worker that misbehaves on purpose, for the host
 * tests. Test use only. Its mode is the name it was started under
 * (argv[0]'s last path component; the Makefile makes one link per mode):
 *
 *   exit-early      exits before HELLO
 *   exit-after-hello  exits right after HELLO
 *   bad-version     HELLO with protocol version 99
 *   no-seccomp      a well-behaved worker that never installs seccomp
 *   crash-load      abort() on LOAD
 *   hang-load       never answers LOAD
 *   hang-call       never answers CALL
 *   garbage         answers CALL with a frame of unknown type
 *   oversize        answers CALL with a length of 0xFFFFFFFF
 *   zero-length     answers CALL with a length of 0
 *   lie-fuel        reports one more unit of fuel than it was given
 *   bad-prop        a proposal with denominator 0
 *   unnormal        a proposal 2/4 (not normalised)
 *   long-label      a proposal label of 65 bytes
 *   trailing        a valid RESULT plus one extra byte
 *   wrong-type      answers CALL with LOADED
 *   bad-status      a RESULT with status -12345
 *   too-many        65 proposals in one RESULT
 *   half-frame      3 bytes of a header, then exits
 *   slow-drip       a valid RESULT, one byte every 50 ms
 *   ignore-quit     ignores QUIT and sleeps
 *   close-and-hang  closes its socket on CALL and sleeps
 *   hygiene         on LOAD, checks what it inherited (fds, environment,
 *                   limits, signals) and answers OK or says what is wrong
 *   props           answers CALL(n) with n valid proposals (n <= 64)
 *   syscall         (with --seccomp it installs the filter first) on LOAD
 *                   opens /dev/null, writes to fd 1 or maps executable
 *                   memory, as LOAD's plugin name says ("open", "write1",
 *                   "mmap-exec"), then answers OK
 *
 * Anything not listed behaves like a correct worker that answers every
 * CALL with result 7 and 10 fuel. */
#if !defined(__APPLE__)
#define _GNU_SOURCE
#endif
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#include <unistd.h>

#include "ti_frame.h"
#include "ti_seccomp.h"
#include "ti_wio.h"
#include "zwasm.h"

extern char **environ;

static const char *g_mode = "";

static int is(const char *m) { return strcmp(g_mode, m) == 0; }

static void sleep_forever(void) {
    for (;;) pause();
}

static void send_or_die(ti_buf *b) {
    if (ti_send_buf(TI_WORKER_FD, b) != 0) _exit(9);
}

/* A raw RESULT body writer, for frames the real encoder refuses. */
typedef struct {
    uint8_t b[16384];
    size_t n;
} raw;

static void raw_bytes(raw *r, const void *p, size_t n) {
    if (r->n + n > sizeof r->b) _exit(8);
    memcpy(r->b + r->n, p, n);
    r->n += n;
}

static void raw_le(raw *r, uint64_t v, int bytes) {
    for (int i = 0; i < bytes; i++) {
        uint8_t c = (uint8_t) (v >> (8 * i));
        raw_bytes(r, &c, 1);
    }
}

static void raw_prop(raw *r, uint32_t kind, int64_t num, int64_t den, uint32_t llen) {
    uint8_t label[255];
    memset(label, 'x', sizeof label);
    raw_le(r, kind, 4);
    raw_le(r, (uint64_t) num, 8);
    raw_le(r, (uint64_t) den, 8);
    raw_le(r, llen, 1);
    raw_bytes(r, label, llen);
}

/* status, has_result, result, fuel, then the caller adds proposals and
 * the message. */
static void raw_head(raw *r, int32_t status, uint64_t fuel, uint32_t nprops) {
    uint32_t st;
    memcpy(&st, &status, sizeof st);
    r->n = 0;
    raw_le(r, st, 4);
    raw_le(r, status == 0 ? 1 : 0, 1);
    raw_le(r, status == 0 ? 7 : 0, 8);
    raw_le(r, fuel, 8);
    raw_le(r, nprops, 1);
}

static void raw_send(const raw *body, uint32_t type, uint32_t extra) {
    uint8_t hdr[5];
    uint32_t len = (uint32_t) body->n + 1u + extra;
    hdr[0] = (uint8_t) len;
    hdr[1] = (uint8_t) (len >> 8);
    hdr[2] = (uint8_t) (len >> 16);
    hdr[3] = (uint8_t) (len >> 24);
    hdr[4] = (uint8_t) type;
    if (ti_write_all(TI_WORKER_FD, hdr, 5) != 0 || ti_write_all(TI_WORKER_FD, body->b, body->n) != 0) _exit(9);
    if (extra) {
        uint8_t z[8] = {0};
        if (ti_write_all(TI_WORKER_FD, z, extra) != 0) _exit(9);
    }
}

static void answer_loaded(int32_t status, const char *msg) {
    ti_loaded l;
    ti_buf out = {NULL, 0, 0};
    memset(&l, 0, sizeof l);
    l.status = status;
    l.msg_len = ti_msg_clean(l.msg, msg);
    if (ti_enc_loaded(&out, &l) != ZXV_OK) _exit(8);
    send_or_die(&out);
    ti_buf_free(&out);
}

static const char *hygiene(void) {
    struct stat a, b;
    struct rlimit rl;
    struct sigaction sa;
    sigset_t mask;
    if (environ && environ[0]) return "the environment is not empty";
    if (stat("/dev/null", &a) != 0) return "cannot stat /dev/null";
    for (int fd = 0; fd <= 2; fd++) {
        if (fd == 2) break; /* stderr may be inherited (TI_F_INHERIT_STDERR) */
        if (fstat(fd, &b) != 0 || b.st_rdev != a.st_rdev || !S_ISCHR(b.st_mode)) return "fd 0 or 1 is not /dev/null";
    }
    if (fcntl(TI_WORKER_FD, F_GETFD) < 0) return "fd 3 is not open";
    if (fcntl(TI_WORKER_FD, F_GETFD) & FD_CLOEXEC) return "fd 3 is close-on-exec";
    for (int fd = 4; fd < 1024; fd++)
        if (fcntl(fd, F_GETFD) >= 0) return "a descriptor above 3 was inherited";
    if (getrlimit(RLIMIT_CORE, &rl) != 0 || rl.rlim_cur != 0 || rl.rlim_max != 0) return "core dumps are allowed";
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0 || rl.rlim_cur > 64) return "RLIMIT_NOFILE is not 64";
    if (sigaction(SIGPIPE, NULL, &sa) != 0 || sa.sa_handler != SIG_DFL) return "SIGPIPE is not default";
    if (sigprocmask(SIG_BLOCK, NULL, &mask) != 0 || sigismember(&mask, SIGTERM)) return "signals are blocked";
    return NULL;
}

static void forbidden(const char *what) {
    if (strcmp(what, "open") == 0) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd >= 0) close(fd);
    } else if (strcmp(what, "write1") == 0) {
        if (write(1, "x", 1) < 0) _exit(7);
    } else if (strcmp(what, "mmap-exec") == 0) {
        void *p = mmap(NULL, 4096, PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p != MAP_FAILED) munmap(p, 4096);
    }
}

int main(int argc, char **argv) {
    ti_buf out = {NULL, 0, 0};
    int seccomp = argc > 1 && strcmp(argv[1], "--seccomp") == 0;
    const char *slash = strrchr(argv[0], '/');
    g_mode = slash ? slash + 1 : argv[0];

    if (is("exit-early")) return 0;
    if (is("syscall") && seccomp && ti_seccomp_install() != ZXV_OK) return 3;

    ti_hello h = {(uint16_t) (is("bad-version") ? 99u : TI_PROTO_VERSION),
                  (uint16_t) (is("syscall") && seccomp ? TI_HELLO_SECCOMP : 0u)};
    if (ti_enc_hello(&out, &h) != ZXV_OK) return 8;
    send_or_die(&out);
    if (is("exit-after-hello")) _exit(0);

    for (;;) {
        uint32_t type = 0;
        uint8_t *body = NULL;
        size_t len = 0;
        int rc = ti_read_frame(TI_WORKER_FD, TI_REQ_MAX, &type, &body, &len);
        if (rc != 0) _exit(rc == 1 ? 0 : 4);
        if (type == TI_MSG_QUIT) {
            free(body);
            ti_buf_free(&out);
            if (is("ignore-quit")) sleep_forever();
            if (is("syscall") && seccomp) {
#if defined(__linux__) && defined(SYS_exit_group)
                syscall(SYS_exit_group, 0); /* past any exit hook */
#endif
                _exit(0);
            }
            return 0;
        }
        if (type == TI_MSG_LOAD) {
            ti_load m;
            if (ti_dec_load(body, len, &m) != ZXV_OK) _exit(4);
            if (is("crash-load")) abort();
            if (is("hang-load")) sleep_forever();
            if (is("syscall")) forbidden(m.name);
            const char *bad = is("hygiene") ? hygiene() : NULL;
            free(body);
            answer_loaded(bad ? ZW_E_LINK : ZXV_OK, bad);
            continue;
        }
        if (type != TI_MSG_CALL) _exit(4);
        ti_call c;
        if (ti_dec_call(body, len, &c) != ZXV_OK) _exit(4);
        free(body);
        raw r;
        if (is("hang-call")) sleep_forever();
        if (is("close-and-hang")) {
            close(TI_WORKER_FD);
            sleep_forever();
        }
        if (is("garbage")) {
            raw_head(&r, 0, 1, 0);
            raw_send(&r, 0x77u, 0);
        } else if (is("oversize") || is("zero-length") || is("half-frame")) {
            uint8_t hdr[4] = {0xFF, 0xFF, 0xFF, 0xFF};
            if (is("zero-length")) memset(hdr, 0, sizeof hdr);
            if (ti_write_all(TI_WORKER_FD, hdr, is("half-frame") ? 3u : 4u) != 0) _exit(9);
            if (is("half-frame")) _exit(0);
            sleep_forever();
        } else if (is("wrong-type")) {
            answer_loaded(ZXV_OK, NULL);
        } else if (is("lie-fuel")) {
            raw_head(&r, 0, c.fuel + 1u, 0);
            raw_le(&r, 0, 2);
            raw_send(&r, TI_MSG_RESULT, 0);
        } else if (is("bad-prop") || is("unnormal") || is("long-label")) {
            raw_head(&r, 0, 10, 1);
            if (is("bad-prop")) raw_prop(&r, 1, 1, 0, 0);
            else if (is("unnormal")) raw_prop(&r, 1, 2, 4, 0);
            else raw_prop(&r, 1, 1, 2, 65);
            raw_le(&r, 0, 2);
            raw_send(&r, TI_MSG_RESULT, 0);
        } else if (is("trailing")) {
            raw_head(&r, 0, 10, 0);
            raw_le(&r, 0, 2);
            raw_send(&r, TI_MSG_RESULT, 1);
        } else if (is("bad-status")) {
            raw_head(&r, -12345, 0, 0);
            raw_le(&r, 0, 2);
            raw_send(&r, TI_MSG_RESULT, 0);
        } else if (is("too-many")) {
            raw_head(&r, 0, 10, 65);
            for (int i = 0; i < 65; i++) raw_prop(&r, 1, 1, 1, 0);
            raw_le(&r, 0, 2);
            raw_send(&r, TI_MSG_RESULT, 0);
        } else if (is("slow-drip")) {
            raw_head(&r, 0, 10, 0);
            raw_le(&r, 0, 2);
            uint8_t hdr[5] = {(uint8_t) (r.n + 1u), 0, 0, 0, (uint8_t) TI_MSG_RESULT};
            for (size_t i = 0; i < 5u + r.n; i++) {
                uint8_t byte = i < 5u ? hdr[i] : r.b[i - 5u];
                if (ti_write_all(TI_WORKER_FD, &byte, 1) != 0) _exit(9);
                usleep(50000);
            }
        } else {
            ti_result *res = (ti_result *) calloc(1, sizeof *res);
            if (!res) _exit(5);
            res->status = ZXV_OK;
            res->has_result = 1;
            res->result = 7;
            res->fuel_used = c.fuel < 10u ? c.fuel : 10u;
            if (is("props") && c.nargs == 1 && c.args[0] >= 0 && c.args[0] <= (int64_t) TI_PROPS_MAX) {
                res->nprops = (uint32_t) c.args[0];
                for (uint32_t i = 0; i < res->nprops; i++) {
                    res->props[i].kind = 2;
                    res->props[i].num = (int64_t) i + 1;
                    res->props[i].den = 1;
                    res->props[i].label_len = (uint32_t) snprintf((char *) res->props[i].label,
                                                                  TI_LABEL_MAX, "p%u", (unsigned) i);
                }
            }
            if (ti_enc_result(&out, res) != ZXV_OK) _exit(8);
            free(res);
            send_or_die(&out);
        }
    }
}
