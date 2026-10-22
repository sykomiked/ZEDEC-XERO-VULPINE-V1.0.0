/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* aerie_model.h — Aerie, radio and ham links, as a model (zxm.h). Two
 * paths, kept apart:
 *
 *   SEALED: messages sealed and opened on the Endian Hopping bands
 *   (ehop.seal is ChaCha20-Poly1305 encryption). An Endian Hopping band is
 *   a message class (control, settle, media, chat, bulk), not a radio
 *   frequency band, and this path is for private bearers only: a mesh, a
 *   LAN, a cable or a radio service that is not amateur. Never amateur
 *   radio (AR5).
 *
 *   AMATEUR: plain text anyone can read, composed for the ham bearer
 *   (docs/retail/BEARERS.md, kernel/src/bearer/br_ham.h): emergency only,
 *   no encryption, no sessions, no payments, no business, the owner's call
 *   sign in the clear. It never calls ehop.seal. zxvd has no method that
 *   signs a ham message or transmits one, so the model composes the exact
 *   bytes br_ham signs ("ZXV-EMRG-1\n" call "\n" text) and hands them to
 *   the view unsigned and unsent; nothing is invented.
 *
 * Also harmonic line frames, long messages as freight, and a bounded log
 * of every message. The rules are aerie_rules.h; whether a message may
 * alert the owner now is Halloo's (halloo_rules.h HR1, HR2).
 *
 * zxvd methods used: ehop.bands, ehop.seal, ehop.open, harmonic.encode,
 * harmonic.decode, freight.pack and freight.unpack. zxvd has no method
 * that drives radio hardware, so nothing here transmits. Messages only,
 * never money: no ledger method, no amount in any request or record. No
 * drawing, no allocation, integers only.
 *
 *   AM1 BANDS. aerie_bands reads ehop.bands whole into the model; a reply
 *       that does not parse, or a band whose largest frame is outside
 *       AERIE_FRAME_MIN..AERIE_FRAME_MAX, leaves the plan as it was.
 *   AM2 THE OWNER'S BANDS. The owner chooses which Endian Hopping bands
 *       the sealed path may use (aerie_set_bands, one bit per band); none
 *       until they do. The settle band carries payments and is never
 *       allowed: Aerie carries messages only.
 *   AM3 SEAL. Checked before anything is sent: the bearer is not amateur
 *       (AR5, aerie_ham_clear_ok(amateur, 1)), the plan is read (AM1), the
 *       band is allowed (AM2), the key is 64 hex digits, the message fits
 *       the band (AR2) and its log record fits (AR4). The sequence number
 *       is taken before the call and never given again, so a failed call
 *       never makes zxvd seal two messages with one nonce. A sealed
 *       message is logged going out.
 *   AM4 OPEN. A frame zxvd opens is kept only when its (sender, seq) is
 *       newer than the last kept from that sender (zxvd keeps no replay
 *       window); a replay, or a new sender when AERIE_SENDERS are already
 *       tracked, is ZXM_E_STATE and nothing is logged. A kept message is
 *       logged coming in, and aerie_t.alert says whether it may alert the
 *       owner now: a known contact, not blocked, outside quiet hours
 *       (halloo_ring_ok over halloo_quiet_left).
 *   AM5 LINE FRAMES. aerie_line_encode refuses parts AR1 refuses and
 *       checks zxvd's tag against AR1; aerie_line_decode resolves with the
 *       owner's threshold (AR3) and checks the split: a tag of 0..199 must
 *       answer the parts AR1 splits it into, a reserved tag (200..255) is
 *       marked reserved and must still answer trunk = tag mod 10.
 *   AM6 FREIGHT. aerie_freight_pack erasure-codes 1..AERIE_FREIGHT_MAX
 *       bytes; aerie_freight_unpack rebuilds them from any k packets.
 *   AM7 THE LOG holds the last AERIE_LOG messages; when it is full the
 *       oldest goes and is counted (log_dropped).
 *   AM8 THE AMATEUR SWITCH is off until the owner gives their own call
 *       sign (br_ax25.h X1: 1..6 of A-Z and 0-9 with a letter and a digit,
 *       optional -SSID 0..15) and confirms they hold an amateur licence
 *       (aerie_ham_enable; br_ham.h H1). AERIE_NOTICE_HAM is shown first.
 *   AM9 AMATEUR MESSAGE. aerie_ham_compose checks AR5 in the clear
 *       (aerie_ham_clear_ok(1, 0)), the switch (AM8), printable ASCII (tab,
 *       CR, LF allowed), 1..AERIE_HAM_TEXT_MAX bytes and the log record
 *       (AR4); it composes the bytes br_ham signs into ham_text, the call
 *       sign in the clear, and logs the message going out. It sends
 *       nothing. */
