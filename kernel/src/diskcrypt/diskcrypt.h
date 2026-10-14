/* diskcrypt.h — full-disk encryption: a key-slot header and an encrypting
 * block device
 *
 * A volume is a run of sectors on a lower block device:
 *
 *   base + 0  .. base + 7    header copy A (4096 bytes)
 *   base + 8  .. base + 15   header copy B
 *   base + 16 ..             data, one Adiantum-encrypted sector per sector
 *
 * Every data sector is encrypted under one random 32-byte volume key with
 * Adiantum (adiantum.h), its tweak being the sector's number within the data
 * area. Nothing about the data says the volume exists beyond the header.
 *
 * KEY SLOTS
 * ---------
 * The volume key is never stored in the clear. The header holds up to
 * DISKCRYPT_SLOTS key slots; each wraps the volume key with
 * ChaCha20-Poly1305 under a key derived from a secret by Argon2id:
 *   - a PASSPHRASE slot, for something the owner types; Argon2id makes each
 *     guess cost DISKCRYPT_MIN_M_KIB KiB of memory or more;
 *   - a RECOVERY slot, for a random 32-byte recovery key written down at
 *     setup and kept somewhere else.
 * Any one slot opens the volume. Changing a passphrase is adding a new slot
 * and removing the old one: the data is never re-encrypted. The slot's
 * AEAD binds the header's fixed fields (uuid, geometry, cipher) and the
 * slot's own index, type and KDF parameters, so a slot moved, re-typed or
 * given weaker parameters by someone editing the disk no longer opens.
 *
 * TORN WRITES
 * -----------
 * The header is kept twice, each copy sealed with SHA-256 and carrying a
 * generation number. An update writes copy A, then copy B, both with the next
 * generation. Power lost during either write leaves the other copy intact,
 * so the volume always opens with either the old slots or the new ones.
 *
 * ERASE
 * -----
 * diskcrypt_erase() overwrites both header copies. Without a key slot the
 * volume key is gone, and with it every data sector, in two writes rather
 * than a pass over the whole disk. It is the building block for remote wipe,
 * and it can not be undone: a caller must have the owner's confirmation.
 *
 * Randomness comes from the caller (diskcrypt_rng_t), so the kernel's
 * entropy source stays its own concern and tests can be deterministic.
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_DISKCRYPT_H
#define ZXV_DISKCRYPT_H

#include <stdint.h>
#include <stdbool.h>
#include "../../include/blockdev.h"
#include "adiantum.h"

#define DISKCRYPT_MAGIC        0x5A58444Bu /* 'ZXDK' */
#define DISKCRYPT_VERSION      1u
#define DISKCRYPT_CIPHER       1u /* adiantum(xchacha12, aes256), 32-byte tweak */
#define DISKCRYPT_HDR_BYTES    4096u
#define DISKCRYPT_HDR_SECTORS  8u
#define DISKCRYPT_DATA_OFFSET  (2u * DISKCRYPT_HDR_SECTORS)
#define DISKCRYPT_SLOTS        8u
#define DISKCRYPT_SECRET_MAX   256u
#define DISKCRYPT_RECOVERY_LEN 32u

/* Argon2id floors. A passphrase slot below these is refused: written by
 * someone else, it would make the passphrase cheap to guess. */
#define DISKCRYPT_MIN_M_KIB 8192u
#define DISKCRYPT_MIN_T     1u
#define DISKCRYPT_MAX_M_KIB (1u << 21) /* 2 GiB */
#define DISKCRYPT_MAX_T     16u
/* Defaults for a new passphrase slot: 64 MiB, 3 passes, 1 lane. */
#define DISKCRYPT_DEFAULT_M_KIB 65536u
#define DISKCRYPT_DEFAULT_T     3u

typedef enum {
    DISKCRYPT_SLOT_EMPTY = 0,
    DISKCRYPT_SLOT_PASSPHRASE = 1,
    DISKCRYPT_SLOT_RECOVERY = 2
} diskcrypt_slot_type_t;

typedef enum {
    DISKCRYPT_OK = 0,
    DISKCRYPT_ERR_ARG,       /* a bad argument */
    DISKCRYPT_ERR_IO,        /* the lower device refused a read or write */
    DISKCRYPT_ERR_NO_VOLUME, /* neither header copy is a valid header */
    DISKCRYPT_ERR_WRONG_KEY, /* no slot opened with this secret */
    DISKCRYPT_ERR_LOCKED,    /* the volume is not unlocked */
    DISKCRYPT_ERR_FULL,      /* no free slot */
    DISKCRYPT_ERR_LAST_SLOT, /* removing it would lock the data out for good */
    DISKCRYPT_ERR_MEMORY,    /* the Argon2id work area is too small */
    DISKCRYPT_ERR_WEAK_KDF   /* KDF parameters below the floor */
} diskcrypt_status_t;

