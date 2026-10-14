/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* br_lora.c — LoRa time on air, EU868 duty cycle, US915 dwell time and the
 * paced fragment link (br_lora.h). Integer only. */
#include "br_lora.h"

uint32_t br_lora_symbol_us(uint8_t sf, uint16_t bw_khz)
{
    if (sf < 6u || sf > 12u) return 0;
    if (bw_khz != 125u && bw_khz != 250u && bw_khz != 500u) return 0;
    return ((uint32_t) 1000u << sf) / bw_khz;
}

br_status_t br_lora_airtime_us(const br_lora_mod_t *m, uint32_t pl, uint32_t *us)
{
    if (!m || !us || pl > BR_LORA_PL_MAX) return BR_ERR_ARG;
    uint32_t ts = br_lora_symbol_us(m->sf, m->bw_khz);
    if (!ts || m->cr < 1u || m->cr > 4u || m->ldro > BR_LORA_LDRO_AUTO) return BR_ERR_ARG;
    if (m->sf == 6u && !m->implicit_header) return BR_ERR_ARG;
    if (m->preamble < 6u) return BR_ERR_ARG;
    uint32_t de = m->ldro == BR_LORA_LDRO_AUTO ? (ts >= 16000u ? 1u : 0u) : m->ldro;
    int32_t num = 8 * (int32_t) pl - 4 * (int32_t) m->sf + 28 + (m->crc_on ? 16 : 0) -
                  (m->implicit_header ? 20 : 0);
    int32_t den = 4 * ((int32_t) m->sf - 2 * (int32_t) de);
    uint32_t blocks = num > 0 ? (uint32_t) ((num + den - 1) / den) : 0u;
    uint32_t nsym = 8u + blocks * ((uint32_t) m->cr + 4u);
    /* quarter symbols: 4 * (preamble + 4.25) + 4 * nsym */
    uint64_t q = 4u * (uint64_t) m->preamble + 17u + 4u * (uint64_t) nsym;
    *us = (uint32_t) ((q * ts) >> 2);
    return BR_OK;
}

uint32_t br_lora_max_payload(const br_lora_mod_t *m, uint32_t max_us)
{
    uint32_t best = 0, t = 0;
    for (uint32_t pl = 1; pl <= BR_LORA_PL_MAX; pl++) {
        if (br_lora_airtime_us(m, pl, &t) != BR_OK) return 0;
        if (t <= max_us) best = pl;
    }
    return best;
}

/* ===== regions ===== */
static const struct {
    uint32_t lo, hi, den;
} g_eu[BR_LORA_EU_BANDS] = {
    {863000000u, 865000000u, 1000u}, {865000000u, 868000000u, 100u}, {868000000u, 868600000u, 100u},
    {868700000u, 869200000u, 1000u}, {869400000u, 869650000u, 10u},  {869700000u, 870000000u, 100u},
};

int br_lora_eu_band(uint32_t freq_hz)
{
    for (int i = 0; i < (int) BR_LORA_EU_BANDS; i++)
        if (freq_hz >= g_eu[i].lo && freq_hz < g_eu[i].hi) return i;
    return -1;
}

uint32_t br_lora_eu_den(int band)
{
    if (band < 0 || band >= (int) BR_LORA_EU_BANDS) return 0;
    return g_eu[band].den;
}

br_status_t br_lora_duty_init(br_lora_duty_t *d, uint8_t region)
{
    if (!d) return BR_ERR_ARG;
    br_mset(d, 0, sizeof *d);
    if (region != BR_LORA_REGION_EU868 && region != BR_LORA_REGION_US915) return BR_ERR_ARG;
    d->region = region;
    return BR_OK;
}

static uint32_t ceil_ms(uint32_t us)
{
    return (us + 999u) / 1000u;
}

br_status_t br_lora_duty_check(const br_lora_duty_t *d, uint32_t freq_hz, uint32_t air_us,
                               uint64_t now_ms)
{
    if (!d || air_us == 0) return BR_ERR_ARG;
    if (d->region == BR_LORA_REGION_EU868) {
        int b = br_lora_eu_band(freq_hz);
        if (b < 0) return BR_ERR_POLICY_BAND;
        return now_ms >= d->band_free_ms[b] ? BR_OK : BR_ERR_BUSY;
    }
    if (d->region == BR_LORA_REGION_US915) {
        if (freq_hz < 902000000u || freq_hz > 928000000u) return BR_ERR_POLICY_BAND;
        if (air_us > BR_LORA_US_DWELL_US) return BR_ERR_POLICY_DWELL;
        uint64_t used = air_us;
        for (uint32_t i = 0; i < BR_LORA_HIST; i++) {
            const br_lora_txrec_t *h = &d->hist[i];
            if (h->used && h->freq_hz == freq_hz && h->start_ms + BR_LORA_US_WINDOW_MS > now_ms)
                used += h->air_us;
        }
        if (used > BR_LORA_US_DWELL_US) return BR_ERR_BUSY;
        const br_lora_txrec_t *slot = &d->hist[d->hist_next];
        if (slot->used && slot->start_ms + BR_LORA_US_WINDOW_MS > now_ms) return BR_ERR_BUSY;
        return BR_OK;
    }
    return BR_ERR_DISABLED; /* no region: fail closed */
}

