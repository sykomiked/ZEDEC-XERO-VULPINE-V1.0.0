/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxsh_host.c — zxsh on a POSIX host (macOS, Linux, BSD).
 *
 *   H1 PROCESSES. Each pipeline stage is a fork/exec. Its end maps to a tag
 *      (zxsh_core.h S1): exit 0 TRUE, 1..125 FALSE, 126/127 NEITHER (could
 *      not run); SIGSEGV SIGBUS SIGILL SIGFPE SIGABRT SIGSYS SIGTRAP are
 *      FORBIDDEN (the OS's memory or instruction check caught it); SIGPIPE on
 *      a stage that is not the last is TRUE (its reader stopped early); any
 *      other signal is NEITHER/entropic (interrupted: no answer).
 *   H2 VERDICT CHANNEL. Every stage gets fd 3 open for writing, named by
 *      ZXSH_VERDICT_FD=3. A program may write one line "BASE[/POLARITY]",
 *      e.g. "both" or "true/negentropic" or "false/forbidden"; it replaces
 *      the exit-code mapping for that stage. A crash is never overridden.
 *   H3 HISTORY. Interactive lines go to $ZXSH_HOME/history (default
 *      ~/.zxsh), one per line: "HASH<TAB>SEQ<TAB>LETTER<TAB>TAG<TAB>TEXT",
 *      HASH = SHA-256(previous HASH hex || the rest of the line). `history
 *      verify` recomputes the chain: a broken link is FORBIDDEN, because an
 *      invariant of the file failed. The quarantine file uses the same chain.
 *   H4 ZXVD. `zxv METHOD [PARAMS-JSON]` sends one JSON-RPC 2.0 line to the
 *      zxvd socket ($ZXVD_SOCK, else the desktop app's default path) and
 *      prints the reply. An "error" member is FALSE; a result carrying
 *      "verdict":"both" (or another base word) uses that base; no socket or
 *      no reply is NEITHER/entropic.
 *   H5 LIMITS. Capabilities are checked by the shell (core S9), not by the
 *      kernel: a program that runs keeps the user's OS permissions. Jobs run
 *      in their own process group with no terminal input (no fg/bg/Ctrl-Z).
 *      Windows needs a CreateProcess executor (not here yet).
 *   H6 JOBS. `cmd &` forks a copy of the shell that runs the and-or list and
 *      sends its final tag back over a pipe. `wait` combines the jobs it
 *      waited for by zxsh_mux.h rule C (agreement keeps the answer,
 *      disagreement is both); finished jobs are announced before a prompt.
 *   H7 SUB-TERMINALS. `split`, `tab` and `together` go to zxsh_mux.c. Inside
 *      a sub-terminal (ZXSH_MUX set) the shell reports each line's tag as
 *      OSC 7770 so the frame can show it.
 *   H8 COMPATIBILITY. A line (or a script) zxsh cannot read, such as `case`
 *      or a function, is handed to bash (else sh), which says so on stderr;
 *      its exit status maps to a verdict as in H1. ZXSH_COMPAT=0 turns this
 *      off. Lines using `<?>` are never handed over.
 */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <dirent.h>
#include <unistd.h>
#include "zxsh_core.h"
#include "zxsh_line.h"
#include "zxsh_mux.h"
#include "../../kernel/src/robin_debanks/sha256.h"

#define VERDICT_FD 3

static char g_home[1024];
static bool g_color;

/* ------------------------------------------------------------- helpers */

static void out_cb(void *u, int fd, const char *s, size_t n)
{
    (void) u;
    while (n) {
        ssize_t w = write(fd, s, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return;
        }
        s += w;
        n -= (size_t) w;
    }
}

static void setenv_cb(void *u, const char *name, const char *value)
{
    (void) u;
    setenv(name, value, 1);
}

static void hex(const uint8_t *d, size_t n, char *o)
{
    static const char H[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        o[2 * i] = H[d[i] >> 4];
        o[2 * i + 1] = H[d[i] & 15];
    }
    o[2 * n] = 0;
}

static void why_set(char *why, uint32_t max, const char *s)
{
    if (why && max && !why[0]) snprintf(why, max, "%s", s);
}

/* -------------------------------------------------- H3: chained files */

static void chain_path(char *o, size_t n, const char *name)
{
    snprintf(o, n, "%s/%s", g_home, name);
}

/* Read the last line's hash (64 hex chars) and the line count. */
static void chain_tail(const char *path, char head[65], uint32_t *count)
{
    memset(head, '0', 64);
    head[64] = 0;
    *count = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        if (strlen(line) > 64 && line[64] == '\t') {
            memcpy(head, line, 64);
            (*count)++;
        }
    }
    fclose(f);
}

static void line_hash(const char *prev, const char *rest, size_t n, char out[65])
{
    sha256_ctx_t c;
    uint8_t d[32];
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) prev, 64);
    sha256_update(&c, (const uint8_t *) rest, n);
    sha256_final(&c, d);
    hex(d, 32, out);
}

static bool chain_append(const char *name, lpx_t tag, const char *text)
{
    char path[1200], prev[65], h[65], rest[2048];
    uint32_t count;
    chain_path(path, sizeof path, name);
    chain_tail(path, prev, &count);
    char esc[1600];
    size_t k = 0;
    for (const char *p = text; *p && k + 3 < sizeof esc; p++) {
        if (*p == '\n')
            esc[k++] = '\\', esc[k++] = 'n';
        else if (*p == '\t')
            esc[k++] = '\\', esc[k++] = 't';
        else if (*p == '\\')
            esc[k++] = '\\', esc[k++] = '\\';
        else
            esc[k++] = *p;
    }
    esc[k] = 0;
    int n = snprintf(rest, sizeof rest, "%u\t%c\t%s/%s/%s\t%s", count + 1, zxsh_letter(tag),
                     lpx_base_name(lpx_base(tag)), lpx_pol_name(lpx_pol(tag)),
                     lpx_phase_name(lpx_phase(tag)), esc);
    if (n < 0 || (size_t) n >= sizeof rest) return false;
    line_hash(prev, rest, (size_t) n, h);
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return false;
    dprintf(fd, "%s\t%s\n", h, rest);
    close(fd);
    return true;
}