typedef struct {
    void (*fill)(void *ctx, uint8_t *out, uint32_t len);
    void *ctx;
} diskcrypt_rng_t;

/* The Argon2id work area: mem_blocks 1 KiB blocks of caller memory. A slot
 * with m_kib larger than this can not be opened with it. */
typedef struct {
    uint64_t *mem;
    uint32_t mem_blocks;
} diskcrypt_work_t;

typedef struct {
    uint32_t type;
    uint32_t m_kib, t, p;
    uint8_t salt[32];
    uint8_t nonce[12];
    uint8_t wrapped[32];
    uint8_t tag[16];
} diskcrypt_slot_t;

typedef struct {
    uint32_t data_sectors;
    uint32_t generation;
    uint8_t uuid[16];
    diskcrypt_slot_t slots[DISKCRYPT_SLOTS];
} diskcrypt_header_t;

typedef struct {
    block_device_t dev; /* the decrypted view: hand this to the filesystem */
    block_device_t *lower;
    uint32_t base;
    diskcrypt_header_t hdr;
    adiantum_t cipher;
    uint8_t vkey[32];
    bool unlocked;
    uint8_t buf[BLOCKDEV_SECTOR_SIZE];
} diskcrypt_volume_t;

/* ---- the header's on-disk form (exposed for tests and tools) */
void diskcrypt_header_encode(const diskcrypt_header_t *h, uint8_t out[DISKCRYPT_HDR_BYTES]);
bool diskcrypt_header_decode(const uint8_t in[DISKCRYPT_HDR_BYTES], diskcrypt_header_t *h);

/* True if `base` holds at least one valid header copy. *data_sectors (may be
 * 0) receives the volume's data size, so the volume ends at
 * base + DISKCRYPT_DATA_OFFSET + *data_sectors. Needs no key. */
bool diskcrypt_probe(block_device_t *lower, uint32_t base, uint32_t *data_sectors);

/* Create a volume over [base, base + count) and add its first slot, of
 * `type`, opened by `secret`. Writes only the two header copies; the data
 * area keeps whatever it held and reads as noise until written. Leaves `v`
 * unlocked. count must leave at least one data sector. */
diskcrypt_status_t diskcrypt_format(diskcrypt_volume_t *v, block_device_t *lower, uint32_t base,
                                    uint32_t count, diskcrypt_slot_type_t type,
                                    const uint8_t *secret, uint32_t secret_len, uint32_t m_kib,
                                    uint32_t t, const diskcrypt_rng_t *rng,
                                    const diskcrypt_work_t *work);

/* Open the volume at `base` with `secret`, trying each slot in turn.
 * *slot_out (may be 0) receives the slot that opened it. */
diskcrypt_status_t diskcrypt_unlock(diskcrypt_volume_t *v, block_device_t *lower, uint32_t base,
                                    const uint8_t *secret, uint32_t secret_len,
                                    const diskcrypt_work_t *work, uint32_t *slot_out);

/* Forget the volume key. The block device stops answering. */
void diskcrypt_lock(diskcrypt_volume_t *v);

/* Add a slot to an unlocked volume. *slot_out receives its index. */
diskcrypt_status_t diskcrypt_add_slot(diskcrypt_volume_t *v, diskcrypt_slot_type_t type,
                                      const uint8_t *secret, uint32_t secret_len, uint32_t m_kib,
                                      uint32_t t, const diskcrypt_rng_t *rng,
                                      const diskcrypt_work_t *work, uint32_t *slot_out);

/* Remove a slot from an unlocked volume. The last slot can not be removed;
 * diskcrypt_erase() is the way to make the data unreadable. */
diskcrypt_status_t diskcrypt_remove_slot(diskcrypt_volume_t *v, uint32_t slot);

/* Overwrite both header copies at `base`, destroying every key slot and so
 * every data sector. Irreversible: callers must have the owner's explicit
 * confirmation. Locks `v` if given. */
diskcrypt_status_t diskcrypt_erase(diskcrypt_volume_t *v, block_device_t *lower, uint32_t base);

/* Make a recovery key and its printable form: 32 random bytes as eight
 * groups of eight hex digits separated by '-', NUL-terminated. */
#define DISKCRYPT_RECOVERY_TEXT 72u
void diskcrypt_recovery_key(const diskcrypt_rng_t *rng, uint8_t key[DISKCRYPT_RECOVERY_LEN],
                            char text[DISKCRYPT_RECOVERY_TEXT]);
/* Parse a typed recovery key; ignores '-', spaces and letter case. */
bool diskcrypt_recovery_parse(const char *text, uint8_t key[DISKCRYPT_RECOVERY_LEN]);

uint32_t diskcrypt_slots_used(const diskcrypt_volume_t *v);
const char *diskcrypt_status_str(diskcrypt_status_t s);

#endif /* ZXV_DISKCRYPT_H */
