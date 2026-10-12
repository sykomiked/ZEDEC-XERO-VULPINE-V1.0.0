/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_phone.c — the phone link (see zxvd_phone.h P1-P2, L1-L4). */
#if !defined(_WIN32)
#    define _DEFAULT_SOURCE
#    define _DARWIN_C_SOURCE
#endif
#include "zxvd_phone.h"

#if defined(_WIN32)

#    include <stdio.h>

int zxvd_phone_init(const char *state_dir)
{
    (void) state_dir;
    return 0; /* phone.* stay "not available" (-32601) */
}
const char *zxvd_phone_listen(const char *bind_ip, uint16_t port)
{
    (void) bind_ip;
    (void) port;
    return "the phone link is not available on Windows yet";
}
void zxvd_phone_unlisten(void) {}
uint16_t zxvd_phone_port(void)
{
    return 0;
}
int zxvd_phone_fds(void *set)
{
    (void) set;
    return -1;
}
void zxvd_phone_service(void *set, uint64_t now_ms)
{
    (void) set;
    (void) now_ms;
}
void zxvd_phone_close(void) {}

#else

#    include <sys/types.h>
#    include <sys/socket.h>
#    include <sys/select.h>
#    include <sys/stat.h>
#    include <netinet/in.h>
#    include <netinet/tcp.h>
#    include <arpa/inet.h>
#    include <ifaddrs.h>
#    include <net/if.h>
#    include <errno.h>
#    include <fcntl.h>
#    include <stdio.h>
#    include <stdlib.h>
#    include <string.h>
#    include <unistd.h>

#    include "zxv_http_guard.h" /* zxv_os_random */
#    include "../../src/tls/x25519.h"
#    include "../../src/tls/aead.h"
#    include "../../src/tls/hkdf.h"
#    include "sha256.h"

#    if defined(MSG_NOSIGNAL)
#        define MSG_NOSIGNAL_ MSG_NOSIGNAL
#    else
#        define MSG_NOSIGNAL_ 0 /* macOS: SO_NOSIGPIPE on the socket instead */
#    endif

#    define PROTO     "zxv-phone/1"
#    define HELLO_LEN (4 + 16 + 32)
#    define REPLY_LEN (32 + 32)
#    define IN_MAX    (4 + ZXVD_RPC_MAX_LINE + 16)

typedef struct {
    uint8_t id[16];
    uint8_t key[32];
    bool post;
    bool used;
    char name[33];
} pairing_t;

typedef struct {
    int fd;      /* -1 when free */
    bool open;   /* handshake done */
    int phone;   /* index into PH */
    uint64_t start_ms, last_ms;
    uint8_t k_in[32], k_out[32];
    uint64_t seq_in, seq_out;
    uint8_t in[IN_MAX];
    size_t n;
} conn_t;

static int LS = -1;
static uint16_t PORT;
static char FILE_[600];
static pairing_t PH[ZXVD_PHONE_MAX];
static conn_t CN[ZXVD_PHONE_MAX_CONN];

/* ---------- helpers ---------- */

static void wipe(void *p, size_t n)
{
    volatile uint8_t *q = (volatile uint8_t *) p;
    while (n--) *q++ = 0;
}

static void hex(char *out, const uint8_t *b, size_t n)
{
    static const char D[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = D[b[i] >> 4];
        out[2 * i + 1] = D[b[i] & 15];
    }
    out[2 * n] = 0;
}

static bool unhex(const char *s, uint8_t *out, size_t n)
{
    if (strlen(s) != 2 * n) return false;
    for (size_t i = 0; i < 2 * n; i++) {
        char c = s[i];
        int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (v < 0) return false;
        if (i & 1)
            out[i / 2] = (uint8_t) (out[i / 2] | v);
        else
            out[i / 2] = (uint8_t) (v << 4);
    }
    return true;
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (24 - 8 * i));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

static void nonce(uint64_t seq, uint8_t out[12])
{
    memset(out, 0, 4);
    for (int i = 0; i < 8; i++) out[4 + i] = (uint8_t) (seq >> (56 - 8 * i));
}

static bool ip_private(uint32_t a) /* host order */
{
    uint8_t b0 = (uint8_t) (a >> 24), b1 = (uint8_t) (a >> 16);
    return b0 == 10 || b0 == 127 || (b0 == 172 && (b1 & 0xF0) == 16) ||
           (b0 == 192 && b1 == 168) || (b0 == 169 && b1 == 254);
}

