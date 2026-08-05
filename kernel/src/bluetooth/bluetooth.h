/* bluetooth.h — ZEDEC XERO pqOS Bluetooth Driver Subsystem
 *
 * Supports: BLE 5.x, Classic, A2DP, HFP, AVRCP, HID, SPP, GATT
 * Features: Discovery, pairing, profiles, audio streaming, input devices
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3 + CC BY 4.0 + OPL v1.1
 * 36N9 Genetics, LLC
 */
#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"

/* ===== Bluetooth versions ===== */
typedef enum {
    BT_VERSION_4_0 = 0,
    BT_VERSION_4_1,
    BT_VERSION_4_2,
    BT_VERSION_5_0,
    BT_VERSION_5_1,
    BT_VERSION_5_2,
    BT_VERSION_5_3,
    BT_VERSION_5_4,
} bt_version_t;

/* ===== Device classes ===== */
typedef enum {
    BT_CLASS_UNKNOWN = 0,
    BT_CLASS_PHONE,
    BT_CLASS_COMPUTER,
    BT_CLASS_HEADSET,
    BT_CLASS_SPEAKER,
    BT_CLASS_KEYBOARD,
    BT_CLASS_MOUSE,
    BT_CLASS_GAMEPAD,
    BT_CLASS_HEALTH,
    BT_CLASS_WEARABLE,
} bt_class_t;

/* ===== Profiles ===== */
typedef enum {
    BT_PROFILE_NONE = 0,
    BT_PROFILE_A2DP_SINK = 0x01,
    BT_PROFILE_A2DP_SRC  = 0x02,
    BT_PROFILE_HFP       = 0x04,
    BT_PROFILE_HSP       = 0x08,
    BT_PROFILE_AVRCP     = 0x10,
    BT_PROFILE_HID       = 0x20,
    BT_PROFILE_SPP       = 0x40,
    BT_PROFILE_GATT      = 0x80,
    BT_PROFILE_LE_AUDIO  = 0x100,
} bt_profile_t;

/* ===== Connection states ===== */
typedef enum {
    BT_STATE_DISCONNECTED = 0,
    BT_STATE_CONNECTING,
    BT_STATE_CONNECTED,
    BT_STATE_PAIRING,
    BT_STATE_PAIRED,
} bt_state_t;

/* ===== Discovered device ===== */
typedef struct {
    uint8_t bdaddr[6];
    char name[248];
    bt_class_t dev_class;
    uint32_t supported_profiles;
    int16_t rssi;
    uint32_t cod;       /* Class of Device */
    bool paired;
    bool connected;
    bt_state_t state;
} bt_device_t;

#define BT_MAX_DEVICES       32
#define BT_MAX_CONNECTIONS   7
#define BT_MAX_GATT_SERVICES 64

/* ===== GATT service ===== */
typedef struct {
    uint16_t start_handle;
    uint16_t end_handle;
    uint8_t uuid[16];
    char name[64];
} bt_gatt_service_t;

/* ===== Bluetooth device (hardware-as-code) ===== */
typedef struct {
    uint32_t device_id;
    char name[128];
    bt_version_t version;

    /* Local device info */
    uint8_t local_addr[6];
    char local_name[248];
    bool discoverable;
    bool connectable;
    uint32_t supported_profiles;

    /* Registers */
    uint32_t reg_command;
    uint32_t reg_status;
    uint32_t reg_acl_mtu;
    uint32_t reg_sco_mtu;

    /* DMA for HCI packets */
    uint8_t tx_dma[4096];
    uint8_t rx_dma[4096];
    uint32_t tx_head, tx_tail;
    uint32_t rx_head, rx_tail;

    /* IRQ */
    bool irq_rx_ready;
    bool irq_tx_done;
    bool irq_inquiry_done;
    bool irq_connected;
    bool irq_disconnected;
    bool irq_pairing_req;

    /* Discovered devices */
    bt_device_t devices[BT_MAX_DEVICES];
    uint32_t num_devices;
    bool inquiring;

    /* Active connections */
    uint32_t connections[BT_MAX_CONNECTIONS];
    uint32_t num_connections;

    /* GATT services */
    bt_gatt_service_t gatt_services[BT_MAX_GATT_SERVICES];
    uint32_t num_gatt_services;

    /* Audio (A2DP/LE Audio) */
    bool audio_streaming;
    uint32_t audio_handle;
    uint16_t audio_codec;
    uint32_t audio_sample_rate;

    /* M5 coordinates */
    m5_coords_t m5;
    double coverage_r;
    double coverage_l;
} bluetooth_device_t;

/* ===== API ===== */
void bt_init(bluetooth_device_t *dev, const char *name);
int bt_set_discoverable(bluetooth_device_t *dev, bool on);
int bt_set_name(bluetooth_device_t *dev, const char *name);
int bt_inquiry(bluetooth_device_t *dev, uint32_t duration);
int bt_pair(bluetooth_device_t *dev, const uint8_t *bdaddr, const char *pin);
int bt_connect(bluetooth_device_t *dev, const uint8_t *bdaddr, bt_profile_t profile);
int bt_disconnect(bluetooth_device_t *dev, const uint8_t *bdaddr);
int bt_send_data(bluetooth_device_t *dev, const uint8_t *bdaddr, const void *data, uint32_t len);
int bt_recv_data(bluetooth_device_t *dev, uint8_t *bdaddr, void *data, uint32_t max_len);

/* Audio */
int bt_audio_connect(bluetooth_device_t *dev, const uint8_t *bdaddr);
int bt_audio_start(bluetooth_device_t *dev, uint16_t codec, uint32_t sample_rate);
int bt_audio_stop(bluetooth_device_t *dev);
int bt_audio_send(bluetooth_device_t *dev, const void *data, uint32_t len);

/* HID */
int bt_hid_connect(bluetooth_device_t *dev, const uint8_t *bdaddr);
int bt_hid_send_report(bluetooth_device_t *dev, const uint8_t *bdaddr, const void *report, uint32_t len);

/* Device management */
bt_device_t *bt_find_device(bluetooth_device_t *dev, const uint8_t *bdaddr);
uint32_t bt_get_device_count(bluetooth_device_t *dev);
bt_device_t *bt_get_device(bluetooth_device_t *dev, uint32_t index);

/* IRQ */
void bt_handle_irq(bluetooth_device_t *dev);

/* Coverage */
bool bt_verify_coverage(bluetooth_device_t *dev);

#endif /* BLUETOOTH_H */