#ifndef AERIE_MODEL_H
#define AERIE_MODEL_H

#include "aerie_rules.h"
#include "halloo_rules.h"
#include "zxm.h"

#define AERIE_BANDS        5u
#define AERIE_BAND_NAME    15u
#define AERIE_KEY_HEX      64u
#define AERIE_FRAME_HEX    (2u * AERIE_FRAME_MAX)
#define AERIE_LINE_HEX     42u /* a 21-octet harmonic frame */
#define AERIE_HEADER_HEX   128u
#define AERIE_PACKETS_HEX  7056u /* 168 packets of 21 bytes */
#define AERIE_SENDERS      16u
#define AERIE_CONTACTS     32u
#define AERIE_LOG          64u
#define AERIE_SETTLE       1u /* ehop's settle band: payments, never Aerie's */
#define AERIE_BANDS_NONE   0u
#define AERIE_CALL_MAX     9u    /* CALL-SSID: 6 and "-15" */
#define AERIE_HAM_TEXT_MAX 1024u /* BR_HAM_TEXT_MAX */
#define AERIE_HAM_HEAD     "ZXV-EMRG-1\n"
#define AERIE_HAM_MAX      (11u + AERIE_CALL_MAX + 1u + AERIE_HAM_TEXT_MAX)

enum { AERIE_IN = 0, AERIE_OUT = 1 };

/* what the view shows before each path is used (AM2, AM8) */
#define AERIE_NOTICE_SEALED                                                                        \
    "Sealed messages are encrypted. Use them only on private links, never on amateur radio. "      \
    "Aerie carries messages only, never money."
#define AERIE_NOTICE_HAM                                                                           \
    "Amateur radio: plain text anyone can read, emergency messages only. No encryption, no "       \
    "payments, no business. Your call sign is sent in the clear with every message. Turn this "    \
    "on only if you hold an amateur licence."

/* harmonic.decode's truth (zt_harmonic_wire.h W5) */
enum {
    AERIE_UNKNOWN = 0,
    AERIE_TRUE = 1,
    AERIE_FALSE = 2,
    AERIE_NEUTRAL = 3,
    AERIE_GLUT = 4,
};

typedef struct {
    char name[AERIE_BAND_NAME + 1];
    int64_t fm_min, fm_max, am_mask, pm_modes, priority, max_frame, latency_ms;
} aerie_band_t;

/* one message in the log (AR4); no amount, ever */
typedef struct {
    int64_t seq, bearer, band, dir, sender, bytes, stamp;
} aerie_msg_t;

/* one sealed channel: what both ends hold */
typedef struct {
    uint32_t bearer;     /* AERIE_PRIVATE, or AERIE_AMATEUR (always refused, AR5) */
    const char *key_hex; /* AERIE_KEY_HEX hex digits, the channel secret */
    uint32_t band;       /* an index of ehop.bands */
    uint32_t chan_id, epoch;
    uint32_t sender; /* this end's sender id, unique on the channel */
} aerie_chan_t;

typedef struct {
    uint32_t sender;
    uint64_t top; /* the newest seq kept from it */
} aerie_seen_t;

typedef struct {
    uint32_t sender;
    bool known, blocked;
} aerie_contact_t;

/* the last line frame encoded or decoded (AM5) */
typedef struct {
    int64_t tag, sync, shell, trunk, trunk_hz, correlation_q16;
    int64_t s_plus[3], s_minus[2];
    bool idle, axis_trunk, reserved;
    int32_t truth; /* AERIE_UNKNOWN .. AERIE_GLUT */
    char frame_hex[AERIE_LINE_HEX + 1];
} aerie_line_t;

/* the last freight packed (AM6) */
typedef struct {
    uint64_t id;
    int64_t k, payload_len;
    char header_hex[AERIE_HEADER_HEX + 1];
    char packets_hex[AERIE_PACKETS_HEX + 1];
} aerie_freight_t;

