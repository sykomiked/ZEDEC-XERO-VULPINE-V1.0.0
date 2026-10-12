/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bearer.h — the links a device can send bytes over, what each may carry,
 * and which one to use for a given message (roadmap R2.2 to R2.6).
 *
 * WHAT IT IS
 * ----------
 * A "bearer" is one kind of physical or OS-provided link: the local network,
 * Bluetooth LE, Wi-Fi Direct, LoRa, a serial cable, a dial-up modem, the
 * phone's cellular data, or amateur (ham) radio through an SDR. The device
 * mesh (kernel/src/devmesh) and the economy hand this layer a message, its
 * class and its protection flags; this layer
 *   - refuses messages a bearer must never carry (the policy gates below),
 *   - picks the best live link from measured latency and loss, the message
 *     class and the cost of the link, with fixed tie-breaks, and
 *   - fails over to the next link when the chosen one reports an error.
 * The bytes on each link are framed by the bearer-specific files:
 *   br_frag.h    fragmentation / reassembly (BLE ATT, LoRa)
 *   br_ble.h     BLE ATT payload sizes, length-prefixed L2CAP-style stream
 *   br_lora.h    LoRa time on air, EU868 duty cycle, US915 dwell time
 *   br_serial.h  COBS + CRC-32 frames for serial and modem lines
 *   br_modem.h   Hayes AT modem driver (a byte-stream state machine)
 *   br_ax25.h    AX.25 UI frames (FCS, optional HDLC bit stuffing)
 *   br_ham.h     signed cleartext emergency messages over AX.25
 *   br_devmesh.h a dm_host_t whose send() goes through this layer
 *
 * RULES
 *   B1  Each bearer kind has fixed capabilities (br_caps()): MTU range,
 *       nominal bit rate, metered or not, whether it may carry encrypted
 *       bytes, whether it may carry settlement or payment records, the
 *       largest message, and the message classes it accepts. A host may
 *       narrow a link (lower MTU, real bit rate, its tariff) with
 *       br_link_tune(); it can never widen a policy field.
 *   B2  Policy gates fail closed (br_policy_check()). Unknown kinds, unknown
 *       classes and unknown flag bits are refused.
 *       HAM_SDR  only BR_CLASS_EMERGENCY, only signed, never encrypted,
 *                never a payment or settlement, and the bytes must be
 *                printable ASCII text. This is designed to follow the US
 *                amateur rules in 47 CFR 97.113 (no business or pecuniary
 *                content) and 97.309/97.113(a)(4) (no codes or ciphers that
 *                obscure meaning); a signature appended in the clear does
 *                not obscure meaning. A licensed operator is still
 *                responsible for every transmission; br_ham.h is OFF until
 *                the host says a licensed operator is present.
 *       LORA     only settlement records, small signed records and
 *                emergency messages, each signed or AEAD-authenticated; never
 *                AI traffic or chat; never more than BR_LORA_MAX_MSG bytes.
 *       All others accept every class within their size limit.
 *   B3  Measurements: round-trip time and loss are integer EWMAs
 *       (RFC 6298 style, gain 1/8 for RTT, 1/16 for loss in parts per
 *       million). The first sample replaces the prior.
 *   B4  Selection: among links that are up and pass the gate, the lowest
 *       score wins, where
 *         t    = rtt + 8 * len * 1e6 / bitrate         (microseconds)
 *         t'   = t * 1e6 / (1e6 - loss)                 (expected retries)
 *         cost = cost_per_kib * ceil(len / 1024)
 *         score = wt * t' / 1000 + wl * loss / 1000 + wc * cost
 *       with weights (wt, wl, wc) per class: AI (1,0,100) avoids metered
 *       links, SETTLEMENT (1,4,1) prefers reliable ones, CHAT (2,1,10)
 *       prefers fast ones, EMERGENCY (1,8,0) ignores cost, RECORD (1,2,10).
 *       Ties go to the fixed kind order LAN, WIFI_DIRECT, SERIAL, BLE,
 *       CELLULAR, MODEM, LORA, HAM_SDR, then to the lower link id.
 *   B5  Failover: br_send() tries the best link; if its send() returns an
 *       error other than BR_ERR_BUSY the link is marked down and the next
 *       best link is tried. BR_ERR_BUSY (e.g. LoRa waiting out its duty
 *       cycle) skips the link without marking it down. A link stays down
 *       until the host calls br_link_set_up().
 *   B6  Integer only, no allocation, no libc; 64-bit division is done by
 *       br_udiv64() (shift and subtract), never by a compiler helper.
 *
 * HONEST LIMITS
 *   - Bit rates in br_caps() are nominal radio/line rates, not measured
 *     goodput; hosts should tune them from real measurements.
 *   - The gates check what the caller declares (class, flags) plus what can
 *     be checked from the bytes (size, printable text for HAM). They cannot
 *     tell whether plain text describes a payment; br_ham.h adds the
 *     signature check and the licensed-operator switch.
 *   - The "regulation" behaviour is designed to follow the cited rules; it
 *     is not legal advice and has not been reviewed by a regulator.
 */
