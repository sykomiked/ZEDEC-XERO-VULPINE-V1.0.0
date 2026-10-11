/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ti_host.c — the host side of Tri-Isolate: starting workers, talking to
 * them under deadlines, charging fuel, killing them, and the S0 queue. */
#if !defined(_WIN32) && !defined(__APPLE__)
#define _GNU_SOURCE
#endif

#include "triisolate.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ti_frame.h"
#include "ti_seccomp.h"

static _Thread_local char g_err[512];

static void set_error(const char *msg) { snprintf(g_err, sizeof g_err, "%s", msg); }

#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
static void set_errorf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
}

/* Copy the current message (truncated to fit). */
static void copy_err(char *dst, size_t cap) {
    size_t n = strlen(g_err);
    if (cap == 0) return;
    if (n >= cap) n = cap - 1u;
    memcpy(dst, g_err, n);
    dst[n] = '\0';
}

int32_t ti_last_error(char *buf, size_t cap, size_t *out_len) {
    size_t n = strlen(g_err);
    if (out_len) *out_len = n;
    if (!buf || cap <= n) return ZXV_E_BUFFER;
    memcpy(buf, g_err, n + 1u);
    return ZXV_OK;
}

const char *ti_status_name(int32_t status) {
    switch (status) {
    case TI_E_SPAWN: return "TI_E_SPAWN";
    case TI_E_PROTOCOL: return "TI_E_PROTOCOL";
    case TI_E_TIMEOUT: return "TI_E_TIMEOUT";
    case TI_E_CRASHED: return "TI_E_CRASHED";
    case TI_E_DEAD: return "TI_E_DEAD";
    default: return zw_status_name(status);
    }
}

#if defined(_WIN32)
/* ===== Windows: not supported (see README.md) ===== */
int32_t ti_seccomp_available(void) { return 0; }
int32_t ti_host_create(const zxv_cap *c, zxv_rmag *r, uint32_t b, const char *w, ti_host **o) {
    (void) c, (void) r, (void) b, (void) w;
    if (o) *o = NULL;
    set_error("Tri-Isolate does not support Windows yet");
    return ZXV_E_NOTIMPL;
}
void ti_host_destroy(ti_host *h) { (void) h; }
#define TI_NOTIMPL(...) { set_error("Tri-Isolate does not support Windows yet"); return ZXV_E_NOTIMPL; }
int32_t ti_host_grant(ti_host *h, uint32_t g) TI_NOTIMPL()
int32_t ti_host_set_limit(ti_host *h, int32_t w, uint64_t v) TI_NOTIMPL()
int32_t ti_host_set_call_fuel(ti_host *h, uint64_t f) TI_NOTIMPL()
int32_t ti_host_set_timeouts(ti_host *h, uint32_t c, uint32_t l) TI_NOTIMPL()
int32_t ti_host_set_accept_caps(ti_host *h, uint64_t n) TI_NOTIMPL()
int32_t ti_host_set_memory_limit(ti_host *h, uint64_t b) TI_NOTIMPL()
int32_t ti_host_set_flags(ti_host *h, uint32_t f) TI_NOTIMPL()
int32_t ti_host_stats(const ti_host *h, uint64_t *a, uint64_t *b, uint64_t *c) TI_NOTIMPL()
int32_t ti_plugin_load(ti_host *h, const uint8_t *w, size_t l, const char *n, ti_plugin **o) TI_NOTIMPL()
void ti_plugin_destroy(ti_plugin *p) { (void) p; }
int32_t ti_plugin_stop(ti_plugin *p) TI_NOTIMPL()
int32_t ti_plugin_state(const ti_plugin *p, int32_t *s) TI_NOTIMPL()
int32_t ti_plugin_pid(const ti_plugin *p, int64_t *o) TI_NOTIMPL()
int32_t ti_plugin_exit(const ti_plugin *p, int32_t *h, int32_t *v) TI_NOTIMPL()
int32_t ti_plugin_call(ti_plugin *p, const char *e, const int64_t *a, uint32_t n, int64_t *r, int32_t *h,
                       uint64_t *f) TI_NOTIMPL()
int32_t ti_queue_pending(const ti_host *h, uint32_t *o) TI_NOTIMPL()
int32_t ti_queue_next(const ti_host *h, uint64_t *o) TI_NOTIMPL()
int32_t ti_proposal_get(const ti_host *h, uint64_t id, int32_t *s, uint32_t *k, int64_t *n, int64_t *d,
                        char p[32], uint8_t *l, size_t c, size_t *ol) TI_NOTIMPL()
int32_t ti_proposal_accept(ti_host *h, const zxv_cap *c, uint64_t id) TI_NOTIMPL()
int32_t ti_proposal_reject(ti_host *h, const zxv_cap *c, uint64_t id) TI_NOTIMPL()
int32_t ti_proposal_accept_post(ti_host *h, const zxv_cap *c, uint64_t id, uint32_t f, uint32_t t,
                                const char *ccy, const char *ref, uint64_t *e) TI_NOTIMPL()
#else
/* ===== POSIX ===== */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif

#define TI_PENDING_MAX 1024u
#define TI_PATH_MAX    1024u
#define TI_QUIT_GRACE_MS 200

