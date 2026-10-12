/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_net_host.c — UDP glue for one Vinea node. See zxv_net_host.h (U1-U5). */
#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    include <windows.h>
#    include <direct.h>
#    include <io.h>
typedef SOCKET nsock_t;
#    define NCLOSE    closesocket
#    define NBAD_SOCK INVALID_SOCKET
typedef int nlen_t;
#else
#    define _DEFAULT_SOURCE
#    define _DARWIN_C_SOURCE
#    include <sys/types.h>
#    include <sys/socket.h>
#    include <sys/time.h>
#    include <netinet/in.h>
#    include <arpa/inet.h>
#    include <sys/stat.h>
#    include <errno.h>
#    include <fcntl.h>
#    include <sys/file.h>
#    include <unistd.h>
typedef int nsock_t;
#    define NCLOSE    close
#    define NBAD_SOCK (-1)
typedef socklen_t nlen_t;
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zxv_net_host.h"
#include "zxv_http_guard.h" /* zxv_os_random */
#include "vna_node.h"
#include "vna_link.h"
#include "vna_lan.h"
#include "vna_space.h"

#define POOL_CAP    256u
#define KC_CAP      48u
#define RP_CAP      64u
#define DD_CAP      64u
#define STORE_CAP   64u
#define ADDR_LEN    6u
#define READ_BURST  64u
#define TICK_MS     100u
#define REFRESH_MS  60000u
#define DGRAM_SLACK 64u
#define BOOK_CAP    32u
#define SOCK_BUF    (1 << 20)            /* send and receive buffers, bytes */
#define IMPORT_CAP  ((uint64_t) 1 << 20) /* internal rate one cycle may import */
#define USAGE_CAP   32u
#define LEASE_CAP   8u
#define QUOTA_MS    3600000u /* per-peer quota period of the owner's agreement */
#define LAN_PORT     8724u    /* U10 */
#define SPACE_STORE_BASE 32u  /* U14: space records held for others (of STORE_CAP) */
#define LAN_PEERS    64u
#define CACHE_MAX    64u     /* U9: peers kept in the cache file */
#define CACHE_PING   16u     /* U9: cached peers pinged at start */
#define SAVE_MS      300000u /* U9: peer cache saved this often */
#define KEEPALIVE_MS 25000u  /* U11: NAT mappings commonly last 30 s or more */
#define ID_MAGIC     "ZXVNID01"
#define PC_MAGIC     "ZXVNPC01"
#define ID_FILE_LEN  (8u + 32u + 32u + 8u)
static const uint8_t LAN_GROUP[4] = {239, 255, 90, 86};

struct zxv_net {
    zxv_net_mode_t mode;
    nsock_t s;
    uint16_t port;
    char bind_ip[16];
    char error[96];
    uint64_t last_tick, last_refresh;
    uint32_t in, out, refused, policy, send_err;
    vna_identity_t idn;
    vna_node_t node;
    vna_contact_t pool[POOL_CAP];
    vna_keycache_ent_t kc[KC_CAP];
    vna_replay_ent_t rp[RP_CAP];
    uint8_t dd[DD_CAP][32];
    vna_store_slot_t store[STORE_CAP];
    vna_outbox_t ob;
    uint8_t *obuf;
    uint8_t rbuf[VNA_MSG_WIRE_MAX + VNA_XFORM_MAX_OVERHEAD + DGRAM_SLACK];
    /* U6: the mesh economy (vna_econ.h) and its trade loop (vna_link.h) */
    vna_book_t book;
    vna_account_t acct[BOOK_CAP];
    vna_gate_t gate;
    uint8_t owner_secret[32];
    vna_link_t link;
    vna_link_done_fn trade_fn; /* U7 */
    void *trade_ctx;
    /* U8: the owner's sharing agreement (kept across restarts as numbers;
     * rebuilt for each start's identity) and its usage table */
    uint64_t share_compute, share_storage;
    vna_agreement_t agr;
    vna_usage_t us;
    vna_usage_ent_t ue[USAGE_CAP];
    vna_lease_t le[LEASE_CAP];
    bool running; /* start completed (stop saves state only then) */
    /* U9 */
    char state_dir[512];
    uint8_t seed[32]; /* the identity's seed while running */
    bool persisted;
    bool shared_dir; /* another process holds the folder: this start is fresh */
#if defined(_WIN32)
    HANDLE lockh;
#else
    int lockfd;
#endif
    uint32_t cached_peers;
    uint64_t saved_lan_seq, last_save;
    /* U10 */
    bool lan_want;
    uint16_t lan_port;
    nsock_t ls;
    bool lan_on;
    uint8_t group[ADDR_LEN];
    vna_lan_t lan;
    vna_lan_peer_t lpeers[LAN_PEERS];
    /* U11 */
    uint64_t last_keepalive;
    /* U12 */
    aipi_ctx_t *ai;
    aipi_vna_t av;
    bool av_on;
    bool aipi_serve;
    uint64_t aipi_min_bounty, aipi_budget, aipi_escrow;
    /* U14 */
    vna_spaces_t *sp; /* only while running (a few MB) */
    uint32_t space_base;
    uint8_t lan_self[4];
    bool lan_self_ok;
};

uint64_t zxv_net_wall_ms(void)
{
#if defined(_WIN32)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t) ft.dwHighDateTime << 32) | ft.dwLowDateTime; /* 100 ns since 1601 */
    return t / 10000u - 11644473600000ull;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t) ts.tv_sec * 1000u + (uint64_t) ts.tv_nsec / 1000000u;
#endif
}

const char *zxv_net_mode_name(zxv_net_mode_t m)
{
    return m == ZXV_NET_LAN ? "lan" : m == ZXV_NET_ONLINE ? "online" : "off";
}

int zxv_net_mode_parse(const char *s, zxv_net_mode_t *m)
{
    if (!s || !m) return -1;
    if (!strcmp(s, "off"))
        *m = ZXV_NET_OFF;
    else if (!strcmp(s, "lan"))
        *m = ZXV_NET_LAN;
    else if (!strcmp(s, "online"))
        *m = ZXV_NET_ONLINE;
    else
        return -1;
    return 0;
}

zxv_net_t *zxv_net_new(void)
{
    zxv_net_t *n = (zxv_net_t *) calloc(1, sizeof *n);
    if (!n) return NULL;
    n->s = NBAD_SOCK;
    n->ls = NBAD_SOCK;
    n->mode = ZXV_NET_OFF;
    n->lan_want = true;
    n->lan_port = LAN_PORT;
    n->space_base = SPACE_STORE_BASE;
#if defined(_WIN32)
    n->lockh = INVALID_HANDLE_VALUE;
#else
    n->lockfd = -1;
#endif
    return n;
}

static void flush(zxv_net_t *n);
static int save_identity(zxv_net_t *n);
static void unlock_dir(zxv_net_t *n);

