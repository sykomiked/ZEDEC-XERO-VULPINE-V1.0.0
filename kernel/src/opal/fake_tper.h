/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fake_tper.h — a host-side fake Opal TPer for the tests (test_opal.c and
 * hwdrv/test/test_nvme.c). Not built into any kernel.
 *
 * It answers Level 0 Discovery, StartSession on the Locking SP with Admin1
 * or User1 password challenges, Set on Locking ranges (ReadLocked,
 * WriteLocked) and on MBRControl (Enable, Done), and EndSession. Its own
 * token reader is written apart from opal.c, so the host is not checked
 * against itself. Every framing rule the host must follow (lengths, padding,
 * reserved fields, session numbers, the status list) is checked, and each
 * violation counts in `strict`. Knobs make it hostile. */
#ifndef ZXV_FAKE_TPER_H
#define ZXV_FAKE_TPER_H

#include <stdint.h>

#define FT_MAX    2048u
#define FT_RANGES 9u /* 0 = global, 1..8 */

typedef struct ftper {
    /* configuration */
    uint16_t comid;    /* base ComID (0x07FE) */
    uint16_t ssc_code; /* SSC feature code (0x0203); 0 = none listed */
    uint16_t num_comids;
    int no_tper, no_locking, enterprise;
    int locking_supported, locking_enabled; /* both 1 */
    int mbr_enabled, mbr_done;
    uint8_t admin1[32], user1[32];
    uint32_t admin1_len, user1_len;
    int user_may_mbr;     /* ACE: User1 may set MBRControl Done */
    uint32_t user_ranges; /* bit per range User1 may Set (all) */
    int rl[FT_RANGES], wl[FT_RANGES];
    uint32_t busy; /* answer "pending" this many IF-RECVs first */
    int silent;    /* never answer on the ComID */
    int fail_send, fail_recv;
    int fail_status;         /* answer every Set with this status (0 = off) */
    uint32_t tsn_start;      /* first TPer session number (0x1001) */
    const uint8_t *disc_raw; /* Level 0 data used instead of the built one */
    uint32_t disc_raw_len;
    /* edit the nth ComID response (1-based; 0 = every one) before it is read */
    void (*mangle)(struct ftper *f, uint8_t *buf, uint32_t len);
    uint32_t mangle_at;
    /* state */
    int in_session, write, auth; /* auth: 0 Anybody, 1 Admin1, 2 User1 */
    uint32_t tsn, hsn;
    uint8_t resp[FT_MAX];
    int have_resp;
    /* observation */
    uint32_t sends, recvs, discs, starts, auth_fail, sets, mbr_sets, ends, strict, responses;
    uint32_t last_len, last_cp_len;
    uint8_t last_cmd[FT_MAX];
    uint8_t last_challenge[64];
    uint32_t last_challenge_len;
    uint8_t last_authority[8];
} ftper_t;

/* Defaults above; Admin1 password a1[0..n), User1 u1[0..m). */
void ftper_init(ftper_t *f, const uint8_t *a1, uint32_t n, const uint8_t *u1, uint32_t m);
/* Locking feature byte as Level 0 Discovery would report it now. */
uint8_t ftper_locking_flags(const ftper_t *f);
/* Build Level 0 Discovery data into buf (len bytes); returns bytes used. */
uint32_t ftper_level0(const ftper_t *f, uint8_t *buf, uint32_t len);

/* The transport callbacks (ctx = ftper_t *). */
int ftper_send(void *ctx, uint8_t protocol, uint16_t sp_specific, uint8_t *buf, uint32_t len);
int ftper_recv(void *ctx, uint8_t protocol, uint16_t sp_specific, uint8_t *buf, uint32_t len);

#endif
