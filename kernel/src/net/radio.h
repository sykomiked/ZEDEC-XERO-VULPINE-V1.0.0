/* radio.h — Cellular, Satellite, AM/FM Radio, and Futuristic Protocol Interfaces
 * All are M5 protocol adapters — they conform to M5, not vice versa.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef RADIO_H
#define RADIO_H

#include <stdint.h>
#include <stdbool.h>
#include "m5route.h"

/* ===== CELLULAR ===== */
typedef enum {
    CELL_GEN_2G = 0,   /* GSM/GPRS/EDGE */
    CELL_GEN_3G = 1,   /* UMTS/HSPA */
    CELL_GEN_4G = 2,   /* LTE/LTE-A */
    CELL_GEN_5G = 3,   /* NR */
    CELL_GEN_6G = 4    /* Future */
} cell_generation_t;

typedef struct cell_modem {
    cell_generation_t gen;
    uint32_t mcc;       /* Mobile Country Code */
    uint32_t mnc;       /* Mobile Network Code */
    uint32_t cell_id;
    uint32_t lac;       /* Location Area Code */
    int8_t   signal_dbm;
    bool     registered;
    bool     data_connected;

    /* AT command interface */
    void (*at_send)(const char *cmd);
    int32_t (*at_recv)(char *buf, uint32_t max_len);
} cell_modem_t;

void cell_init(cell_modem_t *modem, cell_generation_t gen);
int32_t cell_at_command(cell_modem_t *modem, const char *cmd, char *resp, uint32_t max_resp);
int32_t cell_register(cell_modem_t *modem);
int32_t cell_send_sms(cell_modem_t *modem, const char *number, const char *text);
int32_t cell_recv_sms(cell_modem_t *modem, char *number, char *text, uint32_t max_len);
int32_t cell_data_connect(cell_modem_t *modem);
int32_t cell_data_disconnect(cell_modem_t *modem);
int32_t cell_get_signal(cell_modem_t *modem);

/* ===== SATELLITE ===== */
typedef enum {
    SAT_TYPE_GEO = 0,    /* Geostationary */
    SAT_TYPE_LEO = 1,    /* Low Earth Orbit (Starlink, Iridium) */
    SAT_TYPE_MEO = 2     /* Medium Earth Orbit (GPS, O3b) */
} sat_type_t;

typedef struct satellite_link {
    sat_type_t type;
    uint32_t sat_id;
    uint32_t beam_id;
    uint32_t uplink_freq_mhz;
    uint32_t downlink_freq_mhz;
    uint32_t bandwidth_mhz;
    int8_t   signal_dbm;
    uint32_t latency_ms;
    bool     connected;

    /* Orbital tracking for LEO */
    int32_t inclination_mdeg;   /* inclination, milli-degrees */
    int32_t raan_mdeg;          /* Right Ascension of Ascending Node, milli-degrees */
    uint32_t mean_motion_milli; /* Revolutions per day x 1000 */
    uint32_t epoch_tick;
} satellite_link_t;

void sat_init(satellite_link_t *sat, sat_type_t type, uint32_t sat_id);
int32_t sat_connect(satellite_link_t *sat);
int32_t sat_disconnect(satellite_link_t *sat);
int32_t sat_send(satellite_link_t *sat, const void *data, uint32_t len);
int32_t sat_recv(satellite_link_t *sat, void *buf, uint32_t max_len);
int32_t sat_track_leo(satellite_link_t *sat, uint32_t current_tick);
bool sat_is_visible(satellite_link_t *sat, uint32_t current_tick);

/* Starlink-specific */
typedef struct starlink_terminal {
    satellite_link_t link;
    uint32_t user_terminal_id;
    uint8_t  service_cell_id[6];
    bool     phased_array_aligned;
    int32_t azimuth_mdeg;   /* milli-degrees */
    int32_t elevation_mdeg; /* milli-degrees */
} starlink_terminal_t;

void starlink_init(starlink_terminal_t *st);
/* lat/lon in micro-degrees (integer; kernel images have no floating point) */
int32_t starlink_align(starlink_terminal_t *st, int32_t lat_udeg, int32_t lon_udeg);
int32_t starlink_handoff(starlink_terminal_t *st, uint32_t new_sat_id);