void zxv_net_stop(zxv_net_t *n)
{
    if (!n) return;
    if (n->running) {
        if (n->lan_on) { /* U10: a signed GOODBYE, then remember the seq it used */
            (void) vna_lan_stop(&n->lan, zxv_net_wall_ms(), &n->ob);
            flush(n);
        }
        (void) zxv_net_save_peers(n);
        (void) save_identity(n);
    }
    unlock_dir(n);
    n->running = false;
    if (n->ai) memset(&n->ai->mesh, 0, sizeof n->ai->mesh); /* U12: local only while off */
    n->av_on = false;
    n->aipi_escrow = 0;
    free(n->sp); /* U14: spaces live only while the node runs */
    n->sp = NULL;
    n->lan_self_ok = false;
    if (n->ls != NBAD_SOCK) NCLOSE(n->ls);
    n->ls = NBAD_SOCK;
    n->lan_on = false;
    if (n->s != NBAD_SOCK) NCLOSE(n->s);
    n->s = NBAD_SOCK;
    n->mode = ZXV_NET_OFF;
    n->port = 0;
    free(n->obuf);
    n->obuf = NULL;
    /* the secret key does not outlive the session */
    memset(&n->idn, 0, sizeof n->idn);
    memset(n->seed, 0, sizeof n->seed);
    memset(&n->node, 0, sizeof n->node);
    memset(&n->lan, 0, sizeof n->lan);
    memset(n->owner_secret, 0, sizeof n->owner_secret);
    memset(&n->book, 0, sizeof n->book);
    memset(&n->link, 0, sizeof n->link);
}

void zxv_net_free(zxv_net_t *n)
{
    if (!n) return;
    zxv_net_stop(n);
    free(n);
}

intptr_t zxv_net_fd(const zxv_net_t *n)
{
    return (n && n->s != NBAD_SOCK) ? (intptr_t) n->s : (intptr_t) -1;
}

/* U2: private-use, link-local and loopback IPv4 ranges. */
static bool ip_private(const uint8_t a[4])
{
    return a[0] == 10 || a[0] == 127 || (a[0] == 172 && (a[1] & 0xF0) == 16) ||
           (a[0] == 192 && a[1] == 168) || (a[0] == 169 && a[1] == 254);
}

static bool allowed(const zxv_net_t *n, const uint8_t a[4])
{
    if (n->mode == ZXV_NET_ONLINE) return true;
    if (n->mode == ZXV_NET_LAN) return ip_private(a);
    return false;
}

static void to_addr6(const struct sockaddr_in *sa, uint8_t out[ADDR_LEN])
{
    memcpy(out, &sa->sin_addr.s_addr, 4); /* already network order */
    memcpy(out + 4, &sa->sin_port, 2);
}

static void flush(zxv_net_t *n)
{
    for (uint32_t i = 0; i < n->ob.n; i++) {
        const vna_out_ent_t *e = &n->ob.e[i];
        if (e->addr_len == ADDR_LEN && !memcmp(e->addr, n->group, ADDR_LEN) && n->lan_on) {
            struct sockaddr_in g; /* U10: the LAN discovery group (TTL 1) */
            memset(&g, 0, sizeof g);
            g.sin_family = AF_INET;
            memcpy(&g.sin_addr.s_addr, e->addr, 4);
            memcpy(&g.sin_port, e->addr + 4, 2);
            int k = (int) sendto(n->ls, (const char *) n->ob.buf + e->off, (int) e->len, 0,
                                 (const struct sockaddr *) &g, sizeof g);
            if (k == (int) e->len)
                n->out++;
            else
                n->send_err++;
            continue;
        }
        if (e->addr_len != ADDR_LEN || !allowed(n, e->addr)) {
            n->policy++;
            continue;
        }
        struct sockaddr_in to;
        memset(&to, 0, sizeof to);
        to.sin_family = AF_INET;
        memcpy(&to.sin_addr.s_addr, e->addr, 4);
        memcpy(&to.sin_port, e->addr + 4, 2);
        if (to.sin_port == 0) {
            n->policy++;
            continue;
        }
        int k = (int) sendto(n->s, (const char *) n->ob.buf + e->off, (int) e->len, 0,
                             (const struct sockaddr *) &to, sizeof to);
        if (k == (int) e->len)
            n->out++;
        else
            n->send_err++;
    }
    vna_outbox_clear(&n->ob);
}

/* U6: send what the trade loop queued */
static void drain_link(zxv_net_t *n, uint64_t now_ms)
{
    vna_id_t dst;
    const uint8_t *b;
    uint32_t len;
    while (vna_link_next_out(&n->link, &dst, &b, &len))
        (void) vna_node_send_rcpt(&n->node, &dst, b, len, now_ms, &n->ob);
}

/* U8: the agreement the owner's numbers describe, or NULL (share nothing). */
static const vna_agreement_t *build_agreement(zxv_net_t *n)
{
    if (!n->share_compute && !n->share_storage) return NULL;
    vna_agree_default(&n->agr, &n->idn.id);
    n->agr.degree = n->share_compute ? VNA_DEG_COMPUTE : VNA_DEG_STORE;
    n->agr.audience = VNA_AUD_EVERYONE; /* U2 still limits who can reach the node */
    n->agr.compute_per_cycle = n->agr.q_compute = n->share_compute;
    n->agr.storage_bytes = n->agr.q_storage = n->share_storage;
    n->agr.quota_period_ms = QUOTA_MS;
    return &n->agr;
}

/* Register `id` in the book from the key the node verified (keycache), if
 * the book does not know it yet. Not a commons member (the owner admits
 * members). */
static bool book_learn(zxv_net_t *n, const vna_id_t *id)
{
    if (vna_book_find(&n->book, id)) return true;
    const vna_keycache_t *kc = &n->node.kc;
    const vna_keycache_ent_t *e = NULL;
    for (uint32_t i = 0; i < kc->cap && !e; i++)
        if (kc->e[i].used && vna_id_eq(&kc->e[i].id, id)) e = &kc->e[i];
    return e && vna_book_add_peer(&n->book, id, e->pk, e->pow_nonce, e->pow_bits, false) == VNA_OK;
}

/* U6: a receipt from a peer. The node verified the message (signature, PoW,
 * replay); a peer the book does not know yet is registered from the key the
 * node verified, not a commons member (the owner admits members). */
static vna_status_t on_rcpt(void *ctx, const vna_id_t *from, const uint8_t *r, uint32_t len,
                            uint64_t now)
{
    zxv_net_t *n = (zxv_net_t *) ctx;
    if (!book_learn(n, from)) return VNA_ERR_DENIED;
    return vna_link_on_rcpt(&n->link, from, r, len, now);
}

static int set_nonblocking(nsock_t s)
{
#if defined(_WIN32)
    u_long one = 1;
    return ioctlsocket(s, FIONBIO, &one) == 0 ? 0 : -1;
#else
    int fl = fcntl(s, F_GETFL, 0);
    if (fl < 0 || fcntl(s, F_SETFL, fl | O_NONBLOCK) != 0) return -1;
    (void) fcntl(s, F_SETFD, FD_CLOEXEC);
    return 0;
#endif
}

static int fail(zxv_net_t *n, const char *why)
{
    char keep[sizeof n->error];
    snprintf(keep, sizeof keep, "%s", why);
    zxv_net_stop(n);
    memcpy(n->error, keep, sizeof keep);
    return -1;
}

/* ---- U9: the state folder ---- */
static int state_path(const zxv_net_t *n, const char *name, char *out, size_t cap)
{
    int k = snprintf(out, cap, "%s/%s", n->state_dir, name);
    return k > 0 && (size_t) k < cap ? 0 : -1;
}

/* The folder exists (created 0700 if not) and only its owner can write it. */
static int ensure_dir(const char *dir)
{
#if defined(_WIN32)
    if (_mkdir(dir) != 0 && errno != EEXIST) return -1;
    return 0;
#else
    struct stat st;
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) return -1;
    if (lstat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) return -1;
    if ((st.st_mode & 022) || st.st_uid != geteuid()) return -1;
    return 0;
#endif
}

