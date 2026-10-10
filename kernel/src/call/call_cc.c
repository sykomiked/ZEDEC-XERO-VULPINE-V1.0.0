/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* call_cc.c — simplified integer GCC: delay trend at the receiver, rate
 * control at the sender. */
#include "call_cc.h"
#include "call_common.h"
#include "call_rtp.h"

#define GROUP_SPAN_US 5000
#define ACC_LIMIT_US  (1 << 27)
#define D_LIMIT_US    1000000
#define THR_MIN       6000
#define THR_MAX       60000
#define THR_START     10000

static int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* bits * 1000 / ms without 64-bit division */
static uint32_t rate_bps(uint32_t bits, uint32_t ms)
{
    if (ms == 0) return 0;
    uint32_t q = bits / ms, r = bits % ms;
    if (q > 4294967u) return 0xffffffffu;
    return q * 1000u + (r * 1000u) / ms;
}

/* ---- feedback wire format ---- */

int call_cc_fb_write(const call_cc_feedback_t *fb, uint8_t *out, uint32_t cap)
{
    if (!fb || !out) return CALL_ERR_ARG;
    if (cap < CALL_CC_FB_LEN) return CALL_ERR_SPACE;
    call_put16(out + 0, CALL_CC_FB_MAGIC);
    call_put16(out + 2, fb->report_seq);
    call_put32(out + 4, fb->ssrc);
    call_put16(out + 8, fb->highest_seq);
    out[10] = fb->loss_q8;
    out[11] = fb->groups;
    call_put32(out + 12, fb->interval_ms);
    call_put32(out + 16, fb->recv_bps);
    call_put32(out + 20, (uint32_t) fb->trend_us_per_s);
    call_put32(out + 24, fb->echo_ts_us);
    call_put32(out + 28, fb->echo_hold_us);
    return CALL_CC_FB_LEN;
}

int call_cc_fb_parse(const uint8_t *in, uint32_t len, call_cc_feedback_t *fb)
{
    if (!in || !fb) return CALL_ERR_ARG;
    if (len < CALL_CC_FB_LEN) return CALL_ERR_SHORT;
    if (len != CALL_CC_FB_LEN) return CALL_ERR_FORMAT;
    if (call_get16(in) != CALL_CC_FB_MAGIC) return CALL_ERR_FORMAT;
    fb->report_seq = call_get16(in + 2);
    fb->ssrc = call_get32(in + 4);
    fb->highest_seq = call_get16(in + 8);
    fb->loss_q8 = in[10];
    fb->groups = in[11];
    fb->interval_ms = call_get32(in + 12);
    fb->recv_bps = call_get32(in + 16);
    fb->trend_us_per_s = (int32_t) call_get32(in + 20);
    fb->echo_ts_us = call_get32(in + 24);
    fb->echo_hold_us = call_get32(in + 28);
    if (fb->interval_ms == 0 || fb->interval_ms > 60000) return CALL_ERR_FORMAT;
    if (fb->groups > CALL_CC_WINDOW) return CALL_ERR_FORMAT;
    if (fb->trend_us_per_s < -100000000 || fb->trend_us_per_s > 100000000) return CALL_ERR_FORMAT;
    return CALL_CC_FB_LEN;
}

/* ---- receiver ---- */

void call_cc_rx_init(call_cc_rx_t *rx, uint32_t ssrc, uint32_t report_every_ms, uint32_t now_us)
{
    call_fill(rx, 0, sizeof(*rx));
    rx->ssrc = ssrc;
    rx->report_every_ms = report_every_ms ? report_every_ms : 100;
    rx->interval_start_us = now_us;
}

static void push_window(call_cc_rx_t *rx, int32_t delay_us, uint32_t time_ms)
{
    rx->win_delay[rx->win_head] = delay_us;
    rx->win_time_ms[rx->win_head] = time_ms;
    rx->win_head = (rx->win_head + 1) % CALL_CC_WINDOW;
    if (rx->win_n < CALL_CC_WINDOW) rx->win_n++;
}

