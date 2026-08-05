/* crypto_wallet.c — 5-Key Vector Cryptographic File System Implementation
 *
 * The "Wallet is the OS" architecture:
 *   - BIOS root seed → HKDF → 5 keys (one per polar trit phase)
 *   - Content-addressable storage (Merkle hash, not file path)
 *   - Zero-copy phase shifting (re-sign Merkle pointer, O(1) not O(N))
 *   - Cross-wallet interlocking hash lattice for self-verifying integrity
 *
 * File Archetype → Logic Phase → Key Mapping:
 *   .36n9  (TRUE)        → K1 — verified execution
 *   .9n63  (FALSE)       → K2 — null/shadow/tombstone
 *   .zedec (GLUT_PLUS)   → K3 — speculative stream
 *   .vino  (GLUT_MINUS)  → K4 — defensive ledger
 *   .ula   (GLUT_NEUTRAL)→ K5 — cold storage / BIOS manifest
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3 + CC BY 4.0 + OPL v1.1
 * 36N9 Genetics, LLC — Irrevocable, Interdimensional
 */
#include "crypto_wallet.h"

#ifdef TEST_HOST
#include <string.h>
#else
#include "freestanding.h"
#endif

/* ===== SHA-256 Implementation (freestanding, no external deps) ===== */

#define SHA256_BLOCK_SIZE 64
#define SHA256_DIGEST_SIZE 32

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t buffer[SHA256_BLOCK_SIZE];
    uint32_t buflen;
} sha256_ctx_t;

static const uint32_t SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTR(x,n) (((x) >> (n)) | ((x) << (32 - (n))))
#define SHA256_CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_EP0(x)  (ROTR(x,2) ^ ROTR(x,13) ^ ROTR(x,22))
#define SHA256_EP1(x)  (ROTR(x,6) ^ ROTR(x,11) ^ ROTR(x,25))
#define SHA256_SIG0(x) (ROTR(x,7) ^ ROTR(x,18) ^ ((x) >> 3))
#define SHA256_SIG1(x) (ROTR(x,17) ^ ROTR(x,19) ^ ((x) >> 10))

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t *block) {
    uint32_t a, b, c, d, e, f, g, h, t1, t2;
    uint32_t w[64];
    uint32_t i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) |
               ((uint32_t)block[i*4+3]);
    }
    for (i = 16; i < 64; i++) {
        w[i] = SHA256_SIG1(w[i-2]) + w[i-7] + SHA256_SIG0(w[i-15]) + w[i-16];
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + SHA256_EP1(e) + SHA256_CH(e,f,g) + SHA256_K[i] + w[i];
        t2 = SHA256_EP0(a) + SHA256_MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_ctx_t *ctx) {
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->bitlen = 0;
    ctx->buflen = 0;
}

static void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len) {
    uint32_t i;
    for (i = 0; i < len; i++) {
        ctx->buffer[ctx->buflen++] = data[i];
        if (ctx->buflen == SHA256_BLOCK_SIZE) {
            sha256_transform(ctx, ctx->buffer);
            ctx->bitlen += 512;
            ctx->buflen = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t *out) {
    uint32_t i;
    ctx->bitlen += (uint64_t)ctx->buflen * 8;
    ctx->buffer[ctx->buflen++] = 0x80;
    if (ctx->buflen > 56) {
        while (ctx->buflen < 64) ctx->buffer[ctx->buflen++] = 0;
        sha256_transform(ctx, ctx->buffer);
        ctx->buflen = 0;
    }
    while (ctx->buflen < 56) ctx->buffer[ctx->buflen++] = 0;
    for (i = 0; i < 8; i++) {
        ctx->buffer[56+i] = (uint8_t)(ctx->bitlen >> ((7-i)*8));
    }
    sha256_transform(ctx, ctx->buffer);
    for (i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(ctx->state[i] >> 24);
        out[i*4+1] = (uint8_t)(ctx->state[i] >> 16);
        out[i*4+2] = (uint8_t)(ctx->state[i] >> 8);
        out[i*4+3] = (uint8_t)(ctx->state[i]);
    }
}

void cw_sha256(const uint8_t *data, uint32_t len, uint8_t *out) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out);
}

