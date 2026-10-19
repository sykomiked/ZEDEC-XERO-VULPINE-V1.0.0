/* zxvd_app.c — the live app testbed: load and run a .zxapp on a running
 * zxvd with no recompile. The interpreted pillars (Eve, KRON, MYRA,
 * Sutra) register against the manifest host; a POST /api/app carries the
 * manifest text, zxapp_load compiles every logic entry with its pillar,
 * and the app's self-test entry runs — the verdict is the reply. This is
 * the develop-then-integrate loop: apps draft, transfer and test here,
 * and only a complete, debugged set ever combines into the OS image.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#define _POSIX_C_SOURCE 200809L /* usleep, kill, waitpid on strict C11 */
#include <setjmp.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zxapp.h"
#include "zxapp_eve.h"
#include "zxapp_kron.h"
#include "zxapp_myra.h"
#include "zxapp_sutra.h"

#define ZXVD_APP_MAX (16u * 1024u) /* one manifest, bounded */

static zxapp_host_t GH;
static zxapp_t GA;

/* [status][len u16 BE][text] */
/* The drafts are untrusted: the model's source can fault the pillars'
 * compilers. A fault around parse/load/check must not kill the daemon —
 * it becomes a verdict ("the load faulted"), and the next draft runs. */
static jmp_buf GAULT;
static volatile sig_atomic_t IN_LOAD;

static void fault_handler(int sig)
{
    (void) sig;
    if (IN_LOAD) longjmp(GAULT, 1);
    _exit(70); /* a fault outside the guarded region is a real bug */
}

static void put_reply(char **out, size_t *out_len, uint8_t st, const char *txt)
{
    IN_LOAD = 0; /* every reply leaves the guarded region */
    size_t n = strlen(txt);
    char *p;
    if (n > 1400) n = 1400;
    p = (char *) malloc(n + 3);
    if (!p) {
        *out = NULL;
        *out_len = 0;
        return;
    }
    p[0] = (char) st;
    p[1] = (char) (n >> 8);
    p[2] = (char) n;
    memcpy(p + 3, txt, n);
    *out = p;
    *out_len = n + 3;
}

/* The repo reader: a logic path like apps/blocks/fee_check/fee.kron
 * resolves under kernel/src/apps/ (the real apps' shape). The testbed
 * judges drafts that reference the same blocks the built apps use. */
static int32_t repo_read(void *ctx, const char *path, char *buf, uint32_t cap)
{
    char full[512];
    FILE *f;
    (void) ctx;
    snprintf(full, sizeof full, "kernel/src/apps/%s", path);
    f = fopen(full, "rb");
    if (!f) {
        snprintf(full, sizeof full, "kernel/src/%s", path);
        f = fopen(full, "rb");
        if (!f) return -1;
    }
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return (int32_t) n;
}

static int app_judge_impl(const char *body, size_t len, char **out, size_t *out_len);

/* The sandbox: every draft is judged in a CHILD process. The model's
 * source can corrupt memory or loop forever; the corruption dies with
 * the child, and a hung child is killed. The daemon never dies of a
 * draft. The verdict travels back over a pipe. */
int zxvd_app_handle(const char *body, size_t len, char **out, size_t *out_len)
{
    int pfd[2];
    pid_t pid;
    if (pipe(pfd) != 0) return -1;
    pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return -1;
    }
    if (pid == 0) {
        char *o = 0;
        size_t ol = 0;
        close(pfd[0]);
        app_judge_impl(body, len, &o, &ol);
        if (o && ol) {
            ssize_t w = write(pfd[1], o, ol);
            (void) w;
            free(o);
        }
        close(pfd[1]);
        _exit(0);
    }
    close(pfd[1]);
    {
        char buf[4096];
        size_t got = 0;
        int status = 0, timed_out = 0;
        for (int i = 0; i < 100; i++) { /* up to 10 s in 100 ms steps */
            ssize_t r = read(pfd[0], buf + got, sizeof buf - got);
            if (r > 0) {
                got += (size_t) r;
                continue;
            }
            if (r == 0) break;
            if (waitpid(pid, &status, WNOHANG) == pid) break;
            { struct timespec ts = {0, 100000000}; nanosleep(&ts, 0); }
            if (i == 99) timed_out = 1;
        }
        if (!timed_out && waitpid(pid, &status, WNOHANG) != pid) {
            /* the child still runs: give it a moment, then kill */
            for (int i = 0; i < 20; i++) {
                ssize_t r = read(pfd[0], buf + got, sizeof buf - got);
                if (r > 0) got += (size_t) r;
                if (waitpid(pid, &status, WNOHANG) == pid) break;
                { struct timespec ts = {0, 100000000}; nanosleep(&ts, 0); }
                if (i == 19) timed_out = 1;
            }
        }
        close(pfd[0]);
        if (timed_out || !WIFEXITED(status) || WEXITSTATUS(status) != 0 || got < 3) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            put_reply(out, out_len, 5, "the draft faulted or hung the runtime");
            return 0;
        }
        *out = malloc(got);
        if (!*out) return -1;
        memcpy(*out, buf, got);
        *out_len = got;
        return 0;
    }
}

