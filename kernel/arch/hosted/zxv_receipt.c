/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_receipt.c — see zxv_receipt.h (E1-E4). */
#include <stdlib.h>
#include <string.h>

#include "sha256.h"
#include "zxv_receipt.h"

#define BODY_CAP 4096u
#define LINE_CAP 16384u

void zxv_receipt_init(zxv_receipt_chain_t *ch)
{
    memset(ch, 0, sizeof *ch);
}

static void hex(const uint8_t *b, size_t n, char *out)
{
    static const char D[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = D[b[i] >> 4];
        out[2 * i + 1] = D[b[i] & 15];
    }
    out[2 * n] = 0;
}

static int unhex(const char *s, uint8_t *b, size_t n)
{
    for (size_t i = 0; i < 2 * n; i++) {
        char c = s[i];
        int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (v < 0) return -1;
        if (i & 1)
            b[i / 2] = (uint8_t) (b[i / 2] | v);
        else
            b[i / 2] = (uint8_t) (v << 4);
    }
    return 0;
}

/* a JSON string, escaped, appended at *at; false when it does not fit */
static int put_str(char *out, size_t cap, size_t *at, const char *s)
{
    if (*at + 1 >= cap) return 0;
    out[(*at)++] = '"';
    for (const unsigned char *p = (const unsigned char *) (s ? s : ""); *p; p++) {
        char e[8];
        int k;
        if (*p == '"' || *p == '\\')
            k = snprintf(e, sizeof e, "\\%c", *p);
        else if (*p < 0x20)
            k = snprintf(e, sizeof e, "\\u%04x", *p);
        else
            k = snprintf(e, sizeof e, "%c", *p);
        if (*at + (size_t) k + 2 >= cap) return 0;
        memcpy(out + *at, e, (size_t) k);
        *at += (size_t) k;
    }
    out[(*at)++] = '"';
    out[*at] = 0;
    return 1;
}

static int put_raw(char *out, size_t cap, size_t *at, const char *s)
{
    size_t n = strlen(s);
    if (*at + n + 1 >= cap) return 0;
    memcpy(out + *at, s, n + 1);
    *at += n;
    return 1;
}

static void text_sha(const char *s, char out[65])
{
    uint8_t h[32];
    sha256((const uint8_t *) (s ? s : ""), strlen(s ? s : ""), h);
    hex(h, 32, out);
}

int zxv_receipt_seal(zxv_receipt_chain_t *ch, const zxv_receipt_t *r, zxv_receipt_sign_fn sign,
                     void *sign_ctx, FILE *f, uint8_t hash_out[32])
{
    if (!ch || !r || !f) return -1;
    char body[BODY_CAP], num[64], prev[65], q[65], shown[65];
    size_t at = 0;
    hex(ch->prev, 32, prev);
    text_sha(r->question, q);
    text_sha(r->shown, shown);
    snprintf(num, sizeof num, "{\"seq\":%llu,\"prev\":", (unsigned long long) (ch->seq + 1));
    int ok =
        put_raw(body, sizeof body, &at, num) && put_str(body, sizeof body, &at, prev) &&
        put_raw(body, sizeof body, &at, ",\"kind\":") && put_str(body, sizeof body, &at, r->kind) &&
        put_raw(body, sizeof body, &at, ",\"q_sha\":") && put_str(body, sizeof body, &at, q) &&
        put_raw(body, sizeof body, &at, ",\"shown_sha\":") &&
        put_str(body, sizeof body, &at, shown) && put_raw(body, sizeof body, &at, ",\"lead\":") &&
        put_str(body, sizeof body, &at, r->lead) &&
        put_raw(body, sizeof body, &at, ",\"witness\":") &&
        put_str(body, sizeof body, &at, r->witness) &&
        put_raw(body, sizeof body, &at, ",\"verdict\":") &&
        put_str(body, sizeof body, &at, r->verdict);
    snprintf(num, sizeof num,
             ",\"cycle\":%llu,\"tokens\":%llu,\"paused\":", (unsigned long long) r->cycle,
             (unsigned long long) r->tokens);
    ok = ok && put_raw(body, sizeof body, &at, num) && put_str(body, sizeof body, &at, r->paused) &&
         put_raw(body, sizeof body, &at, "}");
    if (!ok) return -1;
    uint8_t h[32];
    sha256((const uint8_t *) body, at, h); /* E2: the body ends with its '}' */
    char hh[65];
    hex(h, 32, hh);
    char *sig = NULL, signer[65] = "";
    if (sign) {
        sig = malloc(LINE_CAP);
        if (!sig || !sign(sign_ctx, h, sig, LINE_CAP, signer)) {
            free(sig);
            return -1;
        }
    }
    body[at - 1] = 0; /* drop the closing brace; hash (and sig) follow */
    int w = fprintf(f, "%s,\"hash\":\"%s\"", body, hh) < 0;
    if (sig) w |= fprintf(f, ",\"sig\":\"%s\",\"signer\":\"%s\"", sig, signer) < 0;
    w |= fputs("}\n", f) < 0;
    w |= fflush(f) != 0;
    free(sig);
    if (w) return -1;
    ch->seq++;
    memcpy(ch->prev, h, 32);
    if (hash_out) memcpy(hash_out, h, 32);
    return 0;
}