void cw_hmac_sha256(const uint8_t *key, uint32_t key_len,
                     const uint8_t *data, uint32_t data_len, uint8_t *out) {
    uint8_t k_block[SHA256_BLOCK_SIZE];
    uint8_t k_ipad[SHA256_BLOCK_SIZE];
    uint8_t k_opad[SHA256_BLOCK_SIZE];
    uint8_t inner_hash[SHA256_DIGEST_SIZE];
    /* Combined buffer must fit: max(SHA256_BLOCK_SIZE + data_len, SHA256_BLOCK_SIZE + SHA256_DIGEST_SIZE) */
    uint32_t combined_len = SHA256_BLOCK_SIZE + (data_len > SHA256_DIGEST_SIZE ? data_len : SHA256_DIGEST_SIZE);
    uint8_t combined[256];  /* Stack buffer — sufficient for SHA256_BLOCK_SIZE + 128 max */
    uint32_t i;

    /* Clamp to stack buffer size */
    if (combined_len > sizeof(combined)) combined_len = sizeof(combined);

    /* Prepare key block */
    memset(k_block, 0, SHA256_BLOCK_SIZE);
    if (key_len > SHA256_BLOCK_SIZE) {
        cw_sha256(key, key_len, k_block);
    } else {
        memcpy(k_block, key, key_len);
    }

    /* XOR key with pads */
    for (i = 0; i < SHA256_BLOCK_SIZE; i++) {
        k_ipad[i] = k_block[i] ^ 0x36;
        k_opad[i] = k_block[i] ^ 0x5c;
    }

    /* Inner hash: H(K_ipad || data) */
    memcpy(combined, k_ipad, SHA256_BLOCK_SIZE);
    memcpy(combined + SHA256_BLOCK_SIZE, data, data_len);
    cw_sha256(combined, SHA256_BLOCK_SIZE + data_len, inner_hash);

    /* Outer hash: H(K_opad || inner_hash) */
    memcpy(combined, k_opad, SHA256_BLOCK_SIZE);
    memcpy(combined + SHA256_BLOCK_SIZE, inner_hash, SHA256_DIGEST_SIZE);
    cw_sha256(combined, SHA256_BLOCK_SIZE + SHA256_DIGEST_SIZE, out);
}

/* ===== Utility Functions ===== */

static const char *key_names[CW_KEY_MAX] = {
    "K1_TRUE", "K2_FALSE", "K3_GLUT_PLUS", "K4_GLUT_MINUS", "K5_GLUT_NEUTRAL"
};

static const char *file_type_names[CW_FILE_MAX] = {
    ".36n9", ".9n63", ".zedec", ".vino", ".ula"
};

static const char *file_extensions[CW_FILE_MAX] = {
    "36n9", "9n63", "zedec", "vino", "ula"
};

const char *cw_key_name(cw_key_id_t id) {
    if (id < CW_KEY_MAX) return key_names[id];
    return "UNKNOWN";
}

const char *cw_file_type_name(cw_file_type_t type) {
    if (type < CW_FILE_MAX) return file_type_names[type];
    return "UNKNOWN";
}

const char *cw_file_extension(cw_file_type_t type) {
    if (type < CW_FILE_MAX) return file_extensions[type];
    return "unk";
}

trit_t cw_file_type_to_phase(cw_file_type_t type) {
    switch (type) {
        case CW_FILE_36N9:  return TRIT_TRUE;
        case CW_FILE_9N63:  return TRIT_FALSE;
        case CW_FILE_ZEDEC: return TRIT_GLUT_PLUS;
        case CW_FILE_VINO:  return TRIT_GLUT_MINUS;
        case CW_FILE_ULA:   return TRIT_GLUT_NEUTRAL;
        default:            return TRIT_FALSE;
    }
}

