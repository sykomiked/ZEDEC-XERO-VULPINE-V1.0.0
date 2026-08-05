/* radio.c — Cellular, Satellite, Radio, and Futuristic Protocol Implementation
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "radio.h"
#include "../../include/freestanding.h"

static void mem_copy(void *d, const void *s, uint32_t n) {
    uint8_t *dst = d; const uint8_t *src = s;
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}
static void mem_set(void *d, int c, uint32_t n) {
    uint8_t *dst = d; for (uint32_t i = 0; i < n; i++) dst[i] = (uint8_t)c;
}
static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* ===== CELLULAR ===== */
void cell_init(cell_modem_t *modem, cell_generation_t gen) {
    mem_set(modem, 0, sizeof(*modem));
    modem->gen = gen;
    modem->signal_dbm = -113; /* No signal */
}

int32_t cell_at_command(cell_modem_t *modem, const char *cmd, char *resp, uint32_t max_resp) {
    if (!modem->at_send || !modem->at_recv) return -1;
    modem->at_send(cmd);
    if (resp && max_resp > 0) {
        int32_t got = modem->at_recv(resp, max_resp);
        return got;
    }
    return 0;
}

int32_t cell_register(cell_modem_t *modem) {
    /* AT+COPS=0 would auto-register to network */
    modem->registered = true;
    return 0;
}

int32_t cell_send_sms(cell_modem_t *modem, const char *number, const char *text) {
    (void)number; (void)text;
    if (!modem->registered) return -1;
    /* AT+CMGS="<number>" then text then Ctrl-Z */
    return 0;
}

int32_t cell_recv_sms(cell_modem_t *modem, char *number, char *text, uint32_t max_len) {
    (void)modem;
    if (number) number[0] = 0;
    if (text) text[0] = 0;
    (void)max_len;
    return 0;
}

int32_t cell_data_connect(cell_modem_t *modem) {
    if (!modem->registered) return -1;
    modem->data_connected = true;
    return 0;
}

int32_t cell_data_disconnect(cell_modem_t *modem) {
    modem->data_connected = false;
    return 0;
}

int32_t cell_get_signal(cell_modem_t *modem) {
    /* AT+CSQ returns signal quality */
    return (int32_t)modem->signal_dbm;
}

/* ===== SATELLITE ===== */
void sat_init(satellite_link_t *sat, sat_type_t type, uint32_t sat_id) {
    mem_set(sat, 0, sizeof(*sat));
    sat->type = type;
    sat->sat_id = sat_id;
    sat->signal_dbm = -120;

    if (type == SAT_TYPE_GEO) {
        sat->latency_ms = 250;   /* ~36000km altitude */
        sat->uplink_freq_mhz = 14000;
        sat->downlink_freq_mhz = 12000;
    } else if (type == SAT_TYPE_LEO) {
        sat->latency_ms = 20;    /* ~550km altitude (Starlink) */
        sat->uplink_freq_mhz = 14000;
        sat->downlink_freq_mhz = 10750;
        sat->mean_motion = 15.0f; /* ~15 orbits/day */
        sat->inclination = 53.0f;
    } else {
        sat->latency_ms = 100;
        sat->uplink_freq_mhz = 6000;
        sat->downlink_freq_mhz = 4000;
    }
}

int32_t sat_connect(satellite_link_t *sat) {
    sat->connected = true;
    return 0;
}

int32_t sat_disconnect(satellite_link_t *sat) {
    sat->connected = false;
    return 0;
}

int32_t sat_send(satellite_link_t *sat, const void *data, uint32_t len) {
    if (!sat->connected) return -1;
    (void)data;
    return (int32_t)len;
}

int32_t sat_recv(satellite_link_t *sat, void *buf, uint32_t max_len) {
    if (!sat->connected) return -1;
    if (buf) mem_set(buf, 0, max_len);
    return 0;
}

int32_t sat_track_leo(satellite_link_t *sat, uint32_t current_tick) {
    if (sat->type != SAT_TYPE_LEO) return 0;
    /* Simplified orbital tracking: compute visibility window */
    /* A LEO satellite at ~550km has ~15 min visibility per pass */
    uint32_t orbit_period = (uint32_t)(86400 / sat->mean_motion); /* seconds in ticks */
    uint32_t phase = (current_tick - sat->epoch_tick) % orbit_period;
    uint32_t visibility_window = 900; /* 15 minutes in seconds */

    if (phase < visibility_window) {
        sat->signal_dbm = -70 - (int8_t)(phase / 10);
        return 1; /* Visible */
    }
    sat->signal_dbm = -120;
    return 0; /* Not visible */
}