typedef struct {
    uint64_t id;
    int32_t state;
    uint32_t kind;
    int64_t num, den;
    char plugin[32];
    uint8_t label[TI_LABEL_MAX];
    uint32_t label_len;
} ti_qprop;

struct ti_host {
    const zxv_cap *budget_cap;
    zxv_rmag *rmag;
    uint32_t budget;
    char worker[TI_PATH_MAX];
    uint32_t grants;
    zw_policy *policy; /* the host's own validation; limits sent to workers */
    uint64_t call_fuel;
    uint32_t call_ms, load_ms;
    uint64_t accept_caps;
    uint64_t mem_limit;
    uint32_t flags;
    ti_qprop *q;
    size_t qn, qcap;
    uint32_t pending;
    uint64_t next_id;
    uint64_t spawned, killed, calls;
};

struct ti_plugin {
    ti_host *host;
    pid_t pid;  /* 0 once reaped */
    int fd;     /* -1 once closed */
    int32_t state;
    int32_t exit_how, exit_value;
    char name[32];
};

/* The host functions each grant adds (as zw_host_grant). */
static const struct {
    const char *name, *sig;
    uint32_t grant;
} k_imports[] = {
    {"rat_add", "i(IIIIi)", ZW_GRANT_RATIONAL}, {"rat_sub", "i(IIIIi)", ZW_GRANT_RATIONAL},
    {"rat_mul", "i(IIIIi)", ZW_GRANT_RATIONAL}, {"rat_div", "i(IIIIi)", ZW_GRANT_RATIONAL},
    {"rat_cmp", "i(IIIIi)", ZW_GRANT_RATIONAL}, {"rat_normalize", "i(IIi)", ZW_GRANT_RATIONAL},
    {"lp_not", "i(i)", ZW_GRANT_LOGIC},         {"lp_and", "i(ii)", ZW_GRANT_LOGIC},
    {"lp_or", "i(ii)", ZW_GRANT_LOGIC},         {"lp_from_evidence", "i(ii)", ZW_GRANT_LOGIC},
    {"propose", "i(iIIii)", ZW_GRANT_PROPOSE},
};

#define ALL_GRANTS (ZW_GRANT_RATIONAL | ZW_GRANT_LOGIC | ZW_GRANT_PROPOSE)

int32_t ti_seccomp_available(void);

/* ----- time ----- */
static int64_t now_ms(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return 0;
    return (int64_t) t.tv_sec * 1000 + (int64_t) (t.tv_nsec / 1000000);
}

static int remaining(int64_t deadline) {
    int64_t r = deadline - now_ms();
    if (r <= 0) return 0;
    return r > 1000000 ? 1000000 : (int) r;
}

/* ----- hosts ----- */
int32_t ti_host_create(const zxv_cap *budget_cap, zxv_rmag *rmag, uint32_t budget,
                       const char *worker_path, ti_host **out_host) {
    ti_host *h;
    int32_t st;
    if (!out_host) return ZXV_E_ARG;
    *out_host = NULL;
    if (!budget_cap || !rmag || !worker_path || !worker_path[0]) {
        set_error("ti_host_create: NULL argument");
        return ZXV_E_ARG;
    }
    if (strlen(worker_path) >= TI_PATH_MAX) {
        set_error("ti_host_create: the worker path is too long");
        return ZXV_E_TOO_LONG;
    }
    st = zxv_cap_check(budget_cap, ZXV_CAP_BUDGET);
    if (st != ZXV_OK) {
        set_error("ti_host_create: the budget capability lacks ZXV_CAP_BUDGET");
        return st;
    }
    st = zxv_rmag_status(rmag, budget, NULL, NULL, NULL, NULL, NULL);
    if (st != ZXV_OK) {
        set_error("ti_host_create: no such budget in the rmag");
        return st;
    }
    h = (ti_host *) calloc(1, sizeof *h);
    if (!h) return ZXV_E_NOMEM;
    if (zw_policy_create(&h->policy) != ZXV_OK) {
        free(h);
        return ZXV_E_NOMEM;
    }
    h->budget_cap = budget_cap;
    h->rmag = rmag;
    h->budget = budget;
    memcpy(h->worker, worker_path, strlen(worker_path) + 1u);
    h->call_fuel = UINT64_C(10000000);
    h->call_ms = 5000;
    h->load_ms = 10000;
    h->accept_caps = ZXV_CAP_LEDGER_POST;
    h->next_id = 1;
    *out_host = h;
    return ZXV_OK;
}

void ti_host_destroy(ti_host *host) {
    if (!host) return;
    zw_policy_destroy(host->policy);
    free(host->q);
    free(host);
}

int32_t ti_host_grant(ti_host *host, uint32_t grants) {
    if (!host || (grants & ~ALL_GRANTS)) {
        set_error("ti_host_grant: unknown grant bits");
        return ZXV_E_ARG;
    }
    for (size_t i = 0; i < sizeof k_imports / sizeof k_imports[0]; i++)
        if ((grants & k_imports[i].grant) && !(host->grants & k_imports[i].grant)) {
            int32_t st = zw_policy_allow_import(host->policy, "zxv", k_imports[i].name, k_imports[i].sig);
            if (st != ZXV_OK) return st;
        }
    host->grants |= grants;
    return ZXV_OK;
}