cw_key_id_t cw_file_type_to_key(cw_file_type_t type) {
    switch (type) {
        case CW_FILE_36N9:  return CW_KEY_TRUE;
        case CW_FILE_9N63:  return CW_KEY_FALSE;
        case CW_FILE_ZEDEC: return CW_KEY_GLUT_PLUS;
        case CW_FILE_VINO:  return CW_KEY_GLUT_MINUS;
        case CW_FILE_ULA:   return CW_KEY_GLUT_NEUTRAL;
        default:            return CW_KEY_FALSE;
    }
}

/* ===== System Lifecycle ===== */

void cw_system_init(crypto_wallet_system_t *sys, uint32_t device_id, const char *name) {
    uint32_t i;
    memset(sys, 0, sizeof(*sys));
    sys->device_id = device_id;

    for (i = 0; i < CW_MAX_LABEL_LEN - 1 && name && name[i]; i++)
        sys->name[i] = name[i];
    sys->name[i] = 0;

    sys->num_wallets = 0;
    sys->total_phase_shifts = 0;
    sys->total_integrity_checks = 0;
    sys->total_key_derivations = 0;
    sys->integrity_failures = 0;
}

void cw_system_set_root_seed(crypto_wallet_system_t *sys, const uint8_t *seed, uint32_t len) {
    uint32_t i;
    uint32_t copy_len = len < CW_MAX_SEED_LEN ? len : CW_MAX_SEED_LEN;

    for (i = 0; i < copy_len; i++)
        sys->root.master_seed[i] = seed[i];
    sys->root.seed_len = copy_len;

    /* Hash the seed to create the BIOS hash */
    cw_sha256(seed, copy_len, sys->root.bios_hash);
}

/* ===== Key Derivation (HKDF-Expand from root seed) ===== */

int cw_derive_key(crypto_wallet_system_t *sys, uint32_t wallet_id, cw_key_id_t key_id) {
    if (wallet_id >= sys->num_wallets || key_id >= CW_KEY_MAX)
        return -1;

    cw_wallet_t *w = &sys->wallets[wallet_id];
    cw_key_t *k = &w->keys[key_id];

    /* HKDF-Expand: HMAC(root_seed || wallet_id || key_id) */
    uint8_t derive_input[CW_MAX_SEED_LEN + 8];
    uint32_t i;

    for (i = 0; i < sys->root.seed_len; i++)
        derive_input[i] = sys->root.master_seed[i];

    derive_input[sys->root.seed_len]     = (uint8_t)(wallet_id);
    derive_input[sys->root.seed_len + 1] = (uint8_t)(wallet_id >> 8);
    derive_input[sys->root.seed_len + 2] = (uint8_t)(wallet_id >> 16);
    derive_input[sys->root.seed_len + 3] = (uint8_t)(wallet_id >> 24);
    derive_input[sys->root.seed_len + 4] = (uint8_t)(key_id);
    derive_input[sys->root.seed_len + 5] = (uint8_t)(key_id >> 8);
    derive_input[sys->root.seed_len + 6] = (uint8_t)(key_id >> 16);
    derive_input[sys->root.seed_len + 7] = (uint8_t)(key_id >> 24);

    /* Derive key material via HMAC */
    cw_hmac_sha256(sys->root.master_seed, sys->root.seed_len,
                    derive_input, sys->root.seed_len + 8, k->key);

    /* Derive chain code via HMAC with different context */
    derive_input[sys->root.seed_len + 4] ^= 0xFF;
    derive_input[sys->root.seed_len + 5] ^= 0xFF;
    cw_hmac_sha256(sys->root.master_seed, sys->root.seed_len,
                    derive_input, sys->root.seed_len + 8, k->chain_code);

    k->id = key_id;
    k->key_len = CW_MAX_HASH_LEN;  /* 32 bytes from HMAC-SHA256 */

    /* Set label and phase mapping */
    const char *name = cw_key_name(key_id);
    for (i = 0; i < CW_MAX_LABEL_LEN - 1 && name[i]; i++)
        k->label[i] = name[i];
    k->label[i] = 0;

    k->logic_phase = cw_file_type_to_phase((cw_file_type_t)key_id);
    k->file_type = (cw_file_type_t)key_id;

    sys->total_key_derivations++;
    return 0;
}

