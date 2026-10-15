/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_shell.c — programs run for the terminal and scripts (see
 * zxvd_shell.h H1-H7). */
#if !defined(_WIN32)
#    define _DEFAULT_SOURCE
#    define _DARWIN_C_SOURCE
#    include <sys/types.h>
#    include <sys/select.h>
#    include <sys/stat.h>
#    include <sys/wait.h>
#    include <fcntl.h>
#    include <signal.h>
#    include <unistd.h>
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxvd_shell.h"
#include "zxvd_log.h"  /* zxvd_mono_us, and a line per job */
#include "web4_web2.h" /* the argv array */

#define E_PARAMS (-32602)

static char ZXSH[1024]; /* H7: "" = off */

int zxvd_shell_enable(const char *zxsh_path)
{
    size_t n = zxsh_path ? strlen(zxsh_path) : 0;
    if (n >= sizeof ZXSH) return -1;
    memcpy(ZXSH, zxsh_path ? zxsh_path : "", n + 1);
    return 0;
}

/* ===================== UTF-8 text out (H2) ===================== */

/* The length of the valid UTF-8 character at p (n bytes there), 0 when it
 * is not one, or -1 when it may be one cut short by the end. */
static int utf8_len(const uint8_t *p, size_t n)
{
    uint8_t c = p[0];
    int k = c < 0x80                 ? 1
            : c >= 0xC2 && c <= 0xDF ? 2
            : c >= 0xE0 && c <= 0xEF ? 3
            : c >= 0xF0 && c <= 0xF4 ? 4
                                     : 0;
    if (k <= 1) return k;
    for (int i = 1; i < k; i++) {
        if ((size_t) i >= n) return -1;
        uint8_t d = p[i];
        if ((d & 0xC0) != 0x80) return 0;
        if (i == 1 && ((c == 0xE0 && d < 0xA0) || (c == 0xED && d > 0x9F) ||
                       (c == 0xF0 && d < 0x90) || (c == 0xF4 && d > 0x8F)))
            return 0; /* overlong, a surrogate, or past U+10FFFF */
    }
    return k;
}

/* Write p[0..n) as a JSON string; returns how many bytes it took (fewer
 * than n only when the last character is cut and `final` is false). */
static size_t json_text(zxvd_buf_t *b, const uint8_t *p, size_t n, bool final)
{
    size_t i = 0;
    zxvd_buf_raw(b, "\"", 1);
    while (i < n) {
        int k = utf8_len(p + i, n - i);
        if (k < 0 && !final) break; /* the rest comes with the next read */
        if (k <= 0) {
            zxvd_buf_raw(b, "\\ufffd", 6);
            i++;
            continue;
        }
        uint8_t c = p[i];
        if (c == '"' || c == '\\') {
            char e[2] = {'\\', (char) c};
            zxvd_buf_raw(b, e, 2);
        } else if (c == '\n') {
            zxvd_buf_raw(b, "\\n", 2);
        } else if (c < 0x20 || c == 0x7f) {
            zxvd_buf_printf(b, "\\u%04x", c);
        } else {
            zxvd_buf_raw(b, (const char *) p + i, (size_t) k);
        }
        i += (size_t) k;
    }
    zxvd_buf_raw(b, "\"", 1);
    return i;
}

#if defined(_WIN32)

int zxvd_shell_fds(void *set, int max)
{
    (void) set;
    return max;
}
void zxvd_shell_service(void *set)
{
    (void) set;
}
void zxvd_shell_close(void) {}

static void not_here(zxvd_call_t *c)
{
    (void) json_text;
    zxvd_fail(c, ZXVD_E_SHELL, "shell.* is not on Windows yet");
}
void zxvd_shell_run(zxvd_call_t *c)
{
    not_here(c);
}
void zxvd_shell_read(zxvd_call_t *c)
{
    not_here(c);
}
void zxvd_shell_write(zxvd_call_t *c)
{
    not_here(c);
}
void zxvd_shell_kill(zxvd_call_t *c)
{
    not_here(c);
}
void zxvd_shell_jobs(zxvd_call_t *c)
{
    zxvd_buf_printf(zxvd_result(c), "{\"on\":false,\"jobs\":[]}");
}

