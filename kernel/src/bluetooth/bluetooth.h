/* bluetooth.h — ZEDEC XERO pqOS Bluetooth host stack (BR/EDR)
 *
 * WHAT THIS IS
 *   The HOST half of a Bluetooth Classic stack: everything above the HCI
 *   transport boundary, and nothing below it. Byte-exact HCI command / event /
 *   ACL encoding and decoding, the discovered-device and bonding table, the
 *   per-link connection state machine, L2CAP B-frame framing with ACL
 *   fragmentation and reassembly, controller buffer credit accounting, and the
 *   protocol state of three profiles (A2DP source signalling, HIDP, SPP over
 *   RFCOMM).
 *
 * WHAT THIS IS NOT
 *   A radio driver. Every operation that must reach the air — inquiry, paging,
 *   pairing, any transmission — leaves through bt_ops_t, a two-function HCI
 *   transport that a board port fills in (UART H4, USB, SDIO). With no backend
 *   bound, every such call returns BT_ENODEV. In particular bt_inquiry() with
 *   no radio returns BT_ENODEV and leaves the device table EMPTY: this stack
 *   never invents a peer it did not hear. Entries appear only when real
 *   Inquiry Result bytes are handed to bt_hci_ingest().
 *
 * ===================== LIMITATIONS — READ THIS =====================
 *
 *   BR/EDR ONLY. There is no LE/BLE support of any kind: no advertising, no
 *   scanning, no LE connections, no ATT, no GATT, no SMP. bt_gatt_service_t
 *   and bluetooth_device_t::gatt_services[] exist because the struct is part
 *   of the ABI; num_gatt_services is always 0 and there are no bt_gatt_*
 *   functions. bt_version_t likewise only records what a controller reports.
 *
 *   PROFILES ARE PROTOCOL STATE, NOT MEDIA.
 *     A2DP  — AVDTP signalling (DISCOVER/SET_CONFIGURATION/OPEN/START/
 *             SUSPEND/CLOSE) plus RTP + SBC-header media framing. There is NO
 *             SBC ENCODER here: bt_audio_send() takes already-encoded frames
 *             and does not inspect them. Sink role is not implemented.
 *     HIDP  — transaction framing over the control/interrupt channels. Report
 *             descriptors are not fetched or parsed; boot protocol is not
 *             negotiated.
 *     SPP   — RFCOMM SABM/UA/DISC/UIH with the GSM 07.10 FCS. Parameter
 *             Negotiation (PN) and Modem Status Command (MSC) are NOT
 *             implemented, so there is NO credit-based flow control and no
 *             modem-signal exchange; peers that insist on either will not
 *             interoperate.
 *     HFP, HSP, AVRCP and LE_AUDIO appear in bt_profile_t for completeness and
 *     are NOT implemented. bt_connect() rejects them with BT_ENOTSUP.
 *
 *   SECURITY IS LEGACY PIN PAIRING, AND THE HOST DOES NO CRYPTO. The link key
 *   is derived by the CONTROLLER (E21/E22); this host only supplies the PIN in
 *   an HCI PIN Code Request Reply and stores whatever key the controller
 *   reports in HCI Link Key Notification. There is no Secure Simple Pairing,
 *   no numeric comparison, no out-of-band pairing, and no host-side key
 *   derivation or verification. Keys live in RAM only; nothing is persisted.
 *   bt_device_t::paired means "the link reached BT_STATE_PAIRED after an HCI
 *   Authentication Complete", not "this link is cryptographically proven".
 *   THE PIN IS RETAINED. bt_pair() copies the PIN into bt_device_t::pin and
 *   it stays there for the life of the record, in cleartext, and is replayed
 *   automatically on every later PIN Code Request for that peer. There is no
 *   bt_forget() — clear bt_device_t::pin_len yourself if that matters to you.
 *
 *   L2CAP IS BASIC MODE ONLY. B-frames, MTU configuration, and the fixed
 *   signalling channel 0x0001. No enhanced retransmission mode, no streaming
 *   mode, no FCS option, no fixed-channel LE signalling.
 *
 *   SDP IS NOT IMPLEMENTED. PSMs and RFCOMM server channels must be supplied
 *   by the caller; nothing is discovered.
 *
 *   NO SCO/eSCO. Only ACL is carried, so HFP voice cannot work even once the
 *   signalling exists.
 *
 *   STACK APPETITE. The framing path builds whole PDUs in automatic buffers,
 *   so it is stack-hungry: measured with gcc -fstack-usage on aarch64 -O2, the
 *   deepest chain is bt_poll (1088) -> bt_hci_ingest (192) ->
 *   rfcomm_send_frame (1040) -> l2cap_send (2096) -> bt_tx_flush (1136) =
 *   5552 bytes. That is comfortable on the 1 MB kernel stack this tree boots
 *   with, but do NOT call bt_poll()/bt_handle_irq() from a small dedicated
 *   interrupt stack without checking yours first.
 *
 *   VERIFICATION STATUS. The packet layouts are checked byte-for-byte against
 *   the Core Spec field order in test_bluetooth.c, and the state machines are
 *   driven end-to-end by a loopback controller model in that same file. NONE
 *   of it has been run against real Bluetooth silicon — no radio was available
 *   on the build host. Expect interop defects on first contact with hardware;
 *   the byte layouts are the part that is actually proven.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 * 36N9 Genetics, LLC
 */
