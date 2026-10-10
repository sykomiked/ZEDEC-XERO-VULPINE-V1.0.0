/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* call_cc.h — delay-gradient congestion control for real-time media.
 *
 * A simplified, all-integer Google Congestion Control (GCC, see
 * draft-ietf-rmcat-gcc-02) split across the two ends:
 *
 * RECEIVER (call_cc_rx_*). For every media packet the caller passes its
 * send time (sender clock, microseconds; e.g. from an abs-send-time or the
 * media timestamp), its arrival time (receiver clock, microseconds), its
 * sequence number and size. Packets sent within 5 ms of each other form a
 * group. Between consecutive groups the one-way delay variation is
 *     d = (arrival_i - arrival_{i-1}) - (send_i - send_{i-1})
 * and its running sum is the queueing delay up to an unknown constant. That
 * sum is smoothed (EWMA 1/8) and kept in a window of 16 groups; the delay
 * trend is (mean of the newer half - mean of the older half) divided by the
 * time between the halves' centres, in microseconds of queue growth per
 * second. Every report interval the receiver emits a FEEDBACK report:
 * receive rate, loss fraction (Q8, as in RTCP), delay trend, the highest
 * sequence seen and an echo of the last sender timestamp for RTT.
 *
 * SENDER (call_cc_tx_*). Feeds reports to an overuse detector with an
 * adaptive threshold (6..60 ms/s), then to the GCC rate controller:
 *   overuse  -> DECREASE to 85% of the measured receive rate, then HOLD;
 *   underuse -> HOLD (let the queue drain);
 *   normal   -> INCREASE: multiplicative 8%/s when far from the last known
 *               link capacity, additive ~half a packet per response time
 *               when near it (avg of rates at past decreases +/- 3 dev).
 * A loss-based controller runs alongside: loss > 10% multiplies the rate by
 * (1 - loss/2) at most once per 300 ms, loss < 2% raises it 5% per 100 ms
 * (the draft applies 5% per report) but never above the delay-based rate.
 * The target bitrate for the encoders is min(delay-based, loss-based),
 * clamped to [min_bps, max_bps]; the delay-based rate is also capped at
 * 1.5 x receive rate + 10 kbit/s. Random loss above 10% therefore drives
 * the target to min_bps, as in GCC.
 *
 * RULES. Freestanding C11: no libc, no malloc, no floating point, no 64-bit
 * division, no __int128. Rates are uint32 bit/s (up to 4 Gbit/s), times are
 * 32-bit wrapping micro- or milliseconds as documented per function.
 *
 * HONEST LIMITS. This is a simplification of GCC, not a byte-for-byte port:
 * no Kalman filter or least-squares trendline, no probing, no pacer, no
 * transport-wide sequence numbers (the caller picks what seq it feeds), and
 * the thresholds were tuned against the deterministic simulator in
 * test_call.c, not against real Internet paths. Delay-based control yields
 * to loss-based TCP flows sharing a bottleneck. There are no codecs here:
 * the target bitrate is advice for encoders that live in the host OS
 * (AVFoundation on macOS) or in bundled libopus/libvpx later.
 */
#ifndef CALL_CC_H
#define CALL_CC_H

#include <stdbool.h>
#include <stdint.h>

#define CALL_CC_WINDOW   16
#define CALL_CC_FB_LEN   32
#define CALL_CC_FB_MAGIC 0xCCFB

typedef enum { CALL_BW_NORMAL = 0, CALL_BW_OVERUSE = 1, CALL_BW_UNDERUSE = 2 } call_bw_state_t;