/* a name is shown on both ends and stored on one line: printable ASCII only */
static bool name_ok(const char *s)
{
    if (!*s) return false;
    for (; *s; s++)
        if ((unsigned char) *s < 0x20 || (unsigned char) *s > 0x7e) return false;
    return true;
}

/* ---------- the pairings file (P2) ---------- */

static void load(void)
{
    memset(PH, 0, sizeof PH);
    FILE *f = fopen(FILE_, "r");
    if (!f) return;
    char line[256];
    int n = 0;
    while (n < ZXVD_PHONE_MAX && fgets(line, sizeof line, f)) {
        char idh[40], kh[80];
        int post, off = 0;
        line[strcspn(line, "\r\n")] = 0;
        if (sscanf(line, "v1 %39s %79s %d %n", idh, kh, &post, &off) != 3 || !off) continue;
        pairing_t *p = &PH[n];
        if (!unhex(idh, p->id, 16) || !unhex(kh, p->key, 32) || strlen(line + off) > 32 ||
            !name_ok(line + off)) {
            wipe(p, sizeof *p);
            continue;
        }
        snprintf(p->name, sizeof p->name, "%s", line + off);
        p->post = post == 1;
        p->used = true;
        n++;
    }
    wipe(line, sizeof line);
    fclose(f);
}