int32_t ti_host_set_limit(ti_host *host, int32_t which, uint64_t value) {
    if (!host) return ZXV_E_ARG;
    return zw_policy_set_limit(host->policy, which, value);
}

int32_t ti_host_set_call_fuel(ti_host *host, uint64_t max_fuel) {
    if (!host || max_fuel == 0 || max_fuel > UINT64_C(1000000000000000)) {
        set_error("ti_host_set_call_fuel: fuel must be 1 .. 10^15");
        return ZXV_E_ARG;
    }
    host->call_fuel = max_fuel;
    return ZXV_OK;
}

int32_t ti_host_set_timeouts(ti_host *host, uint32_t call_ms, uint32_t load_ms) {
    if (!host || call_ms == 0 || load_ms == 0 || call_ms > 3600000u || load_ms > 3600000u) {
        set_error("ti_host_set_timeouts: each timeout must be 1 .. 3600000 ms");
        return ZXV_E_ARG;
    }
    host->call_ms = call_ms;
    host->load_ms = load_ms;
    return ZXV_OK;
}

int32_t ti_host_set_accept_caps(ti_host *host, uint64_t needed) {
    if (!host || needed == 0 || (needed & ~ZXV_CAP_ALL)) {
        set_error("ti_host_set_accept_caps: needs known, non-zero capability bits");
        return ZXV_E_ARG;
    }
    host->accept_caps = needed;
    return ZXV_OK;
}

int32_t ti_host_set_memory_limit(ti_host *host, uint64_t bytes) {
    if (!host) return ZXV_E_ARG;
#if !defined(__linux__)
    if (bytes != 0) {
        set_error("ti_host_set_memory_limit: RLIMIT_AS is only enforced on Linux");
        return ZXV_E_NOTIMPL;
    }
#endif
    if (bytes != 0 && bytes < (UINT64_C(1) << 20)) {
        set_error("ti_host_set_memory_limit: 0 (none) or at least 1 MiB");
        return ZXV_E_RANGE;
    }
    host->mem_limit = bytes;
    return ZXV_OK;
}

int32_t ti_host_set_flags(ti_host *host, uint32_t flags) {
    if (!host || (flags & ~(TI_F_SECCOMP | TI_F_CHARGE_ALLOTTED | TI_F_INHERIT_STDERR))) {
        set_error("ti_host_set_flags: unknown flag bits");
        return ZXV_E_ARG;
    }
    if ((flags & TI_F_SECCOMP) && !ti_seccomp_available()) {
        set_error("ti_host_set_flags: TI_F_SECCOMP needs Linux on x86-64 or arm64");
        return ZXV_E_NOTIMPL;
    }
    host->flags = flags;
    return ZXV_OK;
}

int32_t ti_host_stats(const ti_host *host, uint64_t *out_spawned, uint64_t *out_killed,
                      uint64_t *out_calls) {
    if (!host) return ZXV_E_ARG;
    if (out_spawned) *out_spawned = host->spawned;
    if (out_killed) *out_killed = host->killed;
    if (out_calls) *out_calls = host->calls;
    return ZXV_OK;
}

/* ----- processes ----- */
static void record_exit(ti_plugin *p, int ws) {
    if (WIFEXITED(ws)) {
        p->exit_how = TI_EXIT_CODE;
        p->exit_value = WEXITSTATUS(ws);
    } else if (WIFSIGNALED(ws)) {
        p->exit_how = TI_EXIT_SIGNAL;
        p->exit_value = WTERMSIG(ws);
    } else {
        p->exit_how = TI_EXIT_UNKNOWN;
        p->exit_value = 0;
    }
}

/* Wait for the child; block = 0 only polls. 1 reaped, 0 still running. */
static int reap(ti_plugin *p, int block) {
    int ws = 0;
    pid_t r;
    if (p->pid <= 0) return 1;
    do r = waitpid(p->pid, &ws, block ? 0 : WNOHANG);
    while (r < 0 && errno == EINTR);
    if (r == 0) return 0;
    if (r == p->pid) record_exit(p, ws);
    else { /* ECHILD: someone else reaped it (SIGCHLD ignored, ...) */
        p->exit_how = TI_EXIT_UNKNOWN;
        p->exit_value = 0;
    }
    p->pid = 0;
    return 1;
}

static void close_fd(ti_plugin *p) {
    if (p->fd >= 0) close(p->fd);
    p->fd = -1;
}

/* SIGKILL the worker (if it still runs), reap it, close the socket. */
static void kill_worker(ti_plugin *p) {
    if (p->pid > 0 && !reap(p, 0)) {
        kill(p->pid, SIGKILL);
        p->host->killed++;
        reap(p, 1);
    }
    close_fd(p);
    p->state = TI_PLUGIN_DEAD;
}

/* Sleep a few milliseconds (poll with no descriptors). */
static void nap(int ms) {
    struct pollfd none = {-1, 0, 0};
    (void) poll(&none, 0, ms);
}