bool sat_is_visible(satellite_link_t *sat, uint32_t current_tick) {
    return sat_track_leo(sat, current_tick) > 0;
}

/* ===== STARLINK ===== */
void starlink_init(starlink_terminal_t *st) {
    mem_set(st, 0, sizeof(*st));
    sat_init(&st->link, SAT_TYPE_LEO, 0);
    st->link.bandwidth_mhz = 240;
    st->phased_array_aligned = false;
}

int32_t starlink_align(starlink_terminal_t *st, float lat, float lon) {
    (void)lat; (void)lon;
    /* Phased array auto-aligns to nearest satellite */
    st->azimuth = 180.0f;   /* Simplified */
    st->elevation = 45.0f;
    st->phased_array_aligned = true;
    return 0;
}

int32_t starlink_handoff(starlink_terminal_t *st, uint32_t new_sat_id) {
    st->link.sat_id = new_sat_id;
    return 0;
}

/* ===== AM/FM RADIO ===== */
void radio_init(radio_interface_t *radio, radio_modulation_t mod, uint32_t freq_hz) {
    mem_set(radio, 0, sizeof(*radio));
    radio->modulation = mod;
    radio->frequency_hz = freq_hz;
    radio->signal_dbm = -127;

    switch (mod) {
        case RADIO_AM:      radio->bandwidth_hz = 10000; break;   /* 10 kHz */
        case RADIO_FM:      radio->bandwidth_hz = 200000; break;  /* 200 kHz */
        case RADIO_WBFM:    radio->bandwidth_hz = 200000; break;
        case RADIO_NBFM:    radio->bandwidth_hz = 12500; break;   /* 12.5 kHz */
        case RADIO_SSB_USB:
        case RADIO_SSB_LSB: radio->bandwidth_hz = 2700; break;    /* 2.7 kHz */
        case RADIO_CW:      radio->bandwidth_hz = 500; break;
        default:            radio->bandwidth_hz = 12500; break;
    }
}

int32_t radio_rx(radio_interface_t *radio, int16_t *audio_out, uint32_t max_samples) {
    if (!radio->squelch_open) return 0;

    switch (radio->modulation) {
        case RADIO_AM:
        case RADIO_CW:
            return am_demodulate(radio->iq_buffer, radio->iq_count, audio_out, max_samples);
        case RADIO_FM:
        case RADIO_WBFM:
        case RADIO_NBFM:
        case RADIO_DMR:
            return fm_demodulate(radio->iq_buffer, radio->iq_count, audio_out, max_samples);
        case RADIO_SSB_USB:
            return ssb_demodulate(radio->iq_buffer, radio->iq_count, audio_out, max_samples, true);
        case RADIO_SSB_LSB:
            return ssb_demodulate(radio->iq_buffer, radio->iq_count, audio_out, max_samples, false);
        default:
            return 0;
    }
}

int32_t radio_tx(radio_interface_t *radio, const int16_t *audio_in, uint32_t num_samples) {
    if (!radio->tx_enabled) return -1;
    (void)audio_in;
    return (int32_t)num_samples;
}

int32_t radio_set_frequency(radio_interface_t *radio, uint32_t freq_hz) {
    radio->frequency_hz = freq_hz;
    return 0;
}

int32_t radio_set_bandwidth(radio_interface_t *radio, uint32_t bw_hz) {
    radio->bandwidth_hz = bw_hz;
    return 0;
}

int8_t radio_get_signal(radio_interface_t *radio) {
    return radio->signal_dbm;
}

/* AM demodulation: envelope detection */
int32_t am_demodulate(const int16_t *iq, uint32_t n, int16_t *audio, uint32_t max_out) {
    uint32_t out = 0;
    for (uint32_t i = 0; i + 1 < n && out < max_out; i += 2) {
        /* Magnitude = sqrt(I^2 + Q^2) */
        int32_t i_val = iq[i];
        int32_t q_val = iq[i + 1];
        double mag = fs_sqrt((double)(i_val * i_val + q_val * q_val));
        /* DC removal: subtract average */
        audio[out++] = (int16_t)(mag - 16384);
    }
    return (int32_t)out;
}