typedef struct {
    zxm_link_t *link;
    aerie_band_t band[AERIE_BANDS];
    uint32_t nbands;
    uint32_t allowed;  /* AM2: bit b lets the sealed path use band b */
    int64_t threshold; /* AR3 */
    bool quiet_set;
    int64_t quiet_opens, quiet_closes; /* minutes of the day (HR1) */
    uint64_t next_seq;                 /* AM3 */
    aerie_seen_t seen[AERIE_SENDERS];
    uint32_t nseen;
    aerie_contact_t contact[AERIE_CONTACTS];
    uint32_t ncontacts;
    aerie_msg_t log[AERIE_LOG]; /* AM7: a ring, oldest at log_head */
    uint32_t nlog, log_head, log_dropped;
    int64_t log_seq;                     /* the last record's seq */
    char frame_hex[AERIE_FRAME_HEX + 1]; /* the last sealed frame */
    uint8_t data[AERIE_FREIGHT_MAX];     /* the last opened or unpacked message */
    uint32_t ndata;
    uint32_t from; /* the last opened message's sender */
    bool alert;    /* AM4 */
    aerie_line_t line;
    aerie_freight_t freight;
    bool ham_on;                      /* AM8 */
    char call[AERIE_CALL_MAX + 1];    /* the owner's call sign, upper case */
    char ham_text[AERIE_HAM_MAX + 1]; /* AM9: the bytes br_ham signs */
    uint32_t ham_len;
} aerie_t;

void aerie_init(aerie_t *a, zxm_link_t *link);

/* AM1: ehop.bands; the index of a band by name, or -1. */
int aerie_bands(aerie_t *a);
int32_t aerie_band_index(const aerie_t *a, const char *name);

/* AM2: the Endian Hopping bands the sealed path may use. ZXM_E_ARG for the
 * settle band or a band ehop does not have. */
int aerie_set_bands(aerie_t *a, uint32_t mask);
/* AR3: ZXM_E_ARG outside AERIE_THR_MIN..AERIE_THR_MAX. */
int aerie_set_threshold(aerie_t *a, int64_t thr);
/* HR1: the owner's quiet hours, minutes of the day; ZXM_E_ARG outside
 * 0..1439. */
int aerie_set_quiet(aerie_t *a, int64_t opens, int64_t closes);
/* A contact the owner knows or blocks, by sender id; ZXM_E_STATE when
 * AERIE_CONTACTS are already kept. */
int aerie_set_contact(aerie_t *a, uint32_t sender, bool known, bool blocked);
/* AM4: may a message from sender alert the owner at minute of the day m? */
bool aerie_may_alert(const aerie_t *a, uint32_t sender, int64_t minute);

/* AM3: seal data[0..n) on ch at now_ms (ms since 1970) into a->frame_hex.
 * ZXM_E_STATE, nothing sent, for an amateur bearer (AR5). */
int aerie_seal(aerie_t *a, const aerie_chan_t *ch, const uint8_t *data, uint32_t n, int64_t now_ms);
/* AM4: open frame_hex on ch at now_ms into a->data, a->from and a->alert. */
int aerie_open(aerie_t *a, const aerie_chan_t *ch, const char *frame_hex, int64_t now_ms);

/* AM5 */
int aerie_line_encode(aerie_t *a, int64_t sync, int64_t shell, int64_t trunk,
                      const int64_t s_plus[3], const int64_t s_minus[2]);
int aerie_line_decode(aerie_t *a, const char *frame_hex);

/* AM6 */
int aerie_freight_pack(aerie_t *a, const uint8_t *data, uint32_t n);
int aerie_freight_unpack(aerie_t *a, const char *header_hex, const char *packets_hex);

/* AM8: the amateur switch. ZXM_E_ARG for a call sign that is not one,
 * ZXM_E_STATE without the owner's confirmation that they hold a licence. */
bool aerie_call_ok(const char *call);
int aerie_ham_enable(aerie_t *a, const char *call, bool licence_confirmed);
void aerie_ham_disable(aerie_t *a);
/* AM9: compose text[0..n) for the ham bearer at now_ms; sends nothing. */
int aerie_ham_compose(aerie_t *a, const char *text, uint32_t n, int64_t now_ms);

/* AM7: the i-th message kept, oldest first; NULL past the end. */
const aerie_msg_t *aerie_log_at(const aerie_t *a, uint32_t i);

#endif
