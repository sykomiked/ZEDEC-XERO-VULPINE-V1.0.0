/* jdr_piratenet.c — JDR PirateNet Harmonic Hum Carrier Protocol
 *
 * Hardware-as-code SDR transceiver covering all frequency bands.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "jdr_piratenet.h"

static const char *band_names[] = {
    "ULF", "ELF", "VLF", "LF", "MF", "HF",
    "VHF", "UHF", "SHF", "EHF", "THz",
    "Infrared", "Visible", "Ultraviolet", "X-ray", "Gamma",
    "Acoustic", "Infrasonic", "Ultrasonic",
    "Neutrino", "Quantum", "Harmonic-Hum"
};

static const char *mod_names[] = {
    "CW", "AM", "FM", "PM", "SSB", "DSB",
    "OFDM", "FSK", "PSK", "QAM", "APSK", "GMSK",
    "OFDM-QAM", "CDMA", "FHSS", "DSSS",
    "THz-Pulse", "OOK", "PPM",
    "Harmonic", "Quantum", "Neutrino"
};

static const char *exec_names[] = {
    "DC (Direct)", "AC (Alternating)", "PC (Photonic)"
};

static const uint64_t band_freqs_start[] = {
    3, 3, 3000, 30000, 300000, 3000000,
    30000000, 300000000, 3000000000ULL, 30000000000ULL, 300000000000ULL,
    300000000000000ULL, 430000000000000ULL, 750000000000000ULL,
    3000000000000000ULL, 30000000000000000ULL,
    20, 0, 20000,
    0, 0, 0
};

static const uint64_t band_freqs_end[] = {
    3000, 30, 30000, 300000, 3000000, 30000000,
    300000000, 3000000000ULL, 30000000000ULL, 300000000000ULL, 3000000000000ULL,
    430000000000000ULL, 750000000000000ULL, 3000000000000000ULL,
    30000000000000000ULL, 300000000000000000ULL,
    20000, 20, 100000000,
    0, 0, 0
};

const char *jdr_band_name(jdr_band_t b) {
    if (b < JDR_BAND_MAX) return band_names[b];
    return "Unknown";
}

const char *jdr_modulation_name(jdr_modulation_t m) {
    if (m < JDR_MOD_MAX) return mod_names[m];
    return "Unknown";
}

const char *jdr_exec_mode_name(jdr_exec_mode_t e) {
    if (e <= JDR_EXEC_PC) return exec_names[e];
    return "Unknown";
}

uint64_t jdr_band_freq_start(jdr_band_t b) {
    if (b < JDR_BAND_MAX) return band_freqs_start[b];
    return 0;
}

uint64_t jdr_band_freq_end(jdr_band_t b) {
    if (b < JDR_BAND_MAX) return band_freqs_end[b];
    return 0;
}

void jdr_network_init(jdr_network_t *net) {
    net->num_transceivers = 0;
    net->num_nodes = 0;
    net->num_allocations = 0;
    net->system_coverage = SR_ONE;
    net->total_packets = 0;
    net->total_bytes = 0;
}

uint32_t jdr_transceiver_create(jdr_network_t *net,
                                 uint64_t frequency,
                                 uint64_t bandwidth,
                                 jdr_band_t band,
                                 jdr_modulation_t mod,
                                 jdr_exec_mode_t exec,
                                 const char *callsign) {
    if (net->num_transceivers >= 32) return 0xFFFFFFFF;
    jdr_transceiver_t *tc = &net->transceivers[net->num_transceivers];
    
    tc->device_id = net->num_transceivers;
    tc->reg_frequency = frequency;
    tc->reg_bandwidth = bandwidth;
    tc->reg_power_dbm = 20; /* Default 20 dBm (100 mW) */
    tc->reg_sample_rate = 1000000; /* 1 MSPS default */
    tc->reg_band = band;
    tc->reg_mod = mod;
    tc->reg_exec = exec;
    
    /* Harmonic hum defaults */
    tc->reg_hum_freq = SR_FROM_FLOAT((double)frequency / 1e6);
    tc->reg_hum_amplitude = SR_ONE;
    tc->reg_hum_phase = SR_ZERO;
    tc->reg_harmonic_n = SR_ONE;
    tc->num_harmonics = 1;
    tc->harmonics[0] = SR_ONE;
    uint32_t i;
    for (i = 1; i < 16; i++) tc->harmonics[i] = SR_ZERO;
    
    /* M⁵ */
    tc->m5.omega = net->num_transceivers;
    tc->m5.r = SR_FROM_FLOAT((double)frequency / 1e9); /* GHz-scale */
    tc->m5.ell = SR_ONE;
    tc->m5.phi = SR_ZERO;
    tc->m5.chi = 0;
    tc->coverage_ratio = SR_ONE;
    
    /* DMA */
    tc->tx_head = 0; tc->tx_tail = 0;
    tc->rx_head = 0; tc->rx_tail = 0;
    for (i = 0; i < 256; i++) {
        tc->dma_tx_buffer[i] = 0;
        tc->dma_rx_buffer[i] = 0;
    }
    
    /* IRQs */
    tc->irq_carrier_lock = false;
    tc->irq_rx_data_ready = false;
    tc->irq_tx_complete = false;
    tc->irq_harmonic_resonance = false;
    tc->irq_coverage_breach = false;
    tc->irq_frequency_hop = false;
    
    tc->channel_id = 0;
    tc->node_id = net->num_transceivers + 1;
    
    int j;
    for (j = 0; j < 15 && callsign && callsign[j]; j++)
        tc->callsign[j] = callsign[j];
    tc->callsign[j] = 0;
    
    tc->encrypted = false;
    tc->encryption_key_id = 0;
    
    /* FHSS */
    tc->hop_index = 0;
    tc->hop_rate = 0;
    for (i = 0; i < 64; i++) tc->hop_table[i] = 0;
    
    tc->packets_tx = 0;
    tc->packets_rx = 0;
    tc->bytes_tx = 0;
    tc->bytes_rx = 0;
    tc->active = true;
    
    return net->num_transceivers++;
}