/* Print (or verify) a chained file. Returns the tag for the command. */
static lpx_t chain_show(const char *name, bool verify, char *why, uint32_t wmax)
{
    char path[1200], prev[65], line[4096];
    chain_path(path, sizeof path, name);
    memset(prev, '0', 64);
    prev[64] = 0;
    FILE *f = fopen(path, "r");
    if (!f) {
        why_set(why, wmax, "empty");
        return zxsh_tag(LPX_B_TRUE, LPX_P_NEUTRAL);
    }
    uint32_t n = 0;
    while (fgets(line, sizeof line, f)) {
        size_t L = strlen(line);
        if (L && line[L - 1] == '\n') line[--L] = 0;
        n++;
        if (L <= 65 || line[64] != '\t') {
            fclose(f);
            snprintf(why, wmax, "%s line %u is malformed", name, n);
            return zxsh_tag(LPX_B_FALSE, LPX_P_FORBIDDEN);
        }
        char h[65];
        line_hash(prev, line + 65, L - 65, h);
        if (memcmp(h, line, 64) != 0) {
            fclose(f);
            snprintf(why, wmax, "%s line %u was altered (hash chain broken)", name, n);
            return zxsh_tag(LPX_B_FALSE, LPX_P_FORBIDDEN);
        }
        memcpy(prev, line, 64);
        if (!verify) printf("%s\n", line + 65);
    }
    fclose(f);
    fflush(stdout);
    if (verify) {
        printf("%s: %u lines, chain intact, head %.16s\n", name, n, prev);
        fflush(stdout);
    }
    why_set(why, wmax, verify ? "chain intact" : "");
    return zxsh_tag(LPX_B_TRUE, verify ? LPX_P_NEGENTROPIC : LPX_P_NEUTRAL);
}

static void quarantine_cb(void *u, const lpx_qrec_t *rec, const char *cmd)
{
    (void) u;
    char sub[65], line[1100];
    hex(rec->subject, 32, sub);
    snprintf(line, sizeof line, "subject=%s cmd=%s", sub, cmd);
    if (!chain_append("quarantine", rec->tag, line))
        fprintf(stderr, "zxsh: could not write the quarantine file\n");
}

/* ------------------------------------------------------------ H4: zxvd */

static void zxvd_default(char *o, size_t n)
{
    const char *e = getenv("ZXVD_SOCK");
    const char *home = getenv("HOME");
    if (e && *e) {
        snprintf(o, n, "%s", e);
        return;
    }
#if defined(__APPLE__)
    snprintf(o, n, "%s/Library/Application Support/ZXV/run/zxvd.sock", home ? home : "");
#else
    const char *xdg = getenv("XDG_RUNTIME_DIR");
    if (xdg && xdg[0] == '/')
        snprintf(o, n, "%s/zxv/zxvd.sock", xdg);
    else
        snprintf(o, n, "%s/.local/state/zxv/run/zxvd.sock", home ? home : "");
#endif
}

static lpx_t zxv_call(int argc, char *const *argv, char *why, uint32_t wmax)
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: zxv METHOD [PARAMS-JSON]\n");
        return zxsh_tag(LPX_B_FALSE, LPX_P_NEUTRAL);
    }
    char sock[1024];
    zxvd_default(sock, sizeof sock);
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    if (strlen(sock) >= sizeof a.sun_path) {
        snprintf(why, wmax, "socket path too long");
        return zxsh_tag(LPX_B_NEITHER, LPX_P_ENTROPIC);
    }
    memcpy(a.sun_path, sock, strlen(sock) + 1);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *) &a, sizeof a) != 0) {
        if (fd >= 0) close(fd);
        snprintf(why, wmax, "no zxvd at %.90s", sock);
        return zxsh_tag(LPX_B_NEITHER, LPX_P_ENTROPIC);
    }
    char req[8192];
    int n = snprintf(req, sizeof req, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\"%s%s}\n",
                     argv[1], argc == 3 ? ",\"params\":" : "", argc == 3 ? argv[2] : "");
    for (const char *m = argv[1]; *m; m++)
        if (*m == '"' || *m == '\\' || (unsigned char) *m < 0x20) n = -1;
    if (n < 0 || (size_t) n >= sizeof req) {
        close(fd);
        snprintf(why, wmax, "bad method name or params too long");
        return zxsh_tag(LPX_B_FALSE, LPX_P_NEUTRAL);
    }
    out_cb(NULL, fd, req, (size_t) n);
    static char resp[1 << 20];
    size_t got = 0;
    while (got + 1 < sizeof resp) {
        ssize_t r = read(fd, resp + got, sizeof resp - 1 - got);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        got += (size_t) r;
        if (memchr(resp + got - (size_t) r, '\n', (size_t) r)) break;
    }
    close(fd);
    resp[got] = 0;
    if (!got) {
        snprintf(why, wmax, "zxvd gave no answer");
        return zxsh_tag(LPX_B_NEITHER, LPX_P_ENTROPIC);
    }
    fputs(resp, stdout);
    if (resp[got - 1] != '\n') putchar('\n');
    fflush(stdout);
    if (strstr(resp, "\"error\":")) {
        snprintf(why, wmax, "zxvd returned an error");
        return zxsh_tag(LPX_B_FALSE, LPX_P_NEUTRAL);
    }
    const char *p = strstr(resp, "\"verdict\"");
    if (p) {
        p += 9;
        while (*p == ' ' || *p == ':') p++;
        static const char *const W[4] = {"\"false\"", "\"neither\"", "\"both\"", "\"true\""};
        for (uint8_t b = 0; b < 4; b++)
            if (!strncmp(p, W[b], strlen(W[b]))) {
                snprintf(why, wmax, "zxvd reported %s", W[b]);
                return zxsh_tag(b, LPX_P_NEUTRAL);
            }
    }
    return zxsh_tag(LPX_B_TRUE, LPX_P_NEUTRAL);
}

/* ------------------------------------------------------- H6: jobs */