/* FM demodulation: differentiate phase */
int32_t fm_demodulate(const int16_t *iq, uint32_t n, int16_t *audio, uint32_t max_out) {
    uint32_t out = 0;
    int32_t prev_i = 0, prev_q = 0;
    for (uint32_t i = 0; i + 1 < n && out < max_out; i += 2) {
        int32_t i_val = iq[i];
        int32_t q_val = iq[i + 1];
        /* Phase difference = atan2(q*prev_i - i*prev_q, i*prev_i + q*prev_q) */
        int32_t cross = q_val * prev_i - i_val * prev_q;
        int32_t dot = i_val * prev_i + q_val * prev_q;
        /* Simplified: just use cross product as approximation */
        audio[out++] = (int16_t)(cross / 256);
        prev_i = i_val;
        prev_q = q_val;
    }
    return (int32_t)out;
}

/* SSB demodulation: mix with local oscillator */
int32_t ssb_demodulate(const int16_t *iq, uint32_t n, int16_t *audio, uint32_t max_out, bool usb) {
    uint32_t out = 0;
    (void)usb;
    for (uint32_t i = 0; i + 1 < n && out < max_out; i += 2) {
        /* For SSB, just take the I component (simplified) */
        audio[out++] = iq[i];
    }
    return (int32_t)out;
}

/* ===== FUTURISTIC ===== */
void quantum_init(quantum_link_t *q) {
    mem_set(q, 0, sizeof(*q));
    q->coherence_time_ms = 100;
    q->error_rate_bps = 0;
}

int32_t quantum_entangle(quantum_link_t *q, uint32_t partner_id) {
    q->partner_node_id = partner_id;
    q->entangled = true;
    return 0;
}

int32_t quantum_send(quantum_link_t *q, const void *data, uint32_t len) {
    if (!q->entangled) return -1;
    (void)data;
    /* Quantum teleportation: classical channel + entanglement */
    return (int32_t)len;
}

int32_t quantum_recv(quantum_link_t *q, void *buf, uint32_t max_len) {
    if (!q->entangled) return -1;
    if (buf) mem_set(buf, 0, max_len);
    return 0;
}

void laser_init(laser_link_t *l, uint32_t wavelength_nm) {
    mem_set(l, 0, sizeof(*l));
    l->wavelength_nm = wavelength_nm;
    l->power_mw = 100;
    l->beam_divgence_mrad = 0.1f;
    l->range_km = 1000;
}

int32_t laser_send(laser_link_t *l, const void *data, uint32_t len) {
    if (!l->aligned) return -1;
    (void)data;
    return (int32_t)len;
}

int32_t laser_recv(laser_link_t *l, void *buf, uint32_t max_len) {
    if (!l->aligned) return -1;
    if (buf) mem_set(buf, 0, max_len);
    return 0;
}

void neutrino_init(neutrino_link_t *n) {
    mem_set(n, 0, sizeof(*n));
    n->energy_gev = 1000;
}

int32_t neutrino_send(neutrino_link_t *n, const void *data, uint32_t len) {
    if (!n->active) return -1;
    (void)data;
    /* Neutrino communication: extremely low bandwidth, through any matter */
    return (int32_t)len;
}

int32_t neutrino_recv(neutrino_link_t *n, void *buf, uint32_t max_len) {
    if (!n->active) return -1;
    if (buf) mem_set(buf, 0, max_len);
    return 0;
}

/* ===== M5 ADAPTER REGISTRATION ===== */
/* Each adapter wraps its protocol to conform to M5 routing */

static bool cell_adapter_send(const m5_address_t *dest, const void *data, uint32_t len,
                               const m5_net_header_t *m5_meta) {
    (void)dest; (void)data; (void)len; (void)m5_meta;
    return true; /* Would send via cellular modem */
}

static uint32_t cell_adapter_poll(void *buf, uint32_t max_len, m5_address_t *src) {
    (void)buf; (void)max_len;
    if (src) { mem_set(src, 0, sizeof(*src)); src->proto = M5_PROTO_CELLULAR; }
    return 0;
}

void m5_adapter_register_cellular(m5_router_t *r, cell_modem_t *modem) {
    (void)modem;
    m5_router_register_adapter(r, M5_PROTO_CELLULAR, "Cellular",
                               cell_adapter_send, cell_adapter_poll, 0, 0);
}