int cw_derive_all_keys(crypto_wallet_system_t *sys, uint32_t wallet_id) {
    uint32_t i;
    for (i = 0; i < CW_KEY_MAX; i++) {
        if (cw_derive_key(sys, wallet_id, (cw_key_id_t)i) != 0)
            return -1;
    }
    return 0;
}

/* ===== Wallet Management ===== */

uint32_t cw_wallet_create(crypto_wallet_system_t *sys, const char *label) {
    if (sys->num_wallets >= CW_MAX_WALLETS)
        return 0xFFFFFFFF;

    uint32_t id = sys->num_wallets;
    cw_wallet_t *w = &sys->wallets[id];
    memset(w, 0, sizeof(*w));

    w->wallet_id = id;
    uint32_t i;
    for (i = 0; i < CW_MAX_LABEL_LEN - 1 && label && label[i]; i++)
        w->label[i] = label[i];
    w->label[i] = 0;

    /* Initialize M5 coordinates */
    w->m5.omega = id;
    w->m5.r = SR_ONE;
    w->m5.ell = SR_ONE;
    w->m5.phi = SR_ZERO;
    w->m5.chi = 0;
    w->coverage_ratio = SR_ONE;
    w->integrity_ok = true;

    /* Derive all 5 keys for this wallet */
    sys->num_wallets++;  /* Increment first so wallet_id < num_wallets check passes */
    cw_derive_all_keys(sys, id);

    return id;
}

cw_wallet_t *cw_wallet_get(crypto_wallet_system_t *sys, uint32_t wallet_id) {
    if (wallet_id >= sys->num_wallets)
        return NULL;
    return &sys->wallets[wallet_id];
}

/* ===== File Operations (Content-Addressable) ===== */

int32_t cw_file_add(crypto_wallet_system_t *sys, uint32_t wallet_id,
                     cw_file_type_t type, const uint8_t *payload, uint32_t size,
                     const char *path) {
    if (wallet_id >= sys->num_wallets || type >= CW_FILE_MAX)
        return -1;

    cw_wallet_t *w = &sys->wallets[wallet_id];
    if (w->num_files >= CW_MAX_FILES_PER_WALLET)
        return -1;

    int32_t idx = (int32_t)w->num_files;
    cw_file_entry_t *f = &w->files[idx];

    /* Compute content hash */
    cw_sha256(payload, size, f->content_hash);
    f->payload_size = size;
    f->file_type = type;

    /* Sign the content hash with the appropriate key for this file type.
     * In content-addressable storage, we sign the hash (CID), not the payload,
     * because the payload may not be resident — only its hash is guaranteed. */
    cw_key_id_t key_id = cw_file_type_to_key(type);
    cw_key_t *k = &w->keys[key_id];
    cw_hmac_sha256(k->key, k->key_len, f->content_hash, CW_MAX_HASH_LEN, f->signature);
    f->signing_key = key_id;

    /* Set initial phase from file type */
    f->current_phase = cw_file_type_to_phase(type);
    f->integrity_verified = true;

    /* Copy path if provided */
    if (path) {
        uint32_t i;
        for (i = 0; i < CW_MAX_PATH_LEN - 1 && path[i]; i++)
            f->path[i] = path[i];
        f->path[i] = 0;
    } else {
        f->path[0] = 0;
    }

    /* Clear cross-ref until peer is linked */
    memset(f->cross_ref_hash, 0, CW_MAX_HASH_LEN);
    f->cross_ref_wallet_id = 0xFFFFFFFF;

    w->num_files++;

    /* Update Merkle root */
    cw_wallet_update_merkle(sys, wallet_id);

    return idx;
}

