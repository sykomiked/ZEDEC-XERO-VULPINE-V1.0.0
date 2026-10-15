/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv-index — build and check cartographers' overlays for the Library of
 * Alexandria (kernel/src/alexandria/alx_carto.h, docs/spec/CARTOGRAPHY.md).
 *
 *   zxv-index keygen PK SK [SEED-HEX]
 *       an ML-DSA-65 key pair; the seed comes from /dev/urandom unless one
 *       is given (a given seed is for tests: anyone who knows it can sign)
 *   zxv-index scan DIR DIVISION DOMAIN
 *       a mapping table for the files in DIR (by name), one grid word each
 *       in DIVISION (520 gives 520.01 .. 520.99, then 521.01 ..) and DOMAIN
 *       (data, sensor, embed or attest); edit the words before building
 *   zxv-index build TABLE DIVISION PARENT-CID PK SK OUT
 *       the signed overlay; prints its CID
 *   zxv-index check FILE
 *       the overlay's form and signature; prints its entries
 *
 * A table line is: the word (0x and 16 hex digits), the target's CID
 * (CIDv1, base32, sha2-256, raw or dag-pb), its length in bytes, and
 * optionally its disc sector; '#' starts a comment. scan gives each file
 * the CID of its bytes as one raw block, so it refuses files over 1 MiB:
 * give those the CID IPFS gives them, by hand. */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "alx_carto_mldsa.h"
#include "../../kernel/src/robin_debanks/sha256.h"

static uint8_t buf[ALX_BOOK_MAX + 1];

static int die(const char *msg)
{
    fprintf(stderr, "zxv-index: %s\n", msg);
    return 1;
}

static void cid_str(const uint8_t c[36], char out[60])
{
    static const char A[] = "abcdefghijklmnopqrstuvwxyz234567";
    uint32_t acc = 0, bits = 0, k = 0;
    out[k++] = 'b';
    for (int i = 0; i < 36; i++) {
        acc = acc << 8 | c[i];
        bits += 8;
        while (bits >= 5) out[k++] = A[(acc >> (bits -= 5)) & 31];
    }
    if (bits) out[k++] = A[(acc << (5 - bits)) & 31];
    out[k] = 0;
}

static int read_file(const char *path, uint8_t *b, size_t cap, uint32_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t r = fread(b, 1, cap, f);
    fclose(f);
    if (r == cap) return -2;
    *n = (uint32_t) r;
    return 0;
}

static int write_file(const char *path, const uint8_t *b, size_t n, int private_)
{
    /* a secret key is created owner-only, never readable for a moment */
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, private_ ? 0600 : 0644);
    if (fd < 0) return -1;
    if (private_ && fchmod(fd, 0600)) {
        close(fd);
        return -1;
    }
    FILE *f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        return -1;
    }
    int ok = fwrite(b, 1, n, f) == n;
    return fclose(f) == 0 && ok ? 0 : -1;
}

static int hexbyte(const char *s)
{
    int v = 0;
    for (int i = 0; i < 2; i++) {
        char c = s[i];
        v = v * 16 + (c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1000);
    }
    return v < 0 ? -1 : v;
}

static int keygen(int argc, char **argv)
{
    uint8_t seed[32], pk[ALX_IDX_PK], sk[ALX_MLDSA_SK];
    if (argc == 5) {
        if (strlen(argv[4]) != 64) return die("a seed is 64 hex digits");
        for (int i = 0; i < 32; i++) {
            int v = hexbyte(argv[4] + 2 * i);
            if (v < 0) return die("a seed is 64 hex digits");
            seed[i] = (uint8_t) v;
        }
    } else {
        FILE *r = fopen("/dev/urandom", "rb");
        if (!r || fread(seed, 1, 32, r) != 32) return die("cannot read /dev/urandom");
        fclose(r);
    }
    alx_mldsa_keypair(seed, pk, sk);
    memset(seed, 0, sizeof seed);
    if (write_file(argv[2], pk, sizeof pk, 0) || write_file(argv[3], sk, sizeof sk, 1))
        return die("cannot write the keys");
    memset(sk, 0, sizeof sk);
    return 0;
}

static int domain_of(const char *s)
{
    return !strcmp(s, "data")     ? ALX_G_DATA :
           !strcmp(s, "sensor")   ? ALX_G_SENSOR :
           !strcmp(s, "embed")    ? ALX_G_EMBED :
           !strcmp(s, "attest")   ? ALX_G_ATTEST :
                                    -1;
}