#else

typedef struct {
    uint32_t id; /* 0: a free place */
    pid_t pid;   /* 0 once the process has ended and been reaped */
    int out, in; /* -1 when closed */
    bool line;   /* a zxsh line (H4) */
    bool done, timed_out, killed;
    int code, sig;  /* the exit code, or the signal that ended it (sig > 0) */
    uint64_t total; /* bytes of output so far */
    uint8_t *ring;  /* the last ZXVD_SHELL_KEEP of them */
    uint64_t deadline_ms, kill_ms, ended_ms;
    char what[96]; /* the line or the program, for shell.jobs */
} job_t;

static job_t J[ZXVD_SHELL_JOBS];
static uint32_t NEXT_ID = 1;

static uint64_t now_ms(void)
{
    return zxvd_mono_us() / 1000u;
}

static job_t *job_of(uint32_t id)
{
    for (uint32_t i = 0; id && i < ZXVD_SHELL_JOBS; i++)
        if (J[i].id == id) return &J[i];
    return NULL;
}

static void close_fd(int *fd)
{
    if (*fd >= 0) close(*fd);
    *fd = -1;
}

static void forget(job_t *j)
{
    close_fd(&j->out);
    close_fd(&j->in);
    free(j->ring);
    memset(j, 0, sizeof *j);
    j->out = j->in = -1;
}

/* H5: TERM now, KILL two seconds later */
static void end_job(job_t *j, uint64_t now)
{
    if (j->pid <= 0 || j->kill_ms) return;
    kill(-j->pid, SIGTERM);
    j->kill_ms = now + 2000u;
}

/* Take what the pipe holds now (up to 1 MiB a call); false at its end. */
static void drain(job_t *j)
{
    static uint8_t tmp[64 * 1024];
    for (uint32_t got = 0; j->out >= 0 && got < 1024u * 1024u;) {
        ssize_t k = read(j->out, tmp, sizeof tmp);
        if (k < 0 && errno == EINTR) continue;
        if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (k <= 0) {
            close_fd(&j->out);
            return;
        }
        for (ssize_t i = 0; i < k; i++)
            j->ring[(j->total + (uint64_t) i) % ZXVD_SHELL_KEEP] = tmp[i];
        j->total += (uint64_t) k;
        got += (uint32_t) k;
    }
}

/* Read, reap and time out one job. */
static void pump(job_t *j, uint64_t now)
{
    if (!j->id) return;
    drain(j);
    if (j->pid > 0) {
        int st;
        pid_t r = waitpid(j->pid, &st, WNOHANG);
        if (r == j->pid) {
            j->pid = 0;
            j->done = true;
            j->ended_ms = now;
            if (WIFEXITED(st))
                j->code = WEXITSTATUS(st);
            else if (WIFSIGNALED(st))
                j->sig = WTERMSIG(st);
            drain(j);          /* what it wrote before it ended */
            close_fd(&j->out); /* H4: a child it left behind is not waited for */
            close_fd(&j->in);
            zxvd_log(ZXVD_LOG_INFO, "shell", "job %u ended (%s %d)", j->id,
                     j->sig ? "signal" : "exit", j->sig ? j->sig : j->code);
        } else if (r < 0 && errno == ECHILD) { /* reaped elsewhere: nothing more to learn */
            j->pid = 0;
            j->done = true;
            j->code = -1;
            j->ended_ms = now;
            close_fd(&j->out);
            close_fd(&j->in);
        }
    }
    if (j->pid > 0 && now >= j->deadline_ms && !j->kill_ms) {
        j->timed_out = true;
        end_job(j, now);
    }
    if (j->pid > 0 && j->kill_ms && now >= j->kill_ms) kill(-j->pid, SIGKILL);
}