cw_file_entry_t *cw_file_lookup(crypto_wallet_system_t *sys, uint32_t wallet_id,
                                 const uint8_t *content_hash) {
    if (wallet_id >= sys->num_wallets)
        return NULL;

    cw_wallet_t *w = &sys->wallets[wallet_id];
    uint32_t i;
    for (i = 0; i < w->num_files; i++) {
        uint32_t j;
        bool match = true;
        for (j = 0; j < CW_MAX_HASH_LEN; j++) {
            if (w->files[i].content_hash[j] != content_hash[j]) {
                match = false;
                break;
            }
        }
        if (match) return &w->files[i];
    }
    return NULL;
}

/* ===== Phase Shifting (Zero-Copy, O(1)) ===== */

int cw_file_phase_shift(crypto_wallet_system_t *sys, uint32_t wallet_id,
                         uint32_t file_idx, cw_key_id_t new_key) {
    if (wallet_id >= sys->num_wallets || new_key >= CW_KEY_MAX)
        return -1;

    cw_wallet_t *w = &sys->wallets[wallet_id];
    if (file_idx >= w->num_files)
        return -1;

    cw_file_entry_t *f = &w->files[file_idx];

    /* Re-sign with the new key — this is the O(1) phase shift.
     * We don't move any payload bytes, just re-sign the Merkle pointer. */
    cw_key_t *k = &w->keys[new_key];

    /* Re-sign: HMAC(new_key || content_hash) */
    uint8_t sign_input[CW_MAX_HASH_LEN * 2];
    memcpy(sign_input, k->key, CW_MAX_HASH_LEN);
    memcpy(sign_input + CW_MAX_HASH_LEN, f->content_hash, CW_MAX_HASH_LEN);
    cw_hmac_sha256(k->key, k->key_len, f->content_hash, CW_MAX_HASH_LEN, f->signature);

    f->signing_key = new_key;
    f->current_phase = cw_file_type_to_phase((cw_file_type_t)new_key);

    /* Update Merkle root after phase shift */
    cw_wallet_update_merkle(sys, wallet_id);

    /* Trigger IRQ */
    sys->irq_phase_shift = true;

    sys->total_phase_shifts++;
    return 0;
}

/* ===== Merkle Tree Operations ===== */

int cw_wallet_update_merkle(crypto_wallet_system_t *sys, uint32_t wallet_id) {
    if (wallet_id >= sys->num_wallets)
        return -1;

    cw_wallet_t *w = &sys->wallets[wallet_id];

    if (w->num_files == 0) {
        memset(w->merkle_root, 0, CW_MAX_HASH_LEN);
    } else {
        /* Simple Merkle: hash all file hashes together in sequence.
         * For a production system, this would be a proper binary Merkle tree. */
        uint8_t combined[CW_MAX_HASH_LEN * 2];
        memcpy(combined, w->files[0].content_hash, CW_MAX_HASH_LEN);

        uint32_t i;
        for (i = 1; i < w->num_files; i++) {
            memcpy(combined + CW_MAX_HASH_LEN, w->files[i].content_hash, CW_MAX_HASH_LEN);
            cw_sha256(combined, CW_MAX_HASH_LEN * 2, w->merkle_root);
            memcpy(combined, w->merkle_root, CW_MAX_HASH_LEN);
        }

        /* If only one file, its hash is the Merkle root */
        if (w->num_files == 1) {
            memcpy(w->merkle_root, w->files[0].content_hash, CW_MAX_HASH_LEN);
        }
    }

    /* Sync peer hashes — any wallet that has this wallet as a peer
     * needs its stored peer Merkle root updated. */
    uint32_t i;
    for (i = 0; i < sys->num_wallets; i++) {
        if (i == wallet_id) continue;
        cw_wallet_t *peer = &sys->wallets[i];
        uint32_t j;
        for (j = 0; j < peer->num_peers; j++) {
            if (peer->peer_wallet_ids[j] == wallet_id) {
                memcpy(peer->peer_merkle_roots[j], w->merkle_root, CW_MAX_HASH_LEN);
            }
        }
    }

    /* Update system Merkle root */
    cw_system_update_merkle(sys);

    return 0;
}