/* Give a worker that is ending `ms` to become reapable; then kill it. */
static void reap_grace(ti_plugin *p, int ms) {
    int64_t deadline = now_ms() + ms;
    while (!reap(p, 0) && remaining(deadline) > 0) nap(2);
    kill_worker(p);
}

/* Ask the worker to quit; give it TI_QUIT_GRACE_MS; then kill it. */
static void stop_worker(ti_plugin *p) {
    if (p->fd >= 0 && p->pid > 0) {
        uint8_t quit[5] = {1, 0, 0, 0, (uint8_t) TI_MSG_QUIT};
        int flags = 0;
#if defined(MSG_NOSIGNAL)
        flags = MSG_NOSIGNAL;
#endif
        (void) send(p->fd, quit, sizeof quit, flags);
    }
    reap_grace(p, TI_QUIT_GRACE_MS);
}

static void child_fail(void) { _exit(127); }

/* fork + exec the worker with its socket on fd 3. Only async-signal-safe
 * calls run in the child before exec. */
static int32_t spawn(ti_host *h, ti_plugin *p) {
    int sv[2];
    char *argv[3];
    char *envp[1] = {NULL};
    char seccomp_arg[] = "--seccomp";
    struct rlimit rl_core = {0, 0}, rl_as = {(rlim_t) h->mem_limit, (rlim_t) h->mem_limit};
    struct rlimit rl_nofile = {64, 64};
    pid_t parent = getpid();
    int maxfd = (int) sysconf(_SC_OPEN_MAX);
    if (maxfd < 0 || maxfd > 65536) maxfd = 65536;
#if defined(SOCK_CLOEXEC)
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) != 0) {
#else
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
#endif
        set_errorf("cannot create the worker socket: %s", strerror(errno));
        return TI_E_SPAWN;
    }
    (void) fcntl(sv[0], F_SETFD, FD_CLOEXEC);
    (void) fcntl(sv[1], F_SETFD, FD_CLOEXEC);
#if defined(SO_NOSIGPIPE)
    {
        int one = 1;
        (void) setsockopt(sv[0], SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    }
#endif
    argv[0] = h->worker;
    argv[1] = (h->flags & TI_F_SECCOMP) ? seccomp_arg : NULL;
    argv[2] = NULL;

    pid_t pid = fork();
    if (pid < 0) {
        set_errorf("fork failed: %s", strerror(errno));
        close(sv[0]);
        close(sv[1]);
        return TI_E_SPAWN;
    }
    if (pid == 0) {
        /* ----- child ----- */
        sigset_t none;
        struct sigaction dfl;
        memset(&dfl, 0, sizeof dfl);
        dfl.sa_handler = SIG_DFL;
        for (int s = 1; s < 32; s++)
            if (s != SIGKILL && s != SIGSTOP) (void) sigaction(s, &dfl, NULL);
        sigemptyset(&none);
        (void) sigprocmask(SIG_SETMASK, &none, NULL);
#if defined(__linux__)
        /* die with the host */
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() != parent) child_fail();
#else
        (void) parent;
#endif
        if (sv[1] == 3) {
            if (fcntl(3, F_SETFD, 0) != 0) child_fail();
        } else if (dup2(sv[1], 3) != 3) {
            child_fail();
        }
        int nul = open("/dev/null", O_RDWR);
        if (nul < 0) child_fail();
        if (dup2(nul, 0) != 0 || dup2(nul, 1) != 1) child_fail();
        if (!(h->flags & TI_F_INHERIT_STDERR) && dup2(nul, 2) != 2) child_fail();
        /* close everything else */
#if defined(__linux__) && defined(SYS_close_range)
        if (syscall(SYS_close_range, 4u, ~0u, 0u) != 0)
#endif
            for (int fd = 4; fd < maxfd; fd++) (void) close(fd);
        if (setrlimit(RLIMIT_CORE, &rl_core) != 0) child_fail();
        if (setrlimit(RLIMIT_NOFILE, &rl_nofile) != 0) child_fail();
        if (h->mem_limit && setrlimit(RLIMIT_AS, &rl_as) != 0) child_fail();
        execve(h->worker, argv, envp);
        child_fail();
    }
    /* ----- host ----- */
    close(sv[1]);
    int fl = fcntl(sv[0], F_GETFL);
    if (fl < 0 || fcntl(sv[0], F_SETFL, fl | O_NONBLOCK) != 0) {
        close(sv[0]);
        kill(pid, SIGKILL);
        p->pid = pid;
        reap(p, 1);
        set_error("cannot make the worker socket non-blocking");
        return TI_E_SPAWN;
    }
    p->pid = pid;
    p->fd = sv[0];
    h->spawned++;
    return ZXV_OK;
}

/* ----- deadline-bounded I/O ----- */
#define IO_OK       0
#define IO_TIMEOUT  1
#define IO_EOF      2
#define IO_PROTOCOL 3
#define IO_NOMEM    4

