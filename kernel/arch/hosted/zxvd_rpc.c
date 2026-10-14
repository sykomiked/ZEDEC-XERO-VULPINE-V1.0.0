/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_rpc.c — zxvd's JSON-RPC 2.0 control socket (see zxvd_rpc.h). */
#if !defined(_WIN32)
#    define _DEFAULT_SOURCE
#    define _DARWIN_C_SOURCE
#    if defined(__linux__)
#        define _GNU_SOURCE /* struct ucred */
#    endif
#    include <sys/types.h>
#    include <sys/socket.h>
#    include <sys/select.h>
#    include <sys/stat.h>
#    include <sys/file.h>
#    include <sys/un.h>
#    include <unistd.h>
#    include <fcntl.h>
#    include <errno.h>
#endif
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#    include <io.h>
#    include <fcntl.h>
#    include <sys/stat.h>
#endif

#include "zxvd_rpc.h"
#include "sha256.h"    /* the books file's hash chain */
#include "web4_web2.h" /* the bounded RFC 8259 tokenizer */
#include "web4_util.h" /* hex */
#include "zxv_api.h"   /* libzxv: the frozen C ABI (never zxv_model_*: see below) */
#include "zxvd_rpc_modules.h" /* freight, harmonic, stream, ehop, web4 */

/* zxv_host.c has its own zxv_model_open/zxv_model_info (zxv_model_host.h)
 * with other signatures; the executable's definitions win over libzxv's
 * exported ones, so this file must never call libzxv's zxv_model_* forms. */

#define MAX_TOK  512
#define PROTOCOL "zxvd-rpc/1"

/* JSON-RPC 2.0 error codes */
#define E_PARSE    (-32700)
#define E_REQUEST  (-32600)
#define E_METHOD   (-32601)
#define E_PARAMS   (-32602)
#define E_INTERNAL (-32603)
#define E_LIBZXV   (-32000) /* a libzxv status: data = {status, name, class} */

/* ===================== output buffer ===================== */

static void grow(zxvd_buf_t *b, size_t need)
{
    if (b->oom || b->len + need + 1 <= b->cap) return;
    size_t nc = (b->cap ? b->cap * 2 : 256) + need + 1;
    char *np = realloc(b->p, nc);
    if (!np) {
        b->oom = true;
        return;
    }
    b->p = np;
    b->cap = nc;
}

void zxvd_buf_raw(zxvd_buf_t *b, const char *s, size_t n)
{
    grow(b, n);
    if (b->oom) return;
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
}

void zxvd_buf_printf(zxvd_buf_t *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    grow(b, (size_t) n);
    if (b->oom) return;
    va_start(ap, fmt);
    vsnprintf(b->p + b->len, (size_t) n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t) n;
}

void zxvd_buf_json_str(zxvd_buf_t *b, const char *s)
{
    zxvd_buf_raw(b, "\"", 1);
    for (; *s; s++) {
        unsigned char c = (unsigned char) *s;
        if (c == '"' || c == '\\') {
            char e[2] = {'\\', (char) c};
            zxvd_buf_raw(b, e, 2);
        } else if (c < 0x20) {
            zxvd_buf_printf(b, "\\u%04x", c);
        } else {
            zxvd_buf_raw(b, s, 1);
        }
    }
    zxvd_buf_raw(b, "\"", 1);
}

/* ===================== state ===================== */

static const zxvd_host_ops_t *OPS;
static zxv_ctx *CTX;
static zxv_cap *CAP; /* LEDGER_READ | LEDGER_POST, never more (zxvd_rpc.h M2) */

void zxvd_rpc_set_ops(const zxvd_host_ops_t *ops)
{
    OPS = ops;
}

static int earn_open(void);
static void mesh_open(void);
static int bf_load(void);
static void bf_close(void);
static void books_close(void);

int zxvd_rpc_books_open(void)
{
    uint32_t ma = 0, mi = 0, pa = 0;
    if (CTX) return 0;
    /* the methods here need ABI 1.3 (zxvd_rpc.h M2): refuse an older library */
    if (zxv_version(&ma, &mi, &pa) != ZXV_OK || ma != 1 || mi < 3) return -1;
    if (zxv_ctx_create(&CTX) != ZXV_OK) return -1;
    if (zxv_cap_create(CTX, 1, NULL, ZXV_CAP_LEDGER_READ | ZXV_CAP_LEDGER_POST, &CAP) != ZXV_OK) {
        zxv_ctx_destroy(CTX);
        CTX = NULL;
        return -1;
    }
    if (bf_load() != 0 || earn_open() != 0) {
        books_close();
        return -1;
    }
    mesh_open();
    return 0;
}

static void books_close(void)
{
    bf_close();
    if (CAP) zxv_cap_destroy(CAP);
    if (CTX) zxv_ctx_destroy(CTX);
    CAP = NULL;
    CTX = NULL;
}

/* ===================== the books file (zxvd_rpc.h F1-F6) ===================== */

#if defined(_WIN32)
#    define BF_OPEN(p)                                                                             \
        _open((p), _O_RDWR | _O_CREAT | _O_BINARY | _O_NOINHERIT, _S_IREAD | _S_IWRITE)
#    define BF_READ(f, b, n)  _read((f), (b), (unsigned) (n))
#    define BF_WRITE(f, b, n) _write((f), (b), (unsigned) (n))
#    define BF_SYNC(f)        _commit(f)
#    define BF_TRUNC(f, n)    _chsize_s((f), (long long) (n))
#    define BF_END(f)         _lseeki64((f), 0, SEEK_END)
#    define BF_CLOSE(f)       _close(f)
#else
#    define BF_OPEN(p)        open((p), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR)
#    define BF_READ(f, b, n)  read((f), (b), (n))
#    define BF_WRITE(f, b, n) write((f), (b), (n))
#    define BF_SYNC(f)        fsync(f)
#    define BF_TRUNC(f, n)    ftruncate((f), (off_t) (n))
#    define BF_END(f)         lseek((f), 0, SEEK_END)
#    define BF_CLOSE(f)       close(f)
#endif

#define BF_MAGIC "{\"op\":\"books\",\"v\":1}"
#define LIT(b, s)                                                                                  \
    zxvd_buf_raw((b), (s), sizeof(s) - 1) /* a string literal, no count to get wrong */
#define BF_MAX_LINE 1024u                 /* a record is far smaller; longer is damage */

static char BF_PATH[1024];  /* "" = the books live in memory only (F1) */
static int BF = -1;         /* the open file */
static uint8_t BF_HEAD[32]; /* F2: the hash of the last record */
static uint64_t BF_RECS;    /* records in the file */
static bool BF_REPLAY;      /* true while F4 replays: nothing is appended */
static bool BF_BROKEN;      /* F5: a write failed; the books are read-only */
static char BF_WHY[160];    /* the reason for the last ZXV_E_IO */

int zxvd_rpc_books_file(const char *path)
{
    if (CTX) return -1;
    if (!path || !path[0]) {
        BF_PATH[0] = 0;
        return 0;
    }
    size_t n = strlen(path);
    if (n >= sizeof BF_PATH) return -1;
    memcpy(BF_PATH, path, n + 1);
    return 0;
}

static void hex32(const uint8_t h[32], char out[65])
{
    static const char X[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[2 * i] = X[h[i] >> 4];
        out[2 * i + 1] = X[h[i] & 15];
    }
    out[64] = 0;
}

/* F2: h = SHA-256(previous h || the record's JSON) */
static void chain(const uint8_t prev[32], const char *js, size_t n, uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, prev, 32);
    sha256_update(&c, (const uint8_t *) js, n);
    sha256_final(&c, out);
}

static bool write_all(int fd, const char *p, size_t n)
{
    while (n) {
        long k = (long) BF_WRITE(fd, p, n);
        if (k <= 0) return false;
        p += k;
        n -= (size_t) k;
    }
    return true;
}

/* F3: append one record and make it durable before the call answers */
static bool bf_append(const char *js, size_t n)
{
    if (BF < 0 || BF_REPLAY) return true;
    uint8_t h[32];
    char hx[65];
    chain(BF_HEAD, js, n, h);
    hex32(h, hx);
    hx[64] = ' ';
    if (!write_all(BF, hx, 65) || !write_all(BF, js, n) || !write_all(BF, "\n", 1) ||
        BF_SYNC(BF) != 0)
        return false;
    memcpy(BF_HEAD, h, 32);
    BF_RECS++;
    return true;
}

static int32_t io_fail(const char *why)
{
    snprintf(BF_WHY, sizeof BF_WHY, "%s", why);
    return ZXV_E_IO;
}

/* F5: a change libzxv made that the file did not take. It stays in memory
 * for this run only, and nothing more is accepted until zxvd restarts. */
static int32_t saved(zxvd_buf_t *rec)
{
    bool ok = !rec->oom && bf_append(rec->p, rec->len);
    free(rec->p);
    if (ok) return ZXV_OK;
    BF_BROKEN = true;
    return io_fail("the books file could not be written: this change is not saved and the "
                   "books are read-only until zxvd restarts");
}

static bool writable(int32_t *st)
{
    BF_WHY[0] = 0;
    if (!BF_BROKEN) return true;
    *st = io_fail("the books file could not be written earlier: the books are read-only until "
                  "zxvd restarts");
    return false;
}

