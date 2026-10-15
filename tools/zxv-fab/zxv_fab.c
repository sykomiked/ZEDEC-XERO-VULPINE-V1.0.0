/* zxv_fab.c — the zxv-fab tool: make keys, pack a file into a fabrication
 * recipe over public blocks, inspect a recipe, and rebuild the file from it.
 * Format and rules: kernel/src/fab/fab.h and docs/spec/FABRICATION.md.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 *
 *   zxv-fab keygen-sign PK SK [SEED-HEX]       ML-DSA-87 key pair (SK owner-only)
 *   zxv-fab keygen-kem EK DK [SEED-HEX]        ML-KEM-1024 key pair (DK owner-only)
 *   zxv-fab cid FILE                           the file's UnixFS CID (the target CID)
 *   zxv-fab put STORE FILE...                  copy blocks into STORE, named by CID
 *   zxv-fab pack TARGET OUT --pk PK --sk SK [--to EK] [--window N] BLOCK...
 *   zxv-fab inspect RECIPE [--dk DK] [--trust PK]...
 *   zxv-fab unpack RECIPE OUT --store STORE [--dk DK] [--trust PK]... [--airgap]
 *
 * pack without --to writes a PUBLIC recipe (app installs: nothing secret);
 * with --to it is SEALED to that recipient. unpack writes OUT only after the
 * rebuilt bytes match the target CID. --airgap fetches and checks every block
 * first, then refuses to go on unless no network interface but loopback is
 * up (Linux /sys/class/net), and only then uses the recipient key for the
 * diff. STORE is a directory of blocks named by their CID string, the layout
 * `put` writes; any block source that verifies by CID would do.
 */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../kernel/src/fab/fab.h"

#define MAX_FILE (1u << 30)

static fab_work_t W;

static void die(const char *m)
{
    fprintf(stderr, "zxv-fab: %s\n", m);
    exit(1);
}

static const char *err_name(int rc)
{
    switch (rc) {
    case FAB_OK:
        return "ok";
    case FAB_ERR_ARG:
        return "bad argument";
    case FAB_ERR_FORM:
        return "not a well-formed recipe";
    case FAB_ERR_TRUST:
        return "sender not trusted";
    case FAB_ERR_SIGN:
        return "signature does not verify";
    case FAB_ERR_KEY:
        return "recipe is not for this key";
    case FAB_ERR_AUTH:
        return "decryption failed (tampered or wrong key)";
    case FAB_ERR_PLAN:
        return "plan breaks a rule";
    case FAB_ERR_SPACE:
        return "buffer too small";
    case FAB_ERR_FETCH:
        return "a block is not in the store";
    case FAB_ERR_BLOCK:
        return "a block does not match its CID";
    case FAB_ERR_TARGET:
        return "rebuilt bytes do not match the target CID";
    case FAB_ERR_SEAL:
        return "air-gap seal is not in place";
    default:
        return "error";
    }
}

static void fail(const char *what, int rc)
{
    fprintf(stderr, "zxv-fab: %s: %s (%d)\n", what, err_name(rc), rc);
    exit(2);
}

static uint8_t *read_file(const char *path, uint32_t *len, uint32_t max)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "zxv-fab: %s: %s\n", path, strerror(errno));
        exit(1);
    }
    if (fseek(f, 0, SEEK_END) != 0) die("cannot seek");
    long n = ftell(f);
    if (n < 0 || (unsigned long) n > max) die("file too large");
    rewind(f);
    uint8_t *b = malloc(n ? (size_t) n : 1u);
    if (!b) die("out of memory");
    if (n && fread(b, 1, (size_t) n, f) != (size_t) n) die("short read");
    fclose(f);
    *len = (uint32_t) n;
    return b;
}

/* Write to PATH.tmp, then rename: a reader never sees half a file. */
static void write_file(const char *path, const uint8_t *b, uint32_t len, int mode)
{
    char tmp[4096];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int) sizeof tmp) die("path too long");
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) die("cannot create output");
    uint32_t o = 0;
    while (o < len) {
        ssize_t w = write(fd, b + o, len - o);
        if (w <= 0) die("write failed");
        o += (uint32_t) w;
    }
    if (fsync(fd) != 0 || close(fd) != 0) die("write failed");
    if (rename(tmp, path) != 0) die("rename failed");
}