static int send_all(int fd, const uint8_t *b, size_t n, int64_t deadline) {
    int flags = 0;
#if defined(MSG_NOSIGNAL)
    flags = MSG_NOSIGNAL;
#endif
    while (n) {
        ssize_t k = send(fd, b, n, flags);
        if (k > 0) {
            b += (size_t) k;
            n -= (size_t) k;
            continue;
        }
        if (k < 0 && errno == EINTR) continue;
        if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            int ms = remaining(deadline);
            struct pollfd pf = {fd, POLLOUT, 0};
            if (ms == 0) return IO_TIMEOUT;
            int r = poll(&pf, 1, ms);
            if (r < 0 && errno != EINTR) return IO_EOF;
            if (r > 0 && (pf.revents & (POLLERR | POLLHUP | POLLNVAL)) && !(pf.revents & POLLOUT)) return IO_EOF;
            continue;
        }
        return IO_EOF; /* EPIPE, ECONNRESET: the worker is gone */
    }
    return IO_OK;
}

static int recv_exact(int fd, uint8_t *b, size_t n, int64_t deadline) {
    while (n) {
        ssize_t k = recv(fd, b, n, 0);
        if (k > 0) {
            b += (size_t) k;
            n -= (size_t) k;
            continue;
        }
        if (k == 0) return IO_EOF;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            int ms = remaining(deadline);
            struct pollfd pf = {fd, POLLIN, 0};
            if (ms == 0) return IO_TIMEOUT;
            int r = poll(&pf, 1, ms);
            if (r < 0 && errno != EINTR) return IO_EOF;
            continue;
        }
        return IO_EOF;
    }
    return IO_OK;
}

/* One frame from the worker, of the expected type, into a malloc'ed body. */
static int recv_frame(int fd, uint32_t want, int64_t deadline, uint8_t **out_body, size_t *out_len) {
    uint8_t hdr[5];
    uint32_t len = 0;
    int rc = recv_exact(fd, hdr, 4, deadline);
    *out_body = NULL;
    *out_len = 0;
    if (rc != IO_OK) return rc;
    if (ti_frame_header(hdr, TI_REPLY_MAX, &len) != ZXV_OK) {
        set_error("the worker sent a frame with a bad length");
        return IO_PROTOCOL;
    }
    rc = recv_exact(fd, hdr + 4, 1, deadline);
    if (rc != IO_OK) return rc;
    if (hdr[4] != want) {
        set_errorf("the worker sent frame type %u, expected %u", (unsigned) hdr[4], (unsigned) want);
        return IO_PROTOCOL;
    }
    if (len > 1u) {
        uint8_t *b = (uint8_t *) malloc(len - 1u);
        if (!b) return IO_NOMEM;
        rc = recv_exact(fd, b, len - 1u, deadline);
        if (rc != IO_OK) {
            free(b);
            return rc;
        }
        *out_body = b;
        *out_len = len - 1u;
    }
    return IO_OK;
}

/* Turn an I/O failure into a status, killing the worker. */
static int32_t io_fail(ti_plugin *p, int rc, const char *what) {
    char sig[64] = "";
    int32_t st;
    if (rc == IO_EOF) {
        reap_grace(p, 100); /* a worker that closed the socket but lingers is killed */
        if (p->exit_how == TI_EXIT_SIGNAL) snprintf(sig, sizeof sig, " (killed by signal %d)", (int) p->exit_value);
        else if (p->exit_how == TI_EXIT_CODE) snprintf(sig, sizeof sig, " (exit status %d)", (int) p->exit_value);
        set_errorf("plugin '%s': the worker ended during %s%s", p->name, what, sig);
        st = TI_E_CRASHED;
    } else if (rc == IO_TIMEOUT) {
        set_errorf("plugin '%s': %s went past the timeout; the worker was killed", p->name, what);
        st = TI_E_TIMEOUT;
    } else if (rc == IO_NOMEM) {
        st = ZXV_E_NOMEM;
    } else {
        char why[256];
        copy_err(why, sizeof why);
        set_errorf("plugin '%s': protocol error during %s (%s); the worker was killed", p->name, what, why);
        st = TI_E_PROTOCOL;
    }
    kill_worker(p);
    return st;
}