#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "surplus.h"
#include "edp_risk.h"   /* m5_coords_t */

/* ===== Return codes =====
 * Every int-returning entry point uses these. A non-negative value is a real
 * result (BT_OK, or a byte/entry count); negative is a failure that DID NOT
 * happen. Nothing here ever reports success for work it could not do. */
#define BT_OK          0
#define BT_ENODEV    (-1)   /* no bt_ops_t bound — there is no radio */
#define BT_EINVAL    (-2)   /* caller argument is out of range or NULL */
#define BT_ENOENT    (-3)   /* address / handle / channel not known */
#define BT_ENOSPC    (-4)   /* a fixed-size table or queue is full */
#define BT_ESTATE    (-5)   /* illegal in the current protocol state */
#define BT_EIO       (-6)   /* the bound transport reported a failure */
#define BT_EAGAIN    (-7)   /* nothing available / no controller credits yet */
#define BT_EMSGSIZE  (-8)   /* larger than the negotiated MTU */
#define BT_ENOTSUP   (-9)   /* a real part of the spec we did not implement */
#define BT_EPROTO   (-10)   /* received bytes are malformed */

/* ===== Bluetooth versions ===== */
typedef enum {
    BT_VERSION_4_0 = 0,
    BT_VERSION_4_1,
    BT_VERSION_4_2,
    BT_VERSION_5_0,
    BT_VERSION_5_1,
    BT_VERSION_5_2,
    BT_VERSION_5_3,
    BT_VERSION_5_4,
} bt_version_t;

/* ===== Device classes ===== */
typedef enum {
    BT_CLASS_UNKNOWN = 0,
    BT_CLASS_PHONE,
    BT_CLASS_COMPUTER,
    BT_CLASS_HEADSET,
    BT_CLASS_SPEAKER,
    BT_CLASS_KEYBOARD,
    BT_CLASS_MOUSE,
    BT_CLASS_GAMEPAD,
    BT_CLASS_HEALTH,
    BT_CLASS_WEARABLE,
} bt_class_t;

/* ===== Profiles ===== */
typedef enum {
    BT_PROFILE_NONE = 0,
    BT_PROFILE_A2DP_SINK = 0x01,   /* NOT IMPLEMENTED (source role only) */
    BT_PROFILE_A2DP_SRC  = 0x02,
    BT_PROFILE_HFP       = 0x04,   /* NOT IMPLEMENTED (needs SCO) */
    BT_PROFILE_HSP       = 0x08,   /* NOT IMPLEMENTED (needs SCO) */
    BT_PROFILE_AVRCP     = 0x10,   /* NOT IMPLEMENTED */
    BT_PROFILE_HID       = 0x20,
    BT_PROFILE_SPP       = 0x40,
    BT_PROFILE_GATT      = 0x80,   /* NOT IMPLEMENTED (LE only) */
    BT_PROFILE_LE_AUDIO  = 0x100,  /* NOT IMPLEMENTED (LE only) */
} bt_profile_t;

/* ===== Connection states ===== */
typedef enum {
    BT_STATE_DISCONNECTED = 0,
    BT_STATE_CONNECTING,
    BT_STATE_CONNECTED,
    BT_STATE_PAIRING,
    BT_STATE_PAIRED,
} bt_state_t;

/* ===== Discovered device ===== */
#define BT_RSSI_UNKNOWN ((int16_t)-32768)  /* plain Inquiry Result carries none */

typedef struct {
    uint8_t bdaddr[6];
    char name[248];
    bt_class_t dev_class;
    uint32_t supported_profiles;
    int16_t rssi;
    uint32_t cod;       /* Class of Device */
    bool paired;
    bool connected;
    bt_state_t state;
    /* Bonding material reported by the controller (never derived here). */
    uint8_t link_key[16];
    bool link_key_valid;
    uint8_t link_key_type;
    uint8_t pin[16];
    uint8_t pin_len;    /* 0 = no PIN offered for this peer */
    bool name_valid;
} bt_device_t;

#define BT_MAX_DEVICES       32
#define BT_MAX_CONNECTIONS   7
#define BT_MAX_GATT_SERVICES 64

/* ===== GATT service (declared for ABI only — see LIMITATIONS) ===== */
typedef struct {
    uint16_t start_handle;
    uint16_t end_handle;
    uint8_t uuid[16];
    char name[64];
} bt_gatt_service_t;

/* ================= HCI wire constants (Core Spec Vol 4 Part E) =========== */

/* H4 UART transport packet indicators. Other transports (USB, SDIO) carry the
 * type out of band, which is why bt_ops_t passes it as a separate argument. */
#define BT_H4_CMD    0x01
#define BT_H4_ACL    0x02
#define BT_H4_SCO    0x03
#define BT_H4_EVT    0x04