/* 1 missing, 0 read, -1 error, -2 readable or writable by others (POSIX). */
static int read_file(const char *path, uint8_t *buf, size_t cap, size_t *len, bool secret)
{
    *len = 0;
#if defined(_WIN32)
    (void) secret;
    FILE *f = fopen(path, "rb");
    if (!f) return errno == ENOENT ? 1 : -1;
    *len = fread(buf, 1, cap, f);
    int bad = ferror(f);
    fclose(f);
    return bad ? -1 : 0;
#else
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return errno == ENOENT ? 1 : -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return -1;
    }
    if (secret && ((st.st_mode & 077) || st.st_uid != geteuid())) {
        close(fd);
        return -2;
    }
    while (*len < cap) {
        ssize_t k = read(fd, buf + *len, cap - *len);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) break;
        *len += (size_t) k;
    }
    close(fd);
    return 0;
#endif
}

/* Write to path.tmp (0600), sync, rename over path. */
static int write_file(const char *path, const uint8_t *buf, size_t len)
{
    char tmp[600];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int) sizeof tmp) return -1;
#if defined(_WIN32)
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    size_t w = fwrite(buf, 1, len, f);
    int bad = fflush(f) != 0 || w != len;
    if (fclose(f) != 0) bad = 1;
    if (bad || !MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove(tmp);
        return -1;
    }
    return 0;
#else
    (void) unlink(tmp);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    size_t off = 0;
    while (off < len) {
        ssize_t k = write(fd, buf + off, len - off);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) break;
        off += (size_t) k;
    }
    int bad = off != len || fsync(fd) != 0;
    if (close(fd) != 0) bad = 1;
    if (bad || rename(tmp, path) != 0) {
        (void) unlink(tmp);
        return -1;
    }
    return 0;
#endif
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

static int save_identity(zxv_net_t *n)
{
    if (!n->persisted || !n->state_dir[0]) return 0;
    uint64_t seq = n->lan_on ? vna_lan_seq(&n->lan) : 0;
    if (seq < n->saved_lan_seq) seq = n->saved_lan_seq;
    uint8_t f[ID_FILE_LEN];
    char path[560];
    memcpy(f, ID_MAGIC, 8);
    memcpy(f + 8, n->seed, 32);
    memcpy(f + 40, n->idn.id.b, 32);
    put64(f + 72, seq);
    int r = state_path(n, "vinea_identity", path, sizeof path) == 0 ? write_file(path, f, sizeof f)
                                                                    : -1;
    memset(f, 0, sizeof f);
    if (r == 0) n->saved_lan_seq = seq;
    return r;
}

/* U9: one process at a time owns the state folder (two instances must not
 * share a NodeID or a message seq). 0 held, 1 held by another, -1 error. */
static int lock_dir(zxv_net_t *n)
{
    char path[560];
    if (state_path(n, "vinea.lock", path, sizeof path) != 0) return -1;
#if defined(_WIN32)
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_SHARING_VIOLATION ? 1 : -1;
    n->lockh = h;
#else
    int fd = open(path, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        int busy = errno == EWOULDBLOCK;
        close(fd);
        return busy ? 1 : -1;
    }
    n->lockfd = fd;
#endif
    return 0;
}

static void unlock_dir(zxv_net_t *n)
{
#if defined(_WIN32)
    if (n->lockh != INVALID_HANDLE_VALUE) CloseHandle(n->lockh);
    n->lockh = INVALID_HANDLE_VALUE;
#else
    if (n->lockfd >= 0) close(n->lockfd); /* releases the lock */
    n->lockfd = -1;
#endif
}

/* U5 + U9: this start's identity: from the state folder, or new. NULL on
 * success, else the reason. */
static const char *load_identity(zxv_net_t *n)
{
    n->persisted = false;
    n->shared_dir = false;
    n->saved_lan_seq = 0;
    int lk = 0;
    if (n->state_dir[0]) {
        if (ensure_dir(n->state_dir) != 0) return "state folder missing or writable by others";
        lk = lock_dir(n);
        if (lk < 0) return "state folder cannot be locked";
        n->shared_dir = lk == 1;
    }
    if (!n->state_dir[0] || n->shared_dir) {
        if (zxv_os_random(n->seed, sizeof n->seed) != 0) return "no secure random source";
        return vna_identity_create(&n->idn, n->seed, 0, 1) == VNA_OK ? NULL
                                                                     : "identity creation failed";
    }
    char path[560];
    uint8_t f[ID_FILE_LEN + 1];
    size_t len;
    if (state_path(n, "vinea_identity", path, sizeof path) != 0)
        return "state folder path too long";
    int r = read_file(path, f, sizeof f, &len, true);
    if (r == -2) return "identity file is readable by others (chmod 600 it)";
    if (r < 0) return "identity file unreadable";
    if (r == 1) { /* first start: make one and keep it */
        if (zxv_os_random(n->seed, sizeof n->seed) != 0) return "no secure random source";
        if (vna_identity_create(&n->idn, n->seed, 0, 1) != VNA_OK)
            return "identity creation failed";
        n->persisted = true;
        if (save_identity(n) != 0) {
            n->persisted = false;
            return "could not write the identity file";
        }
        return NULL;
    }
    if (len != ID_FILE_LEN || memcmp(f, ID_MAGIC, 8) != 0) {
        memset(f, 0, sizeof f);
        return "identity file is damaged";
    }
    memcpy(n->seed, f + 8, 32);
    vna_status_t st = vna_identity_create(&n->idn, n->seed, 0, 1);
    bool match = st == VNA_OK && memcmp(n->idn.id.b, f + 40, 32) == 0;
    n->saved_lan_seq = get64(f + 72);
    memset(f, 0, sizeof f);
    if (!match) return "identity file does not give the NodeID it records";
    n->persisted = true;
    return NULL;
}

int zxv_net_set_state_dir(zxv_net_t *n, const char *dir)
{
    if (!n) return -1;
    if (!dir || !dir[0]) {
        n->state_dir[0] = 0;
        return 0;
    }
    if (strlen(dir) >= sizeof n->state_dir - 32) return -1;
    snprintf(n->state_dir, sizeof n->state_dir, "%s", dir);
    return 0;
}

int zxv_net_save_peers(zxv_net_t *n)
{
    if (!n || !n->state_dir[0] || !n->persisted || n->s == NBAD_SOCK) return 0;
    static vna_contact_t c[POOL_CAP];
    static uint8_t f[12 + CACHE_MAX * (32 + ADDR_LEN)];
    uint32_t k = vna_node_contacts(&n->node, c, POOL_CAP), m = 0;
    memcpy(f, PC_MAGIC, 8);
    for (uint32_t i = 0; i < k && m < CACHE_MAX; i++) {
        if (c[i].addr_len != ADDR_LEN) continue;
        uint8_t *e = f + 12 + m * (32 + ADDR_LEN);
        memcpy(e, c[i].id.b, 32);
        memcpy(e + 32, c[i].addr, ADDR_LEN);
        m++;
    }
    for (int i = 0; i < 4; i++) f[8 + i] = (uint8_t) (m >> (8 * i));
    char path[560];
    if (state_path(n, "vinea_peers", path, sizeof path) != 0) return -1;
    return write_file(path, f, 12 + (size_t) m * (32 + ADDR_LEN));
}

