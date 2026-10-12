/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* efi_models.h — check the AI models a golden disc carries, from the EFI stub.
 *
 * Included by both kernel/arch/{arm64,x86_64}/efi_stub.c. Before the stub
 * leaves boot services it opens the volume it was loaded from (LoadedImage ->
 * DeviceHandle -> SimpleFileSystem) and looks for \ZXV\MODELS.SHA, written by
 * build_system/mk_golden_disc.sh. One line per model:
 *
 *     <sha256, 64 hex> <bytes> <file name in \ZXV\MODELS\>
 *
 * Each file is streamed through SHA-256 in 4 MiB reads and compared with its
 * line, so a corrupt or swapped model is caught on the machine that boots it.
 * It prints one serial line per model and a summary:
 *
 *     [MODEL_OK] <name> <bytes>      or  [MODEL_BAD] <name> <why>
 *     [MODELS_OK] <n> verified       or  [MODELS_FAIL] <bad> of <n>
 *
 * A plain disc has no MODELS.SHA and this prints nothing. A failed model never
 * stops the boot; uefi_boot_check.sh fails a golden disc that lacks
 * "[MODELS_OK]". Includer defines EFIAPI (ms_abi on x86_64, empty on arm64),
 * u8/u16/u32/u64, EFI_STATUS, EFI_HANDLE, EFI_GUID, uputs and udec. Integer
 * code only: the stubs build with -mgeneral-regs-only. No struct or array
 * initialisers that the compiler could lower to memset/memcpy calls. */
#ifndef ZXV_EFI_MODELS_H
#define ZXV_EFI_MODELS_H

typedef EFI_STATUS (EFIAPI *EM_HANDLEPROTO)(EFI_HANDLE, EFI_GUID *, void **);
typedef EFI_STATUS (EFIAPI *EM_POOL)(u32, u64, void **);
typedef EFI_STATUS (EFIAPI *EM_FREE)(void *);

typedef struct EM_FILE EM_FILE;
struct EM_FILE {
    u64 Revision;
    EFI_STATUS (EFIAPI *Open)(EM_FILE *, EM_FILE **, u16 *, u64, u64);
    EFI_STATUS (EFIAPI *Close)(EM_FILE *);
    void *Delete;
    EFI_STATUS (EFIAPI *Read)(EM_FILE *, u64 *, void *);
};
typedef struct {
    u64 Revision;
    EFI_STATUS (EFIAPI *OpenVolume)(void *, EM_FILE **);
} EM_SFS;
typedef struct {
    u32 Revision; EFI_HANDLE ParentHandle; void *SystemTable; EFI_HANDLE DeviceHandle;
} EM_LOADED_IMAGE;

/* ---- SHA-256 (FIPS 180-4), integer only ---- */
typedef struct { u32 h[8]; u64 len; u8 buf[64]; u32 n; } em_sha;

static const u32 em_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

#define EM_ROR(x, r) (((x) >> (r)) | ((x) << (32 - (r))))

