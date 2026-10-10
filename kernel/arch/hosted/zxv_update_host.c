/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_update_host.c — see zxv_update_host.h (C1-C4). */
#if !defined(_WIN32)
#    define _DEFAULT_SOURCE
#    define _DARWIN_C_SOURCE
#    include <sys/types.h>
#    include <sys/wait.h>
#    include <fcntl.h>
#    include <signal.h>
#    include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_update_host.h"
#include "zx_upcheck.h"

#define STORE_BYTES (16u << 20)
#define BS_ENTRIES  1024u /* power of two */
#define CURL_SECS   "20"

static struct {
    bool ready; /* init ran */
    bool alloc; /* the work buffers exist (first check) */
    zxn_bus_t *bus;
    char gateway[128];
    uint32_t installed;
    uint64_t next_auto;
    zxv_update_status_t st;
    /* the checker and its node (allocated at the first check) */
    zxu_config_t cfg;
    zxu_t u;
    zxu_work_t *work;
    ipfsn_walk_t walk;
    ipfsn_memstore_t ms;
    ipfsn_storage_ops_t ops;
    ipfsn_bs_t bs;
    ipfsn_bs_entry_t *tab;
    ipfsn_pin_t pins[4];
    ipfsn_node_t node;
    uint8_t *scratch, *net, *leaf, *blk;
    zxu_result_t *res;
} U;

/* ---- C2: the gateway GET ---- */