static void random_bytes(uint8_t *b, uint32_t n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f || fread(b, 1, n, f) != n) die("no entropy source");
    fclose(f);
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void seed_from(const char *hex, uint8_t *out, uint32_t n)
{
    if (!hex) {
        random_bytes(out, n);
        return;
    }
    if (strlen(hex) != 2u * n) die("seed must be hex of the right length");
    for (uint32_t i = 0; i < n; i++) {
        int a = hexval(hex[2 * i]), b = hexval(hex[2 * i + 1]);
        if (a < 0 || b < 0) die("seed is not hex");
        out[i] = (uint8_t) (a << 4 | b);
    }
}

static void print_hex(const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) printf("%02x", b[i]);
}

static uint8_t *read_key(const char *path, uint32_t want)
{
    uint32_t n;
    uint8_t *k = read_file(path, &n, want);
    if (n != want) die("key file has the wrong length");
    return k;
}

/* ---- store ---------------------------------------------------------------- */

static int store_get(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    char name[IPFSN_CID_STR_MAX], path[4096];
    if (ipfsn_cid_to_string(cid, name, sizeof name) < 0) return -1;
    if (snprintf(path, sizeof path, "%s/%s", (const char *) ctx, name) >= (int) sizeof path)
        return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, cap, f);
    int more = fgetc(f) != EOF;
    fclose(f);
    if (more) return -1;
    *len = (uint32_t) n;
    return 0;
}

/* ---- air-gap seal ---------------------------------------------------------- */

/* TRUE when every interface but loopback is down; FALSE when one is up;
 * NEITHER when the state cannot be read (which refuses, like FALSE). */
static lpres_state_t net_sealed(void *ctx)
{
    (void) ctx;
    DIR *d = opendir("/sys/class/net");
    if (!d) return LPRES_STATE_NEITHER;
    lpres_state_t st = LPRES_STATE_TRUE;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || strcmp(e->d_name, "lo") == 0) continue;
        char path[512], state[32] = {0};
        snprintf(path, sizeof path, "/sys/class/net/%s/operstate", e->d_name);
        FILE *f = fopen(path, "r");
        if (!f || !fgets(state, sizeof state, f)) {
            if (f) fclose(f);
            st = LPRES_STATE_NEITHER;
            continue;
        }
        fclose(f);
        if (strncmp(state, "down", 4) != 0 && strncmp(state, "lowerlayerdown", 14) != 0 &&
            strncmp(state, "notpresent", 10) != 0) {
            fprintf(stderr, "zxv-fab: interface %s is %s", e->d_name, state);
            st = LPRES_STATE_FALSE;
        }
    }
    closedir(d);
    return st;
}

/* ---- commands --------------------------------------------------------------- */

static int cmd_keygen_sign(int argc, char **argv)
{
    if (argc < 2 || argc > 3) die("usage: keygen-sign PK SK [SEED-HEX]");
    static uint8_t pk[FAB_SIG_PK], sk[FAB_SIG_SK];
    uint8_t seed[32], id[FAB_ID];
    seed_from(argc == 3 ? argv[2] : 0, seed, 32);
    fab_sig_keypair(seed, pk, sk);
    write_file(argv[0], pk, FAB_SIG_PK, 0644);
    write_file(argv[1], sk, FAB_SIG_SK, 0600);
    fab_key_id(pk, FAB_SIG_PK, id);
    print_hex(id, FAB_ID);
    printf("\n");
    return 0;
}

static int cmd_keygen_kem(int argc, char **argv)
{
    if (argc < 2 || argc > 3) die("usage: keygen-kem EK DK [SEED-HEX]");
    static uint8_t ek[FAB_KEM_EK], dk[FAB_KEM_DK];
    uint8_t seed[64], id[FAB_ID];
    seed_from(argc == 3 ? argv[2] : 0, seed, 64);
    fab_kem_keypair(seed, seed + 32, ek, dk);
    write_file(argv[0], ek, FAB_KEM_EK, 0644);
    write_file(argv[1], dk, FAB_KEM_DK, 0600);
    fab_key_id(ek, FAB_KEM_EK, id);
    print_hex(id, FAB_ID);
    printf("\n");
    return 0;
}

