/* wifi.h — ZEDEC XERO pqOS Wi-Fi (IEEE 802.11) STATION/AP subsystem
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 * 36N9 Genetics, LLC
 *
 * Build note: this header needs -Iinclude -Isrc/edp_risk -Isrc/surplus
 * (m5_coords_t lives in edp_risk.h, which pulls in surplus.h).
 *
 * =====================================================================
 * WHAT THIS SUBSYSTEM IS, AND WHAT IT IS NOT
 * =====================================================================
 *
 * There is no Wi-Fi silicon in this tree. Everything below is split along
 * that line, deliberately and visibly:
 *
 *   SOFTWARE (real, complete, tested here)
 *     - 802.11 MAC frame header construction and parsing, including the
 *       4-address / QoS variants and sequence-control packing
 *     - beacon / probe-response information-element parsing (SSID, DS param,
 *       supported rates, RSN, WPA vendor IE, HT/VHT/HE presence)
 *     - the scan-result table (merge by BSSID, weakest-entry eviction, and a
 *       learned SSID that survives a later nameless beacon from the same AP)
 *     - the association state machine, with an explicit legal-transition
 *       table; illegal transitions are refused, not silently applied
 *     - channel <-> frequency mapping for 2.4 / 5 / 6 GHz, against the real
 *       operating-channel sets, not open-ended arithmetic
 *     - SHA-1, HMAC-SHA1, PBKDF2-HMAC-SHA1, the 802.11i PRF, WPA2 PMK and
 *       PTK derivation, and the EAPOL-Key MIC
 *     - the supplicant half of the WPA2 4-way handshake (M1 -> M2, M3 -> M4)
 *     - the TX/RX staging rings over the device's DMA buffers
 *
 *   HARDWARE (absent; reached only through wifi_ops_t)
 *     - scanning, transmit, receive, channel set, TX power, power save,
 *       AP beaconing, and the entropy source used for the SNonce
 *
 * Every operation in the second list returns WIFI_ENODEV when no ops struct
 * is bound. In particular wifi_scan() with no radio reports WIFI_ENODEV and
 * leaves the scan table exactly as it found it: it never invents an access
 * point, and num_scan_results does not move. Counters (tx_packets, rx_bytes,
 * ...) advance only for bytes that a bound backend actually accepted or
 * produced.
 *
 * =====================================================================
 * LIMITATIONS — please read before relying on any of this
 * =====================================================================
 *
 *  L1. NO RADIO IS INCLUDED. Binding wifi_ops_t is the whole job of a real
 *      driver. Unbound, this subsystem is a protocol engine, not a NIC.
 *
 *  L2. CRYPTO ANCHORS, AND WHERE THEY STOP.
 *      Anchored to published vectors in test_wifi.c:
 *        - SHA-1            FIPS 180-1 (4 vectors, incl. the 1,000,000-'a')
 *        - HMAC-SHA1        RFC 2202 cases 1, 2, 3, 6
 *        - PBKDF2-HMAC-SHA1 RFC 6070 cases 1, 2, 3, 5, 6
 *        - WPA2 PMK         IEEE 802.11i Annex H.4 cases 1 and 2
 *      NOT anchored, and deliberately so:
 *        - 802.11i Annex H.4 case 3 uses a 64-character passphrase, one
 *          octet outside the 8..63 range the same standard specifies, and
 *          its expected PSK could not be confirmed from a trusted source
 *          here. It is not asserted. Six other vectors already pin PBKDF2
 *          byte-for-byte, so this is a gap in citation, not in confidence.
 *        - The 802.11i PRF-384 and the PTK split on top of it are
 *          implemented from the specification text and checked only
 *          STRUCTURALLY: determinism, the Min/Max canonical ordering, KCK /
 *          KEK / TK separation, single-bit avalanche on PMK, MAC and nonce,
 *          and that the PRF output equals HMAC-SHA1(K, label||0x00||data||i)
 *          concatenated. No published PTK vector is asserted. Treat the PTK
 *          layer as "believed correct, not vector-anchored".
 *
 *  L3. WPA2-PSK ONLY, SUPPLICANT SIDE ONLY. Key-descriptor version 2
 *      (HMAC-SHA1-128 MIC, CCMP) is implemented. Version 1 (RC4/HMAC-MD5)
 *      and version 3 (AES-128-CMAC, used by WPA3/802.11w) return
 *      WIFI_ENOTSUP: there is no MD5 and no AES-CMAC here, and guessing a
 *      MIC would be worse than refusing. The authenticator (AP) side of the
 *      4-way handshake is NOT implemented.
 *
 *  L4. NO GTK INSTALL, NO KEY-DATA DECRYPTION. M3 carries the group key
 *      inside encrypted Key Data (AES key wrap under the KEK). That unwrap
 *      is not implemented, so the GTK is not extracted; the pairwise TK is.
 *      wifi_wpa_rx_eapol() reports this by leaving gtk_valid false.
 *
 *  L5. NO CIPHER. Deriving a TK is not the same as encrypting traffic. CCMP
 *      (AES-CCM) framing/encryption is NOT implemented; frames handed to
 *      wifi_tx_packet() go out exactly as the caller built them.
 *
 *  L6. wifi_connect() ACCEPTS ONLY WIFI_SEC_OPEN AND WIFI_SEC_WPA2.
 *      WIFI_SEC_WEP needs RC4, WIFI_SEC_WPA needs HMAC-MD5, and WIFI_SEC_WPA3
 *      needs SAE — none of those primitives exist in this tree, so those
 *      suites return WIFI_ENOTSUP instead of being half-attempted. Beacon
 *      parsing still RECOGNISES all of them, including the SAE AKM, so a
 *      scan reports WIFI_SEC_WPA3 correctly; we simply cannot join one.
 *
 *  L7. RATE REPORTING IS LEGACY-ONLY. wifi_scan_result_t.max_rate_mbps is
 *      the highest rate advertised in the Supported/Extended Rates IEs. The
 *      HT/VHT/HE MCS rate tables are not decoded, so a Wi-Fi 6 AP reports
 *      54 Mbps here, not its real PHY rate. standard/band are decoded.
 *
 *  L8. NO HT CONTROL FIELD. Frames with the Order bit set (the +HTC variant)
 *      are refused by both the builder and the parser with WIFI_ENOTSUP
 *      rather than being mis-sized by four bytes.
 *
 *  L9. NO FRAGMENTATION, NO AGGREGATION, NO PS-POLL/TIM HANDLING, NO
 *      ROAMING/FT, NO MESH PEERING, NO P2P/WPS. Only STATION and AP have
 *      behaviour behind them. Precisely what the mode values do:
 *        - WIFI_MODE_MESH and WIFI_MODE_MONITOR are REFUSED outright by
 *          wifi_create_interface() (id 0), because supports_mesh and
 *          supports_monitor are false and pretending otherwise would be the
 *          lie this file exists to avoid.
 *        - WIFI_MODE_ADHOC and WIFI_MODE_P2P are accepted as LABELS only.
 *          An interface may carry them and may enter BEACONING (ADHOC), but
 *          no IBSS or P2P protocol is implemented behind either.
 *
 * L10. MAC ADDRESSES. With no hardware to ask, wifi_create_interface()
 *      assigns a deterministic LOCALLY ADMINISTERED address derived from the
 *      device name and interface index (bit 0x02 of octet 0 set, multicast
 *      bit clear). It is a placeholder, flagged as mac_is_placeholder, and
 *      is overwritten by ops->get_mac() when a backend is bound or by
 *      wifi_set_mac().
 */