typedef struct call_cc_feedback {
    uint32_t ssrc;          /* reporter */
    uint16_t report_seq;    /* increments per report */
    uint16_t highest_seq;   /* highest media seq seen */
    uint32_t interval_ms;   /* time covered by this report */
    uint32_t recv_bps;      /* bits received / interval */
    int32_t trend_us_per_s; /* queueing-delay growth, microseconds per second */
    uint32_t echo_ts_us;    /* last sender timestamp seen (for RTT) */
    uint32_t echo_hold_us;  /* time it was held at the receiver */
    uint8_t loss_q8;        /* lost fraction * 256 */
    uint8_t groups;         /* delay samples behind the trend */
} call_cc_feedback_t;

/* Wire: 32 octets, big-endian. Returns length or a CALL_ERR_*. */
int call_cc_fb_write(const call_cc_feedback_t *fb, uint8_t *out, uint32_t cap);
int call_cc_fb_parse(const uint8_t *in, uint32_t len, call_cc_feedback_t *fb);

typedef struct call_cc_rx {
    uint32_t ssrc;
    /* current group */
    uint32_t g_first_send, g_last_send, g_last_arrival;
    uint8_t g_open;
    /* previous group */
    uint32_t p_send, p_arrival;
    uint8_t p_valid;
    int32_t acc_us;    /* accumulated delay variation */
    int32_t smooth_us; /* EWMA of acc_us */
    int32_t win_delay[CALL_CC_WINDOW];
    uint32_t win_time_ms[CALL_CC_WINDOW];
    uint32_t win_n, win_head;
    /* loss / rate accounting */
    uint16_t base_seq, max_seq;
    uint8_t seq_valid;
    uint32_t cycles; /* seq wraps */
    uint32_t expected_prior, received_prior;
    uint32_t received_total;
    uint32_t bytes_interval;
    uint32_t interval_start_us;
    uint32_t last_send_us, last_send_arrival_us;
    uint16_t report_seq;
    uint32_t report_every_ms;
} call_cc_rx_t;

void call_cc_rx_init(call_cc_rx_t *rx, uint32_t ssrc, uint32_t report_every_ms, uint32_t now_us);
void call_cc_rx_on_packet(call_cc_rx_t *rx, uint16_t seq, uint32_t send_us, uint32_t arrival_us,
                          uint32_t bytes);
/* Returns true and fills *fb when a report is due. */
bool call_cc_rx_report(call_cc_rx_t *rx, uint32_t now_us, call_cc_feedback_t *fb);

typedef enum { CALL_RC_HOLD = 0, CALL_RC_INCREASE = 1, CALL_RC_DECREASE = 2 } call_rc_state_t;

typedef struct call_cc_tx {
    uint32_t min_bps, max_bps;
    uint32_t delay_bps; /* delay-based estimate */
    uint32_t loss_bps;  /* loss-based estimate */
    uint32_t target_bps;
    uint32_t avg_max_kbps; /* receive rate at past decreases, 0 = unknown */
    uint32_t dev_max_kbps;
    uint32_t thr_us_per_s; /* adaptive overuse threshold */
    uint32_t rtt_ms;
    uint32_t last_fb_ms;
    uint32_t last_loss_dec_ms;
    uint32_t last_recv_bps;
    call_rc_state_t rc;
    call_bw_state_t bw;
    uint8_t over_count;
    uint8_t have_fb;
    /* statistics */
    uint32_t n_feedback, n_overuse, n_underuse, n_decrease, n_loss_dec;
} call_cc_tx_t;

void call_cc_tx_init(call_cc_tx_t *tx, uint32_t start_bps, uint32_t min_bps, uint32_t max_bps);
/* Process a feedback report received at now_ms; now_us is the sender clock
 * used for echo_ts_us (RTT = now_us - echo_ts_us - echo_hold_us).
 * Returns the new target bitrate. */
uint32_t call_cc_tx_on_feedback(call_cc_tx_t *tx, const call_cc_feedback_t *fb, uint32_t now_ms,
                                uint32_t now_us);
static inline uint32_t call_cc_target(const call_cc_tx_t *tx)
{
    return tx->target_bps;
}

#endif /* CALL_CC_H */