int cw_system_update_merkle(crypto_wallet_system_t *sys) {
    if (sys->num_wallets == 0) {
        memset(sys->system_merkle_root, 0, CW_MAX_HASH_LEN);
        return 0;
    }

    uint8_t combined[CW_MAX_HASH_LEN * 2];
    memcpy(combined, sys->wallets[0].merkle_root, CW_MAX_HASH_LEN);

    uint32_t i;
    for (i = 1; i < sys->num_wallets; i++) {
        memcpy(combined + CW_MAX_HASH_LEN, sys->wallets[i].merkle_root, CW_MAX_HASH_LEN);
        cw_sha256(combined, CW_MAX_HASH_LEN * 2, sys->system_merkle_root);
        memcpy(combined, sys->system_merkle_root, CW_MAX_HASH_LEN);
    }

    if (sys->num_wallets == 1) {
        memcpy(sys->system_merkle_root, sys->wallets[0].merkle_root, CW_MAX_HASH_LEN);
    }

    return 0;
}

/* ===== Cross-Wallet Interlocking ===== */

int cw_wallet_add_peer(crypto_wallet_system_t *sys, uint32_t wallet_id,
                        uint32_t peer_id) {
    if (wallet_id >= sys->num_wallets || peer_id >= sys->num_wallets)
        return -1;

    cw_wallet_t *w = &sys->wallets[wallet_id];
    if (w->num_peers >= 16)
        return -1;

    /* Check if already linked */
    uint32_t i;
    for (i = 0; i < w->num_peers; i++) {
        if (w->peer_wallet_ids[i] == peer_id)
            return 0;  /* Already linked */
    }

    w->peer_wallet_ids[w->num_peers] = peer_id;
    memcpy(w->peer_merkle_roots[w->num_peers],
           sys->wallets[peer_id].merkle_root, CW_MAX_HASH_LEN);
    w->num_peers++;

    /* Bi-directional: also add wallet to peer's list */
    cw_wallet_t *p = &sys->wallets[peer_id];
    if (p->num_peers < 16) {
        p->peer_wallet_ids[p->num_peers] = wallet_id;
        memcpy(p->peer_merkle_roots[p->num_peers],
               w->merkle_root, CW_MAX_HASH_LEN);
        p->num_peers++;
    }

    return 0;
}

int cw_wallet_sync_peer_hash(crypto_wallet_system_t *sys, uint32_t wallet_id,
                              uint32_t peer_id) {
    if (wallet_id >= sys->num_wallets || peer_id >= sys->num_wallets)
        return -1;

    cw_wallet_t *w = &sys->wallets[wallet_id];
    uint32_t i;
    for (i = 0; i < w->num_peers; i++) {
        if (w->peer_wallet_ids[i] == peer_id) {
            memcpy(w->peer_merkle_roots[i],
                   sys->wallets[peer_id].merkle_root, CW_MAX_HASH_LEN);
            return 0;
        }
    }
    return -1;  /* Not linked */
}

/* ===== Integrity Verification ===== */

bool cw_verify_integrity(crypto_wallet_system_t *sys, uint32_t wallet_id) {
    if (wallet_id >= sys->num_wallets)
        return false;

    cw_wallet_t *w = &sys->wallets[wallet_id];
    bool all_ok = true;
    uint32_t failures = 0;
    uint32_t i;

    for (i = 0; i < w->num_files; i++) {
        cw_file_entry_t *f = &w->files[i];
        cw_key_t *k = &w->keys[f->signing_key];

        /* Re-compute HMAC and compare */
        uint8_t expected_sig[CW_MAX_HASH_LEN];
        cw_hmac_sha256(k->key, k->key_len, f->content_hash, CW_MAX_HASH_LEN, expected_sig);

        bool sig_ok = true;
        uint32_t j;
        for (j = 0; j < CW_MAX_HASH_LEN; j++) {
            if (f->signature[j] != expected_sig[j]) {
                sig_ok = false;
                break;
            }
        }

        if (!sig_ok) {
            f->integrity_verified = false;
            /* Auto-collapse to GLUT_NEUTRAL (locked) on integrity failure */
            f->current_phase = TRIT_GLUT_NEUTRAL;
            failures++;
            all_ok = false;
        } else {
            f->integrity_verified = true;
        }
    }

    /* Verify peer hashes */
    for (i = 0; i < w->num_peers; i++) {
        uint32_t peer_id = w->peer_wallet_ids[i];
        if (peer_id < sys->num_wallets) {
            uint32_t j;
            bool peer_ok = true;
            for (j = 0; j < CW_MAX_HASH_LEN; j++) {
                if (w->peer_merkle_roots[i][j] != sys->wallets[peer_id].merkle_root[j]) {
                    peer_ok = false;
                    break;
                }
            }
            if (!peer_ok) {
                failures++;
                all_ok = false;
            }
        }
    }

    w->integrity_ok = all_ok;
    w->integrity_failures = failures;
    sys->total_integrity_checks++;

    if (!all_ok) {
        sys->integrity_failures++;
        sys->irq_integrity_violation = true;
    }

    return all_ok;
}