/* Opcode Group Fields */
#define BT_OGF_LINK_CTL      0x01
#define BT_OGF_LINK_POLICY   0x02
#define BT_OGF_HOST_CTL      0x03
#define BT_OGF_INFO_PARAM    0x04
#define BT_OGF_STATUS_PARAM  0x05
#define BT_OGF_LE_CTL        0x08
#define BT_OGF_VENDOR        0x3F

/* Opcodes = (OGF << 10) | OCF, transmitted little-endian. */
#define BT_HCI_INQUIRY               0x0401
#define BT_HCI_INQUIRY_CANCEL        0x0402
#define BT_HCI_CREATE_CONNECTION     0x0405
#define BT_HCI_DISCONNECT            0x0406
#define BT_HCI_ACCEPT_CONN_REQ       0x0409
#define BT_HCI_REJECT_CONN_REQ       0x040A
#define BT_HCI_LINK_KEY_REQ_REPLY    0x040B
#define BT_HCI_LINK_KEY_REQ_NEG_REPLY 0x040C
#define BT_HCI_PIN_CODE_REQ_REPLY    0x040D
#define BT_HCI_PIN_CODE_REQ_NEG_REPLY 0x040E
#define BT_HCI_AUTH_REQUESTED        0x0411
#define BT_HCI_REMOTE_NAME_REQUEST   0x0419
#define BT_HCI_RESET                 0x0C03
#define BT_HCI_WRITE_LOCAL_NAME      0x0C13
#define BT_HCI_READ_LOCAL_NAME       0x0C14
#define BT_HCI_WRITE_SCAN_ENABLE     0x0C1A
#define BT_HCI_READ_LOCAL_VERSION    0x1001
#define BT_HCI_READ_BUFFER_SIZE      0x1005
#define BT_HCI_READ_BD_ADDR          0x1009

/* Event codes */
#define BT_EVT_INQUIRY_COMPLETE      0x01
#define BT_EVT_INQUIRY_RESULT        0x02
#define BT_EVT_CONN_COMPLETE         0x03
#define BT_EVT_CONN_REQUEST          0x04
#define BT_EVT_DISCONN_COMPLETE      0x05
#define BT_EVT_AUTH_COMPLETE         0x06
#define BT_EVT_REMOTE_NAME_COMPLETE  0x07
#define BT_EVT_CMD_COMPLETE          0x0E
#define BT_EVT_CMD_STATUS            0x0F
#define BT_EVT_NUM_COMP_PKTS         0x13
#define BT_EVT_PIN_CODE_REQUEST      0x16
#define BT_EVT_LINK_KEY_REQUEST      0x17
#define BT_EVT_LINK_KEY_NOTIFY       0x18
#define BT_EVT_INQUIRY_RESULT_RSSI   0x22
#define BT_EVT_EXT_INQUIRY_RESULT    0x2F

/* Inquiry: General/Unlimited Inquiry Access Code, sent LSB first. */
#define BT_GIAC   0x9E8B33u
#define BT_INQUIRY_LEN_MIN  0x01   /* units of 1.28 s */
#define BT_INQUIRY_LEN_MAX  0x30

/* Write Scan Enable bit values */
#define BT_SCAN_DISABLED     0x00
#define BT_SCAN_INQUIRY      0x01
#define BT_SCAN_PAGE         0x02
#define BT_SCAN_BOTH         0x03

/* ACL Packet Boundary flags (bits 12..13 of the handle word) */
#define BT_PB_CONTINUE     0x01
#define BT_PB_START_FLUSH  0x02
/* Broadcast flags (bits 14..15) */
#define BT_BC_POINT_TO_POINT 0x00

#define BT_HCI_CMD_HDR_LEN   3
#define BT_HCI_EVT_HDR_LEN   2
#define BT_HCI_ACL_HDR_LEN   4
#define BT_HCI_MAX_PARAM   255
/* Largest HCI packet this stack will build or queue. Bigger than any command
 * (3 + 255) and any ACL fragment a sane controller advertises. */
#define BT_MAX_HCI_PKT    1024

/* ADDRESS BYTE ORDER: BD_ADDRs are stored exactly as HCI transmits them,
 * least significant byte first. bdaddr[0] is the LSB. Every API here — the
 * device table, bt_connect(), bt_pair() — uses that same order, so bytes that
 * came off the wire can be handed straight back in. */

/* ================= L2CAP (Core Spec Vol 3 Part A) ================= */
#define BT_L2CAP_HDR_LEN     4
#define BT_L2CAP_CID_SIGNAL  0x0001
#define BT_L2CAP_CID_CONNLESS 0x0002
#define BT_L2CAP_CID_DYN_BASE 0x0040

#define BT_L2CAP_CMD_REJECT      0x01
#define BT_L2CAP_CONN_REQ        0x02
#define BT_L2CAP_CONN_RSP        0x03
#define BT_L2CAP_CONF_REQ        0x04
#define BT_L2CAP_CONF_RSP        0x05
#define BT_L2CAP_DISCONN_REQ     0x06
#define BT_L2CAP_DISCONN_RSP     0x07
#define BT_L2CAP_ECHO_REQ        0x08
#define BT_L2CAP_ECHO_RSP        0x09

#define BT_L2CAP_CONF_OPT_MTU    0x01
#define BT_L2CAP_DEFAULT_MTU     672   /* spec default for basic mode */