/* U9: ping the cached peers (their PONG proves who answers). */
static void load_peers(zxv_net_t *n, uint64_t now_ms)
{
    static uint8_t f[12 + CACHE_MAX * (32 + ADDR_LEN)];
    char path[560];
    size_t len;
    n->cached_peers = 0;
    if (!n->persisted || state_path(n, "vinea_peers", path, sizeof path) != 0 ||
        read_file(path, f, sizeof f, &len, false) != 0 || len < 12 || memcmp(f, PC_MAGIC, 8) != 0)
        return;
    uint32_t m =
        (uint32_t) f[8] | (uint32_t) f[9] << 8 | (uint32_t) f[10] << 16 | (uint32_t) f[11] << 24;
    if (m > CACHE_MAX || len != 12 + (size_t) m * (32 + ADDR_LEN)) return;
    for (uint32_t i = 0; i < m && n->cached_peers < CACHE_PING; i++) {
        const uint8_t *a = f + 12 + i * (32 + ADDR_LEN) + 32;
        if (!allowed(n, a) || (a[4] == 0 && a[5] == 0)) continue;
        if (vna_node_bootstrap(&n->node, a, ADDR_LEN, now_ms, &n->ob) == VNA_OK) n->cached_peers++;
        flush(n);
    }
}

/* ---- U10: LAN discovery ---- */
static void lan_open(zxv_net_t *n, uint64_t now_ms)
{
    struct in_addr ifa;
    bool specific = strcmp(n->bind_ip, "0.0.0.0") != 0;
    if (inet_pton(AF_INET, specific ? n->bind_ip : "0.0.0.0", &ifa) != 1) return;
    /* this node's LAN address for its record */
    uint8_t self[4];
    memcpy(self, &ifa.s_addr, 4);
    if (!specific) { /* the interface the OS would send to the group from */
        nsock_t t = socket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in g, me;
        memset(&g, 0, sizeof g);
        g.sin_family = AF_INET;
        memcpy(&g.sin_addr.s_addr, LAN_GROUP, 4);
        g.sin_port = htons(n->lan_port);
        nlen_t ml = sizeof me;
        bool ok = t != NBAD_SOCK && connect(t, (struct sockaddr *) &g, sizeof g) == 0 &&
                  getsockname(t, (struct sockaddr *) &me, &ml) == 0;
        if (t != NBAD_SOCK) NCLOSE(t);
        if (!ok) return;
        memcpy(self, &me.sin_addr.s_addr, 4);
    }
    if ((self[0] | self[1] | self[2] | self[3]) == 0) return;
    memcpy(n->lan_self, self, 4); /* U14: where space visitors reach this node */
    n->lan_self_ok = true;

    nsock_t s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == NBAD_SOCK) return;
    int one = 1;
    (void) setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *) &one, sizeof one);
#if defined(SO_REUSEPORT) && !defined(_WIN32)
    (void) setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (const char *) &one, sizeof one);
#endif
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(n->lan_port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    struct ip_mreq mr;
    memset(&mr, 0, sizeof mr);
    memcpy(&mr.imr_multiaddr.s_addr, LAN_GROUP, 4);
    mr.imr_interface = specific ? ifa : (struct in_addr){0};
#if defined(_WIN32)
    DWORD ttl = 1, loop = 1;
#else
    unsigned char ttl = 1, loop = 1;
#endif
    if (set_nonblocking(s) != 0 || bind(s, (struct sockaddr *) &sa, sizeof sa) != 0 ||
        setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *) &mr, sizeof mr) != 0) {
        NCLOSE(s);
        return;
    }
    (void) setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (const char *) &ttl, sizeof ttl);
    (void) setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, (const char *) &loop, sizeof loop);
    if (specific)
        (void) setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (const char *) &ifa, sizeof ifa);

    vna_lan_cfg_t cfg;
    vna_lan_cfg_default(&cfg);
    vna_addr_el_t me;
    me.len = ADDR_LEN;
    memcpy(me.a, self, 4);
    me.a[4] = (uint8_t) (n->port >> 8);
    me.a[5] = (uint8_t) n->port;
    memcpy(n->group, LAN_GROUP, 4);
    n->group[4] = (uint8_t) (n->lan_port >> 8);
    n->group[5] = (uint8_t) n->lan_port;
    uint8_t lseed[32];
    if (zxv_os_random(lseed, sizeof lseed) != 0 ||
        vna_lan_init(&n->lan, &n->node, &cfg, n->group, ADDR_LEN, &me, 1, n->lpeers, LAN_PEERS,
                     lseed) != VNA_OK) {
        NCLOSE(s);
        return;
    }
    memset(lseed, 0, sizeof lseed);
    /* never reuse a record seq a peer may still hold (wall clock as the floor) */
    vna_lan_resume_seq(&n->lan, n->saved_lan_seq > now_ms ? n->saved_lan_seq : now_ms);
    n->ls = s;
    n->lan_on = true;
    (void) vna_lan_start(&n->lan, now_ms, &n->ob);
    flush(n);
}

static void drain_lan(zxv_net_t *n, uint64_t now_ms)
{
    if (!n->lan_on) return;
    for (uint32_t i = 0; i < READ_BURST; i++) {
        struct sockaddr_in from;
        nlen_t fl = sizeof from;
        int k = (int) recvfrom(n->ls, (char *) n->rbuf, (int) sizeof n->rbuf, 0,
                               (struct sockaddr *) &from, &fl);
        if (k < 0) {
#if defined(_WIN32)
            int e = WSAGetLastError();
            if (e == WSAECONNRESET || e == WSAEMSGSIZE) continue;
#else
            if (errno == EINTR || errno == ECONNREFUSED) continue;
#endif
            return;
        }
        uint8_t a[ADDR_LEN];
        if (fl < (nlen_t) sizeof from || from.sin_family != AF_INET) continue;
        to_addr6(&from, a);
        if (!allowed(n, a) || (size_t) k > VNA_LAN_MSG_MAX) {
            n->policy++;
            continue;
        }
        n->in++;
        (void) vna_lan_handle(&n->lan, n->rbuf, (uint32_t) k, now_ms, &n->ob);
        flush(n);
    }
}

void zxv_net_set_lan_discovery(zxv_net_t *n, bool on, uint16_t port)
{
    if (!n) return;
    n->lan_want = on;
    n->lan_port = port ? port : LAN_PORT;
}

intptr_t zxv_net_lan_fd(const zxv_net_t *n)
{
    return (n && n->lan_on) ? (intptr_t) n->ls : (intptr_t) -1;
}

/* ---- U12: AIPI bounties on the mesh economy ---- */
static int av_reserve(void *ctx, uint64_t amount)
{
    zxv_net_t *n = (zxv_net_t *) ctx;
    if (amount > n->aipi_budget || n->aipi_escrow > n->aipi_budget - amount) return -1;
    n->aipi_escrow += amount;
    return 0;
}

static void av_refund(void *ctx, uint64_t amount)
{
    zxv_net_t *n = (zxv_net_t *) ctx;
    n->aipi_escrow = amount > n->aipi_escrow ? 0 : n->aipi_escrow - amount;
}

static int av_pay(void *ctx, const vna_id_t *executor, uint64_t amount, const ipfsn_cid_t *call)
{
    zxv_net_t *n = (zxv_net_t *) ctx;
    (void) call;
    av_refund(ctx, amount); /* leaves escrow: owed to the executor now */
    if (!book_learn(n, executor)) return -1;
    return vna_link_want_from(&n->link, executor, VNA_RES_COMPUTE, SWARM_CAP_SYSTEM, 1, amount) >= 0
               ? 0
               : -1;
}

static void av_paid(void *ctx, const vna_id_t *caller, uint64_t amount, const ipfsn_cid_t *call)
{
    zxv_net_t *n = (zxv_net_t *) ctx;
    (void) call;
    if (!n->share_compute || amount == 0 || !book_learn(n, caller)) return;
    (void) vna_link_sell(&n->link, caller, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1,
                         amount, "offer(compute, units: 1)", n->av.now);
}