static bool sat_adapter_send(const m5_address_t *dest, const void *data, uint32_t len,
                              const m5_net_header_t *m5_meta) {
    (void)dest; (void)data; (void)len; (void)m5_meta;
    return true;
}

static uint32_t sat_adapter_poll(void *buf, uint32_t max_len, m5_address_t *src) {
    (void)buf; (void)max_len;
    if (src) { mem_set(src, 0, sizeof(*src)); src->proto = M5_PROTO_SATELLITE; }
    return 0;
}

void m5_adapter_register_satellite(m5_router_t *r, satellite_link_t *sat) {
    (void)sat;
    m5_router_register_adapter(r, M5_PROTO_SATELLITE, "Satellite-GEO",
                               sat_adapter_send, sat_adapter_poll, 0, 0);
}

void m5_adapter_register_starlink(m5_router_t *r, starlink_terminal_t *st) {
    (void)st;
    m5_router_register_adapter(r, M5_PROTO_LEO, "Starlink-LEO",
                               sat_adapter_send, sat_adapter_poll, 0, 0);
}

static bool radio_adapter_send(const m5_address_t *dest, const void *data, uint32_t len,
                                const m5_net_header_t *m5_meta) {
    (void)dest; (void)data; (void)len; (void)m5_meta;
    return true;
}

static uint32_t radio_adapter_poll(void *buf, uint32_t max_len, m5_address_t *src) {
    (void)buf; (void)max_len;
    if (src) { mem_set(src, 0, sizeof(*src)); src->proto = M5_PROTO_AM_RADIO; }
    return 0;
}

void m5_adapter_register_radio(m5_router_t *r, radio_interface_t *radio) {
    m5_proto_t proto = (radio->modulation == RADIO_AM) ? M5_PROTO_AM_RADIO : M5_PROTO_FM_RADIO;
    m5_router_register_adapter(r, proto, "Radio",
                               radio_adapter_send, radio_adapter_poll, 0, 0);
}

void m5_adapter_register_quantum(m5_router_t *r, quantum_link_t *q) {
    (void)q;
    m5_router_register_adapter(r, M5_PROTO_QUANTUM, "Quantum",
                               radio_adapter_send, radio_adapter_poll, 0, 0);
}

void m5_adapter_register_laser(m5_router_t *r, laser_link_t *l) {
    (void)l;
    m5_router_register_adapter(r, M5_PROTO_LASER, "Laser",
                               radio_adapter_send, radio_adapter_poll, 0, 0);
}

void m5_adapter_register_neutrino(m5_router_t *r, neutrino_link_t *n) {
    (void)n;
    m5_router_register_adapter(r, M5_PROTO_NEUTRINO, "Neutrino",
                               radio_adapter_send, radio_adapter_poll, 0, 0);
}

/* ===== RADAR ===== */
void radar_init(radar_interface_t *r, radar_band_t band, radar_mode_t mode, uint32_t freq_mhz) {
    mem_set(r, 0, sizeof(*r));
    r->band = band;
    r->mode = mode;
    r->frequency_mhz = freq_mhz;
    r->pulse_width_us = 1;
    r->prf_hz = 1000;
    r->range_km = 100;
    r->scanning = false;
    r->num_tracks = 0;
    r->iq_count = 0;
}

int32_t radar_scan(radar_interface_t *r, uint32_t azimuth, uint32_t elevation) {
    r->azimuth_deg = azimuth;
    r->elevation_deg = elevation;
    r->scanning = true;
    r->iq_count = 0;
    return 0;
}

int32_t radar_track_update(radar_interface_t *r, uint32_t track_id,
                            uint32_t range, uint32_t bearing, int32_t velocity) {
    for (uint32_t i = 0; i < r->num_tracks; i++) {
        if (r->tracks[i].track_id == track_id) {
            r->tracks[i].range_km = range;
            r->tracks[i].bearing_deg = bearing;
            r->tracks[i].velocity_kts = velocity;
            r->tracks[i].active = true;
            return 0;
        }
    }
    if (r->num_tracks < 32) {
        r->tracks[r->num_tracks].track_id = track_id;
        r->tracks[r->num_tracks].range_km = range;
        r->tracks[r->num_tracks].bearing_deg = bearing;
        r->tracks[r->num_tracks].velocity_kts = velocity;
        r->tracks[r->num_tracks].snr_db = 20;
        r->tracks[r->num_tracks].active = true;
        r->num_tracks++;
        return 0;
    }
    return -1;
}