typedef struct {
    int id;
    pid_t pid;
    int vfd; /* the job writes its tag byte here when it ends */
    bool done;
    bool told; /* its end was announced at the prompt: the slot may be reused */
    lpx_t tag;
    char text[200];
} job_t;

#define MAX_JOBS 64
static job_t g_jobs[MAX_JOBS];
static int g_next_job = 1;

static lpx_t job_finish(job_t *j, int status)
{
    unsigned char t;
    ssize_t r = read(j->vfd, &t, 1);
    close(j->vfd);
    j->vfd = -1;
    j->done = true;
    j->tag = r == 1 && t < 64 ? (lpx_t) t : zmux_tag_from_status(status);
    return j->tag;
}

/* Collect jobs that ended; print one line for each when announce. */
static void jobs_reap(bool announce)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        job_t *j = &g_jobs[i];
        int status;
        if (!j->id || j->done || waitpid(j->pid, &status, WNOHANG) != j->pid) continue;
        job_finish(j, status);
        if (announce) {
            fprintf(stderr, "[%d] done  %c  %s\n", j->id, zxsh_letter(j->tag), j->text);
            j->told = true;
        }
    }
}

static lpx_t bg_cb(void *u, zxsh_t *sh, const char *text)
{
    (void) u;
    job_t *j = NULL;
    jobs_reap(false);
    for (int i = 0; i < MAX_JOBS && !j; i++)
        /* a job that ended but was never reported keeps its slot for wait */
        if (!g_jobs[i].id || (g_jobs[i].done && g_jobs[i].told)) j = &g_jobs[i];
    int vp[2];
    if (!j || pipe(vp) != 0) {
        snprintf(sh->why, ZXSH_WHY_MAX, "no room for another background job");
        return zxsh_tag(LPX_B_NEITHER, LPX_P_ENTROPIC);
    }
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        close(vp[0]), close(vp[1]);
        snprintf(sh->why, ZXSH_WHY_MAX, "could not start the job");
        return zxsh_tag(LPX_B_NEITHER, LPX_P_ENTROPIC);
    }
    if (pid == 0) {
        close(vp[0]);
        setpgid(0, 0); /* Ctrl-C at the prompt does not reach it */
        int dn = open("/dev/null", O_RDONLY);
        if (dn >= 0) dup2(dn, 0), close(dn);
        signal(SIGINT, SIG_DFL);
        sh->interactive = false;
        sh->caps &= ~ZXSH_CAP_RESOLVE; /* a person is not watching it */
        zxsh_eval(sh, text);
        unsigned char t = (unsigned char) sh->last;
        if (write(vp[1], &t, 1) != 1) _exit(5);
        fflush(stdout);
        _exit(zxsh_code(sh->last));
    }
    close(vp[1]);
    memset(j, 0, sizeof *j);
    j->id = g_next_job++;
    j->pid = pid;
    j->vfd = vp[0];
    snprintf(j->text, sizeof j->text, "%s", text);
    if (isatty(2)) fprintf(stderr, "[%d] %d\n", j->id, (int) pid);
    return zxsh_tag(LPX_B_TRUE, LPX_P_NEUTRAL);
}

static lpx_t jobs_wait(int argc, char *const *argv, char *why, uint32_t wmax)
{
    lpx_t tags[MAX_JOBS];
    int n = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        job_t *j = &g_jobs[i];
        if (!j->id) continue;
        bool want = argc < 2;
        for (int k = 1; k < argc; k++)
            if (atoi(argv[k] + (argv[k][0] == '%')) == j->id) want = true;
        if (!want) continue;
        if (!j->done) {
            int status = 0;
            while (waitpid(j->pid, &status, 0) < 0 && errno == EINTR) {
            }
            job_finish(j, status);
        }
        tags[n++] = j->tag;
        j->id = 0; /* reported: forget it */
    }
    if (!n) {
        if (argc > 1) {
            snprintf(why, wmax, "no such job: %s", argv[1]);
            return zxsh_tag(LPX_B_FALSE, LPX_P_NEUTRAL);
        }
        return zxsh_tag(LPX_B_TRUE, LPX_P_NEUTRAL);
    }
    lpx_t v = zmux_combine(tags, n);
    if (n > 1 && lpx_base(v) == LPX_B_BOTH) snprintf(why, wmax, "the jobs disagree");
    return v;
}

/* ------------------------------------------------------- host builtins */

static zxsh_t *g_sh;
static char g_self[4096];
static bool g_compat = true;

static char *slurp(const char *path);

static bool on_path(const char *name, char *out, size_t n)
{
    if (strchr(name, '/')) {
        snprintf(out, n, "%s", name);
        return access(name, X_OK) == 0;
    }
    const char *p = getenv("PATH");
    if (!p) p = "/usr/bin:/bin";
    while (*p) {
        const char *e = strchr(p, ':');
        size_t L = e ? (size_t) (e - p) : strlen(p);
        snprintf(out, n, "%.*s/%s", (int) L, L ? p : ".", name);
        struct stat st;
        if (stat(out, &st) == 0 && S_ISREG(st.st_mode) && access(out, X_OK) == 0) return true;
        p += L + (e != NULL);
    }
    return false;
}

static lpx_t T(void)
{
    return zxsh_tag(LPX_B_TRUE, LPX_P_NEUTRAL);
}

static lpx_t F(char *why, uint32_t wmax, const char *msg)
{
    snprintf(why, wmax, "%s", msg);
    fprintf(stderr, "zxsh: %s\n", msg);
    return zxsh_tag(LPX_B_FALSE, LPX_P_NEUTRAL);
}

