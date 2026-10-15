/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* opal.h — a TCG Opal host for unlocking a self-encrypting drive at boot.
 *
 * Follows TCG Storage Architecture Core Spec 2.01 (tokens, ComPacket/Packet/
 * SubPacket framing, session manager) and Opal SSC 2.01/2.02 (Level 0
 * Discovery, Locking SP, Locking table, MBRControl). Only what the boot
 * unlock needs: Level 0 Discovery, StartSession with a password challenge,
 * Set on a Locking range and on MBRControl, EndSession.
 *
 * The transport is two callbacks (IF-SEND / IF-RECV): an NVMe Security
 * Send/Receive, an ATA TRUSTED SEND/RECEIVE or a test fake. Each moves a
 * whole OPAL_MAX_COMPACKET buffer, the size mainline Linux uses.
 *
 * Integers only, no allocation: the command and response buffers live in
 * opal_dev_t. Every length the drive reports is checked against the buffer
 * before it is used, so a hostile drive cannot move a read or write outside
 * them. The command buffer, which carries the challenge, is zeroed after
 * every send.
 *
 * SSCs: Opal 2 (0x0203) and Opal 1 (0x0200) are used; Ruby (0x0304) and
 * Pyrite 1/2 (0x0302/0x0303) have the same descriptor layout, Locking SP,
 * Locking table and MBRControl, so they are used the same way. Enterprise
 * (0x0100) has a different method set and is reported as unsupported. */
#ifndef ZXV_OPAL_H
#define ZXV_OPAL_H

#include <stdint.h>

#define OPAL_MAX_COMPACKET 2048u /* both buffers; IF-SEND/IF-RECV size */
#define OPAL_MAX_TOKENS    64u   /* tokens kept from one response */
#define OPAL_KEY_MAX       32u   /* longest challenge accepted */
#define OPAL_POLLS         64u   /* default IF-RECV retries per response */
#define OPAL_HSN           0x41u /* our host session number */
#define OPAL_FIRST_TSN     4096u /* lowest TPer session number accepted */
#define OPAL_RANGES_MAX    64u   /* highest Locking range number accepted */

/* Security protocols and ComIDs */
#define OPAL_PROTO_TCG    0x01u
#define OPAL_COMID_LEVEL0 0x0001u

/* Framing sizes (Core 3.2.3) */
#define OPAL_CP_HDR  20u
#define OPAL_PKT_HDR 24u
#define OPAL_SUB_HDR 12u
#define OPAL_HDRS    (OPAL_CP_HDR + OPAL_PKT_HDR + OPAL_SUB_HDR)

/* Control tokens (Core 3.2.2.3.1) */
#define OPAL_TK_STARTLIST    0xF0u
#define OPAL_TK_ENDLIST      0xF1u
#define OPAL_TK_STARTNAME    0xF2u
#define OPAL_TK_ENDNAME      0xF3u
#define OPAL_TK_CALL         0xF8u
#define OPAL_TK_ENDOFDATA    0xF9u
#define OPAL_TK_ENDOFSESSION 0xFAu
#define OPAL_TK_STARTTRANS   0xFBu
#define OPAL_TK_ENDTRANS     0xFCu
#define OPAL_TK_EMPTY        0xFFu

/* Level 0 feature codes */
#define OPAL_FC_TPER       0x0001u
#define OPAL_FC_LOCKING    0x0002u
#define OPAL_FC_GEOMETRY   0x0003u
#define OPAL_FC_ENTERPRISE 0x0100u
#define OPAL_FC_OPAL1      0x0200u
#define OPAL_FC_SINGLEUSER 0x0201u
#define OPAL_FC_DATASTORE  0x0202u
#define OPAL_FC_OPAL2      0x0203u
#define OPAL_FC_PYRITE1    0x0302u
#define OPAL_FC_PYRITE2    0x0303u
#define OPAL_FC_RUBY       0x0304u
#define OPAL_FC_BLOCKSID   0x0402u

/* Locking feature flags (byte 4 of the descriptor) */
#define OPAL_LK_SUPPORTED 0x01u
#define OPAL_LK_ENABLED   0x02u
#define OPAL_LK_LOCKED    0x04u
#define OPAL_LK_MEDIAENC  0x08u
#define OPAL_LK_MBR_EN    0x10u
#define OPAL_LK_MBR_DONE  0x20u