/* ===== AM/FM RADIO ===== */
typedef enum {
    RADIO_AM = 0,
    RADIO_FM = 1,
    RADIO_SSB_USB = 2,   /* Single Sideband - Upper */
    RADIO_SSB_LSB = 3,   /* Single Sideband - Lower */
    RADIO_CW = 4,        /* Continuous Wave (Morse) */
    RADIO_NBFM = 5,      /* Narrow FM */
    RADIO_WBFM = 6,      /* Wide FM (broadcast) */
    RADIO_DMR = 7,       /* Digital Mobile Radio */
    RADIO_DSTAR = 8,     /* Icom D-STAR */
    RADIO_YSF = 9,       /* Yaesu System Fusion */
    RADIO_P25 = 10       /* APCO P25 */
} radio_modulation_t;

typedef struct radio_interface {
    radio_modulation_t modulation;
    uint32_t frequency_hz;
    uint32_t bandwidth_hz;
    int8_t   signal_dbm;
    bool     squelch_open;
    bool     tx_enabled;

    /* SDR-style IQ buffer */
    int16_t  iq_buffer[4096];
    uint32_t iq_count;
} radio_interface_t;

void radio_init(radio_interface_t *radio, radio_modulation_t mod, uint32_t freq_hz);
int32_t radio_rx(radio_interface_t *radio, int16_t *audio_out, uint32_t max_samples);
int32_t radio_tx(radio_interface_t *radio, const int16_t *audio_in, uint32_t num_samples);
int32_t radio_set_frequency(radio_interface_t *radio, uint32_t freq_hz);
int32_t radio_set_bandwidth(radio_interface_t *radio, uint32_t bw_hz);
int8_t  radio_get_signal(radio_interface_t *radio);

/* AM demodulation */
int32_t am_demodulate(const int16_t *iq, uint32_t n, int16_t *audio, uint32_t max_out);
/* FM demodulation */
int32_t fm_demodulate(const int16_t *iq, uint32_t n, int16_t *audio, uint32_t max_out);
/* SSB demodulation */
int32_t ssb_demodulate(const int16_t *iq, uint32_t n, int16_t *audio, uint32_t max_out, bool usb);

/* ===== FUTURISTIC ===== */
typedef struct quantum_link {
    uint32_t entanglement_id;
    uint32_t partner_node_id;
    bool     entangled;
    uint32_t coherence_time_ms;
    uint32_t error_rate_bps;
} quantum_link_t;

void quantum_init(quantum_link_t *q);
int32_t quantum_entangle(quantum_link_t *q, uint32_t partner_id);
int32_t quantum_send(quantum_link_t *q, const void *data, uint32_t len);
int32_t quantum_recv(quantum_link_t *q, void *buf, uint32_t max_len);

typedef struct laser_link {
    uint32_t wavelength_nm;    /* e.g., 1550nm for telecom */
    uint32_t power_mw;
    uint32_t beam_divergence_urad; /* micro-radians */
    uint32_t range_km;
    bool     aligned;
} laser_link_t;

void laser_init(laser_link_t *l, uint32_t wavelength_nm);
int32_t laser_send(laser_link_t *l, const void *data, uint32_t len);
int32_t laser_recv(laser_link_t *l, void *buf, uint32_t max_len);

typedef struct neutrino_link {
    uint32_t detector_id;
    uint32_t target_id;
    uint32_t energy_gev;
    bool     active;
} neutrino_link_t;

void neutrino_init(neutrino_link_t *n);
int32_t neutrino_send(neutrino_link_t *n, const void *data, uint32_t len);
int32_t neutrino_recv(neutrino_link_t *n, void *buf, uint32_t max_len);

/* ===== M5 ADAPTER REGISTRATION ===== */
void m5_adapter_register_cellular(m5_router_t *r, cell_modem_t *modem);
void m5_adapter_register_satellite(m5_router_t *r, satellite_link_t *sat);
void m5_adapter_register_starlink(m5_router_t *r, starlink_terminal_t *st);
void m5_adapter_register_radio(m5_router_t *r, radio_interface_t *radio);
void m5_adapter_register_quantum(m5_router_t *r, quantum_link_t *q);
void m5_adapter_register_laser(m5_router_t *r, laser_link_t *l);
void m5_adapter_register_neutrino(m5_router_t *r, neutrino_link_t *n);