static bool host_builtin(zxsh_t *sh, const zxsh_stage_t *s, lpx_t *v, char *why, uint32_t wmax)
{
    const char *c = s->argv[0];
    char msg[4200];
    if (!strcmp(c, "cd")) {
        const char *d = s->argc > 1 ? s->argv[1] : getenv("HOME");
        bool dash = d && !strcmp(d, "-");
        if (dash) d = getenv("OLDPWD");
        char old[4096];
        if (!getcwd(old, sizeof old)) old[0] = 0;
        if (!d || chdir(d) != 0) {
            snprintf(msg, sizeof msg, "cd: %s: %s",
                     d      ? d
                     : dash ? "(no previous directory)"
                            : "(no HOME)",
                     d ? strerror(errno) : "unset");
            *v = F(why, wmax, msg);
        } else {
            char now[4096];
            if (old[0]) setenv("OLDPWD", old, 1);
            if (getcwd(now, sizeof now)) {
                setenv("PWD", now, 1);
                if (dash) printf("%s\n", now), fflush(stdout);
            }
            *v = T();
        }
        return true;
    }
    if (!strcmp(c, "pwd")) {
        char b[4096];
        if (getcwd(b, sizeof b)) {
            printf("%s\n", b);
            fflush(stdout);
            *v = T();
        } else {
            *v = zxsh_tag(LPX_B_NEITHER, LPX_P_ENTROPIC);
        }
        return true;
    }
    if (!strcmp(c, "history") || !strcmp(c, "qlog")) {
        bool verify = s->argc > 1 && !strcmp(s->argv[1], "verify");
        *v = chain_show(c[0] == 'h' ? "history" : "quarantine", verify, why, wmax);
        return true;
    }
    if (!strcmp(c, "zxv")) {
        *v = zxv_call(s->argc, s->argv, why, wmax);
        return true;
    }
    if (!strcmp(c, "source") || !strcmp(c, ".")) {
        if (s->argc < 2) {
            *v = F(why, wmax, "usage: source FILE");
            return true;
        }
        char *src = slurp(s->argv[1]);
        if (!src) {
            snprintf(msg, sizeof msg, "%s: %s", s->argv[1], strerror(errno));
            *v = F(why, wmax, msg);
            return true;
        }
        void *frame = malloc(zxsh_frame_size(sh));
        if (!frame) {
            free(src);
            *v = F(why, wmax, "source: out of memory");
            return true;
        }
        zxsh_status_t st = zxsh_eval_nested(sh, src, frame);
        free(frame);
        free(src);
        *v = st == ZXSH_SYNTAX || st == ZXSH_NEED_MORE
                 ? F(why, wmax, "source: the file has a syntax error")
                 : sh->last;
        return true;
    }
    if (!strcmp(c, "type")) {
        *v = T();
        for (int i = 1; i < s->argc; i++) {
            const char *n = s->argv[i], *al = zxsh_alias_get(sh, n);
            int k = zxsh_word_kind(n);
            char path[4096];
            if (al)
                printf("%s is an alias for %s\n", n, al);
            else if (k == 1 || k == 2)
                printf("%s is a shell builtin\n", n);
            else if (k == 3)
                printf("%s is a shell keyword\n", n);
            else if (on_path(n, path, sizeof path))
                printf("%s is %s\n", n, path);
            else {
                fflush(stdout);
                snprintf(msg, sizeof msg, "type: %s: not found", n);
                *v = F(why, wmax, msg);
            }
        }
        fflush(stdout);
        return true;
    }
    if (!strcmp(c, "jobs")) {
        jobs_reap(false);
        for (int i = 0; i < MAX_JOBS; i++) {
            job_t *j = &g_jobs[i];
            if (!j->id) continue;
            if (j->done)
                printf("[%d] done     %c  %s\n", j->id, zxsh_letter(j->tag), j->text);
            else
                printf("[%d] running     %s\n", j->id, j->text);
        }
        fflush(stdout);
        *v = T();
        return true;
    }
    if (!strcmp(c, "wait")) {
        *v = jobs_wait(s->argc, s->argv, why, wmax);
        return true;
    }
    if (!strcmp(c, "split") || !strcmp(c, "tab") || !strcmp(c, "together")) {
        zmux_cfg_t cfg = {g_self, g_home};
        char dir = 'h';
        int a = 1;
        if (c[0] == 's' && s->argc > 1 && (!strcmp(s->argv[1], "-v") || !strcmp(s->argv[1], "-h")))
            dir = s->argv[1][1], a = 2;
        const char *cmds[ZXSH_MAX_ARGS];
        int n = 0;
        char joined[4096] = "";
        if (c[0] == 't' && c[1] == 'o') {
            for (int i = a; i < s->argc; i++) cmds[n++] = s->argv[i];
            if (!n) {
                *v = F(why, wmax, "usage: together 'CMD' 'CMD' ...");
                return true;
            }
        } else if (a < s->argc) {
            for (int i = a; i < s->argc; i++) {
                if (i > a) strncat(joined, " ", sizeof joined - strlen(joined) - 1);
                strncat(joined, s->argv[i], sizeof joined - strlen(joined) - 1);
            }
            cmds[n++] = joined;
        }
        *v = zmux_request(&cfg,
                          c[0] == 's'   ? "split"
                          : c[1] == 'a' ? "tab"
                                        : "together",
                          dir, cmds, n, why, wmax);
        if (why[0] && lpx_base(*v) != LPX_B_TRUE && lpx_base(*v) != LPX_B_BOTH)
            fprintf(stderr, "zxsh: %s\n", why);
        return true;
    }
    return false;
}

/* ----------------------------------------------------- H1/H2 pipelines */

static const char *sig_name(int s)
{
    switch (s) {
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
    case SIGINT:
        return "SIGINT";
    case SIGTERM:
        return "SIGTERM";
    case SIGKILL:
        return "SIGKILL";
    case SIGHUP:
        return "SIGHUP";
    case SIGPIPE:
        return "SIGPIPE";
    default:
        return "a signal";
    }
}

/* H2: parse "base[/polarity]"; false if absent or unreadable. */
static bool parse_verdict(const char *s, uint8_t *base, uint8_t *pol)
{
    char w[64];
    size_t n = 0;
    while (*s == ' ' || *s == '\n') s++;
    while (s[n] && s[n] != '\n' && n + 1 < sizeof w) {
        w[n] = s[n];
        n++;
    }
    w[n] = 0;
    char *slash = strchr(w, '/');
    const char *p = "neutral";
    if (slash) {
        *slash = 0;
        p = slash + 1;
    }
    static const char *const B[4] = {"false", "neither", "both", "true"};
    static const char *const P[4] = {"entropic", "neutral", "forbidden", "negentropic"};
    int b = -1, q = -1;
    for (int i = 0; i < 4; i++) {
        if (!strcmp(w, B[i])) b = i;
        if (!strcmp(p, P[i])) q = i;
    }
    if (b < 0 || q < 0) return false;
    *base = (uint8_t) b;
    *pol = (uint8_t) q;
    return true;
}

