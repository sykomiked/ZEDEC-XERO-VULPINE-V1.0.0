/* jdr_piratenet.h — JDR PirateNet Harmonic Hum Carrier Protocol
 *
 * A comprehensive telecommunication protocol based on the JDR PirateNet
 * harmonic hum carrier concept. Covers all frequency bands and domains:
 *
 *   - ULF/ELF/VLF (3 Hz – 30 kHz): submarine, geological, deep penetration
 *   - LF/MF (30 kHz – 3 MHz): AM broadcast, maritime, navigation
 *   - HF (3 – 30 MHz): shortwave, over-the-horizon, skywave
 *   - VHF (30 – 300 MHz): FM, TV, ham, TETRA, marine, aviation
 *   - UHF (300 MHz – 3 GHz): cellular, WiFi, Bluetooth, GPS, radar
 *   - SHF (3 – 30 GHz): microwave, satellite, WiGig, 5G mmWave
 *   - EHF (30 – 300 GHz): 6G, ultra-broadband, radio astronomy
 *   - THz (300 GHz – 3 THz): terahertz communication
 *   - Optical (IR, visible, UV): free-space optical, Li-Fi, laser
 *   - X-ray/Gamma: exotic/futuristic communication
 *   - Acoustic: underwater, ultrasonic, infrasonic
 *   - Neutrino: beyond-light-speed communication
 *   - Quantum entanglement: instantaneous
 *   - Harmonic hum: the carrier itself — vibrational substrate
 *
 * Hardware-as-code: the protocol is implemented as a virtual SDR
 * (Software Defined Radio) device with register maps, DMA buffers,
 * and IRQ handlers. Three execution modes:
 *   DC: Direct carrier (CW/AM — like direct current)
 *   AC: Alternating carrier (FM/PM — like alternating current)
 *   PC: Phase/photonic carrier (digital/quantum — like photonic current)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */
#ifndef JDR_PIRATENET_H
#define JDR_PIRATENET_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "edp_risk.h"

/* ===== Frequency Bands ===== */

typedef enum {
    JDR_BAND_ULF        = 0,   /* 3 Hz – 3 kHz */
    JDR_BAND_ELF        = 1,   /* 3 – 30 Hz */
    JDR_BAND_VLF        = 2,   /* 3 – 30 kHz */
    JDR_BAND_LF         = 3,   /* 30 – 300 kHz */
    JDR_BAND_MF         = 4,   /* 300 kHz – 3 MHz */
    JDR_BAND_HF         = 5,   /* 3 – 30 MHz */
    JDR_BAND_VHF        = 6,   /* 30 – 300 MHz */
    JDR_BAND_UHF        = 7,   /* 300 MHz – 3 GHz */
    JDR_BAND_SHF        = 8,   /* 3 – 30 GHz */
    JDR_BAND_EHF        = 9,   /* 30 – 300 GHz */
    JDR_BAND_THZ        = 10,  /* 300 GHz – 3 THz */
    JDR_BAND_IR         = 11,  /* Infrared */
    JDR_BAND_VISIBLE    = 12,  /* Visible light */
    JDR_BAND_UV         = 13,  /* Ultraviolet */
    JDR_BAND_XRAY       = 14,  /* X-ray */
    JDR_BAND_GAMMA      = 15,  /* Gamma ray */
    JDR_BAND_ACOUSTIC   = 16,  /* Acoustic/sonic */
    JDR_BAND_INFRASONIC = 17,  /* Below 20 Hz acoustic */
    JDR_BAND_ULTRASONIC = 18,  /* Above 20 kHz acoustic */
    JDR_BAND_NEUTRINO   = 19,  /* Neutrino beam */
    JDR_BAND_QUANTUM    = 20,  /* Quantum entanglement */
    JDR_BAND_HARMONIC   = 21,  /* Pure harmonic hum substrate */
    JDR_BAND_MAX        = 22
} jdr_band_t;

/* ===== Modulation Schemes ===== */