int zxv_receipt_verify(const char *path, uint64_t *n, zxv_receipt_chain_t *out, char *err,
                       size_t err_cap)
{
    zxv_receipt_chain_t ch;
    zxv_receipt_init(&ch);
    if (n) *n = 0;
    if (err && err_cap) err[0] = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (out) *out = ch;
        return 0;
    }
    char *line = malloc(LINE_CAP);
    if (!line) {
        fclose(f);
        if (err && err_cap) snprintf(err, err_cap, "out of memory");
        return -1;
    }
    int rc = 0;
    while (fgets(line, (int) LINE_CAP, f)) {
        uint64_t k = ch.seq + 1;
        size_t len = strlen(line);
        if (!len || line[len - 1] != '\n') {
            snprintf(err, err_cap, "receipt %llu: the line is cut or too long",
                     (unsigned long long) k);
            rc = -1;
            break;
        }
        char want_head[96];
        snprintf(want_head, sizeof want_head, "{\"seq\":%llu,\"prev\":\"", (unsigned long long) k);
        char *hp = strstr(line, ",\"hash\":\"");
        uint8_t prev[32], h[32], got[32];
        size_t wl = strlen(want_head);
        if (strncmp(line, want_head, wl) != 0 || unhex(line + wl, prev, 32) != 0 || !hp ||
            unhex(hp + 9, h, 32) != 0 || hp[9 + 64] != '"') {
            snprintf(err, err_cap, "receipt %llu: seq or the hash fields are wrong",
                     (unsigned long long) k);
            rc = -1;
            break;
        }
        if (memcmp(prev, ch.prev, 32) != 0) {
            snprintf(err, err_cap, "receipt %llu: prev is not the previous receipt's hash",
                     (unsigned long long) k);
            rc = -1;
            break;
        }
        sha256_ctx_t c;
        sha256_init(&c);
        sha256_update(&c, (const uint8_t *) line, (size_t) (hp - line));
        sha256_update(&c, (const uint8_t *) "}", 1);
        sha256_final(&c, got);
        if (memcmp(got, h, 32) != 0) {
            snprintf(err, err_cap, "receipt %llu: its hash does not match its contents",
                     (unsigned long long) k);
            rc = -1;
            break;
        }
        ch.seq = k;
        memcpy(ch.prev, h, 32);
    }
    free(line);
    fclose(f);
    if (n) *n = ch.seq;
    if (rc == 0 && out) *out = ch;
    return rc;
}

int zxv_receipt_resume(const char *path, zxv_receipt_chain_t *ch, char *err, size_t err_cap)
{
    return zxv_receipt_verify(path, NULL, ch, err, err_cap);
}