/* ----- plugins ----- */
static int32_t start(ti_host *h, ti_plugin *p, const uint8_t *wasm, size_t len) {
    int64_t deadline = now_ms() + (int64_t) h->load_ms;
    uint8_t *body = NULL;
    size_t blen = 0;
    ti_hello hello;
    ti_loaded loaded;
    ti_load m;
    ti_buf out = {NULL, 0, 0};
    int32_t st = spawn(h, p);
    int rc;
    if (st != ZXV_OK) return st;

    rc = recv_frame(p->fd, TI_MSG_HELLO, deadline, &body, &blen);
    if (rc != IO_OK) {
        st = io_fail(p, rc, "start-up");
        if (st == TI_E_CRASHED) {
            char why[512];
            copy_err(why, sizeof why);
            set_errorf("%s; check the worker path '%s'", why, h->worker);
            st = TI_E_SPAWN;
        }
        return st;
    }
    {
        const char *bad = NULL;
        if (ti_dec_hello(body, blen, &hello) != ZXV_OK) bad = "HELLO: malformed";
        else if (hello.version != TI_PROTO_VERSION) bad = "HELLO: unknown protocol version";
        else if ((h->flags & TI_F_SECCOMP) && !(hello.flags & TI_HELLO_SECCOMP))
            bad = "HELLO: the worker did not install the seccomp filter";
        free(body);
        body = NULL;
        if (bad) {
            set_error(bad);
            return io_fail(p, IO_PROTOCOL, "start-up");
        }
    }

    memset(&m, 0, sizeof m);
    m.grants = h->grants;
    for (uint32_t i = 0; i < TI_LIMITS_COUNT; i++)
        (void) zw_policy_get_limit(h->policy, (int32_t) (i + 1u), &m.limits[i]);
    m.name_len = (uint32_t) strlen(p->name);
    memcpy(m.name, p->name, m.name_len + 1u);
    m.wasm = wasm;
    m.wasm_len = len;
    st = ti_enc_load(&out, &m);
    if (st != ZXV_OK) {
        kill_worker(p);
        ti_buf_free(&out);
        return st;
    }
    rc = send_all(p->fd, out.b, out.n, deadline);
    ti_buf_free(&out);
    if (rc == IO_OK) rc = recv_frame(p->fd, TI_MSG_LOADED, deadline, &body, &blen);
    if (rc != IO_OK) return io_fail(p, rc, "load");
    if (ti_dec_loaded(body, blen, &loaded) != ZXV_OK) {
        free(body);
        set_error("LOADED: malformed");
        return io_fail(p, IO_PROTOCOL, "load");
    }
    free(body);
    if (loaded.status != ZXV_OK) {
        set_errorf("plugin '%s': the worker refused it: %s", p->name, loaded.msg);
        stop_worker(p);
        return loaded.status;
    }
    return ZXV_OK;
}

int32_t ti_plugin_load(ti_host *host, const uint8_t *wasm, size_t len, const char *name,
                       ti_plugin **out_plugin) {
    ti_plugin *p;
    int32_t st;
    if (!out_plugin) return ZXV_E_ARG;
    *out_plugin = NULL;
    if (!host || !wasm || len == 0) {
        set_error("ti_plugin_load: NULL argument or empty module");
        return ZXV_E_ARG;
    }
    if (name && strlen(name) > TI_NAME_MAX) {
        set_error("ti_plugin_load: the name is longer than 31 bytes");
        return ZXV_E_TOO_LONG;
    }
    if (name)
        for (const char *c = name; *c; c++)
            if ((unsigned char) *c < 0x20u || (unsigned char) *c > 0x7Eu) {
                set_error("ti_plugin_load: the name must be printable ASCII");
                return ZXV_E_ARG;
            }
    if (len > TI_WASM_MAX) {
        set_error("ti_plugin_load: the module is larger than 4 MiB");
        return ZW_E_REJECTED;
    }
    /* The host's own validation first: a refused module never reaches a
     * worker. */
    {
        char why[400];
        size_t n = 0;
        why[0] = '\0';
        st = zw_validate(host->policy, wasm, len, NULL, NULL, NULL, NULL, why, sizeof why, &n);
        if (st != ZXV_OK) {
            set_errorf("the host's validator refused the module: %s", why);
            return ZW_E_REJECTED;
        }
    }
    p = (ti_plugin *) calloc(1, sizeof *p);
    if (!p) return ZXV_E_NOMEM;
    p->host = host;
    p->fd = -1;
    p->state = TI_PLUGIN_READY;
    snprintf(p->name, sizeof p->name, "%s", name ? name : "");
    st = start(host, p, wasm, len);
    if (st != ZXV_OK) {
        if (p->pid > 0 || p->fd >= 0) kill_worker(p);
        free(p);
        return st;
    }
    *out_plugin = p;
    return ZXV_OK;
}

int32_t ti_plugin_stop(ti_plugin *plugin) {
    if (!plugin) return ZXV_E_ARG;
    stop_worker(plugin);
    return ZXV_OK;
}

void ti_plugin_destroy(ti_plugin *plugin) {
    if (!plugin) return;
    stop_worker(plugin);
    free(plugin);
}

int32_t ti_plugin_state(const ti_plugin *plugin, int32_t *out_state) {
    if (!plugin || !out_state) return ZXV_E_ARG;
    *out_state = plugin->state;
    return ZXV_OK;
}

int32_t ti_plugin_pid(const ti_plugin *plugin, int64_t *out_pid) {
    if (!plugin || !out_pid) return ZXV_E_ARG;
    *out_pid = (int64_t) plugin->pid;
    return ZXV_OK;
}

int32_t ti_plugin_exit(const ti_plugin *plugin, int32_t *out_how, int32_t *out_value) {
    if (!plugin) return ZXV_E_ARG;
    if (out_how) *out_how = plugin->pid > 0 ? TI_EXIT_RUNNING : plugin->exit_how;
    if (out_value) *out_value = plugin->pid > 0 ? 0 : plugin->exit_value;
    return ZXV_OK;
}

/* Charge `fuel` to the host's budget. */
static int32_t charge(ti_host *h, uint64_t fuel) {
    if (fuel == 0) return ZXV_OK;
    return zxv_rmag_consume(h->budget_cap, h->rmag, h->budget, (int64_t) fuel, 1);
}

