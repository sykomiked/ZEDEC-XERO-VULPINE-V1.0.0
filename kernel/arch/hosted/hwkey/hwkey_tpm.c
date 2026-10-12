/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwkey_tpm.c — TPM 2.0 as an hwkey provider, spoken directly (no
 * tpm2-tss): five commands, marshalled by hand per the TPM 2.0 Library
 * Specification, Part 3.
 *
 *   create  TPM2_CreatePrimary in the owner hierarchy: an unrestricted
 *           ECC NIST P-256 decryption key (fixedTPM, fixedParent,
 *           sensitiveDataOrigin, userWithAuth, noDA). Primary keys are
 *           re-derived from the owner seed and the template, and the
 *           template's unique field is SHAKE256 of the label, so the
 *           handle stored is just that 32-byte value. The key survives
 *           reboots; it is lost when the TPM is cleared (owner seed reset).
 *   wrap    TPM2_ECDH_KeyGen: the TPM picks the ephemeral E and returns
 *           Z = ECDH(key, E) and E. No elliptic-curve code on the host.
 *   unwrap  TPM2_ECDH_ZGen(key, E) returns Z again.
 *   random  TPM2_GetRandom.
 *   Z is the x coordinate, 32 bytes; E is stored as 04 || x || y.
 *
 * Transport: /dev/tpmrm0 (the kernel's resource manager), or the MS
 * simulator protocol over TCP when ZXV_TPM=mssim:HOST:PORT (swtpm socket
 * --tpm2 --server type=tcp,port=PORT --flags startup-clear), or another
 * device path in ZXV_TPM. Authorisation is the empty password (TPM_RS_PW).
 * POSIX only; Windows uses hwkey_win.c.
 */
#if !defined(_GNU_SOURCE)
#    define _GNU_SOURCE
#endif
#include "hwkey_host.h"
#include "../../../src/mlkem/keccak.h"
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TPM_ST_NO_SESSIONS   0x8001u
#define TPM_ST_SESSIONS      0x8002u
#define TPM_CC_CreatePrimary 0x00000131u
#define TPM_CC_ECDH_ZGen     0x00000154u
#define TPM_CC_ECDH_KeyGen   0x00000163u
#define TPM_CC_FlushContext  0x00000165u
#define TPM_CC_GetRandom     0x0000017Bu
#define TPM_RH_OWNER         0x40000001u
#define TPM_RS_PW            0x40000009u
#define TPM_ALG_ECC          0x0023u
#define TPM_ALG_SHA256       0x000Bu
#define TPM_ALG_NULL         0x0010u
#define TPM_ECC_NIST_P256    0x0003u
/* fixedTPM | fixedParent | sensitiveDataOrigin | userWithAuth | noDA | decrypt */
#define KEY_ATTRS 0x00020472u

#define HANDLE_VER 1u
#define HANDLE_LEN 33u /* version + unique */
#define MSSIM_SEND 8u
#define BUF_MAX    4096u

typedef struct {
    int fd;
    int mssim;
    char where[96];
    /* one cached primary key */
    uint8_t cached_unique[32];
    uint32_t cached_handle;
    int cached;
} tpm_t;

/* ---- transport --------------------------------------------------------- */
static int io_all(int fd, uint8_t *p, size_t n, int wr)
{
    size_t d = 0;
    while (d < n) {
        ssize_t r = wr ? write(fd, p + d, n - d) : read(fd, p + d, n - d);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        d += (size_t) r;
    }
    return 0;
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}
static uint16_t be16(const uint8_t *p)
{
    return (uint16_t) ((p[0] << 8) | p[1]);
}

/* Send a command, receive the response; returns the TPM response code, or
 * -1 on a transport error. */
static long xfer(tpm_t *t, const uint8_t *cmd, uint32_t n, uint8_t *rsp, uint32_t *rn)
{
    if (t->mssim) {
        uint8_t h[9] = {0,
                        0,
                        0,
                        MSSIM_SEND,
                        0,
                        (uint8_t) (n >> 24),
                        (uint8_t) (n >> 16),
                        (uint8_t) (n >> 8),
                        (uint8_t) n};
        uint8_t l[4];
        if (io_all(t->fd, h, sizeof h, 1) || io_all(t->fd, (uint8_t *) cmd, n, 1) ||
            io_all(t->fd, l, 4, 0))
            return -1;
        uint32_t len = be32(l);
        if (len < 10 || len > BUF_MAX || io_all(t->fd, rsp, len, 0) || io_all(t->fd, l, 4, 0))
            return -1;
        *rn = len;
    } else {
        if (io_all(t->fd, (uint8_t *) cmd, n, 1)) return -1;
        ssize_t r;
        do r = read(t->fd, rsp, BUF_MAX);
        while (r < 0 && errno == EINTR);
        if (r < 10) return -1;
        *rn = (uint32_t) r;
    }
    if (be32(rsp + 2) != *rn) return -1;
    return (long) be32(rsp + 6);
}

/* ---- marshalling ------------------------------------------------------- */
typedef struct {
    uint8_t b[BUF_MAX];
    uint32_t n;
} mb_t;

static void u8(mb_t *m, uint8_t v)
{
    m->b[m->n++] = v;
}
static void u16(mb_t *m, uint16_t v)
{
    u8(m, (uint8_t) (v >> 8));
    u8(m, (uint8_t) v);
}
static void u32(mb_t *m, uint32_t v)
{
    u16(m, (uint16_t) (v >> 16));
    u16(m, (uint16_t) v);
}
static void bytes(mb_t *m, const uint8_t *p, uint32_t n)
{
    memcpy(m->b + m->n, p, n);
    m->n += n;
}
static void begin(mb_t *m, uint16_t tag, uint32_t cc)
{
    m->n = 0;
    u16(m, tag);
    u32(m, 0); /* size, patched by finish */
    u32(m, cc);
}
static void finish(mb_t *m)
{
    m->b[2] = (uint8_t) (m->n >> 24);
    m->b[3] = (uint8_t) (m->n >> 16);
    m->b[4] = (uint8_t) (m->n >> 8);
    m->b[5] = (uint8_t) m->n;
}
static void pw_session(mb_t *m)
{
    u32(m, 9);
    u32(m, TPM_RS_PW);
    u16(m, 0); /* nonce */
    u8(m, 0);  /* attributes */
    u16(m, 0); /* empty password */
}

/* parse a TPM2B_ECC_POINT at *o into 32-byte x and y (left-padded) */
static int ecc_point(const uint8_t *r, uint32_t rn, uint32_t *o, uint8_t x[32], uint8_t y[32])
{
    if (*o + 2 > rn) return -1;
    uint32_t sz = be16(r + *o), end = *o + 2 + sz;
    *o += 2;
    if (end > rn) return -1;
    uint8_t *xy[2] = {x, y};
    for (int i = 0; i < 2; i++) {
        if (*o + 2 > end) return -1;
        uint32_t l = be16(r + *o);
        *o += 2;
        if (l > 32 || *o + l > end) return -1;
        memset(xy[i], 0, 32);
        memcpy(xy[i] + 32 - l, r + *o, l);
        *o += l;
    }
    return *o == end ? 0 : -1;
}

static void flush(tpm_t *t, uint32_t handle)
{
    mb_t m;
    uint8_t r[BUF_MAX];
    uint32_t rn;
    begin(&m, TPM_ST_NO_SESSIONS, TPM_CC_FlushContext);
    u32(&m, handle);
    finish(&m);
    (void) xfer(t, m.b, m.n, r, &rn);
}

/* the primary key for `unique`, cached for the process */
static int primary(tpm_t *t, const uint8_t unique[32], uint32_t *handle)
{
    if (t->cached && !memcmp(t->cached_unique, unique, 32)) {
        *handle = t->cached_handle;
        return 0;
    }
    mb_t m;
    begin(&m, TPM_ST_SESSIONS, TPM_CC_CreatePrimary);
    u32(&m, TPM_RH_OWNER);
    pw_session(&m);
    u16(&m, 4); /* TPM2B_SENSITIVE_CREATE: empty userAuth, empty data */
    u16(&m, 0);
    u16(&m, 0);
    uint32_t at = m.n;
    u16(&m, 0); /* TPM2B_PUBLIC size, patched below */
    u16(&m, TPM_ALG_ECC);
    u16(&m, TPM_ALG_SHA256);
    u32(&m, KEY_ATTRS);
    u16(&m, 0);            /* authPolicy */
    u16(&m, TPM_ALG_NULL); /* symmetric */
    u16(&m, TPM_ALG_NULL); /* scheme */
    u16(&m, TPM_ECC_NIST_P256);
    u16(&m, TPM_ALG_NULL); /* kdf */
    u16(&m, 32);
    bytes(&m, unique, 32);
    u16(&m, 0);
    uint32_t pub = m.n - at - 2;
    m.b[at] = (uint8_t) (pub >> 8);
    m.b[at + 1] = (uint8_t) pub;
    u16(&m, 0); /* outsideInfo */
    u32(&m, 0); /* creationPCR: none */
    finish(&m);
    uint8_t r[BUF_MAX];
    uint32_t rn;
    long rc = xfer(t, m.b, m.n, r, &rn);
    if (rc != 0 || rn < 14) return rc ? (int) (rc & 0x7fffffff) : -1;
    if (t->cached) flush(t, t->cached_handle);
    t->cached_handle = be32(r + 10);
    memcpy(t->cached_unique, unique, 32);
    t->cached = 1;
    *handle = t->cached_handle;
    return 0;
}

/* ---- provider callbacks ------------------------------------------------ */
static int tpm_create(void *ctx, const uint8_t *label, uint32_t label_len, uint8_t *handle,
                      uint32_t *handle_len)
{
    tpm_t *t = ctx;
    if (*handle_len < HANDLE_LEN || label_len > HWK_LABEL_MAX) return -1;
    uint8_t in[12 + HWK_LABEL_MAX];
    memcpy(in, "ZXV-TPM-KEY1", 12);
    if (label_len) memcpy(in + 12, label, label_len);
    handle[0] = HANDLE_VER;
    shake256(in, 12 + label_len, handle + 1, 32);
    *handle_len = HANDLE_LEN;
    uint32_t h;
    return primary(t, handle + 1, &h) ? -1 : 0;
}

static int tpm_wrap(void *ctx, const uint8_t *handle, uint32_t handle_len, uint8_t *eph,
                    uint32_t *eph_len, uint8_t secret[HWK_SECRET_BYTES])
{
    tpm_t *t = ctx;
    uint32_t h;
    if (handle_len != HANDLE_LEN || handle[0] != HANDLE_VER || *eph_len < 65) return -1;
    if (primary(t, handle + 1, &h)) return -1;
    mb_t m;
    begin(&m, TPM_ST_NO_SESSIONS, TPM_CC_ECDH_KeyGen);
    u32(&m, h);
    finish(&m);
    uint8_t r[BUF_MAX], zy[32];
    uint32_t rn, o = 10;
    if (xfer(t, m.b, m.n, r, &rn) != 0) return -1;
    eph[0] = 0x04;
    int bad = ecc_point(r, rn, &o, secret, zy) || ecc_point(r, rn, &o, eph + 1, eph + 33);
    hwk_wipe(zy, sizeof zy);
    hwk_wipe(r, sizeof r);
    if (bad) return -1;
    *eph_len = 65;
    return 0;
}

static int tpm_unwrap(void *ctx, const uint8_t *handle, uint32_t handle_len, const uint8_t *eph,
                      uint32_t eph_len, uint8_t secret[HWK_SECRET_BYTES])
{
    tpm_t *t = ctx;
    uint32_t h;
    if (handle_len != HANDLE_LEN || handle[0] != HANDLE_VER || eph_len != 65 || eph[0] != 0x04)
        return -1;
    if (primary(t, handle + 1, &h)) return -1;
    mb_t m;
    begin(&m, TPM_ST_SESSIONS, TPM_CC_ECDH_ZGen);
    u32(&m, h);
    pw_session(&m);
    u16(&m, 2 + 32 + 2 + 32);
    u16(&m, 32);
    bytes(&m, eph + 1, 32);
    u16(&m, 32);
    bytes(&m, eph + 33, 32);
    finish(&m);
    uint8_t r[BUF_MAX], zy[32];
    uint32_t rn, o = 14; /* header + parameterSize */
    if (xfer(t, m.b, m.n, r, &rn) != 0 || rn < 14) return -1;
    int bad = ecc_point(r, rn, &o, secret, zy) ? -1 : 0;
    hwk_wipe(zy, sizeof zy);
    hwk_wipe(r, sizeof r);
    return bad;
}

static int tpm_random(void *ctx, uint8_t *out, uint32_t len)
{
    tpm_t *t = ctx;
    while (len) {
        uint16_t want = len > 32 ? 32 : (uint16_t) len;
        mb_t m;
        begin(&m, TPM_ST_NO_SESSIONS, TPM_CC_GetRandom);
        u16(&m, want);
        finish(&m);
        uint8_t r[BUF_MAX];
        uint32_t rn;
        if (xfer(t, m.b, m.n, r, &rn) != 0 || rn < 12) return -1;
        uint16_t got = be16(r + 10);
        if (got == 0 || got > want || 12u + got > rn) return -1;
        memcpy(out, r + 12, got);
        out += got;
        len -= got;
    }
    return 0;
}

static int connect_mssim(const char *spec, char *why, size_t cap)
{
    char host[64];
    const char *colon = strrchr(spec, ':');
    if (!colon || (size_t) (colon - spec) >= sizeof host) return -1;
    memcpy(host, spec, (size_t) (colon - spec));
    host[colon - spec] = 0;
    struct addrinfo hints, *ai = 0;
    memset(&hints, 0, sizeof hints);
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, colon + 1, &hints, &ai) != 0 || !ai) {
        snprintf(why, cap, "cannot resolve TPM simulator %s", spec);
        return -1;
    }
    int fd = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol);
    if (fd >= 0 && connect(fd, ai->ai_addr, ai->ai_addrlen) != 0) {
        close(fd);
        fd = -1;
    }
    freeaddrinfo(ai);
    if (fd < 0) snprintf(why, cap, "cannot connect to TPM simulator %s", spec);
    return fd;
}