static int division_of(const char *s)
{
    char *end;
    long d = strtol(s, &end, 10);
    return *end || d < 100 || d > 990 || d % 10 ? -1 : (int) d;
}

static int byname(const void *a, const void *b)
{
    return strcmp(*(char *const *) a, *(char *const *) b);
}

static int scan(char **argv)
{
    int div = division_of(argv[3]), dom = domain_of(argv[4]);
    if (div < 0) return die("a division is 100 .. 990, a multiple of 10");
    if (dom < 0) return die("a domain is data, sensor, embed or attest");
    DIR *d = opendir(argv[2]);
    if (!d) return die("cannot open the directory");
    static char *names[990];
    int n = 0;
    for (struct dirent *e; (e = readdir(d)) != 0;) {
        if (e->d_name[0] == '.') continue;
        if (n == 990) return die("more than 990 files: split them across divisions");
        names[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, (size_t) n, sizeof names[0], byname);
    printf("# word\ttarget\tlength\t(file)\n");
    for (int i = 0; i < n; i++) {
        char path[4096], cs[60];
        uint32_t len;
        uint8_t cid[36];
        snprintf(path, sizeof path, "%s/%s", argv[2], names[i]);
        struct stat st;
        if (stat(path, &st) || !S_ISREG(st.st_mode)) continue;
        int r = read_file(path, buf, sizeof buf, &len);
        if (r == -2) {
            fprintf(stderr, "zxv-index: %s is over 1 MiB: give it its IPFS CID by hand\n", names[i]);
            return 1;
        }
        if (r || len == 0) return die("cannot read a file, or it is empty");
        alx_carto_cid(buf, len, cid);
        cid_str(cid, cs);
        uint64_t w = (uint64_t) dom << 56 | (uint64_t) (div + i / 99) << 40 |
                     (uint64_t) (i % 99 + 1) << 32;
        printf("0x%016llx\t%s\t%u\t%s\n", (unsigned long long) w, cs, len, names[i]);
    }
    return 0;
}

typedef struct {
    uint64_t w;
    uint8_t cid[36];
    uint32_t len, lba;
} row_t;

static row_t rows[ALX_IDX_MAX];

static int byword(const void *a, const void *b)
{
    uint64_t x = ((const row_t *) a)->w, y = ((const row_t *) b)->w;
    return x < y ? -1 : x > y;
}

static void put(uint8_t *p, uint64_t v, int n)
{
    for (int i = 0; i < n; i++) p[i] = (uint8_t) (v >> (8 * (n - 1 - i)));
}

static int build(char **argv)
{
    int div = division_of(argv[3]);
    uint8_t parent[36], pk[ALX_IDX_PK], sk[ALX_MLDSA_SK], rnd[32];
    uint32_t n = 0, len;
    if (div < 0) return die("a division is 100 .. 990, a multiple of 10");
    if (alx_cid_parse(argv[4], (uint32_t) strlen(argv[4]), parent) != ALX_OK)
        return die("the parent is not a CIDv1 sha2-256");
    if (read_file(argv[5], pk, sizeof pk + 1, &len) || len != sizeof pk)
        return die("cannot read the public key");
    if (read_file(argv[6], sk, sizeof sk + 1, &len) || len != sizeof sk)
        return die("cannot read the secret key");
    FILE *f = fopen(argv[2], "r");
    if (!f) return die("cannot open the table");
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char word[64], cid[128];
        unsigned long long w;
        unsigned l, lba = 0;
        if (line[0] == '#' || line[0] == '\n') continue;
        int k = sscanf(line, "%63s %127s %u %u", word, cid, &l, &lba);
        if (k < 3 || sscanf(word, "0x%llx", &w) != 1 || strlen(word) != 18) {
            fclose(f);
            fprintf(stderr, "zxv-index: bad line: %s", line);
            return 1;
        }
        if (n == ALX_IDX_MAX) return die("too many entries for one overlay");
        rows[n].w = w;
        rows[n].len = l;
        rows[n].lba = k == 4 ? lba : 0;
        if (alx_cid_parse(cid, (uint32_t) strlen(cid), rows[n].cid) != ALX_OK) {
            fclose(f);
            fprintf(stderr, "zxv-index: not a CIDv1 sha2-256: %s\n", cid);
            return 1;
        }
        n++;
    }
    fclose(f);
    if (n == 0) return die("the table is empty");
    qsort(rows, n, sizeof rows[0], byword);
    uint32_t body = ALX_IDX_HEAD + n * ALX_IDX_ENTRY, total = body + ALX_IDX_PK + ALX_IDX_SIG;
    if (total > ALX_BOOK_MAX) return die("the overlay would be over 1 MiB");
    memset(buf, 0, total);
    memcpy(buf, "ALXI", 4);
    buf[4] = 1;
    buf[5] = ALX_IDX_OVERLAY;
    put(buf + 6, (uint64_t) div, 2);
    put(buf + 8, n, 4);
    memcpy(buf + 16, parent, 36);
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *e = buf + ALX_IDX_HEAD + i * ALX_IDX_ENTRY;
        put(e, rows[i].w, 8);
        memcpy(e + 8, rows[i].cid, 36);
        put(e + 44, rows[i].lba, 4);
        put(e + 48, rows[i].len, 4);
    }
    uint8_t h[32], ocid[36];
    sha256(buf, body, h);
    memcpy(buf + body, pk, ALX_IDX_PK);
    FILE *r = fopen("/dev/urandom", "rb");
    if (!r || fread(rnd, 1, 32, r) != 32) return die("cannot read /dev/urandom");
    fclose(r);
    alx_mldsa_sign(sk, h, 32, rnd, buf + body + ALX_IDX_PK);
    memset(sk, 0, sizeof sk);
    int s = alx_carto_overlay_check(buf, total, alx_mldsa_verify, 0);
    if (s) {
        fprintf(stderr, "zxv-index: the overlay is refused: %s (a word outside the division, "
                        "a code word, a repeated word or a zero length?)\n",
                alx_status_name(s));
        return 1;
    }
    if (write_file(argv[7], buf, total, 0)) return die("cannot write the overlay");
    char cs[60];
    alx_carto_cid(buf, total, ocid);
    cid_str(ocid, cs);
    printf("%s\n", cs);
    return 0;
}

