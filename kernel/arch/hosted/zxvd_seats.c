/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_seats.c — seats.*: the people who use this device and the login
 * after boot (kernel/src/seats), served by zxvd for the native UI.
 *
 * THE GROUP
 *   L1  Feature "seats", not regulated (no money, no identity data a law
 *       regulates). Every method is marked `always` (zxvd_ext.h X4): the
 *       login must work at boot before anyone could flip a switch, so the
 *       switch changes nothing here.
 *   L2  Roles "owner", "person", "guest"; locks "none", "pass", "passkey"
 *       (seats.h). A passphrase is stored only as its Argon2id hash with a
 *       per-seat salt; zxvd uses the module's default parameters (64 MiB,
 *       3 passes, 1 lane) and allocates the 64 MiB for each call that
 *       hashes, wiping and freeing it before the call answers.
 *   L3  seats.list never shows a hash, a salt or the KDF parameters.
 *
 * SESSIONS
 *   L4  seats.unlock answers {"ok":true,"wait_ms":0,"session":64 hex}: 32
 *       random bytes from the OS, bound to that seat's id. Sessions live in
 *       memory only (at most 64; a new one replaces the oldest), so a restart
 *       ends them all. seats.lock ends one; removing a seat ends its own.
 *   L5  seats.add (after the first seat), seats.remove, seats.set_lock and
 *       seats.set_auto act as the seat of their "session". The module
 *       decides what that seat may do: only an owner adds or removes seats
 *       or changes the auto-enter setting; a person may change their own
 *       lock (giving "old_pass" when they had a passphrase) and an owner may
 *       reset anyone's. The first seat on an empty device needs no session
 *       and becomes the owner.
 *
 * REFUSALS
 *   L6  A refusal is error -32080 whose message starts with the seats.h
 *       status name, e.g. "SEAT_ERR_DENIED: ...". An unknown or ended
 *       session is SEAT_ERR_DENIED, an unknown id SEAT_ERR_NOTFOUND.
 *   L7  The outcome of a login is a result, not an error: a wrong entry or a
 *       back-off answers {"ok":false,"status":"SEAT_ERR_WRONG" or
 *       "SEAT_ERR_WAIT","wait_ms":N}, N the milliseconds before the next try
 *       is heard (0 while tries are still free). The clock is the wall
 *       clock; a wait longer than the module's maximum (a clock set back) is
 *       cut to the maximum.
 *
 * PERSISTENCE
 *   L8  With a books file (zxvd_rpc.h F1) the roster is saved in "seats"
 *       beside it, mode 0600, written whole to "seats.tmp", flushed to disk
 *       and renamed over the old file, so a crash leaves the old roster or
 *       the new one. A change that cannot be saved is undone and answers
 *       -32603. Failed tries and back-off are saved too (best effort: if
 *       that write fails the count stays in memory). Without a books file
 *       the roster lives in memory for this run.
 *   L9  At start the file is read and checked (header, every field, unique
 *       ids and names, at least one owner, the trailer); a damaged file
 *       stops zxvd from starting. Nothing is repaired silently.
 */
#if !defined(_WIN32)
#    define _POSIX_C_SOURCE 200809L
#    include <fcntl.h>
#    include <sys/stat.h>
#    include <unistd.h>
#else
#    include <windows.h>
#    include <io.h>
#    include <fcntl.h>
#    include <sys/stat.h>
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zxvd_groups.h"
#include "zxv_http_guard.h" /* zxv_os_random */
#include "seats.h"

#define ZXVD_E_SEATS (-32080)
#define SEATS_MAX    4096u
#define SESS_MAX     64u
#define TOK_LEN      32u
#define LINE_MAX_    400u
#define FILE_MAX     (SEATS_MAX * LINE_MAX_ + 64u)
#define KDF_M_MAX    (1u << 21) /* 2 GiB: a larger saved m_kib is damage */
#define MAGIC        "zxv-seats"

typedef struct {
    bool used;
    uint8_t tok[TOK_LEN];
    uint8_t seat[16];
    uint64_t seq;
} sess_t;

static struct {
    bool ready, rng_bad;
    seat_roster_t r;
    sess_t sess[SESS_MAX];
    uint64_t seq;
    char path[1100];
} S;