static int cmd_cid(int argc, char **argv)
{
    if (argc != 1) die("usage: cid FILE");
    uint32_t n;
    uint8_t *b = read_file(argv[0], &n, MAX_FILE);
    ipfsn_cid_t c;
    char s[IPFSN_CID_STR_MAX];
    int rc = fab_target_cid(b, n, &W, &c);
    if (rc) fail("cid", rc);
    ipfsn_cid_to_string(&c, s, sizeof s);
    printf("%s\n", s);
    free(b);
    return 0;
}

static int cmd_put(int argc, char **argv)
{
    if (argc < 2) die("usage: put STORE FILE...");
    for (int i = 1; i < argc; i++) {
        uint32_t n;
        uint8_t *b = read_file(argv[i], &n, FAB_BLOCK_MAX);
        ipfsn_cid_t c;
        char s[IPFSN_CID_STR_MAX], path[4096];
        if (n == 0) die("a block cannot be empty");
        if (ipfsn_cid_sha256(IPFSN_MC_RAW, b, n, &c) != IPFSN_OK) die("cannot hash block");
        ipfsn_cid_to_string(&c, s, sizeof s);
        snprintf(path, sizeof path, "%s/%s", argv[0], s);
        write_file(path, b, n, 0644);
        printf("%s %s\n", s, argv[i]);
        free(b);
    }
    return 0;
}

static int cmd_pack(int argc, char **argv)
{
    const char *pkp = 0, *skp = 0, *ekp = 0;
    uint32_t window = 32;
    const char *pos[2 + FAB_MAX_BLOCKS];
    int np = 0;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--pk") && i + 1 < argc)
            pkp = argv[++i];
        else if (!strcmp(argv[i], "--sk") && i + 1 < argc)
            skp = argv[++i];
        else if (!strcmp(argv[i], "--to") && i + 1 < argc)
            ekp = argv[++i];
        else if (!strcmp(argv[i], "--window") && i + 1 < argc)
            window = (uint32_t) atoi(argv[++i]);
        else if (np < (int) (sizeof pos / sizeof pos[0]))
            pos[np++] = argv[i];
        else
            die("too many blocks");
    }
    if (np < 2 || !pkp || !skp)
        die("usage: pack TARGET OUT --pk PK --sk SK [--to EK] [--window N] BLOCK...");
    uint32_t nsrc = (uint32_t) np - 2u, tlen;
    uint8_t *t = read_file(pos[0], &tlen, MAX_FILE);
    fab_src_t *src = calloc(nsrc ? nsrc : 1u, sizeof *src);
    if (!src) die("out of memory");
    uint64_t public_bytes = 0;
    for (uint32_t i = 0; i < nsrc; i++) {
        uint8_t *b = read_file(pos[2 + i], &src[i].len, FAB_BLOCK_MAX);
        if (!src[i].len) die("a block cannot be empty");
        src[i].data = b;
        if (ipfsn_cid_sha256(IPFSN_MC_RAW, b, src[i].len, &src[i].cid) != IPFSN_OK)
            die("cannot hash block");
        public_bytes += src[i].len;
    }
    /* every match covers at least 8 bytes and has at most one literal before
     * it, so a target needs fewer than tlen / 4 + 2 ops */
    uint32_t bits = 20, ops_cap = tlen / 4u + 16u, no = 0, dl = 0, pl = 0, rl = 0;
    if (ops_cap > FAB_MAX_OPS) ops_cap = FAB_MAX_OPS;
    fab_ient_t *table = malloc(sizeof *table << bits);
    uint8_t *ops = malloc((size_t) ops_cap * FAB_OP_REC), *diff = malloc(tlen ? tlen : 1u);
    if (!table || !ops || !diff) die("out of memory");
    int rc = fab_build(src, nsrc, t, tlen, window, table, bits, ops, ops_cap, &no, diff, tlen, &dl);
    if (rc) fail("build", rc);
    ipfsn_cid_t tc;
    if ((rc = fab_target_cid(t, tlen, &W, &tc))) fail("target cid", rc);
    uint32_t pcap = FAB_PLAN_HDR + nsrc * FAB_BLOCK_REC + no * FAB_OP_REC;
    uint8_t *plan = malloc(pcap);
    if (!plan) die("out of memory");
    if ((rc = fab_plan_write(&tc, tlen, 0, src, nsrc, ops, no, plan, pcap, &pl))) fail("plan", rc);

    uint8_t *pk = read_key(pkp, FAB_SIG_PK), *sk = read_key(skp, FAB_SIG_SK), *ek = 0;
    uint8_t coins[32], rnd[32];
    random_bytes(coins, 32);
    random_bytes(rnd, 32);
    fab_seal_args_t a = {FAB_KIND_FILE, FAB_MODE_PUBLIC, pk, sk, 0, 0, rnd};
    if (ekp) {
        ek = read_key(ekp, FAB_KEM_EK);
        a.mode = FAB_MODE_SEALED;
        a.recipient_ek = ek;
        a.kem_coins = coins;
    }
    uint32_t rcap = pl + dl + FAB_HDR + FAB_KEM_CT + 2u * FAB_TAG + FAB_SIG_PK + FAB_SIG;
    uint8_t *rec = malloc(rcap);
    if (!rec) die("out of memory");
    if ((rc = fab_recipe_seal(&a, plan, pl, diff, dl, 0, rec, rcap, &rl))) fail("seal", rc);
    write_file(pos[1], rec, rl, 0644);

    uint32_t used = 0;
    for (uint32_t b = 0; b < nsrc; b++) {
        for (uint32_t k = 0; k < no; k++) {
            const uint8_t *o = ops + k * FAB_OP_REC;
            if (o[0] == FAB_OP_SLICE && (uint32_t) ((o[2] << 8) | o[3]) == b) {
                used++;
                break;
            }
        }
    }
    char s[IPFSN_CID_STR_MAX];
    ipfsn_cid_to_string(&tc, s, sizeof s);
    printf("target %s %u bytes\n", s, tlen);
    printf("mode %s, ops %u, blocks %u used + %u decoy (%llu public bytes)\n",
           ekp ? "sealed" : "public", no, used, nsrc - used, (unsigned long long) public_bytes);
    printf("diff %u bytes, recipe %u bytes\n", dl, rl);
    return 0;
}

