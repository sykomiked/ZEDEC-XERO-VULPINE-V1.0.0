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
 *      kernel: a program that runs keeps the user's OS permissions. No job
 *      control, no line editing beyond the terminal's own; Windows needs a
 *      CreateProcess executor (not here yet).
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
#include <unistd.h>
#include "zxsh_core.h"
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
        if (*p == '\n') esc[k++] = '\\', esc[k++] = 'n';
        else if (*p == '\t') esc[k++] = '\\', esc[k++] = 't';
        else if (*p == '\\') esc[k++] = '\\', esc[k++] = '\\';
        else esc[k++] = *p;
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
    if (xdg && xdg[0] == '/') snprintf(o, n, "%s/zxv/zxvd.sock", xdg);
    else snprintf(o, n, "%s/.local/state/zxv/run/zxvd.sock", home ? home : "");
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
    int n = snprintf(req, sizeof req, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\"%s%s}\n", argv[1],
                     argc == 3 ? ",\"params\":" : "", argc == 3 ? argv[2] : "");
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

/* ------------------------------------------------------- host builtins */

static bool host_builtin(const zxsh_stage_t *s, lpx_t *v, char *why, uint32_t wmax)
{
    const char *c = s->argv[0];
    if (!strcmp(c, "cd")) {
        const char *d = s->argc > 1 ? s->argv[1] : getenv("HOME");
        if (!d || chdir(d) != 0) {
            snprintf(why, wmax, "cd: %s: %s", d ? d : "(no HOME)", strerror(errno));
            fprintf(stderr, "zxsh: %s\n", why);
            *v = zxsh_tag(LPX_B_FALSE, LPX_P_NEUTRAL);
        } else {
            *v = zxsh_tag(LPX_B_TRUE, LPX_P_NEUTRAL);
        }
        return true;
    }
    if (!strcmp(c, "pwd")) {
        char b[4096];
        if (getcwd(b, sizeof b)) {
            printf("%s\n", b);
            fflush(stdout);
            *v = zxsh_tag(LPX_B_TRUE, LPX_P_NEUTRAL);
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
    return false;
}

/* ----------------------------------------------------- H1/H2 pipelines */

static const char *sig_name(int s)
{
    switch (s) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGABRT: return "SIGABRT";
    case SIGSYS: return "SIGSYS";
    case SIGTRAP: return "SIGTRAP";
    case SIGINT: return "SIGINT";
    case SIGTERM: return "SIGTERM";
    case SIGKILL: return "SIGKILL";
    case SIGHUP: return "SIGHUP";
    case SIGPIPE: return "SIGPIPE";
    default: return "a signal";
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
        if (s == SIGSEGV || s == SIGBUS || s == SIGILL || s == SIGFPE || s == SIGABRT || s == SIGSYS ||
            s == SIGTRAP) {
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
    if (!s->glob) return (char **) s->argv;
    size_t cap = 64, n = 0;
    char **av = malloc(cap * sizeof *av);
    for (int i = 0; av && i < s->argc; i++) {
        glob_t g;
        memset(&g, 0, sizeof g);
        bool hit = (s->glob & (1u << i)) && glob(s->argv[i], 0, NULL, &g) == 0;
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

static lpx_t exec_cb(void *u, zxsh_t *sh, const zxsh_stage_t *st, unsigned n, char *cap,
                     uint32_t capmax, uint32_t *caplen, char *why, uint32_t wmax)
{
    (void) u;
    (void) sh;
    lpx_t v;
    if (n == 1 && !cap && !st[0].in && !st[0].out && host_builtin(&st[0], &v, why, wmax)) return v;
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
            if (st[i].out) {
                int fd = open(st[i].out, O_WRONLY | O_CREAT | (st[i].append ? O_APPEND : O_TRUNC), 0644);
                if (fd < 0) {
                    fprintf(stderr, "zxsh: %s: %s\n", st[i].out, strerror(errno));
                    _exit(1);
                }
                dup2(fd, 1), close(fd);
            }
            char **av = expand_globs(&st[i]);
            execvp(av[0], av);
            fprintf(stderr, "zxsh: %s: %s\n", av[0], strerror(errno));
            _exit(errno == ENOENT ? 127 : 126);
        }
        close(vp[1]);
        vfd[i] = vp[0];
        fcntl(vp[0], F_SETFL, O_NONBLOCK);
        if (prev_rd >= 0) close(prev_rd);
        if (pp[1] >= 0) close(pp[1]);
        if (last && cap) cap_rd = pp[0];
        else prev_rd = pp[0];
        pids[i] = pid;
        started++;
    }
    if (prev_rd >= 0) close(prev_rd);
    if (cap_rd >= 0) {
        uint32_t got = 0;
        char sink[4096];
        for (;;) {
            ssize_t r = got < capmax ? read(cap_rd, cap + got, capmax - got) : read(cap_rd, sink, sizeof sink);
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
    }
    return v;
}

/* ------------------------------------------------------------------ main */

static void prompt(const zxsh_t *sh)
{
    char l = zxsh_letter(sh->last);
    const char *col = l == 'T' ? "\033[32m" : l == 'F' ? "\033[31m" : l == 'B' ? "\033[33m" : l == 'N' ? "\033[90m" : "\033[1;35m";
    unsigned h = zxsh_held_count(sh);
    if (g_color) fprintf(stderr, "zxsh %s[%c]\033[0m", col, l);
    else fprintf(stderr, "zxsh [%c]", l);
    if (h) fprintf(stderr, " %u held", h);
    fprintf(stderr, " $ ");
}

static int status_code(const zxsh_t *sh, zxsh_status_t st)
{
    if (st == ZXSH_EXIT) return sh->exit_code;
    if (st == ZXSH_SYNTAX) return 2;
    if (st == ZXSH_ABORTED) return 3;
    switch (lpx_base(sh->last)) {
    case LPX_B_TRUE: return 0;
    case LPX_B_FALSE: return 1;
    case LPX_B_BOTH: return 4;
    default: return 5;
    }
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

int main(int argc, char **argv)
{
    static zxsh_t sh;
    static const zxsh_ops_t ops = {out_cb, exec_cb, setenv_cb, quarantine_cb};
    const char *zh = getenv("ZXSH_HOME"), *home = getenv("HOME");
    if (zh && *zh) snprintf(g_home, sizeof g_home, "%s", zh);
    else snprintf(g_home, sizeof g_home, "%s/.zxsh", home ? home : ".");
    mkdir(g_home, 0700);

    const char *cmd = NULL, *script = NULL;
    int argi = 1;
    if (argc > 2 && !strcmp(argv[1], "-c")) cmd = argv[2], argi = 3;
    else if (argc > 1 && !strcmp(argv[1], "--help")) {
        printf("usage: zxsh [-c COMMAND | SCRIPT [ARGS...]]\n");
        return 0;
    } else if (argc > 1) script = argv[1], argi = 2;

    bool inter = !cmd && !script && isatty(0);
    g_color = inter && isatty(2) && !getenv("NO_COLOR");
    zxsh_init(&sh, &ops, NULL, inter);
    if (home) zxsh_set(&sh, "HOME", home);
    for (int i = argi, k = 1; i < argc && k < 10; i++, k++) {
        char nm[8];
        snprintf(nm, sizeof nm, "arg%d", k);
        zxsh_set(&sh, nm, argv[i]);
    }
    if (!inter) signal(SIGPIPE, SIG_DFL);
    else signal(SIGINT, SIG_IGN), signal(SIGQUIT, SIG_IGN);

    if (cmd || script) {
        char *src = script ? slurp(script) : NULL;
        if (script && !src) {
            fprintf(stderr, "zxsh: %s: %s\n", script, strerror(errno));
            return 127;
        }
        zxsh_status_t st = zxsh_eval(&sh, cmd ? cmd : src);
        if (st == ZXSH_NEED_MORE) {
            fprintf(stderr, "zxsh: syntax: unexpected end of input\n");
            st = ZXSH_SYNTAX;
        }
        if (zxsh_held_count(&sh))
            fprintf(stderr, "zxsh: %u item(s) still held; run them interactively to resolve\n",
                    zxsh_held_count(&sh));
        free(src);
        return status_code(&sh, st);
    }

    /* interactive or piped stdin */
    static char buf[65536];
    size_t used = 0;
    zxsh_status_t st = ZXSH_OK;
    for (;;) {
        if (inter) {
            if (used) fprintf(stderr, "> ");
            else prompt(&sh);
        }
        char line[8192];
        if (!fgets(line, sizeof line, stdin)) break;
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
        if (inter && buf[strspn(buf, " \t\n")]) {
            if (buf[used - 1] == '\n') buf[used - 1] = 0;
            chain_append("history", sh.last, buf);
        }
        used = 0;
        if (st == ZXSH_EXIT) break;
        if (st == ZXSH_ABORTED && !inter) break;
    }
    if (st == ZXSH_NEED_MORE) {
        fprintf(stderr, "zxsh: syntax: unexpected end of input\n");
        st = ZXSH_SYNTAX;
    }
    if (inter) fputc('\n', stderr);
    return status_code(&sh, st);
}