#define BT_PSM_SDP        0x0001
#define BT_PSM_RFCOMM     0x0003
#define BT_PSM_HID_CTRL   0x0011
#define BT_PSM_HID_INTR   0x0013
#define BT_PSM_AVCTP      0x0017
#define BT_PSM_AVDTP      0x0019

/* ================= RFCOMM (TS 07.10 / RFCOMM 1.1) ================= */
#define BT_RFCOMM_SABM  0x2F   /* with P/F set: 0x3F */
#define BT_RFCOMM_UA    0x63   /* with P/F set: 0x73 */
#define BT_RFCOMM_DM    0x0F
#define BT_RFCOMM_DISC  0x43   /* with P/F set: 0x53 */
#define BT_RFCOMM_UIH   0xEF   /* P/F clear for data */

/* ================= AVDTP (A2DP signalling) ================= */
#define BT_AVDTP_DISCOVER      0x01
#define BT_AVDTP_GET_CAPS      0x02
#define BT_AVDTP_SET_CONFIG    0x03
#define BT_AVDTP_GET_CONFIG    0x04
#define BT_AVDTP_RECONFIGURE   0x05
#define BT_AVDTP_OPEN          0x06
#define BT_AVDTP_START         0x07
#define BT_AVDTP_CLOSE         0x08
#define BT_AVDTP_SUSPEND       0x09
#define BT_AVDTP_ABORT         0x0A

#define BT_AVDTP_MSG_COMMAND   0x00
#define BT_AVDTP_MSG_GEN_REJ   0x01
#define BT_AVDTP_MSG_ACCEPT    0x02
#define BT_AVDTP_MSG_REJECT    0x03
#define BT_AVDTP_PKT_SINGLE    0x00

/* A2DP codec identifiers (Assigned Numbers) */
#define BT_A2DP_CODEC_SBC      0x00
#define BT_A2DP_CODEC_MPEG12   0x01
#define BT_A2DP_CODEC_AAC      0x02
#define BT_A2DP_CODEC_ATRAC    0x04
#define BT_A2DP_RTP_HDR_LEN    12
#define BT_A2DP_RTP_PAYLOAD_TYPE 96   /* dynamic */

/* ================= HIDP ================= */
#define BT_HIDP_HANDSHAKE     0x0
#define BT_HIDP_HID_CONTROL   0x1
#define BT_HIDP_GET_REPORT    0x4
#define BT_HIDP_SET_REPORT    0x5
#define BT_HIDP_GET_PROTOCOL  0x6
#define BT_HIDP_SET_PROTOCOL  0x7
#define BT_HIDP_DATA          0xA

#define BT_HIDP_RTYPE_OTHER   0x0
#define BT_HIDP_RTYPE_INPUT   0x1
#define BT_HIDP_RTYPE_OUTPUT  0x2
#define BT_HIDP_RTYPE_FEATURE 0x3

/* ================= Parsed-packet views =================
 * All parse functions return BORROWED pointers into the caller's buffer. They
 * copy nothing. The view is valid exactly as long as that buffer is. */
typedef struct {
    uint16_t opcode;
    uint8_t  ogf;
    uint16_t ocf;
    uint8_t  plen;
    const uint8_t *params;
} bt_hci_cmd_t;

typedef struct {
    uint8_t  code;
    uint8_t  plen;
    const uint8_t *params;
} bt_hci_event_t;

typedef struct {
    uint16_t handle;   /* 12 bits */
    uint8_t  pb;
    uint8_t  bc;
    uint16_t dlen;
    const uint8_t *data;
} bt_hci_acl_t;

typedef struct {
    uint16_t len;      /* payload length, header excluded */
    uint16_t cid;
    const uint8_t *payload;
} bt_l2cap_frame_t;

typedef struct {
    uint8_t  code;
    uint8_t  ident;
    uint16_t len;
    const uint8_t *data;
} bt_l2cap_sig_t;

typedef struct {
    uint8_t  dlci;
    uint8_t  cr;        /* command/response bit as transmitted */
    uint8_t  ctrl;      /* control field with the P/F bit masked off */
    bool     pf;
    uint16_t len;
    const uint8_t *data;
    uint8_t  fcs;
    bool     fcs_ok;
} bt_rfcomm_frame_t;

typedef struct {
    uint8_t  label;
    uint8_t  pkt_type;
    uint8_t  msg_type;
    uint8_t  signal_id;
    uint16_t plen;
    const uint8_t *params;
} bt_avdtp_msg_t;

/* ================= ACL reassembly ================= */
#define BT_REASM_MAX 1024

typedef struct {
    uint8_t  buf[BT_REASM_MAX];
    uint32_t have;      /* bytes accumulated so far, header included */
    uint32_t want;      /* total expected once the L2CAP header is in */
    uint16_t handle;
    bool     active;
    uint32_t drops;     /* continuation without a start, or overflow */
} bt_reasm_t;