typedef struct {
    const char *rec;
    const char *out;
    const char *store;
    const char *dk;
    const char *trust[64];
    int ntrust;
    int airgap;
} opts_t;

static void parse_opts(int argc, char **argv, opts_t *o, int npos)
{
    int np = 0;
    memset(o, 0, sizeof *o);
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--store") && i + 1 < argc)
            o->store = argv[++i];
        else if (!strcmp(argv[i], "--dk") && i + 1 < argc)
            o->dk = argv[++i];
        else if (!strcmp(argv[i], "--trust") && i + 1 < argc && o->ntrust < 64)
            o->trust[o->ntrust++] = argv[++i];
        else if (!strcmp(argv[i], "--airgap"))
            o->airgap = 1;
        else if (np == 0)
            o->rec = argv[i], np++;
        else if (np == 1 && npos == 2)
            o->out = argv[i], np++;
        else
            die("unexpected argument");
    }
    if (np != npos) die("missing argument");
}

/* Parse and check; the trust list is SHA-256 of each --trust key. */
static uint8_t *open_recipe(const opts_t *o, fab_recipe_t *r, uint32_t *len)
{
    uint8_t *buf = read_file(o->rec, len, MAX_FILE);
    int rc = fab_recipe_parse(buf, *len, r);
    if (rc) fail("parse", rc);
    uint8_t(*ids)[FAB_ID] = 0;
    if (o->ntrust) {
        ids = calloc((size_t) o->ntrust, FAB_ID);
        if (!ids) die("out of memory");
        for (int i = 0; i < o->ntrust; i++) {
            uint8_t *k = read_key(o->trust[i], FAB_SIG_PK);
            fab_key_id(k, FAB_SIG_PK, ids[i]);
            free(k);
        }
    }
    rc = fab_recipe_check(r, (const uint8_t(*)[FAB_ID]) ids, (uint32_t) o->ntrust);
    free(ids);
    if (rc) fail("check", rc);
    return buf;
}

