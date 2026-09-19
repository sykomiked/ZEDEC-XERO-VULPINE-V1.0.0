/* firmware_adapters.h — JDR PirateNet Firmware Adapter Layer
 *
 * Bridges the virtual SDR (jdr_transceiver_t) to actual hardware devices.
 * Each adapter implements the hardware-specific register maps, DMA,
 * and IRQ handling for a specific radio device type.
 *
 * 22 Bands × 22 Modulations × 3 Execution Modes = 2,904 combinations
 * Each adapter provides the hardware-specific implementation.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef JDR_FIRMWARE_ADAPTERS_H
#define JDR_FIRMWARE_ADAPTERS_H

#include <stdint.h>
#include <stdbool.h>
#include "jdr_piratenet.h"
#include "rtl_device.h"
#include "surplus.h"
#include "lpres.h"
#include "m5_types.h"

/* ===== Adapter Capability Flags ===== */
#define JDR_ADAPTER_CAP_TX          0x00000001
#define JDR_ADAPTER_CAP_RX          0x00000002
#define JDR_ADAPTER_CAP_FHSS        0x00000004
#define JDR_ADAPTER_CAP_HARMONIC    0x00000008
#define JDR_ADAPTER_CAP_QUANTUM     0x00000010
#define JDR_ADAPTER_CAP_NEUTRINO    0x00000020
#define JDR_ADAPTER_CAP_HARDWARE_SDR 0x00000040
#define JDR_ADAPTER_CAP_SOFTWARE_SDR 0x00000080
#define JDR_ADAPTER_CAP_MIMO        0x00000100
#define JDR_ADAPTER_CAP_BEAMFORMING 0x00000200
#define JDR_ADAPTER_CAP_FULL_DUPLEX 0x00000400
#define JDR_ADAPTER_CAP_HALF_DUPLEX 0x00000800
#define JDR_ADAPTER_CAP_CARRIER_LOCK 0x00001000
#define JDR_ADAPTER_CAP_HUM_CARRIER 0x00002000
#define JDR_ADAPTER_CAP_DC_MODE     0x00004000
#define JDR_ADAPTER_CAP_AC_MODE     0x00008000
#define JDR_ADAPTER_CAP_PC_MODE     0x00010000