/* The four changes the books can take, each saved as one record (F3). */
static int32_t bk_ccy(const char *code)
{
    int32_t st;
    uint8_t mu;
    uint32_t fl;
    if (!writable(&st)) return st;
    bool had = zxv_ccy_get(CTX, code, &mu, &fl) == ZXV_OK;
    if ((st = zxv_ccy_enable_cap(CAP, code)) != ZXV_OK || had) return st;
    zxvd_buf_t r = {0};
    LIT(&r, "{\"op\":\"ccy\",\"code\":");
    zxvd_buf_json_str(&r, code);
    LIT(&r, "}");
    return saved(&r);
}

static int32_t bk_unit(const char *code, uint8_t minor)
{
    int32_t st;
    uint8_t mu;
    uint32_t fl;
    if (!writable(&st)) return st;
    bool had = zxv_ccy_get(CTX, code, &mu, &fl) == ZXV_OK;
    if ((st = zxv_ccy_register_private_cap(CAP, code, minor)) != ZXV_OK || had) return st;
    zxvd_buf_t r = {0};
    LIT(&r, "{\"op\":\"unit\",\"code\":");
    zxvd_buf_json_str(&r, code);
    zxvd_buf_printf(&r, ",\"minor\":%u}", (unsigned) minor);
    return saved(&r);
}

static int32_t bk_acct(const char *name, const char *ccy, int32_t kind, uint32_t *id)
{
    int32_t st;
    if (!writable(&st)) return st;
    if ((st = zxv_ledger_open_account_cap(CAP, name, ccy, kind, id)) != ZXV_OK) return st;
    zxvd_buf_t r = {0};
    LIT(&r, "{\"op\":\"acct\",\"name\":");
    zxvd_buf_json_str(&r, name);
    LIT(&r, ",\"ccy\":");
    zxvd_buf_json_str(&r, ccy);
    zxvd_buf_printf(&r, ",\"kind\":%d,\"id\":%u}", (int) kind, (unsigned) *id);
    return saved(&r);
}

static int32_t bk_post(uint32_t from, uint32_t to, int64_t amount, const char *ccy, const char *ref,
                       uint64_t *entry)
{
    int32_t st;
    if (!writable(&st)) return st;
    if ((st = zxv_ledger_post_cap(CAP, from, to, amount, ccy, ref, entry)) != ZXV_OK) return st;
    zxvd_buf_t r = {0};
    zxvd_buf_printf(&r, "{\"op\":\"post\",\"from\":%u,\"to\":%u,\"amount\":\"%lld\",\"ccy\":",
                    (unsigned) from, (unsigned) to, (long long) amount);
    zxvd_buf_json_str(&r, ccy);
    LIT(&r, ",\"ref\":");
    zxvd_buf_json_str(&r, ref);
    zxvd_buf_printf(&r, ",\"id\":\"%llu\"}", (unsigned long long) *entry);
    return saved(&r);
}

/* J1-J3: the jurisdiction bridge's settings, saved in the books file */
static const char *const JU_STD[] = {"",       "IFRS",  "US_GAAP", "IPSAS",
                                     "AAOIFI", "OHADA", "CAS",     "SAP_INSURER"};
#define JU_NSTD (sizeof JU_STD / sizeof JU_STD[0])
static bool JU_OPEN;    /* J2: false = closed clearing (the default) */
static uint32_t JU_IDX; /* J3: index in JU_STD; 0 = not chosen */
static char JU_CC[3];   /* J1: ISO 3166-1 alpha-2, or "" */

static int ju_std(const char *s)
{
    for (uint32_t i = 0; i < JU_NSTD; i++)
        if (!strcmp(s, JU_STD[i])) return (int) i;
    return -1;
}

static bool ju_cc(const char *s)
{
    return !s[0] || (s[0] >= 'A' && s[0] <= 'Z' && s[1] >= 'A' && s[1] <= 'Z' && !s[2]);
}

/* J4: saved first, then applied; an unchanged setting writes nothing */
static int32_t bk_juris(bool open, uint32_t idx, const char *cc)
{
    int32_t st;
    if (!writable(&st)) return st;
    if (open == JU_OPEN && idx == JU_IDX && !strcmp(cc, JU_CC)) return ZXV_OK;
    zxvd_buf_t r = {0};
    zxvd_buf_printf(&r,
                    "{\"op\":\"juris\",\"market\":\"%s\",\"standard\":\"%s\",\"country\":\"%s\"}",
                    open ? "open" : "closed", JU_STD[idx], cc);
    if ((st = saved(&r)) != ZXV_OK) return st;
    JU_OPEN = open;
    JU_IDX = idx;
    memcpy(JU_CC, cc, strlen(cc) + 1);
    return ZXV_OK;
}

/* F4: apply one replayed record; NULL, or why it does not fit the books */
static const char *bf_apply(const char *js, size_t n)
{
    static w4_jtok_t t[64];
    int32_t k = w4_json_parse(js, (uint32_t) n, t, 64, 4);
    if (k <= 0 || t[0].type != W4_J_OBJ) return "not a JSON object";
    int32_t op = w4_json_get(js, t, k, 0, "op");
    if (op < 0 || t[op].type != W4_J_STR) return "no op";
    char a[72], b[16];
    uint64_t x, y, z;
    if (BF_RECS == 0)
        return n == sizeof BF_MAGIC - 1 && !memcmp(js, BF_MAGIC, n) ? NULL
                                                                    : "not a zxvd books file";
    if (w4_json_str_eq(js, &t[op], "ccy")) {
        if (w4_json_get_str(js, t, k, 0, "code", b, sizeof b) < 0) return "bad ccy record";
        return zxv_ccy_enable_cap(CAP, b) == ZXV_OK ? NULL : "currency refused";
    }
    if (w4_json_str_eq(js, &t[op], "unit")) {
        if (w4_json_get_str(js, t, k, 0, "code", b, sizeof b) < 0 ||
            w4_json_get_u64(js, t, k, 0, "minor", &x) != W4_OK || x > 255)
            return "bad unit record";
        return zxv_ccy_register_private_cap(CAP, b, (uint8_t) x) == ZXV_OK ? NULL : "unit refused";
    }
    if (w4_json_str_eq(js, &t[op], "acct")) {
        uint32_t id;
        if (w4_json_get_str(js, t, k, 0, "name", a, sizeof a) < 0 ||
            w4_json_get_str(js, t, k, 0, "ccy", b, sizeof b) < 0 ||
            w4_json_get_u64(js, t, k, 0, "kind", &x) != W4_OK ||
            w4_json_get_u64(js, t, k, 0, "id", &y) != W4_OK || x > 1)
            return "bad account record";
        if (zxv_ledger_open_account_cap(CAP, a, b, (int32_t) x, &id) != ZXV_OK)
            return "account refused";
        return id == y ? NULL : "account number differs";
    }
    if (w4_json_str_eq(js, &t[op], "post")) {
        int32_t am = w4_json_get(js, t, k, 0, "amount"), ei = w4_json_get(js, t, k, 0, "id");
        int64_t v;
        uint64_t entry;
        if (w4_json_get_u64(js, t, k, 0, "from", &x) != W4_OK ||
            w4_json_get_u64(js, t, k, 0, "to", &y) != W4_OK || x > UINT32_MAX || y > UINT32_MAX ||
            am < 0 || t[am].type != W4_J_STR || ei < 0 || t[ei].type != W4_J_STR ||
            w4_json_get_str(js, t, k, 0, "ccy", b, sizeof b) < 0 ||
            w4_json_get_str(js, t, k, 0, "ref", a, sizeof a) < 0)
            return "bad posting record";
        /* the amount and id are decimal strings, as on the socket (M3) */
        char num[24];
        if (w4_json_str(js, &t[am], num, sizeof num) <= 0 ||
            sscanf(num, "%lld", (long long *) &v) != 1 ||
            w4_json_str(js, &t[ei], num, sizeof num) <= 0 ||
            sscanf(num, "%llu", (unsigned long long *) &z) != 1)
            return "bad posting record";
        if (zxv_ledger_post_cap(CAP, (uint32_t) x, (uint32_t) y, v, b, a, &entry) != ZXV_OK)
            return "posting refused";
        return entry == z ? NULL : "entry number differs";
    }
    if (w4_json_str_eq(js, &t[op], "juris")) {
        char m[8], c2[4];
        int ix;
        if (w4_json_get_str(js, t, k, 0, "market", m, sizeof m) < 0 ||
            w4_json_get_str(js, t, k, 0, "standard", a, sizeof a) < 0 ||
            w4_json_get_str(js, t, k, 0, "country", c2, sizeof c2) < 0 ||
            (strcmp(m, "open") && strcmp(m, "closed")) || (ix = ju_std(a)) < 0 || !ju_cc(c2))
            return "bad jurisdiction record";
        JU_OPEN = !strcmp(m, "open");
        JU_IDX = (uint32_t) ix;
        memcpy(JU_CC, c2, strlen(c2) + 1);
        return NULL;
    }
    return "unknown op";
}