static int cmd_inspect(int argc, char **argv)
{
    opts_t o;
    parse_opts(argc, argv, &o, 1);
    fab_recipe_t r;
    uint32_t len;
    uint8_t *buf = open_recipe(&o, &r, &len);
    printf("kind %s, mode %s, %u bytes\nsender ", r.kind ? "stream" : "file",
           r.mode ? "sealed" : "public", len);
    print_hex(r.sender_id, FAB_ID);
    printf("\n");
    if (r.mode == FAB_MODE_SEALED) {
        printf("recipient ");
        print_hex(r.recipient_id, FAB_ID);
        printf("\n");
        if (!o.dk) return 0;
    }
    uint8_t *dk = o.dk ? read_key(o.dk, FAB_KEM_DK) : 0;
    uint8_t *plan = malloc(r.plan_len);
    fab_plan_t p;
    if (!plan) die("out of memory");
    int rc = fab_open_plan(&r, r.mode ? dk : 0, plan, r.plan_len, &p);
    if (rc) fail("plan", rc);
    char s[IPFSN_CID_STR_MAX];
    ipfsn_cid_to_string(&p.target, s, sizeof s);
    printf("target %s %llu bytes, %u ops, diff %u bytes\n", s, (unsigned long long) p.target_len,
           p.n_ops, r.diff_len);
    for (uint32_t i = 0; i < p.n_blocks; i++) {
        ipfsn_cid_t c;
        uint32_t bl;
        fab_plan_block(&p, i, &c, &bl);
        ipfsn_cid_to_string(&c, s, sizeof s);
        printf("block %s %u\n", s, bl);
    }
    free(buf);
    return 0;
}

static int cmd_unpack(int argc, char **argv)
{
    opts_t o;
    parse_opts(argc, argv, &o, 2);
    if (!o.store) die("unpack needs --store");
    fab_recipe_t r;
    uint32_t len, got = 0, olen = 0;
    uint8_t *buf = open_recipe(&o, &r, &len);
    uint8_t *dk = o.dk ? read_key(o.dk, FAB_KEM_DK) : 0;
    if (r.mode == FAB_MODE_SEALED && !dk) die("a sealed recipe needs --dk");
    if (r.kind != FAB_KIND_FILE) die("stream recipes are opened by a stream receiver");
    uint8_t *plan = malloc(r.plan_len);
    fab_plan_t p;
    if (!plan) die("out of memory");
    int rc = fab_open_plan(&r, dk, plan, r.plan_len, &p);
    if (rc) fail("plan", rc);
    if (p.target_len > MAX_FILE) die("target too large for this tool");
    fab_io_t io = {.get = store_get,
                   .get_ctx = (void *) o.store,
                   .diff_buf = malloc(r.diff_len ? r.diff_len : 1u),
                   .diff_cap = r.diff_len,
                   .out = malloc(p.target_len ? (size_t) p.target_len : 1u),
                   .out_cap = (uint32_t) p.target_len,
                   .work = &W};
    if (!io.diff_buf || !io.out) die("out of memory");
    if ((rc = fab_hydrate(&p, &io, &got))) fail("hydrate", rc);
    if (o.airgap) io.seal = net_sealed;
    if ((rc = fab_assemble(&r, &p, dk, &io, 0, &olen))) fail("assemble", rc);
    write_file(o.out, io.out, olen, 0600);
    char s[IPFSN_CID_STR_MAX];
    ipfsn_cid_to_string(&p.target, s, sizeof s);
    printf("%s %u bytes from %u blocks\n", s, olen, got);
    free(buf);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: zxv-fab keygen-sign|keygen-kem|cid|put|pack|inspect|unpack ...\n");
        return 1;
    }
    W.loaded = -1;
    const char *c = argv[1];
    argc -= 2;
    argv += 2;
    if (!strcmp(c, "keygen-sign")) return cmd_keygen_sign(argc, argv);
    if (!strcmp(c, "keygen-kem")) return cmd_keygen_kem(argc, argv);
    if (!strcmp(c, "cid")) return cmd_cid(argc, argv);
    if (!strcmp(c, "put")) return cmd_put(argc, argv);
    if (!strcmp(c, "pack")) return cmd_pack(argc, argv);
    if (!strcmp(c, "inspect")) return cmd_inspect(argc, argv);
    if (!strcmp(c, "unpack")) return cmd_unpack(argc, argv);
    die("unknown command");
    return 1;
}