int zxvd_shell_fds(void *set, int max)
{
    fd_set *s = set;
    for (uint32_t i = 0; i < ZXVD_SHELL_JOBS; i++)
        if (J[i].id && J[i].out >= 0 && J[i].out < FD_SETSIZE) {
            FD_SET(J[i].out, s);
            if (J[i].out > max) max = J[i].out;
        }
    return max;
}

void zxvd_shell_service(void *set)
{
    (void) set; /* every job is pumped: a reap or a deadline needs no ready pipe */
    uint64_t now = now_ms();
    for (uint32_t i = 0; i < ZXVD_SHELL_JOBS; i++) pump(&J[i], now);
}

void zxvd_shell_close(void)
{
    bool any = false;
    for (uint32_t i = 0; i < ZXVD_SHELL_JOBS; i++)
        if (J[i].id && J[i].pid > 0) {
            kill(-J[i].pid, SIGTERM);
            any = true;
        }
    for (int t = 0; any && t < 20; t++) { /* up to 0.2 s to go quietly */
        any = false;
        for (uint32_t i = 0; i < ZXVD_SHELL_JOBS; i++)
            if (J[i].id && J[i].pid > 0 && waitpid(J[i].pid, NULL, WNOHANG) == 0)
                any = true;
            else if (J[i].id)
                J[i].pid = 0;
        if (any) usleep(10000);
    }
    for (uint32_t i = 0; i < ZXVD_SHELL_JOBS; i++) {
        if (J[i].id && J[i].pid > 0) {
            kill(-J[i].pid, SIGKILL);
            waitpid(J[i].pid, NULL, 0);
        }
        if (J[i].id) forget(&J[i]);
    }
}

/* ===================== the methods ===================== */

static const char *verdict(const job_t *j)
{
    static const int CRASH[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGSYS, SIGTRAP};
    if (j->sig) {
        for (size_t i = 0; i < sizeof CRASH / sizeof CRASH[0]; i++)
            if (j->sig == CRASH[i]) return "forbidden";
        return "neither";
    }
    if (j->line) {
        switch (j->code) {
        case 0:
            return "true";
        case 3:
            return "forbidden";
        case 4:
            return "both";
        case 5:
            return "neither";
        default:
            return "false";
        }
    }
    return j->code == 0 ? "true" : j->code == 126 || j->code == 127 ? "neither" : "false";
}

static const char *sig_name(int s)
{
    switch (s) {
    case SIGTERM:
        return "SIGTERM";
    case SIGKILL:
        return "SIGKILL";
    case SIGINT:
        return "SIGINT";
    case SIGHUP:
        return "SIGHUP";
    case SIGPIPE:
        return "SIGPIPE";
    case SIGSEGV:
        return "SIGSEGV";
    case SIGBUS:
        return "SIGBUS";
    case SIGILL:
        return "SIGILL";
    case SIGFPE:
        return "SIGFPE";
    case SIGABRT:
        return "SIGABRT";
    case SIGSYS:
        return "SIGSYS";
    case SIGTRAP:
        return "SIGTRAP";
    default:
        return "other";
    }
}

/* a place for a new job: a free one, else the finished job that ended first */
static job_t *place(void)
{
    job_t *old = NULL;
    for (uint32_t i = 0; i < ZXVD_SHELL_JOBS; i++) {
        if (!J[i].id) return &J[i];
        if (J[i].done && (!old || J[i].ended_ms < old->ended_ms)) old = &J[i];
    }
    if (old) forget(old);
    return old;
}

static bool p_job(zxvd_call_t *c, job_t **j)
{
    uint32_t id = 0;
    if (!zxvd_param_u32(c, "job", &id, true)) return false;
    if ((*j = job_of(id))) pump(*j, now_ms());
    if (!*j || !(*j)->id) {
        zxvd_fail(c, ZXVD_E_SHELL, "no job %u (it ended and was read to the end, or never was)",
                  id);
        return false;
    }
    return true;
}