static int bf_hexval(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* F4: replay the file into the fresh books, then keep it open for appends */
static int bf_load(void)
{
    BF_RECS = 0;
    BF_BROKEN = false;
    JU_OPEN = false;
    JU_IDX = 0;
    JU_CC[0] = 0;
    memset(BF_HEAD, 0, sizeof BF_HEAD);
    if (!BF_PATH[0]) return 0;
    BF = BF_OPEN(BF_PATH);
    if (BF < 0) {
        fprintf(stderr, "zxvd: cannot open the books file %s: %s\n", BF_PATH, strerror(errno));
        return -1;
    }
#if !defined(_WIN32)
    struct stat st;
    if (fstat(BF, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
        (st.st_mode & (S_IRWXG | S_IRWXO))) {
        fprintf(stderr,
                "zxvd: the books file %s must be a regular file of ours that nobody else can "
                "read or write (chmod 600)\n",
                BF_PATH);
        bf_close();
        return -1;
    }
    if (flock(BF, LOCK_EX | LOCK_NB) != 0) { /* F7 */
        fprintf(stderr, "zxvd: the books file %s is open in another zxvd\n", BF_PATH);
        bf_close();
        return -1;
    }
#endif
    char *buf = NULL;
    size_t len = 0, cap = 0;
    for (;;) {
        if (len + 65536 > cap) {
            char *nb = realloc(buf, cap = cap * 2 + 65536);
            if (!nb) break;
            buf = nb;
        }
        long k = (long) BF_READ(BF, buf + len, cap - len);
        if (k < 0) {
            len = (size_t) -1;
            break;
        }
        if (k == 0) break;
        len += (size_t) k;
    }
    int rc = 0;
    size_t off = 0, line = 0;
    const char *why = NULL;
    if (len == (size_t) -1 || (len && !buf)) {
        why = "read failed";
        len = 0;
    }
    BF_REPLAY = true;
    while (!why && off < len) {
        char *nl = memchr(buf + off, '\n', len - off);
        line++;
        if (!nl) { /* F6: a record cut short by a crash: drop it */
            fprintf(stderr, "zxvd: the books file's last record was incomplete; dropped it\n");
            if (BF_TRUNC(BF, off) != 0) why = "could not drop the incomplete last record";
            break;
        }
        size_t ll = (size_t) (nl - (buf + off));
        const char *p = buf + off;
        uint8_t want[32], got[32];
        if (ll < 66 || ll > BF_MAX_LINE || p[64] != ' ') {
            why = "malformed record";
            break;
        }
        for (int i = 0; i < 32 && !why; i++) {
            int hi = bf_hexval(p[2 * i]), lo = bf_hexval(p[2 * i + 1]);
            if (hi < 0 || lo < 0)
                why = "malformed record";
            else
                want[i] = (uint8_t) (hi << 4 | lo);
        }
        if (why) break;
        chain(BF_HEAD, p + 65, ll - 65, got);
        if (memcmp(got, want, 32)) {
            why = "the hash chain is broken (the file was changed outside zxvd)";
            break;
        }
        if ((why = bf_apply(p + 65, ll - 65)) != NULL) break;
        memcpy(BF_HEAD, got, 32);
        BF_RECS++;
        off += ll + 1;
    }
    BF_REPLAY = false;
    free(buf);
    if (!why && BF_END(BF) < 0) why = "seek failed";
    if (!why && BF_RECS == 0 && !bf_append(BF_MAGIC, sizeof BF_MAGIC - 1))
        why = "could not write the first record";
    if (why) {
        fprintf(stderr,
                "zxvd: the books file %s, record %zu: %s. zxvd will not start on damaged "
                "books; move the file aside to start with empty books.\n",
                BF_PATH, line, why);
        bf_close();
        rc = -1;
    }
    return rc;
}

static void bf_close(void)
{
    if (BF >= 0) BF_CLOSE(BF);
    BF = -1;
}

void zxvd_rpc_books_close(void)
{
    books_close();
}

/* ===================== swarm earnings (zxvd_rpc.h E1-E4) ===================== */

static char EARN_CCY[9] = "VFV"; /* E1 */
static uint64_t EARN_RATE = 1;   /* minor units per SWC */
static uint32_t EARN_SRC, EARN_OWNER;
static bool EARN_ACCTS;    /* both accounts are open */
static int64_t EARN_HELD;  /* the owner's balance, minor units */
static int64_t EARN_CARRY; /* E5: the balance the books file brought back */
static bool EARN_RESUMED;  /* E5: the accounts came back from the books file */
static bool EARN_BASED;    /* E5: this run's first sync set EARN_BASE */
static uint64_t EARN_BASE; /* E5: the SWC held at this run's first sync */
static uint64_t EARN_POSTS, EARN_SYNCS;
static uint64_t EARN_REF; /* the last "swarm earnings N" reference number used */

int zxvd_rpc_earnings_set(const char *ccy, uint64_t rate)
{
    size_t n = strlen(ccy);
    if (CTX || n < 3 || n >= sizeof EARN_CCY || rate == 0 || rate > (uint64_t) ZXV_AMOUNT_MAX)
        return -1;
    memcpy(EARN_CCY, ccy, n + 1);
    EARN_RATE = rate;
    return 0;
}

/* An account named `name` in `ccy` of `kind`, the first one; true if found. */
static bool find_account(const char *name, const char *ccy, int32_t kind, uint32_t *id)
{
    uint32_t n = 0;
    if (zxv_ledger_account_count_cap(CAP, &n) != ZXV_OK) return false;
    for (uint32_t i = 0; i < n; i++) {
        char nm[80], cc[16];
        size_t l;
        int32_t k;
        if (zxv_ledger_account_name_cap(CAP, i, nm, sizeof nm, &l) == ZXV_OK &&
            zxv_ledger_account_ccy_cap(CAP, i, cc, sizeof cc, &l) == ZXV_OK &&
            zxv_ledger_account_kind_cap(CAP, i, &k) == ZXV_OK && k == kind && !strcmp(nm, name) &&
            !strcmp(cc, ccy)) {
            *id = i;
            return true;
        }
    }
    return false;
}

static int64_t equity(uint32_t acct)
{
    int64_t eq = 0;
    zxv_ledger_balance_cap(CAP, acct, NULL, NULL, &eq);
    return eq;
}

static uint32_t entries(uint32_t acct)
{
    uint32_t n = 0;
    zxv_ledger_entry_count_cap(CAP, acct, &n);
    return n;
}

/* A currency for postings zxvd makes itself: an ISO 4217 code from the
 * table, anything else a private unit with 2 minor units (E1). */
static int32_t own_ccy(const char *code)
{
    int32_t st = bk_ccy(code);
    return st == ZXV_E_CURRENCY ? bk_unit(code, 2) : st;
}

static int earn_open(void)
{
    int32_t st = own_ccy(EARN_CCY);
    EARN_ACCTS = false;
    EARN_HELD = 0;
    EARN_CARRY = 0;
    EARN_RESUMED = false;
    EARN_BASED = false;
    EARN_BASE = 0;
    EARN_POSTS = 0;
    EARN_SYNCS = 0;
    EARN_REF = 0;
    /* E5: accounts the books file brought back carry their balance on */
    if (st == ZXV_OK && find_account("swarm market", EARN_CCY, ZXV_ACCT_ISSUER, &EARN_SRC) &&
        find_account("owner: swarm earnings", EARN_CCY, ZXV_ACCT_HOLDER, &EARN_OWNER)) {
        EARN_ACCTS = true;
        EARN_RESUMED = true;
        EARN_HELD = EARN_CARRY = equity(EARN_OWNER);
        EARN_POSTS = EARN_REF = entries(EARN_OWNER);
    }
    return st == ZXV_OK ? 0 : -1;
}

/* Post with a numbered reference "<prefix> N", skipping numbers already
 * used (a reference is unique in the books, and a resumed book has some). */
static int32_t post_numbered(uint32_t from, uint32_t to, int64_t amount, const char *ccy,
                             const char *prefix, uint64_t *counter)
{
    int32_t st = ZXV_E_DUPLICATE;
    for (int tries = 0; tries < 64 && st == ZXV_E_DUPLICATE; tries++) {
        char ref[36];
        uint64_t entry;
        snprintf(ref, sizeof ref, "%.14s %llu", prefix, (unsigned long long) *counter + 1u);
        st = bk_post(from, to, amount, ccy, ref, &entry);
        if (st == ZXV_OK || st == ZXV_E_DUPLICATE) ++*counter;
    }
    return st;
}

int zxvd_rpc_earnings_sync(uint64_t held_swc)
{
    if (!CAP) return 0; /* no books yet, nothing to mirror */
    if (held_swc > (uint64_t) ZXV_AMOUNT_MAX / EARN_RATE) return -1;
    /* E1: the accounts open with the first settled cycle */
    if (!EARN_ACCTS) {
        if (bk_acct("swarm market", EARN_CCY, ZXV_ACCT_ISSUER, &EARN_SRC) != ZXV_OK ||
            bk_acct("owner: swarm earnings", EARN_CCY, ZXV_ACCT_HOLDER, &EARN_OWNER) != ZXV_OK)
            return -1;
        EARN_ACCTS = true;
    }
    /* E5: a resumed book keeps its balance and follows this run's change */
    if (EARN_RESUMED && !EARN_BASED) {
        EARN_BASE = held_swc;
        EARN_BASED = true;
    }
    int64_t target = EARN_CARRY + ((int64_t) held_swc - (int64_t) EARN_BASE) * (int64_t) EARN_RATE;
    if (target < 0) target = 0; /* a holder account never goes below zero */
    if (target > ZXV_AMOUNT_MAX) return -1;
    int64_t d = target - EARN_HELD;
    if (d != 0) {
        int32_t st =
            d > 0 ? post_numbered(EARN_SRC, EARN_OWNER, d, EARN_CCY, "swarm earnings", &EARN_REF)
                  : post_numbered(EARN_OWNER, EARN_SRC, -d, EARN_CCY, "swarm earnings", &EARN_REF);
        if (st != ZXV_OK) return -1;
        EARN_HELD += d;
        EARN_POSTS++;
    }
    EARN_SYNCS++;
    return 0;
}

/* ===================== mesh sales and purchases (zxvd_rpc.h S1-S4) ===================== */

#define MESH_CCY "VFV" /* the peer book's unit (zxv_peer_host.h P6) */

static uint32_t MESH_PEERS, MESH_WALLET, MESH_FEES;
static bool MESH_ACCTS;
static uint64_t MESH_SOLD, MESH_BOUGHT, MESH_SKIPPED;

static void mesh_open(void)
{
    MESH_ACCTS = find_account("mesh counterparties", MESH_CCY, ZXV_ACCT_ISSUER, &MESH_PEERS) &&
                 find_account("owner: mesh wallet", MESH_CCY, ZXV_ACCT_HOLDER, &MESH_WALLET) &&
                 find_account("mesh: assurance fees paid", MESH_CCY, ZXV_ACCT_HOLDER, &MESH_FEES);
    MESH_SOLD = MESH_BOUGHT = MESH_SKIPPED = 0;
}

static int32_t mesh_post(uint32_t from, uint32_t to, uint64_t amount, const char *what,
                         const char *tag)
{
    char ref[36];
    uint64_t entry;
    snprintf(ref, sizeof ref, "mesh %.5s %.16s", what, tag);
    int32_t st = bk_post(from, to, (int64_t) amount, MESH_CCY, ref, &entry);
    return st == ZXV_E_DUPLICATE ? ZXV_OK : st; /* S3: already in the books */
}

int zxvd_rpc_mesh_post(bool sold, uint64_t gross, uint64_t fee, const uint8_t invoice[32])
{
    if (!CAP) return 0;
    if (!invoice || gross > (uint64_t) ZXV_AMOUNT_MAX || fee > (uint64_t) ZXV_AMOUNT_MAX - gross)
        return -1;
    if (gross == 0) { /* a free answer moves no money */
        MESH_SKIPPED++;
        return 0;
    }
    if (!MESH_ACCTS) { /* S1 */
        if (own_ccy(MESH_CCY) != ZXV_OK ||
            bk_acct("mesh counterparties", MESH_CCY, ZXV_ACCT_ISSUER, &MESH_PEERS) != ZXV_OK ||
            bk_acct("owner: mesh wallet", MESH_CCY, ZXV_ACCT_HOLDER, &MESH_WALLET) != ZXV_OK ||
            bk_acct("mesh: assurance fees paid", MESH_CCY, ZXV_ACCT_HOLDER, &MESH_FEES) != ZXV_OK)
            return -1;
        MESH_ACCTS = true;
    }
    char tag[65];
    hex32(invoice, tag);
    tag[16] = 0; /* S3: the invoice hash's first 64 bits name the request */
    if (sold) {  /* S2: the price comes in from the counterparties */
        if (mesh_post(MESH_PEERS, MESH_WALLET, gross, "sale", tag) != ZXV_OK) return -1;
        MESH_SOLD++;
        return 0;
    }
    /* S2: the price and the fee go out; S4: a shortfall is the demo float */
    int64_t have = equity(MESH_WALLET);
    int64_t need = (int64_t) (gross + fee);
    if (have < need &&
        mesh_post(MESH_PEERS, MESH_WALLET, (uint64_t) (need - have), "float", tag) != ZXV_OK)
        return -1;
    if (mesh_post(MESH_WALLET, MESH_PEERS, gross, "buy", tag) != ZXV_OK ||
        (fee && mesh_post(MESH_WALLET, MESH_FEES, fee, "fee", tag) != ZXV_OK))
        return -1;
    MESH_BOUGHT++;
    return 0;
}

/* ===================== one request ===================== */

typedef struct zxvd_call {
    const char *js;
    const w4_jtok_t *t;
    int32_t n;
    int32_t params;  /* token index of the params object, or -1 */
    zxvd_buf_t *res; /* the "result" value */
    int err;         /* 0, or a JSON-RPC error code */
    char msg[240];
    int32_t zst; /* libzxv status for E_LIBZXV */
} call_t;

static void vfail(call_t *c, int code, const char *fmt, va_list ap)
{
    if (c->err) return;
    c->err = code;
    vsnprintf(c->msg, sizeof c->msg, fmt, ap);
}

static void fail(call_t *c, int code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfail(c, code, fmt, ap);
    va_end(ap);
}

/* A libzxv status: true if OK, else the call fails with its name and the
 * library's own last-error detail. */
static bool zok(call_t *c, int32_t st)
{
    if (st == ZXV_OK) return true;
    char detail[160] = "";
    size_t dl = 0;
    if (st == ZXV_E_IO && BF_WHY[0])
        snprintf(detail, sizeof detail, "%s", BF_WHY);
    else
        zxv_last_error(detail, sizeof detail, &dl);
    c->zst = st;
    fail(c, E_LIBZXV, "%s%s%s", zxv_status_name(st), detail[0] ? ": " : "", detail);
    return false;
}

static int32_t param(call_t *c, const char *key)
{
    if (c->params < 0) return W4_ERR_NOTFOUND;
    return w4_json_get(c->js, c->t, c->n, c->params, key);
}

static bool p_str(call_t *c, const char *key, char *out, uint32_t cap, bool required)
{
    int32_t i = param(c, key);
    if (i < 0) {
        if (required) fail(c, E_PARAMS, "missing string parameter \"%s\"", key);
        out[0] = 0;
        return !required;
    }
    int32_t got = c->t[i].type == W4_J_STR ? w4_json_str(c->js, &c->t[i], out, cap) : -1;
    if (got < 0) {
        fail(c, E_PARAMS, "\"%s\" must be a string of at most %u bytes", key, cap - 1);
        return false;
    }
    if ((size_t) got != strlen(out)) { /* an escaped NUL would cut it short silently */
        fail(c, E_PARAMS, "\"%s\" must not contain NUL", key);
        return false;
    }
    return true;
}

static bool p_i64(call_t *c, const char *key, int64_t *v)
{
    int32_t i = param(c, key);
    if (i < 0) {
        fail(c, E_PARAMS, "missing integer parameter \"%s\"", key);
        return false;
    }
    if (c->t[i].type != W4_J_NUM || w4_json_i64(c->js, &c->t[i], v) != W4_OK) {
        fail(c, E_PARAMS, "\"%s\" must be an integer (no fraction or exponent)", key);
        return false;
    }
    return true;
}

static bool p_u32(call_t *c, const char *key, uint32_t *v)
{
    int64_t x;
    if (!p_i64(c, key, &x)) return false;
    if (x < 0 || x > (int64_t) UINT32_MAX) {
        fail(c, E_PARAMS, "\"%s\" is out of range", key);
        return false;
    }
    *v = (uint32_t) x;
    return true;
}

/* X1-X4: methods another module adds */
static const zxvd_method_t *EXT;
static uint32_t N_EXT;

void zxvd_rpc_add_methods(const zxvd_method_t *m, uint32_t n)
{
    EXT = n ? m : NULL;
    N_EXT = m ? n : 0;
}

bool zxvd_param_str(call_t *c, const char *key, char *out, uint32_t cap, bool required)
{
    return p_str(c, key, out, cap, required);
}

bool zxvd_param_bool(call_t *c, const char *key, bool *v, bool required)
{
    int32_t i = param(c, key);
    if (i < 0) {
        if (required) fail(c, E_PARAMS, "missing boolean parameter \"%s\"", key);
        return !required;
    }
    if (c->t[i].type != W4_J_TRUE && c->t[i].type != W4_J_FALSE) {
        fail(c, E_PARAMS, "\"%s\" must be true or false", key);
        return false;
    }
    *v = c->t[i].type == W4_J_TRUE;
    return true;
}

zxvd_buf_t *zxvd_result(call_t *c)
{
    return c->res;
}

void zxvd_fail(call_t *c, int code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfail(c, code, fmt, ap);
    va_end(ap);
}

/* M3: an amount is a JSON integer or a decimal string ("-12", no sign "+",
 * no spaces, no fraction): the SDKs send 64-bit values as strings. */
static bool p_amount(call_t *c, const char *key, int64_t *v)
{
    int32_t i = param(c, key);
    if (i < 0) {
        fail(c, E_PARAMS, "missing integer parameter \"%s\"", key);
        return false;
    }
    if (c->t[i].type == W4_J_NUM && w4_json_i64(c->js, &c->t[i], v) == W4_OK) return true;
    if (c->t[i].type == W4_J_STR) {
        const char *p = c->js + c->t[i].start, *e = c->js + c->t[i].end;
        bool neg = p < e && *p == '-';
        if (neg) p++;
        uint64_t x = 0;
        bool ok = p < e && e - p <= 19;
        for (; ok && p < e; p++) {
            if (*p < '0' || *p > '9')
                ok = false;
            else
                x = x * 10u + (uint64_t) (*p - '0');
        }
        if (ok && x <= (uint64_t) INT64_MAX) {
            *v = neg ? -(int64_t) x : (int64_t) x;
            return true;
        }
    }
    fail(c, E_PARAMS, "\"%s\" must be an integer or a decimal string (no fraction or exponent)",
         key);
    return false;
}

/* ----- host methods (M1) ----- */

static void m_state(call_t *c)
{
    if (!OPS || !OPS->state) return fail(c, E_METHOD, "not available");
    OPS->state(c->res);
}

static void m_ask(call_t *c)
{
    char prompt[4096];
    if (!OPS || !OPS->ask) return fail(c, E_METHOD, "not available");
    if (!p_str(c, "prompt", prompt, sizeof prompt, true)) return;
    zxvd_buf_t a = {0};
    OPS->ask(prompt, &a);
    zxvd_buf_raw(c->res, "{\"answer\":", 10);
    if (a.p)
        zxvd_buf_raw(c->res, a.p, a.len);
    else
        zxvd_buf_raw(c->res, "\"\"", 2);
    zxvd_buf_raw(c->res, "}", 1);
    free(a.p);
}

static void m_model(call_t *c)
{
    if (!OPS || !OPS->model) return fail(c, E_METHOD, "not available");
    OPS->model(c->res);
}

static void m_notes(call_t *c)
{
    if (!OPS || !OPS->notes) return fail(c, E_METHOD, "not available");
    OPS->notes(c->res);
}

static void m_notes_read(call_t *c)
{
    if (!OPS || !OPS->notes_read) return fail(c, E_METHOD, "not available");
    zxvd_buf_printf(c->res, "{\"marked\":%u}", (unsigned) OPS->notes_read());
}

static void m_net(call_t *c)
{
    char cmd[256];
    if (!OPS || !OPS->net) return fail(c, E_METHOD, "not available");
    if (!p_str(c, "command", cmd, sizeof cmd, true)) return;
    const char *r = OPS->net(cmd);
    if (!r) return fail(c, E_PARAMS, "command must be off, lan, online, peer, share, want or sell");
    zxvd_buf_raw(c->res, "{\"result\":", 10);
    zxvd_buf_json_str(c->res, r);
    zxvd_buf_raw(c->res, "}", 1);
}

/* ----- peer.* (zxv_peer_host.h) ----- */

static const zxvd_peer_ops_t *PEER;

void zxvd_rpc_set_peer_ops(const zxvd_peer_ops_t *ops)
{
    PEER = ops;
}

static bool p_price(call_t *c, const char *key, uint64_t *v)
{
    int64_t x;
    if (!p_amount(c, key, &x)) return false;
    if (x < 0) {
        fail(c, E_PARAMS, "\"%s\" must not be negative", key);
        return false;
    }
    *v = (uint64_t) x;
    return true;
}

static void m_peer_ask(call_t *c)
{
    char peer[64], prompt[4097];
    uint64_t max_price;
    if (!PEER || !PEER->ask) return fail(c, E_METHOD, "not available");
    if (!p_str(c, "peer", peer, sizeof peer, true) ||
        !p_str(c, "prompt", prompt, sizeof prompt, true) ||
        !p_price(c, "max_price_minor", &max_price))
        return;
    const char *why = PEER->ask(peer, prompt, max_price, c->res);
    if (why) {
        c->res->len = 0;
        fail(c, ZXVD_E_PEER, "%s", why);
    }
}

static void m_peer_serve(call_t *c)
{
    uint64_t price = 0;
    if (!PEER || !PEER->serve) return fail(c, E_METHOD, "not available");
    bool on = param(c, "price_minor") >= 0;
    if (on && !p_price(c, "price_minor", &price)) return;
    const char *why = PEER->serve(on, price, c->res);
    if (why) {
        c->res->len = 0;
        fail(c, ZXVD_E_PEER, "%s", why);
    }
}

static void m_peer_status(call_t *c)
{
    if (!PEER || !PEER->status) return fail(c, E_METHOD, "not available");
    PEER->status(c->res);
}

/* ----- aipi.* (zxvd_aipi.c) ----- */

static const zxvd_aipi_ops_t *AIPI;

void zxvd_rpc_set_aipi_ops(const zxvd_aipi_ops_t *ops)
{
    AIPI = ops;
}

/* an optional boolean; absent = def */
static bool p_bool(call_t *c, const char *key, bool def, bool *v)
{
    int32_t i = param(c, key);
    *v = def;
    if (i < 0) return true;
    if (w4_json_bool(&c->t[i], v) != W4_OK) {
        fail(c, E_PARAMS, "\"%s\" must be true or false", key);
        return false;
    }
    return true;
}

static bool aipi_done(call_t *c, const char *why)
{
    if (!why) return true;
    c->res->len = 0;
    fail(c, ZXVD_E_AIPI, "%s", why);
    return false;
}

static void m_aipi_put(call_t *c)
{
    static char hex[2u * ZXVD_AIPI_HEX_MAX + 1u];
    static uint8_t data[ZXVD_AIPI_HEX_MAX];
    char path[1024];
    bool priv;
    if (!AIPI || !AIPI->put) return fail(c, E_METHOD, "not available");
    bool has_hex = param(c, "hex") >= 0, has_path = param(c, "path") >= 0;
    if (has_hex == has_path) return fail(c, E_PARAMS, "give exactly one of \"hex\" and \"path\"");
    if (!p_bool(c, "private", false, &priv)) return;
    if (has_path) {
        if (!p_str(c, "path", path, sizeof path, true)) return;
        if (!path[0]) return fail(c, E_PARAMS, "\"path\" must not be empty");
        (void) aipi_done(c, AIPI->put(NULL, 0, path, priv, c->res));
        return;
    }
    if (!p_str(c, "hex", hex, sizeof hex, true)) return;
    int32_t n = w4_hex_decode(hex, (uint32_t) strlen(hex), data, sizeof data);
    if (n < 0) return fail(c, E_PARAMS, "\"hex\" must be an even number of hex digits");
    (void) aipi_done(c, AIPI->put(data, (uint32_t) n, NULL, priv, c->res));
}

static void m_aipi_get(call_t *c)
{
    char cid[128];
    if (!AIPI || !AIPI->get) return fail(c, E_METHOD, "not available");
    if (!p_str(c, "cid", cid, sizeof cid, true)) return;
    (void) aipi_done(c, AIPI->get(cid, c->res));
}

static void m_aipi_publish(call_t *c)
{
    static const char *const VERIFY[] = {"checker", "cert", "quorum", "local"};
    zxvd_aipi_fn_t fn;
    char verify[16];
    uint32_t i;
    memset(&fn, 0, sizeof fn);
    if (!AIPI || !AIPI->publish) return fail(c, E_METHOD, "not available");
    if (!p_str(c, "module", fn.module, sizeof fn.module, true) ||
        !p_str(c, "contract", fn.contract, sizeof fn.contract, false) ||
        !p_str(c, "entry", fn.entry, sizeof fn.entry, true) ||
        !p_str(c, "checker", fn.checker, sizeof fn.checker, false) ||
        !p_str(c, "certifier", fn.certifier, sizeof fn.certifier, false) ||
        !p_str(c, "verify", verify, sizeof verify, true) ||
        !p_bool(c, "local_only", false, &fn.local_only) ||
        !p_bool(c, "deterministic", true, &fn.deterministic))
        return;
    for (i = 0; i < 4u && strcmp(verify, VERIFY[i]) != 0; i++) {
    }
    if (i == 4u)
        return fail(c, E_PARAMS,
                    "\"verify\" must be \"checker\", \"cert\", \"quorum\" or \"local\"");
    fn.verify = (uint8_t) (i + 1u); /* AIPI_VERIFY_CHECKER .. AIPI_VERIFY_LOCAL */
    if (!p_price(c, "fuel_max", &fn.fuel_max)) return;
    (void) aipi_done(c, AIPI->publish(&fn, c->res));
}

static void m_aipi_execute(call_t *c)
{
    char fnc[128];
    int64_t args[ZXVD_AIPI_ARGS_MAX];
    bool priv;
    if (!AIPI || !AIPI->execute) return fail(c, E_METHOD, "not available");
    if (!p_str(c, "function", fnc, sizeof fnc, true) || !p_bool(c, "private", false, &priv)) return;
    int32_t a = param(c, "args");
    if (a < 0 || c->t[a].type != W4_J_ARR || c->t[a].size > ZXVD_AIPI_ARGS_MAX)
        return fail(c, E_PARAMS, "\"args\" must be an array of at most %u integers",
                    ZXVD_AIPI_ARGS_MAX);
    uint32_t n = c->t[a].size;
    for (uint32_t k = 0; k < n; k++) {
        int32_t e = w4_json_at(c->t, c->n, a, k);
        if (e < 0 || c->t[e].type != W4_J_NUM || w4_json_i64(c->js, &c->t[e], &args[k]) != W4_OK)
            return fail(c, E_PARAMS,
                        "\"args\" must hold 64-bit integers (no fraction or exponent)");
    }
    (void) aipi_done(c, AIPI->execute(fnc, args, n, priv, c->res));
}

static void m_aipi_status(call_t *c)
{
    if (!AIPI || !AIPI->status) return fail(c, E_METHOD, "not available");
    AIPI->status(c->res);
}

static void m_update(call_t *c)
{
    if (!OPS || !OPS->update_check) return fail(c, E_METHOD, "not available");
    OPS->update_check(c->res);
}

static void m_shutdown(call_t *c)
{
    if (!OPS || !OPS->shutdown) return fail(c, E_METHOD, "not available");
    OPS->shutdown();
    zxvd_buf_raw(c->res, "{\"stopping\":true}", 17);
}

static void m_version(call_t *c)
{
    uint32_t ma = 0, mi = 0, pa = 0, feat = 0;
    zxv_version(&ma, &mi, &pa);
    zxv_features(&feat);
    zxvd_buf_raw(c->res, "{\"app\":", 7);
    zxvd_buf_json_str(c->res, OPS && OPS->app_version ? OPS->app_version : "unknown");
    zxvd_buf_printf(
        c->res, ",\"protocol\":\"" PROTOCOL "\",\"libzxv\":\"%u.%u.%u\",\"libzxv_features\":%u}",
        (unsigned) ma, (unsigned) mi, (unsigned) pa, (unsigned) feat);
}

/* ----- book methods (M2), all through CAP ----- */

static bool books(call_t *c)
{
    if (CAP) return true;
    fail(c, E_INTERNAL, "the books are not open");
    return false;
}

static void m_ccy_enable(call_t *c)
{
    char code[16];
    if (!books(c) || !p_str(c, "code", code, sizeof code, true)) return;
    if (!zok(c, bk_ccy(code))) return;
    zxvd_buf_raw(c->res, "{\"enabled\":", 11);
    zxvd_buf_json_str(c->res, code);
    zxvd_buf_raw(c->res, "}", 1);
}

static void m_open_account(call_t *c)
{
    char name[64], ccy[16], kind[16];
    uint32_t id;
    if (!books(c) || !p_str(c, "name", name, sizeof name, true) ||
        !p_str(c, "currency", ccy, sizeof ccy, true) || !p_str(c, "kind", kind, sizeof kind, false))
        return;
    int32_t k;
    if (!kind[0] || !strcmp(kind, "holder"))
        k = ZXV_ACCT_HOLDER;
    else if (!strcmp(kind, "issuer"))
        k = ZXV_ACCT_ISSUER;
    else
        return fail(c, E_PARAMS, "kind must be \"holder\" or \"issuer\"");
    if (!zok(c, bk_acct(name, ccy, k, &id))) return;
    zxvd_buf_printf(c->res, "{\"account\":%u}", (unsigned) id);
}

static void m_post(call_t *c)
{
    uint32_t from, to;
    int64_t amount;
    char ccy[16], ref[40];
    uint64_t entry;
    if (!books(c) || !p_u32(c, "from", &from) || !p_u32(c, "to", &to) ||
        !p_amount(c, "amount_minor", &amount) || !p_str(c, "currency", ccy, sizeof ccy, true) ||
        !p_str(c, "reference", ref, sizeof ref, true))
        return;
    if (!zok(c, bk_post(from, to, amount, ccy, ref, &entry))) return;
    zxvd_buf_printf(c->res, "{\"entry_id\":\"%llu\"}", (unsigned long long) entry);
}

static void m_balance(call_t *c)
{
    uint32_t acct;
    int64_t d, cr, eq;
    if (!books(c) || !p_u32(c, "account", &acct)) return;
    if (!zok(c, zxv_ledger_balance_cap(CAP, acct, &d, &cr, &eq))) return;
    zxvd_buf_printf(
        c->res, "{\"debit_minor\":\"%lld\",\"credit_minor\":\"%lld\",\"equity_minor\":\"%lld\"}",
        (long long) d, (long long) cr, (long long) eq);
}

static void m_accounts(call_t *c)
{
    uint32_t n;
    if (!books(c) || !zok(c, zxv_ledger_account_count_cap(CAP, &n))) return;
    zxvd_buf_raw(c->res, "{\"accounts\":[", 13);
    for (uint32_t i = 0; i < n; i++) {
        char name[80], ccy[16];
        size_t l;
        int32_t kind;
        if (!zok(c, zxv_ledger_account_name_cap(CAP, i, name, sizeof name, &l)) ||
            !zok(c, zxv_ledger_account_ccy_cap(CAP, i, ccy, sizeof ccy, &l)) ||
            !zok(c, zxv_ledger_account_kind_cap(CAP, i, &kind)))
            return;
        zxvd_buf_printf(c->res, "%s{\"account\":%u,\"name\":", i ? "," : "", (unsigned) i);
        zxvd_buf_json_str(c->res, name);
        zxvd_buf_raw(c->res, ",\"currency\":", 12);
        zxvd_buf_json_str(c->res, ccy);
        zxvd_buf_printf(c->res, ",\"kind\":\"%s\"}", kind == ZXV_ACCT_ISSUER ? "issuer" : "holder");
    }
    zxvd_buf_raw(c->res, "]}", 2);
}

static void m_entries(call_t *c)
{
    uint32_t acct, n;
    if (!books(c) || !p_u32(c, "account", &acct) ||
        !zok(c, zxv_ledger_entry_count_cap(CAP, acct, &n)))
        return;
    zxvd_buf_raw(c->res, "{\"entries\":[", 12);
    for (uint32_t i = 0; i < n; i++) {
        uint64_t id;
        int64_t amt;
        uint32_t cp;
        char ref[64];
        size_t rl;
        if (!zok(c, zxv_ledger_entry_at_cap(CAP, acct, i, &id, &amt, &cp, ref, sizeof ref, &rl)))
            return;
        zxvd_buf_printf(c->res,
                        "%s{\"entry_id\":\"%llu\",\"signed_minor\":\"%lld\",\"counterparty\":%u,"
                        "\"reference\":",
                        i ? "," : "", (unsigned long long) id, (long long) amt, (unsigned) cp);
        zxvd_buf_json_str(c->res, ref);
        zxvd_buf_raw(c->res, "}", 1);
    }
    zxvd_buf_raw(c->res, "]}", 2);
}

static void m_earnings(call_t *c)
{
    if (!books(c)) return;
    zxvd_buf_raw(c->res, "{\"currency\":", 12);
    zxvd_buf_json_str(c->res, EARN_CCY);
    zxvd_buf_printf(c->res, ",\"rate_minor\":\"%llu\"", (unsigned long long) EARN_RATE);
    if (EARN_ACCTS)
        zxvd_buf_printf(c->res, ",\"source_account\":%u,\"owner_account\":%u", (unsigned) EARN_SRC,
                        (unsigned) EARN_OWNER);
    else
        zxvd_buf_raw(c->res, ",\"source_account\":null,\"owner_account\":null", 43);
    zxvd_buf_printf(c->res,
                    ",\"balance_minor\":\"%lld\",\"postings\":\"%llu\",\"cycles\":\"%llu\","
                    "\"resumed\":%s}",
                    (long long) EARN_HELD, (unsigned long long) EARN_POSTS,
                    (unsigned long long) EARN_SYNCS, EARN_RESUMED ? "true" : "false");
}

static void m_mesh(call_t *c)
{
    if (!books(c)) return;
    zxvd_buf_printf(c->res, "{\"currency\":\"%s\"", MESH_CCY);
    if (MESH_ACCTS)
        zxvd_buf_printf(c->res,
                        ",\"wallet_account\":%u,\"counterparty_account\":%u,\"fee_account\":%u,"
                        "\"wallet_minor\":\"%lld\",\"fees_minor\":\"%lld\"",
                        (unsigned) MESH_WALLET, (unsigned) MESH_PEERS, (unsigned) MESH_FEES,
                        (long long) equity(MESH_WALLET), (long long) equity(MESH_FEES));
    else
        zxvd_buf_printf(c->res, "%s",
                        ",\"wallet_account\":null,\"counterparty_account\":null,"
                        "\"fee_account\":null,\"wallet_minor\":\"0\",\"fees_minor\":\"0\"");
    zxvd_buf_printf(c->res, ",\"sold\":\"%llu\",\"bought\":\"%llu\"}",
                    (unsigned long long) MESH_SOLD, (unsigned long long) MESH_BOUGHT);
}

static void m_books(call_t *c)
{
    if (!books(c)) return;
    LIT(c->res, "{\"saved\":");
    zxvd_buf_printf(c->res, "%s", BF >= 0 ? "true" : "false");
    LIT(c->res, ",\"path\":");
    if (BF >= 0)
        zxvd_buf_json_str(c->res, BF_PATH);
    else
        LIT(c->res, "null");
    char hx[65];
    hex32(BF_HEAD, hx);
    zxvd_buf_printf(c->res, ",\"records\":\"%llu\",\"head\":\"%s\",\"writable\":%s}",
                    (unsigned long long) BF_RECS, hx, BF_BROKEN ? "false" : "true");
}

/* ----- jurisdiction.* (J1-J5) ----- */
static void ju_put(zxvd_buf_t *o)
{
    zxvd_buf_printf(o, "{\"market\":\"%s\",\"standard\":\"%s\",\"country\":\"%s\",\"standards\":[",
                    JU_OPEN ? "open" : "closed", JU_STD[JU_IDX], JU_CC);
    for (uint32_t i = 1; i < JU_NSTD; i++)
        zxvd_buf_printf(o, "%s\"%s\"", i > 1 ? "," : "", JU_STD[i]);
    LIT(o, "],\"in_effect\":false}");
}

static void m_juris_get(call_t *c)
{
    if (books(c)) ju_put(c->res);
}

static void m_juris_set(call_t *c)
{
    char m[16], sd[16], cc[8];
    if (!books(c) || !p_str(c, "market", m, sizeof m, false) ||
        !p_str(c, "standard", sd, sizeof sd, false) || !p_str(c, "country", cc, sizeof cc, false))
        return;
    bool open = JU_OPEN;
    int ix = (int) JU_IDX;
    if (param(c, "market") >= 0) {
        if (strcmp(m, "open") && strcmp(m, "closed"))
            return fail(c, E_PARAMS, "\"market\" must be \"closed\" or \"open\"");
        open = !strcmp(m, "open");
    }
    if (param(c, "standard") >= 0 && (ix = ju_std(sd)) < 0)
        return fail(c, E_PARAMS,
                    "\"standard\" must be \"\" or one of IFRS, US_GAAP, IPSAS, "
                    "AAOIFI, OHADA, CAS, SAP_INSURER");
    if (param(c, "country") < 0)
        memcpy(cc, JU_CC, sizeof JU_CC);
    else if (!ju_cc(cc))
        return fail(c, E_PARAMS, "\"country\" must be \"\" or two capital letters (ISO 3166-1)");
    if (zok(c, bk_juris(open, (uint32_t) ix, cc))) ju_put(c->res);
}

/* ----- views from the host (V1-V3) ----- */
static const zxvd_view_ops_t *VIEWS;

void zxvd_rpc_set_view_ops(const zxvd_view_ops_t *ops)
{
    VIEWS = ops;
}

static void m_net_peers(call_t *c)
{
    if (!VIEWS || !VIEWS->peers) return fail(c, E_METHOD, "not available");
    VIEWS->peers(c->res);
}

static void m_net_offers(call_t *c)
{
    if (!VIEWS || !VIEWS->offers) return fail(c, E_METHOD, "not available");
    VIEWS->offers(c->res);
}

static void m_player_card(call_t *c)
{
    if (!VIEWS || !VIEWS->card) return fail(c, E_METHOD, "not available");
    VIEWS->card(c->res);
}

static void m_check(call_t *c)
{
    if (!books(c) || !zok(c, zxv_ledger_check_cap(CAP))) return;
    zxvd_buf_raw(c->res, "{\"ok\":true}", 11);
}

static void m_discover(call_t *c);

typedef struct {
    const char *name;
    void (*fn)(call_t *);
} method_t;

static const method_t METHODS[] = {
    {"rpc.discover", m_discover},
    {"zxvd.version", m_version},
    {"zxvd.shutdown", m_shutdown},
    {"swarm.state", m_state},
    {"swarm.ask", m_ask},
    {"model.info", m_model},
    {"notes.list", m_notes},
    {"notes.mark_read", m_notes_read},
    {"net.command", m_net},
    {"net.peers", m_net_peers},
    {"net.offers", m_net_offers},
    {"player.card", m_player_card},
    {"peer.ask", m_peer_ask},
    {"peer.serve", m_peer_serve},
    {"peer.status", m_peer_status},
    {"update.check", m_update},
    {"currency.enable", m_ccy_enable},
    {"ledger.open_account", m_open_account},
    {"ledger.post", m_post},
    {"ledger.balance", m_balance},
    {"ledger.accounts", m_accounts},
    {"ledger.entries", m_entries},
    {"ledger.check", m_check},
    {"swarm.earnings", m_earnings},
    {"mesh.books", m_mesh},
    {"books.file", m_books},
    {"jurisdiction.get", m_juris_get},
    {"jurisdiction.set", m_juris_set},
    {"aipi.put", m_aipi_put},
    {"aipi.get", m_aipi_get},
    {"aipi.publish", m_aipi_publish},
    {"aipi.execute", m_aipi_execute},
    {"aipi.status", m_aipi_status},
};
#define N_METHODS (sizeof METHODS / sizeof METHODS[0])

static void m_discover(call_t *c)
{
    zxvd_buf_raw(c->res, "{\"protocol\":\"" PROTOCOL "\",\"methods\":[",
                 sizeof("{\"protocol\":\"" PROTOCOL "\",\"methods\":[") - 1);
    for (size_t i = 0; i < N_METHODS; i++) {
        if (i) zxvd_buf_raw(c->res, ",", 1);
        zxvd_buf_json_str(c->res, METHODS[i].name);
    }
    zxvd_mod_names(c->res);
    for (uint32_t i = 0; i < N_EXT; i++) {
        zxvd_buf_raw(c->res, ",", 1);
        zxvd_buf_json_str(c->res, EXT[i].name);
    }
    zxvd_buf_raw(c->res, "]}", 2);
}

/* the id, copied verbatim (a string or an integer), or null */
static void put_id(zxvd_buf_t *out, const char *js, const w4_jtok_t *t, int32_t id)
{
    if (id < 0) {
        zxvd_buf_raw(out, "null", 4);
    } else if (t[id].type == W4_J_STR) {
        zxvd_buf_raw(out, "\"", 1);
        zxvd_buf_raw(out, js + t[id].start, t[id].end - t[id].start);
        zxvd_buf_raw(out, "\"", 1);
    } else {
        zxvd_buf_raw(out, js + t[id].start, t[id].end - t[id].start);
    }
}

static void put_error(zxvd_buf_t *out, const char *js, const w4_jtok_t *t, int32_t id, int code,
                      const char *msg, int32_t zst)
{
    zxvd_buf_raw(out, "{\"jsonrpc\":\"2.0\",\"id\":", 22);
    put_id(out, js, t, id);
    zxvd_buf_printf(out, ",\"error\":{\"code\":%d,\"message\":", code);
    zxvd_buf_json_str(out, msg);
    if (code == E_LIBZXV) {
        static const char *const CLASS[] = {"OK", "USAGE", "REFUSED", "FATAL", "SYSTEM"};
        int32_t k = zxv_status_class(zst);
        zxvd_buf_printf(out, ",\"data\":{\"status\":%d,\"name\":", (int) zst);
        zxvd_buf_json_str(out, zxv_status_name(zst));
        zxvd_buf_printf(out, ",\"class\":\"%s\"}", k >= 0 && k <= 4 ? CLASS[k] : "SYSTEM");
    }
    zxvd_buf_raw(out, "}}", 2);
}

bool zxvd_rpc_handle_line(const char *line, size_t len, zxvd_buf_t *out)
{
    static w4_jtok_t t[MAX_TOK];
    out->len = 0;
    if (len > ZXVD_RPC_MAX_LINE) {
        put_error(out, line, t, -1, E_REQUEST, "request too long", 0);
        return true;
    }
    int32_t n = w4_json_parse(line, (uint32_t) len, t, MAX_TOK, 16);
    if (n <= 0) {
        put_error(out, line, t, -1, n == W4_ERR_SPACE ? E_REQUEST : E_PARSE,
                  n == W4_ERR_SPACE ? "request too complex" : "parse error", 0);
        return true;
    }
    if (t[0].type != W4_J_OBJ) {
        put_error(out, line, t, -1, E_REQUEST,
                  t[0].type == W4_J_ARR ? "batches are not supported" : "not a request object", 0);
        return true;
    }
    int32_t id = w4_json_get(line, t, n, 0, "id");
    if (id == W4_ERR_PARSE) {
        put_error(out, line, t, -1, E_REQUEST, "duplicate id", 0);
        return true;
    }
    bool notify = id < 0;
    if (!notify && t[id].type != W4_J_STR && t[id].type != W4_J_NUM && t[id].type != W4_J_NULL) {
        put_error(out, line, t, -1, E_REQUEST, "id must be a string, number or null", 0);
        return true;
    }
    if (!notify && t[id].type == W4_J_NULL) id = -1; /* answered with "id":null */
    int32_t v = w4_json_get(line, t, n, 0, "jsonrpc");
    int32_t m = w4_json_get(line, t, n, 0, "method");
    int32_t p = w4_json_get(line, t, n, 0, "params");
    if (v < 0 || !w4_json_str_eq(line, &t[v], "2.0") || m < 0 || t[m].type != W4_J_STR ||
        p == W4_ERR_PARSE) {
        put_error(out, line, t, id, E_REQUEST, "invalid request", 0);
        return !notify;
    }
    if (p >= 0 && t[p].type != W4_J_OBJ) {
        put_error(out, line, t, id, E_PARAMS, "params must be an object", 0);
        return !notify;
    }
    const method_t *fn = NULL;
    for (size_t i = 0; i < N_METHODS; i++)
        if (w4_json_str_eq(line, &t[m], METHODS[i].name)) fn = &METHODS[i];
    int mod = fn ? -1 : zxvd_mod_find(line, &t[m]); /* zxvd_rpc_modules.h */
    if (!fn && mod < 0) {
    void (*ext)(call_t *) = NULL;
    for (uint32_t i = 0; !fn && i < N_EXT; i++)
        if (w4_json_str_eq(line, &t[m], EXT[i].name)) ext = EXT[i].fn;
    if (!fn && !ext) {
        put_error(out, line, t, id, E_METHOD, "method not found", 0);
        return !notify;
    }
    zxvd_buf_t res = {0};
    call_t c = {line, t, n, p, &res, 0, "", 0};
    if (fn)
        fn->fn(&c);
    else
        zxvd_mod_call(mod, line, t, n, p, &res, &c.err, c.msg, sizeof c.msg);
        ext(&c);
    if (!c.err && res.oom) fail(&c, E_INTERNAL, "out of memory");
    if (!c.err && !res.len) fail(&c, E_INTERNAL, "no result");
    if (c.err) {
        put_error(out, line, t, id, c.err, c.msg, c.zst);
    } else {
        zxvd_buf_raw(out, "{\"jsonrpc\":\"2.0\",\"id\":", 22);
        put_id(out, line, t, id);
        zxvd_buf_raw(out, ",\"result\":", 10);
        zxvd_buf_raw(out, res.p, res.len);
        zxvd_buf_raw(out, "}", 1);
    }
    free(res.p);
    return !notify;
}

/* ===================== the socket (T1-T4) ===================== */

#if defined(_WIN32)
/* Windows: the same transport on Winsock's AF_UNIX, in zxvd_rpc_win.c
 * (zxvd_rpc_books_close above closes the books there). */
#else

typedef struct {
    int fd;                         /* -1 when free */
    char in[ZXVD_RPC_MAX_LINE + 2]; /* a full-length line, its LF, and a NUL */
    size_t n;
    uint64_t last_ms;
} client_t;

static int LS = -1;
static char PATH_[sizeof(((struct sockaddr_un *) 0)->sun_path)];
static client_t CL[ZXVD_RPC_MAX_CLIENTS];

static bool same_user(int fd)
{
#    if defined(__linux__)
    struct ucred cr;
    socklen_t l = sizeof cr;
    return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cr, &l) == 0 && cr.uid == geteuid();
#    else
    uid_t u;
    gid_t g;
    return getpeereid(fd, &u, &g) == 0 && u == geteuid();
#    endif
}

/* Open the socket's folder, creating it 0700 if missing, and check that it is
 * ours and that nobody else can write in it (else they could swap the socket
 * file under us). Returns the folder's descriptor (name = the socket's file
 * name in it), or -1. Every later look at or change to the socket file goes
 * through this descriptor (fstatat, unlinkat, fchmodat), so what was checked
 * is what is changed. A bare file name is the current folder, the owner's own
 * choice, and is not checked; a file in "/" is refused (not our folder). */
static int open_dir(const char *path, const char **name)
{
    char dir[sizeof PATH_];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (!slash) {
        *name = path;
        return open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    }
    *name = path + (slash - dir) + 1;
    *slash = 0;
    (void) mkdir(dir, S_IRWXU); /* EEXIST is fine: the checks below decide */
    int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    struct stat st;
    if (fstat(dfd, &st) != 0 || st.st_uid != geteuid() || (st.st_mode & (S_IWGRP | S_IWOTH))) {
        close(dfd);
        return -1;
    }
    return dfd;
}

static int open_fail(int dfd, bool books)
{
    close(dfd);
    if (books) books_close();
    return -1;
}

int zxvd_rpc_open(const char *path, const zxvd_host_ops_t *ops)
{
    struct sockaddr_un a;
    if (!path || strlen(path) >= sizeof a.sun_path) {
        fprintf(stderr, "zxvd: --rpc path too long (max %zu bytes)\n", sizeof a.sun_path - 1);
        return -1;
    }
    const char *name;
    int dfd = open_dir(path, &name);
    if (dfd < 0) {
        fprintf(stderr,
                "zxvd: the folder of %s is not usable (missing, not ours, or writable by others)\n",
                path);
        return -1;
    }
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    memcpy(a.sun_path, path, strlen(path) + 1);
    struct stat st;
    if (fstatat(dfd, name, &st, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!S_ISSOCK(st.st_mode)) {
            fprintf(stderr, "zxvd: %s exists and is not a socket; refusing to replace it\n", path);
            return open_fail(dfd, false);
        }
        int probe = socket(AF_UNIX, SOCK_STREAM, 0);
        bool live = probe >= 0 && connect(probe, (struct sockaddr *) &a, sizeof a) == 0;
        if (probe >= 0) close(probe);
        if (live) {
            fprintf(stderr, "zxvd: another zxvd answers on %s\n", path);
            return open_fail(dfd, false);
        }
        unlinkat(dfd, name, 0); /* stale */
    }
    if (zxvd_rpc_books_open() != 0) {
        fprintf(
            stderr,
            "zxvd: the books could not be opened (a books file problem above, libzxv older than "
            "ABI 1.3, or the earnings currency was refused)\n");
        return open_fail(dfd, false);
    }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return open_fail(dfd, true);
    mode_t old = umask(S_IXUSR | S_IRWXG | S_IRWXO); /* the socket file is born 0600 */
    int b = bind(fd, (struct sockaddr *) &a, sizeof a);
    umask(old);
    if (b != 0 || fchmodat(dfd, name, S_IRUSR | S_IWUSR, 0) != 0 || listen(fd, 8) != 0) {
        fprintf(stderr, "zxvd: cannot listen on %s: %s\n", path, strerror(errno));
        close(fd);
        return open_fail(dfd, true);
    }
    close(dfd);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    LS = fd;
    snprintf(PATH_, sizeof PATH_, "%s", path);
    for (int i = 0; i < ZXVD_RPC_MAX_CLIENTS; i++) CL[i].fd = -1;
    OPS = ops;
    return 0;
}

int zxvd_rpc_fds(void *set)
{
    fd_set *s = set;
    if (LS < 0) return -1;
    int mx = LS;
    FD_SET(LS, s);
    for (int i = 0; i < ZXVD_RPC_MAX_CLIENTS; i++)
        if (CL[i].fd >= 0) {
            FD_SET(CL[i].fd, s);
            if (CL[i].fd > mx) mx = CL[i].fd;
        }
    return mx;
}

static void drop(client_t *c)
{
    close(c->fd);
    c->fd = -1;
    c->n = 0;
}

/* send all of it, or give up on this client (2 s send timeout) */
static bool send_line(client_t *c, const zxvd_buf_t *b)
{
    size_t off = 0;
    while (off < b->len) {
        ssize_t k = send(c->fd, b->p + off, b->len - off, 0);
        if (k <= 0) return false;
        off += (size_t) k;
    }
    return send(c->fd, "\n", 1, 0) == 1;
}

static void serve(client_t *c, uint64_t now_ms)
{
    ssize_t k = recv(c->fd, c->in + c->n, sizeof c->in - 1 - c->n, 0);
    if (k <= 0) {
        drop(c);
        return;
    }
    c->n += (size_t) k;
    c->last_ms = now_ms;
    zxvd_buf_t out = {0};
    for (;;) {
        char *nl = memchr(c->in, '\n', c->n);
        if (!nl) break;
        size_t ll = (size_t) (nl - c->in);
        size_t body = ll && c->in[ll - 1] == '\r' ? ll - 1 : ll;
        bool reply = body ? zxvd_rpc_handle_line(c->in, body, &out) : false;
        if (reply && (out.oom || !send_line(c, &out))) {
            drop(c);
            free(out.p);
            return;
        }
        memmove(c->in, nl + 1, c->n - ll - 1);
        c->n -= ll + 1;
    }
    free(out.p);
    if (c->n >= sizeof c->in - 1) drop(c); /* T3: line too long */
}

void zxvd_rpc_service(void *set, uint64_t now_ms)
{
    fd_set *s = set;
    if (LS < 0) return;
    for (int i = 0; i < ZXVD_RPC_MAX_CLIENTS; i++) {
        client_t *c = &CL[i];
        if (c->fd < 0) continue;
        if (FD_ISSET(c->fd, s))
            serve(c, now_ms);
        else if (now_ms - c->last_ms > ZXVD_RPC_IDLE_MS)
            drop(c); /* T4 */
    }
    if (FD_ISSET(LS, s)) {
        int fd = accept(LS, NULL, NULL);
        if (fd < 0) return;
        fcntl(fd, F_SETFD, FD_CLOEXEC);
        if (!same_user(fd)) { /* T2 */
            close(fd);
            return;
        }
        struct timeval to = {2, 0};
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof to);
        for (int i = 0; i < ZXVD_RPC_MAX_CLIENTS; i++)
            if (CL[i].fd < 0) {
                CL[i].fd = fd;
                CL[i].n = 0;
                CL[i].last_ms = now_ms;
                return;
            }
        close(fd); /* T4: full */
    }
}

void zxvd_rpc_close(void)
{
    for (int i = 0; i < ZXVD_RPC_MAX_CLIENTS; i++)
        if (CL[i].fd >= 0) drop(&CL[i]);
    if (LS >= 0) {
        close(LS);
        unlink(PATH_);
        LS = -1;
    }
    books_close();
}
#endif