static uint64_t wall_ms(void)
{
    return (uint64_t) time(NULL) * 1000u;
}

static int (*RANDOM)(uint8_t *p, size_t n) = zxv_os_random;
static uint64_t (*CLOCK)(void) = wall_ms;
static uint32_t KDF_M, KDF_T;

void zxvd_seats_set_random(int (*fn)(uint8_t *p, size_t n))
{
    RANDOM = fn ? fn : zxv_os_random;
}

void zxvd_seats_set_clock(uint64_t (*fn)(void))
{
    CLOCK = fn ? fn : wall_ms;
}

static void apply_kdf(void)
{
    id_kdf_default(&S.r.kdf);
    if (KDF_M) S.r.kdf.m_kib = KDF_M;
    if (KDF_T) S.r.kdf.t = KDF_T;
}

void zxvd_seats_set_kdf(uint32_t m_kib, uint32_t t)
{
    KDF_M = m_kib;
    KDF_T = t;
    apply_kdf();
}

static void wipe(void *p, size_t n)
{
    volatile uint8_t *b = (volatile uint8_t *) p;
    while (n--) *b++ = 0;
}

static const char *const ROLES[] = {"owner", "person", "guest"};
static const char *const LOCKS[] = {"none", "pass", "passkey"};
static const char *const NAMES[] = {"SEAT_OK",           "SEAT_ERR_ARG",    "SEAT_ERR_FULL",
                                    "SEAT_ERR_NOTFOUND", "SEAT_ERR_DENIED", "SEAT_ERR_LAST_OWNER",
                                    "SEAT_ERR_WRONG",    "SEAT_ERR_WAIT",   "SEAT_ERR_KDF",
                                    "SEAT_ERR_NAME"};
static const char *const WHY[] = {"",
                                  "bad argument (a passphrase is 4 to 256 bytes)",
                                  "the device has the most seats zxvd keeps",
                                  "no seat with that id",
                                  "this session may not do that",
                                  "the device would be left with no owner",
                                  "wrong passphrase or passkey",
                                  "too many wrong tries; wait",
                                  "Argon2id failed",
                                  "the name is empty, longer than 47 bytes or already used"};

static void refuse(zxvd_call_t *c, seat_status_t st)
{
    zxvd_fail(c, ZXVD_E_SEATS, "%s: %s", NAMES[st], WHY[st]);
}

/* ===================== the roster ===================== */

static void roster_free(void)
{
    wipe(S.r.v, (size_t) S.r.cap * sizeof(seat_t)); /* cap is 0 without an array */
    free(S.r.v);
    seat_roster_init(&S.r, NULL, 0);
}

static void seats_close(void)
{
    roster_free();
    wipe(S.sess, sizeof S.sess);
    S.seq = 0;
    S.path[0] = 0;
    S.ready = false;
}

static void ready(void)
{
    if (S.ready) return;
    seat_roster_init(&S.r, NULL, 0);
    apply_kdf();
    S.ready = true;
}

/* room for one more seat: false only when out of memory (a full roster at
 * SEATS_MAX is left for seat_add to answer SEAT_ERR_FULL) */
static bool room(void)
{
    if (S.r.n < S.r.cap || S.r.cap >= SEATS_MAX) return true;
    uint32_t cap = S.r.cap ? S.r.cap * 2u : 4u; /* 4 << k reaches SEATS_MAX exactly */
    seat_t *v = calloc(cap, sizeof(seat_t));
    if (!v) return false;
    seat_t *old = S.r.v;
    (void) seat_roster_grow(&S.r, v, cap); /* cap > n and a new array: cannot fail */
    free(old);
    return true;
}

static int32_t find_seat(const uint8_t id[16])
{
    for (uint32_t i = 0; i < S.r.n; i++)
        if (!memcmp(S.r.v[i].id, id, 16)) return (int32_t) i;
    return -1;
}

static void rng(uint8_t *out, uint32_t len, void *ctx)
{
    (void) ctx;
    if (RANDOM(out, len) != 0) S.rng_bad = true;
}

/* Argon2id memory for a call that hashes: the larger of the roster's
 * parameters and the seat's own. false after failing the call. */