/* ===== Hardware Device Types ===== */
typedef enum {
    /* SDR Hardware */
    JDR_HW_AD9361          = 0,   /* Analog Devices AD9361 (2x2 MIMO, 70MHz-6GHz) */
    JDR_HW_AD9364          = 1,   /* AD9364 (1x1, 70MHz-6GHz) */
    JDR_HW_AD9371          = 2,   /* AD9371 (4x4 MIMO, 300MHz-6GHz) */
    JDR_HW_LIME_SDR        = 3,   /* LimeSDR (LMS7002M, 100kHz-3.8GHz) */
    JDR_HW_LIME_SDR_MINI   = 4,   /* LimeSDR Mini */
    JDR_HW_HACKRF_ONE      = 5,   /* HackRF One (1MHz-6GHz) */
    JDR_HW_HACKRF_PORTAPACK = 6,  /* HackRF + PortaPack */
    JDR_HW_BLADE_RF        = 7,   /* bladeRF 2.0 micro (47MHz-6GHz) */
    JDR_HW_BLADE_RF_MICRO  = 8,
    JDR_HW_USRP_B210       = 9,   /* Ettus USRP B210 (50MHz-6GHz) */
    JDR_HW_USRP_X310       = 10,  /* USRP X310 (DC-6GHz) */
    JDR_HW_PLUTO_SDR       = 11,  /* ADALM-PLUTO (325MHz-3.8GHz) */
    JDR_HW_RTLSDR          = 12,  /* RTL-SDR (24MHz-1.7GHz) */
    JDR_HW_AIRSPY_R2       = 13,  /* Airspy R2 (24MHz-1.8GHz) */
    JDR_HW_AIRSPY_HF       = 14,  /* Airspy HF+ (9kHz-31MHz, 60-260MHz) */
    JDR_HW_SDRPLAY_RSP1A   = 15,  /* SDRplay RSP1A (1kHz-2GHz) */
    JDR_HW_SDRPLAY_RSPDX   = 16,  /* SDRplay RSPdx */
    JDR_HW_NETSDR          = 17,  /* RFspace NetSDR */
    JDR_HW_PERSEUS         = 18,  /* Microtelecom Perseus */
    JDR_HW_KIWI_SDR        = 19,  /* KiwiSDR (10kHz-30MHz) */
    JDR_HW_REDS_PITAYA     = 20,  /* Red Pitaya STEMlab */
    JDR_HW_ARDUINO_DUE     = 21,  /* Arduino Due with RF shield */
    JDR_HW_RASPBERRY_PI    = 22,  /* Raspberry Pi with RF hat */
    JDR_HW_ESP32_S2        = 23,  /* ESP32-S2 with RF */
    JDR_HW_STM32H7         = 24,  /* STM32H7 with RF frontend */
    JDR_HW_NRF52840        = 25,  /* Nordic nRF52840 (2.4GHz) */
    JDR_HW_CC1352          = 26,  /* TI CC1352 (Sub-1GHz + 2.4GHz) */
    JDR_HW_SX1280          = 27,  /* Semtech SX1280 (2.4GHz LoRa/FLRC) */
    JDR_HW_SX1276          = 28,  /* Semtech SX1276 (LoRa Sub-GHz) */
    JDR_HW_SX1262          = 29,  /* Semtech SX1262 (LoRa Sub-GHz) */
    JDR_HW_LR1121          = 30,  /* Semtech LR1121 (LoRa + GNSS) */
    JDR_HW_CC1101          = 31,  /* TI CC1101 (Sub-1GHz) */
    JDR_HW_NRF24L01        = 32,  /* Nordic nRF24L01 (2.4GHz) */
    JDR_HW_SI4463          = 33,  /* Silicon Labs Si4463 */
    JDR_HW_SI4468          = 34,  /* Silicon Labs Si4468 */
    JDR_HW_RFM95           = 35,  /* HopeRF RFM95 (LoRa) */
    JDR_HW_RFM69           = 36,  /* HopeRF RFM69 (Sub-GHz) */
    JDR_HW_CC2500          = 37,  /* TI CC2500 (2.4GHz) */
    JDR_HW_CC2530          = 38,  /* TI CC2530 (Zigbee) */
    JDR_HW_CC2652          = 39,  /* TI CC2652 (Thread/Zigbee) */
    JDR_HW_EFR32MG21       = 40,  /* Silicon Labs EFR32MG21 */
    JDR_HW_EFR32BG22       = 41,  /* Silicon Labs EFR32BG22 */
    JDR_HW_NRF9160         = 42,  /* Nordic nRF9160 (LTE-M/NB-IoT) */
    JDR_HW_BG96            = 43,  /* Quectel BG96 (LTE-M/NB-IoT) */
    JDR_HW_SIM7600         = 44,  /* SIMCom SIM7600 (LTE Cat 4) */
    JDR_HW_UBLOX_SARA_R4   = 45,  /* u-blox SARA-R4 (LTE-M/NB-IoT) */
    JDR_HW_TELIT_ME910     = 46,  /* Telit ME910 (LTE Cat M1) */
    JDR_HW_UBLOX_SARA_N2   = 47,  /* u-blox SARA-N2 (NB-IoT) */
    JDR_HW_SIM7020         = 48,  /* SIMCom SIM7020 (NB-IoT) */
    JDR_HW_MURATA_1SC      = 49,  /* Murata Type 1SC (WiFi/BT) */
    JDR_HW_ESP32_WROOM     = 50,  /* ESP32-WROOM-32 (WiFi/BT) */
    JDR_HW_ESP32_C3        = 51,  /* ESP32-C3 (WiFi/BLE 5) */
    JDR_HW_ESP32_S3        = 52,  /* ESP32-S3 (WiFi/BLE 5) */
    JDR_HW_CYW43439        = 53,  /* Infineon CYW43439 (WiFi 4/BT 5.1) */
    JDR_HW_CYW43455        = 54,  /* Infineon CYW43455 (WiFi 5/BT 5.2) */
    JDR_HW_CYW43012        = 55,  /* Infineon CYW43012 (WiFi 6/BT 5.2) */
    JDR_HW_CYW4373         = 56,  /* Infineon CYW4373 (WiFi 6E/BT 5.3) */
    JDR_HW_QCA6174         = 57,  /* Qualcomm QCA6174 (WiFi 5) */
    JDR_HW_QCA6390         = 58,  /* Qualcomm QCA6390 (WiFi 6) */
    JDR_HW_QCA6490         = 59,  /* Qualcomm QCA6490 (WiFi 6E) */
    JDR_HW_QCA6750         = 60,  /* Qualcomm QCA6750 (WiFi 7) */
    JDR_HW_MEDIATEK_MT7921 = 61,  /* MediaTek MT7921 (WiFi 6) */
    JDR_HW_MEDIATEK_MT7922 = 62,  /* MediaTek MT7922 (WiFi 6E) */
    JDR_HW_MEDIATEK_MT7925 = 63,  /* MediaTek MT7925 (WiFi 7) */
    JDR_HW_INTEL_AX210     = 63,  /* Intel AX210 (WiFi 6E/BT 5.3) */
    JDR_HW_INTEL_BE200     = 64,  /* Intel BE200 (WiFi 7/BT 5.4) */
    JDR_HW_REALTEK_8852C   = 65,  /* Realtek RTL8852C (WiFi 6) */
    JDR_HW_REALTEK_8852B   = 66,  /* Realtek RTL8852B (WiFi 6) */
    JDR_HW_REALTEK_8852A   = 67,  /* Realtek RTL8852A (WiFi 6) */
    JDR_HW_BROADCOM_BCM4375 = 68, /* Broadcom BCM4375 (WiFi 6E) */
    JDR_HW_BROADCOM_BCM4389 = 69, /* Broadcom BCM4389 (WiFi 6E) */
    JDR_HW_BROADCOM_BCM4398 = 70, /* Broadcom BCM4398 (WiFi 7) */
    JDR_HW_NXP_88W8997     = 71,  /* NXP 88W8997 (WiFi 5) */
    JDR_HW_NXP_88W9098     = 72,  /* NXP 88W9098 (WiFi 6) */
    JDR_HW_NXP_IW612       = 73,  /* NXP IW612 (WiFi 6E/BT 5.3) */
    JDR_HW_NXP_IW622       = 74,  /* NXP IW622 (WiFi 7) */
    JDR_HW_MARVELL_88W8987 = 75,  /* Marvell 88W8987 (WiFi 5) */
    JDR_HW_MARVELL_88W9098 = 76,  /* Marvell 88W9098 (WiFi 6) */
    JDR_HW_MARVELL_88W9198 = 77,  /* Marvell 88W9198 (WiFi 7) */
    JDR_HW_MAX             = 78
} jdr_hw_device_t;