/* ================= L2CAP channel ================= */
typedef enum {
    BT_CHAN_CLOSED = 0,
    BT_CHAN_CONN_SENT,     /* Connection Request out, no response yet */
    BT_CHAN_CONFIG,        /* connected, configuration exchange running */
    BT_CHAN_OPEN,          /* both configuration directions accepted */
} bt_chan_state_t;

typedef enum {
    BT_ROLE_NONE = 0,
    BT_ROLE_RFCOMM,
    BT_ROLE_AVDTP_SIG,
    BT_ROLE_AVDTP_MEDIA,
    BT_ROLE_HID_CTRL,
    BT_ROLE_HID_INTR,
} bt_chan_role_t;

typedef struct {
    uint16_t psm;
    uint16_t scid;       /* our CID — what the peer sends to */
    uint16_t dcid;       /* peer CID — what we send to */
    uint16_t out_mtu;    /* what the peer accepted for our direction */
    uint16_t in_mtu;     /* what we advertised */
    bt_chan_state_t state;
    bt_chan_role_t  role;
    bool cfg_out_done;   /* our Configuration Request was accepted */
    bool cfg_in_done;    /* we accepted the peer's Configuration Request */
    uint8_t last_ident;
} bt_l2cap_chan_t;

/* SDP is not used, so the worst case is RFCOMM + AVDTP signalling + AVDTP
 * media + HID control + HID interrupt = 5. One spare. */
#define BT_MAX_CHANNELS 6

/* ================= Profile session state ================= */
typedef enum {
    BT_A2DP_IDLE = 0,
    BT_A2DP_DISCOVERING,
    BT_A2DP_DISCOVERED,
    BT_A2DP_CONFIGURING,
    BT_A2DP_CONFIGURED,
    BT_A2DP_OPENING,
    BT_A2DP_OPEN,
    BT_A2DP_STARTING,
    BT_A2DP_STREAMING,
    BT_A2DP_SUSPENDING,
    BT_A2DP_CLOSING,
} bt_a2dp_state_t;

typedef enum {
    BT_RFCOMM_CLOSED = 0,
    BT_RFCOMM_SABM0_SENT,
    BT_RFCOMM_MUX_UP,
    BT_RFCOMM_SABM_SENT,
    BT_RFCOMM_OPEN,
} bt_rfcomm_state_t;

/* ================= Per-link connection ================= */
typedef struct {
    bool     in_use;
    uint16_t handle;         /* controller ACL handle, 12 bits, never 0 in use */
    uint8_t  bdaddr[6];
    int32_t  dev_index;      /* index into bluetooth_device_t::devices */
    bt_state_t state;
    uint32_t requested_profile;
    uint32_t active_profiles;  /* bit set only once the channels are OPEN */
    bool     incoming;

    bt_l2cap_chan_t chans[BT_MAX_CHANNELS];
    bt_reasm_t reasm;

    bt_rfcomm_state_t rfcomm_state;
    uint8_t  rfcomm_server_channel;   /* 1..30 */
    uint8_t  rfcomm_dlci;

    bt_a2dp_state_t a2dp_state;
    uint8_t  a2dp_label;              /* AVDTP transaction label counter */
    uint8_t  a2dp_seid;               /* remote stream endpoint id */
    uint16_t rtp_seq;
    uint32_t rtp_ts;
    uint32_t rtp_ssrc;
} bt_conn_t;

/* ================= Statistics =================
 * Every counter here increments only after the counted thing actually
 * happened, and each one names WHICH thing:
 *   hci_cmds_sent / acl_tx_pkts / bytes_tx  — the bound transport ACCEPTED it.
 *   l2cap_tx_frames                          — a PDU was framed and QUEUED;
 *       whether its fragments reached the transport is bytes_tx's business.
 *   everything else                          — a received packet was parsed
 *       and acted on, or was rejected. */
typedef struct {
    uint32_t hci_cmds_sent;
    uint32_t hci_cmds_failed;    /* transport refused, or none was bound */
    uint32_t hci_events_rx;
    uint32_t hci_events_bad;
    uint32_t acl_tx_pkts;
    uint32_t acl_rx_pkts;
    uint32_t acl_rx_bad;
    uint32_t l2cap_tx_frames;
    uint32_t l2cap_rx_frames;
    uint32_t reasm_drops;
    uint32_t inquiry_results;
    uint32_t name_updates;
    uint32_t pin_replies;
    uint32_t link_keys;
    uint32_t conn_refused;       /* L2CAP Connection Response with result != 0 */
    uint32_t rx_ring_drops;
    uint32_t tx_queue_full;
    uint32_t rfcomm_fcs_errors;
    uint64_t bytes_tx;           /* ACL payload bytes the transport accepted */
    uint64_t bytes_rx;           /* L2CAP payload bytes delivered to the ring */
} bt_stats_t;

/* ================= Radio backend =================
 * The ONLY route to the air. A board port implements these two and calls
 * bt_bind_ops(). Everything above this line is portable logic with no MMIO.
 *
 *   hci_send: hand one complete HCI packet of type `pkt_type` to the
 *             controller. Return >= 0 on acceptance, < 0 on failure. The
 *             transport adds its own framing (H4 indicator, USB endpoint...).
 *   hci_recv: fetch at most one packet. Return the byte count written, 0 when
 *             nothing is pending, < 0 on failure. *pkt_type receives the type.
 */
