/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* seedbank_model.h — Seedbank, IPFS, as a model (zxm.h): pin content into
 * this node's blockstore by CID, fetch it back, see how full the
 * blockstore is, and record the CID a published space lives under. The
 * rules are seedbank_rules.h.
 *
 * zxvd methods used: aipi.status, aipi.put, aipi.get, web4.cid, files.root,
 * files.stat and spaces.set_cid. No drawing, no allocation, integers only.
 *
 *   SM1 TWO NAMES, ONE CID. aipi.put names a block in its own text form,
 *       "z" + base58btc (docs/AIPI.md); web4.cid and spaces.set_cid use
 *       "b" + base32. Both spell the same binary CIDv1 raw sha2-256 (36
 *       bytes: 0x01 0x55 0x12 0x20 and the digest). A pin keeps both: the
 *       block's name for aipi.get, and the CID text, turned from it here in
 *       C (seedbank_block_to_cid) and held to SR2 before it is kept.
 *   SM2 PINNING. Bytes are pinned by hex (at most 8000, zxvd's limit for
 *       one aipi.put by hex) after web4.cid has named them on this device:
 *       the blockstore's name must be that very CID, or nothing is kept. A
 *       file of the Hoard is pinned by path (files.root + its path) after
 *       files.stat says it is a file within SR3; the blockstore must report
 *       the size files.stat gave. Pinning content already pinned keeps the
 *       one record. Every pin is a ^ZXSEED record that fits SR5.
 *   SM3 FETCHING asks aipi.get only for a pin within SR4, and keeps the
 *       bytes only when they are as many as the pin says.
 *   SM4 PUBLISHING records a pin's CID on a cloud top or park this node
 *       owns (spaces.set_cid), and unpublishing records "": both only on
 *       the owner's confirmed word (nothing is sent without it). A space
 *       has one CID, so publishing moves the mark from any other pin.
 *   SM5 FORGETTING drops a pin from Seedbank's table only. zxvd has no
 *       method that unpins a block or announces one to the network; none is
 *       invented. A published pin is unpublished first. */
#ifndef SEEDBANK_MODEL_H
#define SEEDBANK_MODEL_H

#include "seedbank_rules.h"
#include "zxm.h"

#define SEEDBANK_PINS      64u   /* pins in the table; SR5's pin fits i2 */
#define SEEDBANK_BLOCK_MAX 127u  /* aipi's text form of a CID */
#define SEEDBANK_URI_MAX   79u   /* "ipfs://" + a CID text */
#define SEEDBANK_HEX_PUT   8000u /* zxvd: one aipi.put by hex (ZXVD_AIPI_HEX_MAX) */
#define SEEDBANK_PATH_MAX  1023u /* aipi.put's path */
#define SEEDBANK_BIN_LEN   36u   /* SM1: a CIDv1 raw sha2-256, binary */

typedef struct {
    char block[SEEDBANK_BLOCK_MAX + 1]; /* aipi's name: "z" + base58btc */
    char cid[SEEDBANK_CID_LEN + 1];     /* the same CID: "b" + base32 (SR2) */
    char uri[SEEDBANK_URI_MAX + 1];     /* web4.cid's ipfs:// form, "" until asked */
    char space[ZXM_SPACE_MAX + 1];      /* the space it is published under, "" none */
    int64_t bytes;                      /* SR3 */
    int64_t stamp;                      /* when it was pinned, seconds since 1970 */
    bool sealed;                        /* put private: sealed at rest */
} seedbank_pin_t;

typedef struct {
    zxm_link_t *link;
    /* aipi.status */
    bool online;
    uint64_t blocks, store_bytes, store_cap;
    int64_t store_x100; /* used_share.used_x100's meaning; -1 unknown */
    /* the pins, in the order they were made */
    seedbank_pin_t pin[SEEDBANK_PINS];
    uint32_t npins;
    /* the last fetch (SM3) */
    uint8_t got[SEEDBANK_GET_MAX];
    uint32_t got_len;
    int32_t got_pin; /* -1 none */
} seedbank_t;

void seedbank_init(seedbank_t *s, zxm_link_t *link);

/* SM1: the CID text ("b" + base32, SR2) of aipi's block name z; 0, or
 * ZXM_E_ARG when z is not "z" + base58btc of a CIDv1 raw sha2-256. out
 * holds SEEDBANK_CID_LEN + 1 bytes. */
int seedbank_block_to_cid(const char *z, char out[SEEDBANK_CID_LEN + 1]);

/* aipi.status: online, blocks, store use and its share (store_x100). */
int seedbank_status(seedbank_t *s);

/* SM2: pin data[0..len) (at most SEEDBANK_HEX_PUT bytes), sealed at rest
 * when asked; *at is the pin. now is seconds since 1970. */
int seedbank_pin_bytes(seedbank_t *s, const uint8_t *data, uint32_t len, bool sealed, int64_t now,
                       uint32_t *at);
/* SM2: pin the file at path in the Hoard (home). */
int seedbank_pin_file(seedbank_t *s, const char *path, bool sealed, int64_t now, uint32_t *at);

/* A pin by its CID text or its block name; -1 when none. */
int32_t seedbank_find(const seedbank_t *s, const char *text);

/* SM3: aipi.get the pin's bytes into s->got. */
int seedbank_fetch(seedbank_t *s, uint32_t at);
/* web4.cid: the pin's ipfs:// form (pin.uri). */
int seedbank_uri(seedbank_t *s, uint32_t at);

/* SM4: spaces.set_cid, only when the owner confirmed. */
int seedbank_publish(seedbank_t *s, uint32_t at, const char *space, bool owner_confirmed);
int seedbank_unpublish(seedbank_t *s, const char *space, bool owner_confirmed);

/* SM5: drop a pin from the table (nothing is sent). */
int seedbank_forget(seedbank_t *s, uint32_t at);

#endif