static void bind_aipi(zxv_net_t *n)
{
    n->av_on = false;
    if (!n->ai || n->s == NBAD_SOCK) return;
    aipi_vna_econ_t ec = {n, av_reserve, av_pay, av_refund, av_paid};
    if (aipi_vna_init(&n->av, n->ai, &n->node, &n->ob, &ec) != AIPI_OK) return;
    aipi_vna_serve(&n->av, n->aipi_serve, n->aipi_min_bounty);
    n->av_on = true;
}

int zxv_net_attach_aipi(zxv_net_t *n, aipi_ctx_t *ai)
{
    if (!n) return -1;
    if (n->ai && n->ai != ai) memset(&n->ai->mesh, 0, sizeof n->ai->mesh);
    if (n->av_on) vna_node_set_aipi_handler(&n->node, NULL, NULL);
    n->ai = ai;
    n->av_on = false;
    bind_aipi(n);
    return 0;
}

aipi_vna_t *zxv_net_aipi(zxv_net_t *n)
{
    return (n && n->av_on) ? &n->av : NULL;
}

void zxv_net_aipi_budget(zxv_net_t *n, uint64_t max_escrow)
{
    if (n) n->aipi_budget = max_escrow;
}

void zxv_net_aipi_serve(zxv_net_t *n, bool on, uint64_t min_bounty)
{
    if (!n) return;
    n->aipi_serve = on;
    n->aipi_min_bounty = min_bounty;
    if (n->av_on) aipi_vna_serve(&n->av, on, min_bounty);
}

int zxv_net_aipi_execute(zxv_net_t *n, const ipfsn_cid_t *fn_cid, const uint8_t *in,
                         uint32_t in_len, bool input_private, uint64_t bounty, uint64_t ttl_ms,
                         uint64_t now_ms, uint32_t *job)
{
    if (!n || !n->ai) return AIPI_ERR_ARG;
    if (!n->av_on) /* off: the context has no mesh, so this runs locally or not at all */
        return aipi_execute(n->ai, fn_cid, in, in_len, input_private, bounty, now_ms, ttl_ms, job);
    int r =
        aipi_vna_execute(&n->av, fn_cid, in, in_len, input_private, bounty, now_ms, ttl_ms, job);
    drain_link(n, now_ms);
    flush(n);
    return r;
}

/* ---- U11 ---- */
int zxv_net_punch(zxv_net_t *n, const uint8_t target[32], uint64_t now_ms)
{
    if (!n || n->s == NBAD_SOCK || !target) return 0;
    vna_id_t t;
    memcpy(t.b, target, 32);
    static vna_contact_t c[POOL_CAP];
    uint32_t k = vna_rt_closest(&n->node.rt, &t, 4, &t, c), asked = 0;
    for (uint32_t i = 0; i < k && asked < 3; i++)
        if (vna_node_punch(&n->node, &t, &c[i].id, now_ms, &n->ob) == VNA_OK) asked++;
    flush(n);
    return (int) asked;
}

/* ---- U14: public spaces and the watch hall ---- */
/* The address host records carry: the public one two peers agree on
 * (ONLINE), else the LAN address, else a specific bind address. */
static void spaces_addr(zxv_net_t *n)
{
    uint8_t a[VNA_ADDR_MAX], al = 0;
    struct in_addr ip;
    if (n->mode == ZXV_NET_ONLINE && vna_node_reflexive(&n->node, a, &al) >= 2 && al == ADDR_LEN) {
        vna_spaces_set_addr(n->sp, a, ADDR_LEN);
        return;
    }
    if (n->lan_self_ok)
        memcpy(a, n->lan_self, 4);
    else if (strcmp(n->bind_ip, "0.0.0.0") != 0 && inet_pton(AF_INET, n->bind_ip, &ip) == 1)
        memcpy(a, &ip.s_addr, 4);
    else {
        vna_spaces_set_addr(n->sp, a, 0); /* peers use the address they heard from */
        return;
    }
    a[4] = (uint8_t) (n->port >> 8);
    a[5] = (uint8_t) n->port;
    vna_spaces_set_addr(n->sp, a, ADDR_LEN);
}

static int spaces_open(zxv_net_t *n)
{
    uint8_t seed[32];
    if (!(n->sp = (vna_spaces_t *) calloc(1, sizeof *n->sp))) return fail(n, "out of memory");
    if (zxv_os_random(seed, sizeof seed) != 0) return fail(n, "no secure random source");
    vna_status_t st = (vna_status_t) vna_spaces_init(n->sp, &n->node, &n->ob, seed);
    memset(seed, 0, sizeof seed);
    if (st != VNA_OK) return fail(n, "spaces init failed");
    vna_spaces_store_base(n->sp, n->space_base);
    spaces_addr(n);
    return 0;
}

int zxv_net_space_store(zxv_net_t *n, uint32_t cap)
{
    if (!n || cap > STORE_CAP) return -1;
    n->space_base = cap;
    if (n->sp) vna_spaces_store_base(n->sp, cap);
    return 0;
}

const vna_spaces_t *zxv_net_spaces(const zxv_net_t *n)
{
    return n ? n->sp : NULL;
}

int zxv_net_space_create(zxv_net_t *n, uint8_t kind, uint8_t gov, uint8_t quorum_pct,
                         const char *name, const char *topic, uint64_t now_ms,
                         uint8_t cid[VNA_CID_RAW_LEN])
{
    if (!n || !n->sp) return VNA_ERR_ARG;
    int r = vna_space_create(n->sp, kind, gov, quorum_pct, name, topic ? topic : "", now_ms, cid);
    flush(n);
    return r;
}

int zxv_net_space_join(zxv_net_t *n, const uint8_t cid[VNA_CID_RAW_LEN], uint64_t now_ms)
{
    if (!n || !n->sp) return VNA_ERR_ARG;
    int r = vna_space_join(n->sp, cid, now_ms);
    flush(n);
    return r;
}

int zxv_net_space_leave(zxv_net_t *n, const uint8_t cid[VNA_CID_RAW_LEN], uint64_t now_ms)
{
    if (!n || !n->sp) return VNA_ERR_ARG;
    int r = vna_space_leave(n->sp, cid, now_ms);
    flush(n);
    return r;
}

int zxv_net_space_post(zxv_net_t *n, const uint8_t cid[VNA_CID_RAW_LEN], const uint8_t *text,
                       uint32_t len, uint64_t now_ms)
{
    if (!n || !n->sp) return VNA_ERR_ARG;
    int r = vna_space_post(n->sp, cid, text, len, now_ms);
    flush(n);
    return r;
}

int zxv_net_hall_set(zxv_net_t *n, const uint8_t cid[VNA_CID_RAW_LEN], uint8_t state,
                     const uint8_t *media, uint32_t media_len, uint64_t pos_ms, uint32_t rate_q16,
                     uint64_t now_ms)
{
    if (!n || !n->sp) return VNA_ERR_ARG;
    int r = vna_space_hall_set(n->sp, cid, state, media, media_len, pos_ms, rate_q16, now_ms);
    flush(n);
    return r;
}

int zxv_net_space_find(zxv_net_t *n, const char *topic, uint64_t now_ms)
{
    if (!n || !n->sp) return VNA_ERR_ARG;
    int r = vna_space_find(n->sp, topic, now_ms);
    flush(n);
    return r;
}