static lpx_t stage_tag(int status, bool last, const char *vbuf, const char *name, char *why,
                       uint32_t wmax)
{
    char r[160];
    if (WIFSIGNALED(status)) {
        int s = WTERMSIG(status);
        if (s == SIGSEGV || s == SIGBUS || s == SIGILL || s == SIGFPE || s == SIGABRT ||
            s == SIGSYS || s == SIGTRAP) {
            snprintf(r, sizeof r, "%s crashed with %s", name, sig_name(s));
            why_set(why, wmax, r);
            return zxsh_tag(LPX_B_FALSE, LPX_P_FORBIDDEN);
        }
        if (s == SIGPIPE && !last) return zxsh_tag(LPX_B_TRUE, LPX_P_NEUTRAL);
        snprintf(r, sizeof r, "%s was interrupted by %s", name, sig_name(s));
        why_set(why, wmax, r);
        return zxsh_tag(LPX_B_NEITHER, LPX_P_ENTROPIC);
    }
    uint8_t b, p;
    if (vbuf[0] && parse_verdict(vbuf, &b, &p)) {
        snprintf(r, sizeof r, "%s reported %s", name, vbuf);
        r[strcspn(r, "\n")] = 0;
        if (b != LPX_B_TRUE) why_set(why, wmax, r);
        return zxsh_tag(b, p);
    }
    int code = WEXITSTATUS(status);
    if (code == 0) return zxsh_tag(LPX_B_TRUE, LPX_P_NEUTRAL);
    if (code == 126 || code == 127) {
        snprintf(r, sizeof r, "%s %s", name, code == 127 ? "was not found" : "could not run");
        why_set(why, wmax, r);
        return zxsh_tag(LPX_B_NEITHER, LPX_P_NEUTRAL);
    }
    snprintf(r, sizeof r, "%s exited %d", name, code);
    why_set(why, wmax, r);
    return zxsh_tag(LPX_B_FALSE, LPX_P_NEUTRAL);
}

/* Child side: argv with each flagged word replaced by its matches (the word
 * itself when nothing matches, as POSIX shells do). */
static char **expand_globs(const zxsh_stage_t *s)
{
    bool any = false;
    for (int i = 0; i < s->argc; i++) any |= s->glob[i];
    if (!any) return (char **) s->argv;
    size_t cap = 64, n = 0;
    char **av = malloc(cap * sizeof *av);
    for (int i = 0; av && i < s->argc; i++) {
        glob_t g;
        memset(&g, 0, sizeof g);
        bool hit = s->glob[i] && glob(s->argv[i], 0, NULL, &g) == 0;
        size_t k = hit ? g.gl_pathc : 1;
        if (n + k + 1 > cap) {
            while (n + k + 1 > cap) cap *= 2;
            char **nv = realloc(av, cap * sizeof *av);
            if (!nv) return (char **) s->argv;
            av = nv;
        }
        if (hit)
            for (size_t m = 0; m < k; m++) av[n++] = g.gl_pathv[m]; /* kept until exec */
        else
            av[n++] = s->argv[i];
    }
    if (!av) return (char **) s->argv;
    av[n] = NULL;
    return av;
}