/* Method status codes (Core 5.1.5) */
#define OPAL_ST_SUCCESS          0x00u
#define OPAL_ST_NOT_AUTHORIZED   0x01u
#define OPAL_ST_SP_BUSY          0x03u
#define OPAL_ST_SP_FAILED        0x04u
#define OPAL_ST_SP_DISABLED      0x05u
#define OPAL_ST_SP_FROZEN        0x06u
#define OPAL_ST_NO_SESSIONS      0x07u
#define OPAL_ST_UNIQUENESS       0x08u
#define OPAL_ST_INSUFF_SPACE     0x09u
#define OPAL_ST_INSUFF_ROWS      0x0Au
#define OPAL_ST_INVALID_PARAM    0x0Cu
#define OPAL_ST_TPER_MALFUNCTION 0x0Fu
#define OPAL_ST_TRANS_FAILURE    0x10u
#define OPAL_ST_RESP_OVERFLOW    0x11u
#define OPAL_ST_AUTH_LOCKED_OUT  0x12u
#define OPAL_ST_FAIL             0x3Fu

/* Column numbers used with Set */
#define OPAL_COL_READLOCKED  7u /* Locking table */
#define OPAL_COL_WRITELOCKED 8u
#define OPAL_COL_MBR_ENABLE  1u /* MBRControl table */
#define OPAL_COL_MBR_DONE    2u
#define OPAL_SET_VALUES      1u /* Set's Values parameter name */
#define OPAL_SS_CHALLENGE    0u /* StartSession optional parameter names */
#define OPAL_SS_AUTHORITY    3u

/* Results: 0 or one of these. */
#define OPAL_OK             0
#define OPAL_E_TRANSPORT    (-1)  /* IF-SEND or IF-RECV failed */
#define OPAL_E_DISC         (-2)  /* Level 0 Discovery data malformed */
#define OPAL_E_NOT_SED      (-3)  /* no Locking feature: nothing to unlock */
#define OPAL_E_UNSUPPORTED  (-4)  /* Locking present but no usable SSC */
#define OPAL_E_FRAME        (-5)  /* ComPacket/Packet/SubPacket malformed */
#define OPAL_E_COMID        (-6)  /* response for another ComID */
#define OPAL_E_SESSION      (-7)  /* wrong session numbers, or TPer ended it */
#define OPAL_E_TOKEN        (-8)  /* bad token stream or method shape */
#define OPAL_E_STATUS       (-9)  /* method status not SUCCESS: see status */
#define OPAL_E_NO_RESPONSE  (-10) /* TPer still busy after every poll */
#define OPAL_E_TOOBIG       (-11) /* would not fit OPAL_MAX_COMPACKET */
#define OPAL_E_ARG          (-12) /* caller error (bad key length, range) */
#define OPAL_E_STATE        (-13) /* no discovery yet, or session state */
#define OPAL_E_NOT_ATTESTED (-14) /* opal_arb: no attested arbiter */
#define OPAL_E_KEY          (-15) /* opal_arb: the arbiter did not release */

typedef int (*opal_if_fn)(void *ctx, uint8_t protocol, uint16_t sp_specific, uint8_t *buf,
                          uint32_t len); /* 0 = transferred */

typedef enum {
    OPAL_SSC_NONE = 0,
    OPAL_SSC_PYRITE1,
    OPAL_SSC_PYRITE2,
    OPAL_SSC_OPAL1,
    OPAL_SSC_RUBY,
    OPAL_SSC_OPAL2 /* higher is preferred when a drive lists several */
} opal_ssc_t;

typedef struct opal_disc {
    uint32_t revision;
    uint8_t have_tper, have_locking, enterprise;
    uint8_t tper_flags, locking_flags;
    opal_ssc_t ssc;
    uint16_t base_comid, num_comids;
    uint16_t num_admins, num_users; /* Opal 2 / Ruby only, else 0 */
    uint32_t features;              /* descriptors walked */
    uint32_t unknown;               /* descriptors with a code not known here */
    uint16_t first_unknown;
} opal_disc_t;

typedef enum { OPAL_K_UINT = 0, OPAL_K_SINT, OPAL_K_BYTES, OPAL_K_CTRL } opal_kind_t;

typedef struct opal_tok {
    uint8_t kind; /* opal_kind_t */
    uint8_t ctl;  /* control token byte for OPAL_K_CTRL */
    uint32_t off; /* bytes: offset of the data in the response buffer */
    uint32_t len; /* bytes: data length; integers: encoded length */
    uint64_t u;   /* integers up to 8 bytes */
} opal_tok_t;