typedef struct bt_ops {
    int (*hci_send)(void *ctx, uint8_t pkt_type, const uint8_t *pkt, uint32_t len);
    int (*hci_recv)(void *ctx, uint8_t *pkt_type, uint8_t *buf, uint32_t cap);
    void *ctx;
} bt_ops_t;

/* ===== Bluetooth device (host state) ===== */
typedef struct {
    uint32_t device_id;
    char name[128];
    bt_version_t version;      /* only meaningful once version_valid is set */
    bool version_valid;        /* set by HCI Read Local Version Information */

    /* Local device info */
    uint8_t local_addr[6];      /* all zero until Read BD_ADDR completes */
    bool local_addr_valid;
    char local_name[248];
    bool discoverable;          /* set only when the controller confirms */
    bool connectable;
    uint32_t supported_profiles;

    /* Registers / controller-reported buffer geometry */
    uint32_t reg_command;
    uint32_t reg_status;
    uint32_t reg_acl_mtu;       /* HC_ACL_Data_Packet_Length, 0 until known */
    uint32_t reg_sco_mtu;
    uint32_t acl_total_pkts;    /* HC_Total_Num_ACL_Data_Packets */
    uint32_t acl_credits;       /* free controller buffers right now */
    uint32_t cmd_credits;       /* Num_HCI_Command_Packets the controller allows */

    /* DMA-style rings.
     * tx_dma holds queued outbound HCI packets as
     *     [len_lo][len_hi][pkt_type][packet bytes...]
     * rx_dma holds delivered L2CAP payloads as
     *     [len_lo][len_hi][bdaddr 6][payload...]
     * head is the producer index, tail the consumer index; empty when equal. */
    uint8_t tx_dma[4096];
    uint8_t rx_dma[4096];
    uint32_t tx_head, tx_tail;
    uint32_t rx_head, rx_tail;

    /* IRQ latches — set only by events that really arrived */
    bool irq_rx_ready;
    bool irq_tx_done;
    bool irq_inquiry_done;
    bool irq_connected;
    bool irq_disconnected;
    bool irq_pairing_req;

    /* Discovered devices */
    bt_device_t devices[BT_MAX_DEVICES];
    uint32_t num_devices;
    bool inquiring;

    /* Active connections. connections[i] mirrors conns[i].handle, or 0 when
     * slot i is free; num_connections counts the in-use slots. */
    uint32_t connections[BT_MAX_CONNECTIONS];
    uint32_t num_connections;
    bt_conn_t conns[BT_MAX_CONNECTIONS];

    /* GATT services — see LIMITATIONS: always empty. */
    bt_gatt_service_t gatt_services[BT_MAX_GATT_SERVICES];
    uint32_t num_gatt_services;

    /* Audio (A2DP source) */
    bool audio_streaming;       /* true only after AVDTP START is accepted */
    uint32_t audio_handle;
    uint16_t audio_codec;
    uint32_t audio_sample_rate;
    uint16_t audio_samples_per_frame;
    uint8_t  audio_frames_per_pkt;

    /* Radio backend */
    bt_ops_t ops;
    bool ops_bound;

    bt_stats_t stats;
    uint16_t next_scid;
    uint8_t  next_sig_ident;
    uint8_t  pending_scan_enable;
    bool     scan_enable_pending;

    /* M5 coordinates */
    m5_coords_t m5;
    double coverage_r;
    double coverage_l;
} bluetooth_device_t;

/* Coverage floor: bt_verify_coverage() is a state-consistency audit, not the
 * EDP r*l >= 1.8 hyperbola (both factors here are fractions in [0,1], so that
 * floor would be unreachable by construction — the exact tautology this tree
 * was audited for). One inconsistent record out of 32 already fails. */
#define BT_COVERAGE_FLOOR 0.999

/* ===================== Pure codec layer =====================
 * No device state, no hardware, fully deterministic. Build functions return
 * the number of bytes written or a negative BT_E*. Parse functions return
 * BT_OK or a negative BT_E* and never read past `len`. */
uint16_t bt_hci_opcode(uint8_t ogf, uint16_t ocf);
uint8_t  bt_hci_opcode_ogf(uint16_t opcode);
uint16_t bt_hci_opcode_ocf(uint16_t opcode);

int bt_hci_build_cmd(uint8_t *buf, uint32_t cap, uint16_t opcode,
                     const uint8_t *params, uint32_t plen);
int bt_hci_parse_cmd(const uint8_t *buf, uint32_t len, bt_hci_cmd_t *out);
int bt_hci_build_event(uint8_t *buf, uint32_t cap, uint8_t code,
                       const uint8_t *params, uint32_t plen);
int bt_hci_parse_event(const uint8_t *buf, uint32_t len, bt_hci_event_t *out);
int bt_hci_build_acl(uint8_t *buf, uint32_t cap, uint16_t handle,
                     uint8_t pb, uint8_t bc, const uint8_t *data, uint32_t dlen);
int bt_hci_parse_acl(const uint8_t *buf, uint32_t len, bt_hci_acl_t *out);