static int32_t enqueue(ti_host *h, const char *plugin_name, const ti_result *r) {
    if (r->nprops == 0) return ZXV_OK;
    if (h->pending + r->nprops > TI_PENDING_MAX) {
        set_errorf("the host's queue is full (%u pending): the call's %u proposal(s) were dropped",
                   (unsigned) h->pending, (unsigned) r->nprops);
        return ZXV_E_CAPACITY;
    }
    if (h->qn + r->nprops > h->qcap) {
        size_t c = h->qcap ? h->qcap : 64u;
        while (c < h->qn + r->nprops) c *= 2u;
        ti_qprop *q = (ti_qprop *) realloc(h->q, c * sizeof *q);
        if (!q) return ZXV_E_NOMEM;
        h->q = q;
        h->qcap = c;
    }
    for (uint32_t i = 0; i < r->nprops; i++) {
        ti_qprop *e = &h->q[h->qn++];
        memset(e, 0, sizeof *e);
        e->id = h->next_id++;
        e->state = ZXV_LP_NEITHER;
        e->kind = r->props[i].kind;
        e->num = r->props[i].num;
        e->den = r->props[i].den;
        memcpy(e->plugin, plugin_name, sizeof e->plugin);
        memcpy(e->label, r->props[i].label, r->props[i].label_len);
        e->label_len = r->props[i].label_len;
        h->pending++;
    }
    return ZXV_OK;
}

int32_t ti_plugin_call(ti_plugin *plugin, const char *export_name, const int64_t *args,
                       uint32_t nargs, int64_t *out_result, int32_t *out_has_result,
                       uint64_t *out_fuel_used) {
    ti_host *h;
    ti_call c;
    ti_result *r = NULL;
    ti_buf out = {NULL, 0, 0};
    uint8_t *body = NULL;
    size_t blen = 0, elen;
    int64_t rn = 0, rd = 1;
    uint64_t fuel, charged;
    int32_t st;
    int rc;
    if (out_fuel_used) *out_fuel_used = 0;
    if (out_has_result) *out_has_result = 0;
    if (!plugin || !export_name || (nargs && !args)) {
        set_error("ti_plugin_call: NULL argument");
        return ZXV_E_ARG;
    }
    h = plugin->host;
    if (plugin->state != TI_PLUGIN_READY) {
        set_errorf("plugin '%s' stopped earlier; destroy it", plugin->name);
        return TI_E_DEAD;
    }
    elen = strlen(export_name);
    if (elen == 0 || elen > TI_EXPORT_MAX || nargs > TI_ARGS_MAX) {
        set_error("ti_plugin_call: export name of 1..128 bytes and at most 8 arguments");
        return ZXV_E_ARG;
    }

    /* Fuel = min(call fuel, floor(remaining budget)). */
    st = zxv_cap_check(h->budget_cap, ZXV_CAP_BUDGET);
    if (st != ZXV_OK) {
        set_error("ti_plugin_call: the host's budget capability is no longer valid");
        return st;
    }
    st = zxv_rmag_status(h->rmag, h->budget, &rn, &rd, NULL, NULL, NULL);
    if (st != ZXV_OK) return st;
    fuel = (rn > 0 && rd > 0) ? (uint64_t) (rn / rd) : 0u;
    if (fuel > h->call_fuel) fuel = h->call_fuel;
    if (fuel == 0) {
        set_errorf("plugin '%s': the rmag budget has no fuel left", plugin->name);
        return ZXV_E_BUDGET;
    }

    memset(&c, 0, sizeof c);
    c.fuel = fuel;
    c.export_len = (uint32_t) elen;
    memcpy(c.export_name, export_name, elen + 1u);
    c.nargs = nargs;
    for (uint32_t i = 0; i < nargs; i++) c.args[i] = args[i];
    st = ti_enc_call(&out, &c);
    if (st != ZXV_OK) {
        ti_buf_free(&out);
        return st;
    }
    h->calls++;
    int64_t deadline = now_ms() + (int64_t) h->call_ms;
    rc = send_all(plugin->fd, out.b, out.n, deadline);
    ti_buf_free(&out);
    if (rc == IO_OK) rc = recv_frame(plugin->fd, TI_MSG_RESULT, deadline, &body, &blen);
    if (rc == IO_OK) {
        r = (ti_result *) malloc(sizeof *r);
        if (!r) rc = IO_NOMEM;
        else if (ti_dec_result(body, blen, r) != ZXV_OK) {
            set_error("RESULT: malformed");
            rc = IO_PROTOCOL;
        } else if (r->fuel_used > fuel) {
            set_errorf("RESULT: %llu fuel reported, %llu given", (unsigned long long) r->fuel_used,
                       (unsigned long long) fuel);
            rc = IO_PROTOCOL;
        }
    }
    free(body);
    if (rc != IO_OK) {
        /* the worker misbehaved or vanished: the whole allotment is spent */
        free(r);
        st = io_fail(plugin, rc, "a call");
        if (charge(h, fuel) == ZXV_OK && out_fuel_used) *out_fuel_used = fuel;
        return st;
    }

    st = r->status;
    charged = r->fuel_used;
    if (st == ZW_E_FUEL || ((h->flags & TI_F_CHARGE_ALLOTTED) && (st == ZXV_OK || st == ZW_E_TRAP)))
        charged = fuel;
    {
        int32_t cs = charge(h, charged);
        if (cs != ZXV_OK) {
            /* the budget changed under us (another user of the rmag) */
            kill_worker(plugin);
            free(r);
            set_errorf("plugin '%s': could not charge %llu fuel", plugin->name, (unsigned long long) charged);
            return cs;
        }
    }
    if (out_fuel_used) *out_fuel_used = charged;

    if (st == ZXV_OK) {
        if (out_result) *out_result = r->result;
        if (out_has_result) *out_has_result = r->has_result;
        st = enqueue(h, plugin->name, r);
    } else if (st == ZW_E_FUEL || st == ZW_E_TRAP) {
        set_errorf("plugin '%s': %s: %s; the worker was killed", plugin->name, zw_status_name(st), r->msg);
        kill_worker(plugin);
    } else {
        set_errorf("plugin '%s': %s", plugin->name, r->msg);
    }
    free(r);
    return st;
}