static bool env_open(zxvd_call_t *c, seat_env_t *e, bool hashes, uint32_t seat_m_kib)
{
    e->rng = rng;
    e->rng_ctx = NULL;
    e->mem = NULL;
    e->mem_blocks = 0;
    if (!hashes) return true;
    uint32_t blocks = S.r.kdf.m_kib > seat_m_kib ? S.r.kdf.m_kib : seat_m_kib;
    e->mem = calloc(blocks, ID_ARGON2_BLOCK);
    if (!e->mem) {
        zxvd_fail(c, ZXVD_E_INTERNAL, "no memory for Argon2id (%u KiB)", (unsigned) blocks);
        return false;
    }
    e->mem_blocks = blocks;
    return true;
}

static void env_close(seat_env_t *e)
{
    wipe(e->mem, (size_t) e->mem_blocks * ID_ARGON2_BLOCK);
    free(e->mem);
    e->mem = NULL;
}

/* L7: a wait past the module's maximum comes from a clock set back */
static void clamp_wait(seat_t *s, uint64_t now)
{
    if (s->wait_until_ms > now + SEAT_WAIT_MAX_MS) s->wait_until_ms = now + SEAT_WAIT_MAX_MS;
}

/* ===================== the file (L8, L9) ===================== */

static const char HX[] = "0123456789abcdef";

static char *put_hex(char *o, const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        *o++ = HX[p[i] >> 4];
        *o++ = HX[p[i] & 15];
    }
    *o++ = ' ';
    return o;
}

static int hexval(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

/* exactly 2n lower-case hex digits */
static bool get_hex(const char *s, uint8_t *out, uint32_t n)
{
    if (strlen(s) != 2u * n) return false;
    for (uint32_t i = 0; i < n; i++) {
        int hi = hexval(s[2 * i]), lo = hexval(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t) (hi << 4 | lo);
    }
    return true;
}

static bool get_num(const char *s, uint64_t max, uint64_t *v)
{
    uint64_t x = 0;
    if (!*s || (s[0] == '0' && s[1])) return false;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return false;
        uint64_t d = (uint64_t) (*s - '0');
        if (d > max || x > (max - d) / 10u) return false;
        x = x * 10u + d;
    }
    *v = x;
    return true;
}

/* fields separated by single spaces: the count; the first max go in f */
static uint32_t split(char *line, char **f, uint32_t max)
{
    uint32_t n = 0;
    for (char *p = line;; n++) {
        if (n < max) f[n] = p;
        char *sp = strchr(p, ' ');
        if (!sp) return n + 1;
        *sp = 0;
        p = sp + 1;
    }
}