/* H4 (UART) transport framing, for ports that need it. */
int bt_h4_wrap(uint8_t *buf, uint32_t cap, uint8_t pkt_type,
               const uint8_t *pkt, uint32_t len);
int bt_h4_unwrap(const uint8_t *buf, uint32_t len, uint8_t *pkt_type,
                 const uint8_t **pkt, uint32_t *pkt_len);

int bt_l2cap_build(uint8_t *buf, uint32_t cap, uint16_t cid,
                   const uint8_t *payload, uint32_t n);
int bt_l2cap_parse(const uint8_t *buf, uint32_t len, bt_l2cap_frame_t *out);
int bt_l2cap_build_sig(uint8_t *buf, uint32_t cap, uint8_t code, uint8_t ident,
                       const uint8_t *data, uint32_t n);
int bt_l2cap_parse_sig(const uint8_t *buf, uint32_t len, bt_l2cap_sig_t *out);

/* Fragmentation: how many ACL packets an L2CAP PDU needs, and fragment `index`
 * of it. bt_acl_fragment() returns 0 once `index` is past the last fragment. */
uint32_t bt_acl_frag_count(uint32_t pdu_len, uint16_t acl_mtu);
int bt_acl_fragment(uint8_t *out, uint32_t cap, uint16_t handle, uint16_t acl_mtu,
                    const uint8_t *pdu, uint32_t pdu_len, uint32_t index);

/* Reassembly. bt_reasm_push() takes one complete HCI ACL packet and returns
 * the finished PDU length (> 0), 0 when more fragments are needed, or a
 * negative BT_E* when the fragment is unusable. */
void bt_reasm_init(bt_reasm_t *r);
int  bt_reasm_push(bt_reasm_t *r, const uint8_t *acl_pkt, uint32_t len);
const uint8_t *bt_reasm_pdu(const bt_reasm_t *r, uint32_t *len);

/* RFCOMM. bt_rfcomm_fcs() is the GSM 07.10 CRC-8 (reflected poly 0xE0,
 * init 0xFF, result subtracted from 0xFF) over the first `n` header bytes:
 * 2 for UIH, 3 for every other frame type. */
uint8_t bt_rfcomm_fcs(const uint8_t *hdr, uint32_t n);
int bt_rfcomm_build(uint8_t *buf, uint32_t cap, uint8_t dlci, uint8_t cr,
                    uint8_t ctrl, bool pf, const uint8_t *data, uint32_t n);
int bt_rfcomm_parse(const uint8_t *buf, uint32_t len, bt_rfcomm_frame_t *out);

/* AVDTP single-packet signalling messages. */
int bt_avdtp_build(uint8_t *buf, uint32_t cap, uint8_t label, uint8_t msg_type,
                   uint8_t signal_id, const uint8_t *params, uint32_t n);
int bt_avdtp_parse(const uint8_t *buf, uint32_t len, bt_avdtp_msg_t *out);

/* HIDP transaction framing. */
int bt_hidp_build(uint8_t *buf, uint32_t cap, uint8_t trans_type,
                  uint8_t param, const uint8_t *data, uint32_t n);
int bt_hidp_parse(const uint8_t *buf, uint32_t len, uint8_t *trans_type,
                  uint8_t *param, const uint8_t **payload, uint32_t *plen);

/* A2DP media framing: 12-byte RTP header (RFC 3550) + 1-byte SBC header. */
int bt_a2dp_media_header(uint8_t *buf, uint32_t cap, uint16_t seq, uint32_t ts,
                         uint32_t ssrc, uint8_t frame_count);

/* Class of Device decoding (Assigned Numbers, Baseband).
 * PARTIAL BY DESIGN. Only the majors/minors that have a bt_class_t bucket are
 * decoded: computer, phone, headset (wearable headset / hands-free /
 * headphones), speaker (loudspeaker), keyboard, mouse, gamepad (joystick /
 * gamepad), wearable, health. Everything else — including audio MICROPHONE and
 * PORTABLE AUDIO, peripheral REMOTE CONTROL and every unassigned major —
 * returns BT_CLASS_UNKNOWN. It is never rounded to the nearest enum. */
bt_class_t bt_class_from_cod(uint32_t cod);

/* Connection state machine: which transitions the spec allows. */
bool bt_state_transition_ok(bt_state_t from, bt_state_t to);

/* ===================== Device / stack API =====================
 * RETURN SEMANTICS FOR ANYTHING THAT TALKS TO A PEER: BT_OK means the request
 * was built and handed to the transmit path — NOT that the peer agreed. State
 * advances only when the peer's response bytes come back through
 * bt_hci_ingest(). bt_connect() returning BT_OK does not mean "connected";
 * poll bt_find_conn_addr()->state, or the device record, for that. */
void bt_init(bluetooth_device_t *dev, const char *name);

/* Bind the radio transport. Passing NULL unbinds and every air-facing call
 * goes back to BT_ENODEV. */
int  bt_bind_ops(bluetooth_device_t *dev, const bt_ops_t *ops);
bool bt_has_radio(const bluetooth_device_t *dev);

/* Controller bring-up: Reset, Read BD_ADDR, Read Buffer Size. Until the last
 * one completes, reg_acl_mtu is 0 and no ACL data can be sent. */