static int save(void)
{
    char tmp[sizeof FILE_ + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", FILE_);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    if (fd < 0) return -1;
    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        unlink(tmp);
        return -1;
    }
    int bad = 0;
    for (int i = 0; i < ZXVD_PHONE_MAX; i++) {
        if (!PH[i].used) continue;
        char idh[33], kh[65];
        hex(idh, PH[i].id, 16);
        hex(kh, PH[i].key, 32);
        if (fprintf(f, "v1 %s %s %d %s\n", idh, kh, PH[i].post ? 1 : 0, PH[i].name) < 0) bad = 1;
        wipe(kh, sizeof kh);
    }
    if (fflush(f) != 0 || fsync(fileno(f)) != 0) bad = 1;
    if (fclose(f) != 0) bad = 1;
    if (bad || rename(tmp, FILE_) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

/* ---------- connections ---------- */

static void drop(conn_t *c)
{
    close(c->fd);
    wipe(c, sizeof *c);
    c->fd = -1;
}

static bool send_all(int fd, const uint8_t *p, size_t n)
{
    while (n) {
        ssize_t k = send(fd, p, n, MSG_NOSIGNAL_);
        if (k <= 0) return false;
        p += k;
        n -= (size_t) k;
    }
    return true;
}

/* L2 */
static void handshake(conn_t *c)
{
    const uint8_t *h = c->in;
    if (memcmp(h, "ZXP1", 4) != 0) return drop(c);
    int who = -1;
    for (int i = 0; i < ZXVD_PHONE_MAX; i++)
        if (PH[i].used && ct_equal(PH[i].id, h + 4, 16)) who = i;
    if (who < 0) return drop(c);

    uint8_t es[32], epub[32], dh[32], th[32], ikm[64], prk[32], okm[96], reply[REPLY_LEN];
    if (zxv_os_random(es, sizeof es) != 0) return drop(c);
    x25519_public(epub, es);
    bool ok = x25519_shared(dh, es, h + 20);
    sha256_ctx_t s;
    sha256_init(&s);
    sha256_update(&s, (const uint8_t *) PROTO, sizeof PROTO - 1);
    sha256_update(&s, h + 4, 16);
    sha256_update(&s, h + 20, 32);
    sha256_update(&s, epub, 32);
    sha256_final(&s, th);
    memcpy(ikm, dh, 32);
    memcpy(ikm + 32, th, 32);
    hkdf_extract(PH[who].key, 32, ikm, sizeof ikm, prk);
    hkdf_expand(prk, (const uint8_t *) PROTO " keys", sizeof PROTO " keys" - 1, okm, sizeof okm);
    uint8_t msg[14 + 32];
    memcpy(msg, "server confirm", 14);
    memcpy(msg + 14, th, 32);
    memcpy(reply, epub, 32);
    hmac_sha256(okm + 64, 32, msg, sizeof msg, reply + 32);
    memcpy(c->k_in, okm, 32);
    memcpy(c->k_out, okm + 32, 32);
    wipe(es, sizeof es);
    wipe(dh, sizeof dh);
    wipe(ikm, sizeof ikm);
    wipe(prk, sizeof prk);
    wipe(okm, sizeof okm);
    if (!ok || !send_all(c->fd, reply, sizeof reply)) return drop(c);
    c->phone = who;
    c->open = true;
    memmove(c->in, c->in + HELLO_LEN, c->n - HELLO_LEN);
    c->n -= HELLO_LEN;
}

static bool send_frame(conn_t *c, const uint8_t *pt, size_t len)
{
    uint8_t *f = malloc(4 + len + 16);
    if (!f) return false;
    uint8_t nc[12];
    put32(f, (uint32_t) len);
    nonce(c->seq_out++, nc);
    aead_seal(c->k_out, nc, f, 4, pt, f + 4, (uint32_t) len, f + 4 + len);
    bool ok = send_all(c->fd, f, 4 + len + 16);
    free(f);
    return ok;
}

/* L3: one request frame in c->in (complete); false = the connection is gone */
static bool frame(conn_t *c, size_t len)
{
    uint8_t nc[12];
    char *pt = malloc(len + 1);
    if (!pt) {
        drop(c);
        return false;
    }
    nonce(c->seq_in++, nc);
    if (!aead_open(c->k_in, nc, c->in, 4, c->in + 4, (uint8_t *) pt, (uint32_t) len,
                   c->in + 4 + len)) {
        free(pt);
        drop(c);
        return false;
    }
    pt[len] = 0;
    zxvd_buf_t out = {0};
    zxvd_rpc_handle_phone_line(pt, len, &out, PH[c->phone].post);
    free(pt);
    bool ok = !out.oom && out.len <= ZXVD_PHONE_MAX_OUT &&
              send_frame(c, (const uint8_t *) out.p, out.len);
    free(out.p);
    if (!ok) drop(c);
    return ok;
}

static void serve(conn_t *c, uint64_t now_ms)
{
    ssize_t k = recv(c->fd, c->in + c->n, sizeof c->in - c->n, 0);
    if (k <= 0) return drop(c);
    c->n += (size_t) k;
    c->last_ms = now_ms;
    if (!c->open) {
        if (c->n < HELLO_LEN) return;
        handshake(c);
        if (c->fd < 0) return;
    }
    while (c->n >= 4) {
        size_t len = get32(c->in);
        if (len == 0 || len > ZXVD_RPC_MAX_LINE) return drop(c);
        if (c->n < 4 + len + 16) return;
        if (!frame(c, len)) return;
        memmove(c->in, c->in + 4 + len + 16, c->n - (4 + len + 16));
        c->n -= 4 + len + 16;
    }
}

/* ---------- phone.* (P1) ---------- */

/* the address a phone on this network would use: the first private IPv4
 * address that is up and not loopback, else 127.0.0.1 */
static void lan_ip(char out[INET_ADDRSTRLEN])
{
    snprintf(out, INET_ADDRSTRLEN, "127.0.0.1");
    struct ifaddrs *ifs = NULL, *i;
    if (getifaddrs(&ifs) != 0) return;
    for (i = ifs; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        if (!(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
        struct in_addr a = ((struct sockaddr_in *) i->ifa_addr)->sin_addr;
        uint32_t h = ntohl(a.s_addr);
        if (!ip_private(h) || (h >> 24) == 127) continue;
        inet_ntop(AF_INET, &a, out, INET_ADDRSTRLEN);
        break;
    }
    freeifaddrs(ifs);
}

static void url_name(zxvd_buf_t *out, const char *s)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char) *s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.')
            zxvd_buf_raw(out, s, 1);
        else
            zxvd_buf_printf(out, "%%%02X", c);
    }
}

static const char *ph_pair(const char *name, bool allow_post, zxvd_buf_t *out)
{
    if (LS < 0) return "the phone link is off; turn it on with phone.link first";
    if (!name_ok(name)) return "name must be 1..32 printable ASCII characters";
    int slot = -1;
    for (int i = 0; i < ZXVD_PHONE_MAX; i++)
        if (!PH[i].used) {
            slot = i;
            break;
        }
    if (slot < 0) return "too many phones paired; revoke one first";
    pairing_t *p = &PH[slot];
    if (zxv_os_random(p->id, 16) != 0 || zxv_os_random(p->key, 32) != 0) {
        wipe(p, sizeof *p);
        return "no randomness available";
    }
    snprintf(p->name, sizeof p->name, "%s", name);
    p->post = allow_post;
    p->used = true;
    if (save() != 0) {
        wipe(p, sizeof *p);
        return "the pairing could not be saved";
    }
    char idh[33], kh[65], ip[INET_ADDRSTRLEN];
    hex(idh, p->id, 16);
    hex(kh, p->key, 32);
    lan_ip(ip);
    zxvd_buf_t link = {0};
    zxvd_buf_printf(&link, "zxv-phone:1?h=%s&p=%u&id=%s&k=%s&n=", ip, (unsigned) PORT, idh, kh);
    url_name(&link, name);
    zxvd_buf_raw(out, "{\"phone_id\":", 12);
    zxvd_buf_json_str(out, idh);
    zxvd_buf_raw(out, ",\"name\":", 8);
    zxvd_buf_json_str(out, p->name);
    zxvd_buf_printf(out, ",\"allow_post\":%s,\"host\":", allow_post ? "true" : "false");
    zxvd_buf_json_str(out, ip);
    zxvd_buf_printf(out, ",\"port\":%u,\"link\":", (unsigned) PORT);
    zxvd_buf_json_str(out, link.p ? link.p : "");
    zxvd_buf_raw(out, "}", 1);
    if (link.p) wipe(link.p, link.len);
    free(link.p);
    wipe(kh, sizeof kh);
    return link.oom ? "out of memory" : NULL;
}

static void ph_list(zxvd_buf_t *out)
{
    zxvd_buf_printf(out, "{\"on\":%s,\"port\":%u,\"phones\":[", LS >= 0 ? "true" : "false",
                    (unsigned) (LS >= 0 ? PORT : 0));
    bool first = true;
    for (int i = 0; i < ZXVD_PHONE_MAX; i++) {
        if (!PH[i].used) continue;
        char idh[33];
        hex(idh, PH[i].id, 16);
        int conns = 0;
        for (int j = 0; j < ZXVD_PHONE_MAX_CONN; j++)
            if (CN[j].fd >= 0 && CN[j].open && CN[j].phone == i) conns++;
        zxvd_buf_raw(out, first ? "{\"phone_id\":" : ",{\"phone_id\":", first ? 12 : 13);
        first = false;
        zxvd_buf_json_str(out, idh);
        zxvd_buf_raw(out, ",\"name\":", 8);
        zxvd_buf_json_str(out, PH[i].name);
        zxvd_buf_printf(out, ",\"allow_post\":%s,\"connected\":%d}", PH[i].post ? "true" : "false",
                        conns);
    }
    zxvd_buf_raw(out, "]}", 2);
}

static const char *ph_revoke(const char *id_hex, zxvd_buf_t *out)
{
    uint8_t id[16];
    if (!unhex(id_hex, id, 16)) return "phone_id must be 32 lowercase hex digits";
    for (int i = 0; i < ZXVD_PHONE_MAX; i++) {
        if (!PH[i].used || memcmp(PH[i].id, id, 16) != 0) continue;
        pairing_t keep = PH[i];
        wipe(&PH[i], sizeof PH[i]);
        if (save() != 0) {
            PH[i] = keep;
            wipe(&keep, sizeof keep);
            return "the pairings file could not be written";
        }
        wipe(&keep, sizeof keep);
        int closed = 0;
        for (int j = 0; j < ZXVD_PHONE_MAX_CONN; j++)
            if (CN[j].fd >= 0 && CN[j].open && CN[j].phone == i) {
                drop(&CN[j]);
                closed++;
            }
        zxvd_buf_printf(out, "{\"revoked\":true,\"closed\":%d}", closed);
        return NULL;
    }
    return "no such phone";
}

/* ---------- the listener (L1, L4) ---------- */

const char *zxvd_phone_listen(const char *bind_ip, uint16_t port)
{
    static char why[160];
    if (LS >= 0) return NULL;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind_ip && inet_pton(AF_INET, bind_ip, &a.sin_addr) != 1)
        return "the bind address is not an IPv4 address";
    if (bind_ip && !ip_private(ntohl(a.sin_addr.s_addr)))
        return "the bind address is not a private address";
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    if (fd < 0) return "no socket";
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    if (bind(fd, (struct sockaddr *) &a, sizeof a) != 0 || listen(fd, 8) != 0) {
        snprintf(why, sizeof why, "cannot listen on TCP %u: %s", (unsigned) port, strerror(errno));
        close(fd);
        return why;
    }
    socklen_t al = sizeof a;
    getsockname(fd, (struct sockaddr *) &a, &al);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    LS = fd;
    PORT = ntohs(a.sin_port);
    return NULL;
}