/* ===== RADAR ===== */
typedef enum {
    RADAR_BAND_X = 0,       /* 8-12 GHz — military/fire-control */
    RADAR_BAND_S = 1,       /* 2-4 GHz — surveillance/weather */
    RADAR_BAND_L = 2,       /* 1-2 GHz — air traffic/long-range */
    RADAR_BAND_C = 3,       /* 4-8 GHz — weather/military */
    RADAR_BAND_KU = 4,      /* 12-18 GHz — high-resolution mapping */
    RADAR_BAND_KA = 5,      /* 27-40 GHz — short-range/high-def */
    RADAR_BAND_UHF = 6,     /* 300-1000 MHz — early warning/over-horizon */
    RADAR_BAND_HF = 7       /* 3-30 MHz — over-the-horizon radar */
} radar_band_t;

typedef enum {
    RADAR_MODE_SEARCH = 0,      /* Surveillance scan */
    RADAR_MODE_TRACK = 1,       /* Single-target tracking */
    RADAR_MODE_MAP = 2,         /* Ground/surface mapping */
    RADAR_MODE_DOPPLER = 3,     /* Velocity measurement */
    RADAR_MODE_SAR = 4,         /* Synthetic aperture imaging */
    RADAR_MODE_FMCW = 5         /* Frequency-modulated continuous wave */
} radar_mode_t;

typedef struct radar_interface {
    radar_band_t band;
    radar_mode_t mode;
    uint32_t frequency_mhz;     /* Center frequency */
    uint32_t pulse_width_us;    /* Pulse width in microseconds */
    uint32_t prf_hz;            /* Pulse repetition frequency */
    uint32_t range_km;          /* Maximum detection range */
    uint32_t azimuth_deg;       /* Current antenna azimuth */
    uint32_t elevation_deg;     /* Current antenna elevation */
    int16_t  signal_dbm;        /* Return signal strength */
    bool     scanning;          /* Active scan in progress */

    /* Radar return buffer — raw I/Q samples */
    int16_t  iq_returns[8192];
    uint32_t iq_count;

    /* Track table — contacts being tracked */
    struct {
        uint32_t track_id;
        uint32_t range_km;
        uint32_t bearing_deg;
        int32_t  velocity_kts;
        uint32_t snr_db;
        bool     active;
    } tracks[32];
    uint32_t num_tracks;
} radar_interface_t;

void radar_init(radar_interface_t *r, radar_band_t band, radar_mode_t mode, uint32_t freq_mhz);
int32_t radar_scan(radar_interface_t *r, uint32_t azimuth, uint32_t elevation);
int32_t radar_track_update(radar_interface_t *r, uint32_t track_id,
                            uint32_t range, uint32_t bearing, int32_t velocity);
int32_t radar_get_returns(radar_interface_t *r, int16_t *iq_out, uint32_t max_samples);
uint32_t radar_detect_contacts(radar_interface_t *r);
int32_t radar_set_mode(radar_interface_t *r, radar_mode_t mode);
int32_t radar_set_band(radar_interface_t *r, radar_band_t band, uint32_t freq_mhz);

/* ===== LIDAR ===== */
typedef enum {
    LIDAR_TYPE_TOF = 0,        /* Time-of-flight pulsed */
    LIDAR_TYPE_FMCW = 1,       /* Frequency-modulated continuous wave */
    LIDAR_TYPE_FLASH = 2,      /* Flash LIDAR (single-pulse 3D) */
    LIDAR_TYPE_SOLID_STATE = 3 /* Solid-state MEMS */
} lidar_type_t;

typedef enum {
    LIDAR_WAVELENGTH_905NM = 0,   /* Near-infrared — automotive */
    LIDAR_WAVELENGTH_1064NM = 1,  /* Nd:YAG — survey/mapping */
    LIDAR_WAVELENGTH_1550NM = 2,  /* Eye-safe — long range */
    LIDAR_WAVELENGTH_532NM = 3    /* Green — bathymetric/underwater */
} lidar_wavelength_t;

typedef struct lidar_point {
    uint32_t range_mm;        /* Distance in millimeters */
    int32_t  azimuth_centi_deg;  /* Azimuth in centidegrees (0-36000) */
    int32_t  elevation_centi_deg;/* Elevation in centidegrees */
    uint16_t intensity;       /* Reflectivity 0-65535 */
    uint8_t  return_number;   /* 1st/2nd/3rd return */
    uint8_t  classification;  /* Ground/vegetation/building/etc */
} lidar_point_t;