int zxv_net_start(zxv_net_t *n, zxv_net_mode_t mode, const char *bind_ip, uint16_t port,
                  uint64_t now_ms)
{
    if (!n) return -1;
    zxv_net_stop(n);
    n->error[0] = 0;
    n->in = n->out = n->refused = n->policy = n->send_err = 0;
    if (mode == ZXV_NET_OFF) return 0; /* U1: nothing opened */
    if (mode != ZXV_NET_LAN && mode != ZXV_NET_ONLINE) return fail(n, "unknown mode");
    const char *ip = bind_ip && bind_ip[0] ? bind_ip : "0.0.0.0";
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &sa.sin_addr) != 1) return fail(n, "bad bind address");

    /* U5 + U9: identity and node */
    uint8_t dseed[32];
    const char *why = load_identity(n);
    if (why) return fail(n, why);
    if (zxv_os_random(dseed, sizeof dseed) != 0) return fail(n, "no secure random source");
    vna_status_t st;
    vna_node_cfg_t cfg;
    vna_node_cfg_default(&cfg);
    cfg.degree = VNA_DEG_ROUTE;
    vna_node_mem_t mem = {n->pool, POOL_CAP, n->kc,  KC_CAP,   n->rp,
                          RP_CAP,  n->dd,    DD_CAP, n->store, STORE_CAP};
    st = vna_node_init(&n->node, &n->idn, &cfg, &mem, dseed);
    memset(dseed, 0, sizeof dseed);
    if (st != VNA_OK) return fail(n, "node init failed");
    /* U9: a kept identity must never reuse a message seq (peers' replay
     * windows remember it); the wall clock gives a floor above every seq it
     * sent before (ms x 1024) */
    vna_node_resume_seq(&n->node, zxv_net_wall_ms() << 10);
    vna_node_set_agreement(&n->node, NULL, NULL, NULL, NULL);

    /* U6: the book, the owner gate and the trade loop. Until the owner
     * shares something (U8) no agreement is attached, so this node sells
     * nothing (fail closed) and wants nothing; the loop still runs every
     * cycle. */
    uint8_t lseed[32];
    if (zxv_os_random(n->owner_secret, sizeof n->owner_secret) != 0 ||
        zxv_os_random(lseed, sizeof lseed) != 0)
        return fail(n, "no secure random source");
    vna_book_init(&n->book, &n->idn, n->acct, BOOK_CAP);
    vna_gate_init(&n->gate, n->owner_secret, 0, IMPORT_CAP, 4);
    if (vna_gate_begin_cycle(&n->gate, n->owner_secret) != VNA_OK) return fail(n, "gate failed");
    vna_usage_init(&n->us, n->ue, USAGE_CAP, n->le, LEASE_CAP);
    vna_link_init(&n->link, &n->book, build_agreement(n), &n->us, lseed, 1000, 8);
    /* U12: the same agreement decides which AIPI calls the node takes (one
     * compute unit each, from the same usage table); DHT requests stay at
     * the node's degree ROUTE */
    vna_node_set_agreement(&n->node, n->link.agr, &n->us, NULL, NULL);
    memset(lseed, 0, sizeof lseed);
    vna_link_on_done(&n->link, n->trade_fn, n->trade_ctx);
    vna_node_set_rcpt_handler(&n->node, on_rcpt, n);
    size_t obcap = (size_t) (VNA_OUTBOX_MAX + 1u) * (VNA_MSG_WIRE_MAX + VNA_XFORM_MAX_OVERHEAD);
    if (!(n->obuf = (uint8_t *) malloc(obcap))) return fail(n, "out of memory");
    vna_outbox_init(&n->ob, n->obuf, (uint32_t) obcap);

    /* U3: the socket */
    n->s = socket(AF_INET, SOCK_DGRAM, 0);
    if (n->s == NBAD_SOCK) return fail(n, "socket failed");
    if (set_nonblocking(n->s) != 0) return fail(n, "could not make the socket non-blocking");
    /* U3: room for whole frames. macOS caps a UDP datagram at the send
     * buffer (9216 bytes by default), below a signed receipt; best effort. */
    int bufsz = SOCK_BUF;
    (void) setsockopt(n->s, SOL_SOCKET, SO_SNDBUF, (const char *) &bufsz, sizeof bufsz);
    (void) setsockopt(n->s, SOL_SOCKET, SO_RCVBUF, (const char *) &bufsz, sizeof bufsz);
    if (bind(n->s, (struct sockaddr *) &sa, sizeof sa) != 0) return fail(n, "port is busy");
    nlen_t al = sizeof sa;
    if (getsockname(n->s, (struct sockaddr *) &sa, &al) == 0) port = ntohs(sa.sin_port);
    n->port = port;
    snprintf(n->bind_ip, sizeof n->bind_ip, "%s", ip);
    n->mode = mode;
    n->last_tick = n->last_refresh = n->last_save = n->last_keepalive = now_ms;
    n->running = true;
    if (n->lan_want) lan_open(n, now_ms); /* U10: best effort; the node runs without it */
    load_peers(n, now_ms);                /* U9 */
    bind_aipi(n);                         /* U12 */
    return spaces_open(n);                /* U14 */
}

void zxv_net_on_readable(zxv_net_t *n, uint64_t now_ms)
{
    if (!n || n->s == NBAD_SOCK) return;
    drain_lan(n, now_ms);
    for (uint32_t i = 0; i < READ_BURST; i++) {
        struct sockaddr_in from;
        nlen_t fl = sizeof from;
        int k = (int) recvfrom(n->s, (char *) n->rbuf, (int) sizeof n->rbuf, 0,
                               (struct sockaddr *) &from, &fl);
        if (k < 0) {
#if defined(_WIN32)
            int e = WSAGetLastError();
            if (e == WSAECONNRESET || e == WSAEMSGSIZE) continue; /* ICMP echo of an old send */
#else
            if (errno == EINTR || errno == ECONNREFUSED) continue;
#endif
            return; /* would block: drained */
        }
        uint8_t a[ADDR_LEN];
        if (fl < (nlen_t) sizeof from || from.sin_family != AF_INET) continue;
        to_addr6(&from, a);
        if (!allowed(n, a)) {
            n->policy++;
            continue;
        }
        if ((size_t) k > VNA_MSG_WIRE_MAX + VNA_XFORM_MAX_OVERHEAD) {
            n->refused++;
            continue;
        }
        n->in++;
        if (vna_node_handle(&n->node, n->rbuf, (uint32_t) k, a, ADDR_LEN, now_ms, &n->ob) != VNA_OK)
            n->refused++;
        drain_link(n, now_ms);
        flush(n);
    }
}

void zxv_net_tick(zxv_net_t *n, uint64_t now_ms)
{
    if (!n || n->s == NBAD_SOCK) return;
    if (now_ms < n->last_tick + TICK_MS) return;
    n->last_tick = now_ms;
    drain_lan(n, now_ms);
    vna_node_tick(&n->node, now_ms, &n->ob);
    if (n->lan_on) vna_lan_tick(&n->lan, now_ms, &n->ob);
    if (n->av_on) aipi_vna_tick(&n->av, now_ms);
    if (n->sp) {
        spaces_addr(n);
        vna_spaces_tick(n->sp, now_ms);
    }
    vna_link_tick(&n->link, now_ms);
    drain_link(n, now_ms);
    if (n->mode == ZXV_NET_ONLINE && now_ms >= n->last_keepalive + KEEPALIVE_MS) {
        n->last_keepalive = now_ms; /* U11: keep NAT mappings open */
        (void) vna_node_keepalive(&n->node, now_ms, KEEPALIVE_MS, 16, &n->ob);
    }
    if (now_ms >= n->last_save + SAVE_MS) {
        n->last_save = now_ms; /* U9 */
        (void) zxv_net_save_peers(n);
        if (n->lan_on && vna_lan_seq(&n->lan) > n->saved_lan_seq) (void) save_identity(n);
    }
    if (now_ms >= n->last_refresh + REFRESH_MS) {
        n->last_refresh = now_ms;
        vna_node_refresh(&n->node, now_ms, REFRESH_MS * 15u, 2, &n->ob);
    }
    flush(n);
}