void zxvd_phone_unlisten(void)
{
    for (int i = 0; i < ZXVD_PHONE_MAX_CONN; i++)
        if (CN[i].fd >= 0) drop(&CN[i]);
    if (LS >= 0) close(LS);
    LS = -1;
}

static const char *ph_link(bool on, int32_t port, const char *bind_ip, zxvd_buf_t *out)
{
    if (!on) {
        zxvd_phone_unlisten();
    } else {
        const char *why = zxvd_phone_listen(bind_ip, (uint16_t) (port < 0 ? ZXVD_PHONE_PORT : port));
        if (why) return why;
    }
    zxvd_buf_printf(out, "{\"on\":%s,\"port\":%u}", LS >= 0 ? "true" : "false",
                    (unsigned) (LS >= 0 ? PORT : 0));
    return NULL;
}

static const zxvd_phone_ops_t OPS = {ph_link, ph_pair, ph_list, ph_revoke};

int zxvd_phone_init(const char *state_dir)
{
    for (int i = 0; i < ZXVD_PHONE_MAX_CONN; i++) CN[i].fd = -1;
    if ((size_t) snprintf(FILE_, sizeof FILE_, "%s/phones", state_dir) >= sizeof FILE_) {
        fprintf(stderr, "zxvd: phone link: folder name too long\n");
        return -1;
    }
    load();
    zxvd_rpc_set_phone_ops(&OPS);
    return 0;
}

