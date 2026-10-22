/* diskcrypt.c — full-disk encryption. See diskcrypt.h.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "diskcrypt.h"
#include "../robin_debanks/sha256.h"
#include "../tls/aead.h"
#include "../ident/ident.h"

/* Header layout (little-endian):
 *   0  magic          4  version        8  cipher        12 sector size
 *   16 data offset    20 data sectors   24 generation    28 reserved
 *   32 uuid[16]
 *   48 slots, 128 bytes each:
 *        0 type  4 m_kib  8 t  12 p  16 salt[32]  48 nonce[12]
 *        60 wrapped key[32]  92 tag[16]  108 reserved[20]
 *   4064 SHA-256 of bytes 0..4063 */
#define OFF_SLOTS  48u
#define SLOT_BYTES 128u
#define OFF_SEAL   (DISKCRYPT_HDR_BYTES - 32u)

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

static void cpy(uint8_t *d, const uint8_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static void wipe(void *p, uint32_t n)
{
    volatile uint8_t *v = (volatile uint8_t *) p;
    while (n--) *v++ = 0;
}

static void copy_obj(void *d, const void *s, uint32_t n)
{
    cpy((uint8_t *) d, (const uint8_t *) s, n);
}

/* Scratch for the header's on-disk form: 4 KiB is too much for a small
 * kernel stack, and the module is not re-entrant anyway. */
static uint8_t g_hdr_buf[DISKCRYPT_HDR_BYTES];

void diskcrypt_header_encode(const diskcrypt_header_t *h, uint8_t out[DISKCRYPT_HDR_BYTES])
{
    for (uint32_t i = 0; i < DISKCRYPT_HDR_BYTES; i++) out[i] = 0;
    put32(out + 0, DISKCRYPT_MAGIC);
    put32(out + 4, DISKCRYPT_VERSION);
    put32(out + 8, DISKCRYPT_CIPHER);
    put32(out + 12, BLOCKDEV_SECTOR_SIZE);
    put32(out + 16, DISKCRYPT_DATA_OFFSET);
    put32(out + 20, h->data_sectors);
    put32(out + 24, h->generation);
    cpy(out + 32, h->uuid, 16);
    for (uint32_t i = 0; i < DISKCRYPT_SLOTS; i++) {
        const diskcrypt_slot_t *s = &h->slots[i];
        uint8_t *p = out + OFF_SLOTS + SLOT_BYTES * i;
        if (s->type == DISKCRYPT_SLOT_EMPTY) continue;
        put32(p + 0, s->type);
        put32(p + 4, s->m_kib);
        put32(p + 8, s->t);
        put32(p + 12, s->p);
        cpy(p + 16, s->salt, 32);
        cpy(p + 48, s->nonce, 12);
        cpy(p + 60, s->wrapped, 32);
        cpy(p + 92, s->tag, 16);
    }
    sha256(out, OFF_SEAL, out + OFF_SEAL);
}

static void header_clear(diskcrypt_header_t *h)
{
    wipe(h, sizeof *h);
}

bool diskcrypt_header_decode(const uint8_t in[DISKCRYPT_HDR_BYTES], diskcrypt_header_t *h)
{
    header_clear(h);
    if (get32(in) != DISKCRYPT_MAGIC || get32(in + 4) != DISKCRYPT_VERSION) return false;
    uint8_t seal[32], diff = 0;
    sha256(in, OFF_SEAL, seal);
    for (int i = 0; i < 32; i++) diff |= seal[i] ^ in[OFF_SEAL + i];
    if (diff) return false;
    if (get32(in + 8) != DISKCRYPT_CIPHER || get32(in + 12) != BLOCKDEV_SECTOR_SIZE ||
        get32(in + 16) != DISKCRYPT_DATA_OFFSET || get32(in + 20) == 0)
        return false;
    h->data_sectors = get32(in + 20);
    h->generation = get32(in + 24);
    cpy(h->uuid, in + 32, 16);
    for (uint32_t i = 0; i < DISKCRYPT_SLOTS; i++) {
        diskcrypt_slot_t *s = &h->slots[i];
        const uint8_t *p = in + OFF_SLOTS + SLOT_BYTES * i;
        uint32_t type = get32(p);
        if (type == DISKCRYPT_SLOT_EMPTY) continue;
        if (type != DISKCRYPT_SLOT_PASSPHRASE && type != DISKCRYPT_SLOT_RECOVERY) {
            header_clear(h);
            return false;
        }
        s->type = type;
        s->m_kib = get32(p + 4);
        s->t = get32(p + 8);
        s->p = get32(p + 12);
        cpy(s->salt, p + 16, 32);
        cpy(s->nonce, p + 48, 12);
        cpy(s->wrapped, p + 60, 32);
        cpy(s->tag, p + 92, 16);
    }
    return true;
}

/* ---- header I/O: two copies, newest valid wins */

static bool read_copy(block_device_t *lower, uint32_t lba, diskcrypt_header_t *h)
{
    for (uint32_t i = 0; i < DISKCRYPT_HDR_SECTORS; i++)
        if (blockdev_read(lower, lba + i, g_hdr_buf + BLOCKDEV_SECTOR_SIZE * i) != 0) {
            header_clear(h);
            return false;
        }
    return diskcrypt_header_decode(g_hdr_buf, h);
}

static diskcrypt_status_t read_header(block_device_t *lower, uint32_t base, diskcrypt_header_t *h)
{
    static diskcrypt_header_t b;
    bool okA = read_copy(lower, base, h);
    bool okB = read_copy(lower, base + DISKCRYPT_HDR_SECTORS, &b);
    if (!okA && !okB) return DISKCRYPT_ERR_NO_VOLUME;
    /* B wins when A is bad, or when B is newer (A was being rewritten). The
     * generation is compared with wraparound. */
    if (!okA || (okB && (int32_t) (b.generation - h->generation) > 0)) {
        copy_obj(h, &b, sizeof b);
    }
    header_clear(&b);
    return DISKCRYPT_OK;
}

static diskcrypt_status_t write_copy(block_device_t *lower, uint32_t lba)
{
    for (uint32_t i = 0; i < DISKCRYPT_HDR_SECTORS; i++)
        if (blockdev_write(lower, lba + i, g_hdr_buf + BLOCKDEV_SECTOR_SIZE * i) != 0)
            return DISKCRYPT_ERR_IO;
    return DISKCRYPT_OK;
}

/* Write the next generation: copy A, then copy B. */
static diskcrypt_status_t write_header(diskcrypt_volume_t *v)
{
    v->hdr.generation++;
    diskcrypt_header_encode(&v->hdr, g_hdr_buf);
    diskcrypt_status_t st = write_copy(v->lower, v->base);
    if (st == DISKCRYPT_OK) st = write_copy(v->lower, v->base + DISKCRYPT_HDR_SECTORS);
    wipe(g_hdr_buf, sizeof g_hdr_buf);
    return st;
}

bool diskcrypt_probe(block_device_t *lower, uint32_t base, uint32_t *data_sectors)
{
    static diskcrypt_header_t h;
    bool ok = read_header(lower, base, &h) == DISKCRYPT_OK;
    if (data_sectors) *data_sectors = ok ? h.data_sectors : 0;
    header_clear(&h);
    return ok;
}

/* ---- key slots */

/* What a slot's AEAD binds: the header's fixed fields and the slot's own
 * index, type and KDF parameters. */
#define AAD_LEN 64u
static void slot_aad(const diskcrypt_header_t *h, uint32_t idx, const diskcrypt_slot_t *s,
                     uint8_t aad[AAD_LEN])
{
    for (uint32_t i = 0; i < AAD_LEN; i++) aad[i] = 0;
    put32(aad + 0, DISKCRYPT_MAGIC);
    put32(aad + 4, DISKCRYPT_VERSION);
    put32(aad + 8, DISKCRYPT_CIPHER);
    put32(aad + 12, BLOCKDEV_SECTOR_SIZE);
    put32(aad + 16, DISKCRYPT_DATA_OFFSET);
    put32(aad + 20, h->data_sectors);
    cpy(aad + 24, h->uuid, 16);
    put32(aad + 40, idx);
    put32(aad + 44, s->type);
    put32(aad + 48, s->m_kib);
    put32(aad + 52, s->t);
    put32(aad + 56, s->p);
}

static diskcrypt_status_t check_kdf(diskcrypt_slot_type_t type, uint32_t m_kib, uint32_t t,
                                    const diskcrypt_work_t *work)
{
    if (type != DISKCRYPT_SLOT_PASSPHRASE && type != DISKCRYPT_SLOT_RECOVERY)
        return DISKCRYPT_ERR_ARG;
    /* A recovery key is 256 random bits, so it needs no stretching; a
     * passphrase does. */
    uint32_t min_m = type == DISKCRYPT_SLOT_PASSPHRASE ? DISKCRYPT_MIN_M_KIB : 8u;
    if (m_kib < min_m || t < DISKCRYPT_MIN_T) return DISKCRYPT_ERR_WEAK_KDF;
    if (m_kib > DISKCRYPT_MAX_M_KIB || t > DISKCRYPT_MAX_T) return DISKCRYPT_ERR_ARG;
    if (!work || !work->mem || work->mem_blocks < m_kib) return DISKCRYPT_ERR_MEMORY;
    return DISKCRYPT_OK;
}

static diskcrypt_status_t derive_kek(const diskcrypt_slot_t *s, const uint8_t *secret,
                                     uint32_t secret_len, const diskcrypt_work_t *work,
                                     uint8_t kek[32])
{
    id_kdf_params_t kp;
    kp.m_kib = s->m_kib;
    kp.t = s->t;
    kp.p = s->p;
    if (id_argon2id(&kp, secret, secret_len, s->salt, 32, 0, 0, 0, 0, work->mem, work->mem_blocks,
                    kek, 32) != ID_OK)
        return DISKCRYPT_ERR_MEMORY;
    return DISKCRYPT_OK;
}

static diskcrypt_status_t fill_slot(diskcrypt_volume_t *v, uint32_t idx, diskcrypt_slot_type_t type,
                                    const uint8_t *secret, uint32_t secret_len, uint32_t m_kib,
                                    uint32_t t, const diskcrypt_rng_t *rng,
                                    const diskcrypt_work_t *work)
{
    diskcrypt_slot_t s;
    uint8_t kek[32], aad[AAD_LEN];
    wipe(&s, sizeof s);
    s.type = type;
    s.m_kib = m_kib;
    s.t = t;
    s.p = 1;
    rng->fill(rng->ctx, s.salt, 32);
    rng->fill(rng->ctx, s.nonce, 12);
    diskcrypt_status_t st = derive_kek(&s, secret, secret_len, work, kek);
    if (st != DISKCRYPT_OK) return st;
    slot_aad(&v->hdr, idx, &s, aad);
    aead_seal(kek, s.nonce, aad, AAD_LEN, v->vkey, s.wrapped, 32, s.tag);
    wipe(kek, sizeof kek);
    copy_obj(&v->hdr.slots[idx], &s, sizeof s);
    wipe(&s, sizeof s);
    return DISKCRYPT_OK;
}

/* ---- the decrypted block device */

static void sector_tweak(uint32_t lba, uint8_t tweak[ADIANTUM_TWEAK_LEN])
{
    for (uint32_t i = 0; i < ADIANTUM_TWEAK_LEN; i++) tweak[i] = 0;
    put32(tweak, lba);
}

static int vol_read(block_device_t *dev, uint32_t lba, uint8_t *buffer)
{
    diskcrypt_volume_t *v = (diskcrypt_volume_t *) dev->driver_data;
    uint8_t tweak[ADIANTUM_TWEAK_LEN];
    if (!v || !v->unlocked || lba >= v->hdr.data_sectors) return -1;
    if (blockdev_read(v->lower, v->base + DISKCRYPT_DATA_OFFSET + lba, buffer) != 0) return -1;
    sector_tweak(lba, tweak);
    adiantum_decrypt(&v->cipher, tweak, buffer, buffer, BLOCKDEV_SECTOR_SIZE);
    return 0;
}

static int vol_write(block_device_t *dev, uint32_t lba, const uint8_t *buffer)
{
    diskcrypt_volume_t *v = (diskcrypt_volume_t *) dev->driver_data;
    uint8_t tweak[ADIANTUM_TWEAK_LEN];
    if (!v || !v->unlocked || lba >= v->hdr.data_sectors) return -1;
    sector_tweak(lba, tweak);
    adiantum_encrypt(&v->cipher, tweak, buffer, v->buf, BLOCKDEV_SECTOR_SIZE);
    int rc = blockdev_write(v->lower, v->base + DISKCRYPT_DATA_OFFSET + lba, v->buf);
    wipe(v->buf, sizeof v->buf);
    return rc;
}

static void open_device(diskcrypt_volume_t *v)
{
    adiantum_init(&v->cipher, v->vkey);
    v->unlocked = true;
    blockdev_init(&v->dev, vol_read, vol_write, v->hdr.data_sectors, "ZXV encrypted volume");
    v->dev.driver_data = v;
}

void diskcrypt_lock(diskcrypt_volume_t *v)
{
    adiantum_wipe(&v->cipher);
    wipe(v->vkey, sizeof v->vkey);
    wipe(v->buf, sizeof v->buf);
    v->unlocked = false;
    v->dev.present = false;
    v->dev.driver_data = 0;
}

static void volume_reset(diskcrypt_volume_t *v, block_device_t *lower, uint32_t base)
{
    wipe(v, sizeof *v);
    v->lower = lower;
    v->base = base;
}

/* ---- the API */

diskcrypt_status_t diskcrypt_format(diskcrypt_volume_t *v, block_device_t *lower, uint32_t base,
                                    uint32_t count, diskcrypt_slot_type_t type,
                                    const uint8_t *secret, uint32_t secret_len, uint32_t m_kib,
                                    uint32_t t, const diskcrypt_rng_t *rng,
                                    const diskcrypt_work_t *work)
{
    if (!v || !lower || !lower->present || !rng || !rng->fill || !secret || secret_len == 0 ||
        secret_len > DISKCRYPT_SECRET_MAX)
        return DISKCRYPT_ERR_ARG;
    if (count <= DISKCRYPT_DATA_OFFSET || base > lower->total_sectors ||
        count > lower->total_sectors - base)
        return DISKCRYPT_ERR_ARG;
    diskcrypt_status_t st = check_kdf(type, m_kib, t, work);
    if (st != DISKCRYPT_OK) return st;

    volume_reset(v, lower, base);
    v->hdr.data_sectors = count - DISKCRYPT_DATA_OFFSET;
    rng->fill(rng->ctx, v->hdr.uuid, 16);
    rng->fill(rng->ctx, v->vkey, 32);
    st = fill_slot(v, 0, type, secret, secret_len, m_kib, t, rng, work);
    if (st == DISKCRYPT_OK) st = write_header(v);
    if (st != DISKCRYPT_OK) {
        diskcrypt_lock(v);
        return st;
    }
    open_device(v);
    return DISKCRYPT_OK;
}

diskcrypt_status_t diskcrypt_unlock(diskcrypt_volume_t *v, block_device_t *lower, uint32_t base,
                                    const uint8_t *secret, uint32_t secret_len,
                                    const diskcrypt_work_t *work, uint32_t *slot_out)
{
    if (!v || !lower || !secret || secret_len == 0 || secret_len > DISKCRYPT_SECRET_MAX)
        return DISKCRYPT_ERR_ARG;
    volume_reset(v, lower, base);
    diskcrypt_status_t st = read_header(lower, base, &v->hdr);
    if (st != DISKCRYPT_OK) return st;
    /* The header must describe a volume that fits on this device. */
    if (base > lower->total_sectors || lower->total_sectors - base < DISKCRYPT_DATA_OFFSET ||
        v->hdr.data_sectors > lower->total_sectors - base - DISKCRYPT_DATA_OFFSET) {
        header_clear(&v->hdr);
        return DISKCRYPT_ERR_NO_VOLUME;
    }

    diskcrypt_status_t result = DISKCRYPT_ERR_WRONG_KEY;
    for (uint32_t i = 0; i < DISKCRYPT_SLOTS; i++) {
        const diskcrypt_slot_t *s = &v->hdr.slots[i];
        if (s->type == DISKCRYPT_SLOT_EMPTY) continue;
        /* A slot with parameters below the floor was not written by
         * diskcrypt: skip it rather than make the secret cheap to test. */
        if (check_kdf((diskcrypt_slot_type_t) s->type, s->m_kib, s->t, work) ==
                DISKCRYPT_ERR_WEAK_KDF ||
            s->p != 1)
            continue;
        if (!work || !work->mem || work->mem_blocks < s->m_kib) {
            result = DISKCRYPT_ERR_MEMORY;
            continue;
        }
        uint8_t kek[32], aad[AAD_LEN];
        if (derive_kek(s, secret, secret_len, work, kek) != DISKCRYPT_OK) continue;
        slot_aad(&v->hdr, i, s, aad);
        bool ok = aead_open(kek, s->nonce, aad, AAD_LEN, s->wrapped, v->vkey, 32, s->tag);
        wipe(kek, sizeof kek);
        if (ok) {
            if (slot_out) *slot_out = i;
            open_device(v);
            return DISKCRYPT_OK;
        }
    }
    diskcrypt_lock(v);
    return result;
}

diskcrypt_status_t diskcrypt_add_slot(diskcrypt_volume_t *v, diskcrypt_slot_type_t type,
                                      const uint8_t *secret, uint32_t secret_len, uint32_t m_kib,
                                      uint32_t t, const diskcrypt_rng_t *rng,
                                      const diskcrypt_work_t *work, uint32_t *slot_out)
{
    if (!v || !rng || !rng->fill || !secret || secret_len == 0 || secret_len > DISKCRYPT_SECRET_MAX)
        return DISKCRYPT_ERR_ARG;
    if (!v->unlocked) return DISKCRYPT_ERR_LOCKED;
    diskcrypt_status_t st = check_kdf(type, m_kib, t, work);
    if (st != DISKCRYPT_OK) return st;
    uint32_t idx = DISKCRYPT_SLOTS;
    for (uint32_t i = 0; i < DISKCRYPT_SLOTS; i++)
        if (v->hdr.slots[i].type == DISKCRYPT_SLOT_EMPTY) {
            idx = i;
            break;
        }
    if (idx == DISKCRYPT_SLOTS) return DISKCRYPT_ERR_FULL;
    st = fill_slot(v, idx, type, secret, secret_len, m_kib, t, rng, work);
    if (st == DISKCRYPT_OK) st = write_header(v);
    if (st != DISKCRYPT_OK) {
        wipe(&v->hdr.slots[idx], sizeof v->hdr.slots[idx]);
        return st;
    }
    if (slot_out) *slot_out = idx;
    return DISKCRYPT_OK;
}

uint32_t diskcrypt_slots_used(const diskcrypt_volume_t *v)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < DISKCRYPT_SLOTS; i++)
        if (v->hdr.slots[i].type != DISKCRYPT_SLOT_EMPTY) n++;
    return n;
}