#ifndef ZXV_BEARER_H
#define ZXV_BEARER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BR_ID_BYTES     16u
#define BR_MAX_LINKS    16u
#define BR_LORA_MAX_MSG 6144u /* a record plus an ML-DSA-87 signature */
#define BR_HAM_MAX_MSG  8192u

typedef enum {
    BR_OK = 0,
    BR_ERR_ARG = -1,
    BR_ERR_SIZE = -2,
    BR_ERR_POLICY_CLASS = -3,    /* this bearer never carries this class */
    BR_ERR_POLICY_CRYPTO = -4,   /* encrypted bytes on a cleartext-only bearer */
    BR_ERR_POLICY_SETTLE = -5,   /* payment / settlement on a bearer that forbids it */
    BR_ERR_POLICY_UNSIGNED = -6, /* bearer requires a signature (or AEAD) */
    BR_ERR_POLICY_TEXT = -7,     /* bytes are not printable cleartext */
    BR_ERR_NOLINK = -8,          /* no link is up and allowed */
    BR_ERR_BUSY = -9,            /* duty cycle / dwell time: try later */
    BR_ERR_FORMAT = -10,
    BR_ERR_CRC = -11,
    BR_ERR_STATE = -12,
    BR_ERR_FULL = -13,
    BR_ERR_SEND = -14,
    BR_ERR_AUTH = -15,         /* signature did not verify */
    BR_ERR_DISABLED = -16,     /* regulated feature not switched on */
    BR_ERR_POLICY_BAND = -17,  /* frequency outside the configured region */
    BR_ERR_POLICY_DWELL = -18, /* single transmission longer than allowed */
} br_status_t;

typedef enum {
    BR_KIND_NONE = 0,
    BR_LAN = 1,
    BR_BLE = 2,
    BR_WIFI_DIRECT = 3,
    BR_LORA = 4,
    BR_SERIAL = 5,
    BR_MODEM = 6,
    BR_CELLULAR = 7,
    BR_HAM_SDR = 8,
} br_kind_t;
#define BR_KIND_COUNT 9u

typedef enum {
    BR_CLASS_AI = 1,         /* prompts, replies, model traffic: bulk */
    BR_CLASS_SETTLEMENT = 2, /* settlement / ledger records */
    BR_CLASS_CHAT = 3,       /* person-to-person messages */
    BR_CLASS_EMERGENCY = 4,  /* emergency messages */
    BR_CLASS_RECORD = 5,     /* small signed records (rosters, acks, receipts) */
} br_class_t;
#define BR_CLASS_COUNT 6u
#define BR_CLS(c)      (1u << (c))

/* Message flags (what the caller declares about the bytes). */
#define BR_F_ENCRYPTED 0x01u /* bytes are ciphertext: meaning obscured */
#define BR_F_SIGNED    0x02u /* content carries a digital signature */
#define BR_F_AEAD      0x04u /* authenticated by a session AEAD tag */
#define BR_F_PAYMENT   0x08u /* moves value: payment, escrow, settlement */
#define BR_F_ALL       0x0Fu