static bool url_safe(const char *s)
{
    if (!s || !*s) return false;
    for (; *s; s++)
        if (!strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789:/._?=&%-~", *s))
            return false;
    return true;
}

static bool accept_safe(const char *s)
{
    if (!s || !*s || strlen(s) > 64) return false;
    for (; *s; s++)
        if (!strchr("abcdefghijklmnopqrstuvwxyz0123456789./+-", *s)) return false;
    return true;
}

static int https_get(void *ctx, const char *url, const char *accept, uint8_t *buf, uint32_t cap,
                     uint32_t *len)
{
    (void) ctx;
    *len = 0;
    U.st.requests++;
    if (!url_safe(url) || strncmp(url, U.gateway, strlen(U.gateway)) != 0 || !accept_safe(accept))
        return -1;
#if defined(_WIN32)
    (void) buf;
    (void) cap;
    snprintf(U.st.error, sizeof U.st.error, "update checks are not wired on Windows yet");
    return -1;
#else
    char hdr[96], maxsz[24];
    snprintf(hdr, sizeof hdr, "Accept: %s", accept);
    snprintf(maxsz, sizeof maxsz, "%u", (unsigned) cap);
    const char *argv[] = {"curl",
                          "-sS",
                          "--fail",
                          "--max-time",
                          CURL_SECS,
                          "--proto",
                          "=https,http",
                          "--noproxy",
                          "127.0.0.1,localhost",
                          "--max-filesize",
                          maxsz,
                          "-H",
                          hdr,
                          "--",
                          url,
                          NULL};
    int p[2];
    if (pipe(p) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(p[0]);
        close(p[1]);
        return -1;
    }
    if (pid == 0) {
        dup2(p[1], 1);
        int dn = open("/dev/null", O_RDWR);
        if (dn >= 0) {
            dup2(dn, 0);
            dup2(dn, 2);
        }
        for (int fd = 3; fd < 1024; fd++) close(fd); /* not the app's sockets */
        execvp("curl", (char *const *) argv);
        _exit(127);
    }
    close(p[1]);
    uint32_t got = 0;
    bool over = false;
    for (;;) {
        uint8_t tmp[16384];
        ssize_t k = read(p[0], tmp, sizeof tmp);
        if (k < 0) continue;
        if (k == 0) break;
        if (got + (uint32_t) k > cap) {
            over = true;
            kill(pid, SIGKILL);
            break;
        }
        memcpy(buf + got, tmp, (size_t) k);
        got += (uint32_t) k;
    }
    close(p[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
    }
    if (over || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        snprintf(U.st.error, sizeof U.st.error, "the gateway request failed (curl %d)",
                 WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return -1;
    }
    *len = got;
    return 0;
#endif
}

/* ---- notes ---- */

static void post(int kind_hint, const char *title, const char *body, zxn_pri_t pri)
{
    if (!U.bus) return;
    zxn_spec_t s;
    memset(&s, 0, sizeof s);
    s.kind = ZXN_UPDATE;
    s.pri = pri;
    s.source = "updates";
    s.title = title;
    s.body = body;
    s.action = "update";
    s.key = (uint64_t) (kind_hint + 1);
    zxn_post(U.bus, &s);
}

static void on_notify(void *ctx, int kind, const char *title, const char *body)
{
    (void) ctx;
    post(kind, title, body, kind == ZXU_NOTIFY_UPDATE ? ZXN_PRI_HIGH : ZXN_PRI_NORMAL);
}

static uint8_t this_arch(void)
{
#if defined(__x86_64__) || defined(_M_X64)
    return ZXU_ARCH_X86_64;
#elif defined(__aarch64__) || defined(_M_ARM64)
    return ZXU_ARCH_AARCH64;
#else
    return ZXU_ARCH_ANY;
#endif
}

int zxv_update_init(const char *gateway, zxn_bus_t *bus, uint32_t installed)
{
    zxv_update_free();
    memset(&U.st, 0, sizeof U.st);
    const char *g = gateway ? gateway : ZXV_UPDATE_GATEWAY;
    size_t gl = strlen(g);
    bool ok = gl > 8 && gl < sizeof U.gateway && g[gl - 1] != '/' && url_safe(g) &&
              (!strncmp(g, "https://", 8) || !strncmp(g, "http://127.0.0.1", 16) ||
               !strncmp(g, "http://localhost", 16));
    snprintf(U.st.status, sizeof U.st.status, "not checked");
    if (!ok) {
        snprintf(U.st.error, sizeof U.st.error, "refused gateway (https, or http to 127.0.0.1)");
        return -1;
    }
    snprintf(U.gateway, sizeof U.gateway, "%s", g);
    snprintf(U.st.gateway, sizeof U.st.gateway, "%s", g);
    U.bus = bus;
    U.installed = installed;
    U.next_auto = 0;
    if (zxu_config_init(&U.cfg, false, this_arch(), installed, NULL) != ZXU_OK) return -1;
    U.cfg.interval_s = 0; /* only when asked: the host schedules automatic checks itself */
    U.ready = true;
    return 0;
}

static int alloc_work(void)
{
    if (U.alloc) return 0;
    U.ms.cap = STORE_BYTES;
    U.ms.buf = (uint8_t *) malloc(U.ms.cap);
    U.tab = (ipfsn_bs_entry_t *) calloc(BS_ENTRIES, sizeof *U.tab);
    U.scratch = (uint8_t *) malloc(IPFSN_SCRATCH_MIN);
    U.net = (uint8_t *) malloc(IPFSN_BLOCK_MAX + 64);
    U.leaf = (uint8_t *) malloc(IPFSN_BLOCK_MAX);
    U.blk = (uint8_t *) malloc(IPFSN_BLOCK_MAX * 2u);
    U.work = (zxu_work_t *) calloc(1, sizeof *U.work);
    U.res = (zxu_result_t *) calloc(ZXU_MAX_BUCKETS, sizeof *U.res);
    if (!U.ms.buf || !U.tab || !U.scratch || !U.net || !U.leaf || !U.blk || !U.work || !U.res) {
        U.alloc = true; /* so free releases what was allocated */
        zxv_update_free();
        return -1;
    }
    ipfsn_memstore_ops(&U.ms, &U.ops);
    ipfsn_bs_init(&U.bs, &U.ops, U.tab, BS_ENTRIES, U.scratch, IPFSN_SCRATCH_MIN);
    ipfsn_node_init(&U.node, &U.bs, U.pins, 4, U.net, IPFSN_BLOCK_MAX + 64);
    U.node.gw.ctx = NULL;
    U.node.gw.https_get = https_get;
    U.node.gw.base = U.gateway;
    U.walk.leaf = U.leaf;
    U.walk.leaf_cap = IPFSN_BLOCK_MAX;
    U.work->blk = U.blk;
    U.work->blk_cap = IPFSN_BLOCK_MAX * 2u;
    U.work->walk = &U.walk;
    zxu_init(&U.u, &U.cfg, &U.node, U.work, zxu_mldsa65_verify, NULL, on_notify, NULL);
    U.alloc = true;
    return 0;
}

int zxv_update_check_now(uint64_t now_unix)
{
    if (!U.ready) return -1;
    U.st.error[0] = 0;
    if (alloc_work() != 0) {
        snprintf(U.st.error, sizeof U.st.error, "out of memory for the update check");
        return -1;
    }
    U.cfg.enabled = true; /* C1: on only while the check runs */
    uint32_t n = zxu_check_all(&U.u, now_unix, true, U.res, ZXU_MAX_BUCKETS);
    U.cfg.enabled = false;
    U.st.checks++;
    U.st.checked = true;
    U.st.last_check = now_unix;
    U.st.installable = false;
    int builtin = -1;
    for (uint32_t i = 0; i < n; i++) {
        if (zxu_installable(&U.res[i])) U.st.installable = true;
        if (builtin < 0 && U.res[i].bucket < ZXU_MAX_BUCKETS && U.cfg.b[U.res[i].bucket].builtin)
            builtin = (int) i;
    }
    if (builtin < 0 && n) builtin = 0;
    if (builtin >= 0) {
        const zxu_result_t *r = &U.res[builtin];
        snprintf(U.st.status, sizeof U.st.status, "%s", zxu_status_str(r->status));
        snprintf(U.st.title, sizeof U.st.title, "%s", r->title);
        snprintf(U.st.body, sizeof U.st.body, "%s", r->body);
        char line[160];
        snprintf(line, sizeof line, "Update check: %s", U.st.status);
        post(100, line, r->body[0] ? r->body : U.st.error, ZXN_PRI_NORMAL);
        return (int) r->status;
    }
    snprintf(U.st.status, sizeof U.st.status, "no buckets");
    return -1;
}

void zxv_update_tick(uint64_t now_unix, bool online)
{
    U.st.auto_on = online && U.ready;
    if (!U.st.auto_on || now_unix < U.next_auto) return;
    U.next_auto = now_unix + ZXV_UPDATE_AUTO_SECONDS;
    (void) zxv_update_check_now(now_unix);
}

void zxv_update_status(zxv_update_status_t *st)
{
    *st = U.st;
}

void zxv_update_free(void)
{
    if (!U.alloc) return;
    free(U.ms.buf);
    free(U.tab);
    free(U.scratch);
    free(U.net);
    free(U.leaf);
    free(U.blk);
    free(U.work);
    free(U.res);
    U.ms.buf = NULL;
    U.tab = NULL;
    U.scratch = U.net = U.leaf = U.blk = NULL;
    U.work = NULL;
    U.res = NULL;
    U.alloc = false;
}