#if defined(_WIN32)
static int open_new(const char *p)
{
    return _open(p, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
}
static int sync_fd(FILE *f)
{
    return _commit(_fileno(f));
}
static bool replace(const char *tmp, const char *path)
{
    return MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
#    define fdopen _fdopen
#    define close  _close
#    define unlink _unlink
#else
static int open_new(const char *p)
{
    return open(p, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
}
static int sync_fd(FILE *f)
{
    return fsync(fileno(f));
}
static bool replace(const char *tmp, const char *path)
{
    return rename(tmp, path) == 0;
}
#endif

/* L8: the whole text to path.tmp, flushed, then renamed over path */
static bool write_atomic(const char *path, const char *data, size_t len)
{
    char tmp[sizeof S.path + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    unlink(tmp);
    int fd = open_new(tmp);
    FILE *f = fd < 0 ? NULL : fdopen(fd, "wb");
    if (!f) {
        if (fd >= 0) close(fd);
        return false;
    }
    bool ok = fwrite(data, 1, len, f) == len && fflush(f) == 0 && sync_fd(f) == 0;
    ok = fclose(f) == 0 && ok && replace(tmp, path);
    if (!ok) unlink(tmp);
    return ok;
}

/* the whole roster as text: true, or false (nothing changed on disk) */
static bool save(void)
{
    if (!S.path[0]) return true;
    size_t cap = 64u + (size_t) S.r.n * LINE_MAX_;
    char *buf = malloc(cap);
    if (!buf) return false;
    int k = snprintf(buf, cap, "%s 1 %u %u\n", MAGIC, (unsigned) S.r.n, S.r.auto_enter ? 1u : 0u);
    char *o = buf + k;
    for (uint32_t i = 0; i < S.r.n; i++) {
        const seat_t *s = &S.r.v[i];
        o = put_hex(o, s->id, 16);
        o += sprintf(o, "%u %u %u %u %u %u %u %llu ", (unsigned) s->role, (unsigned) s->lock,
                     (unsigned) s->color, (unsigned) s->kdf_m_kib, (unsigned) s->kdf_t,
                     (unsigned) s->kdf_p, (unsigned) s->fails,
                     (unsigned long long) s->wait_until_ms);
        o = put_hex(o, s->salt, SEAT_SALT_LEN);
        o = put_hex(o, s->hash, SEAT_HASH_LEN);
        o = put_hex(o, s->passkey_id, sizeof s->passkey_id);
        o = put_hex(o, (const uint8_t *) s->name, (uint32_t) strlen(s->name));
        o[-1] = '\n';
    }
    memcpy(o, "end\n", 4);
    o += 4;
    bool ok = write_atomic(S.path, buf, (size_t) (o - buf));
    wipe(buf, cap);
    free(buf);
    return ok;
}

/* one seat line into s: NULL, or what is wrong with it */
static const char *parse_seat(char *line, seat_t *s)
{
    char *f[13];
    uint64_t v[8] = {0};
    static const uint64_t MAX[8] = {SEAT_GUEST,      SEAT_LOCK_PASSKEY, UINT32_MAX, KDF_M_MAX,
                                    ID_ARGON2_MAX_T, ID_ARGON2_MAX_P,   UINT32_MAX, UINT64_MAX};
    if (split(line, f, 13) != 13) return "a seat line does not have 13 fields";
    memset(s, 0, sizeof *s);
    if (!get_hex(f[0], s->id, 16)) return "a seat id is not 32 hex digits";
    for (uint32_t i = 0; i < 8; i++)
        if (!get_num(f[1 + i], MAX[i], &v[i])) return "a number is malformed or out of range";
    s->role = (uint8_t) v[0];
    s->lock = (uint8_t) v[1];
    s->color = (uint32_t) v[2];
    s->kdf_m_kib = (uint32_t) v[3];
    s->kdf_t = (uint32_t) v[4];
    s->kdf_p = (uint32_t) v[5];
    s->fails = (uint32_t) v[6];
    s->wait_until_ms = v[7];
    if (!get_hex(f[9], s->salt, SEAT_SALT_LEN) || !get_hex(f[10], s->hash, SEAT_HASH_LEN) ||
        !get_hex(f[11], s->passkey_id, sizeof s->passkey_id))
        return "a salt, hash or passkey id is malformed";
    size_t nl = strlen(f[12]) / 2u;
    if (nl == 0 || nl >= SEAT_NAME_MAX || !get_hex(f[12], (uint8_t *) s->name, (uint32_t) nl) ||
        strlen(s->name) != nl)
        return "a name is empty, too long or malformed";
    if (s->role == SEAT_GUEST && s->lock != SEAT_LOCK_NONE) return "a guest seat has a lock";
    bool pass = s->lock == SEAT_LOCK_PASS;
    if (pass ? s->kdf_m_kib < 8u || s->kdf_t == 0 || s->kdf_p == 0
             : s->kdf_m_kib || s->kdf_t || s->kdf_p)
        return "a seat's Argon2id parameters do not match its lock";
    return NULL;
}

static const char *parse(char *text, size_t len)
{
    char *f[4];
    uint64_t n = 0, au = 0;
    if (len == 0 || text[len - 1] != '\n') return "it does not end with a whole line";
    text[len - 1] = 0;
    char *line = text, *next = strchr(line, '\n');
    if (!next) return "it has no trailer";
    *next++ = 0;
    if (split(line, f, 4) != 4 || strcmp(f[0], MAGIC) || strcmp(f[1], "1") ||
        !get_num(f[2], SEATS_MAX, &n) || !get_num(f[3], 1, &au))
        return "the header is not \"zxv-seats 1 <count> <0|1>\"";
    uint32_t cap = n > 4u ? (uint32_t) n : 4u;
    seat_t *v = calloc(cap, sizeof(seat_t));
    if (!v) return "out of memory";
    seat_roster_grow(&S.r, v, cap);
    bool owner = false;
    for (uint32_t i = 0; i < n; i++) {
        line = next;
        next = line ? strchr(line, '\n') : NULL;
        if (!next) return "it has fewer seats than its header says";
        *next++ = 0;
        seat_t *s = &S.r.v[i];
        const char *bad = parse_seat(line, s);
        if (bad) return bad;
        for (uint32_t j = 0; j < i; j++)
            if (!memcmp(S.r.v[j].id, s->id, 16) || !strcmp(S.r.v[j].name, s->name))
                return "two seats share an id or a name";
        owner = owner || s->role == SEAT_OWNER;
        S.r.n = i + 1;
    }
    if (n && !owner) return "no seat is an owner";
    if (!next || strcmp(next, "end")) return "the trailer is missing or there is more after it";
    S.r.auto_enter = au == 1;
    return NULL;
}

static int load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (errno == ENOENT) return 0; /* no seats yet */
        fprintf(stderr, "zxvd: the seats file %s cannot be read\n", path);
        return -1;
    }
    /* a valid roster is shorter than FILE_MAX, so one cut short there fails
     * the trailer check */
    char *buf = malloc(FILE_MAX);
    size_t len = buf ? fread(buf, 1, FILE_MAX, f) : 0;
    fclose(f);
    const char *bad = buf ? parse(buf, len) : "out of memory";
    wipe(buf, buf ? FILE_MAX : 0);
    free(buf);
    if (!bad) return 0;
    fprintf(stderr,
            "zxvd: the seats file %s is damaged: %s (move it aside to start with no seats)\n", path,
            bad);
    return -1;
}

int zxvd_seats_open(const char *path)
{
    seats_close();
    if (path && strlen(path) >= sizeof S.path) {
        fprintf(stderr, "zxvd: the seats file path is too long\n");
        return -1;
    }
    ready();
    if (!path || !path[0]) return 0;
    memcpy(S.path, path, strlen(path) + 1);
    if (load(path) == 0) return 0;
    seats_close();
    return -1;
}

/* ===================== undo (L8) ===================== */

typedef struct {
    seat_t *v;
    uint32_t n;
    bool auto_enter;
} snap_t;

static bool snap_take(zxvd_call_t *c, snap_t *sn)
{
    sn->n = S.r.n;
    sn->auto_enter = S.r.auto_enter;
    sn->v = malloc(((size_t) sn->n + 1u) * sizeof(seat_t));
    if (sn->v) {
        if (sn->n) memcpy(sn->v, S.r.v, (size_t) sn->n * sizeof(seat_t));
        return true;
    }
    zxvd_fail(c, ZXVD_E_INTERNAL, "out of memory");
    return false;
}

static void snap_drop(snap_t *sn)
{
    wipe(sn->v, (size_t) sn->n * sizeof(seat_t));
    free(sn->v);
    sn->v = NULL;
}

/* a change made since snap_take is kept only if it reached the file */
static bool commit(zxvd_call_t *c, snap_t *sn)
{
    const char *bad = S.rng_bad ? "no random bytes from the OS"
                      : !save() ? "the seats file could not be written; nothing changed"
                                : NULL;
    if (bad) {
        if (S.r.n > sn->n) wipe(&S.r.v[sn->n], (size_t) (S.r.n - sn->n) * sizeof(seat_t));
        if (sn->n) memcpy(S.r.v, sn->v, (size_t) sn->n * sizeof(seat_t));
        S.r.n = sn->n;
        S.r.auto_enter = sn->auto_enter;
        zxvd_fail(c, ZXVD_E_INTERNAL, "%s", bad);
    }
    snap_drop(sn);
    return !bad;
}

/* ===================== parameters ===================== */

static bool p_hex_exact(zxvd_call_t *c, const char *key, uint8_t *out, uint32_t len)
{
    uint32_t n;
    if (!zxvd_p_hex(c, key, out, len, &n, true)) return false;
    if (n == len) return true;
    zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must be %u bytes of hex", key, (unsigned) len);
    return false;
}

static bool p_seat(zxvd_call_t *c, int32_t *idx)
{
    uint8_t id[16];
    if (!p_hex_exact(c, "id", id, 16)) return false;
    *idx = find_seat(id);
    if (*idx >= 0) return true;
    refuse(c, SEAT_ERR_NOTFOUND);
    return false;
}

static sess_t *find_sess(const uint8_t tok[TOK_LEN])
{
    sess_t *hit = NULL;
    for (uint32_t i = 0; i < SESS_MAX; i++) {
        uint8_t d = 0;
        for (uint32_t k = 0; k < TOK_LEN; k++) d |= (uint8_t) (S.sess[i].tok[k] ^ tok[k]);
        if (S.sess[i].used && d == 0) hit = &S.sess[i];
    }
    return hit;
}

/* L5: the acting seat of "session" */
static bool p_session(zxvd_call_t *c, int32_t *actor)
{
    uint8_t tok[TOK_LEN];
    if (!p_hex_exact(c, "session", tok, TOK_LEN)) return false;
    sess_t *s = find_sess(tok);
    *actor = s ? find_seat(s->seat) : -1;
    if (*actor >= 0) return true;
    zxvd_fail(c, ZXVD_E_SEATS, "SEAT_ERR_DENIED: unknown or ended session");
    return false;
}

static bool p_enum(zxvd_call_t *c, const char *key, const char *const *names, uint32_t def,
                   bool required, uint32_t *out)
{
    char s[16];
    if (!zxvd_p_str(c, key, s, sizeof s, required)) return false;
    *out = def;
    if (!s[0] && !zxvd_p_has(c, key)) return true;
    for (uint32_t i = 0; i < 3; i++)
        if (!strcmp(s, names[i])) {
            *out = i;
            return true;
        }
    zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must be \"%s\", \"%s\" or \"%s\"", key, names[0], names[1],
              names[2]);
    return false;
}

/* a passphrase: *got false when absent (the module then sees NULL) */
static bool p_pass(zxvd_call_t *c, const char *key, char out[SEAT_PASS_MAX + 1], bool *got)
{
    *got = zxvd_p_has(c, key);
    return zxvd_p_str(c, key, out, SEAT_PASS_MAX + 1, false);
}

static const uint8_t *pass_ptr(const char *p, bool got)
{
    return got ? (const uint8_t *) p : NULL;
}

/* ===================== methods ===================== */

static void put_seat(zxvd_buf_t *b, const seat_t *s, uint64_t now)
{
    zxvd_buf_raw(b, "{\"id\":", 6);
    zxvd_buf_hex(b, s->id, 16);
    zxvd_buf_raw(b, ",\"name\":", 8);
    zxvd_buf_json_str(b, s->name);
    zxvd_buf_printf(b, ",\"role\":\"%s\",\"lock\":\"%s\",\"color\":%u,\"wait_ms\":%llu}",
                    ROLES[s->role], LOCKS[s->lock], (unsigned) s->color,
                    (unsigned long long) (s->wait_until_ms > now ? s->wait_until_ms - now : 0));
}

static void m_list(zxvd_call_t *c)
{
    ready();
    uint64_t now = CLOCK();
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_raw(b, "{\"seats\":[", 10);
    for (uint32_t i = 0; i < S.r.n; i++) {
        if (i) zxvd_buf_raw(b, ",", 1);
        clamp_wait(&S.r.v[i], now);
        put_seat(b, &S.r.v[i], now);
    }
    zxvd_buf_printf(b, "],\"auto_enter\":%s}", S.r.auto_enter ? "true" : "false");
}

static void m_add(zxvd_call_t *c)
{
    char name[256], pass[SEAT_PASS_MAX + 1];
    uint32_t role, lock, color = 0;
    int32_t actor = -1, idx = -1;
    bool got;
    snap_t sn;
    seat_env_t e;
    ready();
    if (!zxvd_p_str(c, "name", name, sizeof name, true) ||
        !p_enum(c, "role", ROLES, SEAT_PERSON, false, &role) ||
        !p_enum(c, "lock", LOCKS, SEAT_LOCK_NONE, false, &lock) || !p_pass(c, "pass", pass, &got) ||
        (zxvd_p_has(c, "color") && !zxvd_p_u32(c, "color", &color)) ||
        (S.r.n && !p_session(c, &actor)))
        goto out;
    if (!room()) {
        zxvd_fail(c, ZXVD_E_INTERNAL, "out of memory");
        goto out;
    }
    if (!snap_take(c, &sn)) goto out;
    if (!env_open(c, &e, lock == SEAT_LOCK_PASS, 0)) {
        snap_drop(&sn);
        goto out;
    }
    S.rng_bad = false;
    seat_status_t st = seat_add(&S.r, &e, actor, name, (seat_role_t) role, (seat_lock_t) lock,
                                pass_ptr(pass, got), (uint32_t) strlen(pass), &idx);
    env_close(&e);
    if (st != SEAT_OK) {
        snap_drop(&sn);
        refuse(c, st);
        goto out;
    }
    S.r.v[idx].color = color;
    if (!commit(c, &sn)) goto out;
    zxvd_buf_t *b = zxvd_result(c);
    put_seat(b, &S.r.v[idx], 0);
out:
    wipe(pass, sizeof pass);
}

static void m_remove(zxvd_call_t *c)
{
    int32_t actor, idx;
    snap_t sn;
    ready();
    if (!p_session(c, &actor) || !p_seat(c, &idx) || !snap_take(c, &sn)) return;
    uint8_t id[16];
    memcpy(id, S.r.v[idx].id, 16);
    seat_status_t st = seat_remove(&S.r, actor, idx);
    if (st != SEAT_OK) {
        snap_drop(&sn);
        return refuse(c, st);
    }
    S.rng_bad = false;
    if (!commit(c, &sn)) return;
    for (uint32_t i = 0; i < SESS_MAX; i++) /* L4: its sessions end with it */
        if (S.sess[i].used && !memcmp(S.sess[i].seat, id, 16)) wipe(&S.sess[i], sizeof S.sess[i]);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_raw(b, "{\"removed\":", 11);
    zxvd_buf_hex(b, id, 16);
    zxvd_buf_raw(b, "}", 1);
}

static void m_set_lock(zxvd_call_t *c)
{
    char oldp[SEAT_PASS_MAX + 1], pass[SEAT_PASS_MAX + 1];
    uint32_t lock;
    int32_t actor, idx;
    bool got_old, got;
    snap_t sn;
    seat_env_t e;
    ready();
    oldp[0] = pass[0] = 0;
    if (!p_session(c, &actor) || !p_seat(c, &idx) ||
        !p_enum(c, "lock", LOCKS, SEAT_LOCK_NONE, true, &lock) ||
        !p_pass(c, "old_pass", oldp, &got_old) || !p_pass(c, "pass", pass, &got) ||
        !snap_take(c, &sn))
        goto out;
    const seat_t *s = &S.r.v[idx];
    bool hashes = lock == SEAT_LOCK_PASS || (actor == idx && s->lock == SEAT_LOCK_PASS);
    if (!env_open(c, &e, hashes, s->kdf_m_kib)) {
        snap_drop(&sn);
        goto out;
    }
    S.rng_bad = false;
    seat_status_t st =
        seat_set_lock(&S.r, &e, actor, idx, (seat_lock_t) lock, pass_ptr(oldp, got_old),
                      (uint32_t) strlen(oldp), pass_ptr(pass, got), (uint32_t) strlen(pass));
    env_close(&e);
    if (st != SEAT_OK) {
        snap_drop(&sn);
        refuse(c, st);
        goto out;
    }
    if (!commit(c, &sn)) goto out;
    put_seat(zxvd_result(c), &S.r.v[idx], 0);
out:
    wipe(oldp, sizeof oldp);
    wipe(pass, sizeof pass);
}

/* L4: a new session for seat idx, written into the result */
static void issue(zxvd_call_t *c, int32_t idx)
{
    sess_t *slot = &S.sess[0]; /* the oldest; an unused slot has seq 0 */
    for (uint32_t i = 1; i < SESS_MAX; i++)
        if (S.sess[i].seq < slot->seq) slot = &S.sess[i];
    sess_t t;
    if (RANDOM(t.tok, TOK_LEN) != 0) return zxvd_fail(c, ZXVD_E_INTERNAL, "no random bytes");
    t.used = true;
    memcpy(t.seat, S.r.v[idx].id, 16);
    t.seq = ++S.seq;
    *slot = t;
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_raw(b, "{\"ok\":true,\"wait_ms\":0,\"session\":", 33);
    zxvd_buf_hex(b, t.tok, TOK_LEN);
    zxvd_buf_raw(b, ",\"id\":", 6);
    zxvd_buf_hex(b, t.seat, 16);
    zxvd_buf_raw(b, "}", 1);
    wipe(&t, sizeof t);
}

static void m_unlock(zxvd_call_t *c)
{
    char pass[SEAT_PASS_MAX + 1];
    int32_t idx;
    bool got, pk;
    seat_env_t e;
    ready();
    pass[0] = 0;
    if (!p_seat(c, &idx) || !p_pass(c, "pass", pass, &got) ||
        !zxvd_p_bool(c, "passkey_ok", false, &pk))
        goto out;
    seat_t *s = &S.r.v[idx];
    uint64_t now = CLOCK(), wait = 0;
    clamp_wait(s, now);
    uint32_t fails0 = s->fails; /* every change to the back-off changes the count */
    if (!env_open(c, &e, s->lock == SEAT_LOCK_PASS, s->kdf_m_kib)) goto out;
    seat_status_t st =
        seat_unlock(&S.r, &e, idx, pass_ptr(pass, got), (uint32_t) strlen(pass), pk, now, &wait);
    env_close(&e);
    if (s->fails != fails0) (void) save(); /* L8: best effort */
    if (st == SEAT_OK)
        issue(c, idx);
    else /* WRONG or WAIT: the seat exists and the Argon2id memory is there */
        zxvd_buf_printf(zxvd_result(c), "{\"ok\":false,\"status\":\"%s\",\"wait_ms\":%llu}",
                        NAMES[st], (unsigned long long) wait);
out:
    wipe(pass, sizeof pass);
}

static void m_lock(zxvd_call_t *c)
{
    uint8_t tok[TOK_LEN];
    if (!p_hex_exact(c, "session", tok, TOK_LEN)) return;
    sess_t *s = find_sess(tok);
    if (s) wipe(s, sizeof *s);
    zxvd_buf_printf(zxvd_result(c), "{\"ended\":%s}", s ? "true" : "false");
}

static void m_auto(zxvd_call_t *c)
{
    ready();
    int32_t i = seat_auto_enter(&S.r);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_raw(b, "{\"seat\":", 8);
    if (i >= 0)
        zxvd_buf_hex(b, S.r.v[i].id, 16);
    else
        zxvd_buf_raw(b, "null", 4);
    zxvd_buf_raw(b, "}", 1);
}

static void m_set_auto(zxvd_call_t *c)
{
    int32_t actor;
    bool on;
    snap_t sn;
    ready();
    if (!zxvd_p_has(c, "auto_enter"))
        return zxvd_fail(c, ZXVD_E_PARAMS, "missing boolean parameter \"auto_enter\"");
    if (!zxvd_p_bool(c, "auto_enter", true, &on) || !p_session(c, &actor)) return;
    if (S.r.v[actor].role != SEAT_OWNER) return refuse(c, SEAT_ERR_DENIED);
    if (!snap_take(c, &sn)) return;
    S.r.auto_enter = on;
    S.rng_bad = false;
    if (!commit(c, &sn)) return;
    zxvd_buf_printf(zxvd_result(c), "{\"auto_enter\":%s}", on ? "true" : "false");
}

static const zxvd_group_method_t METHODS[] = {
    {"seats.list", m_list, true},     {"seats.add", m_add, true},
    {"seats.remove", m_remove, true}, {"seats.set_lock", m_set_lock, true},
    {"seats.unlock", m_unlock, true}, {"seats.lock", m_lock, true},
    {"seats.auto", m_auto, true},     {"seats.set_auto", m_set_auto, true},
};

static const zxvd_group_t GROUP = {
    "seats",
    false,
    "the people who use this device and the login after boot (kernel/src/seats): Argon2id "
    "passphrases, sessions in memory; always on",
    METHODS,
    sizeof METHODS / sizeof METHODS[0],
    seats_close};

const zxvd_group_t *zxvd_seats_group(void)
{
    return &GROUP;
}