static int open_out(const char *path, bool append)
{
    int fd = open(path, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
    if (fd < 0) {
        fprintf(stderr, "zxsh: %s: %s\n", path, strerror(errno));
        _exit(1);
    }
    return fd;
}

static lpx_t exec_cb(void *u, zxsh_t *sh, const zxsh_stage_t *st, unsigned n, char *cap,
                     uint32_t capmax, uint32_t *caplen, char *why, uint32_t wmax)
{
    (void) u;
    lpx_t v;
    bool plain = n == 1 && !cap && !st[0].in && !st[0].out && !st[0].err && !st[0].err_to_out &&
                 !st[0].out_to_err && !st[0].nenv;
    if (plain && host_builtin(sh, &st[0], &v, why, wmax)) return v;
    fflush(stdout);
    pid_t pids[ZXSH_MAX_STAGES];
    int vfd[ZXSH_MAX_STAGES];
    int prev_rd = -1, cap_rd = -1;
    unsigned started = 0;
    for (unsigned i = 0; i < n; i++) {
        int pp[2] = {-1, -1}, vp[2];
        bool last = i + 1 == n;
        if ((!last || cap) && pipe(pp) != 0) break;
        if (pipe(vp) != 0) {
            if (pp[0] >= 0) close(pp[0]), close(pp[1]);
            break;
        }
        pid_t pid = fork();
        if (pid < 0) {
            close(vp[0]), close(vp[1]);
            if (pp[0] >= 0) close(pp[0]), close(pp[1]);
            break;
        }
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGPIPE, SIG_DFL);
            if (prev_rd >= 0) dup2(prev_rd, 0), close(prev_rd);
            if (pp[1] >= 0) dup2(pp[1], 1), close(pp[1]), close(pp[0]);
            if (cap_rd >= 0) close(cap_rd);
            close(vp[0]);
            if (vp[1] != VERDICT_FD) dup2(vp[1], VERDICT_FD), close(vp[1]);
            setenv("ZXSH_VERDICT_FD", "3", 1);
            for (unsigned k = 0; k < i; k++) close(vfd[k]);
            if (st[i].in) {
                int fd = open(st[i].in, O_RDONLY);
                if (fd < 0) {
                    fprintf(stderr, "zxsh: %s: %s\n", st[i].in, strerror(errno));
                    _exit(1);
                }
                dup2(fd, 0), close(fd);
            }
            /* S10 order: > first, then 2>, then 2>&1, then >&2 */
            if (st[i].out) {
                int fd = open_out(st[i].out, st[i].append);
                dup2(fd, 1), close(fd);
            }
            if (st[i].err) {
                int fd = open_out(st[i].err, st[i].err_append);
                dup2(fd, 2), close(fd);
            }
            if (st[i].err_to_out) dup2(1, 2);
            if (st[i].out_to_err) dup2(2, 1);
            for (int k = 0; k < st[i].nenv; k++) {
                char *eq = strchr(st[i].env[k], '=');
                *eq = 0;
                setenv(st[i].env[k], eq + 1, 1);
            }
            lpx_t bv;
            if (host_builtin(sh, &st[i], &bv, why, wmax) || zxsh_builtin(sh, &st[i], &bv)) {
                char vb[64]; /* the whole verdict, over the H2 channel */
                int vn = snprintf(vb, sizeof vb, "%s/%s\n", lpx_base_name(lpx_base(bv)),
                                  lpx_pol_name(lpx_pol(bv)));
                fflush(stdout);
                if (write(VERDICT_FD, vb, (size_t) vn) != vn) _exit(5);
                _exit(zxsh_code(bv));
            }
            char **av = expand_globs(&st[i]);
            execvp(av[0], av);
            fprintf(stderr, "zxsh: %s: %s\n", av[0],
                    errno == ENOENT ? "command not found" : strerror(errno));
            _exit(errno == ENOENT ? 127 : 126);
        }
        close(vp[1]);
        vfd[i] = vp[0];
        fcntl(vp[0], F_SETFL, O_NONBLOCK);
        if (prev_rd >= 0) close(prev_rd);
        if (pp[1] >= 0) close(pp[1]);
        if (last && cap)
            cap_rd = pp[0];
        else
            prev_rd = pp[0];
        pids[i] = pid;
        started++;
    }
    if (prev_rd >= 0) close(prev_rd);
    if (cap_rd >= 0) {
        uint32_t got = 0;
        char sink[4096];
        for (;;) {
            ssize_t r = got < capmax ? read(cap_rd, cap + got, capmax - got)
                                     : read(cap_rd, sink, sizeof sink);
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) break;
            if (got < capmax) got += (uint32_t) r;
        }
        close(cap_rd);
        *caplen = got;
    }
    v = zxsh_tag(LPX_B_TRUE, LPX_P_NEUTRAL);
    if (started < n) {
        why_set(why, wmax, "could not start the pipeline");
        v = zxsh_tag(LPX_B_NEITHER, LPX_P_ENTROPIC);
    }
    for (unsigned i = 0; i < started; i++) {
        int status = 0;
        while (waitpid(pids[i], &status, 0) < 0 && errno == EINTR) {
        }
        char vb[128] = {0};
        ssize_t r = read(vfd[i], vb, sizeof vb - 1);
        if (r < 0) vb[0] = 0;
        close(vfd[i]);
        v = lpx_and(v, stage_tag(status, i + 1 == n, vb, st[i].argv[0], why, wmax));
        if (i + 1 == n) /* $? as sh reports it */
            sh->status = WIFSIGNALED(status) ? 128 + WTERMSIG(status) : WEXITSTATUS(status);
    }
    return v;
}

/* $(text): a copy of the shell runs text with its stdout into a pipe. */
static void capture_cb(void *u, zxsh_t *sh, const char *text, char *out, uint32_t max,
                       uint32_t *len)
{
    (void) u;
    *len = 0;
    int pp[2];
    if (pipe(pp) != 0) return;
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        close(pp[0]), close(pp[1]);
        return;
    }
    if (pid == 0) {
        close(pp[0]);
        dup2(pp[1], 1), close(pp[1]);
        sh->interactive = false;
        sh->caps &= ~ZXSH_CAP_RESOLVE;
        zxsh_eval(sh, text);
        fflush(stdout);
        _exit(zxsh_code(sh->last));
    }
    close(pp[1]);
    char sink[4096];
    for (;;) {
        ssize_t r =
            *len < max ? read(pp[0], out + *len, max - *len) : read(pp[0], sink, sizeof sink);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        if (*len < max) *len += (uint32_t) r;
    }
    close(pp[0]);
    int status;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
}

static const char *getenv_cb(void *u, const char *name)
{
    (void) u;
    return getenv(name);
}

/* ------------------------------------------------------------------ main */

/* H7: tell a sub-terminal's frame this shell's verdict (zxsh_mux.h). */
static void report_tag(lpx_t t)
{
    if (getenv("ZXSH_MUX") && isatty(1)) {
        printf("\033]7770;%u\007", (unsigned) t);
        fflush(stdout);
    }
}

static void prompt_text(const zxsh_t *sh, char *o, size_t n)
{
    char l = zxsh_letter(sh->last);
    const char *col = l == 'T'   ? "\033[32m"
                      : l == 'F' ? "\033[31m"
                      : l == 'B' ? "\033[38;2;242;194;48m"
                      : l == 'N' ? "\033[90m"
                                 : "\033[1;35m";
    char cwd[4096], held[32] = "";
    const char *home = getenv("HOME");
    if (!getcwd(cwd, sizeof cwd)) snprintf(cwd, sizeof cwd, "?");
    const char *show = cwd;
    char tilde[4096];
    size_t hl = home ? strlen(home) : 0;
    if (hl > 1 && !strncmp(cwd, home, hl) && (cwd[hl] == '/' || !cwd[hl])) {
        snprintf(tilde, sizeof tilde, "~%s", cwd + hl);
        show = tilde;
    }
    unsigned h = zxsh_held_count(sh);
    if (h) snprintf(held, sizeof held, " %u held", h);
    if (g_color)
        snprintf(o, n, "%s[%c]\033[0m \033[1m%s\033[0m%s $ ", col, l, show, held);
    else
        snprintf(o, n, "[%c] %s%s $ ", l, show, held);
}

