/* wifi.h — ZEDEC XERO pqOS Wi-Fi Driver Subsystem
 *
 * Supports: 802.11 a/b/g/n/ac/ax (Wi-Fi 6), WPA2/WPA3, AP/STA modes
 * Features: Scanning, roaming, mesh, hotspot, QoS, power save
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3 + CC BY 4.0 + OPL v1.1
 * 36N9 Genetics, LLC
 */
#ifndef WIFI_H
#define WIFI_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"

/* ===== Wi-Fi standards ===== */
typedef enum {
    WIFI_802_11A = 0,
    WIFI_802_11B,
    WIFI_802_11G,
    WIFI_802_11N,
    WIFI_802_11AC,
    WIFI_802_11AX,  /* Wi-Fi 6 */
    WIFI_802_11BE,  /* Wi-Fi 7 */
} wifi_standard_t;

/* ===== Security modes ===== */
typedef enum {
    WIFI_SEC_OPEN = 0,
    WIFI_SEC_WEP,
    WIFI_SEC_WPA,
    WIFI_SEC_WPA2,
    WIFI_SEC_WPA3,
    WIFI_SEC_WPA2_WPA3,
} wifi_security_t;

/* ===== Operating modes ===== */
typedef enum {
    WIFI_MODE_STATION = 0,  /* Client */
    WIFI_MODE_AP,           /* Access point */
    WIFI_MODE_ADHOC,
    WIFI_MODE_MESH,
    WIFI_MODE_MONITOR,
    WIFI_MODE_P2P,
} wifi_mode_t;

/* ===== Channel/band ===== */
typedef enum {
    WIFI_BAND_2_4GHZ = 0,
    WIFI_BAND_5GHZ,
    WIFI_BAND_6GHZ,
    WIFI_BAND_DUAL,
    WIFI_BAND_TRI,
} wifi_band_t;

/* ===== Scan result ===== */
typedef struct {
    char ssid[33];
    uint8_t bssid[6];
    int16_t rssi;          /* Signal strength dBm */
    uint32_t channel;
    wifi_standard_t standard;
    wifi_security_t security;
    wifi_band_t band;
    bool hidden;
    uint32_t max_rate_mbps;
} wifi_scan_result_t;

#define WIFI_MAX_SCAN_RESULTS  64
#define WIFI_MAX_SSID_LEN      33
#define WIFI_MAX_INTERFACES    4

/* ===== Wi-Fi interface ===== */
typedef struct {
    uint32_t iface_id;
    char name[32];
    wifi_mode_t mode;
    wifi_standard_t standard;
    wifi_band_t band;
    bool up;
    bool connected;

    /* Connection info */
    char ssid[WIFI_MAX_SSID_LEN];
    uint8_t bssid[6];
    uint32_t channel;
    int16_t rssi;
    wifi_security_t security;

    /* IP info */
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns1, dns2;

    /* Stats */
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;

    /* QoS */
    uint8_t wmm_enabled;
    uint8_t power_save_level;
} wifi_interface_t;

/* ===== Wi-Fi device (hardware-as-code) ===== */
typedef struct {
    uint32_t device_id;
    char name[128];

    /* Registers */
    uint32_t reg_command;
    uint32_t reg_status;
    uint32_t reg_channel;
    uint32_t reg_tx_power;

    /* DMA for packet TX/RX */
    uint8_t tx_dma[4096];
    uint8_t rx_dma[4096];
    uint32_t tx_head, tx_tail;
    uint32_t rx_head, rx_tail;

    /* IRQ */
    bool irq_rx_ready;
    bool irq_tx_done;
    bool irq_scan_done;
    bool irq_connected;
    bool irq_disconnected;
    bool irq_auth_failed;

    /* Interfaces */
    wifi_interface_t ifaces[WIFI_MAX_INTERFACES];
    uint32_t num_ifaces;

    /* Scan results */
    wifi_scan_result_t scan_results[WIFI_MAX_SCAN_RESULTS];
    uint32_t num_scan_results;
    bool scanning;

    /* Capabilities */
    wifi_standard_t max_standard;
    wifi_band_t max_band;
    uint32_t max_rate_mbps;
    bool supports_ap;
    bool supports_mesh;
    bool supports_monitor;
    bool supports_wpa3;
    uint8_t max_tx_power;

    /* M5 coordinates */
    m5_coords_t m5;
    double coverage_r;
    double coverage_l;
} wifi_device_t;

/* ===== API ===== */
void wifi_init(wifi_device_t *dev, const char *name);
uint32_t wifi_create_interface(wifi_device_t *dev, const char *name, wifi_mode_t mode);
int wifi_scan(wifi_device_t *dev, uint32_t iface_id);
int wifi_connect(wifi_device_t *dev, uint32_t iface_id, const char *ssid,
                 const char *password, wifi_security_t security);
int wifi_disconnect(wifi_device_t *dev, uint32_t iface_id);
int wifi_start_ap(wifi_device_t *dev, uint32_t iface_id, const char *ssid,
                  const char *password, uint32_t channel);
int wifi_stop_ap(wifi_device_t *dev, uint32_t iface_id);
int wifi_set_channel(wifi_device_t *dev, uint32_t iface_id, uint32_t channel);
int wifi_set_power_save(wifi_device_t *dev, uint32_t iface_id, uint8_t level);
int wifi_tx_packet(wifi_device_t *dev, uint32_t iface_id, const void *data, uint32_t len);
int wifi_rx_packet(wifi_device_t *dev, uint32_t iface_id, void *data, uint32_t max_len);

/* Scan results */
uint32_t wifi_get_scan_count(wifi_device_t *dev);
wifi_scan_result_t *wifi_get_scan_result(wifi_device_t *dev, uint32_t index);

/* IRQ */
void wifi_handle_irq(wifi_device_t *dev);

/* Coverage */
bool wifi_verify_coverage(wifi_device_t *dev);

#endif /* WIFI_H */