int32_t radar_get_returns(radar_interface_t *r, int16_t *iq_out, uint32_t max_samples) {
    uint32_t n = r->iq_count < max_samples ? r->iq_count : max_samples;
    for (uint32_t i = 0; i < n; i++)
        iq_out[i] = r->iq_returns[i];
    return (int32_t)n;
}

uint32_t radar_detect_contacts(radar_interface_t *r) {
    return r->num_tracks;
}

int32_t radar_set_mode(radar_interface_t *r, radar_mode_t mode) {
    r->mode = mode;
    return 0;
}

int32_t radar_set_band(radar_interface_t *r, radar_band_t band, uint32_t freq_mhz) {
    r->band = band;
    r->frequency_mhz = freq_mhz;
    return 0;
}

/* ===== LIDAR ===== */
void lidar_init(lidar_interface_t *l, lidar_type_t type, lidar_wavelength_t wl, uint32_t range_m) {
    mem_set(l, 0, sizeof(*l));
    l->type = type;
    l->wavelength = wl;
    switch (wl) {
        case LIDAR_WAVELENGTH_905NM:  l->wavelength_nm = 905;  break;
        case LIDAR_WAVELENGTH_1064NM: l->wavelength_nm = 1064; break;
        case LIDAR_WAVELENGTH_1550NM: l->wavelength_nm = 1550; break;
        case LIDAR_WAVELENGTH_532NM:  l->wavelength_nm = 532;  break;
        default: l->wavelength_nm = 905; break;
    }
    l->range_m = range_m;
    l->pulse_rate_khz = 200;
    l->angular_res_mdeg = 100;
    l->fov_horizontal_deg = 360;
    l->fov_vertical_deg = 30;
    l->points_per_second = 300000;
    l->scanning = false;
    l->point_count = 0;
    l->rotation_hz = 10;
    l->current_angle_centi_deg = 0;
}

int32_t lidar_scan(lidar_interface_t *l, uint32_t duration_ms) {
    l->scanning = true;
    (void)duration_ms;
    l->point_count = 0;
    return 0;
}

int32_t lidar_get_pointcloud(lidar_interface_t *l, lidar_point_t *out, uint32_t max_points) {
    uint32_t n = l->point_count < max_points ? l->point_count : max_points;
    for (uint32_t i = 0; i < n; i++)
        out[i] = l->point_cloud[i];
    return (int32_t)n;
}

uint32_t lidar_point_count(lidar_interface_t *l) {
    return l->point_count;
}

int32_t lidar_classify_terrain(lidar_interface_t *l) {
    for (uint32_t i = 0; i < l->point_count; i++) {
        if (l->point_cloud[i].intensity < 100)
            l->point_cloud[i].classification = 2;  /* Ground */
        else if (l->point_cloud[i].intensity < 1000)
            l->point_cloud[i].classification = 3;  /* Vegetation */
        else
            l->point_cloud[i].classification = 6;  /* Building */
    }
    return 0;
}

int32_t lidar_detect_obstacles(lidar_interface_t *l, uint32_t range_threshold_m) {
    int32_t count = 0;
    for (uint32_t i = 0; i < l->point_count; i++) {
        if (l->point_cloud[i].range_mm < range_threshold_m * 1000 &&
            l->point_cloud[i].classification != 2)
            count++;
    }
    return count;
}

int32_t lidar_set_resolution(lidar_interface_t *l, uint32_t angular_mdeg) {
    l->angular_res_mdeg = angular_mdeg;
    return 0;
}

/* ===== ULF/ELF/VLF SUBMARINE COMMUNICATIONS ===== */
void ulf_init(ulf_link_t *u, ulf_band_t band, ulf_encoding_t enc, uint32_t freq_hz) {
    mem_set(u, 0, sizeof(*u));
    u->band = band;
    u->encoding = enc;
    u->frequency_hz = freq_hz;
    u->bandwidth_hz = 1;
    u->transmit_power_kw = 1000;
    u->antenna_length_km = 45;
    u->submerged = false;
    u->sub_depth_m = 0;
    u->propagation_loss_db = 0;
    u->msg_len = 0;
    u->msg_progress_bits = 0;

    /* Data rate depends on band */
    switch (band) {
        case ULF_BAND_ELF: u->data_rate_bps = 1;    break;  /* 3-30 Hz: ~1 bps */
        case ULF_BAND_SLF: u->data_rate_bps = 5;    break;  /* 30-300 Hz: ~5 bps */
        case ULF_BAND_ULF: u->data_rate_bps = 50;   break;  /* 300-3000 Hz: ~50 bps */
        case ULF_BAND_VLF: u->data_rate_bps = 100;  break;  /* 3-30 kHz: ~100 bps */
        default: u->data_rate_bps = 1; break;
    }
}