/* ----- the queue ----- */
static ti_qprop *find(const ti_host *h, uint64_t id) {
    /* ids are dense and increasing: binary search */
    size_t lo = 0, hi = h->qn;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (h->q[mid].id < id) lo = mid + 1u;
        else hi = mid;
    }
    return (lo < h->qn && h->q[lo].id == id) ? &h->q[lo] : NULL;
}

int32_t ti_queue_pending(const ti_host *host, uint32_t *out_count) {
    if (!host || !out_count) return ZXV_E_ARG;
    *out_count = host->pending;
    return ZXV_OK;
}

int32_t ti_queue_next(const ti_host *host, uint64_t *out_id) {
    if (!host || !out_id) return ZXV_E_ARG;
    for (size_t i = 0; i < host->qn; i++)
        if (host->q[i].state == ZXV_LP_NEITHER) {
            *out_id = host->q[i].id;
            return ZXV_OK;
        }
    return ZXV_E_NOT_FOUND;
}

int32_t ti_proposal_get(const ti_host *host, uint64_t id, int32_t *out_state, uint32_t *out_kind,
                        int64_t *out_num, int64_t *out_den, char plugin_name[32], uint8_t *label,
                        size_t label_cap, size_t *out_label_len) {
    const ti_qprop *e;
    if (!host) return ZXV_E_ARG;
    e = find(host, id);
    if (!e) return ZXV_E_NOT_FOUND;
    if (out_label_len) *out_label_len = e->label_len;
    if (!label || label_cap <= e->label_len) return ZXV_E_BUFFER;
    memcpy(label, e->label, e->label_len);
    label[e->label_len] = 0;
    if (out_state) *out_state = e->state;
    if (out_kind) *out_kind = e->kind;
    if (out_num) *out_num = e->num;
    if (out_den) *out_den = e->den;
    if (plugin_name) memcpy(plugin_name, e->plugin, 32);
    return ZXV_OK;
}

static int32_t decide(ti_host *host, const zxv_cap *cap, uint64_t id, ti_qprop **out) {
    ti_qprop *e;
    int32_t st;
    if (!host || !cap) return ZXV_E_ARG;
    e = find(host, id);
    if (!e) return ZXV_E_NOT_FOUND;
    if (e->state != ZXV_LP_NEITHER) return ZXV_E_STATE;
    st = zxv_cap_check(cap, host->accept_caps);
    if (st != ZXV_OK) {
        set_error("the capability may not decide proposals (accept caps)");
        return st;
    }
    *out = e;
    return ZXV_OK;
}

int32_t ti_proposal_accept(ti_host *host, const zxv_cap *cap, uint64_t id) {
    ti_qprop *e = NULL;
    int32_t st = decide(host, cap, id, &e);
    if (st != ZXV_OK) return st;
    e->state = ZXV_LP_TRUE;
    host->pending--;
    return ZXV_OK;
}

int32_t ti_proposal_reject(ti_host *host, const zxv_cap *cap, uint64_t id) {
    ti_qprop *e = NULL;
    int32_t st = decide(host, cap, id, &e);
    if (st != ZXV_OK) return st;
    e->state = ZXV_LP_FALSE;
    host->pending--;
    return ZXV_OK;
}

int32_t ti_proposal_accept_post(ti_host *host, const zxv_cap *cap, uint64_t id,
                                uint32_t from_account, uint32_t to_account, const char *ccy,
                                const char *reference, uint64_t *out_entry_id) {
    ti_qprop *e = NULL;
    int32_t st = decide(host, cap, id, &e);
    if (st != ZXV_OK) return st;
    if (e->den != 1 || e->num <= 0) {
        set_error("ti_proposal_accept_post: the value is not a positive integer");
        return ZXV_E_RANGE;
    }
    st = zxv_ledger_post_cap(cap, from_account, to_account, e->num, ccy, reference, out_entry_id);
    if (st != ZXV_OK) return st;
    e->state = ZXV_LP_TRUE;
    host->pending--;
    return ZXV_OK;
}

#endif /* POSIX */