/* Who signs the session: Admin1 or UserN of the Locking SP. */
typedef enum { OPAL_AUTH_ADMIN1 = 0, OPAL_AUTH_USER = 1 } opal_auth_kind_t;
typedef struct opal_auth {
    opal_auth_kind_t kind;
    uint32_t user; /* 1.. for OPAL_AUTH_USER */
} opal_auth_t;

typedef enum {
    OPAL_STAGE_NONE = 0,
    OPAL_STAGE_DISCOVERY,
    OPAL_STAGE_START,
    OPAL_STAGE_RANGE,
    OPAL_STAGE_MBR,
    OPAL_STAGE_END,
    OPAL_STAGE_KEY
} opal_stage_t;

typedef struct opal_dev {
    opal_if_fn send, recv;
    void *ctx;
    uint32_t polls; /* IF-RECV retries while the TPer reports data pending */
    opal_disc_t disc;
    uint8_t have_disc;
    uint8_t in_session;
    uint8_t status; /* last method status received */
    uint16_t comid;
    uint32_t hsn, tsn;
    uint32_t pos;     /* builder position in cmd */
    int berr;         /* builder overflowed */
    uint32_t sub_off; /* payload offset in resp */
    uint32_t ntok;
    opal_tok_t tok[OPAL_MAX_TOKENS];
    uint8_t cmd[OPAL_MAX_COMPACKET];
    uint8_t resp[OPAL_MAX_COMPACKET];
} opal_dev_t;

typedef struct opal_unlock_result {
    opal_stage_t stage;                /* where it stopped (NONE when nothing failed) */
    uint8_t status;                    /* method status at that stage */
    uint8_t needed;                    /* 1 if a session was opened to unlock */
    uint8_t flags_before, flags_after; /* Locking feature flags */
    uint32_t key_status;               /* opal_arb: the arbiter's ARB_KEY_* answer */
} opal_unlock_result_t;

/* Zero d and attach a transport. */
void opal_init(opal_dev_t *d, opal_if_fn send, opal_if_fn recv, void *ctx);

/* Parse Level 0 Discovery data of n bytes. OPAL_OK or OPAL_E_DISC. */
int opal_parse_discovery(const uint8_t *buf, uint32_t n, opal_disc_t *out);

/* Is a parsed drive usable: OPAL_OK, OPAL_E_NOT_SED or OPAL_E_UNSUPPORTED. */
int opal_disc_usable(const opal_disc_t *disc);

/* IF-RECV Level 0 Discovery, parse it and take the base ComID. Returns as
 * opal_disc_usable, or a transport/parse error. */
int opal_discover(opal_dev_t *d);

/* Split n payload bytes into tokens (Empty atoms dropped). Byte offsets in
 * the tokens are relative to buf + base. Count, or OPAL_E_TOKEN/TOOBIG. */
int opal_tokenize(const uint8_t *buf, uint32_t base, uint32_t n, opal_tok_t *tok, uint32_t max);

/* UIDs */
void opal_uid_auth(opal_auth_t a, uint8_t uid[8]);   /* Admin1 / UserN */
void opal_uid_range(uint32_t range, uint8_t uid[8]); /* 0 = global */

/* Methods. Each checks the method status; OPAL_E_STATUS leaves it in
 * d->status. A session must be open for the Set calls. */
int opal_start_session(opal_dev_t *d, opal_auth_t a, const uint8_t *key, uint32_t keylen);
int opal_set_range(opal_dev_t *d, uint32_t range, int read_locked, int write_locked);
int opal_set_mbr_done(opal_dev_t *d, int done);
int opal_end_session(opal_dev_t *d);

/* Does the discovered state need a session: locked, or a shadow MBR that is
 * enabled but not yet done. */
int opal_needs_unlock(const opal_disc_t *disc);

/* The boot unlock: discover (unless done), and if needed StartSession, clear
 * ReadLocked/WriteLocked on `range`, set MBRControl Done when the shadow MBR
 * is enabled and not done, EndSession, discover again for flags_after. A
 * failure after StartSession still ends the session. */
int opal_unlock(opal_dev_t *d, opal_auth_t a, const uint8_t *key, uint32_t keylen, uint32_t range,
                opal_unlock_result_t *res);

/* Wipe n bytes so the compiler cannot drop it. */
void opal_wipe(void *p, uint32_t n);

const char *opal_err_str(int err);
const char *opal_status_str(uint32_t status);
const char *opal_ssc_str(opal_ssc_t ssc);
const char *opal_stage_str(opal_stage_t stage);

#endif