int bt_reset(bluetooth_device_t *dev);

int bt_set_discoverable(bluetooth_device_t *dev, bool on);
/* Sets local_name in host memory ALWAYS, and returns BT_ENODEV when the
 * controller could not be told. A BT_ENODEV return therefore means "the name
 * is set locally but the radio does not know it". */
int bt_set_name(bluetooth_device_t *dev, const char *name);
int bt_inquiry(bluetooth_device_t *dev, uint32_t duration);   /* seconds */
int bt_pair(bluetooth_device_t *dev, const uint8_t *bdaddr, const char *pin);
int bt_connect(bluetooth_device_t *dev, const uint8_t *bdaddr, bt_profile_t profile);
int bt_disconnect(bluetooth_device_t *dev, const uint8_t *bdaddr);
/* SPP path: wraps `data` in an RFCOMM UIH frame on the open SPP channel.
 * Returns the payload byte count ACCEPTED INTO THE TRANSMIT QUEUE. Bytes only
 * leave when the transport takes them and controller credits allow — read
 * bt_get_stats()->bytes_tx for what actually went out. */
int bt_send_data(bluetooth_device_t *dev, const uint8_t *bdaddr, const void *data, uint32_t len);
/* Pops one delivered L2CAP payload. BT_EAGAIN when the ring is empty. */
int bt_recv_data(bluetooth_device_t *dev, uint8_t *bdaddr, void *data, uint32_t max_len);

/* Push queued HCI packets to the transport, credits permitting. Returns the
 * number of packets the transport accepted (>= 0), BT_ENODEV with no backend
 * bound, or BT_EPROTO if the transmit ring was found corrupt — in which case
 * the ring is dropped rather than drained into the controller as garbage. */
int bt_tx_flush(bluetooth_device_t *dev);

/* Feed one received HCI packet (type + payload, transport framing removed).
 * BT_OK means the packet was well-formed and consumed — events this stack has
 * no behaviour for are consumed silently rather than reported as errors. A
 * negative return means the bytes were malformed or unroutable. */
int bt_hci_ingest(bluetooth_device_t *dev, uint8_t pkt_type,
                  const uint8_t *pkt, uint32_t len);

/* Audio (A2DP source).
 * SINGLE-SINK API: bt_audio_start()/stop()/send() take no address. start()
 * acts on the FIRST connection slot that has an AVDTP signalling channel,
 * stop() on the first slot that is actually streaming, and send() on the link
 * named by dev->audio_handle. With two A2DP peers up at once, which one you
 * get is slot order, not your choice. Only one stream is tracked
 * (dev->audio_streaming / audio_handle are per-device, not per-link). */
int bt_audio_connect(bluetooth_device_t *dev, const uint8_t *bdaddr);
int bt_audio_start(bluetooth_device_t *dev, uint16_t codec, uint32_t sample_rate);
int bt_audio_stop(bluetooth_device_t *dev);
/* Sends ONE RTP media packet holding `data` as already-encoded frames. There
 * is no encoder here. The frame geometry below sets the SBC frame-count byte
 * and how far the RTP timestamp advances. */
int bt_audio_send(bluetooth_device_t *dev, const void *data, uint32_t len);
/* Defaults are the common SBC configuration (8 subbands x 16 blocks = 128
 * samples per frame, one frame per packet). Set this to match your encoder. */
int bt_audio_set_frame_geometry(bluetooth_device_t *dev,
                                uint16_t samples_per_frame, uint8_t frames_per_pkt);

/* HID: opens the control (0x0011) and interrupt (0x0013) channels. */
int bt_hid_connect(bluetooth_device_t *dev, const uint8_t *bdaddr);
/* Sends an OUTPUT report host->device (HIDP DATA|Output, first byte 0xA2),
 * which is the only direction a host originates. Input reports arrive the
 * other way and are decoded by bt_hidp_parse(). */
int bt_hid_send_report(bluetooth_device_t *dev, const uint8_t *bdaddr, const void *report, uint32_t len);

/* Device management */
bt_device_t *bt_find_device(bluetooth_device_t *dev, const uint8_t *bdaddr);
uint32_t bt_get_device_count(bluetooth_device_t *dev);
bt_device_t *bt_get_device(bluetooth_device_t *dev, uint32_t index);
bt_conn_t *bt_find_conn_handle(bluetooth_device_t *dev, uint16_t handle);
bt_conn_t *bt_find_conn_addr(bluetooth_device_t *dev, const uint8_t *bdaddr);
const bt_stats_t *bt_get_stats(const bluetooth_device_t *dev);

/* IRQ: drains the transport and ingests whatever really arrived. With no
 * backend bound there is no source, so it does nothing. Returns the number of
 * packets ingested. */
void bt_handle_irq(bluetooth_device_t *dev);
int  bt_poll(bluetooth_device_t *dev, uint32_t max_packets);

/* Coverage: an internal-consistency audit of this stack's own state. It CAN
 * fail — see the invariant list in bluetooth.c. */
bool bt_verify_coverage(bluetooth_device_t *dev);

#endif /* BLUETOOTH_H */