diskcrypt_status_t diskcrypt_remove_slot(diskcrypt_volume_t *v, uint32_t slot)
{
    if (!v || slot >= DISKCRYPT_SLOTS) return DISKCRYPT_ERR_ARG;
    if (!v->unlocked) return DISKCRYPT_ERR_LOCKED;
    if (v->hdr.slots[slot].type == DISKCRYPT_SLOT_EMPTY) return DISKCRYPT_ERR_ARG;
    if (diskcrypt_slots_used(v) <= 1) return DISKCRYPT_ERR_LAST_SLOT;
    diskcrypt_slot_t saved;
    copy_obj(&saved, &v->hdr.slots[slot], sizeof saved);
    wipe(&v->hdr.slots[slot], sizeof v->hdr.slots[slot]);
    diskcrypt_status_t st = write_header(v);
    if (st != DISKCRYPT_OK) copy_obj(&v->hdr.slots[slot], &saved, sizeof saved);
    wipe(&saved, sizeof saved);
    return st;
}

diskcrypt_status_t diskcrypt_erase(diskcrypt_volume_t *v, block_device_t *lower, uint32_t base)
{
    if (!lower || !lower->present || base > lower->total_sectors ||
        lower->total_sectors - base < DISKCRYPT_DATA_OFFSET)
        return DISKCRYPT_ERR_ARG;
    if (v) diskcrypt_lock(v);
    for (uint32_t i = 0; i < DISKCRYPT_HDR_BYTES; i++) g_hdr_buf[i] = 0;
    diskcrypt_status_t a = write_copy(lower, base);
    diskcrypt_status_t b = write_copy(lower, base + DISKCRYPT_HDR_SECTORS);
    return a != DISKCRYPT_OK ? a : b;
}