uint16_t zxvd_phone_port(void)
{
    return LS >= 0 ? PORT : 0;
}

int zxvd_phone_fds(void *set)
{
    fd_set *s = set;
    if (LS < 0) return -1;
    int mx = LS;
    FD_SET(LS, s);
    for (int i = 0; i < ZXVD_PHONE_MAX_CONN; i++)
        if (CN[i].fd >= 0) {
            FD_SET(CN[i].fd, s);
            if (CN[i].fd > mx) mx = CN[i].fd;
        }
    return mx;
}

void zxvd_phone_service(void *set, uint64_t now_ms)
{
    fd_set *s = set;
    if (LS < 0) return;
    for (int i = 0; i < ZXVD_PHONE_MAX_CONN; i++) {
        conn_t *c = &CN[i];
        if (c->fd < 0) continue;
        if (FD_ISSET(c->fd, s))
            serve(c, now_ms);
        else if ((!c->open && now_ms - c->start_ms > ZXVD_PHONE_HS_MS) ||
                 now_ms - c->last_ms > ZXVD_RPC_IDLE_MS)
            drop(c);
    }
    if (!FD_ISSET(LS, s)) return;
    struct sockaddr_in from;
    socklen_t fl = sizeof from;
    int fd = accept(LS, (struct sockaddr *) &from, &fl);
    if (fd < 0) return;
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    if (from.sin_family != AF_INET || !ip_private(ntohl(from.sin_addr.s_addr))) {
        close(fd);
        return;
    }
    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof yes);
#    if defined(SO_NOSIGPIPE)
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes);
#    endif
    struct timeval to = {5, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof to);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);
    for (int i = 0; i < ZXVD_PHONE_MAX_CONN; i++)
        if (CN[i].fd < 0) {
            wipe(&CN[i], sizeof CN[i]);
            CN[i].fd = fd;
            CN[i].start_ms = CN[i].last_ms = now_ms;
            return;
        }
    close(fd); /* L4: full */
}

void zxvd_phone_close(void)
{
    zxvd_phone_unlisten();
    zxvd_rpc_set_phone_ops(NULL);
    wipe(PH, sizeof PH);
}

#endif