typedef struct lidar_interface {
    lidar_type_t type;
    lidar_wavelength_t wavelength;
    uint32_t wavelength_nm;   /* Actual wavelength in nm */
    uint32_t pulse_rate_khz;  /* Laser pulse repetition rate */
    uint32_t range_m;         /* Maximum range in meters */
    uint32_t angular_res_mdeg;/* Angular resolution in millidegrees */
    uint32_t fov_horizontal_deg;  /* Horizontal field of view */
    uint32_t fov_vertical_deg;    /* Vertical field of view */
    uint32_t points_per_second;   /* Point cloud generation rate */
    bool     scanning;

    /* Point cloud buffer */
    lidar_point_t point_cloud[4096];
    uint32_t point_count;

    /* Rotation motor state (for mechanical LIDAR) */
    uint32_t rotation_hz;
    uint32_t current_angle_centi_deg;
} lidar_interface_t;

void lidar_init(lidar_interface_t *l, lidar_type_t type, lidar_wavelength_t wl, uint32_t range_m);
int32_t lidar_scan(lidar_interface_t *l, uint32_t duration_ms);
int32_t lidar_get_pointcloud(lidar_interface_t *l, lidar_point_t *out, uint32_t max_points);
uint32_t lidar_point_count(lidar_interface_t *l);
int32_t lidar_classify_terrain(lidar_interface_t *l);
int32_t lidar_detect_obstacles(lidar_interface_t *l, uint32_t range_threshold_m);
int32_t lidar_set_resolution(lidar_interface_t *l, uint32_t angular_mdeg);

/* ===== ULF/ELF/VLF SUBMARINE COMMUNICATIONS ===== */
typedef enum {
    ULF_BAND_ELF = 0,    /* Extremely Low Frequency: 3-30 Hz */
    ULF_BAND_SLF = 1,    /* Super Low Frequency: 30-300 Hz */
    ULF_BAND_ULF = 2,    /* Ultra Low Frequency: 300-3000 Hz */
    ULF_BAND_VLF = 3     /* Very Low Frequency: 3-30 kHz */
} ulf_band_t;

typedef enum {
    ULF_ENCODING_MORSE = 0,     /* Slow-rate Morse code */
    ULF_ENCODING_BINARY_PPM = 1,/* Binary pulse-position modulation */
    ULF_ENCODING_FREQ_SHIFT = 2,/* Frequency-shift keying */
    ULF_ENCODING_MINUTEMAN = 3 /* Minuteman-style coded orders */
} ulf_encoding_t;

typedef struct ulf_link {
    ulf_band_t band;
    ulf_encoding_t encoding;
    uint32_t frequency_hz;      /* Center frequency (3 Hz - 30 kHz) */
    uint32_t bandwidth_hz;      /* Bandwidth (typically very narrow) */
    uint32_t data_rate_bps;     /* Extremely low: 1-100 bps */
    uint32_t transmit_power_kw; /* Megawatt-scale for ELF/SLF */
    uint32_t antenna_length_km; /* Trailing-wire or buried antenna */
    int16_t  signal_dbm;        /* Received signal (usually very weak) */
    bool     submerged;         /* Operating submerged */
    uint32_t sub_depth_m;       /* Submarine operating depth */
    uint32_t propagation_loss_db;/* Path loss through seawater */

    /* Message buffer — ULF messages are very short */
    uint8_t  msg_buffer[64];
    uint32_t msg_len;
    uint32_t msg_progress_bits; /* Bit-by-bit progress for slow links */
} ulf_link_t;

void ulf_init(ulf_link_t *u, ulf_band_t band, ulf_encoding_t enc, uint32_t freq_hz);
int32_t ulf_transmit(ulf_link_t *u, const void *data, uint32_t len);
int32_t ulf_receive(ulf_link_t *u, void *buf, uint32_t max_len);
int32_t ulf_set_depth(ulf_link_t *u, uint32_t depth_m);
uint32_t ulf_calc_propagation_loss(uint32_t depth_m, uint32_t freq_hz);
uint32_t ulf_max_depth_for_freq(uint32_t freq_hz);
int32_t ulf_send_emergency(ulf_link_t *u, uint32_t code);

/* ===== M5 ADAPTER REGISTRATION (new) ===== */
void m5_adapter_register_radar(m5_router_t *r, radar_interface_t *radar);
void m5_adapter_register_lidar(m5_router_t *r, lidar_interface_t *lidar);
void m5_adapter_register_ulf(m5_router_t *r, ulf_link_t *ulf);

#endif