static int check(char **argv)
{
    uint32_t len;
    if (read_file(argv[2], buf, sizeof buf, &len)) return die("cannot read the overlay");
    int s = alx_carto_overlay_check(buf, len, alx_mldsa_verify, 0);
    if (s) {
        fprintf(stderr, "zxv-index: refused: %s\n", alx_status_name(s));
        return 1;
    }
    uint32_t n = (uint32_t) buf[8] << 24 | (uint32_t) buf[9] << 16 | (uint32_t) buf[10] << 8 | buf[11];
    uint8_t h[32], cid[36];
    char cs[60];
    sha256(buf + ALX_IDX_HEAD + n * ALX_IDX_ENTRY, ALX_IDX_PK, h);
    alx_carto_cid(buf, len, cid);
    cid_str(cid, cs);
    printf("overlay %s\ndivision %u, %u entries, signed by key sha256:", cs,
           (unsigned) (buf[6] << 8 | buf[7]), n);
    for (int i = 0; i < 32; i++) printf("%02x", h[i]);
    cid_str(buf + 16, cs);
    printf("\nparent %s\n", cs);
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = buf + ALX_IDX_HEAD + i * ALX_IDX_ENTRY;
        unsigned long long w = 0;
        for (int k = 0; k < 8; k++) w = w << 8 | e[k];
        cid_str(e + 8, cs);
        printf("0x%016llx\t%s\t%u\n", w, cs,
               (unsigned) ((uint32_t) e[48] << 24 | (uint32_t) e[49] << 16 | (uint32_t) e[50] << 8 | e[51]));
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 4 && argc <= 5 && !strcmp(argv[1], "keygen")) return keygen(argc, argv);
    if (argc == 5 && !strcmp(argv[1], "scan")) return scan(argv);
    if (argc == 8 && !strcmp(argv[1], "build")) return build(argv);
    if (argc == 3 && !strcmp(argv[1], "check")) return check(argv);
    fprintf(stderr, "usage: zxv-index keygen PK SK [SEED-HEX]\n"
                    "       zxv-index scan DIR DIVISION data|sensor|embed|attest\n"
                    "       zxv-index build TABLE DIVISION PARENT-CID PK SK OUT\n"
                    "       zxv-index check FILE\n");
    return 2;
}