static int parse_ip(const char *ip, uint8_t out[4])
{
    struct in_addr ia;
    if (!ip || inet_pton(AF_INET, ip, &ia) != 1) return -1;
    memcpy(out, &ia.s_addr, 4);
    return 0;
}

int zxv_net_add_peer(zxv_net_t *n, const char *ip, uint16_t port, uint64_t now_ms)
{
    if (!n || n->s == NBAD_SOCK || port == 0) return -1;
    uint8_t a[ADDR_LEN];
    if (parse_ip(ip, a) != 0) return -1;
    if (!allowed(n, a)) {
        n->policy++;
        return -1;
    }
    a[4] = (uint8_t) (port >> 8);
    a[5] = (uint8_t) port;
    vna_status_t st = vna_node_bootstrap(&n->node, a, ADDR_LEN, now_ms, &n->ob);
    flush(n);
    return st == VNA_OK ? 0 : -1;
}

int zxv_net_add_peer_str(zxv_net_t *n, const char *ip_port, uint64_t now_ms)
{
    char ip[16];
    if (!ip_port) return -1;
    const char *c = strrchr(ip_port, ':');
    if (!c || c == ip_port || (size_t) (c - ip_port) >= sizeof ip) return -1;
    memcpy(ip, ip_port, (size_t) (c - ip_port));
    ip[c - ip_port] = 0;
    char *e;
    long p = strtol(c + 1, &e, 10);
    if (*e || p <= 0 || p > 65535) return -1;
    return zxv_net_add_peer(n, ip, (uint16_t) p, now_ms);
}

int32_t zxv_net_find_node(zxv_net_t *n, const uint8_t id[32], uint64_t now_ms)
{
    if (!n || n->s == NBAD_SOCK || !id) return -1;
    vna_id_t t;
    memcpy(t.b, id, sizeof t.b);
    int32_t slot = vna_node_lookup(&n->node, &t, VNA_LK_FIND_NODE, now_ms, &n->ob);
    flush(n);
    return slot;
}

int zxv_net_find_result(zxv_net_t *n, int32_t slot)
{
    if (!n || n->s == NBAD_SOCK || slot < 0) return 0;
    if (!vna_node_lookup_done(&n->node, slot)) return -1;
    const vna_node_lookup_t *L = vna_node_lookup_get(&n->node, slot);
    int hit = 0;
    if (L) {
        static vna_cand_t res[VNA_KAD_K];
        uint32_t k = vna_lookup_result(&L->lk, VNA_KAD_K, res);
        for (uint32_t i = 0; i < k; i++) hit |= vna_id_eq(&res[i].id, &L->lk.target);
    }
    vna_node_lookup_release(&n->node, slot);
    return hit;
}

void zxv_net_node_id(const zxv_net_t *n, uint8_t out[32])
{
    memset(out, 0, 32);
    if (n && n->s != NBAD_SOCK) memcpy(out, n->idn.id.b, 32);
}

bool zxv_net_knows(const zxv_net_t *n, const char *ip, uint16_t port)
{
    uint8_t a[ADDR_LEN];
    if (!n || n->s == NBAD_SOCK || parse_ip(ip, a) != 0) return false;
    a[4] = (uint8_t) (port >> 8);
    a[5] = (uint8_t) port;
    static vna_contact_t c[POOL_CAP];
    uint32_t k = vna_node_contacts(&n->node, c, POOL_CAP);
    for (uint32_t i = 0; i < k; i++)
        if (c[i].addr_len == ADDR_LEN && !memcmp(c[i].addr, a, ADDR_LEN)) return true;
    return false;
}

int zxv_net_contact_id(const zxv_net_t *n, const char *ip, uint16_t port, uint8_t id[32])
{
    uint8_t a[ADDR_LEN];
    if (!n || !id || n->s == NBAD_SOCK || parse_ip(ip, a) != 0) return -1;
    a[4] = (uint8_t) (port >> 8);
    a[5] = (uint8_t) port;
    static vna_contact_t c[POOL_CAP];
    uint32_t k = vna_node_contacts(&n->node, c, POOL_CAP);
    for (uint32_t i = 0; i < k; i++)
        if (c[i].addr_len == ADDR_LEN && !memcmp(c[i].addr, a, ADDR_LEN)) {
            memcpy(id, c[i].id.b, 32);
            return 0;
        }
    return -1;
}

const vna_identity_t *zxv_net_identity(const zxv_net_t *n)
{
    return (n && n->s != NBAD_SOCK) ? &n->idn : NULL;
}

int zxv_net_econ_cycle(zxv_net_t *n, swarm_budget_t *sb, uint64_t base_rate, uint64_t now_ms,
                       uint64_t *imported)
{
    if (imported) *imported = 0;
    if (!n || !sb) return -1;
    if (n->s == NBAD_SOCK) return 0; /* U1: off, nothing to close */
    vna_usage_new_cycle(&n->us);     /* U8: the per-cycle compute cap starts again */
    (void) vna_usage_expire(&n->us, now_ms);
    vna_status_t st =
        vna_link_cycle(&n->link, &n->gate, n->owner_secret, sb, base_rate, now_ms, imported);
    drain_link(n, now_ms);
    flush(n);
    return st == VNA_OK ? 0 : -1;
}

void zxv_net_on_trade(zxv_net_t *n, vna_link_done_fn fn, void *ctx)
{
    if (!n) return;
    n->trade_fn = fn;
    n->trade_ctx = fn ? ctx : NULL;
    vna_link_on_done(&n->link, fn, ctx);
}

int zxv_net_share(zxv_net_t *n, uint64_t compute_per_cycle, uint64_t storage_bytes)
{
    if (!n) return -1;
    n->share_compute = compute_per_cycle;
    n->share_storage = storage_bytes;
    if (n->s != NBAD_SOCK) {
        n->link.agr = build_agreement(n);
        vna_node_set_agreement(&n->node, n->link.agr, &n->us, NULL, NULL);
    }
    return 0;
}

static bool tradable(uint8_t resource)
{
    return resource == VNA_RES_COMPUTE || resource == VNA_RES_STORAGE;
}

int32_t zxv_net_want(zxv_net_t *n, uint8_t resource, uint64_t units, uint64_t max_price)
{
    if (!n || n->s == NBAD_SOCK || !tradable(resource) || units == 0) return -1;
    return vna_link_want(&n->link, resource, SWARM_CAP_SYSTEM, units, max_price);
}

int zxv_net_sell(zxv_net_t *n, const uint8_t buyer[32], uint8_t resource, uint64_t units,
                 uint64_t price, uint64_t now_ms)
{
    if (!n || n->s == NBAD_SOCK || !buyer || !tradable(resource) || units == 0) return -1;
    vna_id_t b;
    memcpy(b.b, buyer, sizeof b.b);
    if (!book_learn(n, &b)) return -1; /* never heard from: no verified key */
    char hk[64];
    snprintf(hk, sizeof hk, "offer(%s, units: 1)",
             resource == VNA_RES_COMPUTE ? "compute" : "storage");
    vna_status_t st = vna_link_sell(&n->link, &b, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, resource, units,
                                    price, hk, now_ms);
    drain_link(n, now_ms);
    flush(n);
    return st == VNA_OK ? 0 : -1;
}