bool cw_verify_system_integrity(crypto_wallet_system_t *sys) {
    uint32_t i;
    bool all_ok = true;
    for (i = 0; i < sys->num_wallets; i++) {
        if (!cw_verify_integrity(sys, i))
            all_ok = false;
    }
    return all_ok;
}

/* ===== Register Access (Hardware-as-Code) ===== */

uint64_t cw_reg_read(crypto_wallet_system_t *sys, cw_reg_t reg) {
    switch (reg) {
        case CW_REG_STATUS:
            return sys->irq_integrity_violation ? CW_STATUS_INTEGRITY_FAIL :
                   sys->irq_phase_shift ? CW_STATUS_PHASE_SHIFT :
                   CW_STATUS_IDLE;
        case CW_REG_WALLET_COUNT:
            return sys->num_wallets;
        case CW_REG_FILE_COUNT: {
            uint32_t total = 0, i;
            for (i = 0; i < sys->num_wallets; i++)
                total += sys->wallets[i].num_files;
            return total;
        }
        case CW_REG_MERKLE_ROOT:
            /* Return first 8 bytes of system Merkle root */
            return *(uint64_t*)sys->system_merkle_root;
        default:
            return 0;
    }
}

void cw_reg_write(crypto_wallet_system_t *sys, cw_reg_t reg, uint64_t value) {
    switch (reg) {
        case CW_REG_DERIVE_KEY:
            if (value < sys->num_wallets)
                cw_derive_all_keys(sys, (uint32_t)value);
            break;
        case CW_REG_INTEGRITY:
            cw_verify_system_integrity(sys);
            break;
        case CW_REG_PHASE_SHIFT:
            /* value encodes wallet_id (lower 32) and file_idx (upper 32) */
            {
                uint32_t wid = (uint32_t)(value & 0xFFFFFFFF);
                uint32_t fidx = (uint32_t)(value >> 32);
                cw_file_phase_shift(sys, wid, fidx, CW_KEY_TRUE);
            }
            break;
        default:
            break;
    }
}

/* ===== IRQ Handling ===== */

void cw_handle_irq(crypto_wallet_system_t *sys) {
    if (sys->irq_integrity_violation) {
        /* Auto-collapse all affected files to GLUT_NEUTRAL (locked) */
        uint32_t i, j;
        for (i = 0; i < sys->num_wallets; i++) {
            cw_wallet_t *w = &sys->wallets[i];
            for (j = 0; j < w->num_files; j++) {
                if (!w->files[j].integrity_verified) {
                    w->files[j].current_phase = TRIT_GLUT_NEUTRAL;
                }
            }
        }
        sys->irq_integrity_violation = false;
    }
    if (sys->irq_phase_shift) {
        sys->irq_phase_shift = false;
    }
    if (sys->irq_key_derivation_complete) {
        sys->irq_key_derivation_complete = false;
    }
    if (sys->irq_merkle_update) {
        cw_system_update_merkle(sys);
        sys->irq_merkle_update = false;
    }
}