void jdr_hum_init(jdr_transceiver_t *tc,
                   surplus_real_t freq,
                   surplus_real_t amplitude) {
    tc->reg_hum_freq = freq;
    tc->reg_hum_amplitude = amplitude;
    tc->reg_hum_phase = SR_ZERO;
    tc->num_harmonics = 1;
    tc->harmonics[0] = amplitude;
}

void jdr_hum_add_harmonic(jdr_transceiver_t *tc, uint32_t order) {
    if (tc->num_harmonics >= 16) return;
    /* Harmonic amplitude: A_n = A / n (odd harmonics for square wave) */
    if (order % 2 == 1) {
        surplus_real_t amp = SR_DIV(tc->reg_hum_amplitude, SR_FROM_INT(order));
        tc->harmonics[tc->num_harmonics] = amp;
    } else {
        tc->harmonics[tc->num_harmonics] = SR_ZERO;
    }
    tc->num_harmonics++;
    tc->reg_harmonic_n = SR_FROM_INT(tc->num_harmonics);
}

void jdr_hum_compute(jdr_transceiver_t *tc) {
    /* Compute composite waveform from harmonic series */
    surplus_real_t total = SR_ZERO;
    uint32_t i;
    for (i = 0; i < tc->num_harmonics; i++) {
        total = SR_ADD(total, tc->harmonics[i]);
    }
    
    /* Check resonance: if total amplitude exceeds threshold */
    surplus_real_t threshold = SR_MUL(tc->reg_hum_amplitude, SR_FROM_FLOAT(1.5));
    if (SR_CMP(total, threshold) > 0) {
        tc->irq_harmonic_resonance = true;
    }
    
    /* Update M⁵ */
    tc->m5.phi = tc->reg_hum_phase;
}

bool jdr_hum_lock_carrier(jdr_transceiver_t *tc) {
    /* Carrier lock: phase synchronization */
    tc->reg_hum_phase = SR_ZERO; /* Reset to zero phase */
    tc->irq_carrier_lock = true;
    
    /* Verify coverage */
    return jdr_verify_coverage(tc);
}

int32_t jdr_transmit(jdr_transceiver_t *tc, const void *data, uint32_t len) {
    if (!tc->active || !data) return -1;
    
    /* Push data to TX DMA buffer (simplified — word at a time) */
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t i;
    for (i = 0; i < len && i < 256 * 4; i += 4) {
        uint32_t word = 0;
        int j;
        for (j = 0; j < 4 && i + j < len; j++) {
            word |= ((uint32_t)bytes[i + j]) << (j * 8);
        }
        tc->dma_tx_buffer[tc->tx_tail] = word;
        tc->tx_tail = (tc->tx_tail + 1) % 256;
    }
    
    tc->irq_tx_complete = true;
    tc->packets_tx++;
    tc->bytes_tx += len;
    
    return (int32_t)len;
}

int32_t jdr_receive(jdr_transceiver_t *tc, void *buf, uint32_t max_len) {
    if (!tc->active || !buf) return -1;
    if (tc->rx_head == tc->rx_tail) return 0; /* Empty */
    
    uint8_t *bytes = (uint8_t *)buf;
    uint32_t received = 0;
    
    while (tc->rx_head != tc->rx_tail && received < max_len) {
        uint32_t word = tc->dma_rx_buffer[tc->rx_head];
        int j;
        for (j = 0; j < 4 && received < max_len; j++) {
            bytes[received++] = (uint8_t)(word >> (j * 8));
        }
        tc->rx_head = (tc->rx_head + 1) % 256;
    }
    
    tc->irq_rx_data_ready = false;
    tc->packets_rx++;
    tc->bytes_rx += received;
    
    return (int32_t)received;
}