/* ===== Band Support Matrix ===== */
typedef struct {
    jdr_band_t band;
    bool supported;
    uint64_t min_freq_hz;
    uint64_t max_freq_hz;
    int32_t max_power_dbm;
    uint32_t max_bandwidth_hz;
    uint32_t capabilities;
} jdr_band_support_t;

/* ===== Modulation Support Matrix ===== */
typedef struct {
    jdr_modulation_t modulation;
    bool supported;
    uint32_t capabilities;
    uint32_t min_snr_db;
    uint32_t max_spectral_efficiency;
} jdr_modulation_support_t;

/* ===== Execution Mode Support ===== */
typedef struct {
    jdr_exec_mode_t mode;
    bool supported;
    uint32_t capabilities;
} jdr_exec_mode_support_t;

/* ===== Firmware Adapter Interface ===== */
typedef struct jdr_firmware_adapter {
    /* Device identification */
    jdr_hw_device_t device_type;
    const char *device_name;
    const char *firmware_version;
    uint32_t capabilities;
    
    /* Band support (22 bands) */
    jdr_band_support_t band_support[JDR_BAND_MAX];
    
    /* Modulation support (22 modulations) */
    jdr_modulation_support_t mod_support[JDR_MOD_MAX];
    
    /* Execution mode support (3 modes) */
    jdr_exec_mode_support_t exec_support[3];
    
    /* Hardware-specific register map */
    struct {
        uint32_t base_address;
        uint32_t reg_frequency;
        uint32_t reg_bandwidth;
        uint32_t reg_power;
        uint32_t reg_sample_rate;
        uint32_t reg_band;
        uint32_t reg_modulation;
        uint32_t reg_exec_mode;
        uint32_t reg_hum_freq;
        uint32_t reg_hum_amplitude;
        uint32_t reg_hum_phase;
        uint32_t reg_harmonic_n;
        uint32_t reg_harmonics[16];
        uint32_t reg_dma_tx_base;
        uint32_t reg_dma_rx_base;
        uint32_t reg_dma_tx_head;
        uint32_t reg_dma_tx_tail;
        uint32_t reg_dma_rx_head;
        uint32_t reg_dma_rx_tail;
        uint32_t reg_irq_status;
        uint32_t reg_irq_mask;
        uint32_t reg_irq_clear;
        uint32_t reg_carrier_lock;
        uint32_t reg_rx_ready;
        uint32_t reg_tx_done;
        uint32_t reg_harmonic_resonance;
        uint32_t reg_coverage_breach;
        uint32_t reg_frequency_hop;
        uint32_t reg_fhss_table[64];
        uint32_t reg_fhss_index;
        uint32_t reg_fhss_rate;
    } regs;
    
    /* DMA configuration */
    struct {
        uint32_t tx_buffer_phys;
        uint32_t rx_buffer_phys;
        uint32_t tx_buffer_size;
        uint32_t rx_buffer_size;
        uint32_t tx_descriptor_phys;
        uint32_t rx_descriptor_phys;
        uint32_t num_tx_descriptors;
        uint32_t num_rx_descriptors;
    } dma;
    
    /* IRQ configuration */
    struct {
        uint32_t irq_number;
        uint32_t irq_priority;
        bool irq_shared;
    } irq;
    
    /* Power management */
    struct {
        uint32_t sleep_current_ua;
        uint32_t active_current_ma;
        uint32_t tx_current_ma;
        uint32_t rx_current_ma;
        bool supports_sleep;
        bool supports_deep_sleep;
    } power;
    
    /* M5 coordinates for this adapter */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* ===== Adapter Operations ===== */
    
    /* Initialize hardware */
    int32_t (*init)(struct jdr_firmware_adapter *adapter, jdr_transceiver_t *tc);
    
    /* Deinitialize hardware */
    int32_t (*deinit)(struct jdr_firmware_adapter *adapter);
    
    /* Configure frequency */
    int32_t (*set_frequency)(struct jdr_firmware_adapter *adapter, uint64_t freq_hz);
    
    /* Configure bandwidth */
    int32_t (*set_bandwidth)(struct jdr_firmware_adapter *adapter, uint64_t bw_hz);
    
    /* Configure power */
    int32_t (*set_power)(struct jdr_firmware_adapter *adapter, int32_t power_dbm);
    
    /* Configure sample rate */
    int32_t (*set_sample_rate)(struct jdr_firmware_adapter *adapter, uint32_t sps);
    
    /* Configure band */
    int32_t (*set_band)(struct jdr_firmware_adapter *adapter, jdr_band_t band);
    
    /* Configure modulation */
    int32_t (*set_modulation)(struct jdr_firmware_adapter *adapter, jdr_modulation_t mod);
    
    /* Configure execution mode */
    int32_t (*set_exec_mode)(struct jdr_firmware_adapter *adapter, jdr_exec_mode_t mode);
    
    /* Configure harmonic hum carrier */
    int32_t (*set_hum_carrier)(struct jdr_firmware_adapter *adapter,
                                surplus_real_t freq, surplus_real_t amplitude,
                                surplus_real_t phase, uint32_t harmonic_n);
    
    /* Add harmonic */
    int32_t (*add_harmonic)(struct jdr_firmware_adapter *adapter, uint32_t order);
    
    /* Compute harmonic hum */
    int32_t (*compute_hum)(struct jdr_firmware_adapter *adapter);
    
    /* Lock carrier */
    bool (*lock_carrier)(struct jdr_firmware_adapter *adapter);
    
    /* Transmit */
    int32_t (*transmit)(struct jdr_firmware_adapter *adapter,
                        const void *data, uint32_t len);
    
    /* Receive */
    int32_t (*receive)(struct jdr_firmware_adapter *adapter,
                       void *buf, uint32_t max_len, uint32_t *out_len);
    
    /* Frequency hopping init */
    int32_t (*fhss_init)(struct jdr_firmware_adapter *adapter, uint32_t hop_rate);
    
    /* Next FHSS frequency */
    uint64_t (*fhss_next_freq)(struct jdr_firmware_adapter *adapter);
    
    /* Switch band */
    int32_t (*switch_band)(struct jdr_firmware_adapter *adapter, jdr_band_t band, uint64_t freq);
    
    /* Switch modulation */
    int32_t (*switch_modulation)(struct jdr_firmware_adapter *adapter, jdr_modulation_t mod);
    
    /* Switch execution mode */
    int32_t (*switch_exec)(struct jdr_firmware_adapter *adapter, jdr_exec_mode_t mode);
    
    /* Verify coverage */
    bool (*verify_coverage)(struct jdr_firmware_adapter *adapter);
    
    /* Allocate frequency */
    int32_t (*allocate_frequency)(struct jdr_firmware_adapter *adapter,
                                   jdr_band_t band, uint64_t freq_start, uint64_t freq_end,
                                   uint32_t node_id);
    
    /* Register node */
    int32_t (*register_node)(struct jdr_firmware_adapter *adapter, uint32_t node_id);
    
    /* Get signal strength */
    int32_t (*get_signal_strength)(struct jdr_firmware_adapter *adapter);
    
    /* Get SNR */
    int32_t (*get_snr)(struct jdr_firmware_adapter *adapter);
    
    /* Get BER */
    int32_t (*get_ber)(struct jdr_firmware_adapter *adapter);
    
    /* Enter sleep mode */
    int32_t (*sleep)(struct jdr_firmware_adapter *adapter);
    
    /* Wake from sleep */
    int32_t (*wake)(struct jdr_firmware_adapter *adapter);
    
    /* Self-test */
    int32_t (*self_test)(struct jdr_firmware_adapter *adapter);
    
    /* Firmware update */
    int32_t (*firmware_update)(struct jdr_firmware_adapter *adapter,
                                const uint8_t *firmware, uint32_t len);
    
    /* Get register */
    uint32_t (*reg_read)(struct jdr_firmware_adapter *adapter, uint32_t reg);
    
    /* Set register */
    int32_t (*reg_write)(struct jdr_firmware_adapter *adapter, uint32_t reg, uint32_t val);
    
    /* Dump registers for debugging */
    int32_t (*reg_dump)(struct jdr_firmware_adapter *adapter, char *buf, uint32_t max);
    
} jdr_firmware_adapter_t;