typedef enum {
    JDR_MOD_CW          = 0,   /* Continuous wave (Morse) */
    JDR_MOD_AM          = 1,   /* Amplitude modulation */
    JDR_MOD_FM          = 2,   /* Frequency modulation */
    JDR_MOD_PM          = 3,   /* Phase modulation */
    JDR_MOD_SSB         = 4,   /* Single sideband */
    JDR_MOD_DSB         = 5,   /* Double sideband */
    JDR_MOD_OFDM        = 6,   /* Orthogonal FDM */
    JDR_MOD_FSK         = 7,   /* Frequency shift keying */
    JDR_MOD_PSK         = 8,   /* Phase shift keying */
    JDR_MOD_QAM         = 9,   /* Quadrature amplitude modulation */
    JDR_MOD_APSK        = 10,  /* Amplitude PSK */
    JDR_MOD_GMSK        = 11,  /* Gaussian MSK (GSM) */
    JDR_MOD_OFDM_QAM    = 12,  /* OFDM with QAM (WiFi/LTE) */
    JDR_MOD_CDMA        = 13,  /* Code division multiple access */
    JDR_MOD_FHSS        = 14,  /* Frequency hopping spread spectrum */
    JDR_MOD_DSSS        = 15,  /* Direct sequence spread spectrum */
    JDR_MOD_THZ_PULSE   = 16,  /* THz pulsed */
    JDR_MOD_OOK         = 17,  /* On-off keying (optical) */
    JDR_MOD_PPM         = 18,  /* Pulse position modulation (optical) */
    JDR_MOD_HARMONIC    = 19,  /* Harmonic hum (native) */
    JDR_MOD_QUANTUM     = 20,  /* Quantum state encoding */
    JDR_MOD_NEUTRINO    = 21,  /* Neutrino oscillation encoding */
    JDR_MOD_MAX         = 22
} jdr_modulation_t;

/* ===== Execution Mode ===== */

typedef enum {
    JDR_EXEC_DC = 0,  /* Direct carrier: CW/AM — direct current analog */
    JDR_EXEC_AC = 1,  /* Alternating carrier: FM/PM — alternating current */
    JDR_EXEC_PC = 2,  /* Phase/photonic: digital/quantum — photonic current */
} jdr_exec_mode_t;

/* ===== JDR PirateNet Transceiver Device (hardware-as-code) ===== */

typedef struct {
    uint32_t device_id;
    
    /* Register map — like an SDR chipset */
    uint64_t reg_frequency;         /* Operating frequency (Hz) */
    uint64_t reg_bandwidth;         /* Channel bandwidth (Hz) */
    int32_t  reg_power_dbm;         /* Transmit power (dBm) */
    uint32_t reg_sample_rate;       /* Sample rate (SPS) */
    jdr_band_t reg_band;            /* Active band */
    jdr_modulation_t reg_mod;       /* Active modulation */
    jdr_exec_mode_t reg_exec;       /* Execution mode (DC/AC/PC) */
    
    /* Harmonic hum carrier registers */
    surplus_real_t reg_hum_freq;     /* Fundamental hum frequency */
    surplus_real_t reg_hum_amplitude;/* Hum amplitude */
    surplus_real_t reg_hum_phase;    /* Hum phase */
    surplus_real_t reg_harmonic_n;   /* Harmonic order */
    surplus_real_t harmonics[16];    /* Harmonic series amplitudes */
    uint32_t num_harmonics;
    
    /* M⁵ coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* DMA buffers */
    uint32_t dma_tx_buffer[256];    /* TX DMA ring buffer */
    uint32_t dma_rx_buffer[256];    /* RX DMA ring buffer */
    uint32_t tx_head, tx_tail;
    uint32_t rx_head, rx_tail;
    
    /* IRQ lines */
    bool irq_carrier_lock;          /* Carrier synchronized */
    bool irq_rx_data_ready;         /* Data received */
    bool irq_tx_complete;           /* Transmission complete */
    bool irq_harmonic_resonance;    /* Harmonic resonance achieved */
    bool irq_coverage_breach;       /* M⁵ coverage breached */
    bool irq_frequency_hop;         /* Frequency hopped (FHSS) */
    
    /* Channel state */
    uint32_t channel_id;
    uint32_t node_id;               /* PirateNet node ID */
    char callsign[16];              /* Node callsign */
    bool encrypted;
    uint32_t encryption_key_id;
    
    /* Frequency hopping table (FHSS) */
    uint64_t hop_table[64];
    uint32_t hop_index;
    uint32_t hop_rate;              /* Hops per second */
    
    /* Stats */
    uint32_t packets_tx;
    uint32_t packets_rx;
    uint32_t bytes_tx;
    uint32_t bytes_rx;
    
    bool active;
} jdr_transceiver_t;