void zxvd_shell_run(zxvd_call_t *c)
{
    static char line[ZXVD_SHELL_LINE_MAX + 1], cwd[1024];
    static char argbuf[ZXVD_SHELL_LINE_MAX + ZXVD_SHELL_ARGS_MAX];
    char *argv[ZXVD_SHELL_ARGS_MAX + 4];
    uint32_t timeout = ZXVD_SHELL_TIMEOUT_MS;
    zxvd_json_t a;
    bool have_line = zxvd_param_json(c, "line", &a), have_argv = zxvd_param_json(c, "argv", &a);
    if (!ZXSH[0]) return zxvd_fail(c, ZXVD_E_SHELL, "shell.* is off in this zxvd");
    if (have_line == have_argv)
        return zxvd_fail(c, E_PARAMS,
                         "give \"line\" (a zxsh line) or \"argv\" (a program), not both");
    if (!zxvd_param_str(c, "cwd", cwd, sizeof cwd, false) ||
        !zxvd_param_u32(c, "timeout_ms", &timeout, false))
        return;
    if (timeout < 1 || timeout > ZXVD_SHELL_TIMEOUT_TOP)
        return zxvd_fail(c, E_PARAMS, "\"timeout_ms\" must be 1 to %u", ZXVD_SHELL_TIMEOUT_TOP);
    if (have_line) {
        if (!zxvd_param_str(c, "line", line, sizeof line, true)) return;
        if (!line[0]) return zxvd_fail(c, E_PARAMS, "\"line\" is empty");
        argv[0] = ZXSH;
        argv[1] = "-c";
        argv[2] = line;
        argv[3] = NULL;
    } else {
        const w4_jtok_t *t = a.tok;
        if (t[a.at].type != W4_J_ARR || t[a.at].size < 1 || t[a.at].size > ZXVD_SHELL_ARGS_MAX)
            return zxvd_fail(c, E_PARAMS, "\"argv\" must be 1 to %u strings", ZXVD_SHELL_ARGS_MAX);
        size_t off = 0;
        for (uint32_t i = 0; i < t[a.at].size; i++) {
            int32_t k = w4_json_at(t, a.n, a.at, i);
            int32_t got =
                k >= 0 && t[k].type == W4_J_STR
                    ? w4_json_str(a.js, &t[k], argbuf + off, (uint32_t) (sizeof argbuf - off))
                    : -1;
            if (got < 0 || (size_t) got != strlen(argbuf + off) || (i == 0 && got == 0))
                return zxvd_fail(c, E_PARAMS,
                                 "\"argv\" must be 1 to %u strings with no NUL, the first not "
                                 "empty, %u bytes in all",
                                 ZXVD_SHELL_ARGS_MAX, ZXVD_SHELL_LINE_MAX);
            argv[i] = argbuf + off;
            off += (size_t) got + 1;
        }
        argv[t[a.at].size] = NULL;
    }
    struct stat st;
    if (cwd[0] && (stat(cwd, &st) != 0 || !S_ISDIR(st.st_mode)))
        return zxvd_fail(c, ZXVD_E_SHELL, "\"cwd\" is not a folder");
    job_t *j = place();
    if (!j) return zxvd_fail(c, ZXVD_E_SHELL, "%u jobs are running", ZXVD_SHELL_JOBS);
    if (!(j->ring = malloc(ZXVD_SHELL_KEEP))) return zxvd_fail(c, ZXVD_E_SHELL, "out of memory");
    int po[2] = {-1, -1}, pi[2] = {-1, -1};
    if (pipe(po) != 0 || pipe(pi) != 0) {
        int e = errno;
        for (int i = 0; i < 2; i++) {
            if (po[i] >= 0) close(po[i]);
            if (pi[i] >= 0) close(pi[i]);
        }
        forget(j);
        return zxvd_fail(c, ZXVD_E_SHELL, "no pipe: %s", strerror(e));
    }
    uint32_t id = NEXT_ID++;
    if (!NEXT_ID) NEXT_ID = 1;
    pid_t pid = fork();
    if (pid == 0) { /* the child: its own process group, the pipes as 0, 1 and 2 */
        setpgid(0, 0);
        signal(SIGPIPE, SIG_DFL);
        dup2(pi[0], 0);
        dup2(po[1], 1);
        dup2(po[1], 2);
        close(pi[0]);
        close(pi[1]);
        close(po[0]);
        close(po[1]);
        char idv[16];
        snprintf(idv, sizeof idv, "%u", id);
        setenv("ZXSH_JOB", idv, 1);
        if (cwd[0] && chdir(cwd) != 0) _exit(127);
        if (have_line)
            execv(argv[0], argv);
        else
            execvp(argv[0], argv);
        dprintf(2, "%s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    close(pi[0]);
    close(po[1]);
    if (pid < 0) {
        int e = errno;
        close(pi[1]);
        close(po[0]);
        forget(j);
        return zxvd_fail(c, ZXVD_E_SHELL, "could not start it: %s", strerror(e));
    }
    setpgid(pid, pid); /* also here, so a kill before the child's own call still reaches it */
    fcntl(po[0], F_SETFD, FD_CLOEXEC);
    fcntl(pi[1], F_SETFD, FD_CLOEXEC);
    fcntl(po[0], F_SETFL, fcntl(po[0], F_GETFL) | O_NONBLOCK);
    fcntl(pi[1], F_SETFL, fcntl(pi[1], F_GETFL) | O_NONBLOCK);
    j->id = id;
    j->pid = pid;
    j->out = po[0];
    j->in = pi[1];
    j->line = have_line;
    j->deadline_ms = now_ms() + timeout;
    const char *w = have_line ? line : argv[0];
    size_t wn = strlen(w) < sizeof j->what - 1 ? strlen(w) : sizeof j->what - 1;
    while (wn && wn < strlen(w) && ((uint8_t) w[wn] & 0xC0) == 0x80) wn--; /* a whole character */
    memcpy(j->what, w, wn);
    j->what[wn] = 0;
    zxvd_log(ZXVD_LOG_INFO, "shell", "job %u started (%s)", id,
             have_line ? "zxsh line" : "program");
    zxvd_buf_printf(zxvd_result(c), "{\"job\":%u,\"pid\":%ld}", id, (long) pid);
}

void zxvd_shell_read(zxvd_call_t *c)
{
    static uint8_t win[ZXVD_SHELL_READ_MAX];
    job_t *j;
    int64_t from = 0;
    uint32_t max = ZXVD_SHELL_READ_MAX;
    if (!p_job(c, &j) || !zxvd_param_amount(c, "from", &from, false) ||
        !zxvd_param_u32(c, "max", &max, false))
        return;
    if (from < 0) return zxvd_fail(c, E_PARAMS, "\"from\" must not be negative");
    if (max < 1 || max > ZXVD_SHELL_READ_MAX)
        return zxvd_fail(c, E_PARAMS, "\"max\" must be 1 to %u", ZXVD_SHELL_READ_MAX);
    uint64_t kept = j->total > ZXVD_SHELL_KEEP ? j->total - ZXVD_SHELL_KEEP : 0;
    uint64_t at = (uint64_t) from, dropped = 0;
    if (at > j->total) at = j->total;
    if (at < kept) {
        dropped = kept - at;
        at = kept;
    }
    size_t n = j->total - at < max ? (size_t) (j->total - at) : max;
    for (size_t i = 0; i < n; i++) win[i] = j->ring[(at + i) % ZXVD_SHELL_KEEP];
    bool all_in = j->done && j->out < 0; /* nothing more will come */
    zxvd_buf_t *r = zxvd_result(c);
    zxvd_buf_printf(r, "{\"job\":%u,\"out\":", j->id);
    size_t took = json_text(r, win, n, all_in && at + n == j->total);
    uint64_t next = at + took;
    bool finished = all_in && next == j->total;
    zxvd_buf_printf(r, ",\"from\":\"%llu\",\"next\":\"%llu\",\"dropped\":\"%llu\",\"done\":%s",
                    (unsigned long long) at, (unsigned long long) next,
                    (unsigned long long) dropped, j->done ? "true" : "false");
    if (j->done) {
        if (j->sig)
            zxvd_buf_printf(r, ",\"exit\":null,\"signal\":\"%s\"", sig_name(j->sig));
        else
            zxvd_buf_printf(r, ",\"exit\":%d,\"signal\":null", j->code);
        zxvd_buf_printf(r, ",\"verdict\":\"%s\"", verdict(j));
    }
    zxvd_buf_printf(r, ",\"timed_out\":%s,\"killed\":%s,\"kept\":%s}",
                    j->timed_out ? "true" : "false", j->killed ? "true" : "false",
                    finished ? "false" : "true");
    if (finished) forget(j); /* H5: read to the end */
}

void zxvd_shell_write(zxvd_call_t *c)
{
    static char text[ZXVD_SHELL_LINE_MAX + 1];
    job_t *j;
    bool cl = false;
    if (!p_job(c, &j) || !zxvd_param_str(c, "text", text, sizeof text, false) ||
        !zxvd_param_bool(c, "close", &cl, false))
        return;
    if (j->in < 0) return zxvd_fail(c, ZXVD_E_SHELL, "job %u's input is closed", j->id);
    size_t n = strlen(text), off = 0;
    while (off < n) {
        ssize_t k = write(j->in, text + off, n - off);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) break; /* full (EAGAIN), or the job stopped reading */
        off += (size_t) k;
    }
    if (cl) close_fd(&j->in);
    zxvd_buf_printf(zxvd_result(c), "{\"job\":%u,\"written\":%zu,\"closed\":%s}", j->id, off,
                    j->in < 0 ? "true" : "false");
}

void zxvd_shell_kill(zxvd_call_t *c)
{
    job_t *j;
    if (!p_job(c, &j)) return;
    uint32_t id = j->id;
    bool running = j->pid > 0;
    if (running) {
        j->killed = true;
        end_job(j, now_ms());
    } else {
        forget(j); /* finished: let it go unread */
    }
    zxvd_buf_printf(zxvd_result(c), "{\"job\":%u,\"was_running\":%s}", id,
                    running ? "true" : "false");
}

void zxvd_shell_jobs(zxvd_call_t *c)
{
    zxvd_buf_t *r = zxvd_result(c);
    uint64_t now = now_ms();
    zxvd_buf_printf(r, "{\"on\":%s,\"jobs\":[", ZXSH[0] ? "true" : "false");
    bool first = true;
    for (uint32_t i = 0; i < ZXVD_SHELL_JOBS; i++) {
        job_t *j = &J[i];
        pump(j, now);
        if (!j->id) continue;
        zxvd_buf_printf(r, "%s{\"job\":%u,\"pid\":%ld,\"what\":", first ? "" : ",", j->id,
                        (long) j->pid);
        json_text(r, (const uint8_t *) j->what, strlen(j->what), true);
        zxvd_buf_printf(r, ",\"done\":%s,\"bytes\":\"%llu\"", j->done ? "true" : "false",
                        (unsigned long long) j->total);
        if (j->done) zxvd_buf_printf(r, ",\"verdict\":\"%s\"", verdict(j));
        zxvd_buf_raw(r, "}", 1);
        first = false;
    }
    zxvd_buf_raw(r, "]}", 2);
}

#endif