/* ===== Adapter Registry ===== */
#define JDR_MAX_ADAPTERS 16

typedef struct {
    jdr_firmware_adapter_t *adapters[JDR_MAX_ADAPTERS];
    uint32_t num_adapters;
    jdr_firmware_adapter_t *default_adapter;
} jdr_adapter_registry_t;

/* ===== API ===== */

void jdr_adapter_registry_init(jdr_adapter_registry_t *reg);

/* Register a firmware adapter */
int32_t jdr_adapter_register(jdr_adapter_registry_t *reg, jdr_firmware_adapter_t *adapter);

/* Find adapter by device type */
jdr_firmware_adapter_t *jdr_adapter_find_by_device(jdr_adapter_registry_t *reg, jdr_hw_device_t device);

/* Find best adapter for band/modulation/exec_mode */
jdr_firmware_adapter_t *jdr_adapter_find_best(jdr_adapter_registry_t *reg,
                                               jdr_band_t band,
                                               jdr_modulation_t mod,
                                               jdr_exec_mode_t exec);

/* Auto-detect hardware and load appropriate adapter */
int32_t jdr_adapter_auto_detect(jdr_adapter_registry_t *reg);

/* Initialize all registered adapters */
int32_t jdr_adapters_init_all(jdr_adapter_registry_t *reg, jdr_network_t *net);