int zxvd_app_init(void)
{
    zxapp_host_init(&GH);
    GH.read_file = repo_read;
    GH.read_ctx = 0;
    if (zxapp_eve_register(&GH) != 0) return -1;
    if (zxapp_kron_register(&GH) != 0) return -1;
    if (zxapp_myra_register(&GH) != 0) return -1;
    if (zxapp_sutra_register(&GH) != 0) return -1;
    return 0;
}

/* Handle POST /api/app: body = the .zxapp manifest text. Returns a reply
 * buffer (malloc'd; the caller frees it) or NULL on OOM. The reply is
 * [status][len u16 BE][text]: 0 = loaded and every check line passed; 1 =
 * parse error; 2 = load error; 3 = a check failed (err/err_line in the
 * text); 4 = no check lines. */
static int app_judge_impl(const char *body, size_t len, char **out, size_t *out_len)
{
    void (*old_seg)(int) = signal(SIGSEGV, fault_handler);
    void (*old_fpe)(int) = signal(SIGFPE, fault_handler);
    void (*old_abrt)(int) = signal(SIGABRT, fault_handler);
    void (*old_alrm)(int) = signal(SIGALRM, fault_handler);
    alarm(5); /* a draft may loop forever: the load gets 5 seconds */
#ifdef SIGBUS
    void (*old_bus)(int) = signal(SIGBUS, fault_handler);
#endif
    if (setjmp(GAULT)) {
        IN_LOAD = 0;
        alarm(0);
        signal(SIGSEGV, old_seg);
        signal(SIGFPE, old_fpe);
        signal(SIGABRT, old_abrt);
        signal(SIGALRM, old_alrm);
#ifdef SIGBUS
        signal(SIGBUS, old_bus);
#endif
        put_reply(out, out_len, 5, "the draft faulted or hung the runtime");
        return 0;
    }
    IN_LOAD = 1;

    char why[256];
    int st;
    if (!body || len > ZXVD_APP_MAX) {
        put_reply(out, out_len, 1, "manifest too large");
        return 0;
    }
    zxapp_unload(&GA, &GH);
    memset(&GA, 0, sizeof GA);
    st = zxapp_parse(&GA, body, (uint32_t) len);
    if (st != ZXAPP_OK) {
        snprintf(why, sizeof why, "parse failed (line %d): %s", GA.err_line, GA.err);
        put_reply(out, out_len, 1, why);
        return 0;
    }
    st = zxapp_load(&GA, &GH);
    if (st < 0) {
        snprintf(why, sizeof why, "load failed: %s", GA.err);
        put_reply(out, out_len, 2, why);
        return 0;
    }
    if (st == 0) {
        put_reply(out, out_len, 2, "nothing loaded (no runtime for any entry)");
        return 0;
    }
    /* the app's proof: every check line must pass (K2) */
    st = zxapp_check(&GA, &GH);
    if (st == ZXAPP_E_CHECK || st == ZXAPP_E_NAME || st == ZXAPP_E_CONTRACT) {
        snprintf(why, sizeof why, "check failed (line %d): %s", GA.err_line, GA.err);
        put_reply(out, out_len, 3, why);
        return 0;
    }
    if (st == 0) {
        put_reply(out, out_len, 4, "no check lines");
        return 0;
    }
    snprintf(why, sizeof why, "loaded, %d checks passed", st);
    put_reply(out, out_len, 0, why);
    return 0;
}

void zxvd_app_free(char *p)
{
    free(p);
}