/* ---- recovery keys */

static const char HEX[] = "0123456789ABCDEF";

void diskcrypt_recovery_key(const diskcrypt_rng_t *rng, uint8_t key[DISKCRYPT_RECOVERY_LEN],
                            char text[DISKCRYPT_RECOVERY_TEXT])
{
    rng->fill(rng->ctx, key, DISKCRYPT_RECOVERY_LEN);
    uint32_t o = 0;
    for (uint32_t i = 0; i < DISKCRYPT_RECOVERY_LEN; i++) {
        if (i && (i & 3) == 0) text[o++] = '-';
        text[o++] = HEX[key[i] >> 4];
        text[o++] = HEX[key[i] & 15];
    }
    text[o] = 0;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool diskcrypt_recovery_parse(const char *text, uint8_t key[DISKCRYPT_RECOVERY_LEN])
{
    uint32_t n = 0;
    if (!text) return false;
    for (const char *p = text; *p; p++) {
        if (*p == '-' || *p == ' ') continue;
        int d = hexval(*p);
        if (d < 0 || n >= 2 * DISKCRYPT_RECOVERY_LEN) {
            wipe(key, DISKCRYPT_RECOVERY_LEN);
            return false;
        }
        if (n & 1)
            key[n >> 1] = (uint8_t) (key[n >> 1] | d);
        else
            key[n >> 1] = (uint8_t) (d << 4);
        n++;
    }
    if (n != 2 * DISKCRYPT_RECOVERY_LEN) {
        wipe(key, DISKCRYPT_RECOVERY_LEN);
        return false;
    }
    return true;
}

const char *diskcrypt_status_str(diskcrypt_status_t s)
{
    switch (s) {
    case DISKCRYPT_OK:
        return "ok";
    case DISKCRYPT_ERR_ARG:
        return "bad argument";
    case DISKCRYPT_ERR_IO:
        return "disk read or write failed";
    case DISKCRYPT_ERR_NO_VOLUME:
        return "no encrypted volume here";
    case DISKCRYPT_ERR_WRONG_KEY:
        return "no key slot opens with that secret";
    case DISKCRYPT_ERR_LOCKED:
        return "the volume is locked";
    case DISKCRYPT_ERR_FULL:
        return "every key slot is in use";
    case DISKCRYPT_ERR_LAST_SLOT:
        return "that is the last key slot";
    case DISKCRYPT_ERR_MEMORY:
        return "not enough memory for the key derivation";
    case DISKCRYPT_ERR_WEAK_KDF:
        return "key derivation settings below the minimum";
    }
    return "unknown";
}