static void close_group(call_cc_rx_t *rx)
{
    if (rx->p_valid) {
        int32_t da = (int32_t) (rx->g_last_arrival - rx->p_arrival);
        int32_t ds = (int32_t) (rx->g_last_send - rx->p_send);
        int32_t d = clamp_i32(da - ds, -D_LIMIT_US, D_LIMIT_US);
        rx->acc_us = clamp_i32(rx->acc_us + d, -ACC_LIMIT_US, ACC_LIMIT_US);
        rx->smooth_us += (rx->acc_us - rx->smooth_us) / 8;
        push_window(rx, rx->smooth_us, rx->g_last_arrival / 1000u);
    }
    rx->p_send = rx->g_last_send;
    rx->p_arrival = rx->g_last_arrival;
    rx->p_valid = 1;
}

void call_cc_rx_on_packet(call_cc_rx_t *rx, uint16_t seq, uint32_t send_us, uint32_t arrival_us,
                          uint32_t bytes)
{
    /* loss accounting, RFC 3550 appendix A.3 style */
    if (!rx->seq_valid) {
        rx->seq_valid = 1;
        rx->base_seq = seq;
        rx->max_seq = seq;
    } else {
        int32_t d = call_seq_diff(rx->max_seq, seq);
        if (d > 0) {
            if (seq < rx->max_seq) rx->cycles++;
            rx->max_seq = seq;
        }
    }
    rx->received_total++;
    rx->bytes_interval += bytes;
    rx->last_send_us = send_us;
    rx->last_send_arrival_us = arrival_us;

    /* delay groups */
    if (!rx->g_open) {
        rx->g_open = 1;
        rx->g_first_send = rx->g_last_send = send_us;
        rx->g_last_arrival = arrival_us;
        return;
    }
    int32_t since_first = (int32_t) (send_us - rx->g_first_send);
    if (since_first < 0) return; /* reordered from an older group */
    if (since_first <= GROUP_SPAN_US) {
        if ((int32_t) (send_us - rx->g_last_send) > 0) rx->g_last_send = send_us;
        if ((int32_t) (arrival_us - rx->g_last_arrival) > 0) rx->g_last_arrival = arrival_us;
        return;
    }
    close_group(rx);
    rx->g_first_send = rx->g_last_send = send_us;
    rx->g_last_arrival = arrival_us;
}

static int32_t window_trend(const call_cc_rx_t *rx)
{
    uint32_t n = rx->win_n & ~1u;
    if (n < 4) return 0;
    uint32_t h = n / 2;
    int32_t sy0 = 0, sy1 = 0;
    uint32_t t_ref = rx->win_time_ms[(rx->win_head + CALL_CC_WINDOW - n) % CALL_CC_WINDOW];
    uint32_t sx0 = 0, sx1 = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = (rx->win_head + CALL_CC_WINDOW - n + i) % CALL_CC_WINDOW;
        int32_t y = rx->win_delay[idx] / 8; /* keep sums in range */
        uint32_t x = rx->win_time_ms[idx] - t_ref;
        if (x > 1000000u) x = 1000000u;
        if (i < h) {
            sy0 += y;
            sx0 += x;
        } else {
            sy1 += y;
            sx1 += x;
        }
    }
    int32_t dy = (sy1 - sy0) / (int32_t) h * 8;
    uint32_t dx = (sx1 - sx0) / h;
    if (dx == 0) dx = 1;
    dy = clamp_i32(dy, -2000000, 2000000);
    /* microseconds per millisecond -> microseconds per second */
    int32_t q = dy / (int32_t) dx;
    int32_t r = dy % (int32_t) dx;
    if (q >= 100000) return 100000000;
    if (q <= -100000) return -100000000;
    return q * 1000 + (r * 1000) / (int32_t) dx; /* |r| < dx <= 1e6 */
}