void zxv_net_status(const zxv_net_t *n, zxv_net_status_t *st)
{
    memset(st, 0, sizeof *st);
    if (!n) return;
    st->mode = n->mode;
    st->bound = n->s != NBAD_SOCK;
    st->port = n->port;
    snprintf(st->bind_ip, sizeof st->bind_ip, "%s", st->bound ? n->bind_ip : "");
    snprintf(st->error, sizeof st->error, "%s", n->error);
    st->datagrams_in = n->in;
    st->datagrams_out = n->out;
    st->refused_in = n->refused;
    st->policy_drops = n->policy;
    st->send_errors = n->send_err;
    st->share_compute = n->share_compute; /* U8: kept while off, too */
    st->share_storage = n->share_storage;
    if (!st->bound) return;
    st->trades = n->link.committed + n->link.confirmed;
    st->receipts_refused = n->link.refused + n->node.rcpt_refused;
    st->imported = n->link.imported;
    st->conserved = vna_book_conserved(&n->book);
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        st->node_id[2 * i] = hex[n->idn.id.b[i] >> 4];
        st->node_id[2 * i + 1] = hex[n->idn.id.b[i] & 15];
    }
    st->node_id[16] = 0;
    for (int i = 0; i < 32; i++) {
        st->node_id_full[2 * i] = hex[n->idn.id.b[i] >> 4];
        st->node_id_full[2 * i + 1] = hex[n->idn.id.b[i] & 15];
    }
    st->node_id_full[64] = 0;
    st->wants = 0;
    for (uint32_t i = 0; i < VNA_LINK_WANTS; i++) st->wants += n->link.want[i].used;
    static vna_contact_t c[POOL_CAP];
    st->peers = vna_node_contacts(&n->node, c, POOL_CAP);
    st->persisted = n->persisted;
    st->state_busy = n->shared_dir;
    st->cached_peers = n->cached_peers;
    st->lan_discovery = n->lan_on;
    if (n->lan_on) {
        st->lan_peers = vna_lan_peer_count(&n->lan, zxv_net_wall_ms());
        st->lan_announces = n->lan.sent_announce;
        st->lan_accepted = n->lan.accepted;
        st->lan_refused =
            n->lan.rej_parse + n->lan.rej_record + n->lan.rej_xform; /* not re-announcements */
    }
    uint8_t ra[VNA_ADDR_MAX], rl = 0;
    st->public_votes = vna_node_reflexive(&n->node, ra, &rl);
    if (st->public_votes >= 2 && rl == ADDR_LEN)
        snprintf(st->public_addr, sizeof st->public_addr, "%u.%u.%u.%u:%u", ra[0], ra[1], ra[2],
                 ra[3], (unsigned) (ra[4] << 8 | ra[5]));
    st->punch_asked = n->node.punch_asked;
    st->punch_relayed = n->node.punch_relayed;
    st->punch_ok = n->node.punch_ok;
    st->aipi = n->av_on;
    st->aipi_budget = n->aipi_budget;
    st->aipi_escrow = n->aipi_escrow;
    if (n->av_on) {
        st->aipi_calls_out = n->av.calls_out;
        st->aipi_served = n->av.served;
        st->aipi_claims_in = n->av.claims_in;
        st->aipi_claims_refused = n->av.claims_refused;
    }
    /* U14 */
    st->space_store = n->node.space_store_cap;
    if (n->sp) {
        for (uint32_t i = 0; i < VNA_SPACE_MAX; i++) {
            const vna_space_t *sp = &n->sp->s[i];
            if (!sp->used) continue;
            if (sp->phase == VNA_SPACE_IN)
                st->spaces++;
            else if (sp->phase != VNA_SPACE_NOT_FOUND)
                st->spaces_joining++;
        }
        st->space_events_in = n->sp->events_in;
        st->space_events_out = n->sp->events_out;
        st->space_events_refused = n->sp->events_refused;
    }
    uint64_t now = zxv_net_wall_ms();
    for (uint32_t i = 0; i < STORE_CAP; i++)
        st->space_records += n->store[i].used && n->store[i].rtype == VNA_REC_SPACE &&
                             n->store[i].expires > now;
}

/* ----- U13: listings ----- */
static void hex_id(const uint8_t b[32], char out[65])
{
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[2 * i] = hex[b[i] >> 4];
        out[2 * i + 1] = hex[b[i] & 15];
    }
    out[64] = 0;
}

static uint32_t pending_with(const zxv_net_t *n, const vna_id_t *id)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < VNA_LINK_PEND; i++)
        k += n->link.pend[i].used && vna_id_eq(&n->link.pend[i].peer, id);
    return k;
}

uint32_t zxv_net_peers(const zxv_net_t *n, zxv_net_peer_t *out, uint32_t cap)
{
    if (!n || n->s == NBAD_SOCK) return 0;
    static vna_contact_t c[POOL_CAP];
    uint32_t k = vna_node_contacts(&n->node, c, POOL_CAP);
    for (uint32_t i = 0; i < k && i < cap && out; i++) {
        zxv_net_peer_t *p = &out[i];
        memset(p, 0, sizeof *p);
        hex_id(c[i].id.b, p->id);
        if (c[i].addr_len == ADDR_LEN)
            snprintf(p->addr, sizeof p->addr, "%u.%u.%u.%u:%u", c[i].addr[0], c[i].addr[1],
                     c[i].addr[2], c[i].addr[3], (unsigned) (c[i].addr[4] << 8 | c[i].addr[5]));
        p->bucket = c[i].bucket;
        p->ubh = (c[i].flags & VNA_CF_UBH) != 0;
        p->last_seen_ms = c[i].last_seen_ms;
        for (uint32_t a = 0; a < BOOK_CAP; a++) {
            const vna_account_t *ac = &n->acct[a];
            if (!ac->used || !vna_id_eq(&ac->id, &c[i].id)) continue;
            p->in_book = true;
            p->member = ac->member;
            p->social = ac->social;
            for (uint32_t f = 0; f < VNA_FORMS; f++) {
                p->held += ac->held[f];
                p->receivable += ac->receivable[f];
            }
            break;
        }
        p->pending = pending_with(n, &c[i].id);
    }
    return k;
}

uint32_t zxv_net_wants(const zxv_net_t *n, zxv_net_want_info_t *out, uint32_t cap)
{
    if (!n || n->s == NBAD_SOCK) return 0;
    uint32_t k = 0;
    for (uint32_t i = 0; i < VNA_LINK_WANTS; i++) {
        const vna_link_want_t *w = &n->link.want[i];
        if (!w->used) continue;
        if (out && k < cap)
            out[k] = (zxv_net_want_info_t){i, w->resource, w->units_left, w->max_price};
        k++;
    }
    return k;
}

uint32_t zxv_net_pending(const zxv_net_t *n, zxv_net_pend_info_t *out, uint32_t cap)
{
    if (!n || n->s == NBAD_SOCK) return 0;
    uint32_t k = 0;
    for (uint32_t i = 0; i < VNA_LINK_PEND; i++) {
        const vna_link_pend_t *p = &n->link.pend[i];
        if (!p->used) continue;
        if (out && k < cap) {
            zxv_net_pend_info_t *o = &out[k];
            memset(o, 0, sizeof *o);
            o->selling = p->role == VNA_LINK_SELL;
            hex_id(p->peer.b, o->peer);
            o->units = p->units;
            o->pair_seq = p->pair_seq;
            o->tries = p->tries;
            o->want = o->selling ? -1 : p->want;
        }
        k++;
    }
    return k;
}