#ifndef WIFI_H
#define WIFI_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "edp_risk.h"   /* m5_coords_t */

/* ===== Return codes =====
 * Every int-returning function in this header returns WIFI_OK (0) or a
 * negative code, EXCEPT the frame builders/parsers, which return a positive
 * byte count on success. Nothing here returns success for work it did not do. */
#define WIFI_OK          0
#define WIFI_ENODEV    (-1)   /* no radio backend bound — nothing happened */
#define WIFI_EINVAL    (-2)   /* bad argument */
#define WIFI_ESTATE    (-3)   /* illegal state transition / wrong mode */
#define WIFI_EBUSY     (-4)   /* interface busy (e.g. mid-scan) */
#define WIFI_ENOSPC    (-5)   /* table or ring full */
#define WIFI_EAGAIN    (-6)   /* nothing available right now */
#define WIFI_EAUTH     (-7)   /* authentication / MIC failure */
#define WIFI_ENOTSUP   (-8)   /* understood, deliberately not implemented */
#define WIFI_EMSGSIZE  (-9)   /* buffer too small for the message */
#define WIFI_EIO      (-10)   /* the bound backend reported a failure */

/* ===== Wi-Fi standards ===== */
typedef enum {
    WIFI_802_11A = 0,
    WIFI_802_11B,
    WIFI_802_11G,
    WIFI_802_11N,
    WIFI_802_11AC,
    WIFI_802_11AX,  /* Wi-Fi 6 */
    WIFI_802_11BE,  /* Wi-Fi 7 */
} wifi_standard_t;