static void em_block(u32 *h, const u8 *p) {
    u32 w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (u32)p[4*i] << 24 | (u32)p[4*i+1] << 16 | (u32)p[4*i+2] << 8 | p[4*i+3];
    for (int i = 16; i < 64; i++) {
        u32 s0 = EM_ROR(w[i-15], 7) ^ EM_ROR(w[i-15], 18) ^ (w[i-15] >> 3);
        u32 s1 = EM_ROR(w[i-2], 17) ^ EM_ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        u32 t1 = hh + (EM_ROR(e, 6) ^ EM_ROR(e, 11) ^ EM_ROR(e, 25)) + ((e & f) ^ (~e & g)) + em_k[i] + w[i];
        u32 t2 = (EM_ROR(a, 2) ^ EM_ROR(a, 13) ^ EM_ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

static void em_sha_init(em_sha *s) {
    s->h[0] = 0x6a09e667; s->h[1] = 0xbb67ae85; s->h[2] = 0x3c6ef372; s->h[3] = 0xa54ff53a;
    s->h[4] = 0x510e527f; s->h[5] = 0x9b05688c; s->h[6] = 0x1f83d9ab; s->h[7] = 0x5be0cd19;
    s->len = 0; s->n = 0;
}

static void em_sha_update(em_sha *s, const u8 *p, u64 n) {
    s->len += n;
    while (n && s->n) { s->buf[s->n++] = *p++; n--; if (s->n == 64) { em_block(s->h, s->buf); s->n = 0; } }
    while (n >= 64) { em_block(s->h, p); p += 64; n -= 64; }
    while (n) { s->buf[s->n++] = *p++; n--; }
}

static void em_sha_final(em_sha *s, u8 out[32]) {
    u64 bits = s->len * 8;
    u8 one = 0x80, zero = 0;
    em_sha_update(s, &one, 1);
    while (s->n != 56) em_sha_update(s, &zero, 1);
    u8 l[8];
    for (int i = 0; i < 8; i++) l[i] = (u8)(bits >> (56 - 8*i));
    em_sha_update(s, l, 8);
    for (int i = 0; i < 8; i++) {
        out[4*i] = (u8)(s->h[i] >> 24); out[4*i+1] = (u8)(s->h[i] >> 16);
        out[4*i+2] = (u8)(s->h[i] >> 8); out[4*i+3] = (u8)s->h[i];
    }
}

static int em_hexval(u8 c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void em_guid(EFI_GUID *g, u32 a, u16 b, u16 c, u8 d0, u8 d1, u8 d2, u8 d3,
                    u8 d4, u8 d5, u8 d6, u8 d7) {
    g->a = a; g->b = b; g->c = c;
    g->d[0] = d0; g->d[1] = d1; g->d[2] = d2; g->d[3] = d3;
    g->d[4] = d4; g->d[5] = d5; g->d[6] = d6; g->d[7] = d7;
}

/* Open \<dir>\<name> (ASCII name, at most 120 chars) read-only. */
static EFI_STATUS em_open(EM_FILE *root, const char *dir, const u8 *name, u32 nlen, EM_FILE **out) {
    u16 path[160];
    u32 i = 0;
    for (const char *d = dir; *d && i < 32; d++) path[i++] = (u16)(u8)*d;
    for (u32 j = 0; j < nlen && i < 159; j++) path[i++] = name[j];
    path[i] = 0;
    return root->Open(root, out, path, 1 /*EFI_FILE_MODE_READ*/, 0);
}

static void em_putn(const u8 *s, u32 n) {
    char b[2]; b[1] = 0;
    for (u32 i = 0; i < n; i++) { b[0] = (char)s[i]; uputs(b); }
}

/* Returns 0 when the disc has no model manifest, else the number checked. */
static int efi_check_models(EFI_HANDLE image, void *handle_protocol,
                            void *allocate_pool, void *free_pool) {
    EM_HANDLEPROTO HandleProtocol = (EM_HANDLEPROTO)handle_protocol;
    EM_POOL AllocatePool = (EM_POOL)allocate_pool;
    EM_FREE FreePool = (EM_FREE)free_pool;
    EFI_GUID li_guid, sfs_guid;
    em_guid(&li_guid, 0x5B1B31A1, 0x9562, 0x11d2, 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B);
    em_guid(&sfs_guid, 0x964E5B22, 0x6459, 0x11d2, 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B);

    EM_LOADED_IMAGE *li = 0; EM_SFS *sfs = 0; EM_FILE *root = 0, *mf = 0;
    if (HandleProtocol(image, &li_guid, (void **)&li) || !li) return 0;
    if (HandleProtocol(li->DeviceHandle, &sfs_guid, (void **)&sfs) || !sfs) return 0;
    if (sfs->OpenVolume(sfs, &root) || !root) return 0;
    if (em_open(root, "\\ZXV\\", (const u8 *)"MODELS.SHA", 10, &mf)) { root->Close(root); return 0; }

    u8 *man = 0, *buf = 0;
    u64 mlen = 8192;
    const u64 CHUNK = 4ull << 20;
    if (AllocatePool(2, mlen, (void **)&man) || AllocatePool(2, CHUNK, (void **)&buf)) {
        uputs("[MODELS_FAIL] no memory for the model check\n");
        mf->Close(mf); root->Close(root);
        return -1;
    }
    if (mf->Read(mf, &mlen, man)) mlen = 0;
    mf->Close(mf);

    int n = 0, bad = 0;
    u64 i = 0;
    while (i < mlen) {
        u64 e = i;
        while (e < mlen && man[e] != '\n') e++;
        u64 ln = e;
        if (ln > i && man[ln - 1] == '\r') ln--;
        if (ln > i && man[i] != '#') {
            /* <64 hex> <bytes> <name> */
            u8 want[32]; int ok = (ln - i) > 67;
            for (int k = 0; ok && k < 32; k++) {
                int hi = em_hexval(man[i + 2*k]), lo = em_hexval(man[i + 2*k + 1]);
                if (hi < 0 || lo < 0) ok = 0; else want[k] = (u8)(hi << 4 | lo);
            }
            u64 p = i + 64, size = 0;
            if (ok && man[p] != ' ') ok = 0;
            p++;
            while (ok && p < ln && man[p] >= '0' && man[p] <= '9') size = size * 10 + (man[p++] - '0');
            if (ok && (p >= ln || man[p] != ' ')) ok = 0;
            p++;
            const u8 *name = man + p;
            u32 nlen = (u32)(ln > p ? ln - p : 0);
            if (nlen == 0 || nlen > 120) ok = 0;
            n++;
            if (!ok) { uputs("[MODEL_BAD] unreadable manifest line\n"); bad++; }
            else {
                EM_FILE *f = 0;
                if (em_open(root, "\\ZXV\\MODELS\\", name, nlen, &f)) {
                    uputs("[MODEL_BAD] "); em_putn(name, nlen); uputs(" missing\n"); bad++;
                } else {
                    em_sha s; em_sha_init(&s);
                    u64 got = 0;
                    for (;;) {
                        u64 r = CHUNK;
                        if (f->Read(f, &r, buf) || r == 0) break;
                        em_sha_update(&s, buf, r);
                        got += r;
                    }
                    f->Close(f);
                    u8 dig[32]; em_sha_final(&s, dig);
                    int same = got == size;
                    for (int k = 0; k < 32; k++) if (dig[k] != want[k]) same = 0;
                    uputs(same ? "[MODEL_OK] " : "[MODEL_BAD] ");
                    em_putn(name, nlen); uputs(" "); udec(got);
                    uputs(same ? " bytes, sha256 matches\n" : " bytes, sha256 or size differs\n");
                    if (!same) bad++;
                }
            }
        }
        i = e + 1;
    }
    FreePool(buf); FreePool(man);
    root->Close(root);
    if (n == 0) { uputs("[MODELS_FAIL] MODELS.SHA lists no models\n"); return -1; }
    if (bad) { uputs("[MODELS_FAIL] "); udec((u64)bad); uputs(" of "); udec((u64)n); uputs(" models bad\n"); }
    else { uputs("[MODELS_OK] "); udec((u64)n); uputs(" verified\n"); }
    return n;
}

#endif /* ZXV_EFI_MODELS_H */