/* Deinitialize all adapters */
int32_t jdr_adapters_deinit_all(jdr_adapter_registry_t *reg);

/* Verify all adapters' coverage */
bool jdr_adapters_verify_all_coverage(jdr_adapter_registry_t *reg);

/* Run self-test on all adapters */
int32_t jdr_adapters_self_test_all(jdr_adapter_registry_t *reg);

/* Dump all adapter info */
int32_t jdr_adapters_dump_all(jdr_adapter_registry_t *reg, char *buf, uint32_t max);

/* ===== Pre-defined Adapter Constructors ===== */

/* AD9361/AD9364/AD9371 */
jdr_firmware_adapter_t *jdr_adapter_create_ad9361(void);
jdr_firmware_adapter_t *jdr_adapter_create_ad9364(void);
jdr_firmware_adapter_t *jdr_adapter_create_ad9371(void);

/* LimeSDR */
jdr_firmware_adapter_t *jdr_adapter_create_lime_sdr(void);
jdr_firmware_adapter_t *jdr_adapter_create_lime_sdr_mini(void);

/* HackRF */
jdr_firmware_adapter_t *jdr_adapter_create_hackrf_one(void);
jdr_firmware_adapter_t *jdr_adapter_create_hackrf_portapack(void);

/* bladeRF */
jdr_firmware_adapter_t *jdr_adapter_create_blade_rf(void);
jdr_firmware_adapter_t *jdr_adapter_create_blade_rf_micro(void);