/* ===== Security modes ===== */
typedef enum {
    WIFI_SEC_OPEN = 0,
    WIFI_SEC_WEP,
    WIFI_SEC_WPA,
    WIFI_SEC_WPA2,
    WIFI_SEC_WPA3,
    WIFI_SEC_WPA2_WPA3,
} wifi_security_t;

/* ===== Operating modes ===== */
typedef enum {
    WIFI_MODE_STATION = 0,  /* Client */
    WIFI_MODE_AP,           /* Access point */
    WIFI_MODE_ADHOC,
    WIFI_MODE_MESH,
    WIFI_MODE_MONITOR,
    WIFI_MODE_P2P,
} wifi_mode_t;

/* ===== Channel/band ===== */
typedef enum {
    WIFI_BAND_2_4GHZ = 0,
    WIFI_BAND_5GHZ,
    WIFI_BAND_6GHZ,
    WIFI_BAND_DUAL,
    WIFI_BAND_TRI,
} wifi_band_t;

/* ===== Association state machine =====
 * A STA walks IDLE -> AUTHENTICATING -> ASSOCIATING -> (HANDSHAKING) ->
 * CONNECTED. An AP walks IDLE -> BEACONING. Anything else is refused by
 * wifi_state_set(); see wifi_state_can_transition(). */
typedef enum {
    WIFI_STATE_IDLE = 0,
    WIFI_STATE_SCANNING,
    WIFI_STATE_AUTHENTICATING,
    WIFI_STATE_ASSOCIATING,
    WIFI_STATE_HANDSHAKING,   /* WPA2 4-way in progress */
    WIFI_STATE_CONNECTED,
    WIFI_STATE_BEACONING,     /* AP mode: hostapd-equivalent is up */
    WIFI_STATE_FAILED,
    WIFI_STATE__COUNT
} wifi_assoc_state_t;

/* ===== Scan result ===== */
typedef struct {
    char ssid[33];
    uint8_t bssid[6];
    int16_t rssi;          /* Signal strength dBm */
    uint32_t channel;
    wifi_standard_t standard;
    wifi_security_t security;
    wifi_band_t band;
    bool hidden;
    uint32_t max_rate_mbps;   /* legacy rates only — see L7 */
} wifi_scan_result_t;

#define WIFI_MAX_SCAN_RESULTS  64
#define WIFI_MAX_SSID_LEN      33
#define WIFI_MAX_INTERFACES    4

/* ===== 802.11 frame types ===== */
#define WIFI_FTYPE_MGMT        0
#define WIFI_FTYPE_CTRL        1
#define WIFI_FTYPE_DATA        2

#define WIFI_STYPE_ASSOC_REQ   0
#define WIFI_STYPE_ASSOC_RESP  1
#define WIFI_STYPE_REASSOC_REQ 2
#define WIFI_STYPE_REASSOC_RSP 3
#define WIFI_STYPE_PROBE_REQ   4
#define WIFI_STYPE_PROBE_RESP  5
#define WIFI_STYPE_BEACON      8
#define WIFI_STYPE_DISASSOC   10
#define WIFI_STYPE_AUTH       11
#define WIFI_STYPE_DEAUTH     12

#define WIFI_STYPE_DATA        0
#define WIFI_STYPE_NULLFUNC    4
#define WIFI_STYPE_QOS_DATA    8

#define WIFI_MAC_HDR_MIN      24   /* 3 addresses, no QoS */
#define WIFI_MAC_HDR_MAX      32   /* 4 addresses + QoS control */
#define WIFI_SEQ_MODULO     4096

/* Information element IDs we understand */
#define WIFI_EID_SSID          0
#define WIFI_EID_RATES         1
#define WIFI_EID_DSPARAM       3
#define WIFI_EID_HT_CAP       45
#define WIFI_EID_RSN          48
#define WIFI_EID_EXT_RATES    50
#define WIFI_EID_VHT_CAP     191
#define WIFI_EID_VENDOR      221
#define WIFI_EID_EXTENSION   255
#define WIFI_EID_EXT_HE_CAP   35

/* ===== Parsed 802.11 MAC header =====
 * Field order here mirrors the on-air layout so the mapping stays obvious. */
