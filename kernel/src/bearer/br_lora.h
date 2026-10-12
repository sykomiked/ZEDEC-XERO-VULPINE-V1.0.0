/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* br_lora.h — LoRa bearer: time on air, regional airtime rules, and a
 * link that sends one message as a paced train of fragments.
 *
 * WHAT IT IS FOR
 *   LoRa (ISM band, unlicensed) reaches kilometres at a few hundred to a
 *   few thousand bits per second. It is for settlement records, small
 *   signed records and emergency messages; never AI traffic (bearer.h B2).
 *
 * TIME ON AIR (Semtech SX1276/77/78/79 datasheet section 4.1.1.7, AN1200.13)
 *   Tsym      = 2^SF / BW
 *   Tpreamble = (Npreamble + 4.25) * Tsym
 *   Npayload  = 8 + max(ceil((8*PL - 4*SF + 28 + 16*CRC - 20*IH)
 *                            / (4*(SF - 2*DE))) * (CR + 4), 0)
 *   T         = Tpreamble + Npayload * Tsym
 *   computed exactly in integer microseconds: BW is 125, 250 or 500 kHz,
 *   so Tsym = 2^SF * 1000 / BW_kHz is a whole number of microseconds and
 *   the quarter symbol of the preamble is counted in quarter symbols.
 *   DE (low data rate optimisation) may be forced or left AUTO, which turns
 *   it on when Tsym >= 16 ms (SF11 and SF12 at 125 kHz, SF12 at 250 kHz).
 *
 * REGIONAL RULES (designed to follow; not certified)
 *   EU868  ETSI EN 300 220 sub-bands as listed in LoRaWAN Regional
 *          Parameters RP002 for EU863-870:
 *            863.0-865.0 MHz 0.1%   865.0-868.0 MHz 1%   868.0-868.6 MHz 1%
 *            868.7-869.2 MHz 0.1%   869.4-869.65 MHz 10% 869.7-870.0 MHz 1%
 *          After a transmission of T ms a sub-band stays closed for
 *          T * (1/dc - 1) ms (T rounded up to whole ms), i.e. it reopens
 *          T / dc ms after the transmission started. This is the LoRaWAN
 *          "TOff = TimeOnAir / DutyCycle - TimeOnAir" method; counted over
 *          a fixed clock hour it can exceed dc by at most the air time of
 *          the one packet that started just before the hour ended.
 *          Frequencies outside the table are refused (BR_ERR_POLICY_BAND).
 *   US915  902-928 MHz. 47 CFR 15.247(a)(1)(i)-style dwell time: one
 *          transmission longer than 400 ms is refused (BR_ERR_POLICY_DWELL);
 *          the occupancy of one frequency within any 20 s window may not
 *          pass 400 ms (BR_ERR_BUSY). The last BR_LORA_HIST transmissions
 *          are remembered; if the slot to be reused is still inside the
 *          window the transmission is refused rather than under-counted.
 *   No region configured: every transmission is refused (fails closed).
 *
 * THE LINK (br_lora_link_*)
 *   br_lora_link_send() (a br_link_ops_t send) queues one message and
 *   returns BR_ERR_BUSY while another is still going out. The host calls
 *   br_lora_link_poll(now) regularly; each call sends at most one fragment
 *   (br_frag.h) on the first configured channel whose rules allow it.
 *   In the US the fragment size is reduced until one fragment fits the
 *   400 ms dwell limit. The radio itself is a host callback.
 *
 * HONEST LIMITS
 *   - Not tested with a real radio (SX127x / SX126x driver not included).
 *   - EU868 accounting uses the channel centre frequency, not the occupied
 *     bandwidth; transmit power (ERP) limits and listen-before-talk are not
 *     modelled. US accounting does not model hopping over 50 channels.
 *   - Under a 1% duty cycle a 6 KB signed record takes minutes to send at
 *     SF7: that is the physics and the law, not a bug.
 */
#ifndef ZXV_BR_LORA_H
#define ZXV_BR_LORA_H

#include "br_frag.h"

#define BR_LORA_PL_MAX       255u
#define BR_LORA_LDRO_OFF     0u
#define BR_LORA_LDRO_ON      1u
#define BR_LORA_LDRO_AUTO    2u
#define BR_LORA_US_DWELL_US  400000u
#define BR_LORA_US_WINDOW_MS 20000u
#define BR_LORA_HIST         64u
#define BR_LORA_EU_BANDS     6u
#define BR_LORA_MAX_CHANS    8u