bool call_cc_rx_report(call_cc_rx_t *rx, uint32_t now_us, call_cc_feedback_t *fb)
{
    uint32_t el_us = now_us - rx->interval_start_us;
    if (el_us < rx->report_every_ms * 1000u) return false;
    uint32_t el_ms = el_us / 1000u;
    if (el_ms == 0) el_ms = 1;
    if (el_ms > 60000) el_ms = 60000;
    call_fill(fb, 0, sizeof(*fb));
    fb->ssrc = rx->ssrc;
    fb->report_seq = rx->report_seq++;
    fb->highest_seq = rx->max_seq;
    fb->interval_ms = el_ms;
    fb->recv_bps = rate_bps(rx->bytes_interval * 8u, el_ms);
    uint32_t ext_max = (rx->cycles << 16) + rx->max_seq;
    uint32_t expected = rx->seq_valid ? ext_max - rx->base_seq + 1u : 0;
    uint32_t exp_int = expected - rx->expected_prior;
    uint32_t rec_int = rx->received_total - rx->received_prior;
    rx->expected_prior = expected;
    rx->received_prior = rx->received_total;
    if (exp_int > 0 && exp_int > rec_int) {
        uint32_t lost = exp_int - rec_int;
        uint32_t q = lost >= exp_int ? 255u : (lost * 256u) / exp_int;
        if (lost > 0xffffffu) q = 255;
        fb->loss_q8 = (uint8_t) (q > 255 ? 255 : q);
    }
    fb->trend_us_per_s = window_trend(rx);
    fb->groups = (uint8_t) rx->win_n;
    fb->echo_ts_us = rx->last_send_us;
    fb->echo_hold_us = now_us - rx->last_send_arrival_us;
    rx->bytes_interval = 0;
    rx->interval_start_us = now_us;
    return true;
}

/* ---- sender ---- */

void call_cc_tx_init(call_cc_tx_t *tx, uint32_t start_bps, uint32_t min_bps, uint32_t max_bps)
{
    call_fill(tx, 0, sizeof(*tx));
    if (min_bps == 0) min_bps = 1;
    if (max_bps < min_bps) max_bps = min_bps;
    tx->min_bps = min_bps;
    tx->max_bps = max_bps;
    start_bps = clamp_u32(start_bps, min_bps, max_bps);
    tx->delay_bps = tx->loss_bps = tx->target_bps = start_bps;
    tx->thr_us_per_s = THR_START;
    tx->rtt_ms = 100;
    tx->rc = CALL_RC_INCREASE;
}

static void note_max(call_cc_tx_t *tx, uint32_t recv_bps)
{
    uint32_t x = recv_bps / 1000u;
    if (tx->avg_max_kbps == 0) {
        tx->avg_max_kbps = x;
        tx->dev_max_kbps = x / 20u + 1u;
        return;
    }
    uint32_t dv = x > tx->avg_max_kbps ? x - tx->avg_max_kbps : tx->avg_max_kbps - x;
    if (dv > 3u * tx->dev_max_kbps) { /* the link changed: start over from here */
        tx->avg_max_kbps = x;
        tx->dev_max_kbps = x / 20u + 1u;
        return;
    }
    tx->avg_max_kbps = (tx->avg_max_kbps * 19u + x) / 20u;
    tx->dev_max_kbps = (tx->dev_max_kbps * 19u + dv) / 20u;
    uint32_t floor_dev = tx->avg_max_kbps / 50u + 1u, ceil_dev = tx->avg_max_kbps / 10u + 1u;
    if (tx->dev_max_kbps < floor_dev) tx->dev_max_kbps = floor_dev;
    if (tx->dev_max_kbps > ceil_dev) tx->dev_max_kbps = ceil_dev;
}

static uint32_t mult_increase(uint32_t bps, uint32_t dt)
{
    uint32_t per = bps / 1000u; /* 8%/s: bps * 0.08 * dt / 1000 */
    if (per < 500000u) return per * 8u * dt / 100u;
    return (per * 8u / 100u) * dt;
}