int32_t ulf_transmit(ulf_link_t *u, const void *data, uint32_t len) {
    if (len > 64) len = 64;
    mem_copy(u->msg_buffer, data, len);
    u->msg_len = len;
    u->msg_progress_bits = 0;
    return (int32_t)len;
}

int32_t ulf_receive(ulf_link_t *u, void *buf, uint32_t max_len) {
    uint32_t n = u->msg_len < max_len ? u->msg_len : max_len;
    mem_copy(buf, u->msg_buffer, n);
    return (int32_t)n;
}

int32_t ulf_set_depth(ulf_link_t *u, uint32_t depth_m) {
    u->sub_depth_m = depth_m;
    u->submerged = depth_m > 0;
    u->propagation_loss_db = ulf_calc_propagation_loss(depth_m, u->frequency_hz);
    return 0;
}

uint32_t ulf_calc_propagation_loss(uint32_t depth_m, uint32_t freq_hz) {
    /* Seawater attenuation: alpha = 0.0173 * sqrt(f) dB/m (approximate)
     * where f is in Hz. This is the skin depth model. */
    /* sqrt(f) with integer math — use fixed-point approximation */
    uint32_t sqrt_f = 1;
    while (sqrt_f * sqrt_f < freq_hz) sqrt_f++;
    /* alpha_db_per_m = 173 * sqrt_f / 10000 (scaled) */
    /* total_loss = alpha * depth * 10 (to get dB) */
    uint32_t loss = (depth_m * sqrt_f) / 578;  /* Approximate dB loss */
    return loss;
}

uint32_t ulf_max_depth_for_freq(uint32_t freq_hz) {
    /* Maximum depth where signal is still detectable (~30 dB threshold) */
    uint32_t sqrt_f = 1;
    while (sqrt_f * sqrt_f < freq_hz) sqrt_f++;
    /* depth_max = 30 * 578 / sqrt_f */
    if (sqrt_f == 0) return 0;
    return (30 * 578) / sqrt_f;
}

int32_t ulf_send_emergency(ulf_link_t *u, uint32_t code) {
    /* Emergency broadcast: 3-bit coded pulse */
    u->msg_buffer[0] = (uint8_t)(code & 0xFF);
    u->msg_buffer[1] = (uint8_t)((code >> 8) & 0xFF);
    u->msg_buffer[2] = (uint8_t)((code >> 16) & 0xFF);
    u->msg_buffer[3] = (uint8_t)((code >> 24) & 0xFF);
    u->msg_len = 4;
    u->msg_progress_bits = 0;
    return 4;
}

/* ===== NEW M5 ADAPTER REGISTRATIONS ===== */
static bool radar_adapter_send(const m5_address_t *dest, const void *data, uint32_t len,
                                const m5_net_header_t *m5_meta) {
    (void)dest; (void)data; (void)len; (void)m5_meta;
    return true;
}

static uint32_t radar_adapter_poll(void *buf, uint32_t max_len, m5_address_t *src) {
    (void)buf; (void)max_len;
    if (src) { mem_set(src, 0, sizeof(*src)); src->proto = M5_PROTO_RADAR; }
    return 0;
}

void m5_adapter_register_radar(m5_router_t *r, radar_interface_t *radar) {
    (void)radar;
    m5_router_register_adapter(r, M5_PROTO_RADAR, "Radar",
                               radar_adapter_send, radar_adapter_poll, 0, 0);
}

void m5_adapter_register_lidar(m5_router_t *r, lidar_interface_t *lidar) {
    (void)lidar;
    m5_router_register_adapter(r, M5_PROTO_LIDAR, "LIDAR",
                               radar_adapter_send, radar_adapter_poll, 0, 0);
}

void m5_adapter_register_ulf(m5_router_t *r, ulf_link_t *ulf) {
    (void)ulf;
    m5_router_register_adapter(r, M5_PROTO_ULF, "ULF-Submarine",
                               radar_adapter_send, radar_adapter_poll, 0, 0);
}