typedef struct {
    uint8_t  type;          /* WIFI_FTYPE_* */
    uint8_t  subtype;       /* WIFI_STYPE_* */
    bool     to_ds, from_ds;
    bool     more_frag, retry, pwr_mgmt, more_data, protected_frame, order;
    uint16_t duration_id;
    uint8_t  addr1[6];      /* RA / DA / BSSID, depending on the DS bits */
    uint8_t  addr2[6];      /* TA / SA / BSSID */
    uint8_t  addr3[6];      /* DA / SA / BSSID */
    uint8_t  addr4[6];      /* SA, only when to_ds && from_ds */
    bool     has_addr4;     /* set by the parser; implied by the DS bits */
    uint16_t seq_num;       /* 0..4095 */
    uint8_t  frag_num;      /* 0..15   */
    bool     qos;           /* set by the parser for QoS data subtypes */
    uint16_t qos_ctl;
} wifi_mac_hdr_t;

/* ===== WPA2 key material ===== */
typedef struct {
    uint8_t kck[16];   /* EAPOL-Key confirmation key (the MIC key) */
    uint8_t kek[16];   /* EAPOL-Key encryption key   (unused — see L4) */
    uint8_t tk[16];    /* CCMP temporal key          (unused — see L5) */
} wifi_ptk_t;

/* Supplicant context for one interface. */
typedef struct {
    uint8_t  pmk[32];
    bool     pmk_valid;
    uint8_t  anonce[32];      /* from M1 */
    uint8_t  snonce[32];      /* ours */
    bool     anonce_valid, snonce_valid;
    wifi_ptk_t ptk;
    bool     ptk_derived;     /* PTK computed from M1; usable for MIC checks */
    bool     ptk_valid;       /* PTK installed — only after a verified M3 */
    bool     gtk_valid;       /* always false — see L4 */
    uint64_t replay_counter;  /* highest accepted */
    bool     replay_seen;
    uint8_t  aa[6];           /* authenticator address (the AP) */
    uint8_t  spa[6];          /* supplicant address (us) */
    uint8_t  msg_mask;        /* bit n-1 set once Mn has been accepted */
    uint16_t key_desc_version;
} wifi_supplicant_t;

/* ===== Wi-Fi interface ===== */
typedef struct {
    uint32_t iface_id;              /* 1-based; 0 means "no interface" */
    char name[32];
    wifi_mode_t mode;
    wifi_standard_t standard;
    wifi_band_t band;
    bool up;
    bool connected;

    /* Connection info */
    char ssid[WIFI_MAX_SSID_LEN];
    uint8_t bssid[6];
    uint32_t channel;
    int16_t rssi;
    wifi_security_t security;

    /* IP info (filled in by the DHCP client, not by this module) */
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns1, dns2;

    /* Stats — only ever incremented for bytes a backend really moved */
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t tx_dropped;
    uint64_t rx_dropped;

    /* QoS */
    uint8_t wmm_enabled;
    uint8_t power_save_level;   /* 0 = off, 1 = light, 2 = deep */

    /* MAC / sequencing / association */
    uint8_t  mac[6];
    bool     mac_is_placeholder;   /* see L10 */
    uint16_t seq_num;              /* next sequence number, mod 4096 */
    wifi_assoc_state_t state;
    wifi_supplicant_t  sup;
} wifi_interface_t;

/* ===== Radio backend =====
 * Everything that needs silicon goes through here. A real driver fills this
 * in and calls wifi_bind_ops(); nothing in wifi.c reaches around it.
 *
 * Convention for the int-returning members: >= 0 on success (rx_poll returns
 * the frame length, 0 meaning "nothing waiting"), < 0 on failure. A NULL
 * member is treated exactly like an absent backend: WIFI_ENODEV. */
typedef struct wifi_ops {
    int (*scan_start)(void *ctx, uint32_t channel_hint);
    int (*tx_frame)(void *ctx, const uint8_t *frame, uint32_t len);
    int (*rx_poll)(void *ctx, uint8_t *frame, uint32_t cap);
    int (*set_channel)(void *ctx, uint32_t channel, uint32_t freq_mhz);
    int (*set_power_save)(void *ctx, uint8_t level);
    int (*set_tx_power)(void *ctx, uint8_t dbm);
    int (*ap_start)(void *ctx, const char *ssid, uint32_t channel);
    int (*ap_stop)(void *ctx);
    int (*get_random)(void *ctx, uint8_t *out, uint32_t n);
    int (*get_mac)(void *ctx, uint32_t iface_index, uint8_t mac[6]);
    void *ctx;
} wifi_ops_t;