static int status_code(const zxsh_t *sh, zxsh_status_t st)
{
    if (st == ZXSH_EXIT) return sh->exit_code;
    if (st == ZXSH_SYNTAX) return 2;
    if (st == ZXSH_ABORTED) return 3;
    return zxsh_code(sh->last);
}

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    size_t cap = 4096, n = 0;
    char *b = malloc(cap);
    int c;
    while (b && (c = fgetc(f)) != EOF) {
        if (n + 2 > cap) {
            char *nb = realloc(b, cap *= 2);
            if (!nb) {
                free(b);
                b = NULL;
                break;
            }
            b = nb;
        }
        b[n++] = (char) c;
    }
    fclose(f);
    if (b) b[n] = 0;
    return b;
}

/* H8: a line zxsh cannot read goes to bash (or sh), which says so. Only the
 * name is returned: execvp finds it on PATH again, so no PATH entry is copied
 * into the command line (or from there into a sub-terminal or zxvd request). */
static const char *compat_shell(void)
{
    char path[4096];
    if (!g_compat) return NULL;
    if (on_path("bash", path, sizeof path)) return "bash";
    if (on_path("sh", path, sizeof path)) return "sh";
    return NULL;
}

static zxsh_status_t run_compat(zxsh_t *sh, const char *text, bool is_file, char *const *args,
                                int nargs)
{
    const char *shell = compat_shell();
    if (!shell) {
        fprintf(stderr, "zxsh: %s\n", sh->why);
        return ZXSH_SYNTAX;
    }
    fprintf(stderr, "zxsh: %s handled this (%s)\n", shell, sh->why);
    zxsh_stage_t s;
    memset(&s, 0, sizeof s);
    s.argv[s.argc++] = (char *) shell;
    if (!is_file) s.argv[s.argc++] = (char *) "-c";
    s.argv[s.argc++] = (char *) text;
    for (int i = 0; i < nargs && s.argc < (int) ZXSH_MAX_ARGS - 1; i++) s.argv[s.argc++] = args[i];
    char why[ZXSH_WHY_MAX] = "";
    sh->last = exec_cb(NULL, sh, &s, 1, NULL, 0, NULL, why, sizeof why);
    memcpy(sh->last_why, why, sizeof why);
    return ZXSH_OK;
}

/* Tab completion: commands in the first word, files elsewhere. */
static int add_choice(char **ch, int n, int max, const char *s)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(ch[i], s)) return n;
    if (n < max && (ch[n] = strdup(s))) n++;
    return n;
}

static int complete_cb(void *ctx, const char *line, size_t cur, char **ch, int max, size_t *ws)
{
    zxsh_t *sh = ctx;
    size_t s = cur;
    while (s && !strchr(" \t;|&<>", line[s - 1])) s--;
    *ws = s;
    size_t p = s;
    while (p && (line[p - 1] == ' ' || line[p - 1] == '\t')) p--;
    bool first = !p || strchr(";|&(", line[p - 1]);
    char word[1024];
    size_t wl = cur - s < sizeof word - 1 ? cur - s : sizeof word - 1;
    memcpy(word, line + s, wl);
    word[wl] = 0;
    int n = 0;
    if (first && !strchr(word, '/')) {
        static const char *const B[] = {
            "cd",      "pwd",   "history", "qlog",     "zxv",     "source", "type",     "jobs",
            "wait",    "split", "tab",     "together", "true",    "false",  "both",     "neither",
            "verdict", "exit",  "set",     "unset",    "export",  "held",   "resolve",  "drop",
            "why",     "cap",   "help",    "alias",    "unalias", "break",  "continue", "sandbox",
            "if",      "for",   "while",   "until",    NULL};
        for (int i = 0; B[i]; i++)
            if (!strncmp(B[i], word, wl)) n = add_choice(ch, n, max, B[i]);
        for (unsigned i = 0; i < ZXSH_MAX_ALIAS; i++)
            if (sh->alias[i].name[0] && !strncmp(sh->alias[i].name, word, wl))
                n = add_choice(ch, n, max, sh->alias[i].name);
        const char *path = getenv("PATH");
        char dir[4096];
        while (path && *path && n < max) {
            const char *e = strchr(path, ':');
            size_t L = e ? (size_t) (e - path) : strlen(path);
            snprintf(dir, sizeof dir, "%.*s", (int) L, path);
            DIR *d = opendir(dir);
            struct dirent *de;
            while (d && (de = readdir(d)) && n < max)
                if (de->d_name[0] != '.' && !strncmp(de->d_name, word, wl))
                    n = add_choice(ch, n, max, de->d_name);
            if (d) closedir(d);
            path += L + (e != NULL);
        }
        return n;
    }
    /* files: split the word into directory and name prefix; ~ means HOME */
    char dir[4096], shown[1024];
    const char *slash = strrchr(word, '/');
    size_t dl = slash ? (size_t) (slash - word) + 1 : 0;
    snprintf(shown, sizeof shown, "%.*s", (int) dl, word);
    if (word[0] == '~' && getenv("HOME"))
        snprintf(dir, sizeof dir, "%s%.*s", getenv("HOME"), (int) (dl ? dl - 1 : 0), word + 1);
    else if (dl)
        snprintf(dir, sizeof dir, "%.*s", (int) dl, word);
    else
        snprintf(dir, sizeof dir, ".");
    if (!dir[0]) snprintf(dir, sizeof dir, "/");
    const char *pre = word + dl;
    size_t pl = strlen(pre);
    DIR *d = opendir(dir);
    struct dirent *de;
    while (d && (de = readdir(d)) && n < max) {
        if ((de->d_name[0] == '.' && pre[0] != '.') || strncmp(de->d_name, pre, pl)) continue;
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        char full[8192], c[2200];
        struct stat st;
        snprintf(full, sizeof full, "%s/%s", dir, de->d_name);
        bool isdir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        snprintf(c, sizeof c, "%s%s%s", shown, de->d_name, isdir ? "/" : "");
        n = add_choice(ch, n, max, c);
    }
    if (d) closedir(d);
    return n;
}

static void on_sigint(int s)
{
    (void) s;
    if (g_sh) g_sh->interrupt = 1;
}