typedef struct {
    uint8_t kind;          /* br_kind_t */
    uint16_t mtu;          /* default bytes per link frame */
    uint16_t mtu_max;      /* largest a host may tune it to */
    uint32_t bitrate_bps;  /* nominal */
    uint8_t metered;       /* costs the user money per byte or per minute */
    uint8_t encrypt_ok;    /* may carry ciphertext */
    uint8_t settle_ok;     /* may carry settlement and payment records */
    uint8_t needs_auth;    /* every message signed or AEAD-authenticated */
    uint8_t text_only;     /* bytes must be printable ASCII */
    uint8_t classes;       /* BR_CLS() mask of accepted classes */
    uint32_t max_msg;      /* largest message, bytes */
    uint32_t cost_per_kib; /* default cost units per KiB (host may tune) */
    uint32_t base_rtt_us;  /* prior round-trip time before any sample */
} br_caps_t;

/* NULL for an unknown kind. */
const br_caps_t *br_caps(uint8_t kind);
const char *br_kind_name(uint8_t kind);

/* B2. BR_OK or the first gate that refuses. caps may be NULL (table caps). */
br_status_t br_policy_check(uint8_t kind, uint8_t cls, uint8_t flags, const uint8_t *data,
                            uint32_t len);

/* ===== links ===== */
typedef struct {
    void *ctx;
    /* Send one whole message to device `to` (all zero = broadcast). The
     * link does its own framing / fragmentation. Return BR_OK, BR_ERR_BUSY
     * (try another link, keep this one up) or any other error (link down). */
    int (*send)(void *ctx, const uint8_t to[BR_ID_BYTES], const uint8_t *data, uint32_t len);
} br_link_ops_t;

typedef struct {
    uint8_t used, up, kind;
    uint16_t id;
    br_caps_t caps;
    br_link_ops_t ops;
    uint64_t rtt8;   /* EWMA of RTT in microseconds, times 8 */
    uint32_t loss16; /* EWMA of loss in ppm, times 16 */
    uint32_t rtt_n;  /* RTT samples taken */
    uint32_t loss_n; /* loss samples taken (answered or not) */
    uint32_t fails;  /* send() errors that took the link down */
} br_link_t;

typedef struct {
    br_link_t l[BR_MAX_LINKS];
    uint32_t failovers;
} br_table_t;

void br_table_init(br_table_t *t);
/* Add a link (starts up). id must be unique. */
br_status_t br_link_add(br_table_t *t, uint8_t kind, uint16_t id, const br_link_ops_t *ops);
/* Narrow a link: mtu in [1, mtu_max], bitrate > 0; 0 keeps the field. */
br_status_t br_link_tune(br_table_t *t, uint16_t id, uint16_t mtu, uint32_t bitrate_bps,
                         uint32_t cost_per_kib);
br_status_t br_link_set_up(br_table_t *t, uint16_t id, bool up);
/* One measurement: lost = no answer (rtt ignored). */
br_status_t br_link_measure(br_table_t *t, uint16_t id, uint32_t rtt_us, bool lost);
br_link_t *br_link_get(br_table_t *t, uint16_t id);
uint32_t br_link_rtt_us(const br_link_t *l);
uint32_t br_link_loss_ppm(const br_link_t *l);

/* B4 score of a message on a link (UINT64_MAX if the link can't take it). */
uint64_t br_score(const br_link_t *l, uint8_t cls, uint32_t len);

/* Pick the best link (B4). *id receives its id. BR_ERR_NOLINK if none. */
br_status_t br_select(const br_table_t *t, uint8_t cls, uint8_t flags, const uint8_t *data,
                      uint32_t len, uint16_t *id);

/* Send with failover (B5). *used_id (may be NULL) gets the link that took it. */
br_status_t br_send(br_table_t *t, const uint8_t to[BR_ID_BYTES], uint8_t cls, uint8_t flags,
                    const uint8_t *data, uint32_t len, uint16_t *used_id);

/* ===== small helpers shared by the bearer files ===== */
uint64_t br_udiv64(uint64_t n, uint64_t d); /* d == 0 returns UINT64_MAX */
void br_mcpy(void *dst, const void *src, size_t n);
void br_mset(void *dst, uint8_t v, size_t n);
bool br_meq(const void *a, const void *b, size_t n);
bool br_printable(const uint8_t *p, uint32_t n); /* 0x20..0x7E, \t \r \n */

#endif /* ZXV_BEARER_H */