/* ===== Wi-Fi device (hardware-as-code) ===== */
typedef struct {
    uint32_t device_id;
    char name[128];

    /* Registers */
    uint32_t reg_command;
    uint32_t reg_status;
    uint32_t reg_channel;
    uint32_t reg_tx_power;

    /* Staging rings for packet TX/RX. Records are length-prefixed
     * (2 bytes, little endian) and wrap; *_used tracks occupancy so that
     * "full" and "empty" are distinguishable. */
    uint8_t tx_dma[4096];
    uint8_t rx_dma[4096];
    uint32_t tx_head, tx_tail;
    uint32_t rx_head, rx_tail;
    uint32_t tx_used, rx_used;

    /* IRQ */
    bool irq_rx_ready;
    bool irq_tx_done;
    bool irq_scan_done;
    bool irq_connected;
    bool irq_disconnected;
    bool irq_auth_failed;

    /* Interfaces */
    wifi_interface_t ifaces[WIFI_MAX_INTERFACES];
    uint32_t num_ifaces;

    /* Scan results */
    wifi_scan_result_t scan_results[WIFI_MAX_SCAN_RESULTS];
    uint32_t num_scan_results;
    bool scanning;

    /* Capabilities */
    wifi_standard_t max_standard;
    wifi_band_t max_band;
    uint32_t max_rate_mbps;
    bool supports_ap;
    bool supports_mesh;
    bool supports_monitor;
    bool supports_wpa3;
    uint8_t max_tx_power;

    /* Radio backend — NULL until wifi_bind_ops() */
    const wifi_ops_t *ops;

    /* M5 coordinates */
    m5_coords_t m5;
    uint32_t coverage_r; /* Q16.16 fraction in [0, 1] */
    uint32_t coverage_l; /* Q16.16 fraction in [0, 1] */
} wifi_device_t;

/* ===== Lifecycle ===== */
/* wifi_init tolerates dev == NULL (the ARM32 boot path calls it that way). */
void wifi_init(wifi_device_t *dev, const char *name);
uint32_t wifi_create_interface(wifi_device_t *dev, const char *name, wifi_mode_t mode);
wifi_interface_t *wifi_get_interface(wifi_device_t *dev, uint32_t iface_id);
int wifi_set_mac(wifi_device_t *dev, uint32_t iface_id, const uint8_t mac[6]);

/* ===== Radio backend binding ===== */
int  wifi_bind_ops(wifi_device_t *dev, const wifi_ops_t *ops);
void wifi_unbind_ops(wifi_device_t *dev);
bool wifi_has_radio(const wifi_device_t *dev);

/* ===== Operations that need the radio (WIFI_ENODEV when unbound) =====
 * Arguments are validated BEFORE the backend is consulted, so a caller can
 * exercise the validation without a radio: a bad channel is WIFI_EINVAL even
 * on a device with no ops bound. */
int wifi_scan(wifi_device_t *dev, uint32_t iface_id);
int wifi_scan_complete(wifi_device_t *dev, uint32_t iface_id);  /* driver ISR path */
int wifi_connect(wifi_device_t *dev, uint32_t iface_id, const char *ssid,
                 const char *password, wifi_security_t security);
int wifi_disconnect(wifi_device_t *dev, uint32_t iface_id);
int wifi_start_ap(wifi_device_t *dev, uint32_t iface_id, const char *ssid,
                  const char *password, uint32_t channel);
int wifi_stop_ap(wifi_device_t *dev, uint32_t iface_id);
int wifi_set_channel(wifi_device_t *dev, uint32_t iface_id, uint32_t channel);
int wifi_set_power_save(wifi_device_t *dev, uint32_t iface_id, uint8_t level);
/* Conducted TX power in dBm. Refused above WIFI_MAX_TX_POWER_DBM, and above
 * dev->max_tx_power once a driver has said what the radio can actually do.
 * On success dev->reg_tx_power holds what the radio was really set to. */
#define WIFI_MAX_TX_POWER_DBM 30
int wifi_set_tx_power(wifi_device_t *dev, uint32_t iface_id, uint8_t dbm);
int wifi_tx_packet(wifi_device_t *dev, uint32_t iface_id, const void *data, uint32_t len);
int wifi_rx_packet(wifi_device_t *dev, uint32_t iface_id, void *data, uint32_t max_len);