/* Load the text column of the history file into the line editor. */
static void history_load(void)
{
    char path[1200], line[4096];
    chain_path(path, sizeof path, "history");
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *t = line;
        for (int k = 0; k < 4 && t; k++) {
            t = strchr(t, '\t');
            if (t) t++;
        }
        if (!t) continue;
        t[strcspn(t, "\n")] = 0;
        zline_history_add(t);
    }
    fclose(f);
}

static void find_self(const char *argv0)
{
    char tmp[4096];
    if (strchr(argv0, '/')) {
        if (!realpath(argv0, g_self)) snprintf(g_self, sizeof g_self, "%s", argv0);
    } else if (on_path(argv0, tmp, sizeof tmp)) {
        if (!realpath(tmp, g_self)) snprintf(g_self, sizeof g_self, "%s", tmp);
    } else {
        snprintf(g_self, sizeof g_self, "%s", argv0);
    }
}

int main(int argc, char **argv)
{
    static zxsh_t sh;
    static const zxsh_ops_t ops = {out_cb,    exec_cb,    setenv_cb, quarantine_cb,
                                   getenv_cb, capture_cb, bg_cb};
    const char *zh = getenv("ZXSH_HOME"), *home = getenv("HOME");
    if (zh && *zh)
        snprintf(g_home, sizeof g_home, "%s", zh);
    else
        snprintf(g_home, sizeof g_home, "%s/.zxsh", home ? home : ".");
    mkdir(g_home, 0700);
    find_self(argv[0]);
    const char *cm = getenv("ZXSH_COMPAT");
    g_compat = !(cm && !strcmp(cm, "0"));

    const char *cmd = NULL, *script = NULL;
    int argi = 1;
    if (argc > 2 && !strcmp(argv[1], "-c"))
        cmd = argv[2], argi = 3;
    else if (argc > 1 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        printf("usage: zxsh [-c COMMAND | SCRIPT [ARGS...]]\n"
               "Type `help` inside zxsh for the command list.\n");
        return 0;
    } else if (argc > 1)
        script = argv[1], argi = 2;

    bool inter = !cmd && !script && isatty(0);
    g_color = inter && isatty(1) && !getenv("NO_COLOR");
    zxsh_init(&sh, &ops, NULL, inter);
    g_sh = &sh;
    sh.quiet_syntax = g_compat;
    zxsh_set(&sh, "arg0", script ? script : argv[0]);
    char args[ZXSH_VALUE_MAX] = "", cnt[16];
    int k = 0;
    for (int i = argi; i < argc; i++, k++) {
        char nm[16];
        snprintf(nm, sizeof nm, "arg%d", k + 1);
        if (k < 9) zxsh_set(&sh, nm, argv[i]);
        size_t L = strlen(args);
        snprintf(args + L, sizeof args - L, "%s%s", k ? "\x1f" : "", argv[i]);
    }
    snprintf(cnt, sizeof cnt, "%d", k);
    zxsh_set(&sh, "argc", cnt);
    zxsh_set(&sh, "args", args);
    if (!inter) signal(SIGPIPE, SIG_DFL);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);
    if (inter) signal(SIGQUIT, SIG_IGN);

    if (cmd || script) {
        char *src = script ? slurp(script) : NULL;
        if (script && !src) {
            fprintf(stderr, "zxsh: %s: %s\n", script, strerror(errno));
            return 127;
        }
        zxsh_status_t st = zxsh_eval(&sh, cmd ? cmd : src);
        if (st == ZXSH_NEED_MORE) {
            snprintf(sh.why, sizeof sh.why, "syntax: unexpected end of input");
            st = ZXSH_SYNTAX;
        }
        bool compat = false;
        if (st == ZXSH_SYNTAX) {
            if (!strstr(cmd ? cmd : src, "<?>")) {
                st = run_compat(&sh, cmd ? cmd : script, script != NULL, argv + argi, argc - argi);
                compat = st == ZXSH_OK;
            } else {
                fprintf(stderr, "zxsh: %s\n", sh.why);
            }
        }
        if (zxsh_held_count(&sh))
            fprintf(stderr, "zxsh: %u item(s) still held; run them interactively to resolve\n",
                    zxsh_held_count(&sh));
        free(src);
        report_tag(sh.last);
        return compat ? sh.status : status_code(&sh, st); /* bash's own exit status */
    }

    /* interactive or piped stdin */
    if (inter) history_load();
    static char buf[65536];
    size_t used = 0;
    zxsh_status_t st = ZXSH_OK;
    for (;;) {
        char line[8192], pr[4400];
        if (inter) {
            jobs_reap(true);
            if (used)
                snprintf(pr, sizeof pr, "> ");
            else
                prompt_text(&sh, pr, sizeof pr);
            int r = zline_read(pr, line, sizeof line - 1, complete_cb, &sh);
            if (r == ZLINE_EOF) break;
            if (r == ZLINE_INTR) {
                used = 0;
                continue;
            }
            size_t L = (size_t) r;
            line[L++] = '\n';
            line[L] = 0;
        } else if (!fgets(line, sizeof line, stdin)) {
            break;
        }
        size_t L = strlen(line);
        if (used + L + 1 > sizeof buf) {
            fprintf(stderr, "zxsh: input too long\n");
            used = 0;
            continue;
        }
        memcpy(buf + used, line, L + 1);
        used += L;
        st = zxsh_eval(&sh, buf);
        if (st == ZXSH_NEED_MORE) continue;
        if (st == ZXSH_SYNTAX) {
            if (!strstr(buf, "<?>"))
                st = run_compat(&sh, buf, false, NULL, 0);
            else
                fprintf(stderr, "zxsh: %s\n", sh.why);
        }
        if (inter && buf[strspn(buf, " \t\n")]) {
            if (buf[used - 1] == '\n') buf[used - 1] = 0;
            chain_append("history", sh.last, buf);
            zline_history_add(buf);
            report_tag(sh.last);
        }
        used = 0;
        if (st == ZXSH_EXIT) break;
        if (st == ZXSH_ABORTED && !inter) break;
    }
    if (st == ZXSH_NEED_MORE) {
        fprintf(stderr, "zxsh: syntax: unexpected end of input\n");
        st = ZXSH_SYNTAX;
    }
    return status_code(&sh, st);
}