br_status_t br_lora_duty_commit(br_lora_duty_t *d, uint32_t freq_hz, uint32_t air_us,
                                uint64_t now_ms)
{
    br_status_t rc = br_lora_duty_check(d, freq_hz, air_us, now_ms);
    if (rc != BR_OK) return rc;
    if (d->region == BR_LORA_REGION_EU868) {
        int b = br_lora_eu_band(freq_hz);
        d->band_free_ms[b] = now_ms + (uint64_t) ceil_ms(air_us) * g_eu[b].den;
    } else {
        br_lora_txrec_t *h = &d->hist[d->hist_next];
        h->used = 1;
        h->start_ms = now_ms;
        h->freq_hz = freq_hz;
        h->air_us = air_us;
        d->hist_next = (d->hist_next + 1u) % BR_LORA_HIST;
    }
    d->air_total_us += air_us;
    return BR_OK;
}

/* ===== the link ===== */
br_status_t br_lora_link_init(br_lora_link_t *lk, uint8_t region, const br_lora_mod_t *mod,
                              const uint32_t *chans, uint32_t nchan, br_lora_radio_tx_fn tx,
                              void *ctx)
{
    if (!lk || !mod || !chans || nchan == 0 || nchan > BR_LORA_MAX_CHANS || !tx) return BR_ERR_ARG;
    br_mset(lk, 0, sizeof *lk);
    if (br_lora_duty_init(&lk->duty, region) != BR_OK) return BR_ERR_ARG;
    uint32_t t;
    if (br_lora_airtime_us(mod, 1, &t) != BR_OK) return BR_ERR_ARG;
    br_mcpy(&lk->mod, mod, sizeof *mod);
    for (uint32_t i = 0; i < nchan; i++) {
        br_status_t rc =
            region == BR_LORA_REGION_EU868
                ? (br_lora_eu_band(chans[i]) < 0 ? BR_ERR_POLICY_BAND : BR_OK)
                : ((chans[i] < 902000000u || chans[i] > 928000000u) ? BR_ERR_POLICY_BAND : BR_OK);
        if (rc != BR_OK) return rc;
        lk->chans[i] = chans[i];
    }
    lk->nchan = nchan;
    lk->mtu = region == BR_LORA_REGION_US915 ? br_lora_max_payload(mod, BR_LORA_US_DWELL_US)
                                             : BR_LORA_PL_MAX;
    if (lk->mtu < BR_FRAG_MIN_MTU) return BR_ERR_POLICY_DWELL;
    lk->radio_tx = tx;
    lk->radio_ctx = ctx;
    return BR_OK;
}

int br_lora_link_send(void *ctx, const uint8_t to[BR_ID_BYTES], const uint8_t *data, uint32_t len)
{
    br_lora_link_t *lk = (br_lora_link_t *) ctx;
    (void) to;
    if (!lk || !data || len == 0) return BR_ERR_ARG;
    if (len > BR_LORA_MAX_MSG) return BR_ERR_SIZE;
    if (lk->busy) return BR_ERR_BUSY;
    br_mcpy(lk->msg, data, len);
    if (br_frag_tx_init(&lk->ftx, lk->msg, len, lk->mtu, lk->ctr) != BR_OK) return BR_ERR_ARG;
    lk->ctr = (uint8_t) ((lk->ctr + 1u) & 0x7Fu);
    lk->busy = 1;
    lk->pend_len = 0;
    return BR_OK;
}

int br_lora_link_poll(br_lora_link_t *lk, uint64_t now_ms)
{
    if (!lk) return BR_ERR_ARG;
    lk->now_ms = now_ms;
    if (!lk->busy) return 0;
    if (lk->pend_len == 0) {
        int32_t n = br_frag_tx_next(&lk->ftx, lk->pend, lk->mtu);
        if (n <= 0) {
            lk->busy = 0;
            if (n == 0) lk->msgs_sent++;
            return n < 0 ? n : 0;
        }
        lk->pend_len = n;
    }
    uint32_t air;
    if (br_lora_airtime_us(&lk->mod, (uint32_t) lk->pend_len, &air) != BR_OK) return BR_ERR_ARG;
    for (uint32_t k = 0; k < lk->nchan; k++) {
        uint32_t c = (lk->next_chan + k) % lk->nchan;
        if (br_lora_duty_check(&lk->duty, lk->chans[c], air, now_ms) != BR_OK) continue;
        if (lk->radio_tx(lk->radio_ctx, lk->chans[c], lk->pend, (uint32_t) lk->pend_len) != 0) {
            lk->radio_errors++;
            lk->busy = 0;
            lk->pend_len = 0;
            return BR_ERR_SEND;
        }
        (void) br_lora_duty_commit(&lk->duty, lk->chans[c], air, now_ms);
        lk->next_chan = (c + 1u) % lk->nchan;
        lk->frags_sent++;
        lk->pend_len = 0;
        if (lk->ftx.off >= lk->ftx.len) {
            lk->busy = 0;
            lk->msgs_sent++;
        }
        return 1;
    }
    return 0;
}