/* Deferred TX: enqueue now, hand to the radio later. Enqueueing is NOT a
 * transmit and does not touch tx_packets; only wifi_tx_flush() does. */
int wifi_tx_enqueue(wifi_device_t *dev, const void *data, uint32_t len);
int wifi_tx_flush(wifi_device_t *dev, uint32_t iface_id);
uint32_t wifi_tx_pending(const wifi_device_t *dev);

/* Driver ingress: a real ISR pushes received frames in here. */
int wifi_rx_inject(wifi_device_t *dev, uint32_t iface_id, const uint8_t *frame, uint32_t len);
uint32_t wifi_rx_pending(const wifi_device_t *dev);

/* ===== Scan result table (no radio needed) ===== */
uint32_t wifi_get_scan_count(wifi_device_t *dev);
wifi_scan_result_t *wifi_get_scan_result(wifi_device_t *dev, uint32_t index);
int  wifi_scan_add_result(wifi_device_t *dev, const wifi_scan_result_t *r);
int  wifi_scan_find_bssid(const wifi_device_t *dev, const uint8_t bssid[6]);
int  wifi_scan_find_ssid(const wifi_device_t *dev, const char *ssid);
void wifi_scan_clear(wifi_device_t *dev);

/* ===== SSID / BSSID helpers ===== */
int  wifi_ssid_set(char *dst33, const char *src);       /* WIFI_EINVAL if > 32 */
bool wifi_ssid_equal(const char *a, const char *b);
bool wifi_bssid_is_zero(const uint8_t bssid[6]);
bool wifi_bssid_is_broadcast(const uint8_t bssid[6]);
int  wifi_bssid_format(const uint8_t bssid[6], char *out, uint32_t cap); /* "aa:bb:.." */

/* ===== Channel <-> frequency =====
 * channel_to_freq needs a band because channel numbers repeat across bands
 * (channel 1 is 2412 MHz and also 5955 MHz). WIFI_BAND_DUAL/TRI are resolved
 * as: 1..14 -> 2.4 GHz, >= 32 -> 5 GHz; 6 GHz cannot be named that way, use
 * wifi_set_freq()/an explicit band. Returns 0 for anything not a real
 * operating channel. */
uint32_t wifi_channel_to_freq(uint32_t channel, wifi_band_t band);
uint32_t wifi_freq_to_channel(uint32_t freq_mhz);   /* 0 if not an operating freq */
int      wifi_freq_to_band(uint32_t freq_mhz);      /* wifi_band_t or WIFI_EINVAL */
bool     wifi_channel_valid(uint32_t channel, wifi_band_t band);
int      wifi_set_freq(wifi_device_t *dev, uint32_t iface_id, uint32_t freq_mhz);

/* ===== Association state machine ===== */
bool wifi_state_can_transition(wifi_assoc_state_t from, wifi_assoc_state_t to);
int  wifi_state_set(wifi_device_t *dev, uint32_t iface_id, wifi_assoc_state_t to);
const char *wifi_state_name(wifi_assoc_state_t s);

/* ===== 802.11 frame construction / parsing =====
 * Builders return the number of bytes written; parsers return the number of
 * bytes consumed. Both return a negative WIFI_* code on failure. */
int wifi_mac_hdr_build(const wifi_mac_hdr_t *h, uint8_t *out, uint32_t cap);
int wifi_mac_hdr_parse(const uint8_t *in, uint32_t len, wifi_mac_hdr_t *out);
uint32_t wifi_mac_hdr_len(const wifi_mac_hdr_t *h);
/* Resolve addr1/2/3/4 into DA/SA/BSSID per the to_ds/from_ds combination.
 * The 4-address (WDS) case has no single BSSID and returns WIFI_ENOTSUP. */
int wifi_frame_addrs(const wifi_mac_hdr_t *h, uint8_t da[6], uint8_t sa[6],
                     uint8_t bssid[6]);
uint16_t wifi_next_seq(wifi_interface_t *iface);

int wifi_build_auth(uint8_t *out, uint32_t cap, const uint8_t bssid[6],
                    const uint8_t sa[6], uint16_t seq, uint16_t auth_alg,
                    uint16_t auth_seq, uint16_t status);
int wifi_build_assoc_req(uint8_t *out, uint32_t cap, const uint8_t bssid[6],
                         const uint8_t sa[6], uint16_t seq, const char *ssid,
                         uint16_t cap_info, uint16_t listen_interval);
int wifi_build_deauth(uint8_t *out, uint32_t cap, const uint8_t da[6],
                      const uint8_t sa[6], const uint8_t bssid[6], uint16_t seq,
                      uint16_t reason);
