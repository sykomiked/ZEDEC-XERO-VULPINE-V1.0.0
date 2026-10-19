/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwkey_host.c — pick the machine's chip, or the software fallback
 * (hwkey_host.h). */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#    define _GNU_SOURCE
#endif
#include "hwkey_host.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#    include <windows.h>
#    include <bcrypt.h>
#else
#    include <errno.h>
#    include <fcntl.h>
#    include <sys/stat.h>
#    include <unistd.h>
#    if defined(__APPLE__)
#        include <stdlib.h> /* arc4random_buf */
#    else
#        include <sys/random.h>
#    endif
#endif

int hwk_host_random(void *ctx, uint8_t *out, uint32_t len)
{
    (void) ctx;
#ifdef _WIN32
    return BCryptGenRandom(NULL, out, len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? 0 : -1;
#elif defined(__APPLE__)
    arc4random_buf(out, len);
    return 0;
#else
    uint32_t got = 0;
    while (got < len) {
        ssize_t r = getrandom(out + got, len - got, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        got += (uint32_t) r;
    }
    return 0;
#endif
}

#ifndef _WIN32
/* Read the fallback secret, creating it 0600 on first use. A secret file
 * that others can read is refused. */
static int soft_secret(const char *path, uint8_t secret[32], char *why, size_t cap)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 && errno == ENOENT) {
        uint8_t s[32];
        if (hwk_host_random(0, s, sizeof s)) return -1;
        char tmp[4096];
        if (snprintf(tmp, sizeof tmp, "%s.tmp.%ld", path, (long) getpid()) >= (int) sizeof tmp)
            return -1;
        int w = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (w < 0) {
            snprintf(why, cap, "cannot create %s: %s", tmp, strerror(errno));
            return -1;
        }
        int ok = write(w, s, sizeof s) == (ssize_t) sizeof s && fsync(w) == 0;
        close(w);
        hwk_wipe(s, sizeof s);
        /* link, not rename: never replace a secret another process wrote */
        if (!ok || link(tmp, path) != 0) {
            int e = errno;
            unlink(tmp);
            if (ok && e == EEXIST) return soft_secret(path, secret, why, cap);
            snprintf(why, cap, "cannot write %s", path);
            return -1;
        }
        unlink(tmp);
        fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    }
    if (fd < 0) {
        snprintf(why, cap, "cannot open %s: %s", path, strerror(errno));
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || (st.st_mode & 077) ||
        st.st_uid != geteuid() || st.st_size != 32) {
        close(fd);
        snprintf(why, cap, "%s must be a 32-byte file owned by this user, mode 0600", path);
        return -1;
    }
    ssize_t r = read(fd, secret, 32);
    close(fd);
    return r == 32 ? 0 : -1;
}
#endif

/* A chip that opens but cannot do a wrap/unwrap round trip (a TPM that is
 * disabled or owned with a password, an enclave the process may not use)
 * counts as absent. */
static int probe(const hwk_provider_t *p, char *why, size_t cap)
{
    uint8_t handle[HWK_HANDLE_MAX], eph[HWK_EPH_MAX], z1[HWK_SECRET_BYTES], z2[HWK_SECRET_BYTES];
    uint32_t hl = sizeof handle, el = sizeof eph;
    static const uint8_t label[] = "zxv-probe";
    int ok = p->create(p->ctx, label, sizeof label - 1, handle, &hl) == 0 &&
             p->wrap(p->ctx, handle, hl, eph, &el, z1) == 0 &&
             p->unwrap(p->ctx, handle, hl, eph, el, z2) == 0 && hwk_ct_eq(z1, z2, sizeof z1);
    hwk_wipe(z1, sizeof z1);
    hwk_wipe(z2, sizeof z2);
    if (!ok) snprintf(why, cap, "%s is present but failed a seal/open round trip", p->name);
    return ok ? 0 : -1;
}

hwk_status_t hwk_host_open(hwk_host_t *h, const char *soft_path, unsigned flags)
{
    if (!h) return HWK_ERR_ARG;
    memset(h, 0, sizeof *h);
    hwk_status_t st = HWK_ERR_UNAVAILABLE;
    char why[sizeof h->why] = "";
    if (!(flags & HWK_HOST_SOFT_ONLY)) {
#if defined(__APPLE__)
        st = hwk_se_open(&h->p, &h->impl, why, sizeof why);
#elif defined(_WIN32)
        st = hwk_win_open(&h->p, &h->impl, false, why, sizeof why);
#else
        st = hwk_tpm_open(&h->p, &h->impl, why, sizeof why);
#endif
        if (st == HWK_OK && probe(&h->p, why, sizeof why) == 0) return HWK_OK;
        if (st == HWK_OK) {
#if defined(__APPLE__)
            hwk_se_close(h->impl);
#elif defined(_WIN32)
            hwk_win_close(h->impl);
#else
            hwk_tpm_close(h->impl);
#endif
            h->impl = 0;
            memset(&h->p, 0, sizeof h->p);
        }
    } else {
        snprintf(why, sizeof why, "software only was asked for");
    }
    memcpy(h->why, why, sizeof h->why);
    if (flags & HWK_HOST_REQUIRE_HW) return HWK_ERR_UNAVAILABLE;
#ifdef _WIN32
    (void) soft_path;
    st = hwk_win_open(&h->p, &h->impl, true, why, sizeof why);
    if (st != HWK_OK) memcpy(h->why, why, sizeof h->why);
    return st;
#else
    if (!soft_path) return HWK_ERR_ARG;
    char why2[sizeof h->why] = "";
    if (soft_secret(soft_path, h->soft.secret, why2, sizeof why2)) {
        snprintf(h->why, sizeof h->why, "%s", why2[0] ? why2 : "software secret unreadable");
        return HWK_ERR_HW;
    }
    h->soft.rng = hwk_host_random;
    hwk_soft_provider(&h->p, &h->soft);
    return HWK_OK;
#endif
}

void hwk_host_close(hwk_host_t *h)
{
    if (!h) return;
    if (h->impl) {
#if defined(__APPLE__)
        hwk_se_close(h->impl);
#elif defined(_WIN32)
        hwk_win_close(h->impl);
#else
        hwk_tpm_close(h->impl);
#endif
    }
    hwk_wipe(h, sizeof *h);
}

hwk_status_t hwk_host_ctr(hwk_host_t *h, hwk_ctr_op_t op, const uint8_t id[32], uint64_t *v)
{
    if (!h || !id) return HWK_ERR_ARG;
#if !defined(__APPLE__) && !defined(_WIN32)
    if (h->impl && h->p.kind == HWK_KIND_TPM)
        return hwk_tpm_ctr(h->impl, op, id, v) == 0 ? HWK_OK : HWK_ERR_HW;
#else
    (void) op;
    (void) v;
#endif
    return HWK_ERR_UNAVAILABLE;
}