/* ===== JDR PirateNet Network ===== */

typedef struct {
    jdr_transceiver_t transceivers[32];
    uint32_t num_transceivers;
    
    /* Network topology */
    uint32_t node_addresses[128];   /* Known node IDs */
    uint32_t num_nodes;
    
    /* Frequency allocation table */
    struct {
        jdr_band_t band;
        uint64_t freq_start;
        uint64_t freq_end;
        bool allocated;
        uint32_t node_id;
    } freq_alloc[64];
    uint32_t num_allocations;
    
    /* System metrics */
    surplus_real_t system_coverage;
    uint32_t total_packets;
    uint32_t total_bytes;
} jdr_network_t;

/* ===== API ===== */

void jdr_network_init(jdr_network_t *net);

/* Create transceiver — like powering on an SDR */
uint32_t jdr_transceiver_create(jdr_network_t *net,
                                 uint64_t frequency,
                                 uint64_t bandwidth,
                                 jdr_band_t band,
                                 jdr_modulation_t mod,
                                 jdr_exec_mode_t exec,
                                 const char *callsign);

/* Harmonic hum carrier operations */
void jdr_hum_init(jdr_transceiver_t *tc,
                   surplus_real_t freq,
                   surplus_real_t amplitude);
void jdr_hum_add_harmonic(jdr_transceiver_t *tc, uint32_t order);
void jdr_hum_compute(jdr_transceiver_t *tc);
bool jdr_hum_lock_carrier(jdr_transceiver_t *tc);

/* Transmit/Receive */
int32_t jdr_transmit(jdr_transceiver_t *tc, const void *data, uint32_t len);
int32_t jdr_receive(jdr_transceiver_t *tc, void *buf, uint32_t max_len);

/* Frequency hopping */
void jdr_fhss_init(jdr_transceiver_t *tc, uint32_t hop_rate);
uint64_t jdr_fhss_next_freq(jdr_transceiver_t *tc);

/* Band switching */
int32_t jdr_switch_band(jdr_transceiver_t *tc, jdr_band_t band, uint64_t freq);

/* Modulation switching */
int32_t jdr_switch_modulation(jdr_transceiver_t *tc, jdr_modulation_t mod);

/* Execution mode switching */
int32_t jdr_switch_exec(jdr_transceiver_t *tc, jdr_exec_mode_t mode);

/* Coverage verification */
bool jdr_verify_coverage(jdr_transceiver_t *tc);

/* Frequency allocation */
int32_t jdr_allocate_frequency(jdr_network_t *net, jdr_band_t band,
                                uint64_t freq_start, uint64_t freq_end,
                                uint32_t node_id);

/* Node registration */
int32_t jdr_register_node(jdr_network_t *net, uint32_t node_id);

/* Names */
const char *jdr_band_name(jdr_band_t b);
const char *jdr_modulation_name(jdr_modulation_t m);
const char *jdr_exec_mode_name(jdr_exec_mode_t e);

/* Band frequency ranges */
uint64_t jdr_band_freq_start(jdr_band_t b);
uint64_t jdr_band_freq_end(jdr_band_t b);

#endif /* JDR_PIRATENET_H */