uint32_t call_cc_tx_on_feedback(call_cc_tx_t *tx, const call_cc_feedback_t *fb, uint32_t now_ms,
                                uint32_t now_us)
{
    if (!tx || !fb) return tx ? tx->target_bps : 0;
    uint32_t dt = tx->have_fb ? now_ms - tx->last_fb_ms : fb->interval_ms;
    dt = clamp_u32(dt, 1, 1000);
    tx->last_fb_ms = now_ms;
    tx->have_fb = 1;
    tx->n_feedback++;
    uint32_t recv = fb->recv_bps;
    tx->last_recv_bps = recv;

    if (fb->echo_ts_us) {
        int32_t r = (int32_t) (now_us - fb->echo_ts_us - fb->echo_hold_us);
        if (r > 0 && r < 10000000) tx->rtt_ms = (tx->rtt_ms * 7u + (uint32_t) r / 1000u) / 8u;
    }

    /* overuse detector with adaptive threshold */
    int32_t m = fb->trend_us_per_s;
    uint32_t am = call_abs32(m);
    if (fb->groups >= 4) {
        if (m > (int32_t) tx->thr_us_per_s) {
            if (tx->over_count < 255) tx->over_count++;
            tx->bw = tx->over_count >= 2 ? CALL_BW_OVERUSE : CALL_BW_NORMAL;
        } else if (m < -(int32_t) tx->thr_us_per_s) {
            tx->over_count = 0;
            tx->bw = CALL_BW_UNDERUSE;
        } else {
            tx->over_count = 0;
            tx->bw = CALL_BW_NORMAL;
        }
        uint32_t thr = tx->thr_us_per_s;
        if (am < thr + 15000u) {
            if (am > thr)
                thr += (am - thr) * dt / 200u;
            else
                thr -= (thr - am) * dt / 5000u;
        }
        tx->thr_us_per_s = clamp_u32(thr, THR_MIN, THR_MAX);
    } else {
        tx->bw = CALL_BW_NORMAL;
    }
    if (tx->bw == CALL_BW_OVERUSE) tx->n_overuse++;
    if (tx->bw == CALL_BW_UNDERUSE) tx->n_underuse++;

    /* delay-based rate controller */
    switch (tx->bw) {
    case CALL_BW_OVERUSE:
        if (tx->rc != CALL_RC_DECREASE && recv > 0) {
            uint32_t nb = recv / 100u * 85u;
            if (nb < tx->delay_bps) tx->delay_bps = nb;
            note_max(tx, recv);
            tx->n_decrease++;
        }
        tx->rc = CALL_RC_DECREASE;
        break;
    case CALL_BW_UNDERUSE:
        tx->rc = CALL_RC_HOLD;
        break;
    default:
        if (tx->rc == CALL_RC_DECREASE)
            tx->rc = CALL_RC_HOLD;
        else if (tx->rc == CALL_RC_HOLD)
            tx->rc = CALL_RC_INCREASE;
        break;
    }
    if (tx->rc == CALL_RC_INCREASE) {
        uint32_t rk = recv / 1000u;
        if (tx->avg_max_kbps && rk > tx->avg_max_kbps + 3u * tx->dev_max_kbps)
            tx->avg_max_kbps = 0; /* the link got faster: forget it */
        bool near = false;
        if (tx->avg_max_kbps) {
            uint32_t dv = rk > tx->avg_max_kbps ? rk - tx->avg_max_kbps : tx->avg_max_kbps - rk;
            near = dv <= 3u * tx->dev_max_kbps;
        }
        uint32_t inc;
        if (near) {
            uint32_t resp = tx->rtt_ms + 100u;
            inc = 4800u * dt / resp;
            if (inc < 4u * dt) inc = 4u * dt; /* at least 4 kbit/s per second */
        } else {
            inc = mult_increase(tx->delay_bps, dt);
            if (inc < 1000u) inc = 1000u;
        }
        tx->delay_bps = tx->delay_bps + inc < tx->delay_bps ? 0xffffffffu : tx->delay_bps + inc;
    }
    if (recv > 0) {
        uint32_t cap = recv / 2u * 3u + 10000u;
        if (tx->delay_bps > cap) tx->delay_bps = cap;
    }
    tx->delay_bps = clamp_u32(tx->delay_bps, tx->min_bps, tx->max_bps);

    /* loss-based controller */
    uint32_t lf = fb->loss_q8;
    if (lf > 26) {
        if (!tx->n_loss_dec || (int32_t) (now_ms - tx->last_loss_dec_ms) >= 300) {
            tx->loss_bps = tx->loss_bps / 512u * (512u - lf);
            tx->last_loss_dec_ms = now_ms;
            tx->n_loss_dec++;
        }
    } else if (lf < 5) {
        uint32_t inc = tx->loss_bps / 2000u * dt; /* 5% per 100 ms */
        if (inc < 100u) inc = 100u;
        tx->loss_bps += inc;
    }
    if (tx->loss_bps > tx->delay_bps) tx->loss_bps = tx->delay_bps;
    tx->loss_bps = clamp_u32(tx->loss_bps, tx->min_bps, tx->max_bps);

    uint32_t t = tx->delay_bps < tx->loss_bps ? tx->delay_bps : tx->loss_bps;
    tx->target_bps = clamp_u32(t, tx->min_bps, tx->max_bps);
    return tx->target_bps;
}