hwk_status_t hwk_tpm_open(hwk_provider_t *p, void **impl, char *why, size_t why_cap)
{
    const char *spec = getenv("ZXV_TPM");
    tpm_t *t = calloc(1, sizeof *t);
    if (!t) return HWK_ERR_HW;
    if (spec && !strncmp(spec, "mssim:", 6)) {
        t->mssim = 1;
        t->fd = connect_mssim(spec + 6, why, why_cap);
    } else {
        const char *dev = spec && *spec ? spec : "/dev/tpmrm0";
        t->fd = open(dev, O_RDWR | O_CLOEXEC);
        if (t->fd < 0) snprintf(why, why_cap, "no TPM: %s: %s", dev, strerror(errno));
        spec = dev;
    }
    if (t->fd < 0) {
        free(t);
        return HWK_ERR_UNAVAILABLE;
    }
    snprintf(t->where, sizeof t->where, "tpm2:%s", spec);
    /* probe: a TPM that cannot give random bytes is no TPM */
    uint8_t probe[8];
    if (tpm_random(t, probe, sizeof probe)) {
        snprintf(why, why_cap, "%s does not answer TPM2_GetRandom", t->where);
        close(t->fd);
        free(t);
        return HWK_ERR_UNAVAILABLE;
    }
    p->kind = HWK_KIND_TPM;
    p->name = t->where;
    p->ctx = t;
    p->create = tpm_create;
    p->wrap = tpm_wrap;
    p->unwrap = tpm_unwrap;
    p->random = tpm_random;
    *impl = t;
    return HWK_OK;
}

void hwk_tpm_close(void *impl)
{
    tpm_t *t = impl;
    if (!t) return;
    if (t->cached) flush(t, t->cached_handle);
    close(t->fd);
    hwk_wipe(t, sizeof *t);
    free(t);
}