void jdr_fhss_init(jdr_transceiver_t *tc, uint32_t hop_rate) {
    tc->hop_rate = hop_rate;
    tc->hop_index = 0;
    
    /* Generate pseudo-random hop table across the band */
    uint64_t band_start = jdr_band_freq_start(tc->reg_band);
    uint64_t band_end = jdr_band_freq_end(tc->reg_band);
    uint64_t step = (band_end - band_start) / 64;
    
    uint32_t i;
    for (i = 0; i < 64; i++) {
        /* Simple LFSR-based pseudo-random hop sequence */
        uint32_t seed = tc->node_id * 7919 + i * 31;
        uint64_t offset = ((uint64_t)(seed % 64)) * step;
        tc->hop_table[i] = band_start + offset;
    }
}

uint64_t jdr_fhss_next_freq(jdr_transceiver_t *tc) {
    if (tc->hop_rate == 0) return tc->reg_frequency;
    
    uint64_t freq = tc->hop_table[tc->hop_index];
    tc->hop_index = (tc->hop_index + 1) % 64;
    tc->reg_frequency = freq;
    tc->irq_frequency_hop = true;
    
    return freq;
}

int32_t jdr_switch_band(jdr_transceiver_t *tc, jdr_band_t band, uint64_t freq) {
    /* Verify frequency is within band */
    uint64_t band_start = jdr_band_freq_start(band);
    uint64_t band_end = jdr_band_freq_end(band);
    
    if (band_start > 0 && band_end > 0) {
        if (freq < band_start || freq > band_end) return -1;
    }
    
    tc->reg_band = band;
    tc->reg_frequency = freq;
    
    /* Update M⁵ */
    tc->m5.r = SR_FROM_FLOAT((double)freq / 1e9);
    jdr_verify_coverage(tc);
    
    return 0;
}

int32_t jdr_switch_modulation(jdr_transceiver_t *tc, jdr_modulation_t mod) {
    tc->reg_mod = mod;
    
    /* Update execution mode based on modulation */
    switch (mod) {
        case JDR_MOD_CW:
        case JDR_MOD_AM:
        case JDR_MOD_SSB:
        case JDR_MOD_DSB:
        case JDR_MOD_OOK:
            tc->reg_exec = JDR_EXEC_DC;
            break;
        case JDR_MOD_FM:
        case JDR_MOD_PM:
        case JDR_MOD_FSK:
        case JDR_MOD_GMSK:
            tc->reg_exec = JDR_EXEC_AC;
            break;
        default:
            tc->reg_exec = JDR_EXEC_PC;
            break;
    }
    
    return 0;
}

int32_t jdr_switch_exec(jdr_transceiver_t *tc, jdr_exec_mode_t mode) {
    tc->reg_exec = mode;
    return 0;
}

bool jdr_verify_coverage(jdr_transceiver_t *tc) {
    surplus_real_t product = SR_MUL(tc->m5.r, tc->m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
    tc->coverage_ratio = SR_DIV(product, floor);
    tc->irq_coverage_breach = (SR_CMP(product, floor) < 0);
    return !tc->irq_coverage_breach;
}

int32_t jdr_allocate_frequency(jdr_network_t *net, jdr_band_t band,
                                uint64_t freq_start, uint64_t freq_end,
                                uint32_t node_id) {
    if (net->num_allocations >= 64) return -1;
    
    /* Check for conflicts */
    uint32_t i;
    for (i = 0; i < net->num_allocations; i++) {
        if (net->freq_alloc[i].band == band &&
            net->freq_alloc[i].allocated) {
            /* Check overlap */
            if (freq_start < net->freq_alloc[i].freq_end &&
                freq_end > net->freq_alloc[i].freq_start) {
                return -1; /* Conflict */
            }
        }
    }
    
    net->freq_alloc[net->num_allocations].band = band;
    net->freq_alloc[net->num_allocations].freq_start = freq_start;
    net->freq_alloc[net->num_allocations].freq_end = freq_end;
    net->freq_alloc[net->num_allocations].allocated = true;
    net->freq_alloc[net->num_allocations].node_id = node_id;
    net->num_allocations++;
    
    return 0;
}

int32_t jdr_register_node(jdr_network_t *net, uint32_t node_id) {
    if (net->num_nodes >= 128) return -1;
    
    /* Check for duplicate */
    uint32_t i;
    for (i = 0; i < net->num_nodes; i++) {
        if (net->node_addresses[i] == node_id) return -1;
    }
    
    net->node_addresses[net->num_nodes] = node_id;
    net->num_nodes++;
    return 0;
}