/* USRP */
jdr_firmware_adapter_t *jdr_adapter_create_usrp_b210(void);
jdr_firmware_adapter_t *jdr_adapter_create_usrp_x310(void);

/* PlutoSDR */
jdr_firmware_adapter_t *jdr_adapter_create_pluto_sdr(void);

/* RTL-SDR / Airspy */
jdr_firmware_adapter_t *jdr_adapter_create_rtl_sdr(void);
jdr_firmware_adapter_t *jdr_adapter_create_airspy_r2(void);
jdr_firmware_adapter_t *jdr_adapter_create_airspy_hf(void);

/* SDRplay */
jdr_firmware_adapter_t *jdr_adapter_create_sdrplay_rsp1a(void);
jdr_firmware_adapter_t *jdr_adapter_create_sdrplay_rspdx(void);

/* LoRa / Sub-GHz */
jdr_firmware_adapter_t *jdr_adapter_create_sx1276(void);
jdr_firmware_adapter_t *jdr_adapter_create_sx1262(void);
jdr_firmware_adapter_t *jdr_adapter_create_lr1121(void);
jdr_firmware_adapter_t *jdr_adapter_create_cc1101(void);
jdr_firmware_adapter_t *jdr_adapter_create_nrf24l01(void);
jdr_firmware_adapter_t *jdr_adapter_create_si4463(void);
jdr_firmware_adapter_t *jdr_adapter_create_rfm95(void);
jdr_firmware_adapter_t *jdr_adapter_create_rfm69(void);