typedef enum {
    BR_LORA_REGION_NONE = 0,
    BR_LORA_REGION_EU868 = 1,
    BR_LORA_REGION_US915 = 2,
} br_lora_region_t;

typedef struct {
    uint8_t sf;        /* 6..12 (SF6 needs implicit header) */
    uint16_t bw_khz;   /* 125, 250, 500 */
    uint8_t cr;        /* 1..4 = coding rate 4/5..4/8 */
    uint16_t preamble; /* programmed preamble symbols (LoRaWAN: 8) */
    uint8_t implicit_header;
    uint8_t crc_on;
    uint8_t ldro; /* BR_LORA_LDRO_* */
} br_lora_mod_t;

uint32_t br_lora_symbol_us(uint8_t sf, uint16_t bw_khz); /* 0 if invalid */
/* Exact time on air for a PL-byte payload. */
br_status_t br_lora_airtime_us(const br_lora_mod_t *m, uint32_t pl, uint32_t *us);
/* Largest payload (<= 255) whose time on air is <= max_us; 0 if none. */
uint32_t br_lora_max_payload(const br_lora_mod_t *m, uint32_t max_us);

typedef struct {
    uint64_t start_ms;
    uint32_t freq_hz;
    uint32_t air_us;
    uint8_t used;
} br_lora_txrec_t;

typedef struct {
    uint8_t region;
    uint64_t band_free_ms[BR_LORA_EU_BANDS];
    br_lora_txrec_t hist[BR_LORA_HIST];
    uint32_t hist_next;
    uint64_t air_total_us;
} br_lora_duty_t;

/* EU sub-band index for a frequency, -1 outside the table. */
int br_lora_eu_band(uint32_t freq_hz);
/* 1/dc for an EU sub-band (100 = 1%), 0 for a bad index. */
uint32_t br_lora_eu_den(int band);

br_status_t br_lora_duty_init(br_lora_duty_t *d, uint8_t region);
/* May a transmission of air_us start at now_ms on freq_hz? */
br_status_t br_lora_duty_check(const br_lora_duty_t *d, uint32_t freq_hz, uint32_t air_us,
                               uint64_t now_ms);
/* Check, and if allowed record it. */
br_status_t br_lora_duty_commit(br_lora_duty_t *d, uint32_t freq_hz, uint32_t air_us,
                                uint64_t now_ms);

/* ===== the link ===== */
typedef int (*br_lora_radio_tx_fn)(void *ctx, uint32_t freq_hz, const uint8_t *p, uint32_t n);

typedef struct {
    br_lora_mod_t mod;
    br_lora_duty_t duty;
    uint32_t chans[BR_LORA_MAX_CHANS];
    uint32_t nchan, next_chan;
    br_lora_radio_tx_fn radio_tx;
    void *radio_ctx;
    uint32_t mtu; /* fragment size actually used */
    uint64_t now_ms;
    uint8_t busy, ctr;
    br_frag_tx_t ftx;
    uint8_t pend[BR_LORA_PL_MAX];
    int32_t pend_len; /* fragment waiting for airtime, 0 = none */
    uint8_t msg[BR_LORA_MAX_MSG];
    uint32_t frags_sent, msgs_sent, radio_errors;
} br_lora_link_t;

br_status_t br_lora_link_init(br_lora_link_t *lk, uint8_t region, const br_lora_mod_t *mod,
                              const uint32_t *chans, uint32_t nchan, br_lora_radio_tx_fn tx,
                              void *ctx);
/* br_link_ops_t.send: ctx is the br_lora_link_t. `to` is not used (LoRa is
 * a broadcast medium; devmesh frames carry their own addressing). */
int br_lora_link_send(void *ctx, const uint8_t to[BR_ID_BYTES], const uint8_t *data, uint32_t len);
/* 1 = a fragment went out, 0 = nothing to send or waiting for airtime,
 * <0 = the radio failed (the message is dropped). */
int br_lora_link_poll(br_lora_link_t *lk, uint64_t now_ms);

#endif /* ZXV_BR_LORA_H */