int wifi_build_beacon(wifi_device_t *dev, uint32_t iface_id, uint8_t *out, uint32_t cap);

/* Parse a beacon or probe response into a scan result.
 *
 * freq_mhz is the frequency the frame was actually heard on, and when it is
 * non-zero it WINS: it fixes both channel and band, overriding a stale or
 * lying DS param element.
 *
 * freq_mhz == 0 means "not measured". Then the DS param element is the only
 * evidence: a channel of 1..14 is reported as 2.4 GHz, anything else as
 * 5 GHz. Note the honest edge: if the frame carries NO DS param either, then
 * nothing is known, out->channel is left 0 — an impossible channel, so the
 * result cannot be connected to (wifi_connect() fails EINVAL on it) — and
 * out->band falls in the 5 GHz branch by default rather than by evidence.
 * Pass the measured frequency whenever you have it. */
int wifi_parse_beacon(const uint8_t *frame, uint32_t len, int16_t rssi,
                      uint32_t freq_mhz, wifi_scan_result_t *out);
/* Locate an information element in a management frame body. Returns the
 * element length and sets *val to its first byte, or WIFI_EINVAL. */
int wifi_find_ie(const uint8_t *ies, uint32_t len, uint8_t id, const uint8_t **val);

/* Driver ingress for management frames: drives the association state machine
 * (auth resp -> assoc req, assoc resp -> handshake/connected, deauth -> idle).
 * Returns the management subtype handled, or a negative WIFI_* code.
 *
 * `rssi` is the signal strength the radio measured for THIS frame, in dBm. It
 * is used only for beacons/probe responses, where it becomes the scan result's
 * rssi. There is no way to derive it here, so it is a parameter rather than a
 * guess: a scan result never reports a signal strength nobody measured. */
int wifi_rx_mgmt(wifi_device_t *dev, uint32_t iface_id, const uint8_t *frame,
                 uint32_t len, int16_t rssi);

/* ===== Crypto primitives (no radio needed; vector-anchored — see L2) ===== */
#define WIFI_SHA1_LEN 20
typedef struct {
    uint32_t h[5];
    uint8_t  buf[64];
    uint32_t buf_len;
    uint64_t total;      /* bytes */
} wifi_sha1_ctx_t;

void wifi_sha1_init(wifi_sha1_ctx_t *c);
void wifi_sha1_update(wifi_sha1_ctx_t *c, const uint8_t *data, uint32_t len);
void wifi_sha1_final(wifi_sha1_ctx_t *c, uint8_t out[WIFI_SHA1_LEN]);
void wifi_sha1(const uint8_t *data, uint32_t len, uint8_t out[WIFI_SHA1_LEN]);

/* Streaming HMAC — no staging buffer, therefore no message-length cap. */
typedef struct { wifi_sha1_ctx_t inner; uint8_t opad[64]; } wifi_hmac_sha1_ctx_t;
void wifi_hmac_sha1_init(wifi_hmac_sha1_ctx_t *c, const uint8_t *key, uint32_t klen);
void wifi_hmac_sha1_update(wifi_hmac_sha1_ctx_t *c, const uint8_t *data, uint32_t len);
void wifi_hmac_sha1_final(wifi_hmac_sha1_ctx_t *c, uint8_t out[WIFI_SHA1_LEN]);
void wifi_hmac_sha1(const uint8_t *key, uint32_t klen,
                    const uint8_t *msg, uint32_t mlen, uint8_t out[WIFI_SHA1_LEN]);

int wifi_pbkdf2_sha1(const uint8_t *pass, uint32_t plen,
                     const uint8_t *salt, uint32_t slen,
                     uint32_t iters, uint8_t *out, uint32_t outlen);

/* ===== WPA2 key derivation ===== */
#define WIFI_PMK_LEN 32
#define WIFI_PTK_LEN 48
/* PMK = PBKDF2-HMAC-SHA1(passphrase, ssid, 4096, 32). Passphrase must be
 * 8..63 printable characters, SSID 1..32 bytes, per IEEE 802.11i. */
int wifi_wpa_pmk(const char *passphrase, const char *ssid, uint8_t pmk[WIFI_PMK_LEN]);
/* IEEE 802.11i PRF-n: HMAC-SHA1(K, label || 0x00 || data || i), i = 0,1,... */
int wifi_wpa_prf(const uint8_t *key, uint32_t klen, const char *label,
                 const uint8_t *data, uint32_t dlen, uint8_t *out, uint32_t outlen);