/* Cellular / LTE-M / NB-IoT */
jdr_firmware_adapter_t *jdr_adapter_create_nrf9160(void);
jdr_firmware_adapter_t *jdr_adapter_create_bg96(void);
jdr_firmware_adapter_t *jdr_adapter_create_sim7600(void);
jdr_firmware_adapter_t *jdr_adapter_create_sara_r4(void);
jdr_firmware_adapter_t *jdr_adapter_create_telit_me910(void);
jdr_firmware_adapter_t *jdr_adapter_create_sara_n2(void);
jdr_firmware_adapter_t *jdr_adapter_create_sim7020(void);

/* WiFi / BT */
jdr_firmware_adapter_t *jdr_adapter_create_esp32_wroom(void);
jdr_firmware_adapter_t *jdr_adapter_create_esp32_c3(void);
jdr_firmware_adapter_t *jdr_adapter_create_esp32_s3(void);
jdr_firmware_adapter_t *jdr_adapter_create_cyw43439(void);
jdr_firmware_adapter_t *jdr_adapter_create_cyw43455(void);
jdr_firmware_adapter_t *jdr_adapter_create_cyw43012(void);
jdr_firmware_adapter_t *jdr_adapter_create_cyw4373(void);
jdr_firmware_adapter_t *jdr_adapter_create_intel_ax210(void);
jdr_firmware_adapter_t *jdr_adapter_create_intel_be200(void);
jdr_firmware_adapter_t *jdr_adapter_create_mediatek_mt7921(void);
jdr_firmware_adapter_t *jdr_adapter_create_mediatek_mt7922(void);
jdr_firmware_adapter_t *jdr_adapter_create_mediatek_mt7925(void);
jdr_firmware_adapter_t *jdr_adapter_create_realtek_8852c(void);
jdr_firmware_adapter_t *jdr_adapter_create_qca6390(void);
jdr_firmware_adapter_t *jdr_adapter_create_qca6490(void);
jdr_firmware_adapter_t *jdr_adapter_create_qca6750(void);
jdr_firmware_adapter_t *jdr_adapter_create_broadcom_bcm4375(void);
jdr_firmware_adapter_t *jdr_adapter_create_broadcom_bcm4389(void);
jdr_firmware_adapter_t *jdr_adapter_create_broadcom_bcm4398(void);
jdr_firmware_adapter_t *jdr_adapter_create_nxp_iw612(void);
jdr_firmware_adapter_t *jdr_adapter_create_nxp_iw622(void);
jdr_firmware_adapter_t *jdr_adapter_create_marvell_88w9098(void);
jdr_firmware_adapter_t *jdr_adapter_create_marvell_88w9198(void);

/* ===== Adapter Validation ===== */

/* Verify adapter implements required operations */
bool jdr_adapter_validate(const jdr_firmware_adapter_t *adapter);

/* Verify adapter supports required band/modulation/exec_mode */
bool jdr_adapter_supports(const jdr_firmware_adapter_t *adapter,
                           jdr_band_t band, jdr_modulation_t mod, jdr_exec_mode_t exec);

/* Verify adapter's M5 coverage */
bool jdr_adapter_verify_m5_coverage(const jdr_firmware_adapter_t *adapter);

/* ===== Paraconsistent Adapter State ===== */

typedef enum {
    JDR_ADAPTER_STATE_UNKNOWN = 0,   /* LPRES: NEITHER */
    JDR_ADAPTER_STATE_READY   = 1,   /* LPRES: TRUE */
    JDR_ADAPTER_STATE_FAILED  = 2,   /* LPRES: FALSE */
    JDR_ADAPTER_STATE_CONFLICT = 3   /* LPRES: BOTH */
} jdr_adapter_state_t;

/* Get adapter state (paraconsistent) */
jdr_adapter_state_t jdr_adapter_get_state(const jdr_firmware_adapter_t *adapter);

/* Set adapter state */
void jdr_adapter_set_state(jdr_firmware_adapter_t *adapter, jdr_adapter_state_t state);

/* Safety gate: only proceed if adapter state is TRUE */
bool jdr_adapter_safety_gate(const jdr_firmware_adapter_t *adapter);

#endif /* JDR_FIRMWARE_ADAPTERS_H */