int wifi_wpa_derive_ptk(const uint8_t pmk[WIFI_PMK_LEN],
                        const uint8_t aa[6], const uint8_t spa[6],
                        const uint8_t anonce[32], const uint8_t snonce[32],
                        wifi_ptk_t *ptk);

/* ===== EAPOL-Key (802.1X) ===== */
#define WIFI_EAPOL_HDR_LEN     4    /* version, type, body length */
#define WIFI_EAPOL_KEY_FIXED  99    /* incl. the 4-byte 802.1X header */
#define WIFI_EAPOL_MIC_OFF    81    /* offset of the 16-byte MIC field */
#define WIFI_EAPOL_TYPE_KEY    3

#define WIFI_KI_VERSION_MASK 0x0007
#define WIFI_KI_PAIRWISE     0x0008
#define WIFI_KI_INSTALL      0x0040
#define WIFI_KI_ACK          0x0080
#define WIFI_KI_MIC          0x0100
#define WIFI_KI_SECURE       0x0200
#define WIFI_KI_ERROR        0x0400
#define WIFI_KI_REQUEST      0x0800
#define WIFI_KI_ENCRYPTED    0x1000

typedef struct {
    uint8_t  descriptor_type;
    uint16_t key_info;
    uint16_t key_length;
    uint64_t replay_counter;
    uint8_t  nonce[32];
    uint8_t  rsc[8];
    uint8_t  mic[16];
    uint16_t key_data_len;
    uint32_t key_data_off;   /* offset within the frame */
    uint32_t total_len;
    uint8_t  msg;            /* 1..4 for a pairwise handshake, else 0 */
} wifi_eapol_key_t;

int wifi_eapol_key_parse(const uint8_t *frame, uint32_t len, wifi_eapol_key_t *out);
int wifi_eapol_key_build(const wifi_eapol_key_t *k, const uint8_t *key_data,
                         uint8_t *out, uint32_t cap);
/* MIC over the whole EAPOL frame with the MIC field zeroed (key descriptor
 * version 2 only: HMAC-SHA1-128). Version 1/3 return WIFI_ENOTSUP — see L3. */
int  wifi_eapol_mic(const uint8_t kck[16], const uint8_t *frame, uint32_t len,
                    uint16_t key_desc_version, uint8_t mic[16]);
/* Returns true only when the MIC in the frame matches a freshly computed one.
 * This function CAN and DOES fail; test_wifi.c flips a byte and requires it. */
bool wifi_eapol_mic_verify(const uint8_t kck[16], const uint8_t *frame, uint32_t len,
                           uint16_t key_desc_version);

/* ===== WPA2 4-way handshake, supplicant side ===== */
int wifi_wpa_start(wifi_device_t *dev, uint32_t iface_id, const char *passphrase);
/* Fixes the SNonce explicitly. Without this, M1 handling needs
 * ops->get_random and returns WIFI_ENODEV when no entropy source is bound —
 * a nonce is a security input and will not be invented here. */
int wifi_wpa_set_snonce(wifi_device_t *dev, uint32_t iface_id, const uint8_t snonce[32]);
/* Feed an EAPOL-Key frame. On success returns the message number (1 or 3) and
 * writes the reply (M2 or M4) into reply/reply_len. */
int wifi_wpa_rx_eapol(wifi_device_t *dev, uint32_t iface_id,
                      const uint8_t *frame, uint32_t len,
                      uint8_t *reply, uint32_t reply_cap, uint32_t *reply_len);

/* ===== IRQ ===== */
void wifi_handle_irq(wifi_device_t *dev);

/* ===== Coverage =====
 * r = fraction of interfaces whose advertised state agrees with their data
 *     (a CONNECTED interface must really hold an SSID, a BSSID, a valid
 *     channel and connected == true; a non-CONNECTED one must not claim to
 *     be connected).
 * l = 1.0 only when a radio backend able to both transmit and receive is
 *     bound; a Wi-Fi device with no radio covers nothing.
 * Both are exact small-integer fractions, so the floor is met only when
 * every interface is coherent AND a radio is present. This check fails on a
 * freshly initialised device — that is the intended, honest answer. */
#define WIFI_COVERAGE_FLOOR Q16_ONE /* 1.0 in Q16.16; r*l is compared in Q32.32 */
bool wifi_verify_coverage(wifi_device_t *dev);

#endif /* WIFI_H */
